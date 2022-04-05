
lease_manager_files = files('lease-manager.c')
lease_server_files = files('lease-server.c')
lease_config_files = files('lease-config.c')
main = executable('drm-lease-manager',
    [ 'main.c', lease_manager_files, lease_server_files, lease_config_files ],
    dependencies: [ drm_dep, dlmcommon_dep, thread_dep, toml_dep ],
    install: true,
)

if enable_tests
  subdir('test')
endif
