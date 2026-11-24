// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "devices/cpu/sh2/mame_derived_sh2_adapter.h"
#include "devices/cpu/sh2/sh7604_onchip.h"
#include "sega32x_pwm.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

// The part of the Sega 32X that the two SH-2 processors see, with the 32X's video side left out: the master and the slave SH-2
// with their on-chip peripherals, the 256 KB of SDRAM, the cartridge ROM window, the 4 KB of cache RAM each, the system
// registers (interrupt masks and clears, adapter control), the communication registers shared with the 68000, the PWM
// sound unit, and plausible values for the video registers (bitmap mode, frame buffer control with the vertical blank
// flag) and the frame buffer/palette areas (plain memory).
//
// There is no BIOS: BootFromHeader() does what the 32X BIOS leaves behind (the SH-2 program copied from the cartridge into SDRAM,
// both processors started at the addresses of the cartridge header).
//
// Time is counted in SH-2 clock cycles (23.01136 MHz NTSC); the processor that is furthest behind runs one instruction at a
// time, so a register access always happens in time order with the other processor's and the PWM unit's.
class Sega32xSystem {
public:
    explicit Sega32xSystem(std::vector<std::uint8_t> rom);

    void Reset();
    // The 32X cartridge header (at 0x3D4): source offset, destination in SDRAM, size, the two start addresses and the two
    // vector base addresses. Returns false when the header does not describe a program in the ROM.
    bool BootFromHeader();

    // Runs both SH-2 until each has reached `sh2Cycles`.
    void RunTo(std::uint64_t sh2Cycles);
    // Stops the master SH-2 for good (until Reset()). The game's own master program is an idle loop once the sample player is running,
    // and a Machine that starts the sounds itself does not need it: only the slave runs, which makes the emulation several times faster.
    void HaltMaster() { masterHalted_ = true; }
    std::uint64_t Now() const { return globalCycles_; }

    // The 68000's side: the registers at 0xA15100-0xA1513F as 16-bit values.
    std::uint16_t M68kRead(unsigned offset);
    void M68kWrite(unsigned offset, std::uint16_t value, std::uint16_t mask = 0xFFFF);

    // The vertical blank: asserts the vertical interrupt of both processors that enabled it and sets the flag video reads show.
    void SetVblank(bool vblank);

    // The PWM output, rasterized at `outputRate` for `frames` frames from `startCycle`.
    void RenderPwm(double startCycle, double outputRate, float* left, float* right, std::size_t frames);

    Sega32xPwm& Pwm() { return pwm_; }
    MameDerivedSh2Adapter& Cpu(int index) { return cpu_[index]; }
    std::uint8_t* Sdram() { return sdram_.data(); }
    std::uint16_t CommWord(unsigned index) const { return comm_[index & 7]; }

    // For tests and diagnostics: the SH-2 side reads and writes by a processor (0 master, 1 slave).
    std::uint32_t Read(int cpu, std::uint32_t address, int bytes);
    void Write(int cpu, std::uint32_t address, std::uint32_t value, int bytes);

    // For diagnostics: called for every write an SH-2 makes (processor, address, value, bytes), before it is done.
    using WriteObserver = std::function<void(int cpu, std::uint32_t address, std::uint32_t value, int bytes)>;
    void SetWriteObserverForTest(WriteObserver observer) { writeObserver_ = std::move(observer); }

    static constexpr std::uint32_t kSdramSize = 0x40000;

private:
    enum PendingBit { kVint = 1, kHint = 2, kCmdint = 4, kPwmint = 8 };

    std::uint32_t Rom32(std::uint32_t at) const;
    std::uint16_t SysRead16(int cpu, std::uint32_t offset);
    void SysWrite16(int cpu, std::uint32_t offset, std::uint16_t value, std::uint16_t mask);
    void UpdateInterruptLines(int cpu);
    void StepCpu(int cpu);
    void TakePwmChanges();   // moves the PWM unit's output changes (if any) to the list the rasterizer reads

    std::vector<std::uint8_t> rom_;
    std::vector<std::uint8_t> sdram_;
    std::vector<std::uint8_t> cacheRam_[2];
    std::vector<std::uint8_t> framebuffer_;
    std::vector<std::uint8_t> palette_;
    MameDerivedSh2Adapter cpu_[2];
    std::unique_ptr<Sh7604OnChip> onchip_[2];
    Sega32xPwm pwm_;

    std::uint16_t comm_[8] = {};
    std::uint16_t adapterControl_ = 0;    // 0x4000 bit 15 (frame buffer permission) etc., kept per the master's write
    std::uint8_t enable_[2] = {0, 0};     // bit 3 V, 2 H, 1 CMD, 0 PWM (the low byte of 0x4000)
    std::uint8_t hintInVbl_[2] = {0, 0};
    std::uint8_t pending_[2] = {0, 0};
    std::uint16_t hcount_ = 0;
    std::uint16_t vdpRegs_[8] = {};       // 0x4100-0x410E
    bool vblank_ = false, fbSwap_ = false;
    std::uint16_t dreq_[8] = {};          // 0x4008-0x4017 stored
    std::uint16_t a15102_ = 0;            // bit 0 master, bit 1 slave: command interrupt pending (68000 side view)
    std::uint16_t a15104_ = 0;
    std::uint16_t a15100_ = 0;

    std::uint64_t cycles_[2] = {0, 0};    // each processor's own time
    std::uint64_t globalCycles_ = 0;
    std::uint64_t pwmTime_ = 0;
    std::deque<Sega32xPwm::Change> pwmChanges_;
    float heldLeft_ = 0, heldRight_ = 0;
    bool booted_ = false;
    bool masterHalted_ = false;
    WriteObserver writeObserver_;
};
