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
#include <fff.h>

#include <stdlib.h>
#include <unistd.h>
#include <xf86drmMode.h>

#include "lease-manager.h"
#include "log.h"
#include "test-drm-device.h"
#include "test-helpers.h"

#define INVALID_OBJECT_ID (0)

/* CHECK_LEASE_OBJECTS
 *
 * Checks the list of objects associated with a given lease_index.
 * Asks the lease manager to create the lease, and checks that
 * the requested objects are the ones given in the supplied list. */

#define CHECK_LEASE_OBJECTS(lease, ...)                                     \
	do {                                                                \
		lm_lease_grant(g_lm, lease);                                \
		uint32_t objs[] = {__VA_ARGS__};                            \
		int nobjs = ARRAY_LEN(objs);                                \
		ck_assert_int_eq(drmModeCreateLease_fake.arg2_val, nobjs);  \
		check_uint_array_eq(drmModeCreateLease_fake.arg1_val, objs, \
				    nobjs);                                 \
	} while (0)

/**************  Mock functions  *************/
DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(drmModeResPtr, drmModeGetResources, int);
FAKE_VOID_FUNC(drmModeFreeResources, drmModeResPtr);
FAKE_VALUE_FUNC(drmModePlaneResPtr, drmModeGetPlaneResources, int);
FAKE_VOID_FUNC(drmModeFreePlaneResources, drmModePlaneResPtr);

FAKE_VALUE_FUNC(drmModePlanePtr, drmModeGetPlane, int, uint32_t);
FAKE_VOID_FUNC(drmModeFreePlane, drmModePlanePtr);
FAKE_VALUE_FUNC(drmModeConnectorPtr, drmModeGetConnector, int, uint32_t);
FAKE_VOID_FUNC(drmModeFreeConnector, drmModeConnectorPtr);
FAKE_VALUE_FUNC(drmModeEncoderPtr, drmModeGetEncoder, int, uint32_t);
FAKE_VOID_FUNC(drmModeFreeEncoder, drmModeEncoderPtr);

FAKE_VALUE_FUNC(int, drmModeCreateLease, int, const uint32_t *, int, int,
		uint32_t *);
FAKE_VALUE_FUNC(int, drmModeRevokeLease, int, uint32_t);

/************** Test fixutre functions *************************/
struct lm *g_lm = NULL;

static void test_setup(void)
{
	RESET_FAKE(drmModeGetResources);
	RESET_FAKE(drmModeFreeResources);
	RESET_FAKE(drmModeGetPlaneResources);
	RESET_FAKE(drmModeFreePlaneResources);

	RESET_FAKE(drmModeGetPlane);
	RESET_FAKE(drmModeFreePlane);
	RESET_FAKE(drmModeGetConnector);
	RESET_FAKE(drmModeFreeConnector);
	RESET_FAKE(drmModeGetEncoder);
	RESET_FAKE(drmModeFreeEncoder);

	RESET_FAKE(drmModeCreateLease);
	RESET_FAKE(drmModeRevokeLease);

	drmModeGetResources_fake.return_val = TEST_DEVICE_RESOURCES;
	drmModeGetPlaneResources_fake.return_val = TEST_DEVICE_PLANE_RESOURCES;

	drmModeGetPlane_fake.custom_fake = get_plane;
	drmModeGetConnector_fake.custom_fake = get_connector;
	drmModeGetEncoder_fake.custom_fake = get_encoder;
	drmModeCreateLease_fake.custom_fake = create_lease;

	ck_assert_msg(g_lm == NULL,
		      "Lease manager context not clear at start of test");
}

static void test_shutdown(void)
{
	reset_drm_test_device();
	lm_destroy(g_lm);
	g_lm = NULL;
}

static struct lease_handle **create_leases(int num_leases,
					   struct lease_config *configs)
{
	if (configs)
		g_lm =
		    lm_create_with_config(TEST_DRM_DEVICE, num_leases, configs);
	else
		g_lm = lm_create(TEST_DRM_DEVICE);

	ck_assert_ptr_ne(g_lm, NULL);

	struct lease_handle **handles;
	ck_assert_int_eq(num_leases, lm_get_lease_handles(g_lm, &handles));
	ck_assert_ptr_ne(handles, NULL);

	return handles;
}

/************** Resource enumeration tests *************/

/* These tests verify that the lease manager correctly assigns
 * DRM resources to thier respective leases. In some cases
 * the lease manager must choose which resources to include in
 * each lease, so these tests verify that a valid (but not
 * necessarily optimal) choice is made.
 */

/* all_outputs_connected
 *
 * Test details: Create leases when all crtc/encoder/connector paths are
 *               connected.
 *
 * Expected results: Leases are created for the currently connected sets of
 *                   resources.
 */
START_TEST(all_outputs_connected)
{
	int out_cnt = 2, plane_cnt = 0;

	setup_layout_simple_test_device(out_cnt, plane_cnt);

	struct lease_handle **handles = create_leases(out_cnt, NULL);

	CHECK_LEASE_OBJECTS(handles[0], CRTC_ID(0), CONNECTOR_ID(0));
	CHECK_LEASE_OBJECTS(handles[1], CRTC_ID(1), CONNECTOR_ID(1));
}
END_TEST

/* no_outputs_connected
 *
 * Test details: Create leases when no crtc/encoder/connector paths are
 *               connected.
 *
 * Expected results: Available resources are divided between the leases.
 *                   The same resource should not appear in multiple leases.
 */
START_TEST(no_outputs_connected)
{
	int out_cnt = 2, plane_cnt = 0;

	ck_assert_int_eq(
	    setup_drm_test_device(out_cnt, out_cnt, out_cnt, plane_cnt), true);

	drmModeConnector connectors[] = {
	    CONNECTOR(CONNECTOR_ID(0), 0, &ENCODER_ID(0), 1),
	    CONNECTOR(CONNECTOR_ID(1), 0, &ENCODER_ID(1), 1),
	};

	drmModeEncoder encoders[] = {
	    ENCODER(ENCODER_ID(0), 0, 0x2),
	    ENCODER(ENCODER_ID(1), 0, 0x3),
	};

	setup_test_device_layout(connectors, encoders, NULL);

	g_lm = lm_create(TEST_DRM_DEVICE);
	ck_assert_ptr_ne(g_lm, NULL);

	struct lease_handle **handles = create_leases(out_cnt, NULL);

	CHECK_LEASE_OBJECTS(handles[0], CRTC_ID(1), CONNECTOR_ID(0));
	CHECK_LEASE_OBJECTS(handles[1], CRTC_ID(0), CONNECTOR_ID(1));
}
END_TEST

/* some_outputs_connected  */
/* Test details: Create leases when one output is connected and one is not.
 * Expected results: Currently connected resources should be added to
 *                   the same lease.
 *                   The non-connected resources should be added to a second
 *                   lease.
 */
START_TEST(some_outputs_connected)
{
	int out_cnt = 2, plane_cnt = 0;

	ck_assert_int_eq(
	    setup_drm_test_device(out_cnt, out_cnt, out_cnt, plane_cnt), true);

	drmModeConnector connectors[] = {
	    CONNECTOR(CONNECTOR_ID(0), ENCODER_ID(0), &ENCODER_ID(0), 1),
	    CONNECTOR(CONNECTOR_ID(1), 0, &ENCODER_ID(1), 1),
	};

	drmModeEncoder encoders[] = {
	    ENCODER(ENCODER_ID(0), CRTC_ID(0), 0x3),
	    ENCODER(ENCODER_ID(1), 0, 0x3),
	};

	setup_test_device_layout(connectors, encoders, NULL);

	struct lease_handle **handles = create_leases(out_cnt, NULL);

	CHECK_LEASE_OBJECTS(handles[0], CRTC_ID(0), CONNECTOR_ID(0));
	CHECK_LEASE_OBJECTS(handles[1], CRTC_ID(1), CONNECTOR_ID(1));
}
END_TEST

/* fewer_crtcs_than_connectors  */
/* Test details: Create leases on a system with more connectors than CRTCs
 * Expected results: Number of leases generated should correspond to number of
 *                   CRTCs.
 *                   Leases contain one valid connector for each CRTC.
 */
START_TEST(fewer_crtcs_than_connectors)
{
	int out_cnt = 3, plane_cnt = 0, crtc_cnt = 2;

	ck_assert_int_eq(
	    setup_drm_test_device(crtc_cnt, out_cnt, out_cnt, plane_cnt), true);

	drmModeConnector connectors[] = {
	    CONNECTOR(CONNECTOR_ID(0), 0, &ENCODER_ID(0), 1),
	    CONNECTOR(CONNECTOR_ID(1), 0, &ENCODER_ID(1), 1),
	    CONNECTOR(CONNECTOR_ID(2), 0, &ENCODER_ID(2), 1),
	};

	drmModeEncoder encoders[] = {
	    ENCODER(ENCODER_ID(0), 0, 0x3),
	    ENCODER(ENCODER_ID(1), 0, 0x1),
	    ENCODER(ENCODER_ID(2), 0, 0x3),
	};

	setup_test_device_layout(connectors, encoders, NULL);

	struct lease_handle **handles = create_leases(crtc_cnt, NULL);

	CHECK_LEASE_OBJECTS(handles[0], CRTC_ID(0), CONNECTOR_ID(0));
	CHECK_LEASE_OBJECTS(handles[1], CRTC_ID(1), CONNECTOR_ID(2));
}
END_TEST

/* separate_overlay_planes_by_crtc  */
/* Test details: Add overlay planes to leases. Each plane is tied to a
 *               specific CRTC.
 * Expected results: The leases contain all of the planes for connected to
 *                   each CRTC and no others.
 */
START_TEST(separate_overlay_planes_by_crtc)
{

	int out_cnt = 2, plane_cnt = 3;

	setup_layout_simple_test_device(out_cnt, plane_cnt);

	struct lease_handle **handles = create_leases(out_cnt, NULL);

	CHECK_LEASE_OBJECTS(handles[0], PLANE_ID(0), PLANE_ID(2), CRTC_ID(0),
			    CONNECTOR_ID(0));
	CHECK_LEASE_OBJECTS(handles[1], PLANE_ID(1), CRTC_ID(1),
			    CONNECTOR_ID(1));
}
END_TEST

/* reject_planes_shared_between_multiple_crtcs */
/* Test details: Add overlay planes to leases. Some planes are shared between
 *               multiple CRTCs.
 * Expected results: The leases contain all of the unique planes for each CRTC.
 *                   Planes that can be used on multiple CRTCs are not included
 *                   in any lease.
 */
START_TEST(reject_planes_shared_between_multiple_crtcs)
{

	int out_cnt = 2, plane_cnt = 3;

	ck_assert_int_eq(
	    setup_drm_test_device(out_cnt, out_cnt, out_cnt, plane_cnt), true);

	drmModeConnector connectors[] = {
	    CONNECTOR(CONNECTOR_ID(0), ENCODER_ID(0), &ENCODER_ID(0), 1),
	    CONNECTOR(CONNECTOR_ID(1), ENCODER_ID(1), &ENCODER_ID(1), 1),
	};

	drmModeEncoder encoders[] = {
	    ENCODER(ENCODER_ID(0), CRTC_ID(0), 0x1),
	    ENCODER(ENCODER_ID(1), CRTC_ID(1), 0x2),
	};

	drmModePlane planes[] = {
	    PLANE(PLANE_ID(0), 0x2),
	    PLANE(PLANE_ID(1), 0x1),
	    PLANE(PLANE_ID(2), 0x3),
	};

	setup_test_device_layout(connectors, encoders, planes);

	struct lease_handle **handles = create_leases(out_cnt, NULL);

	CHECK_LEASE_OBJECTS(handles[0], PLANE_ID(1), CRTC_ID(0),
			    CONNECTOR_ID(0));
	CHECK_LEASE_OBJECTS(handles[1], PLANE_ID(0), CRTC_ID(1),
			    CONNECTOR_ID(1));
}
END_TEST

static void add_connector_enum_tests(Suite *s)
{
	TCase *tc = tcase_create("Resource enumeration");

	tcase_add_checked_fixture(tc, test_setup, test_shutdown);

	tcase_add_test(tc, all_outputs_connected);
	tcase_add_test(tc, no_outputs_connected);
	tcase_add_test(tc, fewer_crtcs_than_connectors);
	tcase_add_test(tc, some_outputs_connected);
	tcase_add_test(tc, separate_overlay_planes_by_crtc);
	tcase_add_test(tc, reject_planes_shared_between_multiple_crtcs);
	suite_add_tcase(s, tc);
}

/************** Lease management tests *************/

/* create_and_revoke_lease */
/* Test details: Create leases and revoke them.
 * Expected results: drmModeRevokeLease() is called with the correct leasee_id.
 */
START_TEST(create_and_revoke_lease)
{
	int lease_cnt = 2, plane_cnt = 0;

	setup_layout_simple_test_device(lease_cnt, plane_cnt);

	struct lease_handle **handles = create_leases(lease_cnt, NULL);

	for (int i = 0; i < lease_cnt; i++) {
		ck_assert_int_ge(lm_lease_grant(g_lm, handles[i]), 0);
		lm_lease_revoke(g_lm, handles[i]);
	}

	ck_assert_int_eq(drmModeRevokeLease_fake.call_count, lease_cnt);

	for (int i = 0; i < lease_cnt; i++) {
		ck_assert_int_eq(drmModeRevokeLease_fake.arg1_history[i],
				 LESSEE_ID(i));
	}
}
END_TEST

/* Test lease names */
/* Test details: Create some leases and verify that they have the correct names
 * Expected results: lease names should match the expected values
 */
START_TEST(verify_lease_names)
{
	int lease_cnt = 3;
	bool res = setup_drm_test_device(lease_cnt, lease_cnt, lease_cnt, 0);
	ck_assert_int_eq(res, true);

	drmModeConnector connectors[] = {
	    CONNECTOR_FULL(CONNECTOR_ID(0), ENCODER_ID(0), &ENCODER_ID(0), 1,
			   DRM_MODE_CONNECTOR_HDMIA, 1),
	    CONNECTOR_FULL(CONNECTOR_ID(1), ENCODER_ID(1), &ENCODER_ID(1), 1,
			   DRM_MODE_CONNECTOR_LVDS, 3),
	    CONNECTOR_FULL(CONNECTOR_ID(2), ENCODER_ID(2), &ENCODER_ID(2), 1,
			   DRM_MODE_CONNECTOR_eDP, 6),
	};

	drmModeEncoder encoders[] = {
	    ENCODER(ENCODER_ID(0), CRTC_ID(0), 0x7),
	    ENCODER(ENCODER_ID(1), CRTC_ID(1), 0x7),
	    ENCODER(ENCODER_ID(2), CRTC_ID(2), 0x7),
	};

	setup_test_device_layout(connectors, encoders, NULL);

	const char *expected_names[] = {
	    "card3-HDMI-A-1",
	    "card3-LVDS-3",
	    "card3-eDP-6",
	};

	struct lease_handle **handles = create_leases(lease_cnt, NULL);

	for (int i = 0; i < lease_cnt; i++) {
		ck_assert_str_eq(handles[i]->name, expected_names[i]);
	}
}
END_TEST

static void add_lease_management_tests(Suite *s)
{
	TCase *tc = tcase_create("Lease management");

	tcase_add_checked_fixture(tc, test_setup, test_shutdown);

	tcase_add_test(tc, create_and_revoke_lease);
	tcase_add_test(tc, verify_lease_names);
	suite_add_tcase(s, tc);
}

/***************** Lease Configuration Tests *************/

/* multiple_connector_lease */
/* Test details: Create a lease with multipe connectors
 * Expected results: a lease is created with the CRTC and connector ID for both
 *                   connectors.
 */
START_TEST(multiple_connector_lease)
{
	int out_cnt = 2, plane_cnt = 0, lease_cnt = 1;

	setup_layout_simple_test_device(out_cnt, plane_cnt);

	struct lease_config lconfig = {
	    .lease_name = "Lease Config Test 1",
	    .ncids = 2,
	    .connector_ids = (uint32_t[]){CONNECTOR_ID(0), CONNECTOR_ID(1)},
	};

	struct lease_handle **handles = create_leases(lease_cnt, &lconfig);

	CHECK_LEASE_OBJECTS(handles[0], CRTC_ID(0), CONNECTOR_ID(0), CRTC_ID(1),
			    CONNECTOR_ID(1));
}
END_TEST

/* single_failed_lease */
/* Test details: Create 2 lease configs. One with valid data, one without.
 * Expected results: A handle is created for the single valid lease.
 */
START_TEST(single_failed_lease)
{
	int out_cnt = 3, plane_cnt = 0, success_lease_cnt = 1;

	setup_layout_simple_test_device(out_cnt, plane_cnt);

	struct lease_config lconfigs[2] = {
	    [0] =
		{
		    .lease_name = "Lease Config Test 1",
		    .ncids = 1,
		    .connector_ids = (uint32_t[]){INVALID_OBJECT_ID},
		},
	    [1] =
		{
		    .lease_name = "Lease Config Test 2",
		    .ncids = 2,
		    .connector_ids =
			(uint32_t[]){CONNECTOR_ID(0), CONNECTOR_ID(1)},
		},
	};

	/* Expect fewer leases than configurations supplied, so explicitly
	 * create and check leases. */
	g_lm = lm_create_with_config(TEST_DRM_DEVICE, ARRAY_LEN(lconfigs),
				     lconfigs);
	ck_assert_ptr_ne(g_lm, NULL);

	struct lease_handle **handles;
	ck_assert_int_eq(success_lease_cnt,
			 lm_get_lease_handles(g_lm, &handles));
	ck_assert_ptr_ne(handles, NULL);

	CHECK_LEASE_OBJECTS(handles[0], CRTC_ID(0), CONNECTOR_ID(0), CRTC_ID(1),
			    CONNECTOR_ID(1));
}
END_TEST

/* named_connector_config */
/* Test details: Test specifying connectors by name in config
 * Expected results: A handle is created for each named connector
 */

START_TEST(named_connector_config)
{
	int out_cnt = 2, plane_cnt = 0, lease_cnt = 1;

	ck_assert_int_eq(
	    setup_drm_test_device(out_cnt, out_cnt, out_cnt, plane_cnt), true);

	drmModeConnector connectors[] = {
	    CONNECTOR_FULL(CONNECTOR_ID(0), ENCODER_ID(0), &ENCODER_ID(0), 1,
			   DRM_MODE_CONNECTOR_HDMIA, 1),
	    CONNECTOR_FULL(CONNECTOR_ID(1), ENCODER_ID(1), &ENCODER_ID(1), 1,
			   DRM_MODE_CONNECTOR_VGA, 3),
	};

	drmModeEncoder encoders[] = {
	    ENCODER(ENCODER_ID(0), CRTC_ID(0), 0x1),
	    ENCODER(ENCODER_ID(1), CRTC_ID(1), 0x2),
	};

	setup_test_device_layout(connectors, encoders, NULL);

	struct lease_config lconfig = {
	    .lease_name = "Lease Config Test 1",
	    .cnames = 2,
	    .connector_names = (char *[]){"HDMI-A-1", "VGA-3"},
	};

	struct lease_handle **handles = create_leases(lease_cnt, &lconfig);

	ck_assert_str_eq(handles[0]->name, lconfig.lease_name);
	CHECK_LEASE_OBJECTS(handles[0], CRTC_ID(0), CONNECTOR_ID(0), CRTC_ID(1),
			    CONNECTOR_ID(1));
}
END_TEST

static void add_lease_config_tests(Suite *s)
{
	TCase *tc = tcase_create("Lease configuration");

	tcase_add_checked_fixture(tc, test_setup, test_shutdown);

	tcase_add_test(tc, multiple_connector_lease);
	tcase_add_test(tc, single_failed_lease);
	tcase_add_test(tc, named_connector_config);
	suite_add_tcase(s, tc);
}

int main(void)
{
	int number_failed;
	Suite *s;
	SRunner *sr;

	s = suite_create("DLM lease manager tests");

	add_connector_enum_tests(s);
	add_lease_management_tests(s);
	add_lease_config_tests(s);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
