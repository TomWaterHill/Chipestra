# Chipestra and Third-Party License Notices

## Chipestra

Copyright (C) 2026 Tom Water Hill

Chipestra is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

Chipestra is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

Chipestra is licensed under GPL-2.0-or-later (the GNU General Public License version 2, or, at your option, any later version). The full text of the GPL-2.0 is in the bundled LICENSE file.

## External code and libraries

The following external code and libraries are used by Chipestra. The full texts of the BSD-3-Clause, MIT, LGPL-2.1 and FFTPACK licenses are in the bundled licenses/ folder (BSD-3-Clause.txt, MIT.txt, LGPL-2.1.txt, FFTPACK.txt). The full text of the GPL-2.0 is in LICENSE. The full texts are not repeated in this document.

### CPUs and sound chips derived from MAME

All of the following come from MAME (https://github.com/mamedev/mame), tag mame0289. "Original implementation" for each item gives the path inside that repository. Each item was ported and adapted for Chipestra from the original implementation. All are licensed under BSD-3-Clause (as stated in the license: line at the top of each file). The full text of BSD-3-Clause is in licenses/BSD-3-Clause.txt.

#### Z80
- Copyright holders (as listed in the original files): Juergen Buchmueller (z80.cpp, z80.h). Juergen Buchmueller, Andrei I. Holub (z80.lst)
- Original implementation (path in mame0289): src/devices/cpu/z80/z80.cpp, z80.h, z80.lst

#### 68000
- Copyright holders (as listed in the original files): Karl Stenerud (m68kcpu.cpp, m68kcpu.h, m68kmusashi.h; the text of these files contains "MUSASHI … Copyright Karl Stenerud"). Olivier Galibert (m68000.h)
- Original implementation (path in mame0289): src/devices/cpu/m68000/m68kcpu.cpp, m68kcpu.h, m68kmusashi.h, m68000.h

#### MC6809E
- Copyright holders (as listed in the original files): Nathan Woods (m6809.cpp, m6809.h, m6809inl.h, m6809.lst; the text of these files contains "Copyright John Butler"). Olivier Galibert (m6809make.py)
- Original implementation (path in mame0289): src/devices/cpu/m6809/m6809.cpp, m6809.h, m6809inl.h, m6809.lst, m6809make.py

#### HD63701 (m6800 family)
- Copyright holders (as listed in the original files): Aaron Giles
- Original implementation (path in mame0289): src/devices/cpu/m6800/m6800.cpp, m6800.h, m6801.cpp, m6801.h, 6800ops.hxx

#### H6280
- Copyright holders (as listed in the original files): Bryan McPhail (the text of the files contains "Copyright Bryan McPhail, mish@tendril.co.uk")
- Original implementation (path in mame0289): src/devices/cpu/h6280/h6280.cpp, h6280.h
- Note: the H6280 source states that it is based, with permission, on Juergen Buchmueller's 6502 emulator.

#### i8039 (mcs48)
- Copyright holders (as listed in the original files): Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud (the text of the files contains "Copyright Mirko Buffoni" and "Based on the original work Copyright Dan Boris")
- Original implementation (path in mame0289): src/devices/cpu/mcs48/mcs48.cpp, mcs48.h

#### AY-3-8910
- Copyright holders (as listed in the original files): Couriersud
- Original implementation (path in mame0289): src/devices/sound/ay8910.cpp, ay8910.h

#### K005289
- Copyright holders (as listed in the original files): Bryan McPhail
- Original implementation (path in mame0289): src/devices/sound/k005289.cpp, k005289.h

#### VLM5030
- Copyright holders (as listed in the original files): Tatsuyuki Satoh (vlm5030.cpp, vlm5030.h). Frank Palazzolo, Couriersud, Jonathan Gevaryahu (tms5110r.hxx)
- Original implementation (path in mame0289): src/devices/sound/vlm5030.cpp, vlm5030.h, tms5110r.hxx

#### SegaPCM
- Copyright holders (as listed in the original files): Hiromitsu Shioya, Olivier Galibert
- Original implementation (path in mame0289): src/devices/sound/segapcm.cpp, segapcm.h

#### MSM5205
- Copyright holders (as listed in the original files): Aaron Giles
- Original implementation (path in mame0289): src/devices/sound/msm5205.cpp, msm5205.h

#### OKIM6295
- Copyright holders (as listed in the original files): Mirko Buffoni, Aaron Giles (okim6295.cpp). Andrew Gardner, Aaron Giles (okiadpcm.cpp)
- Original implementation (path in mame0289): src/devices/sound/okim6295.cpp, okiadpcm.cpp

#### CUS30 (Namco)
- Copyright holders (as listed in the original files): Nicola Salmoria, Aaron Giles
- Original implementation (path in mame0289): src/devices/sound/namco.cpp

#### PC060HA / TC0140SYT
- Copyright holders (as listed in the original files): Philip Bennett
- Original implementation (path in mame0289): src/mame/shared/taitosnd.cpp, taitosnd.h

### MAME drivers (referenced)

The sources of the following MAME drivers were read, and the ROM layout, memory map, clocks and chip connections were independently rewritten for Chipestra. This list gives the source of the facts that were referenced. Parts that were copied directly are described in the notes below.

#### src/mame/sega/segaxbd.cpp
- License: BSD-3-Clause
- Copyright holders (as listed in the original files): Aaron Giles
- Board referenced: X-Board

#### src/mame/sega/segaorun.cpp
- License: BSD-3-Clause
- Copyright holders (as listed in the original files): Aaron Giles
- Board referenced: Out Run

#### src/mame/konami/nemesis.cpp
- License: BSD-3-Clause
- Copyright holders (as listed in the original files): Bryan McPhail
- Boards referenced: Gradius, Bubble System

#### src/mame/konami/gyruss.cpp
- License: BSD-3-Clause
- Copyright holders (as listed in the original files): Nicola Salmoria, Couriersud
- Board referenced: Gyruss

#### src/mame/namco/namcos1.cpp
- License: BSD-3-Clause
- Copyright holders (as listed in the original files): Ernesto Corvi
- Board referenced: System 1 (Dragon Spirit)

#### src/mame/taito/darius.cpp
- License: GPL-2.0+
- Copyright holders (as listed in the original files): David Graves, Jarek Burczynski
- Board referenced: Darius

#### src/mame/taito/ninjaw.cpp
- License: BSD-3-Clause
- Copyright holders (as listed in the original files): David Graves
- Board referenced: Ninja Warriors

#### src/mame/taito/taito_f2.cpp
- License: BSD-3-Clause
- Copyright holders (as listed in the original files): David Graves, Bryan McPhail, Brad Oliver, Andrew Prime, Brian Troha, Nicola Salmoria
- Board referenced: Metal Black (F2 System)

#### src/mame/dataeast/vaportra.cpp
- License: BSD-3-Clause
- Copyright holders (as listed in the original files): Bryan McPhail
- Board referenced: Vapor Trail

#### src/mame/capcom/sf.cpp
- License: BSD-3-Clause
- Copyright holders (as listed in the original files): Olivier Galibert
- Board referenced: Street Fighter (Capcom 68000 Based)

Parts copied directly (notes):

- The sound-side memory maps, the I/O ports, the clocks, the 8000 Hz interrupt of the second Z80 and the routing gain values (YM2151 0.60, MSM5205 1.0) of the Street Fighter board are ported as-is from sf.cpp.
- The routing gain values of the X-Board and Out Run devices are copied as-is from the values in segaxbd.cpp and segaorun.cpp.
- The Gradius speech_w handling is ported as-is from the handling in nemesis.cpp.
- The explanatory comments about ld1_w/ld2_w of the K005289 reuse the wording of the comments in k005289.cpp.

### ymfm, libsoxr, pffft, miniz, pugixml

#### ymfm
- Copyright notice: Copyright (c) 2021, Aaron Giles
- License: BSD-3-Clause
- Source: https://github.com/aaronsgiles/ymfm (3rdparty/ymfm bundled with MAME mame0289)
- How Chipestra uses it: the source, identical to the one bundled with MAME mame0289, is compiled into Chipestra (used for YM2151, YM2203 and YM2610)

#### libsoxr (SoX Resampler Library)
- Copyright notice: SoX Resampler Library Copyright (c) 2007-18 robs@users.sourceforge.net
- License: LGPL-2.1-or-later
- Source: https://sourceforge.net/projects/soxr/ (soxr-0.1.3-Source.tar.xz). vcpkg soxr 0.1.3#9
- How Chipestra uses it: dynamically linked (soxr.dll is bundled)

#### pffft / FFTPACK (included in libsoxr)
- Copyright notice: Copyright (c) 2013 Julien Pommier (pommier@modartt.com). Copyright (c) 2004 the University Corporation for Atmospheric Research ("UCAR")
- License: FFTPACK license
- Source: the libsoxr source (src/pffft.c)
- How Chipestra uses it: included in libsoxr (inside soxr.dll)

#### miniz
- Copyright notice: Copyright 2013-2014 RAD Game Tools and Valve Software. Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC
- License: MIT
- Source: https://github.com/richgel999/miniz (tag 3.1.1)
- How Chipestra uses it: the unmodified source is compiled into Chipestra (used only to decompress data inside ZIP files; miniz_export.h is a header generated by CMake and is not part of the upstream tag)

#### pugixml
- Copyright notice: Copyright (C) 2006-2025, by Arseny Kapoulkine. The source also notes: "This work is based on the pugxml parser, which is: Copyright (C) 2003, by Kristen Wegner"
- License: MIT
- Source: https://github.com/zeux/pugixml (1.15)
- How Chipestra uses it: the unmodified source is compiled into Chipestra (used to read XML)

About libsoxr:

- Chipestra bundles a prebuilt soxr.dll in the application folder, next to Chipestra.exe, and uses it through dynamic linking.
- soxr.dll can be replaced with a compatible soxr.dll of the same name and version.
- The original source is the SourceForge distribution file listed above (vcpkg downloads the same file and builds it).
- soxr.dll is built from source that includes four minor patches applied by the vcpkg soxr port (0.1.3#9): variable initialization, suppression of a warning in pffft.c, ARM detection in pffft.c, and the installation settings of the pkg-config file.
- The four patches and the build files (portfile.cmake and vcpkg.json) are in the vcpkg soxr port: https://github.com/microsoft/vcpkg/tree/a1cae005c39be7b18ba319fced856b68d7276271/ports/soxr (vcpkg baseline a1cae005c39be7b18ba319fced856b68d7276271)
- The source that corresponds to the soxr.dll distributed with Chipestra (the original source, the four vcpkg patches, and the vcpkg portfile.cmake and vcpkg.json) is also provided as a separate attachment, libsoxr-source-for-Chipestra-<version>.zip, on the Chipestra GitHub Releases page. Each Release carries the attachment for its own version (<version> is the version of that Release, for example the one in the name of the Chipestra zip file).
- The full text of the LGPL-2.1 is in licenses/LGPL-2.1.txt.
- The full text of the FFTPACK license is in licenses/FFTPACK.txt.

## Chipestra data files

The game definition files in the xml/ folder were created by the author of Chipestra. They are distributed under the same license as Chipestra, GPL-2.0-or-later. The ROM file names and checksums in them are derived from MAME's ROM lists. No ROM data is included.
