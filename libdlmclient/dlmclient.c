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
#include "log.h"
#include "socket-path.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void dlm_enable_debug_log(bool enable)
{
	dlm_log_enable_debug(enable);
}

struct dlm_lease {
	int dlm_server_sock;
	int lease_fd;
};

static bool lease_connect(struct dlm_lease *lease, const char *name)
{
	struct sockaddr_un sa = {
	    .sun_family = AF_UNIX,
	};

	if (!sockaddr_set_lease_server_path(&sa, name))
		return false;

	int dlm_server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (dlm_server_sock < 0) {
		DEBUG_LOG("Socket creation failed: %s\n", strerror(errno));
		return false;
	}

	while (connect(dlm_server_sock, (struct sockaddr *)&sa,
		       sizeof(struct sockaddr_un)) == -1) {
		if (errno == EINTR)
			continue;
		DEBUG_LOG("Cannot connect to %s: %s\n", sa.sun_path,
			  strerror(errno));
		close(dlm_server_sock);
		return false;
	}
	lease->dlm_server_sock = dlm_server_sock;
	return true;
}

static bool lease_recv_fd(struct dlm_lease *lease)
{
	char ctrl_buf[CMSG_SPACE(sizeof(int))] = {0};
	char data[1] = {0};

	struct iovec iov[1];
	iov[0].iov_base = data;
	iov[0].iov_len = sizeof(data);

	struct msghdr msg = {
	    .msg_control = ctrl_buf,
	    .msg_controllen = CMSG_SPACE(sizeof(int)),
	    .msg_iov = iov,
	    .msg_iovlen = 1,
	};

	int ret;
	while ((ret = recvmsg(lease->dlm_server_sock, &msg, 0)) <= 0) {
		if (ret == 0) {
			errno = EACCES;
			DEBUG_LOG("Request rejected by DRM lease manager\n");
			// TODO: Report why the request was rejected.
			return false;
		}
		if (errno != EINTR) {
			DEBUG_LOG("Socket data receive error: %s\n",
				  strerror(errno));
			return false;
		}
	}

	lease->lease_fd = -1;
	struct cmsghdr *cmsg;
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET &&
		    cmsg->cmsg_type == SCM_RIGHTS) {
			int nfds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
			int *fds = (int *)CMSG_DATA(cmsg);

			if (nfds == 1) {
				lease->lease_fd = fds[0];
				break;
			}

			DEBUG_LOG(
			    "Expected 1 fd from lease manager. Received %d\n",
			    nfds);
			/* Close any unexpected fds so we don't leak them. */
			for (int i = 0; i < nfds; i++)
				close(fds[i]);
			break;
		}
	}

	if (lease->lease_fd < 0) {
		DEBUG_LOG("Expected data not received from lease manager\n");
		errno = EPROTO;
		return false;
	}

	return true;
}

struct dlm_lease *dlm_get_lease(const char *name)
{
	struct dlm_lease *lease = calloc(1, sizeof(struct dlm_lease));
	if (!lease) {
		DEBUG_LOG("can't allocate memory : %s\n", strerror(errno));
		return NULL;
	}

	if (!lease_connect(lease, name)) {
		free(lease);
		return NULL;
	}

	if (!lease_recv_fd(lease)) {
		close(lease->dlm_server_sock);
		free(lease);
		return NULL;
	}

	return lease;
}

void dlm_release_lease(struct dlm_lease *lease)
{
	if (!lease)
		return;

	close(lease->lease_fd);
	close(lease->dlm_server_sock);
	free(lease);
}

int dlm_lease_fd(struct dlm_lease *lease)
{
	if (!lease)
		return -1;

	return lease->lease_fd;
}
