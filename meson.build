project('webgpuGlobe', ['c', 'cpp'],
    default_options : [
      'cpp_std=c++17',
      'buildtype=debugoptimized',
      'optimization=2',
      'python.platlibdir=@0@/lib/python3/dist-packages/'.format(get_option('prefix')),
      'python.purelibdir=@0@/lib/python3/dist-packages/'.format(get_option('prefix')),
      ]
    )

#
# Make wgpuNative from scratch. The wrap file downloads the release from github then provides a dependency linking to it.
#
# For imgui I used wrapdb. But it did not work out of box because we must use the `wgpu_dep` or build will fail.
# So provide our own meson.build as a patch.
#
# First time using meson patches. Pretty easy but they go in `subprojects/packagefiles` and it took a few minutes
# to get used to it.
# Did not like at first, but I guess the end result is pretty good in fact.
#

eigen_dep = dependency('eigen3')
gdal_dep = dependency('gdal')
opencv_dep = dependency('opencv4')

# dependency('wgpu_native')
wgpuNative = subproject('wgpu_native')
wgpu_dep = wgpuNative.get_variable('wgpu_dep')

spdlog = subproject('spdlog')
glfw = subproject('glfw')
imgui = subproject('imgui', default_options: [
    # 'webgpu=enabled',
  ])

glfw_dep = glfw.get_variable('glfw_dep')
spdlog_dep = spdlog.get_variable('spdlog_dep')

imgui_dep = imgui.get_variable('imgui_dep')

wglobe = shared_library('wglobe',
  files(
	'src/glfw/glfw3webgpu.cc',

	'src/app/wrappers.cc',
	'src/app/app.cc',
	'src/app/simpleApp.cc',
	'src/app/shader.cc',

	'src/geo/conversions.cc',

	'src/entity/simpleTri.cc',
	'src/entity/simpleTri2.cc',
	'src/entity/ellipsoid.cc',
	'src/entity/sky.cc',
	
	'src/entity/camera.cc',
	'src/entity/globe/globe.cc',
	'src/entity/globe/cast.cc',
	'src/entity/globe/bbox_entity.cc',
	'src/entity/globe/fog.cc',

	'src/entity/globe/tiff/tiff.cc',
	'src/entity/globe/tiff/makeBbFile.cc',
	'src/entity/globe/tiff/gpu/resources.cc',

	'src/util/options.cc',
	'src/util/gdalDataset.cc',
    )
  ,
  dependencies: [wgpu_dep, imgui_dep, glfw_dep, spdlog_dep, eigen_dep, gdal_dep, opencv_dep],
  include_directories: include_directories('./src'),
  )

wglobe_dep = declare_dependency(
  link_with: wglobe,
  dependencies: [wgpu_dep, imgui_dep, glfw_dep, spdlog_dep, eigen_dep, gdal_dep, opencv_dep],
  include_directories: include_directories('./src'))


main = executable('main', files('src/main.cc'), dependencies: wglobe_dep)
