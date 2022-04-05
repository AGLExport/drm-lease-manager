/* Copyright 2021 IGEL Co., Ltd.
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
#include "log.h"
#include "toml/toml.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_ERROR(x, ...) ERROR_LOG("%s: " x, filename, ##__VA_ARGS__)

static bool populate_connector_names(struct lease_config *config,
				     toml_array_t *conns)
{
	config->cnames = toml_array_nelem(conns);
	config->connector_names = calloc(config->cnames, sizeof(char *));
	if (!config->connector_names) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		return false;
	}

	for (int i = 0; i < config->cnames; i++) {
		toml_datum_t conn = toml_string_at(conns, i);
		if (!conn.ok) {
			return false;
		}
		config->connector_names[i] = conn.u.s;
	}
	return true;
}

int parse_config(char *filename, struct lease_config **parsed_config)
{
	struct lease_config *config = NULL;
	int nconfigs = 0;
	char parse_error[160];

	FILE *fp = fopen(filename, "r");
	if (!fp)
		return 0;

	toml_table_t *t_config =
	    toml_parse_file(fp, parse_error, sizeof parse_error);
	if (!t_config) {
		CONFIG_ERROR("configuration file parse error: %s\n",
			     parse_error);
		fclose(fp);
		return 0;
	}

	toml_array_t *leases = toml_array_in(t_config, "lease");
	if (!leases) {
		CONFIG_ERROR(
		    "Invalid config - cannot find any 'lease' configs");
	}
	nconfigs = toml_array_nelem(leases);
	config = calloc(nconfigs, sizeof *config);

	if (!config) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		goto out;
	}

	for (int i = 0; i < toml_array_nelem(leases); i++) {
		toml_table_t *lease = toml_table_at(leases, i);

		toml_datum_t name = toml_string_in(lease, "name");
		if (!name.ok) {
			CONFIG_ERROR("Invalid lease name in entry #%d\n", i);
			goto err;
		}

		config[i].lease_name = name.u.s;

		toml_array_t *conns = toml_array_in(lease, "connectors");
		if (conns && !populate_connector_names(&config[i], conns)) {
			CONFIG_ERROR("Non string connector name in lease: %s\n",
				     config[i].lease_name);
			goto err;
		}
	}

	*parsed_config = config;
out:
	toml_free(t_config);
	fclose(fp);
	return nconfigs;
err:
	free(config);
	goto out;
}

void release_config(int num_leases, struct lease_config *config)
{
	for (int i = 0; i < num_leases; i++) {
		struct lease_config *c = &config[i];
		free(c->lease_name);
		for (int j = 0; j < c->cnames; j++)
			free(c->connector_names[j]);
		free(c->connector_names);
	}
	free(config);
}
