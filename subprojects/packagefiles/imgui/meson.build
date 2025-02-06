project(
  'imgui',
  'cpp')

wgpuNative = subproject('wgpu_native')
glfw = subproject('glfw')
wgpu_dep = wgpuNative.get_variable('wgpu_dep')
glfw_dep = glfw.get_variable('glfw_dep')

sources = files(
  './misc/cpp/imgui_stdlib.cpp',
  './imgui.cpp',
  './imgui_demo.cpp',
  './imgui_draw.cpp',
  './imgui_tables.cpp',
  './imgui_widgets.cpp')

sources += './backends/imgui_impl_wgpu.cpp'
sources += './backends/imgui_impl_glfw.cpp'

includes = include_directories('.')

imgui = library('imgui',
  sources,
  dependencies: [wgpu_dep, glfw_dep],
  include_directories: includes,
  cpp_args: ['-DIMGUI_IMPL_WEBGPU_BACKEND_WGPU=1'],
  install: true,
  )

imgui_dep = declare_dependency(
  link_with: [imgui],
  dependencies: wgpu_dep,
  include_directories: includes,
  )
