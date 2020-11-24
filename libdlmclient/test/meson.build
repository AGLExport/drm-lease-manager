cl_test = executable('libdlmclient-test',
           sources: [ 'libdlmclient-test.c', 'test-socket-server.c'],
           dependencies: [check_dep, fff_dep, dlmcommon_dep, dlmclient_dep],
           include_directories: configuration_inc)

test('Client library test', cl_test, is_parallel: false)
