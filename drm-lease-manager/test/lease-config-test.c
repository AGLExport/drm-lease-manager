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
#include <check.h>
#include <stdio.h>
#include <stdlib.h>

static void test_setup(void)
{
}

static void test_shutdown(void)
{
}

/* parse config file test */
/* Test details: Parse a config file
 * Expected results: a config with the expected results
 */

START_TEST(parse_leases)
{
	char tmp_file[] = "/tmp/dlmconfig_tmpXXXXXX";
	int fd = mkstemp(tmp_file);

	ck_assert_int_ge(fd, 0);

	FILE *config_file = fdopen(fd, "w");
	ck_assert_ptr_ne(config_file, NULL);

	char test_data[] = "[[lease]]\n"
			   "name = \"lease 1\"\n"
			   "connectors = [\"1\", \"b\",\"gamma\" ]\n"
			   "[[lease]]\n"
			   "name = \"lease 2\"\n"
			   "connectors = [\"connector 3\"]\n";

	fwrite(test_data, 1, sizeof(test_data), config_file);
	fclose(config_file);

	struct lease_config *config = NULL;
	int nconfigs = parse_config(tmp_file, &config);

	ck_assert_int_eq(nconfigs, 2);
	ck_assert_ptr_ne(config, NULL);

	ck_assert_str_eq(config[0].lease_name, "lease 1");
	ck_assert_int_eq(config[0].cnames, 3);
	ck_assert_str_eq(config[0].connector_names[0], "1");
	ck_assert_str_eq(config[0].connector_names[1], "b");
	ck_assert_str_eq(config[0].connector_names[2], "gamma");

	ck_assert_str_eq(config[1].lease_name, "lease 2");
	ck_assert_int_eq(config[1].cnames, 1);
	ck_assert_str_eq(config[1].connector_names[0], "connector 3");

	release_config(nconfigs, config);
}
END_TEST

static void add_parse_tests(Suite *s)
{
	TCase *tc = tcase_create("Config file parsing tests");

	tcase_add_checked_fixture(tc, test_setup, test_shutdown);

	tcase_add_test(tc, parse_leases);
	suite_add_tcase(s, tc);
}

int main(void)
{
	int number_failed;
	Suite *s;
	SRunner *sr;

	s = suite_create("DLM lease manager tests");

	add_parse_tests(s);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
