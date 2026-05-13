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

# all includes resolve from src/
src_inc = include_directories('src')

# ---- libraries ----

common_lib = static_library('curfew_common',
  'src/common/config.c',
  'src/common/time_utils.c',
  include_directories : src_inc,
  dependencies : libsystemd,
)

curfew_lib = static_library('curfew_logic',
  'src/service/curfew.c',
  include_directories : src_inc,
  dependencies : libsystemd,
)

# service layer (systemd, notify, enforcement)
service_lib = static_library('curfew_service',
  'src/service/systemd.c',
  'src/service/notify.c',
  include_directories : src_inc,
  link_with : [common_lib],
  dependencies : libsystemd,
)

# D-Bus interface
dbus_lib = static_library('curfew_dbus',
  'src/service/dbus.c',
  include_directories : src_inc,
  link_with : [common_lib, curfew_lib],
  dependencies : libsystemd,
)

# ---- executables ----

# daemon
executable('curfew-daemon',
  'src/service/main.c',
  include_directories : src_inc,
  link_with : [common_lib, curfew_lib, service_lib, dbus_lib],
  dependencies : libsystemd,
  install : true,
  install_dir : get_option('libdir') / 'curfew',
)

# CLI
executable('curfew',
  'src/cli/main.c',
  include_directories : src_inc,
  dependencies : libsystemd,
  install : true,
  install_dir : get_option('bindir'),
)

# GUI
if get_option('gui')
  executable('curfew-gui',
    'src/gui/main.c',
    include_directories : src_inc,
    dependencies : [libsystemd, gtk4, libadwaita],
    install : true,
    install_dir : get_option('bindir'),
  )

  install_data('resources/curfew-gui.ui',
    install_dir : get_option('datadir') / 'curfew',
  )

  install_data('resources/org.curfew.gui.desktop',
    install_dir : get_option('datadir') / 'applications',
  )
endif

# ---- installed config files ----

install_data('configs/curfew-daemon.service',
  install_dir : '/usr/lib/systemd/system',
)

install_data('configs/org.curfew.conf',
  install_dir : get_option('datadir') / 'dbus-1' / 'system.d',
)

install_data('configs/org.curfew.service',
  install_dir : get_option('datadir') / 'dbus-1' / 'system-services',
)

install_data('configs/org.curfew.policy',
  install_dir : get_option('datadir') / 'polkit-1' / 'actions',
)

install_data('configs/curfew.conf.example',
  install_dir : '/etc/curfew',
  rename : 'curfew.conf',
)
