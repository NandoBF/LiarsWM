project('liarswm', 'c')

wlroots = dependency('wlroots-0.20')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')

incdir = include_directories('include')

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
  include_directories : incdir,
  dependencies: [wlroots, wayland_server, xkbcommon]
)

executable(
  'liarsctl',
  'src/liarsctl.c',
  include_directories : incdir,
)
