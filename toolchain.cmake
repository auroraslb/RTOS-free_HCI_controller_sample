set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_TRY_COMPILE_TARGET_TYPE   STATIC_LIBRARY)

#set(CMAKE_AR                        "arm-none-eabi-ar.exe")
#set(CMAKE_ASM_COMPILER              "arm-none-eabi-gcc.exe")
set(CMAKE_C_COMPILER                "arm-none-eabi-gcc.exe")
set(CMAKE_CXX_COMPILER              "arm-none-eabi-g++.exe")
#set(CMAKE_LINKER                    "arm-none-eabi-ld.exe")
#set(CMAKE_OBJCOPY                   "arm-none-eabi-objcopy.exe")
#set(CMAKE_RANLIB                    "arm-none-eabi-ranlib.exe")
#set(CMAKE_SIZE                      "arm-none-eabi-size.exe")
#set(CMAKE_STRIP                     "arm-none-eabi-strip.exe")

set(CMAKE_C_FLAGS                   "-march=armv7e-m -mcpu=cortex-m4 -mfloat-abi=soft -g" CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS                 "${CMAKE_C_FLAGS} -fno-exceptions" CACHE INTERNAL "")

set(CMAKE_C_FLAGS_DEBUG             "-Os -g" CACHE INTERNAL "")
set(CMAKE_C_FLAGS_RELEASE           "-Os -DNDEBUG" CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS_DEBUG           "${CMAKE_C_FLAGS_DEBUG}" CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS_RELEASE         "${CMAKE_C_FLAGS_RELEASE}" CACHE INTERNAL "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_EXECUTABLE_SUFFIX .elf)

#flagg som burde settes -march=armv7e-m -mcpu=cortex-m4 -mfloat-abi=soft