project('curfew', 'c',
  version : '1.0.0',
  license : 'MIT',
  default_options : [
    'c_std=c11',
    'warning_level=2',
  ],
)

add_project_arguments('-D_GNU_SOURCE', language : 'c')

# dependencies
libsystemd = dependency('libsystemd', required : true)
libadwaita = dependency('libadwaita-1', version : '>=1.0', required : get_option('gui'))
gtk4 = dependency('gtk4', version : '>=4.0', required : get_option('gui'))


# includes
include = include_directories('include')


# core lib
core_lib = static_library('curfew_core',
  'src/core/core.c',
  include_directories : include,
)

# config library
config_lib = static_library('curfew_config',
  'src/common/config.c',
  include_directories : include,
  link_with : [core_lib],
)

# systemd library
systemd_lib = static_library('curfew_systemd',
  'src/systemd/systemd.c',
  include_directories : include,
  dependencies : libsystemd,
)

# notify library
notify_lib = static_library('curfew_notify',
  'src/common/notify.c',
  include_directories : include,
  link_with : [systemd_lib],
  dependencies : libsystemd,
)

common_libs = [core_lib, config_lib, systemd_lib, notify_lib]
common_deps = [libsystemd]

# curfew-service
executable('curfew-service',
  'src/service/main.c',
  include_directories : include,
  link_with : common_libs,
  dependencies : common_deps,
  install : true,
  install_dir : get_option('libdir') / 'curfew',
)

# curfew-helper
executable('curfew-helper',
  'src/service/helper.c',
  include_directories : include,
  link_with : common_libs,
  dependencies : common_deps,
  install : true,
  install_dir : get_option('libdir') / 'curfew',
)

# curfew cli
executable('curfew',
  'src/cli/main.c',
  include_directories : include,
  link_with : common_libs,
  dependencies : common_deps,
  install : true,
  install_dir : get_option('bindir'),
)

# curfew gui
if get_option('gui')
  executable('curfew-gui',
    'src/gui/main.c',
    include_directories : include,
    link_with : common_libs,
    dependencies : [common_deps, gtk4, libadwaita],
    install : true,
    install_dir : get_option('bindir'),
  )
endif

# systemd unit files
install_data('configs/curfew.service',
  install_dir : '/usr/lib/systemd/system',
)
install_data('configs/curfew.timer',
  install_dir : '/usr/lib/systemd/system',
)

# polkit policy file
install_data('configs/org.curfew.policy',
  install_dir : get_option('datadir') / 'polkit-1' / 'actions',
)

# desktop file
if get_option('gui')
  install_data('resources/org.curfew.gui.desktop',
    install_dir : get_option('datadir') / 'applications',
  )
endif

# config file
install_data('configs/curfew.conf.example',
  install_dir : '/etc/curfew',
  rename : 'curfew.conf.example',
)

# tests
test_core = executable('test_core',
  'tests/test_core.c',
  include_directories : include,
  link_with : [core_lib],
)
test('core', test_core)

test_config = executable('test_config',
  'tests/test_config.c',
  include_directories : include,
  link_with : [core_lib, config_lib],
)
test('config', test_config)
