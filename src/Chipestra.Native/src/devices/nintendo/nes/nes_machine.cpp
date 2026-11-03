// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "nes_machine.h"

#include "nes_driver_code.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

// One-pole DC blocker, about 30 Hz at the APU's sample rate (the console's own output stage also removes DC).
const float kDcBlockR = DcBlocker::Coefficient(static_cast<double>(NesMachine::kApuSampleRateHz));
// The same for the VRC6, whose samples come once per CPU cycle.
const float kVrc6DcBlockR = DcBlocker::Coefficient(static_cast<double>(NesMachine::kCpuClockHz));
// The VRC7's samples come at its own rate.
const float kVrc7DcBlockR = DcBlocker::Coefficient(static_cast<double>(Vrc7SoundDeviceAdapter::kOutputSampleRateHz));
// The Namco 163 samples come at its own rate too.
const float kN163DcBlockR = DcBlocker::Coefficient(static_cast<double>(Namco163SoundDeviceAdapter::kOutputSampleRateHz));
// The 5B's samples come at the rate of the original's stream.
const float kSunsoft5bDcBlockR = DcBlocker::Coefficient(static_cast<double>(Sunsoft5bSoundDeviceAdapter::kStreamRateHz));

// The start-up routines have to finish within this many cycles (one frame).
constexpr std::uint64_t kStartWarmupCycles = 29'781;

// How long the picture chip's vertical-blank flag reads as set after the start of a frame.
constexpr std::uint64_t kVblankCycles = 2'273;

}  // namespace

float NesMachine::N163RoutingGain(unsigned submapper) {
    switch (submapper) {
    case 2: return 0.0f;
    case 3: return 1.125f;
    case 4: return 1.17f;
    case 5: return 1.19f;
    default: return 1.0f;
    }
}

NesMachine::NesMachine(NesCartridge cartridge, NesDriverConfig driver, std::vector<std::uint8_t> vrc7Instruments)
    : cartridge_(std::move(cartridge)),
      driver_(std::move(driver)),
      mmc3_enabled_(cartridge_.mapper == 4),
      mmc3_(cartridge_.prg.size() / 0x2000),
      vrc6_enabled_(cartridge_.mapper == 24 || cartridge_.mapper == 26),
      vrc6_prg_(cartridge_.prg.size() / 0x2000, cartridge_.mapper == 26),
      vrc7_enabled_(cartridge_.mapper == 85),
      vrc7_prg_(cartridge_.prg.size() / 0x2000, driver_.vrc7RegisterLine.value_or(4)),
      vrc7_instruments_(std::move(vrc7Instruments)),
      n163_enabled_(cartridge_.mapper == 19),
      n163_prg_(cartridge_.prg.size() / 0x2000),
      fme7_enabled_(cartridge_.mapper == 69),
      fme7_prg_(cartridge_.prg.size() / 0x2000),
      clock_converter_(kNativeRuntimeTickRateHz, ClockRate(kCpuClockHz)) {
    if (vrc7_enabled_ && vrc7_instruments_.size() != Vrc7SoundDeviceAdapter::kInstrumentTableSize)
        throw std::invalid_argument("NES: a VRC7 cartridge needs the 144-byte instrument table (ds1001_instruments.bin)");
    if (vrc7_enabled_ && !driver_.vrc7RegisterLine)
        throw std::invalid_argument("NES: a VRC7 cartridge needs the driver's vrc7_register_line");
    if (n163_enabled_ && (!driver_.n163Channels || !driver_.n163Volume))
        throw std::invalid_argument("NES: a Namco 163 cartridge needs the driver n163_channels and n163_volume");
    static const char* const kNames[NesApuDeviceAdapter::kChannelCount] = {"Pulse 1", "Pulse 2", "Triangle", "Noise", "DMC"};
    for (std::uint32_t c = 0; c < NesApuDeviceAdapter::kChannelCount; ++c) {
        LogicalChannel channel;
        channel.Id = c;
        channel.DeviceName = "RP2A03";
        channel.ChannelName = kNames[c];
        channel.NativeSampleRate = kApuSampleRateHz;
        channel.RoutingGain = kRoutingGain;
        channels_.push_back(std::move(channel));
    }
    if (vrc6_enabled_) {
        static const char* const kVrc6Names[Vrc6SoundDeviceAdapter::kChannelCount] = {"Pulse 1", "Pulse 2", "Saw"};
        for (std::uint32_t c = 0; c < Vrc6SoundDeviceAdapter::kChannelCount; ++c) {
            LogicalChannel channel;
            channel.Id = NesApuDeviceAdapter::kChannelCount + c;
            channel.DeviceName = "VRC6";
            channel.ChannelName = kVrc6Names[c];
            channel.NativeSampleRate = static_cast<std::uint32_t>(kCpuClockHz);
            channel.RoutingGain = kVrc6RoutingGain;
            channels_.push_back(std::move(channel));
        }
    }
    if (vrc7_enabled_) {
        for (std::uint32_t c = 0; c < Vrc7SoundDeviceAdapter::kChannelCount; ++c) {
            LogicalChannel channel;
            channel.Id = NesApuDeviceAdapter::kChannelCount + c;
            channel.DeviceName = "VRC7";
            channel.ChannelName = "FM" + std::to_string(c + 1);
            channel.NativeSampleRate = Vrc7SoundDeviceAdapter::kOutputSampleRateHz;
            channel.RoutingGain = kVrc7RoutingGain;
            channels_.push_back(std::move(channel));
        }
    }
    if (n163_enabled_) {
        n163_first_slot_ = Namco163SoundDeviceAdapter::kChannelCount - *driver_.n163Channels;
        n163_gain_ = N163RoutingGain(*driver_.n163Volume);
        for (unsigned slot = n163_first_slot_; slot < Namco163SoundDeviceAdapter::kChannelCount; ++slot) {
            LogicalChannel channel;
            channel.Id = NesApuDeviceAdapter::kChannelCount + (slot - n163_first_slot_);
            channel.DeviceName = "N163";
            channel.ChannelName = "Wave " + std::to_string(slot + 1);
            channel.NativeSampleRate = Namco163SoundDeviceAdapter::kOutputSampleRateHz;
            channel.RoutingGain = n163_gain_;
            channels_.push_back(std::move(channel));
        }
    }
    if (fme7_enabled_) {
        for (std::uint32_t c = 0; c < Sunsoft5bSoundDeviceAdapter::kChannelCount; ++c) {
            LogicalChannel channel;
            channel.Id = NesApuDeviceAdapter::kChannelCount + c;
            channel.DeviceName = "5B";
            channel.ChannelName = "Tone " + std::to_string(c + 1);
            channel.NativeSampleRate = Sunsoft5bSoundDeviceAdapter::kStreamRateHz;
            channel.RoutingGain = kSunsoft5bRoutingGain;
            channels_.push_back(std::move(channel));
        }
    }
}

void NesMachine::BuildStartCode() {
    CodeBanks banks{};
    const bool banked = (mmc3_enabled_ || vrc6_enabled_ || vrc7_enabled_ || n163_enabled_ || fme7_enabled_) && driver_.prgBanksCall && driver_.prgBanksIdle;
    if (banked) {
        banks = CodeBanks{driver_.prgBanksCall->r6, driver_.prgBanksCall->r7, driver_.prgBanksIdle->r6, driver_.prgBanksIdle->r7};
        banks.vrc6 = vrc6_enabled_;
        banks.vrc7 = vrc7_enabled_;
        banks.n163 = n163_enabled_;
        banks.fme7 = fme7_enabled_;
        banks.callR8 = driver_.prgBanksCall->r8;
        banks.idleR8 = driver_.prgBanksIdle->r8;
        banks.vrc7Line = driver_.vrc7RegisterLine.value_or(4);
        banks.mmc3PrgMode0 = driver_.mmc3PrgMode0;
    }
    start_code_ = ::BuildStartCode(driver_.init, kStartCodeAddress, banked ? &banks : nullptr, driver_.inhibitFrameIrq);
    const std::uint16_t* requestCall = driver_.requestCall ? &*driver_.requestCall : nullptr;
    const std::uint16_t* frameCounter = driver_.frameCounter ? &*driver_.frameCounter : nullptr;
    const std::uint16_t* sfxCall = driver_.sfxCall ? &*driver_.sfxCall : nullptr;
    frame_code_ = BuildFrameCode(driver_.frame, banked ? &banks : nullptr, requestCall, frameCounter, driver_.requestRegister, sfxCall);
}

void NesMachine::Initialize() {
    cpu_ = std::make_unique<MameDerivedRp2a03Adapter>();
    apu_ = std::make_unique<NesApuDeviceAdapter>();
    if (vrc6_enabled_) vrc6_ = std::make_unique<Vrc6SoundDeviceAdapter>();
    if (vrc7_enabled_) vrc7_ = std::make_unique<Vrc7SoundDeviceAdapter>(vrc7_instruments_);
    if (n163_enabled_) n163_ = std::make_unique<Namco163SoundDeviceAdapter>();
    if (fme7_enabled_) sun5b_ = std::make_unique<Sunsoft5bSoundDeviceAdapter>();
    cpu_->SetMemoryCallbacks([this](std::uint16_t address) { return ReadBus(address); },
                             [this](std::uint16_t address, std::uint8_t value) { WriteBus(address, value); });
    apu_->SetIrqCallback([this](bool asserted) {
        cpu_->SetInputLine(M6502InputLine::ApuIrq, asserted ? M6502InputLineState::Assert : M6502InputLineState::Clear);
    });
    apu_->SetMemoryReadCallback([this](std::uint16_t address) -> std::uint8_t {
        // The sample bytes come from the cartridge's program ROM; the original's DMC reads cost the CPU no cycles.
        const std::uint8_t value = address >= 0x8000 ? PrgRead(address) : std::uint8_t{0};
        if (dmc_read_observer_ && !starting_up_) dmc_read_observer_(frames_started_ > 0 ? frames_started_ - 1 : 0, address, value);
        return value;
    });
    BuildStartCode();
    ColdStart();
}

void NesMachine::Reset() { ColdStart(); }

void NesMachine::Shutdown() {}

void NesMachine::ColdStart() {
    ram_.fill(0xFF);
    wram_.fill(0);
    if (mmc3_enabled_) {
        if (driver_.prgBanksIdle) mmc3_.SetState(driver_.prgBanksIdle->r6, driver_.prgBanksIdle->r7, !driver_.mmc3PrgMode0);
        else mmc3_.Reset();
    }
    if (vrc6_enabled_) {
        if (driver_.prgBanksIdle) vrc6_prg_.SetState(driver_.prgBanksIdle->r6, driver_.prgBanksIdle->r7);
        else vrc6_prg_.Reset();
        vrc6_->Reset();
    }
    if (vrc7_enabled_) {
        if (driver_.prgBanksIdle) vrc7_prg_.SetState(driver_.prgBanksIdle->r6, driver_.prgBanksIdle->r7, driver_.prgBanksIdle->r8);
        else vrc7_prg_.Reset();
        vrc7_->Reset();
    }
    if (n163_enabled_) {
        if (driver_.prgBanksIdle) n163_prg_.SetState(driver_.prgBanksIdle->r6, driver_.prgBanksIdle->r7, driver_.prgBanksIdle->r8);
        else n163_prg_.Reset();
        n163_->Reset();
    }
    if (fme7_enabled_) {
        if (driver_.prgBanksIdle) fme7_prg_.SetState(driver_.prgBanksIdle->r6, driver_.prgBanksIdle->r7, kFme7InitialBankB);
        else fme7_prg_.Reset();
        sun5b_->Reset();
    }
    request_value_ = 0;
    request_pending_ = 0;
    sfx_value_ = 0;
    sfx_pending_ = 0;
    if (driver_.ramClear) {
        for (std::uint32_t a = driver_.ramClear->first; a <= driver_.ramClear->second; ++a) ram_[a & 0x7FF] = 0;
    }
    for (const auto& [address, value] : driver_.ramSet) ram_[address & 0x7FF] = value;
    cpu_cycles_done_ = 0;
    apu_cycles_ = 0;
    origin_cycles_ = 0;
    cumulative_ticks_ = 0;
    frames_started_ = 0;
    last_frame_start_cycle_ = 0;
    command_sent_since_reset_ = false;
    bus_stats_ = BusStats{};
    dc_block_.fill(DcBlocker{});
    vrc6_dc_block_.fill(DcBlocker{});
    vrc7_dc_block_.fill(DcBlocker{});
    n163_dc_block_.fill(DcBlocker{});
    sun5b_dc_block_.fill(DcBlocker{});
    for (auto& buffer : pending_) buffer.clear();
    for (auto& buffer : vrc6_pending_) buffer.clear();
    for (auto& buffer : vrc7_pending_) buffer.clear();
    for (auto& buffer : n163_pending_) buffer.clear();
    for (auto& buffer : sun5b_pending_) buffer.clear();

    starting_up_ = true;
    cpu_->Reset();
    apu_->Reset();
    RunCpuTo(kStartWarmupCycles, false);
    // The driver's warm-up frames (one by default) of the frame routines with nothing requested (just under
    // the last frame's end, so exactly that many start).
    origin_cycles_ = cpu_cycles_done_;
    RunCpuTo(origin_cycles_ + driver_.warmupFrames * kCyclesPerTwoFrames / 2, true);
    SyncApu(cpu_cycles_done_);
    for (auto& buffer : pending_) buffer.clear();
    for (auto& buffer : vrc6_pending_) buffer.clear();
    for (auto& buffer : vrc7_pending_) buffer.clear();
    for (auto& buffer : n163_pending_) buffer.clear();
    for (auto& buffer : sun5b_pending_) buffer.clear();
    origin_cycles_ = cpu_cycles_done_;
    frames_started_ = 0;
    last_frame_start_cycle_ = cpu_cycles_done_;
    starting_up_ = false;
}

std::uint16_t NesMachine::MutedMask() const {
    std::uint16_t mask = 0;
    for (std::size_t c = 0; c < channels_.size(); ++c)
        if (channels_[c].Mute) mask = static_cast<std::uint16_t>(mask | (1u << c));
    return mask;
}

void NesMachine::SyncApu(std::uint64_t cpuCycle) {
    if (cpuCycle <= apu_cycles_) return;
    const std::uint16_t muted = MutedMask();
    apu_->SetMutedChannels(static_cast<std::uint8_t>(muted & 0x1F));
    apu_->Render(cpuCycle - apu_cycles_, pending_);
    if (vrc6_enabled_) {
        vrc6_->SetMutedChannels(static_cast<std::uint8_t>((muted >> NesApuDeviceAdapter::kChannelCount) & 0x3F));
        vrc6_->Render(cpuCycle - apu_cycles_, vrc6_pending_);
    }
    if (vrc7_enabled_) {
        vrc7_->SetMutedChannels(static_cast<std::uint8_t>((muted >> NesApuDeviceAdapter::kChannelCount) & 0x3F));
        // The chip's clock is twice the CPU's.
        vrc7_->Render((cpuCycle - apu_cycles_) * 2, vrc7_pending_);
    }
    if (n163_enabled_) {
        // The meter channels are the last slots; the mute bits follow the APU's.
        n163_->SetMutedChannels(static_cast<std::uint8_t>(((muted >> NesApuDeviceAdapter::kChannelCount) << n163_first_slot_) & 0xFF));
        n163_->Render(cpuCycle - apu_cycles_, n163_pending_);
    }
    if (fme7_enabled_) {
        sun5b_->SetMutedChannels(static_cast<std::uint8_t>((muted >> NesApuDeviceAdapter::kChannelCount) & 7));
        sun5b_->RenderToSampleCount(Sunsoft5bSoundDeviceAdapter::SamplesBeforeCycle(cpuCycle), sun5b_pending_);
    }
    apu_cycles_ = cpuCycle;
}

void NesMachine::RunCpuTo(std::uint64_t targetCycle, bool frames) {
    while (cpu_cycles_done_ < targetCycle) {
        auto nextFrameCycle = [this]() { return origin_cycles_ + frames_started_ * kCyclesPerTwoFrames / 2; };
        bool raised = false;
        if (frames && cpu_cycles_done_ >= nextFrameCycle()) {
            cpu_->SetInputLine(M6502InputLine::Nmi, M6502InputLineState::Assert);
            last_frame_start_cycle_ = cpu_cycles_done_;
            ++frames_started_;
            raised = true;
        }
        std::uint64_t stop = targetCycle;
        if (frames) stop = std::min(stop, nextFrameCycle());
        const std::uint64_t budget = stop > cpu_cycles_done_ ? stop - cpu_cycles_done_ : 1;
        cpu_cycles_done_ += cpu_->Execute(budget);
        // The edge was latched by the CPU already; releasing the line lets the next frame raise a new one.
        if (raised) cpu_->SetInputLine(M6502InputLine::Nmi, M6502InputLineState::Clear);
    }
}

std::uint8_t NesMachine::PrgRead(std::uint16_t address) const {
    if (mmc3_enabled_) return cartridge_.prg[mmc3_.Offset(address)];
    if (vrc6_enabled_) return cartridge_.prg[vrc6_prg_.Offset(address)];
    if (vrc7_enabled_) return cartridge_.prg[vrc7_prg_.Offset(address)];
    if (n163_enabled_) return cartridge_.prg[n163_prg_.Offset(address)];
    if (fme7_enabled_) return cartridge_.prg[fme7_prg_.Offset(address)];
    return cartridge_.prg[(address & 0x7FFF) % cartridge_.prg.size()];
}

std::uint8_t NesMachine::ReadBus(std::uint16_t address) {
    if (address < 0x2000) return ram_[address & 0x7FF];
    if (address < 0x4000) {
        ++bus_stats_.ppuReads;
        // Register 2 (status): only the vertical-blank flag, set for a while at the start of every frame.
        if ((address & 7) == 2) return (Now() - last_frame_start_cycle_) < kVblankCycles ? 0x80 : 0x00;
        return 0;
    }
    if (address == 0x4015) {
        SyncApu(Now());
        return apu_->ReadStatus();
    }
    if (address == 0x4016 || address == 0x4017) {
        ++bus_stats_.inputReads;
        return 0;  // no buttons pressed
    }
    if (n163_enabled_ && address >= 0x4800 && address < 0x5000) {
        SyncApu(Now());
        return n163_->ReadData();
    }
    if (address == kRequestValueAddress) return request_value_;
    if (address == kRequestPendingAddress) {
        const std::uint8_t pending = request_pending_;
        request_pending_ = 0;  // read once, by the generated frame code
        return pending;
    }
    if (address == kSfxValueAddress) return sfx_value_;
    if (address == kSfxPendingAddress) {
        const std::uint8_t pending = sfx_pending_;
        sfx_pending_ = 0;  // read once, by the generated frame code
        return pending;
    }
    if (address >= kStartCodeAddress && address < kStartCodeAddress + 0x100) {
        const std::size_t i = address - kStartCodeAddress;
        return i < start_code_.size() ? start_code_[i] : std::uint8_t{0};
    }
    if (address >= kFrameCodeAddress && address < kFrameCodeAddress + 0x100) {
        const std::size_t i = address - kFrameCodeAddress;
        return i < frame_code_.size() ? frame_code_[i] : std::uint8_t{0};
    }
    if (address >= 0x8000) {
        // The reset and NMI vectors lead to the generated code; the IRQ vector is the cartridge's own.
        if (address == 0xFFFA) return kFrameCodeAddress & 0xFF;
        if (address == 0xFFFB) return kFrameCodeAddress >> 8;
        if (address == 0xFFFC) return kStartCodeAddress & 0xFF;
        if (address == 0xFFFD) return kStartCodeAddress >> 8;
        return PrgRead(address);
    }
    if ((mmc3_enabled_ || vrc7_enabled_ || n163_enabled_ || fme7_enabled_) && address >= 0x6000) return wram_[address & 0x1FFF];
    return 0;
}

void NesMachine::WriteBus(std::uint16_t address, std::uint8_t value) {
    if (address < 0x2000) {
        ram_[address & 0x7FF] = value;
    } else if (address < 0x4000 || address == 0x4014) {
        ++bus_stats_.ppuWrites;
    } else if (address == 0x4016) {
        ++bus_stats_.inputWrites;
    } else if ((address >= 0x4000 && address <= 0x4013) || address == 0x4015 || address == 0x4017) {
        if (address == 0x4017) ++bus_stats_.inputWrites;
        SyncApu(Now());
        apu_->Write(static_cast<std::uint8_t>(address - 0x4000), value);
        if (apu_write_observer_ && !starting_up_)
            apu_write_observer_(frames_started_ > 0 ? frames_started_ - 1 : 0, static_cast<std::uint8_t>(address - 0x4000), value);
    } else if (n163_enabled_ && address >= 0x4800 && address < 0x5000) {
        SyncApu(Now());
        n163_->WriteData(value);
        if (n163_write_observer_ && !starting_up_)
            n163_write_observer_(frames_started_ > 0 ? frames_started_ - 1 : 0, 0x4800, value);
    } else if ((mmc3_enabled_ || vrc7_enabled_ || n163_enabled_ || fme7_enabled_) && address >= 0x6000 && address < 0x8000) {
        wram_[address & 0x1FFF] = value;
    } else if (vrc6_enabled_ && address >= 0x8000) {
        // The DMC reads through the banks that are mapped until this write, and the sound chip is brought up to
        // this cycle before one of its registers changes.
        SyncApu(Now());
        unsigned soundOffset = 0;
        if (vrc6_prg_.Write(address, value, &soundOffset)) {
            vrc6_->Write(soundOffset, value);
            if (vrc6_write_observer_ && !starting_up_)
                vrc6_write_observer_(frames_started_ > 0 ? frames_started_ - 1 : 0, address, value);
        }
    } else if (vrc7_enabled_ && address >= 0x8000) {
        SyncApu(Now());
        const Vrc7Prg::Sound sound = vrc7_prg_.Write(address, value);
        if (sound != Vrc7Prg::Sound::None) {
            if (sound == Vrc7Prg::Sound::Data) vrc7_->WriteData(value);
            else vrc7_->WriteAddress(value);
            if (vrc7_write_observer_ && !starting_up_)
                vrc7_write_observer_(frames_started_ > 0 ? frames_started_ - 1 : 0, sound == Vrc7Prg::Sound::Data, value);
        }
    } else if (n163_enabled_ && address >= 0x8000) {
        SyncApu(Now());
        std::uint8_t soundAddress = 0;
        const Namco163Prg::Sound sound = n163_prg_.Write(address, value, soundAddress);
        if (sound == Namco163Prg::Sound::Address) {
            n163_->WriteAddress(soundAddress);
            if (n163_write_observer_ && !starting_up_)
                n163_write_observer_(frames_started_ > 0 ? frames_started_ - 1 : 0, 0xF800, soundAddress);
        } else if (sound != Namco163Prg::Sound::None) {
            n163_->SetDisabled(sound == Namco163Prg::Sound::Disable);
        }
    } else if (fme7_enabled_ && address >= 0x8000) {
        // The DMC reads through the banks that are mapped until this write, and the 5B is brought up to this cycle
        // before one of its registers changes.
        SyncApu(Now());
        const Fme7Prg::Sound sound = fme7_prg_.Write(address, value);
        if (sound != Fme7Prg::Sound::None) {
            if (sound == Fme7Prg::Sound::Data) sun5b_->WriteData(value);
            else sun5b_->WriteAddress(value);
            if (sun5b_write_observer_ && !starting_up_)
                sun5b_write_observer_(frames_started_ > 0 ? frames_started_ - 1 : 0, sound == Fme7Prg::Sound::Data ? 0xE000 : 0xC000, value);
        }
    } else if (mmc3_enabled_ && address >= 0x8000) {
        // The DMC reads through the banks that are mapped until this write.
        SyncApu(Now());
        mmc3_.Write(address, value);
    }
    // Other writes to the cartridge (a CHR bank select on CNROM) do not change the sound.
}

void NesMachine::WriteChannels(LogicalChannelSink* sink) {
    for (std::size_t c = 0; c < NesApuDeviceAdapter::kChannelCount; ++c) {
        std::vector<float>& samples = pending_[c];
        dc_block_[c].Process(samples.data(), samples.size(), kDcBlockR);
        if (sink != nullptr) sink->WriteSamples(channels_[c].Id, samples.data(), samples.size());
        samples.clear();
    }
    if (vrc6_enabled_) {
        for (std::size_t c = 0; c < Vrc6SoundDeviceAdapter::kChannelCount; ++c) {
            std::vector<float>& samples = vrc6_pending_[c];
            vrc6_dc_block_[c].Process(samples.data(), samples.size(), kVrc6DcBlockR);
            if (sink != nullptr) sink->WriteSamples(channels_[NesApuDeviceAdapter::kChannelCount + c].Id, samples.data(), samples.size());
            samples.clear();
        }
    }
    if (vrc7_enabled_) {
        for (std::size_t c = 0; c < Vrc7SoundDeviceAdapter::kChannelCount; ++c) {
            std::vector<float>& samples = vrc7_pending_[c];
            vrc7_dc_block_[c].Process(samples.data(), samples.size(), kVrc7DcBlockR);
            if (sink != nullptr) sink->WriteSamples(channels_[NesApuDeviceAdapter::kChannelCount + c].Id, samples.data(), samples.size());
            samples.clear();
        }
    }
    if (fme7_enabled_) {
        for (std::size_t c = 0; c < Sunsoft5bSoundDeviceAdapter::kChannelCount; ++c) {
            std::vector<float>& samples = sun5b_pending_[c];
            sun5b_dc_block_[c].Process(samples.data(), samples.size(), kSunsoft5bDcBlockR);
            if (sink != nullptr) sink->WriteSamples(channels_[NesApuDeviceAdapter::kChannelCount + c].Id, samples.data(), samples.size());
            samples.clear();
        }
    }
    if (n163_enabled_) {
        for (std::size_t slot = 0; slot < Namco163SoundDeviceAdapter::kChannelCount; ++slot) {
            std::vector<float>& samples = n163_pending_[slot];
            if (slot >= n163_first_slot_) {
                n163_dc_block_[slot].Process(samples.data(), samples.size(), kN163DcBlockR);
                if (sink != nullptr)
                    sink->WriteSamples(channels_[NesApuDeviceAdapter::kChannelCount + (slot - n163_first_slot_)].Id, samples.data(), samples.size());
            }
            samples.clear();  // the slots the meters do not show are dropped
        }
    }
}

void NesMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) sink->RegisterChannel(channel.Id);
        channels_registered_ = true;
    }
    cumulative_ticks_ += duration.ticks;
    const std::uint64_t target = origin_cycles_ + clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_));
    RunCpuTo(target, true);
    SyncApu(cpu_cycles_done_);
    WriteChannels(sink);
}

bool NesMachine::AcceptsCommand(const MusicCommand& command) const {
    switch (command.Kind) {
    case MusicCommandKind::Raw: {
        // A control track (the Music Stop) only resets the Machine; it needs no variable and makes no request.
        if (NesDriverConfig::IsControl(command.Auxiliary)) return true;
        if (driver_.requestCall) return command.Auxiliary == 0;
        if (command.Auxiliary == 0) return driver_.bgmVariable.has_value();
        // A request address of <driver requests="...">; the set number is for the Famicom Disk System only.
        return NesDriverConfig::SetNumberOf(command.Auxiliary) == 0 &&
               NesDriverConfig::RequestNumberOf(command.Auxiliary) <= driver_.requests.size();
    }
    case MusicCommandKind::Effect: return driver_.requestCall.has_value() || driver_.sfxVariable.has_value();
    case MusicCommandKind::Stop: return true;
    default: return false;
    }
}

void NesMachine::SendCommand(const MusicCommand& command) {
    if (!AcceptsCommand(command)) return;
    const bool control = command.Kind == MusicCommandKind::Raw && NesDriverConfig::IsControl(command.Auxiliary);
    // Anything still sounding from an earlier command would mix into this one, and the driver has no
    // "silence everything" request: start again from the freshly initialized state.
    if (command_sent_since_reset_ || command.Kind == MusicCommandKind::Stop) Reset();
    if (command.Kind == MusicCommandKind::Stop || control) return;
    if (driver_.requestCall) {
        // The request is made by the next frame's generated code; a number of 0 is a request like any other.
        if (driver_.sfxCall && command.Kind == MusicCommandKind::Effect) {
            sfx_value_ = command.Value;
            sfx_pending_ = 1;
        } else {
            request_value_ = command.Value;
            request_pending_ = 1;
        }
        command_sent_since_reset_ = true;
        return;
    }
    std::uint16_t variable = 0;
    if (command.Kind == MusicCommandKind::Effect) variable = *driver_.sfxVariable;
    else if (command.Auxiliary != 0) variable = driver_.requests[NesDriverConfig::RequestNumberOf(command.Auxiliary) - 1];
    else variable = *driver_.bgmVariable;
    ram_[variable & 0x7FF] = command.Value;
    command_sent_since_reset_ = true;
}
