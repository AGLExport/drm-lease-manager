dlmclient_sources = files(
    'dlmclient.c'
)

dlmclient_headers = files(
    'dlmclient.h'
)

libdlmclient = library(
    'dlmclient',
    sources: dlmclient_sources,
    version: meson.project_version(),
    dependencies: [dlmcommon_dep],
    install: true,
)

dlmclient_dep = declare_dependency(
    link_with: libdlmclient,
    include_directories: include_directories('.')
)


install_headers(dlmclient_headers, subdir: 'libdlmclient')

pkg.generate(
    name: 'libdlmclient',
    libraries: libdlmclient,
    subdirs: [ 'libdlmclient' ],
    version: meson.project_version(),
    description: 'DRM lease manager client library',
)

dlmclient_header_dir = meson.current_source_dir()
subdir('docs')

if enable_tests
  subdir('test')
endif
