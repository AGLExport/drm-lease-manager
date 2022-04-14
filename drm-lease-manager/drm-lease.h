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

#ifndef DRM_LEASE_H
#define DRM_LEASE_H
#include <stdbool.h>
#include <stdint.h>

struct lease_handle {
	char *name;
	void *user_data;
};

struct connector_config {
	char *name;
	bool optional;
};

struct lease_config {
	char *lease_name;

	int ncids;
	uint32_t *connector_ids;

	int nconnectors;
	struct connector_config *connectors;
};

#endif
