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

/* Asynchronous version of the above.  Has the extra overhead of
 * spawning a new thread, so should be used sparingly. */
struct async_req {
	pthread_t tid;
	struct ls *ls;

	bool req_valid;
	struct ls_req expected;
	struct ls_req actual;
};

static void *get_request_thread(void *arg)
{
	struct async_req *async_req = arg;
	async_req->req_valid =
	    ls_get_request(async_req->ls, &async_req->actual);

	return NULL;
}

static struct async_req *
get_and_check_request_async(struct ls *ls, struct lease_handle *expected_lease,
			    enum ls_req_type expected_type)

{
	struct async_req *req = malloc(sizeof(struct async_req));
	ck_assert_ptr_ne(req, NULL);

	*req = (struct async_req){
	    .ls = ls,
	    .expected =
		{
		    .lease_handle = expected_lease,
		    .type = expected_type,
		},
	};

	int ret = pthread_create(&req->tid, NULL, get_request_thread, req);
	ck_assert_int_eq(ret, 0);

	return req;
}

static void check_async_req_result(struct async_req *req)
{

	pthread_join(req->tid, NULL);
	ck_assert_int_eq(req->req_valid, true);
	check_request(&req->actual, req->expected.lease_handle,
		      req->expected.type);
	free(req);
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
}
END_TEST

/* issue_multiple_lease_requests
 *
 * Test details: Generate multiple lease requests to the same lease server from
 *               multiple clients at the same time
 * Expected results: One get lease and one release lease request are returned
 *                   from ls_get_request().
 *                   Requests from all but the first client are rejected
 *                   (sockets are closed).
 */
START_TEST(issue_multiple_lease_requests)
{
	struct lease_handle *leases[] = {
	    &test_lease,
	};
	struct ls *ls = ls_create(leases, 1);

	struct test_config accepted_config;
	struct client_state *accepted_cstate;

	accepted_config = default_test_config;
	accepted_cstate = test_client_start(&accepted_config);
	get_and_check_request(ls, &test_lease, LS_REQ_GET_LEASE);

	/*Try to make additional connections while the first is still
	 *connected. */
	const int nextra_clients = 2;
	struct test_config extra_configs[nextra_clients];
	struct client_state *extra_cstates[nextra_clients];

	for (int i = 0; i < nextra_clients; i++) {
		extra_configs[i] = default_test_config;
		extra_cstates[i] = test_client_start(&extra_configs[i]);
	}

	// Start asyncronously checking for the accepted client to release.
	struct async_req *async_release_req =
	    get_and_check_request_async(ls, &test_lease, LS_REQ_RELEASE_LEASE);

	for (int i = 0; i < nextra_clients; i++) {
		test_client_stop(extra_cstates[i]);
	}

	/* Release the first connection and check results */
	test_client_stop(accepted_cstate);
	check_async_req_result(async_release_req);

	/* Only one connection should be granted access by the lease manager */
	ck_assert_int_eq(accepted_config.connection_completed, true);
	for (int i = 0; i < nextra_clients; i++)
		ck_assert_int_eq(extra_configs[i].connection_completed, false);
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
	ck_assert_int_eq(ls_send_fd(ls, req.server, test_fd), true);

	test_client_stop(cstate);
	get_and_check_request(ls, &test_lease, LS_REQ_RELEASE_LEASE);

	ck_assert_int_eq(default_test_config.connection_completed, true);
	ck_assert_int_eq(default_test_config.has_data, true);
	check_fd_equality(test_fd, default_test_config.received_fd);
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

	ck_assert_int_eq(ls_send_fd(ls, req.server, invalid_fd), false);

	test_client_stop(cstate);
	get_and_check_request(ls, &test_lease, LS_REQ_RELEASE_LEASE);
	ck_assert_int_eq(default_test_config.connection_completed, true);
	ck_assert_int_eq(default_test_config.has_data, false);
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
