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

#include "dlm-protocol.h"
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

	int dlm_server_sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
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

static bool lease_send_request(struct dlm_lease *lease, enum dlm_opcode opcode)
{
	struct dlm_client_request request = {
	    .opcode = opcode,
	};

	if (!send_dlm_client_request(lease->dlm_server_sock, &request)) {
		DEBUG_LOG("Socket data send error: %s\n", strerror(errno));
		return false;
	}
	return true;
}

static bool lease_recv_fd(struct dlm_lease *lease)
{
	lease->lease_fd = receive_lease_fd(lease->dlm_server_sock);

	if (lease->lease_fd < 0)
		goto err;

	return true;

err:
	switch (errno) {
	case EACCES:
		DEBUG_LOG("Lease request rejected by DRM lease manager\n");
		break;
	case EPROTO:
		DEBUG_LOG("Unexpected data received from lease manager\n");
		break;
	default:
		DEBUG_LOG("Lease manager receive data error: %s\n",
			  strerror(errno));
		break;
	}
	return false;
}

struct dlm_lease *dlm_get_lease(const char *name)
{
	int saved_errno;
	struct dlm_lease *lease = calloc(1, sizeof(struct dlm_lease));
	if (!lease) {
		DEBUG_LOG("can't allocate memory : %s\n", strerror(errno));
		return NULL;
	}

	if (!lease_connect(lease, name)) {
		free(lease);
		return NULL;
	}

	if (!lease_send_request(lease, DLM_GET_LEASE))
		goto err_request;

	if (!lease_recv_fd(lease))
		goto err;

	return lease;

err:
	saved_errno = errno;
	lease_send_request(lease, DLM_RELEASE_LEASE);
	errno = saved_errno;
err_request:
	close(lease->dlm_server_sock);
	free(lease);
	return NULL;
}

void dlm_release_lease(struct dlm_lease *lease)
{
	if (!lease)
		return;

	lease_send_request(lease, DLM_RELEASE_LEASE);
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
