// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "devices/sega/megadrive/megadrive68k_machine.h"

#include "pugixml/pugixml.hpp"
#include "resource_hash.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <stdexcept>

namespace {

constexpr float kDcBlockHz = 30.0f;
constexpr std::uint64_t kScanlineTicks = 3420;                       // master clock ticks of one scanline
constexpr std::uint64_t kVblankLine = 224;
constexpr std::uint64_t kZ80IrqPulseTicks = 228 * 15;                // the Z80's interrupt line stays up for one scanline
constexpr std::uint64_t kNever = ~0ull;

bool ParseNumber(std::string_view text, std::uint64_t& value) {
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

void Put16(std::uint8_t* p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v >> 8); p[1] = static_cast<std::uint8_t>(v); }
void Put32(std::uint8_t* p, std::uint32_t v) { Put16(p, static_cast<std::uint16_t>(v >> 16)); Put16(p + 2, static_cast<std::uint16_t>(v)); }

}  // namespace

bool ParseMegaDrive68kDriverConfig(std::string_view driverXml, MegaDrive68kDriverConfig& out, std::string* error) {
    out = MegaDrive68kDriverConfig{};
    pugi::xml_document doc;
    const pugi::xml_parse_result parsed = doc.load_buffer(driverXml.data(), driverXml.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!parsed) return Fail(error, std::string("<driver>: ") + parsed.description());
    const pugi::xml_node node = doc.child("driver");
    if (!node) return Fail(error, "no <driver> element");

    if (const pugi::xml_attribute init = node.attribute("m68k_init")) {
        std::string_view list = init.value();
        std::size_t i = 0;
        while (i < list.size()) {
            while (i < list.size() && list[i] == ' ') ++i;
            const std::size_t start = i;
            while (i < list.size() && list[i] != ' ') ++i;
            if (i == start) break;
            std::uint64_t address = 0;
            if (!ParseNumber(list.substr(start, i - start), address) || address > 0x3FFFFF || (address & 1))
                return Fail(error, "<driver m68k_init>: expected addresses of subroutines in the cartridge (even, below 0x400000) separated by spaces");
            out.init.push_back(static_cast<std::uint32_t>(address));
        }
    }
    std::uint64_t value = 0;
    if (const pugi::xml_attribute vh = node.attribute("vblank_handler")) {
        if (std::string_view(vh.value()) != "game") return Fail(error, "<driver vblank_handler>: expected \"game\"");
        out.vblankHandlerIsGame = true;
    }
    if (const pugi::xml_attribute hb = node.attribute("hblank_lines")) {
        if (!ParseNumber(hb.value(), value) || value < 1 || value > 256)
            return Fail(error, "<driver hblank_lines>: expected a number of scanlines (1-256)");
        out.hblankLines = static_cast<std::uint32_t>(value);
    }
    if (const pugi::xml_attribute a6 = node.attribute("m68k_a6")) {
        if (!ParseNumber(a6.value(), value) || value < 0xE00000 || value > 0xFFFFFF)
            return Fail(error, "<driver m68k_a6>: expected an address in the game's RAM (0xE00000-0xFFFFFF)");
        out.a6 = static_cast<std::uint32_t>(value);
    }
    if (const pugi::xml_attribute rw = node.attribute("m68k_ram_words")) {
        std::string_view list = rw.value();
        std::size_t i = 0;
        while (i < list.size()) {
            while (i < list.size() && list[i] == ' ') ++i;
            const std::size_t start = i;
            while (i < list.size() && list[i] != ' ') ++i;
            if (i == start) break;
            const std::string_view pair = list.substr(start, i - start);
            const std::size_t eq = pair.find('=');
            std::uint64_t address = 0, word = 0;
            if (eq == std::string_view::npos || !ParseNumber(pair.substr(0, eq), address) || !ParseNumber(pair.substr(eq + 1), word) ||
                address < 0xE00000 || address > 0xFFFFFF || (address & 1) || word > 0xFFFF)
                return Fail(error, "<driver m68k_ram_words>: expected \"address=value\" pairs (an even address in the game's RAM, 0xE00000-0xFFFFFF, and a 16-bit value) separated by spaces");
            out.ramWords.emplace_back(static_cast<std::uint32_t>(address), static_cast<std::uint16_t>(word));
        }
    }
    if (const pugi::xml_attribute zb = node.attribute("z80_bus")) {
        if (std::string_view(zb.value()) != "instruction_end") return Fail(error, "<driver z80_bus>: expected \"instruction_end\"");
        out.z80BusAtInstructionEnd = true;
    }
    if (const pugi::xml_attribute ra = node.attribute("request_arg")) {
        if (std::string_view(ra.value()) != "pointer") return Fail(error, "<driver request_arg>: expected \"pointer\"");
        out.requestByPointer = true;
    }
    if (!out.vblankHandlerIsGame) {
        std::string_view list = node.attribute("m68k_frame").as_string("");
        std::size_t i = 0;
        while (i < list.size()) {
            while (i < list.size() && list[i] == ' ') ++i;
            const std::size_t start = i;
            while (i < list.size() && list[i] != ' ') ++i;
            if (i == start) break;
            if (!ParseNumber(list.substr(start, i - start), value) || value > 0x3FFFFF || (value & 1))
                return Fail(error, "<driver m68k_frame>: expected the address of the game's sound routine (even, below 0x400000)");
            out.frames.push_back(static_cast<std::uint32_t>(value));
        }
        if (out.frames.empty())
            return Fail(error, "<driver m68k_frame>: expected the address of the game's sound routine (even, below 0x400000)");
    }
    if (const pugi::xml_attribute requests = node.attribute("requests")) {
        std::string_view list = requests.value();
        std::size_t i = 0;
        while (i < list.size()) {
            while (i < list.size() && (list[i] == ' ' || list[i] == ',')) ++i;
            const std::size_t start = i;
            while (i < list.size() && list[i] != ' ' && list[i] != ',') ++i;
            if (i == start) break;
            std::uint64_t address = 0;
            const bool inRam = ParseNumber(list.substr(start, i - start), address) && address >= 0xE00000 && address <= 0xFFFFFF;
            if (!inRam && (!ParseNumber(list.substr(start, i - start), address) || address > 0x3FFFFF || (address & 1)))
                return Fail(error, "<driver requests>: expected addresses of subroutines in the cartridge (even, below 0x400000) or of bytes in the game's RAM (0xE00000-0xFFFFFF) separated by spaces");
            out.requestRoutines.push_back(static_cast<std::uint32_t>(address));
        }
        if (out.requestRoutines.empty() || out.requestRoutines.size() > 7)
            return Fail(error, "<driver requests>: expected 1 to 7 addresses");
    } else {
        if (!ParseNumber(node.attribute("request").as_string(""), value) || value < 0xE00000 || value > 0xFFFFFF)
            return Fail(error, "<driver request>: expected the 68000 address of the request byte in the game's RAM (0xE00000-0xFFFFFF)");
        out.requestAddress = static_cast<std::uint32_t>(value);
    }
    if (!ParseNumber(node.attribute("warmup_frames").as_string(""), value) || value > 100000)
        return Fail(error, "<driver warmup_frames>: expected a number of frames (0-100000)");
    out.warmupFrames = static_cast<std::uint32_t>(value);
    if (const pugi::xml_attribute region = node.attribute("region")) {
        const std::string_view text = region.value();
        if (text == "japan") out.region = MegaDriveRegion::Japan;
        else if (text == "usa") out.region = MegaDriveRegion::Usa;
        else if (text == "europe") out.region = MegaDriveRegion::Europe;
        else return Fail(error, "<driver region>: expected \"japan\", \"usa\" or \"europe\"");
    }
    if (const pugi::xml_attribute z80 = node.attribute("z80_reset")) {
        if (std::string_view(z80.value()) != "released") return Fail(error, "<driver z80_reset>: expected \"released\"");
        out.z80ResetReleased = true;
    }
    if (const pugi::xml_attribute crc = node.attribute("cart_crc32")) {
        std::string_view text = crc.value();
        std::uint32_t crcValue = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), crcValue, 16);
        if (text.empty() || text.size() > 8 || result.ec != std::errc() || result.ptr != text.data() + text.size())
            return Fail(error, "<driver cart_crc32>: expected a CRC-32 in hexadecimal");
        out.cartridgeCrc32 = crcValue;
    }
    return true;
}

MegaDrive68kMachineInputs AssembleMegaDrive68kMachineInputs(const MachineResources& resources) {
    MegaDrive68kMachineInputs inputs;
    if (resources.Count() == 0) return inputs;
    const ResolvedResource* rom = resources.Find(kMegaDriveCartridgeResource);
    if (rom == nullptr) throw std::runtime_error(std::string("Mega Drive: required resource \"") + kMegaDriveCartridgeResource + "\" is missing");
    const ResolvedResource* driver = resources.Find(kMegaDriveDriverResource);
    if (driver == nullptr)
        throw std::runtime_error(std::string("Mega Drive: required resource \"") + kMegaDriveDriverResource +
                                 "\" is missing (the game XML needs a <driver> element)");
    std::string error;
    if (!ParseMegaDrive68kDriverConfig(std::string_view(reinterpret_cast<const char*>(driver->Data()), driver->Size()), inputs.driver, &error))
        throw std::runtime_error("Mega Drive: " + error);
    inputs.cartridge = DecodeMegaDriveCartridge(rom->Data(), rom->Size());
    if (inputs.driver.cartridgeCrc32.has_value()) {
        const std::uint32_t found = Crc32(inputs.cartridge.data(), inputs.cartridge.size());
        if (found != *inputs.driver.cartridgeCrc32) {
            char text[160];
            std::snprintf(text, sizeof(text), "Mega Drive: the cartridge image (in 68000 order) has CRC-32 %08x, but <driver cart_crc32> is %08x: a different dump or a damaged file", found, *inputs.driver.cartridgeCrc32);
            throw std::runtime_error(text);
        }
    }
    for (std::uint32_t address : inputs.driver.init)
        if (address + 2 > inputs.cartridge.size()) throw std::runtime_error("Mega Drive: a <driver m68k_init> routine is not inside the cartridge");
    for (std::uint32_t address : inputs.driver.requestRoutines)
        if (address < 0xE00000 && address + 2 > inputs.cartridge.size()) throw std::runtime_error("Mega Drive: a <driver requests> routine is not inside the cartridge");
    for (std::uint32_t address : inputs.driver.frames)
        if (address + 2 > inputs.cartridge.size()) throw std::runtime_error("Mega Drive: the <driver m68k_frame> routine is not inside the cartridge");
    return inputs;
}

MegaDrive68kMachine::MegaDrive68kMachine(std::vector<std::uint8_t> cartridge, MegaDrive68kDriverConfig driver)
    : cartridge_(std::move(cartridge)),
      driver_(std::move(driver)),
      timing_(MegaDriveTiming::For(driver_.region)),
      master_hz_(timing_.z80ClockHz * 15),
      frame_ticks_(timing_.cyclesPerFrame * 15),
      clock_converter_(kNativeRuntimeTickRateHz, ClockRate{timing_.z80ClockHz * 15}),
      pending_ym_(Ym2612DeviceAdapter::kChannelCount),
      pending_psg_(Sn76489SoundDeviceAdapter::kChannelCount) {
    auto add = [this](const char* device, const char* name, std::uint32_t rate) {
        LogicalChannel channel;
        channel.Id = ChannelId(static_cast<std::uint32_t>(channels_.size()));
        channel.DeviceName = device;
        channel.ChannelName = name;
        channel.NativeSampleRate = rate;
        channels_.push_back(std::move(channel));
    };
    for (const char* name : {"FM 1", "FM 2", "FM 3", "FM 4", "FM 5", "FM 6", "DAC"}) add("YM2612", name, timing_.ymStreamRateHz);
    for (const char* name : {"Tone 1", "Tone 2", "Tone 3", "Noise"}) add("SN76489", name, timing_.psgStreamRateHz);
    for (std::uint32_t c = 0; c < kYmChannelCount; ++c) channels_[c].RoutingGain = kYmRoutingGain * 2.0f;
    for (std::uint32_t c = kPsgFirstChannel; c < channels_.size(); ++c) channels_[c].RoutingGain = kPsgRoutingGain;
    for (std::uint32_t c = 0; c < kYmChannelCount; ++c) channels_[c].AnalogFilterGroup = AnalogFilterGroupId::Fm;
    for (std::uint32_t c = kPsgFirstChannel; c < channels_.size(); ++c) channels_[c].AnalogFilterGroup = AnalogFilterGroupId::Psg;
}

void MegaDrive68kMachine::Initialize() {
    m68k_ = std::make_unique<MameDerivedM68000Adapter>();
    m68k_->SetStandardTimings(true);   // the instruction times of the 68000 (the Z80's bus waits and the 68000's timing set the time the Z80 is stopped)
    z80_ = std::make_unique<MameDerivedZ80Adapter>();
    ym_ = std::make_unique<Ym2612DeviceAdapter>(timing_.z80ClockHz);
    psg_ = std::make_unique<Sn76489SoundDeviceAdapter>();
    m68k_->SetMemoryCallbacks([this](std::uint32_t address) { return Read68k(address); },
                              [this](std::uint32_t address, std::uint8_t value) { Write68k(address, value); });
    // The vertical-blank interrupt (level 6) is held until the 68000 takes it.
    m68k_->SetAutovectorAcknowledgeCallback([this](std::uint8_t level) {
        if (level == 6) vint_pending_ = false;
        else if (level == 4) hint_pending_ = false;
        else if (level == 2) call_pending_ = false;
        else return;
        UpdateIrqLevel();
    });
    z80_->SetMemoryCallbacks([this](std::uint16_t address) { return ReadZ80Bus(address); },
                             [this](std::uint16_t address, std::uint8_t value) { WriteZ80Bus(address, value); });
    z80_->SetIoCallbacks([](std::uint16_t) -> std::uint8_t { return 0xFF; }, [](std::uint16_t, std::uint8_t) {});
    BuildStub();
    ColdStart();
}

void MegaDrive68kMachine::Reset() { ColdStart(); }

void MegaDrive68kMachine::UpdateIrqLevel() {
    m68k_->SetInterruptLevel(vint_pending_ ? 6 : hint_pending_ ? 4 : call_pending_ ? 2 : 0);
}

// The time of the next horizontal-blank interrupt after `afterTicks` (kNever: the first one). The VDP's counter is loaded
// with register 10 at the top of the frame and counts a line at a time, so the interrupts come at lines N-1, 2N-1, ...
// (N = register 10 + 1) while the line is in the picture (above kVblankLine).
void MegaDrive68kMachine::SetNextHint(std::uint64_t afterTicks) {
    if (driver_.hblankLines == 0) { next_hint_ = kNever; return; }
    const std::uint64_t n = driver_.hblankLines;
    const std::uint64_t frame = frame_ticks_;
    std::uint64_t base = afterTicks == kNever ? 0 : afterTicks + 1;
    for (;;) {
        const std::uint64_t k = base / frame;
        for (std::uint64_t line = n - 1; line < kVblankLine; line += n) {
            const std::uint64_t t = k * frame + line * kScanlineTicks + 10 * 15;
            if (t >= base) { next_hint_ = t; return; }
        }
        base = (k + 1) * frame;
    }
}

void MegaDrive68kMachine::Shutdown() {}

void MegaDrive68kMachine::BuildStub() {
    vectors_.fill(0xFF);
    for (std::size_t i = 0; i < vectors_.size() && i < cartridge_.size(); ++i) vectors_[i] = cartridge_[i];
    Put32(&vectors_[4], kStubResetAddress);          // reset PC
    if (driver_.hblankLines == 0) Put32(&vectors_[28 * 4], kStubReturnAddress);    // level 4 (the horizontal interrupt): never raised
    if (!driver_.vblankHandlerIsGame) Put32(&vectors_[30 * 4], kStubInterruptAddress); // level 6 (the vertical-blank interrupt)
    Put32(&vectors_[26 * 4], kStubCallAddress);      // level 2 (a request that the game would make from its main program)
    stub_.fill(0x4E);
    std::size_t at = 0;
    auto word = [&](std::uint16_t w) { Put16(&stub_[at], w); at += 2; };
    auto jsr = [&](std::uint32_t target) { word(0x4EB9); word(static_cast<std::uint16_t>(target >> 16)); word(static_cast<std::uint16_t>(target)); };
    // reset: call the game's initialization routines, enable interrupts, wait for them
    if (driver_.a6.has_value()) { word(0x2C7C); word(static_cast<std::uint16_t>(*driver_.a6 >> 16)); word(static_cast<std::uint16_t>(*driver_.a6)); }   // MOVEA.L #a6,A6
    for (const auto& w : driver_.ramWords) { word(0x33FC); word(w.second); word(static_cast<std::uint16_t>(w.first >> 16)); word(static_cast<std::uint16_t>(w.first)); }   // MOVE.W #value,address
    for (std::uint32_t routine : driver_.init) jsr(routine);
    word(0x46FC); word(0x2000);       // MOVE #$2000,SR
    word(0x4E72); word(0x2000);       // loop: STOP #$2000
    word(0x60FA);                     // BRA loop
    // vertical-blank interrupt: save everything, call the game's sound routine, restore
    at = kStubInterruptAddress - kStubBase;
    word(0x48E7); word(0xFFFE);       // MOVEM.L D0-D7/A0-A6,-(A7)
    for (std::uint32_t routine : driver_.frames) jsr(routine);
    word(0x4CDF); word(0x7FFF);       // MOVEM.L (A7)+,D0-D7/A0-A6
    word(0x4E73);                     // RTE
    if (!driver_.requestRoutines.empty()) {
        // a request (level 2): call the game's routine with the number in D0 and D1, as the game does from its main program
        at = kStubCallAddress - kStubBase;
        word(0x48E7); word(0xFFFE);       // MOVEM.L D0-D7/A0-A6,-(A7)
        if (driver_.requestByPointer) {
            word(0x2279); word(static_cast<std::uint16_t>(kStubRequestRoutine >> 16)); word(static_cast<std::uint16_t>(kStubRequestRoutine));   // MOVEA.L routine,A1
            word(0x3F39); word(static_cast<std::uint16_t>(kStubRequestValue >> 16)); word(static_cast<std::uint16_t>(kStubRequestValue));         // MOVE.W value,-(A7)
            word(0x204F);                                                                                        // MOVEA.L A7,A0
            word(0x4E91);                                                                                        // JSR (A1)
            word(0x548F);                                                                                        // ADDQ.L #2,A7
        } else {
            word(0x2079); word(static_cast<std::uint16_t>(kStubRequestRoutine >> 16)); word(static_cast<std::uint16_t>(kStubRequestRoutine));   // MOVEA.L routine,A0
            word(0x7000);                                                                                        // MOVEQ #0,D0
            word(0x3039); word(static_cast<std::uint16_t>(kStubRequestValue >> 16)); word(static_cast<std::uint16_t>(kStubRequestValue));         // MOVE.W value,D0
            word(0x2200);                                                                                        // MOVE.L D0,D1
            word(0x4E90);                                                                                        // JSR (A0)
        }
        word(0x4CDF); word(0x7FFF);       // MOVEM.L (A7)+,D0-D7/A0-A6
        word(0x4E73);                     // RTE
    }
    at = kStubReturnAddress - kStubBase;
    word(0x4E73);                     // RTE
}

std::uint8_t MegaDrive68kMachine::RomByte(std::uint32_t address) const {
    const std::size_t size = cartridge_.size();
    if (size == 0) return 0xFF;
    if ((size & (size - 1)) == 0) return cartridge_[address & (size - 1)];   // a smaller cartridge is mirrored
    return address < size ? cartridge_[address] : std::uint8_t{0xFF};
}

void MegaDrive68kMachine::ResetMachineState() {
    ram_.fill(0);
    z80_ram_.fill(0);
    Put32(&stub_[kStubRequestRoutine - kStubBase], 0);
    bank_ = 0;
    z80_stop_pending_ = false;
    z80_resume_pending_ = false;
    bus_requested_ = false;
    z80_reset_ = !driver_.z80ResetReleased;
    z80_stopped_ = bus_requested_ || z80_reset_;
    z80_->Reset();
    z80_->SetInputLine(Z80InputLine::Irq, Z80InputLineState::Clear);
    ym_->Reset();
    psg_->Reset();
    for (auto& channel : pending_ym_) channel.clear();
    for (auto& channel : pending_psg_) channel.clear();
    dc_block_ = {};
    t68_ = 0;
    tz80_ = 0;
    ym_samples_made_ = 0;
    psg_samples_made_ = 0;
    origin_ticks_ = 0;
    cumulative_ticks_ = 0;
    z80_irq_high_ = false;
    vint_pending_ = false;
    hint_pending_ = false;
    call_pending_ = false;
    call_queue_.clear();
    SetNextHint(kNever);
    next_vblank_ = (kVblankLine * 228 + 10) * 15;
    next_z80_irq_edge_ = kNever;
    command_sent_since_reset_ = false;
    m68k_->SetInterruptLevel(0);
    m68k_->Reset();      // reads the vectors (the cartridge's, with the reset and level-6 entries ours)
}

void MegaDrive68kMachine::ColdStart() {
    ResetMachineState();
    if (cartridge_.empty()) return;
    // Run the initialization and the warm-up frames; stop when the 68000 is waiting in the generated loop.
    const std::uint64_t chunk = std::max<std::uint64_t>(frame_ticks_ / 4, 1);
    std::uint64_t target = 0;
    for (int i = 0; i < 4 * (static_cast<int>(driver_.warmupFrames) + 8); ++i) {
        target += chunk;
        RunTo(target, true);   // the chips run: a driver that polls the YM2612's busy flag needs it
        const auto snap = m68k_->Snapshot();
        if (driver_.hblankLines != 0) {
            // the horizontal interrupts are counted in the service count too: wait for the time instead
            if (snap.halted && std::min(t68_, TimeZ80()) >= static_cast<std::uint64_t>(driver_.warmupFrames) * frame_ticks_ && !vint_pending_ && !hint_pending_) break;
        } else if (snap.halted && snap.interruptServiceCount >= driver_.warmupFrames && !vint_pending_) break;
    }
    const std::uint64_t now = std::min(t68_, TimeZ80());
    // The chips have not been touched since the drivers' last access; bring them to this time with the sound discarded.
    SyncYm(now);
    SyncPsg(now);
    for (auto& channel : pending_ym_) channel.clear();   // the sound of the start-up run is discarded
    for (auto& channel : pending_psg_) channel.clear();
    dc_block_ = {};
    origin_ticks_ = now;
    warm_.ram = ram_;
    warm_.z80Ram = z80_ram_;
    warm_.m68k = m68k_->SaveState();
    warm_.z80 = z80_->SaveState();
    warm_.ym = ym_->SaveState();
    warm_.psg = psg_->SaveState();
    warm_.bank = bank_;
    warm_.busRequested = bus_requested_;
    warm_.stopPending = z80_stop_pending_;
    warm_.resumePending = z80_resume_pending_;
    warm_.z80Reset = z80_reset_;
    warm_.irqHigh = z80_irq_high_;
    warm_.vintPending = vint_pending_;
    warm_.hintPending = hint_pending_;
    warm_.nextHint = next_hint_;
    warm_.t68 = t68_;
    warm_.tz80 = tz80_;
    warm_.nextVblank = next_vblank_;
    warm_.nextZ80IrqEdge = next_z80_irq_edge_;
    warm_.ymSamplesMade = ym_samples_made_;
    warm_.psgSamplesMade = psg_samples_made_;
    warm_.valid = true;
}

void MegaDrive68kMachine::QueueCall(std::uint32_t routineNumber, std::uint8_t value) {
    if (driver_.requestRoutines.empty()) return;
    const std::size_t index = routineNumber == 0 ? 0 : std::min<std::size_t>(routineNumber - 1, driver_.requestRoutines.size() - 1);
    if (driver_.requestRoutines[index] >= 0xE00000) {
        // an entry that names a byte of the game's RAM: the number is written there, as the game's main program does
        ram_[driver_.requestRoutines[index] & 0xFFFF] = value;
        return;
    }
    Put32(&stub_[kStubRequestRoutine - kStubBase], driver_.requestRoutines[index]);
    Put16(&stub_[kStubRequestValue - kStubBase], value);
    call_pending_ = true;
    UpdateIrqLevel();
}

void MegaDrive68kMachine::BeginSoundForTest(std::uint64_t ticks) {
    origin_ticks_ = ticks;
    cumulative_ticks_ = 0;
    SyncYm(ticks);
    SyncPsg(ticks);
    keep_sound_ = true;
}

bool MegaDrive68kMachine::AcceptsCommand(const MusicCommand& command) const {
    return command.Kind == MusicCommandKind::Stop || command.Kind == MusicCommandKind::Raw || command.Kind == MusicCommandKind::Effect;
}

void MegaDrive68kMachine::SendCommand(const MusicCommand& command) {
    if (!AcceptsCommand(command) || cartridge_.empty()) return;
    auto restore = [this]() {
        if (!restore_warm_state_ || !warm_.valid) { ColdStart(); }
        // (ColdStart leaves the Machine in the same state as the restore below.)
        Put32(&stub_[kStubRequestRoutine - kStubBase], 0);
        ram_ = warm_.ram;
        z80_ram_ = warm_.z80Ram;
        m68k_->RestoreState(warm_.m68k);
        z80_->RestoreState(warm_.z80);
        ym_->RestoreState(warm_.ym);
        psg_->RestoreState(warm_.psg);
        bank_ = warm_.bank;
        bus_requested_ = warm_.busRequested;
        z80_stop_pending_ = warm_.stopPending;
        z80_resume_pending_ = warm_.resumePending;
        z80_reset_ = warm_.z80Reset;
        z80_stopped_ = (bus_requested_ && !z80_stop_pending_) || z80_reset_;
        z80_irq_high_ = warm_.irqHigh;
        vint_pending_ = warm_.vintPending;
        hint_pending_ = warm_.hintPending;
        next_hint_ = warm_.nextHint;
        call_pending_ = false;
        call_queue_.clear();
        t68_ = warm_.t68;
        tz80_ = warm_.tz80;
        next_vblank_ = warm_.nextVblank;
        next_z80_irq_edge_ = warm_.nextZ80IrqEdge;
        ym_samples_made_ = warm_.ymSamplesMade;
        psg_samples_made_ = warm_.psgSamplesMade;
        UpdateIrqLevel();
        z80_->SetInputLine(Z80InputLine::Irq, z80_irq_high_ ? Z80InputLineState::Assert : Z80InputLineState::Clear);
        for (auto& channel : pending_ym_) channel.clear();
        for (auto& channel : pending_psg_) channel.clear();
        dc_block_ = {};
        origin_ticks_ = std::min(t68_, TimeZ80());
        cumulative_ticks_ = 0;
        command_sent_since_reset_ = false;
    };
    if (command.Kind == MusicCommandKind::Stop) {
        restore();
        return;
    }
    // The drivers have no request that silences everything: start again from the saved state.
    if (command_sent_since_reset_) restore();
    command_sent_since_reset_ = true;
    if (!driver_.requestRoutines.empty()) {
        QueueCall((command.Auxiliary >> 4) & 7u, command.Value);      // 1.. names an entry of `requests`; 0 the first
        return;
    }
    const std::uint32_t at = driver_.requestAddress & 0xFFFF;
    ram_[at] = command.Value;
}

// ---------------------------------------------------------------------------------------------
// The 68000's bus
// ---------------------------------------------------------------------------------------------

std::uint8_t MegaDrive68kMachine::Read68k(std::uint32_t address) {
    const std::uint32_t a = address & 0xFFFFFF;
    if (a >= kStubBase && a < kStubBase + stub_.size()) return stub_[a - kStubBase];
    if (a < vectors_.size()) return vectors_[a];
    if (a < 0x400000) return RomByte(a);
    if (a >= 0xE00000) return ram_[a & 0xFFFF];
    if (a >= 0xA00000 && a < 0xA10000) return ReadZ80Area(a & 0xFFFF);
    if (a >= 0xA10000 && a < 0xA10020) {
        switch (a & 0x1F) {
            case 0x01: return MegaDriveVersionRegister(driver_.region);
            case 0x03: case 0x05: case 0x07: return 0x7F;   // a controller with no button pressed
            default: return 0;
        }
    }
    if (a == 0xA11100 || a == 0xA11101) return bus_requested_ && !z80_reset_ ? 0x00 : 0x01;
    if (a >= 0xC00000 && a < 0xC00020) {
        const std::uint64_t line = (std::min(t68_, TimeZ80()) % frame_ticks_) / kScanlineTicks;
        switch (a & 0x1F) {
            case 0x04: return 0x34;                                                  // the status word: FIFO empty
            case 0x05: return static_cast<std::uint8_t>((line >= kVblankLine ? 0x08 : 0) | (driver_.region == MegaDriveRegion::Europe ? 0x01 : 0));
            case 0x08: return static_cast<std::uint8_t>(line);                       // the vertical counter
            default: return 0;
        }
    }
    return 0xFF;
}

void MegaDrive68kMachine::Write68k(std::uint32_t address, std::uint8_t value) {
    const std::uint32_t a = address & 0xFFFFFF;
    if (a >= 0xE00000) {
        ram_[a & 0xFFFF] = value;
        if (ram_observer_ && (a & 0xFFFFFF) >= ram_observer_low_ && (a & 0xFFFFFF) <= ram_observer_high_) ram_observer_(AccessTime68k(), a, value);
        return;
    }
    if (a >= 0xA00000 && a < 0xA10000) { WriteZ80Area(a & 0xFFFF, value); return; }
    // The two bytes of a word write arrive one after the other; the registers are the even byte (bit 8 of the word).
    if (a == 0xA11100) { SetBusRequest((value & 1) != 0); return; }
    if (a == 0xA11200) { SetZ80Reset((value & 1) == 0); return; }
    if (a >= 0xC00010 && a <= 0xC00017 && (a & 1)) { PsgWrite(AccessTime68k(), '6', value); return; }
}

std::uint8_t MegaDrive68kMachine::ReadZ80Area(std::uint32_t offset) {
    if (!bus_requested_) return 0xFF;    // the Z80 area is not there without the bus
    if (offset < 0x4000) return z80_ram_[offset & 0x1FFF];
    if (offset < 0x6000) {
        const std::uint64_t t = AccessTime68k();
        SyncYm(t);
        ym_->SetAccessTime(t * timing_.ymClockHz / 15);
        return ym_->ReadStatus();
    }
    return offset < 0x7F00 ? std::uint8_t{0xFF} : std::uint8_t{0};
}

void MegaDrive68kMachine::WriteZ80Area(std::uint32_t offset, std::uint8_t value) {
    if (!bus_requested_) return;
    if (offset < 0x4000) {
        z80_ram_[offset & 0x1FFF] = value;
        if (z80_ram_observer_) z80_ram_observer_(AccessTime68k(), offset & 0x1FFF, value);
    } else if (offset < 0x6000) {
        YmWrite(AccessTime68k(), '6', offset & 3u, value);
    } else if (offset < 0x6100) {
        bank_ = ((bank_ >> 1) | ((value & 1u) << 23)) & 0xFF8000u;
    } else if (offset >= 0x7F00 && offset < 0x8000) {
        const std::uint32_t low = offset & 0xFFu;
        if (low == 0x11 || low == 0x13 || low == 0x15 || low == 0x17) PsgWrite(AccessTime68k(), '6', value);
    }
}

void MegaDrive68kMachine::SetBusRequest(bool requested) {
    if (requested == bus_requested_) return;
    bus_requested_ = requested;
    if (bus_observer_) bus_observer_(t68_, requested);
    if (!driver_.z80BusAtInstructionEnd) {
        z80_stopped_ = bus_requested_ || z80_reset_;
        if (!requested && !z80_stopped_) tz80_ = std::max(tz80_, t68_);   // the Z80 goes on from now
        return;
    }
    if (requested) {
        // The Z80 stops when the 68000's instruction has ended (RunTo), after having run up to that time.
        if (z80_stopped_) z80_stop_pending_ = false;
        else z80_stop_pending_ = true;
        z80_resume_pending_ = false;
    } else {
        z80_stop_pending_ = false;
        const bool wasStopped = z80_stopped_;
        z80_stopped_ = z80_reset_;
        if (wasStopped && !z80_stopped_) z80_resume_pending_ = true;   // RunTo lets it go on from the end of the instruction
    }
}

void MegaDrive68kMachine::SetZ80Reset(bool reset) {
    if (reset == z80_reset_) return;
    if (reset) {
        z80_->Reset();
        z80_->SetInputLine(Z80InputLine::Irq, Z80InputLineState::Clear);
        SyncYm(AccessTime68k());
        ym_->SetAccessTime(AccessTime68k() * timing_.ymClockHz / 15);
        ym_->SoftReset();                // the reset line clears the sound chip's registers
    }
    z80_reset_ = reset;
    z80_stopped_ = (bus_requested_ && !z80_stop_pending_) || z80_reset_;
    if (!reset) {
        if (z80_irq_high_) z80_->SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);
        if (!z80_stopped_) tz80_ = std::max(tz80_, t68_);
    }
}

// ---------------------------------------------------------------------------------------------
// The Z80's bus
// ---------------------------------------------------------------------------------------------

std::uint8_t MegaDrive68kMachine::ReadZ80Bus(std::uint16_t address) {
    if (address < 0x4000) return z80_ram_[address & 0x1FFF];
    if (address < 0x6000) {
        const std::uint64_t t = AccessTimeZ80();
        SyncYm(t);
        ym_->SetAccessTime(t * timing_.ymClockHz / 15);
        return ym_->ReadStatus();
    }
    if (address < 0x7F00) return 0xFF;
    if (address < 0x8000) return 0;
    const std::uint32_t source = (bank_ + (address & 0x7FFFu)) & 0xFFFFFF;
    if (source >= 0xA00000 && source < 0xA20000) return 0xFF;        // the Z80 cannot read its own side through the window
    return Read68k(source);
}

void MegaDrive68kMachine::WriteZ80Bus(std::uint16_t address, std::uint8_t value) {
    if (address < 0x4000) {
        z80_ram_[address & 0x1FFF] = value;
    } else if (address < 0x6000) {
        YmWrite(AccessTimeZ80(), 'Z', address & 3u, value);
    } else if (address < 0x6100) {
        bank_ = ((bank_ >> 1) | ((value & 1u) << 23)) & 0xFF8000u;
    } else if (address >= 0x7F00 && address < 0x8000) {
        const std::uint32_t offset = address & 0xFFu;
        if (offset == 0x11 || offset == 0x13 || offset == 0x15 || offset == 0x17) PsgWrite(AccessTimeZ80(), 'Z', value);
    } else if (address >= 0x8000) {
        const std::uint32_t target = bank_ + (address & 0x7FFFu);
        const std::uint32_t low = target & 0x1Fu;
        if ((target & 0xFFFFE0u) == 0xC00000u && (low == 0x11 || low == 0x13 || low == 0x15 || low == 0x17)) PsgWrite(AccessTimeZ80(), 'Z', value);
    }
}

// ---------------------------------------------------------------------------------------------
// Chips
// ---------------------------------------------------------------------------------------------

void MegaDrive68kMachine::SyncYm(std::uint64_t ticks) {
    // The original brings its stream up to the time of a write and counts the sample being made at that moment.
    const std::uint64_t target = ticks * timing_.ymStreamRateHz / master_hz_ + 1;
    if (target <= ym_samples_made_) return;
    if (keep_sound_) ym_->Render((target - ym_samples_made_) * Ym2612DeviceAdapter::kInputClocksPerSample, pending_ym_);
    ym_samples_made_ = target;
}

void MegaDrive68kMachine::SyncPsg(std::uint64_t ticks) {
    const std::uint64_t target = (ticks * timing_.psgStreamRateHz + master_hz_ - 1) / master_hz_;
    if (target <= psg_samples_made_) return;
    if (keep_sound_) psg_->Render((target - psg_samples_made_) * Sn76489SoundDeviceAdapter::kInputClocksPerSample, pending_psg_);
    psg_samples_made_ = target;
}

void MegaDrive68kMachine::YmWrite(std::uint64_t ticks, char cpu, std::uint32_t port, std::uint8_t value) {
    SyncYm(ticks);
    if (chip_write_observer_) chip_write_observer_(ticks, cpu, 'Y', port, value);
    ym_->SetAccessTime(ticks * timing_.ymClockHz / 15);
    ym_->Write(port, value);
}

void MegaDrive68kMachine::PsgWrite(std::uint64_t ticks, char cpu, std::uint8_t value) {
    SyncPsg(ticks);
    if (chip_write_observer_) chip_write_observer_(ticks, cpu, 'P', 0, value);
    psg_->Write(value);
}

// ---------------------------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------------------------

void MegaDrive68kMachine::ProcessEvents(std::uint64_t ticks) {
    while (true) {
        const std::uint64_t scheduledVint = vint_schedule_active_ ? (vint_schedule_index_ < vint_schedule_.size() ? vint_schedule_[vint_schedule_index_] : kNever) : kNever;
        const std::uint64_t next = std::min(std::min(std::min(next_vblank_, next_z80_irq_edge_), scheduledVint), next_hint_);
        if (next > ticks) return;
        if (next == next_hint_ && next != next_vblank_ && next != next_z80_irq_edge_ && next != scheduledVint) {
            hint_pending_ = true;
            UpdateIrqLevel();
            SetNextHint(next);
        } else if (next == scheduledVint && next != next_vblank_) {
            ++vint_schedule_index_;
            vint_pending_ = true;
            UpdateIrqLevel();
        } else if (next == next_vblank_) {
            if (!vint_schedule_active_) {
                vint_pending_ = true;
                UpdateIrqLevel();
            } else if (next == scheduledVint) {
                ++vint_schedule_index_;
                vint_pending_ = true;
                UpdateIrqLevel();
            }
            z80_->SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);
            z80_irq_high_ = true;
            next_z80_irq_edge_ = next_vblank_ + kZ80IrqPulseTicks;
            next_vblank_ += frame_ticks_;
        } else {
            z80_->SetInputLine(Z80InputLine::Irq, Z80InputLineState::Clear);
            z80_irq_high_ = false;
            next_z80_irq_edge_ = kNever;
        }
    }
}

void MegaDrive68kMachine::RunTo(std::uint64_t targetTicks, bool keepSound) {
    keep_sound_ = keepSound;
    for (;;) {
        if (z80_stop_pending_ && tz80_ >= t68_) {
            z80_stop_pending_ = false;
            z80_stopped_ = true;
        }
        const std::uint64_t tz = TimeZ80();
        const std::uint64_t now = std::min(t68_, tz);
        if (now >= targetTicks) break;
        ProcessEvents(now);
        if (!call_queue_.empty() && !call_pending_ && t68_ <= tz && m68k_->Snapshot().halted) {
            const auto next = call_queue_.front();
            call_queue_.erase(call_queue_.begin());
            QueueCall(next.first, next.second);
        }
        if (t68_ <= tz) {
            t68_ += 7ull * m68k_->Execute(1);
            if (z80_resume_pending_) {
                z80_resume_pending_ = false;
                tz80_ = std::max(tz80_, t68_);
            }
        } else {
            // The Z80 is behind: run it up to the 68000's time (but not past the next interrupt edge).
            const std::uint64_t horizon = std::min(t68_, std::min(next_vblank_, next_z80_irq_edge_));
            const std::uint64_t budget = horizon > tz80_ ? (horizon - tz80_ + 14) / 15 : 1;
            tz80_ += 15ull * z80_->Execute(budget);
        }
    }
}

void MegaDrive68kMachine::UpdateRouting() {
    for (std::uint32_t c = 0; c < kYmChannelCount; ++c) {
        const bool left = ym_->ChannelOutputsLeft(c);
        const bool right = ym_->ChannelOutputsRight(c);
        LogicalChannel& channel = channels_[c];
        channel.RoutingGain = kYmRoutingGain * static_cast<float>((left ? 1 : 0) + (right ? 1 : 0));
        if (!channel.PanOverridden) channel.Pan = (left == right) ? 0.0f : (left ? -1.0f : 1.0f);
    }
}

void MegaDrive68kMachine::WriteChannels(LogicalChannelSink* sink) {
    const float ymR = 1.0f - 2.0f * 3.14159265358979f * kDcBlockHz / static_cast<float>(timing_.ymStreamRateHz);
    for (std::uint32_t c = 0; c < kYmChannelCount; ++c) {
        std::vector<float>& samples = pending_ym_[c];
        dc_block_[c].Process(samples.data(), samples.size(), ymR);
        if (sink != nullptr) sink->WriteSamples(channels_[c].Id, samples.data(), samples.size());
        samples.clear();
    }
    for (std::uint32_t c = 0; c < Sn76489SoundDeviceAdapter::kChannelCount; ++c) {
        std::vector<float>& samples = pending_psg_[c];
        if (sink != nullptr) sink->WriteSamples(channels_[kPsgFirstChannel + c].Id, samples.data(), samples.size());
        samples.clear();
    }
}

void MegaDrive68kMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) sink->RegisterChannel(channel.Id);
        channels_registered_ = true;
    }
    if (cartridge_.empty()) return;
    cumulative_ticks_ += duration.ticks;
    const std::uint64_t target = origin_ticks_ + clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_));
    RunTo(target, true);
    const std::uint64_t reached = std::max(target, std::min(t68_, TimeZ80()));
    SyncYm(reached);
    SyncPsg(reached);
    UpdateRouting();
    WriteChannels(sink);
}
