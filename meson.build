project('liarswm', 'c')

wlroots = dependency('wlroots-0.20')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
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
  layer_shell_header,
  include_directories : incdir,
  dependencies: [wlroots, wayland_server, xkbcommon]
)

executable(
  'liarsctl',
  'src/liarsctl.c',
  include_directories : incdir,
)
