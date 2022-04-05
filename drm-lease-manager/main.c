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

#include "lease-config.h"
#include "lease-manager.h"
#include "lease-server.h"
#include "log.h"

#include <assert.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(const char *progname)
{
	printf("Usage: %s [OPTIONS] [<DRM device>]\n\n"
	       "Options:\n"
	       "-h, --help \tPrint this help\n"
	       "-c, --config \t path to configuration file (default "
	       "/etc/drm-lease-manager.toml)\n"
	       "-v, --verbose \tEnable verbose debug messages\n"
	       "-t, --lease-transfer \tAllow lease transfter to new clients\n"
	       "-k, --keep-on-crash \tDon't close lease on client crash\n",
	       progname);
}

const char *opts = "vtkhc";
const struct option options[] = {
    {"help", no_argument, NULL, 'h'},
    {"verbose", no_argument, NULL, 'v'},
    {"lease-transfer", no_argument, NULL, 't'},
    {"keep-on-crash", no_argument, NULL, 'k'},
    {"config", required_argument, NULL, 'c'},
    {NULL, 0, NULL, 0},
};

int main(int argc, char **argv)
{
	char *device = "/dev/dri/card0";
	char *config_file = "/etc/drm-lease-manager.toml";

	bool debug_log = false;
	bool can_transfer_leases = false;
	bool keep_on_crash = false;

	int c;
	while ((c = getopt_long(argc, argv, opts, options, NULL)) != -1) {
		int ret = EXIT_FAILURE;
		switch (c) {
		case 'v':
			debug_log = true;
			break;
		case 't':
			can_transfer_leases = true;
			break;
		case 'k':
			keep_on_crash = true;
			break;
		case 'c':
			config_file = optarg;
			break;
		case 'h':
			ret = EXIT_SUCCESS;
			/* fall through */
		default:
			usage(argv[0]);
			return ret;
		}
	}

	if (optind < argc)
		device = argv[optind];

	dlm_log_enable_debug(debug_log);

	struct lease_config *lease_configs = NULL;
	int num_configs = parse_config(config_file, &lease_configs);

	struct lm *lm =
	    lm_create_with_config(device, num_configs, lease_configs);

	if (!lm) {
		ERROR_LOG("DRM Lease initialization failed\n");
		return EXIT_FAILURE;
	}

	struct lease_handle **lease_handles = NULL;
	int count_ids = lm_get_lease_handles(lm, &lease_handles);
	assert(count_ids > 0);

	struct ls *ls = ls_create(lease_handles, count_ids);
	if (!ls) {
		lm_destroy(lm);
		ERROR_LOG("Client socket initialization failed\n");
		return EXIT_FAILURE;
	}

	struct ls_req req;
	while (ls_get_request(ls, &req)) {
		switch (req.type) {
		case LS_REQ_GET_LEASE: {
			int fd = lm_lease_grant(lm, req.lease_handle);

			if (fd < 0 && can_transfer_leases)
				fd = lm_lease_transfer(lm, req.lease_handle);

			if (fd < 0) {
				ERROR_LOG(
				    "Can't fulfill lease request: lease=%s\n",
				    req.lease_handle->name);
				ls_disconnect_client(ls, req.client);
				break;
			}

			struct ls_client *active_client =
			    req.lease_handle->user_data;
			if (active_client)
				ls_disconnect_client(ls, active_client);

			req.lease_handle->user_data = req.client;

			if (!ls_send_fd(ls, req.client, fd)) {
				ERROR_LOG(
				    "Client communication error: lease=%s\n",
				    req.lease_handle->name);
				ls_disconnect_client(ls, req.client);
				lm_lease_revoke(lm, req.lease_handle);
			}
			break;
		}
		case LS_REQ_RELEASE_LEASE:
		case LS_REQ_CLIENT_DISCONNECT:
			ls_disconnect_client(ls, req.client);
			req.lease_handle->user_data = NULL;
			lm_lease_revoke(lm, req.lease_handle);

			if (!keep_on_crash || req.type == LS_REQ_RELEASE_LEASE)
				lm_lease_close(req.lease_handle);

			break;
		default:
			ERROR_LOG("Internal error: Invalid lease request\n");
			goto done;
		}
	}
done:
	ls_destroy(ls);
	lm_destroy(lm);
	release_config(num_configs, lease_configs);
	return EXIT_FAILURE;
}
