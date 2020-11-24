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

#include "dlmclient.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xf86drmMode.h>

static void usage(const char *name)
{
	fprintf(stderr,
		"%s <lease name>\n"
		"\tlease name: Name of lease to check\n",
		name);
}

static void dump_lease_resources(int lease_fd)
{
	drmModeObjectListPtr ol = drmModeGetLease(lease_fd);
	if (!ol) {
		fprintf(
		    stderr,
		    "drmModeGetLease failed. Received fd is not a DRM lease\n");
		return;
	}
	free(ol);

	drmModeRes *res = drmModeGetResources(lease_fd);
	if (!res) {
		fprintf(stderr, "drmModeGetResources failed\n");
		return;
	}

	for (int i = 0; i < res->count_crtcs; i++)
		printf("crtc-id: %u\n", res->crtcs[i]);

	for (int i = 0; i < res->count_connectors; i++)
		printf("connector-id: %u\n", res->connectors[i]);

	drmModeFreeResources(res);

	drmModePlaneRes *plane_res = drmModeGetPlaneResources(lease_fd);
	if (!plane_res) {
		fprintf(stderr, "drmModeGetPlaneResources failed\n");
		return;
	}

	for (uint32_t i = 0; i < plane_res->count_planes; i++)
		printf("plane-id: %u\n", plane_res->planes[i]);

	drmModeFreePlaneResources(plane_res);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	struct dlm_lease *lease = dlm_get_lease(argv[1]);
	if (!lease) {
		fprintf(stderr, "dlm_get_lease: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	int lease_fd = dlm_lease_fd(lease);
	if (lease_fd < 0) {
		fprintf(stderr, "dlm_lease_fd: %s\n", strerror(errno));
		dlm_release_lease(lease);
		return EXIT_FAILURE;
	}

	dump_lease_resources(lease_fd);
	dlm_release_lease(lease);
	return EXIT_SUCCESS;
}
