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

#ifndef TEST_SOCKET_CLIENT_H
#define TEST_SOCKET_CLIENT_H
#include <stdbool.h>

#include "drm-lease.h"
struct test_config {
	// settings
	struct lease_handle *lease;
	int recv_timeout;

	// outputs
	int received_fd;
	bool has_data;
	bool connection_completed;
};

void test_config_cleanup(struct test_config *config);

struct client_state;
struct client_state *test_client_start(struct test_config *test_config);
void test_client_stop(struct client_state *cstate);
#endif
