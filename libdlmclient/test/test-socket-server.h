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

#ifndef TEST_SOCKET_SERVER_H
#define TEST_SOCKET_SERVER_H
#include <stdbool.h>

struct test_config {
	char *lease_name;
	int nfds;
	int *fds;

	bool send_data_without_fd;
	bool send_no_data;
};

void test_config_cleanup(struct test_config *config);

struct server_state *test_server_start(struct test_config *test_config);
void test_server_stop(struct server_state *sstate);
#endif
