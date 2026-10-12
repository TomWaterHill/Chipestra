# Game definition XML format

Chipestra reads its list of games, ROM requirements and tracks from small XML files. This document
describes how to write or edit them.

## 1. Overview

- One XML file defines **one game**: which sound hardware it runs on, which ROM files that hardware
  needs, and the list of tracks (music and sound effects) shown for it.
- The files live in the `xml` folder next to `Chipestra.exe`. Only files directly in that folder with
  the extension `.xml` are read (sub-folders are not searched).
- All files are read once, **when Chipestra starts**. Restart Chipestra after adding or changing a file.
- Files are read in **file-name order** (plain character-code order, so `a.xml` comes before `b.xml`).
  The order matters only when two files use the same game ID (see section 3).
- The game list on screen is grouped as category, manufacturer, system (board), game, and games are
  sorted by their `title`. Tracks are shown in the order they are written in the file.

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
| `manufacturer` | no | Informational only. The manufacturer shown in the game list comes from the board. |
| `archive` | no | Name of the ROM set, used to find the ROM files (see section 4). At most 63 bytes. |

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

A game whose `machine` is not in this list still appears in the game list, under the manufacturer
"Unknown", but cannot be loaded.

**Duplicate game IDs.** If two files use the same `id`, the file that comes first in file-name order
is used and the later file is reported as an error (section 7).

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
| `size` | no | Expected size in bytes (decimal). |
| `crc32` | no | Expected CRC-32, as hexadecimal digits (for example `d777fc6d`). |
| `sha1` | no | Expected SHA-1, as hexadecimal digits. |

All listed ROMs are required. A `size`, `crc32` or `sha1` value that is not a valid number is read as
0, so nothing will match it.

**How a ROM is found.** When you select a track, Chipestra looks under the ROM Root folder, including
all sub-folders, in loose files and in `.zip` files (stored or deflate-compressed entries):

1. If `archive` is given, only a `.zip` file with that name (without the extension) or files inside a
   folder with that name are considered. Names are compared without regard to upper/lower case.
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
| `type` | no | `control` or `wsg`. Omit it for an ordinary track. |
| `loops` | no | `false` for a track that ends by itself. |

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
  the play time does not advance. Use it for a Music Stop entry.
- `wsg`: a request for a **WSG sound effect**. `command` is the effect number from the game's own
  sound test (the game's test screen labels it "PSG NUMBER"). Only Dragon Spirit (`NAMCO_SYSTEM1`)
  supports it; the numbers are `0x00` to `0x24`. Numbers `0x02`, `0x06`, `0x10`, `0x11`, `0x12` and
  `0x13` currently produce no sound in Chipestra, so they are not listed in the shipped file.
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
| Unknown `type` | `game definition xml: <track id="a">: unknown type "psg" (expected "control" or "wsg", or no type attribute)` |
| `command` not a valid number from 0 to 255 | `OverflowException` or `FormatException` with a short description of the value |
| Same game `id` as an earlier file | `game id "gprider" is already defined by gprider.xml` |

Mistakes that do **not** produce a message when Chipestra starts: a wrong `machine` or `variant`
(reported when you select a track, see section 3), wrong ROM file names, sizes or checksums (reported
when you select a track, see section 4), a wrong ROM `name` (not reported at all, see section 4),
duplicate track ids, and misspelled optional attributes.

## 8. Requests the game does not support

`type="wsg"` is accepted by the XML reader for any game, but only Dragon Spirit can play it. In any
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

Save it as, for example, `xml/afterburn2.xml`, set the ROM Root in Settings to a folder that contains `aburner2.zip` (or a folder named `aburner2`), directly or in a sub-folder, and restart Chipestra.
