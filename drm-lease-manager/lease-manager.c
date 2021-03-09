/* Copyright 2020-2021 IGEL Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE
#include "lease-manager.h"

#include "drm-lease.h"
#include "log.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

/* Number of resources, excluding planes, to be included in each DRM lease.
 * Each lease needs at least a CRTC and conector. */
#define DRM_LEASE_MIN_RES (2)

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))

struct lease {
	struct lease_handle base;

	bool is_granted;
	uint32_t lessee_id;
	int lease_fd;

	uint32_t *object_ids;
	int nobject_ids;
};

struct lm {
	int drm_fd;
	dev_t dev_id;

	drmModeResPtr drm_resource;
	drmModePlaneResPtr drm_plane_resource;
	uint32_t available_crtcs;

	struct lease **leases;
	int nleases;
};

static const char *const connector_type_names[] = {
    [DRM_MODE_CONNECTOR_Unknown] = "Unknown",
    [DRM_MODE_CONNECTOR_VGA] = "VGA",
    [DRM_MODE_CONNECTOR_DVII] = "DVI-I",
    [DRM_MODE_CONNECTOR_DVID] = "DVI-D",
    [DRM_MODE_CONNECTOR_DVIA] = "DVI-A",
    [DRM_MODE_CONNECTOR_Composite] = "Composite",
    [DRM_MODE_CONNECTOR_SVIDEO] = "SVIDEO",
    [DRM_MODE_CONNECTOR_LVDS] = "LVDS",
    [DRM_MODE_CONNECTOR_Component] = "Component",
    [DRM_MODE_CONNECTOR_9PinDIN] = "DIN",
    [DRM_MODE_CONNECTOR_DisplayPort] = "DP",
    [DRM_MODE_CONNECTOR_HDMIA] = "HDMI-A",
    [DRM_MODE_CONNECTOR_HDMIB] = "HDMI-B",
    [DRM_MODE_CONNECTOR_TV] = "TV",
    [DRM_MODE_CONNECTOR_eDP] = "eDP",
    [DRM_MODE_CONNECTOR_VIRTUAL] = "Virtual",
    [DRM_MODE_CONNECTOR_DSI] = "DSI",
    [DRM_MODE_CONNECTOR_DPI] = "DPI",
    [DRM_MODE_CONNECTOR_WRITEBACK] = "Writeback",
};

static char *drm_create_lease_name(struct lm *lm, drmModeConnectorPtr connector)
{
	uint32_t type = connector->connector_type;
	uint32_t id = connector->connector_type_id;

	if (type >= ARRAY_LENGTH(connector_type_names))
		type = DRM_MODE_CONNECTOR_Unknown;

	/* If the type is "Unknown", use the connector_id as the identify to
	 * guarantee that the name will be unique. */
	if (type == DRM_MODE_CONNECTOR_Unknown)
		id = connector->connector_id;

	char *name;
	if (asprintf(&name, "card%d-%s-%d", minor(lm->dev_id),
		     connector_type_names[type], id) < 0)
		return NULL;

	return name;
}

static int drm_get_encoder_crtc_index(struct lm *lm, drmModeEncoderPtr encoder)
{
	uint32_t crtc_id = encoder->crtc_id;
	if (!crtc_id)
		return -1;

	// The CRTC index only makes sense if it is less than the number of
	// bits in the encoder possible_crtcs bitmap, which is 32.
	assert(lm->drm_resource->count_crtcs < 32);

	for (int i = 0; i < lm->drm_resource->count_crtcs; i++) {
		if (lm->drm_resource->crtcs[i] == crtc_id)
			return i;
	}
	return -1;
}

static int drm_get_active_crtc_index(struct lm *lm,
				     drmModeConnectorPtr connector)
{
	drmModeEncoder *encoder =
	    drmModeGetEncoder(lm->drm_fd, connector->encoder_id);
	if (!encoder)
		return -1;

	int crtc_idx = drm_get_encoder_crtc_index(lm, encoder);
	drmModeFreeEncoder(encoder);
	return crtc_idx;
}

static int drm_get_crtc_index(struct lm *lm, drmModeConnectorPtr connector)
{

	// try the active CRTC first
	int crtc_index = drm_get_active_crtc_index(lm, connector);
	if (crtc_index != -1)
		return crtc_index;

	// If not try the first available CRTC on the connector/encoder
	for (int i = 0; i < connector->count_encoders; i++) {
		drmModeEncoder *encoder =
		    drmModeGetEncoder(lm->drm_fd, connector->encoders[i]);

		assert(encoder);

		uint32_t usable_crtcs =
		    lm->available_crtcs & encoder->possible_crtcs;
		int crtc = ffs(usable_crtcs);
		drmModeFreeEncoder(encoder);
		if (crtc == 0)
			continue;
		crtc_index = crtc - 1;
		lm->available_crtcs &= ~(1 << crtc_index);
		break;
	}
	return crtc_index;
}

static void drm_find_available_crtcs(struct lm *lm)
{
	// Assume all CRTCS are available by default,
	lm->available_crtcs = ~0;

	// then remove any that are in use. */
	for (int i = 0; i < lm->drm_resource->count_encoders; i++) {
		int enc_id = lm->drm_resource->encoders[i];
		drmModeEncoderPtr enc = drmModeGetEncoder(lm->drm_fd, enc_id);
		if (!enc)
			continue;

		int crtc_idx = drm_get_encoder_crtc_index(lm, enc);
		if (crtc_idx >= 0)
			lm->available_crtcs &= ~(1 << crtc_idx);

		drmModeFreeEncoder(enc);
	}
}

static bool lease_add_planes(struct lm *lm, struct lease *lease, int crtc_index)
{
	for (uint32_t i = 0; i < lm->drm_plane_resource->count_planes; i++) {
		uint32_t plane_id = lm->drm_plane_resource->planes[i];
		drmModePlanePtr plane = drmModeGetPlane(lm->drm_fd, plane_id);

		assert(plane);

		// Exclude planes that can be used with multiple CRTCs for now
		if (plane->possible_crtcs == (1u << crtc_index)) {
			lease->object_ids[lease->nobject_ids++] = plane_id;
		}
		drmModeFreePlane(plane);
	}
	return true;
}

static void lease_free(struct lease *lease)
{
	free(lease->base.name);
	free(lease->object_ids);
	free(lease);
}

static struct lease *lease_create(struct lm *lm, drmModeConnectorPtr connector)
{
	struct lease *lease = calloc(1, sizeof(struct lease));
	if (!lease) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		return NULL;
	}

	lease->base.name = drm_create_lease_name(lm, connector);
	if (!lease->base.name) {
		DEBUG_LOG("Can't create lease name: %s\n", strerror(errno));
		goto err;
	}

	int nobjects = lm->drm_plane_resource->count_planes + DRM_LEASE_MIN_RES;
	lease->object_ids = calloc(nobjects, sizeof(uint32_t));
	if (!lease->object_ids) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		goto err;
	}

	int crtc_index = drm_get_crtc_index(lm, connector);
	if (crtc_index < 0) {
		DEBUG_LOG("No crtc found for connector: %s\n",
			  lease->base.name);
		goto err;
	}

	if (!lease_add_planes(lm, lease, crtc_index))
		goto err;

	uint32_t crtc_id = lm->drm_resource->crtcs[crtc_index];
	lease->object_ids[lease->nobject_ids++] = crtc_id;
	lease->object_ids[lease->nobject_ids++] = connector->connector_id;

	lease->is_granted = false;

	return lease;

err:
	lease_free(lease);
	return NULL;
}

struct lm *lm_create(const char *device)
{
	struct lm *lm = calloc(1, sizeof(struct lm));
	if (!lm) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		return NULL;
	}
	lm->drm_fd = open(device, O_RDWR);
	if (lm->drm_fd < 0) {
		ERROR_LOG("Cannot open DRM device (%s): %s\n", device,
			  strerror(errno));
		goto err;
	}

	lm->drm_resource = drmModeGetResources(lm->drm_fd);
	if (!lm->drm_resource) {
		ERROR_LOG("Invalid DRM device(%s)\n", device);
		DEBUG_LOG("drmModeGetResources failed: %s\n", strerror(errno));
		goto err;
	}

	lm->drm_plane_resource = drmModeGetPlaneResources(lm->drm_fd);
	if (!lm->drm_plane_resource) {
		DEBUG_LOG("drmModeGetPlaneResources failed: %s\n",
			  strerror(errno));
		goto err;
	}

	struct stat st;
	if (fstat(lm->drm_fd, &st) < 0 || !S_ISCHR(st.st_mode)) {
		DEBUG_LOG("%s is not a valid device file\n", device);
		goto err;
	}

	lm->dev_id = st.st_rdev;

	int num_leases = lm->drm_resource->count_connectors;

	lm->leases = calloc(num_leases, sizeof(struct lease *));
	if (!lm->leases) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		goto err;
	}

	drm_find_available_crtcs(lm);

	for (int i = 0; i < num_leases; i++) {
		uint32_t connector_id = lm->drm_resource->connectors[i];
		drmModeConnectorPtr connector =
		    drmModeGetConnector(lm->drm_fd, connector_id);

		if (!connector)
			continue;

		struct lease *lease = lease_create(lm, connector);
		drmModeFreeConnector(connector);

		if (!lease)
			continue;

		lm->leases[lm->nleases] = lease;
		lm->nleases++;
	}
	if (lm->nleases == 0)
		goto err;

	return lm;

err:
	lm_destroy(lm);
	return NULL;
}

void lm_destroy(struct lm *lm)
{
	assert(lm);

	for (int i = 0; i < lm->nleases; i++) {
		lm_lease_revoke(lm, (struct lease_handle *)lm->leases[i]);
		lease_free(lm->leases[i]);
	}

	free(lm->leases);
	drmModeFreeResources(lm->drm_resource);
	drmModeFreePlaneResources(lm->drm_plane_resource);
	close(lm->drm_fd);
	free(lm);
}

int lm_get_lease_handles(struct lm *lm, struct lease_handle ***handles)
{
	assert(lm);
	assert(handles);

	*handles = (struct lease_handle **)lm->leases;
	return lm->nleases;
}

int lm_lease_grant(struct lm *lm, struct lease_handle *handle)
{
	assert(lm);
	assert(handle);

	struct lease *lease = (struct lease *)handle;
	if (lease->is_granted) {
		/* Lease is already claimed */
		return -1;
	}

	lease->lease_fd =
	    drmModeCreateLease(lm->drm_fd, lease->object_ids,
			       lease->nobject_ids, 0, &lease->lessee_id);
	if (lease->lease_fd < 0) {
		ERROR_LOG("drmModeCreateLease failed on lease %s: %s\n",
			  lease->base.name, strerror(errno));
		return -1;
	}

	lease->is_granted = true;
	return lease->lease_fd;
}

void lm_lease_revoke(struct lm *lm, struct lease_handle *handle)
{
	assert(lm);
	assert(handle);

	struct lease *lease = (struct lease *)handle;

	if (!lease->is_granted)
		return;

	drmModeRevokeLease(lm->drm_fd, lease->lessee_id);
	close(lease->lease_fd);
	lease->is_granted = false;
}
