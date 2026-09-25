# SPDX-License-Identifier: GPL-3.0-or-later
#
# CMake toolchain for the TC002: ARMv7-A, hard float, Linux.
#
# Targets the SigmaStar SSD21x / Cortex-A7 described in the blueprint. Deliberately
# describes only the *architecture*, not the vendor runtime: nothing here knows
# about FlyThings, because stipple_core must cross-compile without any of that
# existing. Being able to build the core for ARM is what proves the §53 boundary
# holds, and that proof should not wait on a device adapter.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7-a)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# -mcpu rather than -march: the Cortex-A7 has a VFPv4 FPU and NEON, and naming
# the core lets the compiler schedule for it. Hard float matches Debian's armhf
# ABI and, by the blueprint's account of the platform, the device's.
# -Wno-psabi silences GCC's notes about an ARM argument-passing change in 7.1.
# They fire on any std::vector iterator and say nothing about this code: the
# change only matters when linking objects built by compilers from either side
# of that boundary, and everything here is built by one.
set(STIPPLE_ARM_FLAGS "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -Wno-psabi")
set(CMAKE_C_FLAGS_INIT "${STIPPLE_ARM_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${STIPPLE_ARM_FLAGS}")

# Look for libraries and headers in the sysroot, but find programs on the host:
# without this, CMake tries to run ARM binaries during configuration checks.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The device has an unmeasured RAM budget and an 8 MiB res partition, so size is
# a constraint rather than a preference. -ffunction-sections with --gc-sections
# lets the linker drop everything unreferenced, which matters most for a core
# that carries features a given build may not use.
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-Os -DNDEBUG -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS_RELEASE_INIT "-Os -DNDEBUG -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections")
