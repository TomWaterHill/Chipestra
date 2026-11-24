// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
//
// Register map and interrupt behaviour after MAME's 32X (src/mame/shared/mega32x.cpp, BSD-3-Clause, copyright-holders
// David Haywood); see the header for what is left out.

#include "sega32x_system.h"

#include <algorithm>

namespace {
constexpr int kLevelV = 12, kLevelH = 10, kLevelCmd = 8, kLevelPwm = 6;
}

Sega32xSystem::Sega32xSystem(std::vector<std::uint8_t> rom) : rom_(std::move(rom)) {
    onchip_[0] = std::make_unique<Sh7604OnChip>(cpu_[0], false);
    onchip_[1] = std::make_unique<Sh7604OnChip>(cpu_[1], true);
    sdram_.assign(kSdramSize, 0);
    cacheRam_[0].assign(0x1000, 0);
    cacheRam_[1].assign(0x1000, 0);
    framebuffer_.assign(0x40000, 0);
    palette_.assign(0x200, 0);
    for (int i = 0; i < 2; ++i) {
        ISh2Core::Callbacks cb;
        cb.read8 = [this, i](std::uint32_t a) { return static_cast<std::uint8_t>(Read(i, a, 1)); };
        cb.read16 = [this, i](std::uint32_t a) { return static_cast<std::uint16_t>(Read(i, a, 2)); };
        cb.read32 = [this, i](std::uint32_t a) { return Read(i, a, 4); };
        cb.write8 = [this, i](std::uint32_t a, std::uint8_t v) { Write(i, a, v, 1); };
        cb.write16 = [this, i](std::uint32_t a, std::uint16_t v) { Write(i, a, v, 2); };
        cb.write32 = [this, i](std::uint32_t a, std::uint32_t v) { Write(i, a, v, 4); };
        cpu_[i].SetCallbacks(cb);
        cpu_[i].SetReadWindow(0, sdram_.data(), 0xDE000000u, 0x06000000u, kSdramSize - 1, kSdramSize);
        cpu_[i].SetReadWindow(1, rom_.data(), 0xDFC00000u, 0x02000000u, 0x3FFFFFu, static_cast<std::uint32_t>(std::min<std::size_t>(rom_.size(), 0x400000)));
        // The DMA controller moves data over the CPU's own bus. The partners that raise DREQ (the FIFO the 68000 fills, the PWM unit) are not
        // connected: only auto-request transfers run.
        onchip_[i]->SetDmaBus([this, i](std::uint32_t addr, int bytes) { return Read(i, addr, bytes); },
                              [this, i](std::uint32_t addr, std::uint32_t value, int bytes) { Write(i, addr, value, bytes); },
                              [](int) { return false; });
    }
    pwm_.SetInterruptCallback([this] {
        for (int i = 0; i < 2; ++i) {
            if (enable_[i] & 1) pending_[i] |= kPwmint;
            UpdateInterruptLines(i);
        }
    });
    Reset();
}

void Sega32xSystem::Reset() {
    std::fill(sdram_.begin(), sdram_.end(), 0);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0);
    std::fill(palette_.begin(), palette_.end(), 0);
    for (auto& c : cacheRam_) std::fill(c.begin(), c.end(), 0);
    for (auto& c : comm_) c = 0;
    for (auto& d : dreq_) d = 0;
    for (auto& v : vdpRegs_) v = 0;
    adapterControl_ = 0; hcount_ = 0; a15100_ = 0; a15102_ = 0; a15104_ = 0;
    enable_[0] = enable_[1] = 0; hintInVbl_[0] = hintInVbl_[1] = 0; pending_[0] = pending_[1] = 0;
    vblank_ = false; fbSwap_ = false;
    cycles_[0] = cycles_[1] = 0; globalCycles_ = 0;
    pwm_.Reset();
    pwmChanges_.clear();
    heldLeft_ = heldRight_ = 0;
    for (int i = 0; i < 2; ++i) { onchip_[i]->Reset(); cpu_[i].Reset(); }
    booted_ = false;
    masterHalted_ = false;
}

std::uint32_t Sega32xSystem::Rom32(std::uint32_t at) const {
    if (at + 4 > rom_.size()) return 0;
    return static_cast<std::uint32_t>(rom_[at]) << 24 | rom_[at + 1] << 16 | rom_[at + 2] << 8 | rom_[at + 3];
}

bool Sega32xSystem::BootFromHeader() {
    const std::uint32_t source = Rom32(0x3D4), destination = Rom32(0x3D8), size = Rom32(0x3DC);
    if (size == 0 || static_cast<std::uint64_t>(source) + size > rom_.size() || static_cast<std::uint64_t>(destination & 0x3FFFF) + size > kSdramSize) return false;
    std::copy(rom_.begin() + source, rom_.begin() + source + size, sdram_.begin() + (destination & 0x3FFFF));
    const std::uint32_t start[2] = {Rom32(0x3E0), Rom32(0x3E4)};
    const std::uint32_t vbr[2] = {Rom32(0x3E8), Rom32(0x3EC)};
    for (int i = 0; i < 2; ++i) {
        MameDerivedSh2Adapter::Registers r;
        r.pc = start[i];
        r.r[15] = 0x06040000;
        r.sr = 0xF0;
        r.vbr = vbr[i];
        cpu_[i].SetRegisters(r);
        cycles_[i] = globalCycles_;
    }
    // what the BIOS leaves in the communication registers: the processors' "M_OK" and "S_OK"
    comm_[0] = 0x4D5F; comm_[1] = 0x4F4B; comm_[2] = 0x535F; comm_[3] = 0x4F4B;
    booted_ = true;
    return true;
}

// ---- the SH-2 side ----

std::uint16_t Sega32xSystem::SysRead16(int cpu, std::uint32_t o) {
    pwm_.AdvanceTo(cycles_[cpu] + cpu_[cpu].CyclesConsumedThisCall());
    switch (o) {
    case 0x00: return static_cast<std::uint16_t>(0x0200 | (adapterControl_ & 0x8000) | (hintInVbl_[cpu] ? 0x80 : 0) | enable_[cpu]);
    case 0x04: return hcount_;
    case 0x06: return dreq_[0];
    case 0x08: case 0x0A: case 0x0C: case 0x0E: case 0x10: case 0x12: return dreq_[(o - 6) / 2];
    default: break;
    }
    if (o >= 0x20 && o < 0x30) return comm_[(o - 0x20) / 2];
    if (o >= 0x30 && o < 0x40) {
        const std::uint16_t v = pwm_.Read(o - 0x30);
        TakePwmChanges();
        return v;
    }
    if (o >= 0x100 && o < 0x110) {
        if (o == 0x10A) return static_cast<std::uint16_t>((vblank_ ? 0x8000 : 0) | (fbSwap_ ? 1 : 0));
        return vdpRegs_[(o - 0x100) / 2];
    }
    if (o >= 0x200 && o < 0x400) return static_cast<std::uint16_t>(palette_[o - 0x200] << 8 | palette_[o - 0x200 + 1]);
    return 0;
}

void Sega32xSystem::SysWrite16(int cpu, std::uint32_t o, std::uint16_t value, std::uint16_t mask) {
    pwm_.AdvanceTo(cycles_[cpu] + cpu_[cpu].CyclesConsumedThisCall());
    auto combine = [&](std::uint16_t old) { return static_cast<std::uint16_t>((old & ~mask) | (value & mask)); };
    switch (o) {
    case 0x00:
        if (mask & 0xFF00) adapterControl_ = static_cast<std::uint16_t>((adapterControl_ & ~0x8000) | (value & 0x8000));
        if (mask & 0x00FF) {
            hintInVbl_[cpu] = value & 0x80;
            enable_[cpu] = static_cast<std::uint8_t>(value & 0x0F);
            UpdateInterruptLines(cpu);
        }
        return;
    case 0x04: hcount_ = combine(hcount_) & 0xFF; return;
    case 0x08: case 0x0A: case 0x0C: case 0x0E: case 0x10: case 0x12: dreq_[(o - 6) / 2] = combine(dreq_[(o - 6) / 2]); return;
    case 0x14: return;                                                       // the reset interrupt: not used
    case 0x16: pending_[cpu] &= ~kVint; UpdateInterruptLines(cpu); return;
    case 0x18: pending_[cpu] &= ~kHint; UpdateInterruptLines(cpu); return;
    case 0x1A: pending_[cpu] &= ~kCmdint; a15102_ &= ~(1 << cpu); UpdateInterruptLines(cpu); return;
    case 0x1C: pending_[cpu] &= ~kPwmint; UpdateInterruptLines(cpu); return;
    case 0x1E: return;
    default: break;
    }
    if (o >= 0x20 && o < 0x30) { auto& w = comm_[(o - 0x20) / 2]; w = combine(w); return; }
    if (o >= 0x30 && o < 0x40) {
        if (mask == 0xFFFF) pwm_.Write(o - 0x30, value);
        else pwm_.Write(o - 0x30, combine(pwm_.Read(o - 0x30)));
        TakePwmChanges();
        return;
    }
    if (o >= 0x100 && o < 0x110) {
        if (o == 0x10A) { if (mask & 0xFF) fbSwap_ = (value & 1) != 0; return; }
        vdpRegs_[(o - 0x100) / 2] = combine(vdpRegs_[(o - 0x100) / 2]);
        return;
    }
    if (o >= 0x200 && o < 0x400) {
        const std::uint16_t old = static_cast<std::uint16_t>(palette_[o - 0x200] << 8 | palette_[o - 0x200 + 1]);
        const std::uint16_t updated = combine(old);
        palette_[o - 0x200] = static_cast<std::uint8_t>(updated >> 8);
        palette_[o - 0x200 + 1] = static_cast<std::uint8_t>(updated);
    }
}

std::uint32_t Sega32xSystem::Read(int cpu, std::uint32_t address, int bytes) {
    // the common reads (the SDRAM and the cartridge, whole inside their area) first
    if ((address >> 30) == 0) {
        const std::uint32_t a = address & 0x1FFFFFFF;
        const std::uint8_t* source = nullptr;
        std::uint32_t offset = 0;
        if (a >= 0x06000000 && a < 0x08000000) { source = sdram_.data(); offset = a & 0x3FFFF; if (offset + bytes > kSdramSize) source = nullptr; }
        else if (a >= 0x02000000 && a < 0x02400000) { source = rom_.data(); offset = a - 0x02000000; if (offset + bytes > rom_.size()) source = nullptr; }
        if (source != nullptr) {
            std::uint32_t result = 0;
            for (int i = 0; i < bytes; ++i) result = result << 8 | source[offset + i];
            return result;
        }
    }
    if (Sh7604OnChip::Contains(address)) return onchip_[cpu]->Read(address, bytes);
    const std::uint32_t top = address >> 29;
    if (top >= 2 && top <= 5) return 0xA5A5A5A5u & (bytes == 4 ? 0xFFFFFFFFu : bytes == 2 ? 0xFFFFu : 0xFFu);   // the cache address array
    std::uint32_t result = 0;
    if (top == 6) {
        for (int i = 0; i < bytes; ++i) result = result << 8 | cacheRam_[cpu][(address + i) & 0xFFF];
        return result;
    }
    const std::uint32_t a = address & 0x1FFFFFFF;
    if (a >= 0x4000 && a < 0x4400) {
        const std::uint32_t o = a - 0x4000;
        if (bytes == 2 && (o & 1) == 0) return SysRead16(cpu, o);
        if (bytes == 4 && (o & 1) == 0) return static_cast<std::uint32_t>(SysRead16(cpu, o)) << 16 | SysRead16(cpu, o + 2);
        if (bytes == 1) { const std::uint16_t v = SysRead16(cpu, o & ~1u); return (o & 1) ? (v & 0xFF) : (v >> 8); }
        return 0;
    }
    for (int i = 0; i < bytes; ++i) {
        const std::uint32_t b = a + i;
        std::uint8_t v = 0;
        if (b >= 0x02000000 && b < 0x02400000) { const std::uint32_t r = b - 0x02000000; v = r < rom_.size() ? rom_[r] : 0; }
        else if (b >= 0x04000000 && b < 0x04080000) v = framebuffer_[b & 0x3FFFF];
        else if (b >= 0x06000000 && b < 0x08000000) v = sdram_[b & 0x3FFFF];
        result = result << 8 | v;
    }
    return result;
}

void Sega32xSystem::Write(int cpu, std::uint32_t address, std::uint32_t value, int bytes) {
    if (writeObserver_) writeObserver_(cpu, address, value, bytes);
    if ((address >> 30) == 0) {
        const std::uint32_t a = address & 0x1FFFFFFF;
        if (a >= 0x06000000 && a < 0x08000000) {
            const std::uint32_t offset = a & 0x3FFFF;
            if (offset + bytes <= kSdramSize) {
                for (int i = 0; i < bytes; ++i) sdram_[offset + i] = static_cast<std::uint8_t>(value >> ((bytes - 1 - i) * 8));
                return;
            }
        }
    }
    if (Sh7604OnChip::Contains(address)) { onchip_[cpu]->Write(address, value, bytes); return; }
    const std::uint32_t top = address >> 29;
    if (top >= 2 && top <= 5) return;
    if (top == 6) {
        for (int i = 0; i < bytes; ++i) cacheRam_[cpu][(address + i) & 0xFFF] = static_cast<std::uint8_t>(value >> ((bytes - 1 - i) * 8));
        return;
    }
    const std::uint32_t a = address & 0x1FFFFFFF;
    if (a >= 0x4000 && a < 0x4400) {
        const std::uint32_t o = a - 0x4000;
        if (bytes == 2 && (o & 1) == 0) SysWrite16(cpu, o, static_cast<std::uint16_t>(value), 0xFFFF);
        else if (bytes == 4 && (o & 1) == 0) { SysWrite16(cpu, o, static_cast<std::uint16_t>(value >> 16), 0xFFFF); SysWrite16(cpu, o + 2, static_cast<std::uint16_t>(value), 0xFFFF); }
        else if (bytes == 1) SysWrite16(cpu, o & ~1u, static_cast<std::uint16_t>((o & 1) ? (value & 0xFF) : (value & 0xFF) << 8), (o & 1) ? 0x00FF : 0xFF00);
        return;
    }
    for (int i = 0; i < bytes; ++i) {
        const std::uint32_t b = a + i;
        const std::uint8_t v = static_cast<std::uint8_t>(value >> ((bytes - 1 - i) * 8));
        if (b >= 0x04000000 && b < 0x04080000) framebuffer_[b & 0x3FFFF] = v;
        else if (b >= 0x06000000 && b < 0x08000000) sdram_[b & 0x3FFFF] = v;
    }
}

void Sega32xSystem::UpdateInterruptLines(int cpu) {
    const std::uint8_t active = static_cast<std::uint8_t>(pending_[cpu] & (((enable_[cpu] >> 3) & 1) * kVint | ((enable_[cpu] >> 2) & 1) * kHint | ((enable_[cpu] >> 1) & 1) * kCmdint | (enable_[cpu] & 1) * kPwmint));
    cpu_[cpu].SetInterruptLine(kLevelV, (active & kVint) != 0);
    cpu_[cpu].SetInterruptLine(kLevelH, (active & kHint) != 0);
    cpu_[cpu].SetInterruptLine(kLevelCmd, (active & kCmdint) != 0);
    cpu_[cpu].SetInterruptLine(kLevelPwm, (active & kPwmint) != 0);
}

void Sega32xSystem::SetVblank(bool vblank) {
    vblank_ = vblank;
    if (vblank) {
        for (int i = 0; i < 2; ++i) {
            if (enable_[i] & 8) pending_[i] |= kVint;
            UpdateInterruptLines(i);
        }
    }
}

// ---- the 68000's side ----

std::uint16_t Sega32xSystem::M68kRead(unsigned o) {
    if (o < 0x20) {
        switch (o) {
        case 0x00: return static_cast<std::uint16_t>((adapterControl_ & 0x8000) | 0x0003 | a15100_);
        case 0x02: return a15102_;
        case 0x04: return a15104_;
        case 0x06: return dreq_[0];
        default: return o >= 0x08 ? dreq_[(o - 6) / 2 < 8 ? (o - 6) / 2 : 0] : 0;
        }
    }
    if (o < 0x30) return comm_[(o - 0x20) / 2];
    if (o < 0x40) { pwm_.AdvanceTo(globalCycles_); return pwm_.Read(o - 0x30); }
    return 0;
}

void Sega32xSystem::M68kWrite(unsigned o, std::uint16_t value, std::uint16_t mask) {
    auto combine = [&](std::uint16_t old) { return static_cast<std::uint16_t>((old & ~mask) | (value & mask)); };
    if (o < 0x20) {
        switch (o) {
        case 0x00: if (mask & 0xFF00) adapterControl_ = static_cast<std::uint16_t>((adapterControl_ & ~0x8000) | (value & 0x8000)); return;
        case 0x02:
            if (mask & 0xFF) {
                for (int i = 0; i < 2; ++i) {
                    if (value & (1 << i)) { a15102_ |= (1 << i); if (enable_[i] & 2) pending_[i] |= kCmdint; UpdateInterruptLines(i); }
                }
            }
            return;
        case 0x04: a15104_ = combine(a15104_); return;
        default: if (o >= 0x06) dreq_[(o - 6) / 2 < 8 ? (o - 6) / 2 : 0] = combine(dreq_[(o - 6) / 2 < 8 ? (o - 6) / 2 : 0]); return;
        }
    }
    if (o < 0x30) { auto& w = comm_[(o - 0x20) / 2]; w = combine(w); return; }
    if (o < 0x40) {
        pwm_.AdvanceTo(globalCycles_);
        std::uint16_t v = value;
        if (o == 0x30) v = static_cast<std::uint16_t>((value & 0x7F) | (pwm_.Read(0) & 0xFF80));   // the 68000 can write only the low seven bits
        else if (mask != 0xFFFF) v = combine(pwm_.Read(o - 0x30));
        pwm_.Write(o - 0x30, v);
        auto changes = pwm_.TakeChanges();
        for (auto& c : changes) pwmChanges_.push_back(c);
    }
}

// ---- running ----

void Sega32xSystem::TakePwmChanges() {
    if (!pwm_.HasChanges()) return;
    auto changes = pwm_.TakeChanges();
    for (auto& c : changes) pwmChanges_.push_back(c);
}

void Sega32xSystem::StepCpu(int cpu) {
    if (pwm_.Due(cycles_[cpu])) {
        pwm_.AdvanceTo(cycles_[cpu]);
        TakePwmChanges();
    }
    const std::uint64_t used = cpu_[cpu].Execute(1);
    onchip_[cpu]->Advance(used);
    cycles_[cpu] += used;
}

void Sega32xSystem::RunTo(std::uint64_t target) {
    while (true) {
        const int cpu = (masterHalted_ || cycles_[0] > cycles_[1]) ? 1 : 0;
        if (cycles_[cpu] >= target) break;
        globalCycles_ = cycles_[cpu];
        StepCpu(cpu);
    }
    globalCycles_ = target;
    pwm_.AdvanceTo(target);
    TakePwmChanges();
}

void Sega32xSystem::RenderPwm(double startCycle, double outputRate, float* left, float* right, std::size_t frames) {
    pwm_.Rasterize(pwmChanges_, startCycle, outputRate, heldLeft_, heldRight_, left, right, frames);
    // only the changes the frames above have applied are dropped (a change between the last frame and the next block's first is kept)
    const double lastFrame = startCycle + static_cast<double>(frames > 0 ? frames - 1 : 0) * pwm_.ClockHz() / outputRate;
    while (!pwmChanges_.empty() && static_cast<double>(pwmChanges_.front().clock) <= lastFrame) pwmChanges_.pop_front();
}
