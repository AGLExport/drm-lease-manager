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

#include "test-socket-server.h"
#include <check.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "socket-path.h"
#include "test-helpers.h"

static void send_fd_list_over_socket(int socket, int nfds, int *fds)
{
	char data;
	struct iovec iov = {
	    .iov_base = &data,
	    .iov_len = 1,
	};

	int bufsize = CMSG_SPACE(nfds * sizeof(int));
	char *buf = malloc(bufsize);

	struct msghdr msg = {
	    .msg_iov = &iov,
	    .msg_iovlen = 1,
	    .msg_controllen = bufsize,
	    .msg_control = buf,
	};

	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(nfds * sizeof(int));
	memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * nfds);

	ck_assert_int_gt(sendmsg(socket, &msg, 0), 0);
	free(buf);
}

struct server_state {
	pthread_t tid;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool is_server_started;

	struct test_config *config;
};

static void *test_server_thread(void *arg)
{
	struct server_state *sstate = arg;
	struct test_config *config = sstate->config;

	struct sockaddr_un address = {
	    .sun_family = AF_UNIX,
	};

	ck_assert_int_eq(
	    sockaddr_set_lease_server_path(&address, config->lease_name), true);

	int server = socket(PF_UNIX, SOCK_STREAM, 0);
	ck_assert_int_ge(server, 0);

	unlink(address.sun_path);

	int ret;
	ret = bind(server, (struct sockaddr *)&address, sizeof(address));
	ck_assert_int_eq(ret, 0);

	ret = listen(server, 1);
	ck_assert_int_eq(ret, 0);

	sstate->is_server_started = true;
	pthread_cond_signal(&sstate->cond);

	int client = accept(server, NULL, NULL);
	/* accept is the cancellation point for this thread. If
	 * pthread_cancel() is called on this thread, accept() may return
	 * -1, so don't assert on it. */

	if (client < 0) {
		close(server);
		return NULL;
	}

	if (config->send_no_data)
		goto done;

	if (config->send_data_without_fd) {
		char data;
		write(client, &data, 1);
		goto done;
	}

	if (config->nfds == 0)
		config->nfds = 1;

	config->fds = calloc(config->nfds, sizeof(int));

	for (int i = 0; i < config->nfds; i++)
		config->fds[i] = get_dummy_fd();

	send_fd_list_over_socket(client, config->nfds, config->fds);
done:
	close(client);
	close(server);
	return NULL;
}

struct server_state *test_server_start(struct test_config *test_config)
{
	struct server_state *sstate = malloc(sizeof(*sstate));

	*sstate = (struct server_state){
	    .lock = PTHREAD_MUTEX_INITIALIZER,
	    .cond = PTHREAD_COND_INITIALIZER,
	    .is_server_started = false,
	    .config = test_config,
	};

	pthread_create(&sstate->tid, NULL, test_server_thread, sstate);

	pthread_mutex_lock(&sstate->lock);
	while (!sstate->is_server_started)
		pthread_cond_wait(&sstate->cond, &sstate->lock);
	pthread_mutex_unlock(&sstate->lock);
	return sstate;
}

void test_server_stop(struct server_state *sstate)
{

	ck_assert_ptr_ne(sstate, NULL);

	pthread_cancel(sstate->tid);
	pthread_join(sstate->tid, NULL);

	free(sstate);
}

void test_config_cleanup(struct test_config *config)
{
	if (!config->fds)
		return;

	for (int i = 0; i < config->nfds; i++)
		close(config->fds[i]);

	free(config->fds);
}
