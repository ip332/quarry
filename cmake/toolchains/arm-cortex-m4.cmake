# CMake toolchain file for a generic ARM Cortex-M4 bare-metal target using
# the GNU Arm Embedded toolchain (arm-none-eabi-gcc).
#
# This targets the Cortex-M4 architecture generically (illustrative
# STM32F446-class hardware profile) -- it is not coupled to any specific
# board, HAL, RTOS, or BSP. See cortex_m/README.md for the standalone
# validation project that consumes this file.
#
# This file intentionally carries only toolchain/architecture selection, not
# project compiler-warning policy (-Wall/-Werror/etc. belong to the
# consuming project's own CMakeLists.txt, not here).
#
# Usage:
#   cmake -S cortex_m -B cortex_m/build \
#       -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/toolchains/arm-cortex-m4.cmake \
#       ...

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# No OS, no startup files/linker script exist yet at CMake's own
# compiler-check time (the consuming project supplies its own vector table,
# Reset_Handler, and linker script) -- a full executable link-check would
# fail here. Checking a static library instead is the standard bare-metal
# CMake toolchain-file workaround.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

find_program(QUARRY_ARM_NONE_EABI_GCC arm-none-eabi-gcc REQUIRED)
find_program(QUARRY_ARM_NONE_EABI_AR arm-none-eabi-ar REQUIRED)
find_program(QUARRY_ARM_NONE_EABI_RANLIB arm-none-eabi-ranlib REQUIRED)
find_program(QUARRY_ARM_NONE_EABI_OBJCOPY arm-none-eabi-objcopy REQUIRED)
find_program(QUARRY_ARM_NONE_EABI_OBJDUMP arm-none-eabi-objdump REQUIRED)
find_program(QUARRY_ARM_NONE_EABI_SIZE arm-none-eabi-size REQUIRED)

set(CMAKE_C_COMPILER "${QUARRY_ARM_NONE_EABI_GCC}")
set(CMAKE_AR "${QUARRY_ARM_NONE_EABI_AR}" CACHE FILEPATH "")
set(CMAKE_RANLIB "${QUARRY_ARM_NONE_EABI_RANLIB}" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${QUARRY_ARM_NONE_EABI_OBJCOPY}" CACHE FILEPATH "")
set(CMAKE_OBJDUMP "${QUARRY_ARM_NONE_EABI_OBJDUMP}" CACHE FILEPATH "")

# Generic Cortex-M4 core selection. Soft-float ABI and no -mfpu flag are
# deliberate: Quarry's generated code and runtime never use floating-point
# hardware capability, plain (non-"F") Cortex-M4 parts exist without an FPU,
# and mixing float ABIs across objects is a common embedded linkage error
# this avoids entirely. Revisit only if a future hard-float-focused
# follow-up needs it.
set(QUARRY_CORTEX_M4_ARCH_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft")
set(CMAKE_C_FLAGS_INIT "${QUARRY_CORTEX_M4_ARCH_FLAGS}")

# newlib-nano (smaller libc) plus stubbed syscalls (_exit/_sbrk/_write/etc.
# as weak no-ops) -- the standard bare-metal newlib pairing. This keeps the
# link from silently pulling in host/Linux facilities.
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${QUARRY_CORTEX_M4_ARCH_FLAGS} --specs=nano.specs --specs=nosys.specs")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
