/* Copyright 2022 IGEL Co., Ltd.
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <toml.h>

#define CONFIG_ERROR(x, ...) ERROR_LOG("%s: " x, filename, ##__VA_ARGS__)

static bool populate_connector_planes(struct connector_config *config,
				      toml_array_t *planes)
{
	config->nplanes = toml_array_nelem(planes);
	config->planes = calloc(config->nplanes, sizeof(uint32_t));
	if (!config->planes) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		return false;
	}

	for (int j = 0; j < config->nplanes; j++) {
		toml_datum_t plane = toml_int_at(planes, j);
		if (!plane.ok) {
			return false;
		}
		config->planes[j] = plane.u.i;
	}
	return true;
}

static bool populate_connector_config(struct lease_config *config,
				      toml_table_t *global_table,
				      toml_array_t *conns)
{
	int nconnectors = toml_array_nelem(conns);
	config->connectors = calloc(nconnectors, sizeof(*config->connectors));
	if (!config->connectors) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		return false;
	}

	config->nconnectors = nconnectors;

	for (int i = 0; i < config->nconnectors; i++) {
		struct connector_config *conn_config = &config->connectors[i];
		toml_datum_t conn = toml_string_at(conns, i);
		if (!conn.ok) {
			ERROR_LOG("Invalid connector in lease %s: idx:%d\n",
				  config->lease_name, i);
			return false;
		}
		conn_config->name = conn.u.s;

		toml_table_t *conn_config_data =
		    toml_table_in(global_table, conn.u.s);
		if (!conn_config_data)
			continue;

		toml_datum_t optional =
		    toml_bool_in(conn_config_data, "optional");
		if (optional.ok)
			config->connectors[i].optional = optional.u.b;

		toml_array_t *planes =
		    toml_array_in(conn_config_data, "planes");
		if (planes && !populate_connector_planes(conn_config, planes)) {
			ERROR_LOG("Invalid plane id for connector: %s\n",
				  conn_config->name);
			return false;
		}
	}
	return true;
}

int parse_config(char *filename, struct lease_config **parsed_config)
{
	struct lease_config *config = NULL;
	int nconfigs, i;
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
		return 0;
	}
	nconfigs = toml_array_nelem(leases);
	config = calloc(nconfigs, sizeof *config);

	if (!config) {
		DEBUG_LOG("Memory allocation failed: %s\n", strerror(errno));
		goto out;
	}

	for (i = 0; i < toml_array_nelem(leases); i++) {
		toml_table_t *lease = toml_table_at(leases, i);

		toml_datum_t name = toml_string_in(lease, "name");
		if (!name.ok) {
			CONFIG_ERROR("Invalid lease name in entry #%d\n", i);
			goto err;
		}

		config[i].lease_name = name.u.s;

		toml_array_t *conns = toml_array_in(lease, "connectors");
		if (conns &&
		    !populate_connector_config(&config[i], t_config, conns)) {
			CONFIG_ERROR("Error configuring lease: %s\n",
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
	release_config(i, config);
	goto out;
}

void release_config(int num_leases, struct lease_config *config)
{
	for (int i = 0; i < num_leases; i++) {
		struct lease_config *c = &config[i];
		free(c->lease_name);
		for (int j = 0; j < c->nconnectors; j++)
			free(c->connectors[j].name);
		free(c->connectors);
	}
	free(config);
}
