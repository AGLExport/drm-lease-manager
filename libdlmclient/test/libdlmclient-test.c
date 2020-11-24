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

#include <check.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "config.h"
#include "dlmclient.h"
#include "test-helpers.h"
#include "test-socket-server.h"

#define SOCKETDIR "/tmp"

#define TEST_LEASE_NAME "test-lease"

/**************  Test fixutre functions *************/
struct test_config default_test_config;

static void test_setup(void)
{
	dlm_enable_debug_log(true);
	setenv("DLM_RUNTIME_PATH", SOCKETDIR, 1);

	default_test_config = (struct test_config){
	    .lease_name = TEST_LEASE_NAME,
	    .nfds = 1,
	};
}

static void test_shutdown(void)
{
	test_config_cleanup(&default_test_config);
}

/**************  Lease manager error tests *************/

/* These tests verify that the client library gracefully handles
 * failures when trying to receive data from the lease manager.
 * Failures or errors in the lease manager should cause meaningful
 * errors to be reported by the client.  Lease manager errors should
 * not cause crashes or invalid state in the client */

/* manager_connection_err
 *
 * Test details: Simulate socket connection failure.
 * Expected results: dlm_get_lease() fails.
 */
START_TEST(manager_connection_err)
{
	struct server_state *sstate = test_server_start(&default_test_config);

	struct dlm_lease *lease = dlm_get_lease(TEST_LEASE_NAME "-bad");

	ck_assert_ptr_eq(lease, NULL);

	test_server_stop(sstate);
}
END_TEST

/* no_data_from_manager
 *
 * Test details: Close the remote (lease manager) without sending any data.
 *               Currently this means that the lease request has been rejected
 *               for some reason.
 *
 * TODO: Update this when the client-server protocol is updated to
 *       include the reason for the lease rejection.
 *
 * Expected results: dlm_get_lease() fails, errno set to EACCESS.
 */
START_TEST(no_data_from_manager)
{

	struct test_config config = {
	    .lease_name = TEST_LEASE_NAME,
	    .send_no_data = true,
	};

	struct server_state *sstate = test_server_start(&config);

	struct dlm_lease *lease = dlm_get_lease(TEST_LEASE_NAME);

	ck_assert_ptr_eq(lease, NULL);
	ck_assert_int_eq(errno, EACCES);

	test_server_stop(sstate);
}
END_TEST

/* no_lease_fd_from_manager
 *
 * Test details: Simulate receiving response from lease manager with
 *               no fd attached.  (i.e. a protocol error)
 *
 * Expected results: dlm_get_lease() fails, errno set to EPROTO.
 */
START_TEST(no_lease_fd_from_manager)
{
	/* Receive message from the lease manager with missing lease fd */
	struct test_config config = {
	    .lease_name = TEST_LEASE_NAME,
	    .send_data_without_fd = true,
	};

	struct server_state *sstate = test_server_start(&config);

	struct dlm_lease *lease = dlm_get_lease(TEST_LEASE_NAME);

	ck_assert_ptr_eq(lease, NULL);
	ck_assert_int_eq(errno, EPROTO);

	test_server_stop(sstate);
}
END_TEST

static void add_lease_manager_error_tests(Suite *s)
{
	TCase *tc = tcase_create("Lease manager error handling");

	tcase_add_checked_fixture(tc, test_setup, test_shutdown);

	tcase_add_test(tc, manager_connection_err);
	tcase_add_test(tc, no_data_from_manager);
	tcase_add_test(tc, no_lease_fd_from_manager);

	suite_add_tcase(s, tc);
}

/**************  Lease handling tests  *****************/

/* These tests verify that the client library handles the received
 * lease data properly. Receiving the lease fds without leaks,
 * properly passing the fds to the client application and cleaning
 * them up on release.
 */

static int count_open_fds(void)
{
	int fds = 0;
	DIR *dirp = opendir("/proc/self/fd");
	while ((readdir(dirp) != NULL))
		fds++;
	closedir(dirp);
	return fds;
}

/* receive_fd_from_manager
 *
 * Test details: Successfully receive a file descriptor.
 * Expected results: dlm_get_lease() succeeds.
 *                   dlm_lease_fd() returns the correct fd value.
 */
START_TEST(receive_fd_from_manager)
{
	struct server_state *sstate = test_server_start(&default_test_config);

	struct dlm_lease *lease = dlm_get_lease(TEST_LEASE_NAME);
	ck_assert_ptr_ne(lease, NULL);

	int received_fd = dlm_lease_fd(lease);

	int sent_fd = default_test_config.fds[0];

	check_fd_equality(received_fd, sent_fd);

	dlm_release_lease(lease);

	test_server_stop(sstate);
	close(sent_fd);
}
END_TEST

/* lease_fd_is_closed_on_release
 *
 * Test details: Verify that dlm_release_lease() closes the lease fd.
 * Expected results: lease fd is closed.
 */
START_TEST(lease_fd_is_closed_on_release)
{
	struct server_state *sstate = test_server_start(&default_test_config);

	struct dlm_lease *lease = dlm_get_lease(TEST_LEASE_NAME);
	ck_assert_ptr_ne(lease, NULL);

	int received_fd = dlm_lease_fd(lease);

	check_fd_is_open(received_fd);
	dlm_release_lease(lease);
	check_fd_is_closed(received_fd);

	test_server_stop(sstate);
}
END_TEST

/* dlm_lease_fd_always_returns_same_lease
 *
 * Test details: Verify that dlm_lease_fd() always returns the same value
 *               for a given lease.
 * Expected results: same value is returned when called multiple times.
 */
START_TEST(dlm_lease_fd_always_returns_same_lease)
{
	struct server_state *sstate = test_server_start(&default_test_config);

	struct dlm_lease *lease = dlm_get_lease(TEST_LEASE_NAME);
	ck_assert_ptr_ne(lease, NULL);

	int received_fd = dlm_lease_fd(lease);

	ck_assert_int_eq(received_fd, dlm_lease_fd(lease));
	ck_assert_int_eq(received_fd, dlm_lease_fd(lease));

	dlm_release_lease(lease);

	test_server_stop(sstate);
}
END_TEST

START_TEST(verify_that_unused_fds_are_not_leaked)
{
	int nopen_fds = count_open_fds();

	struct test_config config = {
	    .lease_name = TEST_LEASE_NAME,
	    .nfds = 2,
	};

	struct server_state *sstate = test_server_start(&config);

	struct dlm_lease *lease = dlm_get_lease(TEST_LEASE_NAME);

	ck_assert_ptr_eq(lease, NULL);
	ck_assert_int_eq(errno, EPROTO);

	dlm_release_lease(lease);

	test_server_stop(sstate);

	test_config_cleanup(&config);
	ck_assert_int_eq(nopen_fds, count_open_fds());
}
END_TEST

static void add_lease_handling_tests(Suite *s)
{
	TCase *tc = tcase_create("Lease processing tests");

	tcase_add_checked_fixture(tc, test_setup, test_shutdown);

	tcase_add_test(tc, receive_fd_from_manager);
	tcase_add_test(tc, lease_fd_is_closed_on_release);
	tcase_add_test(tc, dlm_lease_fd_always_returns_same_lease);
	tcase_add_test(tc, verify_that_unused_fds_are_not_leaked);
	suite_add_tcase(s, tc);
}

int main(void)
{
	int number_failed;
	Suite *s;
	SRunner *sr;

	s = suite_create("DLM client library tests");

	add_lease_manager_error_tests(s);
	add_lease_handling_tests(s);

	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
