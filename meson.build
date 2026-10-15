project('liarswm', 'c')

wlroots = dependency('wlroots-0.20')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
libinput = dependency('libinput')

cc = meson.get_compiler('c')
toml = cc.find_library('tomlc17', dirs: meson.current_source_dir() / 'lib', required: true)

add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')

incdir = include_directories('include')

wayland_scanner = find_program('wayland-scanner')

layer_shell_header = custom_target(
  'wlr-layer-shell-unstable-v1-protocol.h',
  output: 'wlr-layer-shell-unstable-v1-protocol.h',
  input: 'protocols/wlr-layer-shell-unstable-v1.xml',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

executable(
  'liars',
  'liar.c',
  'src/server.c',
  'src/workspace.c',
  'src/input.c',
  'src/xdg_view.c',
  'src/view.c',
  'src/layout.c',
  'src/ipc.c',
  'src/command.c',
  'src/x11_view.c',
  'src/layer_view.c',
  'src/pointer_constraint.c',
  'src/config.c',
  'src/parser.c',
  'src/decoration.c',
  layer_shell_header,
  include_directories : incdir,
  dependencies: [wlroots, wayland_server, xkbcommon, libinput, toml]
)

executable(
  'liarsctl',
  'src/liarsctl.c',
  include_directories : incdir,
)
