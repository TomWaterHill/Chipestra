# Game definition XML format

Chipestra reads its list of games, ROM requirements and tracks from small XML files. This document
describes how to write or edit them.

## 1. Overview

- One XML file defines **one game**: which sound hardware it runs on, which ROM files that hardware
  needs, and the list of tracks (music and sound effects) shown for it.
- The files live in the `xml` folder next to `Chipestra.exe`, in that folder or in any folder below it. Every
  file with the extension `.xml` is read. Where a file sits has **no effect** on how the game is listed: the
  game list is built from what is written in the file. The files shipped with Chipestra are sorted into
  folders by board (see "Folders of the shipped files" below); your own files can go anywhere in the `xml` folder.
- All files are read once, **when Chipestra starts**. Restart Chipestra after adding or changing a file.
- Files are read in the order of their **path inside the `xml` folder**, written with `/` and compared by
  plain character code (so `Arcade/a.xml` comes before `Arcade/b.xml`, and `A/x.xml` before `B/x.xml`). The
  order matters only when two files use the same game ID (see section 3).
- The game list on screen is grouped as category, manufacturer, system (board), game, and games are
  sorted by their `title`. Tracks are shown in the order they are written in the file.

### Folders of the shipped files

The files shipped in `xml` follow one rule, so that a new file has an obvious place. This is only for tidiness;
the program ignores the folders.

```
xml/Arcade/<maker of the board>/<board>/<file>.xml
xml/Console/<maker of the console>/<console>/<publisher>/<file>.xml
```

- A folder name is the name written without anything except letters and digits (spaces, `-` and other marks
  are removed), keeping the capitals: `Data East` becomes `DataEast`, `68000 Based` becomes `68000Based`,
  `X-Board` becomes `XBoard`, `Caveman Ninja Hardware` becomes `CavemanNinjaHardware`. A leading `The ` of a board name is dropped (`The Ninja Warriors` is
  `NinjaWarriors`).
- The maker and the board (or console) are the ones shown in the game list (section 3); the console's
  publisher is the file's `manufacturer`. The category is `Arcade` or `Console`.
- The repository's tests check that each shipped file is in the folders its content names.

Examples: `Arcade/Sega/XBoard/gprider.xml`, `Arcade/Taito/F2System/metalblk.xml`,
`Console/Nintendo/NES/Hudson/starsoldier.xml`.

## 2. File format

- The file must be **UTF-8**. A byte order mark (BOM) is accepted but not needed.
- The root element must be `<chipestra>`. Any other root element is an error ("no `<chipestra><game>`
  element found").
- An XML declaration (`<?xml version="1.0" encoding="UTF-8"?>`) is allowed but optional. Comments
  (`<!-- ... -->`) are allowed anywhere.
- `<!DOCTYPE ...>` is **not allowed**; a file containing it is rejected.
- The file may not be larger than 4 MiB (4,194,304 bytes).
- Only the first `<game>` element inside `<chipestra>` is used. Put one game in each file.
- Unknown elements and unknown attributes are ignored without any message. A misspelled optional
  attribute (for example `loop="false"`) therefore has no effect and is not reported.
- Attribute names and values are case-sensitive.

## 3. The `<game>` element

| Attribute | Required | Meaning |
|---|---|---|
| `id` | yes | Identifier of the game. Must be unique across all files. At most 63 bytes. |
| `title` | yes | Name shown in the game list. At most 127 bytes of UTF-8. |
| `machine` | yes | The board (sound hardware) the game runs on. See the table below. |
| `variant` | yes | The variant of that board. See the table below. |
| `manufacturer` | no | Arcade: informational only (the manufacturer in the game list comes from the board). Console: the **publisher** of the game, shown as its own level in the game list (see below). |
| `archive` | no | Name of the ROM set, used to find the ROM files (see section 4). At most 63 bytes. |

**Game list levels.** The list is `ARCADE` or `CONSOLE`, then the maker of the hardware, then the system, then the game. For a console game there is one more level between the system and the game, the publisher, taken from `manufacturer`: for example `CONSOLE > Nintendo > NES > Hudson > Star Soldier`. A console game without `manufacturer` (or with an empty one) is listed under a publisher named `Unknown`; it is not an error. Games of one publisher are listed together, publishers in alphabetical order. Arcade games are listed as before and ignore `manufacturer`.

A required attribute that is missing or empty is an error. Values longer than the limits above are
silently cut off.

`machine` and `variant` must match one of the following pairs **exactly** (they are not checked when
the file is read; an unknown value makes the game fail to load when you select a track, with the
message `Failed to load game: NotFound`):

| `machine` | `variant` | Board |
|---|---|---|
| `SEGA_XBOARD` | `XBOARD_BASE` | Sega X-Board (for example After Burner II, GP Rider, Thunder Blade) |
| `SEGA_OUTRUN` | `OUTRUN_BASE` | Sega Out Run hardware |
| `KONAMI_GRADIUS_BUBBLESYSTEM` | `GRADIUS_BASE` | Konami GX400 hardware (Gradius) |
| `KONAMI_BUBBLESYSTEM_GRADIUS` | `BUBBLESYSTEM_BIOS_BASE` | Konami Bubble System, BIOS only (no game cartridge) |
| `KONAMI_BUBBLESYSTEM_GRADIUS` | `BUBBLESYSTEM_GRADIUS_BASE` | Konami Bubble System with the Gradius cartridge (no shipped file uses it) |
| `KONAMI_GYRUSS` | `GYRUSS_BASE` | Konami Gyruss hardware |
| `TAITO_DARIUS` | `DARIUS_BASE` | Taito Darius hardware |
| `TAITO_NINJAWARRIORS` | `NINJAWARRIORS_BASE` | Taito The Ninja Warriors hardware |
| `TAITO_F2SYSTEM` | `METALBLACK_BASE` | Taito F2 System (Metal Black) |
| `DATAEAST_VAPORTRAIL` | `VAPORTRAIL_BASE` | Data East Caveman Ninja hardware (Vapor Trail) |
| `NAMCO_SYSTEM1` | `SYSTEM1_BASE` | Namco System 1 (Dragon Spirit) |
| `CAPCOM_68000` | `SF_BASE` | Capcom 68000 Based (Street Fighter) |
| `NINTENDO_NES` | `NES_BASE` | Nintendo Famicom / NES cartridge (Star Soldier); needs a `<driver>` element, see section 4a |
| `NINTENDO_FDS` | `FDS_BASE` | Famicom Disk System (Metroid); needs a `<driver>` element with disk files, see sections 4a and 4b |

A game whose `machine` is not in this list still appears in the game list, under the manufacturer
"Unknown", but cannot be loaded.

**Duplicate game IDs.** If two files use the same `id`, the file that comes first in path order is
used and the later file is reported as an error that names both files, for example
`game id "gprider" is already defined by Arcade/Sega/XBoard/gprider.xml (My/gprider.xml is ignored)` (section 7).
A file moved to another folder must not be left in its old place as well.

## 4. ROM files: `<resources>` and `<rom>`

Chipestra does not include any game data. You provide the ROM files yourself and tell Chipestra where
they are in **Settings, ROM Root**. The ROM file names and checksums used in the shipped files follow
the ROM lists of the corresponding MAME sets.

`<resources>` contains one `<rom>` element per ROM file the board needs:

```xml
<resources>
  <rom name="soundcpu" file="epr-11112.17" size="65536"  crc32="d777fc6d"/>
  <rom name="pcm1"     file="mpr-10931.11" size="131072" crc32="9209068f"/>
</resources>
```

| Attribute | Required | Meaning |
|---|---|---|
| `name` | yes | The role of the ROM on the board (for example `soundcpu`, `pcm1`). Use exactly the names the board expects (see below). |
| `file` | yes | File name to look for. |
| `archive` | no | The ROM set this one file comes from, when it is not the game's own (`archive` of `<game>`). Used by a board that needs a file of another set, for example the sound chip's table of a VRC7 game. Same rules as `archive` of `<game>` (see below). |
| `size` | no | Expected size in bytes (decimal). |
| `crc32` | no | Expected CRC-32, as hexadecimal digits (for example `d777fc6d`). |
| `sha1` | no | Expected SHA-1, as hexadecimal digits. |

All listed ROMs are required. A `size`, `crc32` or `sha1` value that is not a valid number is read as
0, so nothing will match it.

**How a ROM is found.** When you select a track, Chipestra looks under the ROM Root folder, including
all sub-folders, in loose files and in `.zip` files (stored or deflate-compressed entries):

1. If `archive` is given, only a `.zip` file with that name (without the extension) or files inside a
   folder with that name are considered. Names are compared without regard to upper/lower case. The
   `archive` of the `<rom>` is used if it has one, otherwise the `archive` of the `<game>`. When the
   archive does not exist, the message names it (for example `ds1001.zip`).
2. The file name must equal `file` (without regard to case; for a `.zip`, the name of the entry
   without its folder).
3. If `size`, `crc32` or `sha1` is given, the candidate must match it.
4. If more than one candidate is left, Chipestra does not choose one: the ROM is reported as
   ambiguous. Use `archive`, `size` or `crc32` to tell the candidates apart, or remove the duplicate
   files.
5. A file larger than 64 MiB is not read.

If a ROM cannot be resolved, the game does not load and the message lists each problem, for example
`ROM resolution failed: pcm3: NotFound (no candidate matched an accepted filename for 'pcm3')`. The
possible results are:

- `NotFound`: no file with that name (or none with the given `size`).
- `HashMismatch`: files with that name exist, but none has the given `crc32` / `sha1`.
- `AmbiguousMatch`: more than one candidate is left (the message lists them).
- `Unsupported`: a matching file could not be read (unsupported ZIP compression, or too large).

If the ROM Root is not set at all, the message is `ROM root is not set. Open Settings to configure it
before selecting a track.`

The ROM files are looked up before the board is created, so a wrong `machine` or `variant` is only
reported (as `Failed to load game: NotFound`) once all ROMs have been found.

A `name` that the board does not expect is **not reported**. On the X-Board, for example, a ROM with
a wrong name is simply ignored and that ROM socket stays empty, so the game loads but part of the
sound is missing; some other boards refuse to load. Always use the names in the table below.

**ROM names by board** (the `name` values used by the shipped files):

| Board | `name` values |
|---|---|
| `SEGA_XBOARD` | `soundcpu`, `pcm1`, `pcm2`, `pcm3` |
| `SEGA_OUTRUN` | `soundcpu`, `pcm1` to `pcm6` |
| `KONAMI_GRADIUS_BUBBLESYSTEM` | `soundcpu`, `wave0`, `wave1`, `maincpu_hi`, `maincpu_lo`, `maindata_hi`, `maindata_lo` |
| `KONAMI_BUBBLESYSTEM_GRADIUS` (BIOS only) | `soundcpu`, `wave0`, `wave1` |
| `KONAMI_GYRUSS` | `soundcpu_lo`, `soundcpu_hi`, `audio2` |
| `TAITO_DARIUS` | `soundcpu`, `adpcm` |
| `TAITO_NINJAWARRIORS` | `soundcpu`, `adpcma1`, `adpcma2`, `adpcma3`, `adpcmb` |
| `TAITO_F2SYSTEM` | `soundcpu`, `adpcma`, `adpcmb` |
| `DATAEAST_VAPORTRAIL` | `soundcpu`, `oki1`, `oki2` |
| `NAMCO_SYSTEM1` | `soundcpu0`, `soundcpu1`, `mcu`, `voice0` to `voice4` |
| `NINTENDO_NES` | `cart` (the whole `.nes` file) |
| `NINTENDO_FDS` | `disk` (the whole `.fds` file, with or without the 16-byte header) |

## 4a. Sound routines: `<driver>` (NES and Famicom Disk System)

A game for `NINTENDO_NES` (or `NINTENDO_FDS`, section 4b) has no separate sound CPU: its music is made by routines of the game's own
program. Chipestra does not run the game; it runs only the routines named here, from a few bytes of its
own start-up code, and starts a song or an effect by writing the song number to the game's request
variable. The game's program is never changed. `<driver>` goes between `<resources>` and `<tracks>`.

```xml
<resources>
  <rom name="cart" file="Star Soldier (Japan).nes" size="65552" crc32="7abe32dd"/>
</resources>
<driver ram_clear="0x0000-0x07FF" init="0xB666 0xB809" frame="0xB690 0xB82A"
        bgm="0x95" sfx="0x8C" prg_crc32="e6c4ec06" chr_crc32="67651e57"/>
```

| Attribute | Required | Meaning |
|---|---|---|
| `ram_clear` | no | The part of the console's 2 KB of RAM (`first-last`, inside `0x0000-0x1FFF`) the game clears to zero when it starts. The rest of the RAM starts as `0xFF`. |
| `init` | no | Addresses of the routines the game calls once when it starts (after the RAM is cleared), in order. |
| `frame` | no | Addresses of the routines the game calls once for every video frame (60 times a second), in order. |
| `bgm` | no | RAM address of the variable the game reads to start a song. Needed for ordinary tracks. |
| `sfx` | no | RAM address of the variable the game reads to start a sound effect. Needed for `type="sfx"` tracks. |
| `prg_crc32` | no | CRC-32 (hexadecimal) of the program data in the `.nes` file. If given, a cartridge with other data is rejected. |
| `chr_crc32` | no | The same for the graphics data. |
| `ram_set` | no | Bytes written to the console's RAM after `ram_clear` and before `init`, as `address=value` pairs separated by spaces (for example `0x0028=0x7C 0x0680=0x01`). The address must be below `0x2000`. |
| `warmup_frames` | no | Number of frames the `frame` routines run at start-up, before the first request (1 to 600; default 1). |
| `requests` | no | RAM addresses (below `0x2000`, at most 7) a track may write its value to, for a driver with several request variables. A track names one with `request` (section 5). See "Request variables" below. |
| `request_call` | no | Address of a routine the game calls with the request number in the A register (a sound driver that takes its requests as a call, not through RAM variables). See "Request routine" below. |
| `request_register` | no | `A` (default), `X` or `Y`: the register `request_call` (and `sfx_call`) takes the request number in. Needs `request_call`. |
| `sfx_call` | no | Address of a second routine, called the same way as `request_call`, that a `type="sfx"` track requests its sound effect from; without it effects use `request_call` too. Needs `request_call`. |
| `frame_irq` | no | `off`: the start-up code writes `0x40` to `$4017`, as the game's own reset code does, so that the APU's frame interrupt (enabled at power-on) never reaches the CPU. For a game whose sound routines do not write it themselves. |
| `mmc3_prg_mode` | no | `0` or `1` (default `1`): the PRG mode an MMC3 cartridge is switched in. In mode 1 the `R6` bank is mapped at `$C000` (and a fixed bank at `$8000`), in mode 0 at `$8000` (and a fixed bank at `$C000`). |
| `prg_banks_call`, `prg_banks_idle` | no | For a cartridge with banked program ROM: the banks the game has mapped while its sound routines run (`prg_banks_call`) and the rest of the time (`prg_banks_idle`), each as two numbers (three on a VRC7 or a Namco 163). Both are required together, with the same number of values. The two numbers mean `R6 R7` on an MMC3 (mapper 4), the values of the `$8000` and `$C000` bank registers on a VRC6 (mapper 24 or 26) and the banks of the commands 9 and A on an FME-7 (mapper 69); the three numbers are the banks at `$8000`, `$A000` and `$C000` on a VRC7 (mapper 85) and the values of the bank registers on a Namco 163 (mapper 19). See "MMC3 cartridges", "VRC6 cartridges", "VRC7 cartridges", "Namco 163 cartridges" and "FME-7 / Sunsoft 5B cartridges" below. |
| `vrc7_register_line` | VRC7 only | `A3` or `A4`: the address line that tells the registers of a VRC7 apart (the board's wiring; it is not in the iNES header). Required for a VRC7 cartridge and refused for any other. |
| `n163_channels`, `n163_volume` | Namco 163 only | The number of sound channels shown in the meters (1-8) and the volume number of the board (0-5). Both are required for a Namco 163 cartridge and refused for any other. See "Namco 163 cartridges" below. |
| `frame_counter` | no | RAM address (below `0x2000`) of a byte the generated frame code adds 1 to at the end of every frame, after the `frame` routines and the request. For a driver that reads a counter the game itself keeps in its own frame code. The byte is cleared by `ram_clear` like any other. |

Addresses are hexadecimal with a `0x` prefix or decimal, separated by spaces or commas; there can be at most
60 routines in a list. The `cart` resource is the whole `.nes` file (an iNES header, mapper 0, 3, 4, 19, 24, 26, 69 or 85; mapper 0
and 3 have 16 or 32 KB of program data, mapper 4 has 16 KB times a power of two, up to 512 KB, mapper 24 and 26 have 32, 64, 128 or 256 KB, mapper 85, mapper 69 and mapper 19 have 32, 64, 128, 256 or 512 KB); the resource name must be `cart`. The routines must not need the picture chip or the
controllers to finish; Chipestra answers reads of them with fixed values (no button pressed, and the vertical-blank flag set at the start of every frame).
**The `command` of a song track is the song number written to the `bgm` variable; the `command` of an
`sfx` track is the number written to the `sfx` variable.** Choosing another track starts the game's sound
from a freshly initialized state, so nothing of the previous track remains.

**Request variables.** A driver that reads several variables (one for songs, one for jingles, others for sound
effects of different groups) lists them in `requests`, and each track names the one it writes to with
`request="0x04F5"` (section 5; the same attribute as in section 4b). The `command` of such a track is the value
written to that address, for example one bit of a group of effects. A track without `request` writes to `bgm`.
A game with no `bgm` variable needs a `request` on every track other than the Music Stop (a `control` track, see "Control tracks" below).

**MMC3 cartridges.** The program ROM of an MMC3 game is 8 KB banks, and the game maps different banks at
different times. Chipestra models the bank select and bank data registers (`$8000-$9FFF`, even and odd
addresses; R6 and R7, the PRG mode bit) and the 8 KB of WRAM at `$6000-$7FFF`, which is always usable; the CHR
banks, mirroring, WRAM protect and the scanline IRQ are not modelled. The generated code does what the game's own
sound update does: it maps the `prg_banks_call` banks, calls the routines (with the X register set to 0), and maps
the `prg_banks_idle` banks again. The routines see the first pair; the DMC reads its samples through the second pair
in between, and through the first pair while the routines run. The APU is brought up to date before every write
to the mapper, so each DMC byte is read through the banks that were mapped at that cycle.

```xml
<driver ram_clear="0x0000-0x07FF" frame="0xA000" prg_banks_call="0x1D 0x1C" prg_banks_idle="0x0A 0x1A"
        requests="0x04F1 0x04F2 0x04F3 0x04F4 0x04F5 0x04F6" prg_crc32="bbac200c" chr_crc32="a0ae2b4b"/>
```

**Request routine.** Some drivers are started by a call, not by a RAM variable: the game loads the sound number into the A register and calls
a routine. `request_call="0xE249"` names that routine. The generated frame code calls the `frame` routines and then, in the
same frame, loads the track's `command` into A and calls the routine once, as the game's own code does. A track's
`command` is that number, for songs and for `type="sfx"` tracks alike, `0` included (a track with `command="0x00"` that is not a `control` track requests the number 0); `request` and `set` are not used. The routines are called with the X register set to 0 when the cartridge is an MMC3, a VRC6, a Namco 163 or an FME-7 (and the Y register too on a Namco 163 and an FME-7).

A driver that takes the number in another register says so with `request_register="X"` (or `"Y"`): the generated code then loads that register
from the request (and does not set it to 0 first) before the call. A driver with a separate routine for sound effects names it with
`sfx_call`: a `type="sfx"` track calls that routine, a song track calls `request_call`, both with the number in the register chosen. Without these
two attributes a request is made in A, with `request_call`, as before.

```xml
<driver ram_clear="0x0000-0x07FF" init="0x8000" frame="0x8006" request_call="0x8003" request_register="X" sfx_call="0x8009"
        frame_irq="off" mmc3_prg_mode="0" prg_banks_call="0x02 0x03" prg_banks_idle="0x02 0x03"/>
```

**Control tracks.** A track of `type="control"` (the Music Stop) makes no request, with a `request_call`, a `bgm` variable or a `request` alike: choosing it only starts the Machine again from the freshly initialized state, which silences everything. Every other track makes its request whatever its number.

**VRC6 cartridges.** Konami's VRC6 (mapper 24, or mapper 26 with the two low address lines exchanged) maps a 16 KB bank at
`$8000` (the register at `$8000-$8FFF`, 4 bits) and an 8 KB bank at `$C000` (the register at `$C000-$CFFF`, 5 bits); the
last 8 KB bank is fixed at `$E000`. Chipestra models these registers and the cartridge's three extra sound channels
(two pulse waves and a sawtooth, `$9000-$9003`, `$A000-$A002`, `$B000-$B002`; they are mixed with the console's own sound at the
same level as the original) and nothing else: the CHR banks, the mirroring and PRG RAM register (`$B003`) and the scanline IRQ
are not modelled. `prg_banks_call` and `prg_banks_idle` are the values written to the `$8000` and `$C000` registers:
`prg_banks_call="0x0C 0x1E"` maps the 16 KB bank 12 at `$8000` and the 8 KB bank 30 at `$C000`. A VRC6 game shows 8 channels in
the meters: the console's 5 and the cartridge's 3.

```xml
<driver ram_clear="0x0000-0x07FF" init="0xE151 0xE211" frame="0xE238" request_call="0xE249"
        prg_banks_call="0x0C 0x1E" prg_banks_idle="0x0C 0x1E" prg_crc32="ba6bdd6a" chr_crc32="bf15255c"/>
```

**VRC7 cartridges.** Konami's VRC7 (mapper 85) maps three 8 KB banks, at `$8000`, `$A000` and `$C000` (6 bits each), and fixes the
last 8 KB bank at `$E000`. The registers are told apart by one address line, `A4` on most boards and `A3` on others; the iNES header does
not say which, so the XML does: `vrc7_register_line="A4"` means the registers are at `$8000`, `$8010`, `$9000` and the sound address and data
registers at `$9010` and `$9030`; with `A3` they are at `$8000`, `$8008`, `$9000`, `$9008` and `$9028`. Chipestra models these registers, 8 KB
of WRAM at `$6000-$7FFF` (always usable, cleared at every reset; nothing is saved), and the cartridge's six FM channels (mixed with the
console's own sound at the same level as the original). The mirroring and WRAM register (`$E000`), the CHR banks and the scanline IRQ are not
modelled. `prg_banks_call` and `prg_banks_idle` are three bank numbers: `prg_banks_call="0 1 2"` maps the banks 0, 1 and 2 at `$8000`, `$A000`
and `$C000` while the routines run. A VRC7 game shows 11 channels in the meters: the console's 5 and the cartridge's 6.

The FM chip has 15 built-in instruments in a 144-byte table that is a separate ROM, not part of the cartridge. It is a second resource with
the logical name `instruments`, the file `ds1001_instruments.bin` and the archive `ds1001` (that is, `ds1001.zip` in the ROM Root); a VRC7 game
does not load without it. Unlike the cartridge it does not belong to the game's archive, so its `<rom>` has an `archive` of its own.
A write to the cartridge on the other register line than `vrc7_register_line` is counted, and the tests fail if a driver makes one, so a wrong
`vrc7_register_line` is found by the tests, not by listening.

A driver that keeps a counter in its frame code and reads it in a sound request (the byte is not updated by the sound routines themselves)
needs `frame_counter`: the generated code adds 1 to that byte once for every frame, as the game's own frame code does.

```xml
<resources>
  <rom name="cart" file="Lagrange Point (Japan).nes" size="524304" crc32="ead4dedc"/>
  <rom name="instruments" file="ds1001_instruments.bin" archive="ds1001" size="144" crc32="9d699efc"/>
</resources>
<driver ram_clear="0x0000-0x07FF" init="0x8066" frame="0x8304" request_call="0x80CB"
        prg_banks_call="0 1 2" prg_banks_idle="0 1 2" vrc7_register_line="A4" frame_counter="0x0022"
        prg_crc32="743387ff"/>
```

**Namco 163 cartridges.** Namco's 163 (mapper 19) maps three 8 KB banks, at `$8000`, `$A000` and `$C000` (6 bits each, written to `$E000`,
`$E800` and `$F000`), and fixes the last 8 KB bank at `$E000`. Chipestra models these registers, 8 KB of WRAM at `$6000-$7FFF` (always
usable, cleared at every reset), the chip's sound registers and its wavetable channels, mixed with the console's own sound at the same ratio as the
original: the sound address register (`$F800`, bit 7 = increase the address after every access, bits 6-0 = the chip's internal RAM address) and the sound
data port (`$4800-$4FFF`, which can also be read). Bit 6 of the value written to `$E000` stops the chip's sound. The CHR and nametable registers, the
scanline IRQ (`$5000/$5800`) and the WRAM protect bits are not modelled. `prg_banks_call` and `prg_banks_idle` are three register values:
`prg_banks_call="0x3C 0x3D 0x3E"` writes `0x3C`, `0x3D` and `0x3E` to `$E000`, `$E800` and `$F000` while the routines run. A value larger than the
ROM wraps around its size, as the unconnected address lines of the board do (a 128 KB game has 16 banks, so `0x3C` is bank 12). The routines are called with
both the X and the Y registers set to 0.

The chip makes sound with 1 to 8 channels that share its time, and the game chooses how many it uses; the iNES header does not say, so the XML does.
Two settings are required for a Namco 163 cartridge and refused for any other:

| Attribute | Meaning |
|---|---|
| `n163_channels` | `1` to `8`: the number of channels the meters show. They are the last ones (`4` shows the channels 5 to 8, named `Wave 5` to `Wave 8`). Use the number of channels the game sets up; the others are not shown. |
| `n163_volume` | `0` to `5`: how loud the chip is against the console's own sound, as the number of MAME's submapper for the board (the console's own sound is mixed at 0.90 and the chip at 0.5 times a gain that depends on the number: `0` and `1`: 1.0; `2`: 0, the chip makes no sound; `3`: 1.125; `4`: 1.17; `5`: 1.19). It is a property of the board, not of the file, and is not read from the iNES header. |

A Namco 163 game shows 5 + `n163_channels` channels in the meters.

```xml
<resources>
  <rom name="cart" file="Rolling Thunder (Japan).nes" size="393232" crc32="4837b489"/>
</resources>
<driver ram_clear="0x0000-0x07FF" ram_set="0x00B6=0xFF" init="0xB307" frame="0x8000" bgm="0x00B6" sfx="0x00B6"
        prg_banks_call="0x3C 0x3D 0x3E" prg_banks_idle="0x3C 0x3D 0x3E" n163_channels="4" n163_volume="4"
        prg_crc32="3deac303"/>
```

**FME-7 / Sunsoft 5B cartridges.** Sunsoft's FME-7 (mapper 69) maps three 8 KB banks, at `$8000`, `$A000` and `$C000`, and fixes the last 8 KB bank
at `$E000`. A write to `$8000-$9FFF` selects a command (its low four bits) and a write to `$A000-$BFFF` gives that command its data; the commands 9, A and B
set the three banks. The Sunsoft 5B is the same board with a sound chip: a write to `$C000-$DFFF` selects one of the chip's registers (the low four
bits) and a write to `$E000-$FFFF` writes the selected register. Chipestra models these registers, 8 KB of WRAM at `$6000-$7FFF` (always usable,
cleared at every reset) and the chip's three tone channels, mixed with the console's own sound at the same ratio as the original (the console at 0.90, the
chip at 0.50); the other commands (the CHR banks, the RAM/ROM switch at `$6000`, the mirroring and the scanline IRQ) are not modelled. A 5B game
shows 8 channels in the meters: the console's 5 and the chip's 3 (`Tone 1` to `Tone 3`).

`prg_banks_call` and `prg_banks_idle` are two numbers, the banks of the commands 9 and A: `prg_banks_call="0x07 0x00"` writes `0x07` as the data of
command 9 and `0x00` as the data of command A before every call. **Command B, the bank at `$C000` where the DMC reads its samples, is never written
by Chipestra**: the sound driver chooses it itself, and it starts as bank 0, the value the real game has in it when the sound update is entered. The routines are
called with both the X and the Y registers set to 0. The chip makes one sample for every 16 CPU cycles, stepped to the same sample count as the
original's stream (111860 samples a second); it is brought up to date before every write to the cartridge.

```xml
<driver ram_clear="0x0000-0x07FF" init="0x8000" frame="0x8009" request_call="0x8006"
        prg_banks_call="0x07 0x00" prg_banks_idle="0x07 0x00" prg_crc32="aad7bdc1" chr_crc32="9868acbe"/>
```

## 4b. Disk files: the Famicom Disk System (`NINTENDO_FDS`)

A Disk System game keeps its sound driver and its data in files on the disk. Chipestra does not run the
disk's BIOS or the game: it copies the files the sound driver needs into the RAM adapter's memory, at the
addresses the disk gives for each file, and then calls the driver's entry routine (`frame`, section 4a)
once for every video frame. The BIOS's start-up settings of the sound registers are applied by Chipestra.
The `disk` resource is the whole `.fds` file, with or without the 16-byte header; a plain image is a whole
number of 65,500-byte sides.

The files are named inside `<driver>`, and checked by name and CRC-32 (not by the CRC of the whole image, so a disk
whose save data differs is still accepted):

```xml
<driver ram_clear="0x0000-0x07FF" ram_set="0x0680=0x01" frame="0xDFF3" warmup_frames="20"
        base="MET.HEX" requests="0x0680 0x0681 0x0682 0x0684 0x0685">
  <file name="MET.HEX" crc32="474edcec"/>
  <file name="MMROCK"  crc32="7287c42a"/>
  <file name="STG1PGM" crc32="fb9bf89b"/>
  <set id="rock_stg1" files="MMROCK STG1PGM"/>
  <set id="variant" ram_set="0x0686=0x01"/>
</driver>
```

| Element / attribute | Meaning |
|---|---|
| `<file name="..." crc32="..."/>` | A file of the disk (its name as stored on the disk, at most 8 characters). `crc32` is optional; if given, it is the CRC-32 (hexadecimal) of the file's data. Each file must be program data (not picture data), must be on the disk, and must lie inside the adapter RAM (`0x6000` to `0xDFFF`). |
| `base` | Names of the files that are always loaded, in order. |
| `<set id="..." files="..." ram_set="..."/>` | A combination of files loaded on top of the base files, in the order listed (a later file overwrites an earlier one where they overlap), and optionally bytes written to RAM for the tracks that use it (`ram_set`, the same `address=value` pairs as the `<driver>` attribute, applied after it). `files` may be empty, so a set can be only a few RAM values (a sound the driver plays differently depending on a variable). At most 15 sets; each `id` must be unique. |
| `requests` | The RAM addresses (below `0x2000`, at most 7) a track may write its value to. The sound driver reads its requests from these. |

A track chooses a set and a request address with two attributes (section 5): `set` (the `id` of a `<set>`;
omit it to use the base files alone; the files and the RAM values come with the set, because the track's
settings travel to the Machine as one byte) and `request` (one of the addresses of `requests`). **The `command` of such
a track is the value written to that address**, for example a single bit. Choosing a track starts again from a
freshly initialized state with the files of its set, so nothing of the previous track remains; a track with
no `request` only loads its files, which makes it a Music Stop when it is a `control` track.

A sound driver of this kind may start a request only in the right combination of files; a request in the
wrong combination can make the driver run data as code and stop, so list only the combinations that work.

## 5. Tracks: `<tracks>` and `<track>`

```xml
<tracks>
  <track id="BGM_00" command="0x00" title="Music Stop" type="control"/>
  <track id="BGM_92" command="0x92" title="Maximum Power"/>
</tracks>
```

| Attribute | Required | Meaning |
|---|---|---|
| `id` | yes | Identifier of the track. Any text, at most 63 bytes. Must be unique within the file. |
| `title` | yes | Name shown in the track list. |
| `command` | yes | The value sent to the game's sound program. |
| `type` | no | `control`, `wsg` or `sfx`. Omit it for an ordinary track. |
| `loops` | no | `false` for a track that ends by itself. |
| `set` | no | Famicom Disk System only: the `id` of the `<driver><set>` whose files are loaded for this track (section 4b). |
| `request` | no | NES and Famicom Disk System: the address, from `<driver requests>`, the `command` is written to (sections 4a and 4b). |

**`set` and `request`.** They are read for every game, but only the NES (`request`) and the Famicom Disk System use them. A `set` that no
`<driver><set>` declares, or a `request` that is not in `<driver requests>`, is an error and the whole file is not
loaded.

**`id`.** Duplicate ids inside one file are not detected. Selecting either of two tracks with the same
`id` plays the first one. Values longer than 63 bytes are silently cut off, which can turn two
different ids into the same one.

**`title`.** At most 127 bytes of UTF-8. ASCII uses 1 byte per character; Japanese characters use 3
bytes, so about 42 Japanese characters fit. Anything longer is cut off at the byte limit, and if the
cut falls inside a character, that last character is shown as a replacement character.

**`command`.** The value sent to the sound hardware for this track. Write it as hexadecimal with a
`0x` prefix (`0x92`), or as a decimal number (`146`). The value must be between 0 and 255.
Hexadecimal digits without the `0x` prefix (`ff`), values above 255, negative values, and text that is
not a number are errors. What a value means is decided by each game's sound program, so this document
does not list values; take them from the game's own sound test or from existing files.

**`type`.**

- Omitted: an ordinary track. It is sent to the sound hardware and played.
- `control`: a control command such as "stop". The command is sent, but playback is not started and
  the play time does not advance. Use it for a Music Stop entry. On an NES the Machine only starts again from
  the freshly initialized state for it and makes no request (section 4a, "Control tracks").
- `wsg`: a request for a **WSG sound effect**. `command` is the effect number from the game's own
  sound test (the game's test screen labels it "PSG NUMBER"). Only Dragon Spirit (`NAMCO_SYSTEM1`)
  supports it; the numbers are `0x00` to `0x24`. Numbers `0x02`, `0x06`, `0x10`, `0x11`, `0x12` and
  `0x13` currently produce no sound in Chipestra, so they are not listed in the shipped file.
- `sfx`: a request for a **sound effect of the game's own sound program** (the NES games). `command` is
  the effect number the game itself uses. It is played like a song (the game starts the effect through its
  own effect request), and only games that name an `sfx` variable in `<driver>` (section 4a) can play it.
- Any other value (`Control`, `psg`, an empty value, ...) is an error and the whole file is not loaded.

**`loops`.** Tracks are assumed to loop forever and are never stopped automatically. Write
`loops="false"` for a track that ends by itself (an ending, a jingle, a sound effect). Such a track
stops automatically when the sound has been silent for about 4 seconds after it was first heard;
silence at the beginning of the track is not counted, and a track that never makes a sound is not
stopped. Only the exact lower-case text `false` has this effect; any other value (`False`, `0`, `no`)
is treated as a looping track.

Examples from the shipped files:

```xml
<track id="30" title="Boss Destroyed (Boss Clear)" command="0x16" loops="false"/>
<track id="0"  title="Music Stop" command="0x00" type="control"/>
<track id="WSG_00" command="0x00" type="wsg" title="[SFX] Credit" loops="false"/>
```

## 6. Writing special characters

Inside an attribute value written with double quotes, these characters must be written as entities:

| Character | Write as |
|---|---|
| `"` | `&quot;` |
| `&` | `&amp;` |
| `<` | `&lt;` |
| `>` | `&gt;` |
| `'` | `&apos;` (optional inside double quotes) |

Numeric character references such as `&#x3042;` also work. Example: `title="Rock &amp; Roll"` is shown as
`Rock & Roll`.

## 7. Mistakes and what happens

A file with an error is **not loaded**, but all other files are. After Chipestra starts, one message
box titled "Chipestra - XML Load Errors" lists the files that failed and why (at most 10 files are
listed, followed by a count of the rest), and the other games remain usable:

```
2 game definition files could not be loaded:

broken1.xml
    FormatException: game definition xml: XML parse error: Error parsing start element tag
broken2.xml
    FormatException: game definition xml: <track> is missing required attribute 'id'

The other games are still available.
```

Typical causes and the text shown after the file name:

| Mistake | Text |
|---|---|
| Not well-formed XML | `game definition xml: XML parse error: ...` |
| Root element is not `<chipestra>`, or no `<game>` | `game definition xml: no <chipestra><game> element found` |
| `<!DOCTYPE` present | `game definition xml: DOCTYPE declarations are not accepted in definition XML` |
| File larger than 4 MiB | `game definition xml: input size exceeds the 4194304 byte limit` |
| Required attribute missing or empty | `game definition xml: <track> is missing required attribute 'id'` (also `<game>`, `<rom>`) |
| Unknown `type` | `game definition xml: <track id="a">: unknown type "psg" (expected "control", "wsg" or "sfx", or no type attribute)` |
| `command` not a valid number from 0 to 255 | `OverflowException` or `FormatException` with a short description of the value |
| Same game `id` as an earlier file | `game id "gprider" is already defined by Arcade/Sega/XBoard/gprider.xml (My/gprider.xml is ignored)` |

Mistakes that do **not** produce a message when Chipestra starts: a wrong `machine` or `variant`
(reported when you select a track, see section 3), wrong ROM file names, sizes or checksums (reported
when you select a track, see section 4), a wrong ROM `name` (not reported at all, see section 4),
duplicate track ids, and misspelled optional attributes.

## 8. Requests the game does not support

`type="wsg"` and `type="sfx"` are accepted by the XML reader for any game, but only some games can play them (`wsg`: Dragon Spirit; `sfx`: the NES games). In any
other game, selecting such a track shows the message `Cannot play "<title>": this game's sound driver
does not support this kind of track.` and nothing is played. The same message appears in Dragon Spirit
for a number above `0x24`.

## 9. A minimal complete example

```xml
<chipestra>
  <game id="AFTERBURNER2" title="After Burner II" machine="SEGA_XBOARD" variant="XBOARD_BASE"
        manufacturer="Sega" archive="aburner2">
    <resources>
      <rom name="soundcpu" file="epr-11112.17" size="65536"  crc32="d777fc6d"/>
      <rom name="pcm1"     file="mpr-10931.11" size="131072" crc32="9209068f"/>
      <rom name="pcm2"     file="mpr-10930.12" size="131072" crc32="6493368b"/>
      <rom name="pcm3"     file="epr-11102.13" size="131072" crc32="6c07c78d"/>
    </resources>
    <tracks>
      <track id="BGM_00" command="0x00" title="Music Stop" type="control"/>
      <track id="BGM_92" command="0x92" title="Maximum Power"/>
      <track id="BGM_91" command="0x91" title="Final Take Off"/>
    </tracks>
  </game>
</chipestra>
```

Save it as, for example, `xml/Arcade/Sega/XBoard/afterburn2.xml` (any folder below `xml` works), set the ROM Root in Settings to a folder that contains `aburner2.zip` (or a folder named `aburner2`), directly or in a sub-folder, and restart Chipestra.
