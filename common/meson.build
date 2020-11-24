libdlmcommon_sources = [
        'socket-path.c',
        'log.c'
]

libdlmcommon_inc = [include_directories('.')]

if enable_tests
    libdlmcommon_inc += include_directories('test')
    libdlmcommon_sources += ['test/test-helpers.c']
endif

libdlmcommon = static_library(
        'common',
        sources: libdlmcommon_sources,
        include_directories : configuration_inc,
)

dlmcommon_dep = declare_dependency(
    link_with : libdlmcommon,
    include_directories : libdlmcommon_inc
)
