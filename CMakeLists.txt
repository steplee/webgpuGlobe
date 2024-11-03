cmake_minimum_required(VERSION 3.28 FATAL_ERROR)
project(webgpuGlobe)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS_DEBUG "-g -O2")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
# add_definitions(-DWEBGPU_BACKEND_WGPU 1)
# add_definitions(-DWGPU 1)

# set(WEBGPU_BACKEND_DAWN)
set(WEBGPU_BACKEND_WGPU)



include(FetchContent)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)



# ------------------------------------------------------------------------
# wgpu-native
# ------------------------------------------------------------------------

FetchContent_Declare(
        wgpu_native
		URL https://github.com/gfx-rs/wgpu-native/releases/download/v22.1.0.5/wgpu-linux-x86_64-debug.zip
)
FetchContent_MakeAvailable(wgpu_native)
FetchContent_GetProperties(wgpu_native BINARY_DIR WGPU_NATIVE_BINARY_DIR SOURCE_DIR WGPU_NATIVE_SOURCE_DIR)

# ------------------------------------------------------------------------
# spdlog
# ------------------------------------------------------------------------

set(SPDLOG_BUILD_TESTING OFF)
set(SPDLOG_BUILD_EXAMPLES OFF)
set(SPDLOG_BUILD_SHARED ON)
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.x
)
FetchContent_MakeAvailable(spdlog)

# ------------------------------------------------------------------------
# GLFW
# ------------------------------------------------------------------------

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# ------------------------------------------------------------------------
# ImGUI
# ------------------------------------------------------------------------

FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.3
)
FetchContent_MakeAvailable(imgui)
FetchContent_GetProperties(imgui SOURCE_DIR IMGUI_SOURCE_DIR)

# ------------------------------------------------------------------------
# Wrangle packages
# ------------------------------------------------------------------------

find_package(PkgConfig)
pkg_check_modules(gdal REQUIRED gdal)

find_package(spdlog REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs)


message(STATUS "wgpu_native bin: ${WGPU_NATIVE_BINARY_DIR}")
message(STATUS "wgpu_native src: ${WGPU_NATIVE_SOURCE_DIR}")

link_directories(${WGPU_NATIVE_SOURCE_DIR}/lib)
include_directories(${WGPU_NATIVE_SOURCE_DIR}/include)
include_directories(${WGPU_NATIVE_SOURCE_DIR}/include/webgpu)

add_library(webgpu SHARED IMPORTED)
set_target_properties(webgpu PROPERTIES IMPORTED_LOCATION ${WGPU_NATIVE_SOURCE_DIR}/lib/libwgpu_native.so)

# ------------------------------------------------------------------------
# Build targets
# ------------------------------------------------------------------------

# ------------------------------------------------------------------------
# Imgui

add_library(imgui
	SHARED
	${IMGUI_SOURCE_DIR}/imgui.cpp
	${IMGUI_SOURCE_DIR}/imgui_draw.cpp
	${IMGUI_SOURCE_DIR}/imgui_tables.cpp
	${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
	${IMGUI_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
	${IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_SOURCE_DIR})
target_compile_definitions(imgui PUBLIC -DIMGUI_IMPL_WEBGPU_BACKEND_WGPU=1)
target_link_libraries(imgui PUBLIC webgpu glfw)

# ------------------------------------------------------------------------
# wglobe

add_library(wglobe
	SHARED
	src/glfw/glfw3webgpu.cc

	src/app/wrappers.cc
	src/app/app.cc
	src/app/simpleApp.cc
	src/app/shader.cc

	src/geo/conversions.cc

	src/entity/simpleTri.cc
	src/entity/simpleTri2.cc
	src/entity/ellipsoid.cc
	src/entity/sky.cc
	
	src/entity/camera.cc
	src/entity/globe/globe.cc
	src/entity/globe/bbox_entity.cc
	src/entity/globe/tiff/tiff.cc
	src/entity/globe/tiff/makeObbFile.cc

	src/util/options.cc
	src/util/gdalDataset.cc

)
target_link_libraries(wglobe PUBLIC webgpu glfw imgui spdlog::spdlog Eigen3::Eigen ${OpenCV_LIBRARIES} ${gdal_LIBRARIES})
target_include_directories(wglobe PRIVATE ${gdal_INCLUDE_DIRS})

add_executable(mainStandalone src/mainStandalone.cc)
target_link_libraries(mainStandalone wglobe)
add_executable(main src/main.cc)
target_link_libraries(main wglobe)
add_executable(testAlign src/util/testAlign.cc)
target_link_libraries(testAlign wglobe)
