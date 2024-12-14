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
	'webgpuGlobe/glfw/glfw3webgpu.cc',

	'webgpuGlobe/app/wrappers.cc',
	'webgpuGlobe/app/app.cc',
	'webgpuGlobe/app/simpleApp.cc',
	'webgpuGlobe/app/shader.cc',

	'webgpuGlobe/geo/conversions.cc',
    
	'webgpuGlobe/camera/camera.cc',
	'webgpuGlobe/camera/globe_camera.cc',
	'webgpuGlobe/camera/orthographic_camera.cc',

	'webgpuGlobe/entity/renderable.cc',
	'webgpuGlobe/entity/simpleTri.cc',
	'webgpuGlobe/entity/simpleTri2.cc',
	'webgpuGlobe/entity/ellipsoid.cc',
	'webgpuGlobe/entity/sky.cc',
	
	'webgpuGlobe/entity/globe/globe.cc',
	'webgpuGlobe/entity/globe/cast.cc',
	'webgpuGlobe/entity/globe/bbox_entity.cc',

	'webgpuGlobe/entity/fog/fog.cc',
	'webgpuGlobe/entity/deferredCast/deferredCast.cc',

	'webgpuGlobe/entity/globe/tiff/tiff.cc',
	'webgpuGlobe/entity/globe/tiff/makeBbFile.cc',
	'webgpuGlobe/entity/globe/tiff/gpu/resources.cc',

	'webgpuGlobe/entity/primitive/primitive.cc',
	'webgpuGlobe/entity/primitive/textured_primitive.cc',
	'webgpuGlobe/entity/thickPrimitive/line.cc',
	'webgpuGlobe/entity/thickPrimitive/point.cc',

	'webgpuGlobe/util/options.cc',
	'webgpuGlobe/util/gdalDataset.cc',
    )
  ,
  dependencies: [wgpu_dep, imgui_dep, glfw_dep, spdlog_dep, eigen_dep, gdal_dep, opencv_dep],
  include_directories: include_directories('./webgpuGlobe'),
  )

wglobe_dep = declare_dependency(
  link_with: wglobe,
  dependencies: [wgpu_dep, imgui_dep, glfw_dep, spdlog_dep, eigen_dep, gdal_dep, opencv_dep],
  include_directories: include_directories('./'))


main = executable('main', files('webgpuGlobe/main.cc'), dependencies: wglobe_dep)
