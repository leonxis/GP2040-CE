# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wireless_tx_2354)

# ---------------------------------------------------------------------------
# Post-build: collect the three independently-flashable images into
# build-esp/images/ so later iterations only need to reflash app.bin.
#   bootloader.bin       -> 0x1000
#   partition-table.bin  -> 0x8000
#   app.bin              -> 0x10000
# ESP-IDF never emits a merged image; the root <project>.bin is app-only.
# ---------------------------------------------------------------------------
if(TARGET app_check_size)
    set(_img_dir ${CMAKE_BINARY_DIR}/images)
    add_custom_target(collect_images ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_img_dir}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin
                ${_img_dir}/bootloader.bin
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_BINARY_DIR}/partition_table/partition-table.bin
                ${_img_dir}/partition-table.bin
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
                ${_img_dir}/app.bin
        COMMENT "Collecting split images (bootloader/partition-table/app) into ${_img_dir}"
        DEPENDS app_check_size)
endif()
