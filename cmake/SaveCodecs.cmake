# Static, fixed official sources. The JPEG project requires an external build.
include(ExternalProject)
set(gvn_jpeg_source "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjpeg-turbo")
set(gvn_jpeg_binary "${CMAKE_CURRENT_BINARY_DIR}/save-codecs/jpeg")
file(MAKE_DIRECTORY "${gvn_jpeg_binary}")
ExternalProject_Add(gvn_jpeg_build
  SOURCE_DIR "${gvn_jpeg_source}"
  BINARY_DIR "${gvn_jpeg_binary}"
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  CMAKE_ARGS
    -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF
    -DWITH_TOOLS=OFF -DWITH_TESTS=OFF -DWITH_SIMD=OFF -DWITH_CRT_DLL=OFF
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target jpeg-static --parallel 4
  BUILD_BYPRODUCTS "${gvn_jpeg_binary}/$<CONFIG>/jpeg-static.lib"
  INSTALL_COMMAND "")
add_library(gvn_jpeg STATIC IMPORTED GLOBAL)
set_target_properties(gvn_jpeg PROPERTIES
  IMPORTED_LOCATION "${gvn_jpeg_binary}/$<CONFIG>/jpeg-static.lib"
  IMPORTED_LOCATION_RELEASE "${gvn_jpeg_binary}/Release/jpeg-static.lib"
  IMPORTED_LOCATION_DEBUG "${gvn_jpeg_binary}/Debug/jpeg-static.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${gvn_jpeg_source}/src;${gvn_jpeg_binary}")
add_dependencies(gvn_jpeg gvn_jpeg_build)

# Only the static compression/PNG library targets are built; no install or vendor tests are registered.
set(gvn_zlib_source "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib")
add_library(gvn_zlib STATIC
  "${gvn_zlib_source}/adler32.c" "${gvn_zlib_source}/compress.c" "${gvn_zlib_source}/crc32.c"
  "${gvn_zlib_source}/deflate.c" "${gvn_zlib_source}/inflate.c" "${gvn_zlib_source}/inftrees.c"
  "${gvn_zlib_source}/inffast.c" "${gvn_zlib_source}/trees.c" "${gvn_zlib_source}/uncompr.c"
  "${gvn_zlib_source}/zutil.c")
target_include_directories(gvn_zlib PUBLIC "${gvn_zlib_source}")
target_compile_definitions(gvn_zlib PRIVATE _CRT_SECURE_NO_WARNINGS)
set(gvn_png_source "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpng")
set(gvn_png_binary "${CMAKE_CURRENT_BINARY_DIR}/save-codecs/png")
file(MAKE_DIRECTORY "${gvn_png_binary}")
configure_file("${gvn_png_source}/scripts/pnglibconf.h.prebuilt" "${gvn_png_binary}/pnglibconf.h" COPYONLY)
add_library(gvn_png STATIC
  "${gvn_png_source}/png.c" "${gvn_png_source}/pngerror.c" "${gvn_png_source}/pngget.c"
  "${gvn_png_source}/pngmem.c" "${gvn_png_source}/pngpread.c" "${gvn_png_source}/pngread.c"
  "${gvn_png_source}/pngrio.c" "${gvn_png_source}/pngrtran.c" "${gvn_png_source}/pngrutil.c"
  "${gvn_png_source}/pngset.c" "${gvn_png_source}/pngtrans.c" "${gvn_png_source}/pngwio.c"
  "${gvn_png_source}/pngwrite.c" "${gvn_png_source}/pngwtran.c" "${gvn_png_source}/pngwutil.c")
target_include_directories(gvn_png PUBLIC "${gvn_png_source}" "${gvn_png_binary}")
target_compile_definitions(gvn_png PRIVATE PNG_INTEL_SSE_OPT=0 _CRT_SECURE_NO_WARNINGS)
target_link_libraries(gvn_png PUBLIC gvn_zlib)
set(gvn_gif_source "${CMAKE_CURRENT_SOURCE_DIR}/third_party/giflib")
add_library(gvn_gif STATIC "${gvn_gif_source}/egif_lib.c" "${gvn_gif_source}/gifalloc.c"
  "${gvn_gif_source}/gif_hash.c" "${gvn_gif_source}/gif_err.c" "${gvn_gif_source}/openbsd-reallocarray.c")
target_include_directories(gvn_gif PUBLIC "${gvn_gif_source}")
target_compile_definitions(gvn_gif PRIVATE _CRT_SECURE_NO_WARNINGS)
add_library(gvn_saving src/saving.cpp src/saving_indexed.cpp src/saving_file.cpp src/color_depth.cpp)
target_include_directories(gvn_saving PUBLIC include)
target_compile_definitions(gvn_saving PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(gvn_saving PUBLIC gvn_core PRIVATE gvn_png gvn_jpeg gvn_gif plum windowscodecs ole32)
target_compile_options(gvn_saving PRIVATE /W4 /utf-8 /permissive-)
add_dependencies(gvn_saving gvn_jpeg_build)
