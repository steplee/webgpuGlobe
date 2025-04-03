project('wgpuNative', ['c', 'cpp'])

wgpu = meson.get_compiler('c').find_library('wgpu_native', dirs: [meson.current_source_dir() / 'lib'])
wgpu_dep = declare_dependency(
  dependencies: wgpu,
  # include_directories: include_directories(meson.current_source_dir() / 'include')
  include_directories: include_directories(
    './include',
    './include/webgpu',
    )
)

lib_path = 'lib/libwgpu_native.so'
install_data(lib_path, install_dir : get_option('libdir'))
