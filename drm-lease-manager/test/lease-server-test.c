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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <pthread.h>

#include "lease-server.h"
#include "log.h"
#include "test-helpers.h"
#include "test-socket-client.h"

#define SOCKETDIR "/tmp"

#define ARRAY_LENGTH(x) sizeof(x) / sizeof(x[0])

/************** Test fixutre functions *************************/
struct test_config default_test_config;

#define TEST_LEASE_NAME "test-lease"

static struct lease_handle test_lease = {
    .name = TEST_LEASE_NAME,
};

static void test_setup(void)
{
	dlm_log_enable_debug(true);
	setenv("DLM_RUNTIME_PATH", SOCKETDIR, 1);

	default_test_config = (struct test_config){
	    .lease = &test_lease,
	};
}

static void test_shutdown(void)
{
	test_config_cleanup(&default_test_config);
}

static struct ls *create_default_server(void)
{
	struct lease_handle *leases[] = {
	    &test_lease,
	};
	struct ls *ls = ls_create(leases, 1);
	ck_assert_ptr_ne(ls, NULL);
	return ls;
}

/**************  Lease server error handling tests *************/

/* duplicate_server_failure
 *
 * Test details: Try to intialize the same server twice
 * Expected results: ls_create() fails.
 */
START_TEST(duplicate_server_failure)
{
	struct lease_handle *leases[] = {&test_lease, &test_lease};
	struct ls *ls = ls_create(leases, 2);
	ck_assert_ptr_eq(ls, NULL);
}
END_TEST

START_TEST(long_lease_name_failure)
{
	char long_lease_name[200];

	size_t len = sizeof(long_lease_name) - 1;
	memset(long_lease_name, 'a', len);
	long_lease_name[len] = '\0';

	struct lease_handle long_name_lease = {.name = long_lease_name};

	struct lease_handle *leases[] = {&long_name_lease};
	struct ls *ls = ls_create(leases, 1);
	ck_assert_ptr_eq(ls, NULL);
}
END_TEST

static void add_error_tests(Suite *s)
{
	TCase *tc = tcase_create("Lease server errors");

	tcase_add_checked_fixture(tc, test_setup, test_shutdown);

	tcase_add_test(tc, duplicate_server_failure);
	tcase_add_test(tc, long_lease_name_failure);
	suite_add_tcase(s, tc);
}

/**************  Client request handling tests ************/

/* Test the handling of client requests.  Make sure that the
 * proper struct ls_req are generated for each client request.
 */

static void check_request(struct ls_req *req,
			  struct lease_handle *expected_lease,
			  enum ls_req_type expected_type)
{
	ck_assert_ptr_eq(req->lease_handle, expected_lease);
	ck_assert_int_eq(req->type, expected_type);
}

static void get_and_check_request(struct ls *ls,
				  struct lease_handle *expected_lease,
				  enum ls_req_type expected_type)
{
	struct ls_req req;
	bool req_valid = ls_get_request(ls, &req);
	ck_assert_int_eq(req_valid, true);
	check_request(&req, expected_lease, expected_type);
}

/* issue_lease_request_and_release
 *
 * Test details: Generate a lease request and lease release command from
 *               a client.
 * Expected results: One get lease and one release lease request are returned
 *                   from ls_get_request().
 */
START_TEST(issue_lease_request_and_release)
{
	struct ls *ls = create_default_server();

	struct client_state *cstate = test_client_start(&default_test_config);

	get_and_check_request(ls, &test_lease, LS_REQ_GET_LEASE);
	test_client_stop(cstate);
	get_and_check_request(ls, &test_lease, LS_REQ_RELEASE_LEASE);
	ls_destroy(ls);
}
END_TEST

/* issue_lease_request_and_early_release
 *
 * Test details: Close client connection immediately after connecting (before
 *               lease request is processed)
 * Expected results: Should be the same result as
 * issue_lease_request_and_release.
 */
START_TEST(issue_lease_request_and_early_release)
{
	struct ls *ls = create_default_server();

	struct client_state *cstate = test_client_start(&default_test_config);

	test_client_stop(cstate);
	get_and_check_request(ls, &test_lease, LS_REQ_GET_LEASE);
	get_and_check_request(ls, &test_lease, LS_REQ_RELEASE_LEASE);
	ls_destroy(ls);
}
END_TEST

/* issue_multiple_lease_requests
 *
 * Test details: Generate multiple lease requests to the same lease server from
 *               multiple clients at the same time
 * Expected results: One lease request per client is returned from
 *                   ls_get_request().
 *                   (Test will process each connection in series and either
 *                    keep the current connection or switch a new one.)
 *                   Only one client remains connected at the end of the test,
 *                   and it returns a validlease release request.
 */
START_TEST(issue_multiple_lease_requests)
{
	/* List of which client connections to accept.
	 * If the nth element is `false` that client request will be
	 * rejected, and should be reflected in the final configuration
	 * state */
	bool keep_current_client[] = {false, true, true, false, true};

	struct lease_handle *leases[] = {
	    &test_lease,
	};
	struct ls *ls = ls_create(leases, 1);

	const int clients = ARRAY_LENGTH(keep_current_client);
	struct test_config configs[clients];
	struct client_state *cstates[clients];

	struct ls_req req;
	struct ls_client *current_client = NULL;

	/* Start all clients and accept / reject connections */
	for (int i = 0; i < clients; i++) {
		configs[i] = default_test_config;
		cstates[i] = test_client_start(&configs[i]);
		ck_assert_int_eq(ls_get_request(ls, &req), true);
		check_request(&req, &test_lease, LS_REQ_GET_LEASE);
		if (current_client && keep_current_client[i]) {
			ls_disconnect_client(ls, req.client);
		} else {
			if (current_client)
				ls_disconnect_client(ls, current_client);
			current_client = req.client;
		}
	}

	/* Shut down all clients */
	for (int i = 0; i < clients; i++)
		test_client_stop(cstates[i]);

	/* Check that a valid release is received from the last accepted client
	 * connection */
	ck_assert_int_eq(ls_get_request(ls, &req), true);
	check_request(&req, &test_lease, LS_REQ_RELEASE_LEASE);
	ck_assert_ptr_eq(current_client, req.client);

	/* Check that no other client connections have completed */
	int connections_completed = 0;
	for (int i = 0; i < clients; i++) {
		if (configs[i].connection_completed) {
			connections_completed++;
			ck_assert_int_eq(connections_completed, 1);
		}
	}
	ls_destroy(ls);
}
END_TEST

static void add_client_request_tests(Suite *s)
{
	TCase *tc = tcase_create("Client request testing");

	tcase_add_checked_fixture(tc, test_setup, test_shutdown);

	tcase_add_test(tc, issue_lease_request_and_release);
	tcase_add_test(tc, issue_lease_request_and_early_release);
	tcase_add_test(tc, issue_multiple_lease_requests);
	suite_add_tcase(s, tc);
}

/**************  File descriptor sending tests ************/

/* Test the sending (and failure to send) of file descriptors
 * to the client.
 */

/* send_fd_to_client
 *
 * Test details: Send a valid fd to a given client.
 * Expected results: The correct fd is successfully sent.
 */
START_TEST(send_fd_to_client)
{
	struct ls *ls = create_default_server();

	struct client_state *cstate = test_client_start(&default_test_config);

	struct ls_req req;
	bool req_valid = ls_get_request(ls, &req);
	ck_assert_int_eq(req_valid, true);
	check_request(&req, &test_lease, LS_REQ_GET_LEASE);

	/* send an fd to the client*/
	int test_fd = get_dummy_fd();
	ck_assert_int_eq(ls_send_fd(ls, req.client, test_fd), true);

	test_client_stop(cstate);
	get_and_check_request(ls, &test_lease, LS_REQ_RELEASE_LEASE);

	ck_assert_int_eq(default_test_config.connection_completed, true);
	ck_assert_int_eq(default_test_config.has_data, true);
	check_fd_equality(test_fd, default_test_config.received_fd);
	ls_destroy(ls);
}
END_TEST

/* ls_send_fd_is_noop_when_fd_is_invalid
 *
 * Test details: Call ls_send_fd() with an invalid  fd.
 * Expected results: No fd is sent to client.  The connection to the
 *                   client is closed.
 */
START_TEST(ls_send_fd_is_noop_when_fd_is_invalid)
{
	struct ls *ls = create_default_server();

	struct client_state *cstate = test_client_start(&default_test_config);

	struct ls_req req;
	bool req_valid = ls_get_request(ls, &req);
	ck_assert_int_eq(req_valid, true);
	check_request(&req, &test_lease, LS_REQ_GET_LEASE);

	int invalid_fd = get_dummy_fd();
	close(invalid_fd);

	ck_assert_int_eq(ls_send_fd(ls, req.client, invalid_fd), false);

	test_client_stop(cstate);
	get_and_check_request(ls, &test_lease, LS_REQ_RELEASE_LEASE);
	ck_assert_int_eq(default_test_config.connection_completed, true);
	ck_assert_int_eq(default_test_config.has_data, false);
	ls_destroy(ls);
}
END_TEST

static void add_fd_send_tests(Suite *s)
{
	TCase *tc = tcase_create("File descriptor sending tests");

	tcase_add_checked_fixture(tc, test_setup, test_shutdown);

	tcase_add_test(tc, send_fd_to_client);
	tcase_add_test(tc, ls_send_fd_is_noop_when_fd_is_invalid);
	suite_add_tcase(s, tc);
}

int main(void)
{
	int number_failed;
	Suite *s;
	SRunner *sr;

	s = suite_create("DLM lease server tests");

	add_error_tests(s);
	add_client_request_tests(s);
	add_fd_send_tests(s);

	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
