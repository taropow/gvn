# Pinned official source, statically linked; no download or system codec is needed.
set(BUILD_SHARED_LIBS OFF)
set(WEBP_LINK_STATIC ON CACHE BOOL "" FORCE)
foreach(option ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO WEBPMUX EXTRAS WEBP_JS FUZZTEST)
  set(WEBP_BUILD_${option} OFF CACHE BOOL "" FORCE)
endforeach()
# The mux library writes output resolution metadata without copying source orientation.
set(WEBP_BUILD_LIBWEBPMUX ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/libwebp ${CMAKE_BINARY_DIR}/webp EXCLUDE_FROM_ALL)
target_sources(gvn_saving PRIVATE src/webp_save.cpp)
target_include_directories(gvn_saving PRIVATE third_party/libwebp/src)
target_link_libraries(gvn_saving PRIVATE webp libwebpmux)
