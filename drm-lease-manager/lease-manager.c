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
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

/* Number of resources, to be included in a DRM lease for each connector.
 * Each connector needs both a CRTC and conector object:. */
#define DRM_OBJECTS_PER_CONNECTOR (2)

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))

struct lease {
	struct lease_handle base;

	bool is_granted;
	uint32_t lessee_id;
	int lease_fd;

	uint32_t *object_ids;
	int nobject_ids;

	/* for lease transfer completion */
	uint32_t crtc_id;
	pthread_t transition_tid;
	bool transition_running;
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

static char *drm_create_default_lease_name(struct lm *lm,
					   drmModeConnectorPtr connector)
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

/* Lease transition
 * Wait for a client to update the DRM framebuffer on the CRTC managed by
 * a lease.  Once the framebuffer has been updated, it is safe to close
 * the fd associated with the previous lease client, freeing the previous
 * framebuffer if there are no other references to it. */
static void wait_for_fb_update(struct lease *lease, uint32_t old_fb)
{
	uint32_t current_fb = old_fb;

	struct pollfd drm_poll = {
	    .fd = lease->lease_fd,
	    .events = POLLIN,
	};

	while (current_fb == old_fb) {
		drmModeCrtcPtr crtc;
		if (poll(&drm_poll, 1, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		crtc = drmModeGetCrtc(lease->lease_fd, lease->crtc_id);
		current_fb = crtc->buffer_id;
		drmModeFreeCrtc(crtc);
	}
}

struct transition_ctx {
	struct lease *lease;
	int close_fd;
	uint32_t old_fb;
};

static void transition_done(void *arg)
{
	struct transition_ctx *ctx = arg;
	close(ctx->close_fd);
	free(ctx);
}

static void *finish_transition_task(void *arg)
{
	struct transition_ctx *ctx = arg;
	pthread_cleanup_push(transition_done, ctx);
	wait_for_fb_update(ctx->lease, ctx->old_fb);
	pthread_cleanup_pop(true);
	return NULL;
}

static void close_after_lease_transition(struct lease *lease, int close_fd)
{
	struct transition_ctx *ctx = calloc(1, sizeof(*ctx));

	assert(ctx);

	drmModeCrtcPtr crtc = drmModeGetCrtc(lease->lease_fd, lease->crtc_id);

	ctx->lease = lease;
	ctx->close_fd = close_fd;
	ctx->old_fb = crtc->buffer_id;

	drmModeFreeCrtc(crtc);

	int ret = pthread_create(&lease->transition_tid, NULL,
				 finish_transition_task, ctx);

	lease->transition_running = (ret == 0);
}

static void cancel_lease_transition_thread(struct lease *lease)
{

	if (lease->transition_running) {
		pthread_cancel(lease->transition_tid);
		pthread_join(lease->transition_tid, NULL);
	}

	lease->transition_running = false;
}

static void lease_free(struct lease *lease)
{
	free(lease->base.name);
	free(lease->object_ids);
	free(lease);
}

static struct lease *lease_create(struct lm *lm,
				  const struct lease_config *config)
{
	struct lease *lease;

	if (!config->lease_name) {
		ERROR_LOG("Mising lease name\n");
		return NULL;
	}

	lease = calloc(1, sizeof(struct lease));
	if (!lease) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		return NULL;
	}

	lease->base.name = strdup(config->lease_name);
	if (!lease->base.name) {
		DEBUG_LOG("Can't create lease name: %s\n", strerror(errno));
		goto err;
	}

	int nobjects = lm->drm_plane_resource->count_planes +
		       config->ncids * DRM_OBJECTS_PER_CONNECTOR;

	lease->object_ids = calloc(nobjects, sizeof(uint32_t));
	if (!lease->object_ids) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		goto err;
	}

	for (int i = 0; i < config->ncids; i++) {
		drmModeConnectorPtr connector =
		    drmModeGetConnector(lm->drm_fd, config->connector_ids[i]);

		if (connector == NULL) {
			ERROR_LOG("Can't find connector id: %d\n",
				  config->connector_ids);
			goto err;
		}

		uint32_t connector_id = connector->connector_id;

		int crtc_index = drm_get_crtc_index(lm, connector);

		drmModeFreeConnector(connector);

		if (crtc_index < 0) {
			DEBUG_LOG("No crtc found for connector: %d, lease %s\n",
				  connector_id, lease->base.name);
			goto err;
		}

		if (!lease_add_planes(lm, lease, crtc_index))
			goto err;

		uint32_t crtc_id = lm->drm_resource->crtcs[crtc_index];
		lease->crtc_id = crtc_id;
		lease->object_ids[lease->nobject_ids++] = crtc_id;
		lease->object_ids[lease->nobject_ids++] = connector_id;
	}
	lease->is_granted = false;
	lease->lease_fd = -1;

	return lease;

err:
	lease_free(lease);
	return NULL;
}

static void destroy_default_lease_configs(int num_configs,
					  struct lease_config *configs)
{
	for (int i = 0; i < num_configs; i++) {
		free(configs[i].connector_ids);
		free(configs[i].lease_name);
	}

	free(configs);
}

static int create_default_lease_configs(struct lm *lm,
					struct lease_config **configs)
{
	struct lease_config *def_configs;
	int num_configs = lm->drm_resource->count_connectors;

	if (num_configs < 0)
		return -1;

	def_configs = calloc(num_configs, sizeof(*def_configs));
	if (!def_configs) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		return -1;
	}

	for (int i = 0; i < num_configs; i++) {
		uint32_t cid = lm->drm_resource->connectors[i];

		def_configs[i].connector_ids = malloc(sizeof(uint32_t));
		if (!def_configs[i].connector_ids) {
			DEBUG_LOG("Memory allocation failed: %s\n",
				  strerror(errno));
			goto err;
		}

		drmModeConnectorPtr connector;
		connector = drmModeGetConnector(lm->drm_fd, cid);
		def_configs[i].lease_name =
		    drm_create_default_lease_name(lm, connector);

		if (!def_configs[i].lease_name) {
			DEBUG_LOG(
			    "Can't create lease name for connector %d: %s\n",
			    cid, strerror(errno));
			goto err;
		}

		drmModeFreeConnector(connector);

		def_configs[i].connector_ids[0] = cid;
		def_configs[i].ncids = 1;
	}

	*configs = def_configs;
	return num_configs;

err:
	destroy_default_lease_configs(num_configs, def_configs);
	return -1;
}

static struct lm *drm_device_get_resources(const char *device)
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

	return lm;
err:
	lm_destroy(lm);
	return NULL;
}

static int lm_create_leases(struct lm *lm, int num_leases,
			    const struct lease_config *configs)
{
	lm->leases = calloc(num_leases, sizeof(struct lease *));
	if (!lm->leases) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		return -1;
	}

	drm_find_available_crtcs(lm);

	for (int i = 0; i < num_leases; i++) {
		struct lease *lease = lease_create(lm, &configs[i]);
		if (!lease)
			continue;

		lm->leases[lm->nleases] = lease;
		lm->nleases++;
	}
	if (lm->nleases == 0)
		return -1;

	return 0;
}

struct lm *lm_create_with_config(const char *device, int num_leases,
				 struct lease_config *configs)
{
	struct lease_config *default_configs = NULL;
	struct lm *lm = drm_device_get_resources(device);

	if (!lm)
		return NULL;

	if (configs == NULL) {
		num_leases = create_default_lease_configs(lm, &default_configs);
		if (num_leases < 0) {
			lm_destroy(lm);
			ERROR_LOG("DRM connector enumeration failed\n");
			return NULL;
		}
		configs = default_configs;
	}

	if (lm_create_leases(lm, num_leases, configs) < 0) {
		lm_destroy(lm);
		lm = NULL;
	}

	if (default_configs)
		destroy_default_lease_configs(num_leases, default_configs);
	return lm;
}

struct lm *lm_create(const char *device)
{
	return lm_create_with_config(device, 0, NULL);
}

void lm_destroy(struct lm *lm)
{
	assert(lm);

	for (int i = 0; i < lm->nleases; i++) {
		struct lease_handle *lease_handle = &lm->leases[i]->base;
		lm_lease_revoke(lm, lease_handle);
		lm_lease_close(lease_handle);
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

	int lease_fd =
	    drmModeCreateLease(lm->drm_fd, lease->object_ids,
			       lease->nobject_ids, 0, &lease->lessee_id);
	if (lease_fd < 0) {
		ERROR_LOG("drmModeCreateLease failed on lease %s: %s\n",
			  lease->base.name, strerror(errno));
		return -1;
	}

	lease->is_granted = true;

	int old_lease_fd = lease->lease_fd;
	lease->lease_fd = lease_fd;

	if (old_lease_fd >= 0)
		close_after_lease_transition(lease, old_lease_fd);

	return lease_fd;
}

int lm_lease_transfer(struct lm *lm, struct lease_handle *handle)
{
	assert(lm);
	assert(handle);

	struct lease *lease = (struct lease *)handle;
	if (!lease->is_granted)
		return -1;

	lm_lease_revoke(lm, handle);
	if (lm_lease_grant(lm, handle) < 0) {
		lm_lease_close(handle);
		return -1;
	}

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
	cancel_lease_transition_thread(lease);
	lease->is_granted = false;
}

void lm_lease_close(struct lease_handle *handle)
{
	assert(handle);

	struct lease *lease = (struct lease *)handle;
	if (lease->lease_fd >= 0)
		close(lease->lease_fd);
	lease->lease_fd = -1;
}
