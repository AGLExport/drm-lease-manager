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

#ifndef TEST_DRM_DEVICE_H
#define TEST_DRM_DEVICE_H

#include <xf86drmMode.h>

/* TEST_DRM_DEVICE can be the path to any
 * file that can be opened.
 */
#define TEST_DRM_DEVICE "/dev/null"

struct drm_device {
	drmModeRes resources;
	drmModePlaneRes plane_resources;
	struct {
		drmModeConnector *connectors;
		drmModeEncoder *encoders;
		drmModePlane *planes;
	} layout;

	struct {
		int count;
		uint32_t *lessee_ids;
		int count_lessee_ids;
	} leases;
};

extern struct drm_device test_device;

bool setup_drm_test_device(int crtcs, int connectors, int encoders, int planes);
void setup_test_device_layout(drmModeConnector *connectors,
			      drmModeEncoder *encoders, drmModePlane *planes);
void reset_drm_test_device(void);

drmModeConnectorPtr get_connector(int fd, uint32_t id);
drmModeEncoderPtr get_encoder(int fd, uint32_t id);
drmModePlanePtr get_plane(int fd, uint32_t id);
int create_lease(int fd, const uint32_t *objects, int num_objects, int flags,
		 uint32_t *lessee_id);

#define TEST_DEVICE_RESOURCES (&test_device.resources)
#define TEST_DEVICE_PLANE_RESOURCES (&test_device.plane_resources)

#define CRTC_ID(x) (test_device.resources.crtcs[x])
#define CONNECTOR_ID(x) (test_device.resources.connectors[x])
#define ENCODER_ID(x) (test_device.resources.encoders[x])
#define PLANE_ID(x) (test_device.plane_resources.planes[x])
#define LESSEE_ID(x) (test_device.leases.lessee_ids[x])

#define CONNECTOR(cid, eid, encs, enc_cnt)                   \
	{                                                    \
		.connector_id = cid, .encoder_id = eid,      \
		.count_encoders = enc_cnt, .encoders = encs, \
	}

#define ENCODER(eid, crtc, crtc_mask)               \
	{                                           \
		.encoder_id = eid, .crtc_id = crtc, \
		.possible_crtcs = crtc_mask,        \
	}

#define PLANE(pid, crtc_mask)                                 \
	{                                                     \
		.plane_id = pid, .possible_crtcs = crtc_mask, \
	}

#endif
