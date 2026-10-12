libsoxr source for Chipestra @VERSION@
=====================================

This archive contains the source that corresponds to the soxr.dll bundled with
Chipestra @VERSION@ (the SoX Resampler library, "libsoxr", version 0.1.3).

Contents
--------
soxr-0.1.3-Source.tar.xz
    The original libsoxr 0.1.3 source distribution from
    https://sourceforge.net/projects/soxr/ (unmodified).
    SHA-512: f4883ed298d5650399283238aac3dbe78d605b988246bea51fa343d4a8ce5ce97c6e143f6c3f50a3ff81795d9c19e7a07217c586d4020f6ced102aceac46aaa8
    SHA-256: b111c15fdc8c029989330ff559184198c161100a59312f5dc19ddeb9b5a15889

vcpkg-port-soxr/
    The vcpkg "soxr" port, version 0.1.3, port-version 9, from the vcpkg
    baseline a1cae005c39be7b18ba319fced856b68d7276271
    (https://github.com/microsoft/vcpkg/tree/a1cae005c39be7b18ba319fced856b68d7276271/ports/soxr):
      portfile.cmake                          build instructions
      vcpkg.json                              port manifest
      001_initialize-resampler.patch          variable initialization
      002_disable_warning.patch               suppress a warning in pffft.c
      003_detect_arm.patch                    ARM detection in pffft.c
      004_install-pkgconfig-windows.patch     installation settings of the pkg-config file

How soxr.dll was built
----------------------
The original source was extracted, the four patches above were applied, and the
library was built as a shared library (x64-windows triplet, Release) with
vcpkg, following the steps in portfile.cmake (CMake options: BUILD_TESTS=OFF,
BUILD_EXAMPLES=OFF, BUILD_SHARED_RUNTIME=ON, no OpenMP, no LSR bindings).

License
-------
libsoxr is licensed under the GNU Lesser General Public License, version 2.1 or
later (LGPL-2.1-or-later). The full license text is in the LICENCE file inside
soxr-0.1.3-Source.tar.xz, and in the licenses/LGPL-2.1.txt file that is
included with Chipestra. The library includes PFFFT / FFTPACK (BSD-style
license, see the same source).

Chipestra links soxr.dll dynamically; you can replace soxr.dll with a
compatible build of the same name and version.