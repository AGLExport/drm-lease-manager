check_dep = dependency('check')

ls_inc = include_directories('..')

test_c_args = [
   '-Wno-missing-field-initializers', #not all tests explicitly initialize all lease config fields
]

ls_objects = main.extract_objects(lease_server_files)
ls_test_sources = [
   'lease-server-test.c',
   'test-socket-client.c',
]

ls_test = executable('lease-server-test',
           sources: ls_test_sources,
           objects: ls_objects,
           dependencies: [check_dep, fff_dep, dlmcommon_dep, thread_dep],
           c_args: test_c_args,
           include_directories: ls_inc)

lm_objects = main.extract_objects(lease_manager_files)
lm_test_sources = [
    'lease-manager-test.c',
    'test-drm-device.c',
]

lm_test = executable('lease-manager-test',
           sources: lm_test_sources,
           objects: lm_objects,
           dependencies: [check_dep, fff_dep, dlmcommon_dep, drm_dep],
           c_args: test_c_args,
           include_directories: ls_inc)

test('DRM Lease manager - socket server test', ls_test, is_parallel: false)
test('DRM Lease manager - DRM interface test', lm_test)
