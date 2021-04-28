#can be built using "cmake -GNinja" (to write build files to build-folder use "cmake -GNinja -Bbuild")
#then compiled linked using "cmake --build ." (can be called within build-folder)
set(MDK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/nrfx/mdk)

#minimum required version
cmake_minimum_required(VERSION 3.13)

#set toolchain file for cross-compilation
include(toolchain.cmake)

# set the project name
project(cmake_testapp C ASM)

#set sources for project
set( SRCS
    nrfx/mdk/gcc_startup_nrf52840.S
    nrfx/mdk/system_nrf52840.c
    nrfx/drivers/src/nrfx_uarte.c
    nrfx/drivers/src/nrfx_rng.c
    rand_numbers.c
    main.c
)

add_compile_definitions(NRF52840_XXAA)

# add the executable
add_executable(cmake_testapp ${SRCS})

#include directories for target
target_include_directories(cmake_testapp PRIVATE "modules/hal/cmsis/CMSIS/Core/Include"
                                                 "nrfx"
                                                 "nrfx/mdk"
                                                 "nrfx/drivers/include"
                                                 "nrfx/drivers/src"
                                                 "nrfx_porting"
                                                 "sdk-nrfxlib/softdevice_controller/include"
                                                 "sdk-nrfxlib/mpsl/include"
                                                 "modules/lib/openthread/third_party/NordicSemiconductor/softdevice/s140/headers"
)

#link target using desired linker file
target_link_options(cmake_testapp PRIVATE -B${MDK_INCLUDE_DIR} -T ${MDK_INCLUDE_DIR}/nrf52840_xxaa.ld)

#link with libraries
target_link_libraries(cmake_testapp
                      ${CMAKE_CURRENT_SOURCE_DIR}/sdk-nrfxlib/softdevice_controller/lib/cortex-m4/soft-float/libsoftdevice_controller_multirole.a
                      ${CMAKE_CURRENT_SOURCE_DIR}/sdk-nrfxlib/mpsl/lib/cortex-m4/soft-float/libmpsl.a
                      )
