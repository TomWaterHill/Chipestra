// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "fds_machine.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

const float kApuDcBlockR = DcBlocker::Coefficient(static_cast<double>(FdsMachine::kApuSampleRateHz));
const float kFdsDcBlockR = DcBlocker::Coefficient(static_cast<double>(FdsMachine::kFdsSampleRateHz));

// The start-up routines have to finish within this many cycles (one frame).
constexpr std::uint64_t kStartWarmupCycles = 29'781;

// How long the picture chip's vertical-blank flag reads as set after the start of a frame.
constexpr std::uint64_t kVblankCycles = 2'273;

// Finds the disk file a <driver><file> element names: same name, and the same CRC-32 when one is given.
const FdsFile* FindDiskFile(const FdsDisk& disk, const NesDriverConfig::DiskFile& wanted, std::string* problem) {
    const FdsFile* sameName = nullptr;
    for (const FdsFile& file : disk.files) {
        if (file.name != wanted.name) continue;
        if (!wanted.crc32 || file.crc32 == *wanted.crc32) return &file;
        sameName = &file;
    }
    if (problem != nullptr) {
        char message[160];
        if (sameName != nullptr)
            std::snprintf(message, sizeof(message), "disk file \"%s\" has CRC-32 %08x, the game XML expects %08x", wanted.name.c_str(),
                          sameName->crc32, *wanted.crc32);
        else
            std::snprintf(message, sizeof(message), "disk file \"%s\" is not on the disk", wanted.name.c_str());
        *problem = message;
    }
    return nullptr;
}

const NesDriverConfig::DiskFile* DriverFileNamed(const NesDriverConfig& driver, const std::string& name) {
    for (const NesDriverConfig::DiskFile& d : driver.diskFiles)
        if (d.name == name) return &d;
    return nullptr;
}

}  // namespace

FdsMachine::FdsMachine(FdsDisk disk, NesDriverConfig driver)
    : disk_(std::move(disk)),
      driver_(std::move(driver)),
      clock_converter_(kNativeRuntimeTickRateHz, ClockRate(kCpuClockHz)) {
    static const char* const kNames[NesApuDeviceAdapter::kChannelCount] = {"Pulse 1", "Pulse 2", "Triangle", "Noise", "DMC"};
    for (std::uint32_t c = 0; c < NesApuDeviceAdapter::kChannelCount; ++c) {
        LogicalChannel channel;
        channel.Id = c;
        channel.DeviceName = "RP2A03";
        channel.ChannelName = kNames[c];
        channel.NativeSampleRate = kApuSampleRateHz;
        channel.RoutingGain = kApuRoutingGain;
        channels_.push_back(std::move(channel));
    }
    LogicalChannel wave;
    wave.Id = kFdsChannelId;
    wave.DeviceName = "RP2C33";
    wave.ChannelName = "Wave";
    wave.NativeSampleRate = kFdsSampleRateHz;
    wave.RoutingGain = kFdsRoutingGain;
    channels_.push_back(std::move(wave));
}

const FdsFile* FdsMachine::FindFile(const NesDriverConfig::DiskFile& wanted) const { return FindDiskFile(disk_, wanted, nullptr); }

void FdsMachine::Initialize() {
    cpu_ = std::make_unique<MameDerivedRp2a03Adapter>();
    apu_ = std::make_unique<NesApuDeviceAdapter>();
    fds_ = std::make_unique<Rp2c33SoundDeviceAdapter>();
    cpu_->SetMemoryCallbacks([this](std::uint16_t address) { return ReadBus(address); },
                             [this](std::uint16_t address, std::uint8_t value) { WriteBus(address, value); });
    apu_->SetIrqCallback([this](bool asserted) {
        cpu_->SetInputLine(M6502InputLine::ApuIrq, asserted ? M6502InputLineState::Assert : M6502InputLineState::Clear);
    });
    // The DMC reads sample bytes from the CPU's address space; the original's DMC reads cost the CPU no cycles.
    apu_->SetMemoryReadCallback([this](std::uint16_t address) -> std::uint8_t {
        return address >= kRamAdapterFirst && address <= kRamAdapterLast ? adapter_ram_[address - kRamAdapterFirst] : std::uint8_t{0};
    });
    start_code_ = BuildStartCode(driver_.init, kStartCodeAddress);
    frame_code_ = BuildFrameCode(driver_.frame);
    ColdStart(0);
}

void FdsMachine::Reset() { ColdStart(0); }

void FdsMachine::Shutdown() {}

void FdsMachine::LoadFiles(std::uint32_t setNumber) {
    adapter_ram_.fill(0);
    auto load = [this](const std::string& name) {
        const NesDriverConfig::DiskFile* wanted = DriverFileNamed(driver_, name);
        const FdsFile* file = wanted != nullptr ? FindFile(*wanted) : nullptr;
        if (file == nullptr || file->kind != 0) return;  // checked when the Machine was assembled
        for (std::size_t i = 0; i < file->data.size(); ++i) {
            const std::uint32_t address = static_cast<std::uint32_t>(file->loadAddress) + static_cast<std::uint32_t>(i);
            if (address >= kRamAdapterFirst && address <= kRamAdapterLast) adapter_ram_[address - kRamAdapterFirst] = file->data[i];
        }
    };
    for (const std::string& name : driver_.baseFiles) load(name);
    if (setNumber >= 1 && setNumber <= driver_.sets.size())
        for (const std::string& name : driver_.sets[setNumber - 1].files) load(name);
}

void FdsMachine::ColdStart(std::uint32_t setNumber) {
    ram_.fill(0xFF);
    if (driver_.ramClear) {
        for (std::uint32_t a = driver_.ramClear->first; a <= driver_.ramClear->second; ++a) ram_[a & 0x7FF] = 0;
    }
    LoadFiles(setNumber);
    loaded_set_ = setNumber;
    for (const auto& [address, value] : driver_.ramSet) ram_[address & 0x7FF] = value;
    if (setNumber >= 1 && setNumber <= driver_.sets.size())
        for (const auto& [address, value] : driver_.sets[setNumber - 1].ramSet) ram_[address & 0x7FF] = value;
    cpu_cycles_done_ = 0;
    apu_cycles_ = 0;
    fds_cycles_ = 0;
    origin_cycles_ = 0;
    cumulative_ticks_ = 0;
    frames_started_ = 0;
    last_frame_start_cycle_ = 0;
    command_sent_since_reset_ = false;
    bus_stats_ = BusStats{};
    dc_block_.fill(DcBlocker{});
    fds_dc_block_ = DcBlocker{};
    for (auto& buffer : pending_) buffer.clear();
    pending_fds_.clear();

    starting_up_ = true;
    cpu_->Reset();
    apu_->Reset();
    fds_->Reset();
    // What the disk system's BIOS does before it hands over to a game (it runs once at power-on, before any game
    // code): the disk adapter's sound registers are switched on ($4023 bit 1), the APU gets the DMC off, the frame
    // IRQ inhibited (a game's IRQ vector is the BIOS's, which is not here), pulse 1/2, triangle and noise enabled,
    // and the wavetable chip gets volume 0 in direct mode and the envelope speed 0xE8.
    fds_->SetSoundIoEnabled(true);
    apu_->Write(0x10, 0x00);
    apu_->Write(0x17, 0xC0);
    apu_->Write(0x15, 0x0F);
    fds_->WriteIo(0x4080, 0x80);
    fds_->WriteIo(0x408A, 0xE8);
    RunCpuTo(kStartWarmupCycles, false);
    // The driver's warm-up frames of the frame routines with nothing requested.
    origin_cycles_ = cpu_cycles_done_;
    RunCpuTo(origin_cycles_ + driver_.warmupFrames * kCyclesPerTwoFrames / 2, true);
    SyncApu(cpu_cycles_done_);
    SyncFds(cpu_cycles_done_);
    for (auto& buffer : pending_) buffer.clear();
    pending_fds_.clear();
    origin_cycles_ = cpu_cycles_done_;
    frames_started_ = 0;
    last_frame_start_cycle_ = cpu_cycles_done_;
    starting_up_ = false;
}

std::uint8_t FdsMachine::MutedMask() const {
    std::uint8_t mask = 0;
    for (std::size_t c = 0; c < NesApuDeviceAdapter::kChannelCount; ++c)
        if (channels_[c].Mute) mask = static_cast<std::uint8_t>(mask | (1u << c));
    return mask;
}

void FdsMachine::SyncApu(std::uint64_t cpuCycle) {
    if (cpuCycle <= apu_cycles_) return;
    apu_->SetMutedChannels(MutedMask());
    apu_->Render(cpuCycle - apu_cycles_, pending_);
    apu_cycles_ = cpuCycle;
}

void FdsMachine::SyncFds(std::uint64_t cpuCycle) {
    if (cpuCycle <= fds_cycles_) return;
    fds_->Render(cpuCycle - fds_cycles_, pending_fds_);
    fds_cycles_ = cpuCycle;
}

void FdsMachine::RunCpuTo(std::uint64_t targetCycle, bool frames) {
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
        if (raised) cpu_->SetInputLine(M6502InputLine::Nmi, M6502InputLineState::Clear);
    }
}

std::uint8_t FdsMachine::ReadBus(std::uint16_t address) {
    if (address < 0x2000) return ram_[address & 0x7FF];
    if (address < 0x4000) {
        ++bus_stats_.ppuReads;
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
    if (address >= 0x4020 && address <= 0x4033) {
        ++bus_stats_.diskReads;
        return 0;
    }
    if ((address >= 0x4040 && address < 0x4080) || address == 0x4090 || address == 0x4092) {
        SyncFds(Now());
        return fds_->ReadIo(address);
    }
    if (address >= kStartCodeAddress && address < kStartCodeAddress + 0x100) {
        const std::size_t i = address - kStartCodeAddress;
        return i < start_code_.size() ? start_code_[i] : std::uint8_t{0};
    }
    if (address >= kFrameCodeAddress && address < kFrameCodeAddress + 0x100) {
        const std::size_t i = address - kFrameCodeAddress;
        return i < frame_code_.size() ? frame_code_[i] : std::uint8_t{0};
    }
    if (address >= kRamAdapterFirst && address <= kRamAdapterLast) return adapter_ram_[address - kRamAdapterFirst];
    if (address >= 0xE000) {
        // The reset and NMI vectors lead to the generated code; the IRQ vector and the rest are the BIOS's.
        if (address == 0xFFFA) return kFrameCodeAddress & 0xFF;
        if (address == 0xFFFB) return kFrameCodeAddress >> 8;
        if (address == 0xFFFC) return kStartCodeAddress & 0xFF;
        if (address == 0xFFFD) return kStartCodeAddress >> 8;
        if (address < 0xFFFA) ++bus_stats_.biosReads;
        return 0;
    }
    return 0;
}

void FdsMachine::WriteBus(std::uint16_t address, std::uint8_t value) {
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
        if (register_write_observer_ && !starting_up_ && address != 0x4017)
            register_write_observer_(frames_started_ > 0 ? frames_started_ - 1 : 0, address, value);
    } else if (address >= 0x4020 && address <= 0x4033) {
        ++bus_stats_.diskWrites;
        if (address == 0x4023) fds_->SetSoundIoEnabled((value & 0x02) != 0);
    } else if (address >= 0x4040 && address <= 0x4092) {
        SyncFds(Now());
        fds_->WriteIo(address, value);
        if (register_write_observer_ && !starting_up_ && address <= 0x408A)
            register_write_observer_(frames_started_ > 0 ? frames_started_ - 1 : 0, address, value);
    } else if (address >= kRamAdapterFirst && address <= kRamAdapterLast) {
        adapter_ram_[address - kRamAdapterFirst] = value;
    } else if (address >= 0xE000) {
        ++bus_stats_.biosWrites;
    }
}

void FdsMachine::WriteChannels(LogicalChannelSink* sink) {
    for (std::size_t c = 0; c < NesApuDeviceAdapter::kChannelCount; ++c) {
        std::vector<float>& samples = pending_[c];
        dc_block_[c].Process(samples.data(), samples.size(), kApuDcBlockR);
        if (sink != nullptr) sink->WriteSamples(channels_[c].Id, samples.data(), samples.size());
        samples.clear();
    }
    fds_dc_block_.Process(pending_fds_.data(), pending_fds_.size(), kFdsDcBlockR);
    if (sink != nullptr) sink->WriteSamples(kFdsChannelId, pending_fds_.data(), pending_fds_.size());
    pending_fds_.clear();
}

void FdsMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) sink->RegisterChannel(channel.Id);
        channels_registered_ = true;
    }
    cumulative_ticks_ += duration.ticks;
    const std::uint64_t target = origin_cycles_ + clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_));
    RunCpuTo(target, true);
    SyncApu(cpu_cycles_done_);
    SyncFds(cpu_cycles_done_);
    WriteChannels(sink);
}

bool FdsMachine::AcceptsCommand(const MusicCommand& command) const {
    if (command.Kind == MusicCommandKind::Stop) return true;
    if (command.Kind != MusicCommandKind::Raw) return false;
    return NesDriverConfig::SetNumberOf(command.Auxiliary) <= driver_.sets.size() &&
           NesDriverConfig::RequestNumberOf(command.Auxiliary) <= driver_.requests.size();
}

void FdsMachine::SendCommand(const MusicCommand& command) {
    if (!AcceptsCommand(command)) return;
    if (command.Kind == MusicCommandKind::Stop) {
        ColdStart(0);
        return;
    }
    const std::uint32_t setNumber = NesDriverConfig::SetNumberOf(command.Auxiliary);
    const std::uint32_t requestNumber = NesDriverConfig::RequestNumberOf(command.Auxiliary);
    // Anything still sounding from an earlier command would mix into this one, and the driver has no "silence
    // everything" request: start again from the freshly initialized state, with the files this track needs.
    if (command_sent_since_reset_ || setNumber != loaded_set_ || requestNumber == 0) ColdStart(setNumber);
    if (requestNumber == 0) return;
    ram_[driver_.requests[requestNumber - 1] & 0x7FF] = command.Value;
    command_sent_since_reset_ = true;
}

FdsMachineInputs AssembleFdsMachineInputs(const MachineResources& resources) {
    FdsMachineInputs inputs;
    if (resources.Count() == 0) return inputs;

    const ResolvedResource* disk = resources.Find(kFdsDiskResource);
    if (disk == nullptr) throw std::runtime_error(std::string("Famicom Disk System: required resource \"") + kFdsDiskResource + "\" is missing");
    const ResolvedResource* driver = resources.Find(kFdsDriverResource);
    if (driver == nullptr)
        throw std::runtime_error(std::string("Famicom Disk System: required resource \"") + kFdsDriverResource +
                                 "\" is missing (the game XML needs a <driver> element)");

    std::string error;
    if (!ParseNesDriverConfig(std::string_view(reinterpret_cast<const char*>(driver->Data()), driver->Size()), inputs.driver, &error))
        throw std::runtime_error("Famicom Disk System: " + error);
    inputs.disk = ParseFdsDisk(disk->Data(), disk->Size());

    // Every file a track can load must be on the disk, with the expected CRC-32, and fit in the adapter's RAM.
    for (const NesDriverConfig::DiskFile& wanted : inputs.driver.diskFiles) {
        std::string problem;
        const FdsFile* file = FindDiskFile(inputs.disk, wanted, &problem);
        if (file == nullptr) throw std::runtime_error("Famicom Disk System: " + problem);
        if (file->kind != 0)
            throw std::runtime_error("Famicom Disk System: disk file \"" + wanted.name + "\" is not program data (kind " + std::to_string(file->kind) + ")");
        if (!file->data.empty() && (file->loadAddress < FdsMachine::kRamAdapterFirst ||
                                    static_cast<std::uint32_t>(file->loadAddress) + file->data.size() - 1 > FdsMachine::kRamAdapterLast))
            throw std::runtime_error("Famicom Disk System: disk file \"" + wanted.name + "\" is not inside the adapter RAM ($6000-$DFFF)");
    }
    return inputs;
}
