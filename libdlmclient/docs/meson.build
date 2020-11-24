doxygen = find_program('doxygen', required : false)

readme = join_paths(meson.source_root(), 'README.md')

if get_option('enable-docs') and doxygen.found()
    conf_data = configuration_data()
    conf_data.set('README', readme)
    conf_data.set('CLIENT_HEADER_DIR', dlmclient_header_dir)
    conf_data.set('DOXYGEN_OUTPUT', meson.current_build_dir())
    doxyfile = configure_file(
        input: 'Doxyfile.in',
        output: 'Doxyfile',
        configuration: conf_data
    )
    custom_target('docs',
      input: [doxyfile, readme, dlmclient_headers],
      build_by_default: true,
      command: [doxygen, '@INPUT0@'],
      output: ['html']
    )
endif
