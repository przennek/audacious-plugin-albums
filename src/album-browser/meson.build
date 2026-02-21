taglib_dep = dependency('taglib', version: '>= 1.9', required: true)

shared_module('album-browser',
  'album-browser.cc',
  'metadata.cc',
  'scanner.cc',
  dependencies: [audacious_dep, gtk_dep, audgui_dep, gio_dep, taglib_dep],
  name_prefix: '',
  install: true,
  install_dir: general_plugin_dir
)
