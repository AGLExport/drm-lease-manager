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
#include <stdio.h>
#include <stdlib.h>

#define CONFIG_ERROR(x, ...) ERROR_LOG("%s: " x, filename, ##__VA_ARGS__)

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
		return 0;
	}

	toml_array_t *leases = toml_array_in(t_config, "lease");
	if (!leases) {
		CONFIG_ERROR(
		    "Invalid config - cannot find any 'lease' configs");
	}
	nconfigs = toml_array_nelem(leases);
	config = calloc(nconfigs, sizeof *config);

	for (int i = 0; i < toml_array_nelem(leases); i++) {
		toml_table_t *lease = toml_table_at(leases, i);

		toml_datum_t name = toml_string_in(lease, "name");
		if (!name.ok) {
			CONFIG_ERROR("Invalid lease name in entry #%d\n", i);
			continue;
		}

		config[i].lease_name = name.u.s;

		toml_array_t *conns = toml_array_in(lease, "connectors");
		if (conns) {
			config[i].cnames = toml_array_nelem(conns);
			config[i].connector_names =
			    calloc(config[i].cnames, sizeof(char *));
			for (int j = 0; j < config[i].cnames; j++) {
				toml_datum_t conn = toml_string_at(conns, j);
				if (!conn.ok) {
					CONFIG_ERROR("Non string connector "
						     "name in lease: %s\n",
						     config[i].lease_name);
					goto out;
				}
				config[i].connector_names[j] = conn.u.s;
			}
		}
	}

	toml_free(t_config);
	*parsed_config = config;
	return nconfigs;
out:
	free(config);
	return 0;
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
