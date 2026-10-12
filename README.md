# Chipestra

Chipestra (pronounced "chip-es-tra", from "chip" and "orchestra") is a sound
hardware player. It plays the music of classic arcade games by emulating the
original sound hardware (sound CPUs and sound chips) from the game's own ROMs.
Console boards are planned.

Chipestra is not a music composition tool, and it is not a player for VGM or
other pre-rendered music files.

Version: Ver 1.001.00

## Supported Boards & Verified Titles

Chipestra is designed so that additional games on a supported board can be
added by writing an XML definition file (see [docs/xml-format.md](docs/xml-format.md)).
The titles listed below are the ones the author has checked. Other titles are
not verified, even if they run on the same hardware family.

- Sega X-Board: After Burner II, GP Rider, Thunder Blade
- Sega Out Run: Out Run
- Konami Gyruss: Gyruss
- Konami Bubble System: Bubble System BIOS
- Konami GX400: Gradius
- Taito Ninja Warriors: The Ninja Warriors
- Taito F2 System: Metal Black
- Taito Darius: Darius
- Data East Caveman Ninja Hardware: Vapor Trail
- Namco System 1: Dragon Spirit
- Capcom 68000 Based: Street Fighter (US version only: the ROM set "sf"; music, sound effects and voices)

## Features

- Per-channel level meters, pan, mute and solo
- Master volume and level meter
- Selectable output sample rate (automatic by default; in shared mode the
  Windows sound settings take priority)
- WASAPI shared-mode output
- Tracks that end by themselves stop automatically
- Games and track lists are defined in XML files, so you can add or correct
  them yourself (see [docs/xml-format.md](docs/xml-format.md))

## What you need

- Windows 11 (64-bit). Chipestra has only been tested on Windows 11.
- .NET 10 Desktop Runtime (x64), from Microsoft:
  https://dotnet.microsoft.com/download/dotnet/10.0
  If it is not installed, Windows shows a message with a download link when
  you start Chipestra.
- ROM files of the games you want to play. Chipestra does NOT include any
  ROMs, BIOS images or other game data. You must supply your own, and
  point Chipestra at the folder that contains them (Settings > ROM Root).

## Download and run

1. Download Chipestra-1.001.00.zip from the Releases page of this repository.
2. Extract it. You get a folder named Chipestra-1.001.00.
3. Run Chipestra.exe in that folder.
4. Open Settings and set ROM Root to the folder that contains your ROM files.
   Sub-folders are searched too.

The executable is not code-signed, so Windows SmartScreen may show a warning
on first launch (More info > Run anyway).

Settings are saved in %LOCALAPPDATA%\Chipestra\settings.json. To remove
Chipestra, delete the extracted folder and the %LOCALAPPDATA%\Chipestra folder.

## Building from source

Tools (the project is built and tested with these):

- Windows 11 (64-bit) and PowerShell 7 or later
- Visual Studio 2026 (18.x) with the workloads "Desktop development with C++"
  and ".NET desktop development", and the "vcpkg package manager" component
- .NET SDK 10 (the projects target net10.0 / net10.0-windows; built with 10.0.401)
- CMake 3.24 or later, on PATH (CMakeLists.txt requires 3.24; built with 4.4.2)
- vcpkg: the Native build uses vcpkg manifest mode
  (src/Chipestra.Native/vcpkg.json lists libsoxr, pinned by a baseline). The
  build script uses the vcpkg bundled with Visual Studio, or the one that
  VCPKG_ROOT points to. libsoxr is downloaded and built the first time you
  configure, so an internet connection is needed then.

Build and test everything (Release, x64) from a PowerShell prompt in the
repository root:

    .\tools\verify.ps1

This builds the Native library with CMake, runs the Native tests, builds
Chipestra.slnx with dotnet, runs the managed tests and checks that the build
output is consistent. The last check fails if the working tree has
uncommitted or untracked files. The application ends up in
src\Chipestra.App\bin\Release\net10.0-windows\Chipestra.exe.

Build the release package (run verify.ps1 first):

    .\tools\package-release.ps1 -Zip

This creates dist\Chipestra-<version>\ and dist\Chipestra-<version>.zip
(the version comes from version.props).

Tests that need ROM files are optional. Set the environment variable
CHIPESTRA_ROM_ROOT to a folder that contains your ROM files before running
verify.ps1. If it is not set, those tests are skipped.

## Contributing and support

Bug reports are welcome. Code contributions are not accepted for now.

Please do not ask where to get ROMs. Chipestra does not provide ROMs, and
questions about obtaining them will not be answered.

## License

Chipestra is free software, licensed under the GNU General Public License,
version 2 or (at your option) any later version. See [LICENSE](LICENSE).

Third-party code and libraries are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md); their license texts are in
the [licenses/](licenses/) folder. The source code of the bundled libsoxr
library (soxr.dll) is available as a separate download on the Releases page.

Copyright (C) 2026 Tom Water Hill

## Acknowledgments

Chipestra's CPU and sound-chip implementations are ported and adapted from
the work of many developers, especially the MAME project. See
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for the full list of
copyright holders.

## Disclaimer

Chipestra is an independent project. It is not affiliated with or endorsed by
the MAME project or by any game publisher. All game titles and trademarks
belong to their respective owners.
