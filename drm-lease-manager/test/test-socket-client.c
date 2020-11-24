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

#include "test-socket-client.h"

#include <check.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "socket-path.h"

#define DEFAULT_RECV_TIMEOUT (100) // timeout in ms to receive data from server

struct client_state {
	pthread_t tid;

	int socket_fd;
	struct test_config *config;
};

static void client_gst_socket_status(int socket_fd, struct test_config *config)
{

	config->connection_completed = true;

	struct pollfd pfd = {.fd = socket_fd, .events = POLLIN};
	if (poll(&pfd, 1, config->recv_timeout) <= 0)
		return;

	if (pfd.revents & POLLHUP)
		config->connection_completed = false;

	if (pfd.revents & POLLIN)
		config->has_data = true;

	return;
}

static int receive_fd_from_socket(int sockfd)
{
	union {
		struct cmsghdr align;
		char buf[CMSG_SPACE(sizeof(int))];
	} u;

	char data;
	struct iovec iov = {.iov_base = &data, .iov_len = sizeof(data)};
	struct msghdr msg = {
	    .msg_iov = &iov,
	    .msg_iovlen = 1,
	    .msg_control = u.buf,
	    .msg_controllen = sizeof(u.buf),
	};

	if (recvmsg(sockfd, &msg, 0) < 0)
		return -1;

	int recv_fd = -1;
	for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		ck_assert_int_eq(cmsg->cmsg_level, SOL_SOCKET);

		if (cmsg->cmsg_type != SCM_RIGHTS)
			continue;

		int nfds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
		ck_assert_int_eq(nfds, 1);
		recv_fd = *(int *)CMSG_DATA(cmsg);
	}
	return recv_fd;
}

static void *test_client_thread(void *arg)
{
	struct client_state *cstate = arg;
	struct test_config *config = cstate->config;

	struct sockaddr_un address = {
	    .sun_family = AF_UNIX,
	};

	ck_assert_int_eq(
	    sockaddr_set_lease_server_path(&address, config->lease->name),
	    true);

	int client = socket(PF_UNIX, SOCK_STREAM, 0);
	ck_assert_int_ge(client, 0);

	int ret;
	ret = connect(client, (struct sockaddr *)&address, sizeof(address));
	if (ret != 0) {
		printf("Connect failed;: %s\n", strerror(errno));
		close(client);
		return NULL;
	}

	if (!config->recv_timeout)
		config->recv_timeout = DEFAULT_RECV_TIMEOUT;

	client_gst_socket_status(client, config);

	if (config->has_data) {
		config->received_fd = receive_fd_from_socket(client);
	}

	cstate->socket_fd = client;

	return NULL;
}

struct client_state *test_client_start(struct test_config *test_config)
{
	struct client_state *cstate = malloc(sizeof(*cstate));

	*cstate = (struct client_state){
	    .config = test_config,
	};

	pthread_create(&cstate->tid, NULL, test_client_thread, cstate);

	return cstate;
}

void test_client_stop(struct client_state *cstate)
{

	ck_assert_ptr_ne(cstate, NULL);

	pthread_join(cstate->tid, NULL);

	if (cstate->socket_fd >= 0)
		close(cstate->socket_fd);

	free(cstate);
}

void test_config_cleanup(struct test_config *config)
{
	if (config->has_data && config->received_fd >= 0)
		close(config->received_fd);
}
