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

#include "socket-path.h"
#include "config.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNTIME_PATH DLM_DEFAULT_RUNTIME_PATH

bool sockaddr_set_lease_server_path(struct sockaddr_un *sa,
				    const char *lease_name)
{
	int maxlen = sizeof(sa->sun_path);
	char *socket_dir = getenv("DLM_RUNTIME_PATH") ?: RUNTIME_PATH;

	int len =
	    snprintf(sa->sun_path, maxlen, "%s/%s", socket_dir, lease_name);

	if (len < 0) {
		DEBUG_LOG("Socket path creation failed: %s\n", strerror(errno));
		return false;
	}
	if (len >= maxlen) {
		errno = ENAMETOOLONG;
		DEBUG_LOG("Socket directoy path too long. "
			  "Full path to socket must be less than %d bytes\n",
			  maxlen);
		return false;
	}
	return true;
}
