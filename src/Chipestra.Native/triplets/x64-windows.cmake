# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tom Water Hill
#
# The vcpkg x64-windows triplet (x64, dynamic CRT, dynamic libraries), with one addition: the linker
# writes only the file name of each PDB into the DLL it builds, not the absolute path of the build
# directory on the machine that built it.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_LINKER_FLAGS "/PDBALTPATH:%_PDB%")
