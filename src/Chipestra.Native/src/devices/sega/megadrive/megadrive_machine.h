// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"
#include "clock.h"
#include "devices/cpu/z80/mame_derived_z80_adapter.h"
#include "devices/sound/sn76489/sn76489_sound_device_adapter.h"
#include "devices/sound/ym2612/ym2612_device_adapter.h"
#include "resource_resolver.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The settings of a Mega Drive game, read from the game definition XML's <driver> element. Every number is an
// offset into the cartridge or an address in the Z80's RAM; nothing here is a ROM byte.
//
//   <driver music="0x16A6C:5366" request="0x1804" release_cycle="3365294" warmup_frames="104" region="japan">
//     <set id="voice_a" image="0x15904:4456"/>
//   </driver>
//
//   music          "offset:size" of the Z80 program in the cartridge. It is copied to Z80 address 0 and started from
//                  reset; this is the game's sound driver (it plays songs and effects). "offset:lzss" is a program
//                  stored compressed (an LZ77 stream with a 4 KB window: a big-endian 16-bit count, then groups of
//                  eight items led by a flag byte; see DecodeMegaDriveLzss); its length comes from the stream.
//   song_table     (optional) offset of a table of songs in the cartridge: big-endian 16-bit offsets from the start of the
//                  table, each to a compressed block (same format as "lzss" above) that the game's 68000 copies to Z80
//                  RAM at song_address before it writes the request. The first offset is twice the number of songs.
//                  A request from song_first on (song 0 is the first block) loads its block; any other request
//                  loads the first block, as the game has one loaded when it starts
//   song_first     the request number of the first song of song_table (default 0x81)
//   song_address   the Z80 RAM address the blocks are copied to (default 0x1000)
//   queue          (optional) "base:read:write:mask": the driver takes its requests from a ring of two-byte entries
//                  (a value, then a command code) in Z80 RAM: `base` is the address of the ring, `read` and `write`
//                  the addresses of the bytes that hold the read position (moved by the driver) and the write position
//                  (moved by whoever writes a request), `mask` wraps them after each entry of two. A request writes its
//                  value and queue_code at the write position and moves it on; `request` is not used
//   queue_code     the command code written with a request (default 0)
//   queue_init     entries that are already in the ring when the Z80 starts, as "value:code" in hexadecimal separated
//                  by spaces (the game's 68000 writes them at once)
//   stop_request   (optional) a request number that means "silence everything": the Machine starts again and writes no
//                  request (for a driver that has no request of its own for it, and none that is safe to send)
//   music_data     (optional) "offset:size@address": bytes of the cartridge the game's 68000 copies to Z80 RAM besides
//                  the program (constants the driver reads), applied after the program is loaded
//   region         (optional) "japan" (Mega Drive, NTSC; the default), "usa" (Genesis, NTSC) or "europe" (Mega Drive, PAL):
//                  the console the game is played on. It sets the master clock (and so the clocks of the Z80, the YM2612 and
//                  the PSG), the number of scanlines of a frame (262 or 313) and so the rate of the vertical-blank interrupt
//                  (60 or 50 a second), and the region bits of the version register ($A10001), which is kept for a 68000
//                  that may be run one day (MegaDriveVersionRegister) and is not read by the sound driver Machine
//   request        the Z80 RAM address of the driver's request byte (a song or effect number is written there)
//   release_cycle  when the Z80 starts running after the game's own 68000 has loaded the driver and released it from
//                  reset, in Z80 cycles of the region's clock (3579545 Hz in the NTSC machines) since power-on; its
//                  position in the frame (59736 cycles per NTSC frame) decides where in the frame the sound driver's
//                  interrupts fall
//   cart_crc32     (optional) CRC-32 of the cartridge in 68000 order; the file may be a Super Magic Drive copy or a plain
//                  image, and is checked after its layout has been made plain
//   warmup_frames  how long the game's sound driver runs before the game first writes a request, in frames: the
//                  request is written at the start of the frame that is this many frames after the frame the Z80 was
//                  released in. The Machine runs the driver this long once and keeps the state (see MegaDriveMachine)
//   <set>          another Z80 program of the cartridge the game loads in place of the sound driver ("voice" programs
//                  that play a sampled phrase). A track names one with set="voice_a"; it replaces the driver until the
//                  next track.
enum class MegaDriveRegion : std::uint8_t { Japan, Usa, Europe };

// The clocks of a console. The master clock is divided by 15 for the Z80 and the PSG and by 7 for the YM2612, each
// truncated to a whole number of Hz as the original does; a scanline is 3420 master clocks (228 Z80 cycles) and a frame
// 262 (NTSC) or 313 (PAL) scanlines.
struct MegaDriveTiming {
    std::uint64_t masterClockHz = 0;
    std::uint64_t z80ClockHz = 0;
    std::uint64_t ymClockHz = 0;
    std::uint64_t cyclesPerFrame = 0;
    std::uint32_t ymStreamRateHz = 0;   // ymClockHz / 144
    std::uint32_t psgStreamRateHz = 0;  // z80ClockHz / 2
    static MegaDriveTiming For(MegaDriveRegion region);
};

// The version register ($A10001) of a console of the region: bit 7 is set in a console sold outside Japan and bit 6 in
// a PAL one (the other bits are not those of one model of console and are left clear).
std::uint8_t MegaDriveVersionRegister(MegaDriveRegion region);
const char* MegaDriveRegionName(MegaDriveRegion region);  // "japan", "usa" or "europe"

struct MegaDriveDriverConfig {
    MegaDriveRegion region = MegaDriveRegion::Japan;
    struct Image {
        std::uint32_t romOffset = 0;
        std::uint32_t size = 0;  // for a compressed image: the length of the compressed stream, set when it is checked
        bool lzss = false;
    };
    struct DataCopy {
        std::uint32_t romOffset = 0;
        std::uint32_t size = 0;
        std::uint16_t address = 0;
    };
    Image music;
    std::vector<DataCopy> musicData;
    struct RequestQueue {
        std::uint16_t base = 0;
        std::uint16_t readAddress = 0;
        std::uint16_t writeAddress = 0;
        std::uint8_t mask = 0;
        std::uint8_t code = 0;
        std::vector<std::pair<std::uint8_t, std::uint8_t>> initial;  // (value, code)
    };
    std::optional<RequestQueue> queue;
    std::optional<std::uint8_t> stopRequest;
    std::optional<std::uint32_t> songTable;
    std::uint32_t songCount = 0;     // set when the table is checked
    std::uint8_t songFirst = 0x81;
    std::uint16_t songAddress = 0x1000;
    std::vector<std::string> setIds;
    std::vector<Image> sets;
    std::uint16_t requestAddress = 0x1804;
    std::uint64_t releaseCycle = 0;
    std::uint64_t requestCycle = 0;  // derived from warmup_frames: Z80 cycles since power-on
    std::optional<std::uint32_t> cartridgeCrc32;  // CRC-32 of the cartridge image in 68000 order (after the layout is fixed)
};

// Decodes a compressed Z80 program at `offset` of the cartridge. Returns false if the stream runs past the cartridge or
// the result would not fit in the Z80's 8 KB RAM; `consumed` is the number of cartridge bytes the stream occupies.
bool DecodeMegaDriveLzss(const std::vector<std::uint8_t>& cartridge, std::uint32_t offset, std::vector<std::uint8_t>& out, std::uint32_t* consumed);

bool ParseMegaDriveDriverConfig(std::string_view driverXml, MegaDriveDriverConfig& out, std::string* error);

// A game that can be played on more than one console lists them in `region` separated by '|' (region="usa|europe") and
// may give `warmup_frames` and `release_cycle` per region in the same order (warmup_frames="784|782"; one value
// serves all). The application makes one game of the list for each region by writing a <driver> with the values of that
// region alone; this does the same for the native tests and tools: it returns `driverXml` with those three attributes
// reduced to the values of region number `index`, or an empty string (and a message in `error`) if the lists do not fit.
std::string SelectMegaDriveDriverRegion(std::string_view driverXml, std::size_t index, std::string* error);
// The number of regions `region` lists (1 if it is single or missing).
std::size_t CountMegaDriveDriverRegions(std::string_view driverXml);

// A Mega Drive as far as its sound goes: the Z80 with its 8 KB RAM, the YM2612, the PSG in the VDP, the bank register
// ($6000) and the window onto the cartridge ($8000-$FFFF). The 68000 and the VDP are not run: this Machine does what
// the game's 68000 does for the sound driver (loads the Z80 program, releases the Z80 from reset, writes the request
// byte) and raises the vertical-blank interrupt of the Z80 every frame.
//
// The interrupt is the VDP's: the line goes high at scanline 224 of each 262-line frame (59736 Z80 cycles; see
// kIrqAssertCycle) and low one scanline (228 cycles) later; a Z80 that has interrupts disabled for that whole time never sees it.
//
// Switching: a command sent after another one starts everything again, as the game does (the Z80 program is loaded
// afresh, both chips are reset) and writes the request at the time the game itself writes its first one. The run up
// to that time (warmup_frames) is done once; the state after it - Z80 registers and RAM, bank register, both chips,
// the position of the interrupt - is saved and restored at every switch. A track with a set replaces the sound driver
// by that program, started at once.
//
// LogicalChannels: FM 1-6 and DAC (ids 0-6, device "YM2612"), Tone 1-3 and Noise (ids 7-10, device "SN76489").
class MegaDriveMachine final : public IMachine {
public:
    // The NTSC console's numbers (the timing of a Machine is MegaDriveTiming::For(region)).
    static constexpr std::uint64_t kZ80ClockHz = 3'579'545;
    static constexpr std::uint64_t kCyclesPerFrame = 59'736;
    // Scanline 224 of the frame, 10 cycles into the line (the position of the original's vertical-blank interrupt,
    // measured against its sound-chip writes).
    static constexpr std::uint64_t kIrqAssertCycle = 224 * 228 + 10;
    static constexpr std::uint64_t kIrqLengthCycles = 228;       // one scanline
    static constexpr std::uint32_t kYmStreamRateHz = 53'267;
    static constexpr std::uint64_t kMasterTicksPerZ80Cycle = 7'670'453;  // the YM2612's tick (see Ym2612DeviceAdapter): its clock in Hz
    static constexpr std::uint32_t kPsgStreamRateHz = 1'789'772;
    static constexpr std::uint32_t kYmChannelCount = Ym2612DeviceAdapter::kChannelCount;
    static constexpr std::uint32_t kPsgFirstChannel = kYmChannelCount;
    static constexpr float kYmRoutingGain = 0.5f;   // per enabled output bus
    static constexpr float kPsgRoutingGain = 0.5f;  // 0.25 on each side: the original routes it at 0.5 into the VDP and 0.5 on to each side

    // The cartridge, deinterleaved: 68000-order bytes.
    MegaDriveMachine(std::vector<std::uint8_t> cartridge, MegaDriveDriverConfig driver);

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override { return channels_; }
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;
    bool AcceptsCommand(const MusicCommand& command) const override;

    // Test-only observation points - not part of production playback.
    // Called for every write the Z80 makes to the YM2612 (kind 'Y', value = register data, address = port 0-3) or the
    // PSG (kind 'P'), with the time as Z80 cycles since power-on of the game (release_cycle + cycles since the start).
    using ChipWriteObserver = std::function<void(std::uint64_t cycle, char kind, std::uint32_t port, std::uint8_t value)>;
    void SetChipWriteObserverForTest(ChipWriteObserver observer) { chip_write_observer_ = std::move(observer); }
    std::uint8_t Z80RamForTest(std::uint16_t address) const { return ram_[address & 0x1FFF]; }
    std::uint32_t LoadedSetForTest() const { return loaded_set_; }
    std::uint64_t BankForTest() const { return bank_; }
    MameDerivedZ80Adapter::RegisterSnapshot CpuSnapshotForTest() const { return z80_->Snapshot(); }
    // Starts the Machine afresh with a program but runs only `cyclesAfterRelease` Z80 cycles, discarding the sound.
    // Used to compare the Z80's chip writes with the original's without the request.
    void ColdStartForTest(std::uint32_t setNumber, bool runToRequest);
    void PokeRequestForTest(std::uint8_t value) { ram_[driver_.requestAddress & 0x1FFF] = value; }
    // Runs the Z80 (with the chips) up to `cycle` Z80 cycles after the start, discarding the sound.
    void RunToForTest(std::uint64_t cycle) { RunZ80To(cycle, false); }
    // The same with the chips running (the busy flag of the YM2612 only clears while it runs): a driver that waits for
    // it gets on only this way, as when it plays.
    void RunToWithSoundForTest(std::uint64_t cycle) { RunZ80To(cycle, true); }
    std::uint64_t CyclesSinceStartForTest() const { return cycles_done_; }
    std::uint64_t ReleaseCycleForTest() const { return driver_.releaseCycle; }
    const MegaDriveTiming& TimingForTest() const { return timing_; }
    // false: every switch runs the warm-up again instead of restoring the saved state (to compare the two).
    void SetWarmStateRestoreForTest(bool enabled) { restore_warm_state_ = enabled; }

private:
    struct DcBlocker {
        float prevInput = 0.0f;
        float prevOutput = 0.0f;
        void Process(float* samples, std::size_t count, float r) {
            for (std::size_t i = 0; i < count; ++i) {
                const float out = samples[i] - prevInput + r * prevOutput;
                prevInput = samples[i];
                prevOutput = out;
                samples[i] = out;
            }
        }
    };

    void ColdStart(std::uint32_t setNumber, bool runToRequest);
    void ResetMachineState();

    // The state after the sound driver has run for warmup_frames.
    struct WarmState {
        bool valid = false;
        std::array<std::uint8_t, 0x2000> ram{};
        std::uint32_t bank = 0;
        MameDerivedZ80Adapter::SavedState z80;
        Ym2612DeviceAdapter::SavedState ym;
        Sn76489SoundDeviceAdapter::SavedState psg;
        std::uint64_t cyclesDone = 0, ymSamplesMade = 0, psgSamplesMade = 0, nextIrqEdge = 0;
        bool irqHigh = false;
    };
    void LoadImage(const MegaDriveDriverConfig::Image& image, bool applyData);
    void LoadSong(std::uint8_t request);
    void QueueRequest(std::uint8_t value, std::uint8_t code);
    std::uint8_t ReadBus(std::uint16_t address);
    void WriteBus(std::uint16_t address, std::uint8_t value);
    std::uint64_t Now() const { return cycles_done_ + z80_->CyclesConsumedThisCall(); }
    std::uint64_t AbsoluteCycle(std::uint64_t cycle) const { return driver_.releaseCycle + cycle; }
    void SyncYm(std::uint64_t cycle);
    void SyncPsg(std::uint64_t cycle);
    void RunZ80To(std::uint64_t targetCycle, bool keepSound);
    void UpdateRouting();
    void WriteChannels(LogicalChannelSink* sink);

    std::vector<std::uint8_t> cartridge_;
    MegaDriveDriverConfig driver_;
    MegaDriveTiming timing_;

    std::array<std::uint8_t, 0x2000> ram_{};
    std::unique_ptr<MameDerivedZ80Adapter> z80_;
    std::unique_ptr<Ym2612DeviceAdapter> ym_;
    std::unique_ptr<Sn76489SoundDeviceAdapter> psg_;
    ClockConverter clock_converter_;
    std::uint32_t bank_ = 0;

    std::vector<LogicalChannel> channels_;
    bool channels_registered_ = false;
    std::vector<std::vector<float>> pending_ym_;
    std::vector<std::vector<float>> pending_psg_;
    std::array<DcBlocker, Ym2612DeviceAdapter::kChannelCount> dc_block_{};

    std::uint32_t loaded_set_ = 0;
    std::uint64_t cycles_done_ = 0;       // Z80 cycles since the start (the release from reset)
    std::uint64_t origin_cycles_ = 0;     // cycle count at the start of the sound that Render() delivers
    std::uint64_t cumulative_ticks_ = 0;
    std::uint64_t ym_samples_made_ = 0;
    std::uint64_t psg_samples_made_ = 0;
    std::uint64_t next_irq_edge_ = 0;
    bool irq_high_ = false;
    bool keep_sound_ = false;
    bool command_sent_since_reset_ = false;
    bool restore_warm_state_ = true;
    WarmState warm_;
    ChipWriteObserver chip_write_observer_;
};

struct MegaDriveMachineInputs {
    std::vector<std::uint8_t> cartridge;
    MegaDriveDriverConfig driver;
};

// Logical resource names: the cartridge image (a plain image, or a Super Magic Drive copy with its 512-byte header)
// and the text of the game's <driver> element (supplied by Managed Core from the game definition XML).
inline constexpr const char* kMegaDriveCartridgeResource = "rom";
inline constexpr const char* kMegaDriveDriverResource = "driver_xml";

// Turns the cartridge file into 68000-order bytes. The layout is decided from the content: a Super Magic Drive file
// (512-byte header with AA BB at offsets 8 and 9, then 16 KB blocks with the odd bytes first) is deinterleaved; anything
// else (.bin, .md) is already in 68000 order and is used as it is.
std::vector<std::uint8_t> DecodeMegaDriveCartridge(const std::uint8_t* data, std::size_t size);

// Maps resolved resources onto a cartridge and driver settings. An entirely empty `resources` yields a
// registered-but-not-playable set. Once any resource is supplied both are required and every program of the driver
// must lie inside the cartridge: a problem throws std::runtime_error naming it.
MegaDriveMachineInputs AssembleMegaDriveMachineInputs(const MachineResources& resources);
