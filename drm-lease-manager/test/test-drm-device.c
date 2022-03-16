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

#include <check.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drmMode.h>

#include "test-drm-device.h"
#define UNUSED(x) (void)(x)

/* Set the base value for IDs of each resource type.
 * These can be adjusted if test cases need more IDs. */
#define IDS_PER_RES_TYPE 32

#define CRTC_BASE (IDS_PER_RES_TYPE)
#define CONNECTOR_BASE (CRTC_BASE + IDS_PER_RES_TYPE)
#define ENCODER_BASE (CONNECTOR_BASE + IDS_PER_RES_TYPE)
#define PLANE_BASE (ENCODER_BASE + IDS_PER_RES_TYPE)
#define LESSEE_ID_BASE (PLANE_BASE + IDS_PER_RES_TYPE)

struct drm_device test_device;

#define ALLOC_RESOURCE(res, container)                           \
	do {                                                     \
		if (res != 0) {                                  \
			test_device.container.res =              \
			    malloc(sizeof(uint32_t) * res);      \
			if (!test_device.container.res)          \
				return false;                    \
			test_device.container.count_##res = res; \
		}                                                \
	} while (0)

#define FILL_RESOURCE(res, RES, container)                     \
	for (int i = 0; i < res; i++) {                        \
		test_device.container.res[i] = RES##_BASE + i; \
	}

bool setup_drm_test_device(int crtcs, int connectors, int encoders, int planes)
{
	int lessee_ids = crtcs;
	ALLOC_RESOURCE(crtcs, resources);
	ALLOC_RESOURCE(connectors, resources);
	ALLOC_RESOURCE(encoders, resources);
	ALLOC_RESOURCE(planes, plane_resources);
	ALLOC_RESOURCE(lessee_ids, leases);

	FILL_RESOURCE(crtcs, CRTC, resources);
	FILL_RESOURCE(connectors, CONNECTOR, resources);
	FILL_RESOURCE(encoders, ENCODER, resources);
	FILL_RESOURCE(planes, PLANE, plane_resources);
	FILL_RESOURCE(lessee_ids, LESSEE_ID, leases);

	return true;
}

void reset_drm_test_device(void)
{
	free(test_device.resources.crtcs);
	free(test_device.resources.connectors);
	free(test_device.resources.encoders);
	free(test_device.plane_resources.planes);
	free(test_device.leases.lessee_ids);
	memset(&test_device, 0, sizeof(test_device));
}

void setup_test_device_layout(drmModeConnector *connectors,
			      drmModeEncoder *encoders, drmModePlane *planes)
{
	test_device.layout.connectors = connectors;
	test_device.layout.encoders = encoders;
	test_device.layout.planes = planes;
}

#define GET_DRM_RESOURCE_FN(Res, res, RES, container)                       \
	drmMode##Res##Ptr get_##res(int fd, uint32_t id)                    \
	{                                                                   \
		UNUSED(fd);                                                 \
		if (id == 0)                                                \
			return NULL;                                        \
		ck_assert_int_ge(id, RES##_BASE);                           \
		ck_assert_int_lt(                                           \
		    id, RES##_BASE + test_device.container.count_##res##s); \
		return &test_device.layout.res##s[id - RES##_BASE];         \
	}

GET_DRM_RESOURCE_FN(Connector, connector, CONNECTOR, resources)
GET_DRM_RESOURCE_FN(Encoder, encoder, ENCODER, resources)
GET_DRM_RESOURCE_FN(Plane, plane, PLANE, plane_resources)

int create_lease(int fd, const uint32_t *objects, int num_objects, int flags,
		 uint32_t *lessee_id)
{
	UNUSED(objects);
	UNUSED(num_objects);
	UNUSED(flags);

	int lease_count = test_device.leases.count;
	if (lease_count < test_device.leases.count_lessee_ids)
		*lessee_id = test_device.leases.lessee_ids[lease_count];
	else
		*lessee_id = 0;

	test_device.leases.count++;

	return dup(fd);
}
