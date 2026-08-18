cmake_minimum_required(VERSION 3.16)
project(rk3576_rtsp_terminal C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(LIVE555_DIR ${CMAKE_SOURCE_DIR}/third_party/live555)

add_library(rk3576_common STATIC
    common/frame_queue.c
    common/ini_config.c
    common/process_supervisor.c
    common/test_pattern.c
    common/mpp_codec.c
    common/net_init.c
    common/camera_release.c
)
target_include_directories(rk3576_common PUBLIC ${CMAKE_SOURCE_DIR}/common)
target_link_libraries(rk3576_common PUBLIC rockchip_mpp)

add_library(rk3576_display STATIC common/display_rga.c common/display_output.c)
target_include_directories(rk3576_display PUBLIC ${CMAKE_SOURCE_DIR}/common /usr/include)
target_link_libraries(rk3576_display PUBLIC rga pthread)

find_path(DRM_INCLUDE_DIR NAMES xf86drm.h)
find_path(DRM_LIBDRM_INCLUDE_DIR NAMES drm.h PATH_SUFFIXES libdrm)
find_library(DRM_LIBRARY drm)
if(DRM_INCLUDE_DIR AND DRM_LIBDRM_INCLUDE_DIR AND DRM_LIBRARY)
    target_include_directories(rk3576_display PRIVATE ${DRM_INCLUDE_DIR} ${DRM_LIBDRM_INCLUDE_DIR})
    target_link_libraries(rk3576_display PUBLIC ${DRM_LIBRARY})
    target_compile_definitions(rk3576_display PUBLIC RK3576_HAVE_DRM=1)
    message(STATUS "rk3576_display: libdrm enabled")
else()
    message(STATUS "rk3576_display: libdrm not found, YUV dump only")
endif()

add_executable(test_pattern tools/test_pattern/main.c)
target_link_libraries(test_pattern PRIVATE rk3576_common)

add_executable(shm_dump tools/shm_dump/main.c)
target_link_libraries(shm_dump PRIVATE rk3576_common)

add_executable(rtsp_channel preview/rtsp_channel_main.c)
target_link_libraries(rtsp_channel PRIVATE rk3576_common)

add_executable(player preview/player_main.c)
target_link_libraries(player PRIVATE rk3576_common rk3576_display pthread)

add_executable(preview_supervisor preview/main.c)
target_link_libraries(preview_supervisor PRIVATE rk3576_common)

add_executable(encoder publisher/encoder_main.c)
target_link_libraries(encoder PRIVATE rk3576_common)

add_executable(publisher_supervisor publisher/main.c)
target_link_libraries(publisher_supervisor PRIVATE rk3576_common)

include(ExternalProject)
ExternalProject_Add(live555_build
    SOURCE_DIR ${LIVE555_DIR}
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir ${LIVE555_DIR} ./genMakefiles linux-no-openssl
    BUILD_COMMAND ${CMAKE_COMMAND} -E chdir ${LIVE555_DIR} ${CMAKE_MAKE_PROGRAM} -j4
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
        ${LIVE555_DIR}/liveMedia/libliveMedia.a
        ${LIVE555_DIR}/groupsock/libgroupsock.a
        ${LIVE555_DIR}/BasicUsageEnvironment/libBasicUsageEnvironment.a
        ${LIVE555_DIR}/UsageEnvironment/libUsageEnvironment.a
)

add_library(live555_media STATIC IMPORTED GLOBAL)
add_dependencies(live555_media live555_build)
set_target_properties(live555_media PROPERTIES
    IMPORTED_LOCATION ${LIVE555_DIR}/liveMedia/libliveMedia.a)

add_library(live555_groupsock STATIC IMPORTED GLOBAL)
add_dependencies(live555_groupsock live555_build)
set_target_properties(live555_groupsock PROPERTIES
    IMPORTED_LOCATION ${LIVE555_DIR}/groupsock/libgroupsock.a)

add_library(live555_basic STATIC IMPORTED GLOBAL)
add_dependencies(live555_basic live555_build)
set_target_properties(live555_basic PROPERTIES
    IMPORTED_LOCATION ${LIVE555_DIR}/BasicUsageEnvironment/libBasicUsageEnvironment.a)

add_library(live555_usage STATIC IMPORTED GLOBAL)
add_dependencies(live555_usage live555_build)
set_target_properties(live555_usage PROPERTIES
    IMPORTED_LOCATION ${LIVE555_DIR}/UsageEnvironment/libUsageEnvironment.a)

add_executable(rtsp_server publisher/rtsp_server_main.cpp)
add_dependencies(rtsp_server live555_build)
target_compile_definitions(rtsp_server PRIVATE NO_OPENSSL=1)
target_include_directories(rtsp_server PRIVATE
    ${LIVE555_DIR}/liveMedia/include
    ${LIVE555_DIR}/groupsock/include
    ${LIVE555_DIR}/BasicUsageEnvironment/include
    ${LIVE555_DIR}/UsageEnvironment/include
    ${LIVE555_DIR}/mediaServer/include
)
target_link_libraries(rtsp_server PRIVATE
    rk3576_common
    live555_media
    live555_groupsock
    live555_basic
    live555_usage
)

install(TARGETS test_pattern shm_dump rtsp_channel player preview_supervisor
              encoder rtsp_server publisher_supervisor
        RUNTIME DESTINATION bin)
install(DIRECTORY config/ DESTINATION config)
