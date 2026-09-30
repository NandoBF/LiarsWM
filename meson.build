project('liarswm', 'c')

wlroots = dependency('wlroots-0.20')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')

incdir = include_directories('include')

executable(
  'liarswm',
  'liar.c',
  'src/server.c',
  'src/workspace.c',
  'src/input.c',
  'src/xdg_view.c',
  'src/workspace.c',
  'src/view.c',
  include_directories : incdir,
  dependencies: [wlroots, wayland_server, xkbcommon]
)
