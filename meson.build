project('liarswm', 'c')

wlroots = dependency('wlroots-0.20')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
libinput = dependency('libinput')

cc = meson.get_compiler('c')
toml = cc.find_library('tomlc17', dirs: meson.current_source_dir() / 'lib', required: true)

add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')
add_project_arguments('-D_GNU_SOURCE', language: 'c')

incdir = include_directories('include')

wayland_scanner = find_program('wayland-scanner')

layer_shell_header = custom_target(
  'wlr-layer-shell-unstable-v1-protocol.h',
  output: 'wlr-layer-shell-unstable-v1-protocol.h',
  input: 'protocols/wlr-layer-shell-unstable-v1.xml',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

subdir('src')

executable(
  'liars',
  'liar.c',
  sources,
  layer_shell_header,
  include_directories : incdir,
  dependencies: [wlroots, wayland_server, xkbcommon, libinput, toml]
)

executable(
  'liarsctl',
  'src/liarsctl.c',
  include_directories : incdir,
)
