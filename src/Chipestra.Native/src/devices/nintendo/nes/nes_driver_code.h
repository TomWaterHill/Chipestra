// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstddef>
#include <iterator>
#include <cstdint>
#include <vector>

// The few bytes of generated 6502 code the Famicom/NES and Famicom Disk System Machines use to call a game's
// sound driver (see NesMachine): `JSR routine` for each routine, then a trailer. Shared by both Machines.

// MMC3 cartridges: the bank switches around the routines (the game's own sound update does the same with
// R6 and R7). `banks` are {R6, R7} of the state to switch to; each register is written as the game does,
// through $8000 (select, PRG mode 1) and $8001 (data).
// VRC6 cartridges: the same, with the two values written to the 16 KB bank register ($8000) and the 8 KB bank
// register ($C000) (`vrc6` set; the fields keep their names, r6 / callR6 is the $8000 value, r7 the $C000 value).
// VRC7 cartridges (`vrc7` set): three values for the banks at $8000, $A000 and $C000 (r6, r7, r8), written to the
// registers at $8000, $8010 (or $8008 on a board whose register line is A3; `vrc7Line` is 3 or 4) and $9000.
// Namco 163 cartridges (`n163` set): three values for the banks at $8000, $A000 and $C000 (r6, r7, r8), written to
// the registers at $E000, $E800 and $F000; X and Y are cleared before each call (the driver may read either).
// FME-7 / Sunsoft 5B cartridges (`fme7` set): two values for the commands 9 and A (r6, r7: the banks at $8000 and
// $A000), written through the command register ($8000) and the data register ($A000). Command B (the bank at
// $C000, where the DMC reads) is never written, the driver chooses it; X and Y are cleared before each call.
struct CodeBanks {
    std::uint8_t callR6, callR7, idleR6, idleR7;
    bool vrc6 = false;
    bool vrc7 = false;
    bool n163 = false;
    bool fme7 = false;
    std::uint8_t callR8 = 0, idleR8 = 0;
    std::uint8_t vrc7Line = 4;
    bool mmc3PrgMode0 = false;  // MMC3: PRG mode 0 ($8000 = R6) instead of the default mode 1 ($C000 = R6)
};

inline void AppendBankSwitch(std::vector<std::uint8_t>& code, std::uint8_t r6, std::uint8_t r7, bool vrc6 = false,
                             bool vrc7 = false, std::uint8_t r8 = 0, std::uint8_t vrc7Line = 4, bool n163 = false,
                             bool fme7 = false, bool mmc3PrgMode0 = false) {
    if (fme7) {
        const std::uint8_t bytes[] = {0xA9, 0x09, 0x8D, 0x00, 0x80, 0xA9, r6, 0x8D, 0x00, 0xA0,
                                      0xA9, 0x0A, 0x8D, 0x00, 0x80, 0xA9, r7, 0x8D, 0x00, 0xA0};
        code.insert(code.end(), std::begin(bytes), std::end(bytes));
        return;
    }
    if (n163) {
        const std::uint8_t bytes[] = {0xA9, r6, 0x8D, 0x00, 0xE0, 0xA9, r7, 0x8D, 0x00, 0xE8, 0xA9, r8, 0x8D, 0x00, 0xF0};
        code.insert(code.end(), std::begin(bytes), std::end(bytes));
        return;
    }
    if (vrc7) {
        const std::uint8_t secondLow = vrc7Line == 3 ? 0x08 : 0x10;
        const std::uint8_t bytes[] = {0xA9, r6, 0x8D, 0x00, 0x80, 0xA9, r7, 0x8D, secondLow, 0x80, 0xA9, r8, 0x8D, 0x00, 0x90};
        code.insert(code.end(), std::begin(bytes), std::end(bytes));
        return;
    }
    if (vrc6) {
        const std::uint8_t bytes[] = {0xA9, r6, 0x8D, 0x00, 0x80, 0xA9, r7, 0x8D, 0x00, 0xC0};
        code.insert(code.end(), std::begin(bytes), std::end(bytes));
        return;
    }
    const std::uint8_t mode = mmc3PrgMode0 ? 0x00 : 0x40;
    const std::uint8_t bytes[] = {0xA9, static_cast<std::uint8_t>(mode | 0x06), 0x8D, 0x00, 0x80, 0xA9, r6, 0x8D, 0x01, 0x80,
                                  0xA9, static_cast<std::uint8_t>(mode | 0x07), 0x8D, 0x00, 0x80, 0xA9, r7, 0x8D, 0x01, 0x80};
    code.insert(code.end(), std::begin(bytes), std::end(bytes));
}

inline void AppendCall(std::vector<std::uint8_t>& code, std::uint16_t routine, bool clearX, bool clearY = false) {
    if (clearX) {
        code.push_back(0xA2);  // LDX #0: the sound routines may read the X the game was interrupted with
        code.push_back(0x00);
    }
    if (clearY) {
        code.push_back(0xA0);  // LDY #0, likewise
        code.push_back(0x00);
    }
    code.push_back(0x20);
    code.push_back(static_cast<std::uint8_t>(routine & 0xFF));
    code.push_back(static_cast<std::uint8_t>(routine >> 8));
}

// start-up code: JSR for every init routine, then `JMP self` (the CPU spins there). With `banks` the routines
// are called between the two bank switches, with X = 0.
inline void AppendCallBanks(std::vector<std::uint8_t>& code, const CodeBanks& banks) {
    AppendBankSwitch(code, banks.callR6, banks.callR7, banks.vrc6, banks.vrc7, banks.callR8, banks.vrc7Line, banks.n163, banks.fme7, banks.mmc3PrgMode0);
}
inline void AppendIdleBanks(std::vector<std::uint8_t>& code, const CodeBanks& banks) {
    AppendBankSwitch(code, banks.idleR6, banks.idleR7, banks.vrc6, banks.vrc7, banks.idleR8, banks.vrc7Line, banks.n163, banks.fme7, banks.mmc3PrgMode0);
}

inline std::vector<std::uint8_t> BuildStartCode(const std::vector<std::uint16_t>& initRoutines, std::uint16_t baseAddress,
                                                const CodeBanks* banks = nullptr, bool inhibitFrameIrq = false) {
    std::vector<std::uint8_t> code;
    // frame_irq="off": LDA #$40; STA $4017 - what a game's reset code does, so the APU's frame IRQ never fires.
    if (inhibitFrameIrq) {
        const std::uint8_t inhibit[] = {0xA9, 0x40, 0x8D, 0x17, 0x40};
        code.insert(code.end(), std::begin(inhibit), std::end(inhibit));
    }
    // The VRC7 driver's routines do not read X, so it is not cleared there (the other mappers' drivers may).
    const bool clearX = banks != nullptr && !banks->vrc7;
    if (banks != nullptr && !initRoutines.empty()) AppendCallBanks(code, *banks);
    const bool clearY = banks != nullptr && (banks->n163 || banks->fme7);
    for (std::uint16_t routine : initRoutines) AppendCall(code, routine, clearX, clearY);
    if (banks != nullptr && !initRoutines.empty()) AppendIdleBanks(code, *banks);
    const std::uint16_t idle = static_cast<std::uint16_t>(baseAddress + code.size());
    code.push_back(0x4C);
    code.push_back(static_cast<std::uint8_t>(idle & 0xFF));
    code.push_back(static_cast<std::uint8_t>(idle >> 8));
    return code;
}

// Where the Machine puts a pending request for a driver that is called with the request number in a register
// (`request_call`): the value at kRequestValueAddress, a non-zero flag at kRequestPendingAddress (read once).
// A driver with a separate routine for effects (`sfx_call`) gets its effect requests at kSfxValueAddress and
// kSfxPendingAddress.
inline constexpr std::uint16_t kRequestValueAddress = 0x4600;
inline constexpr std::uint16_t kRequestPendingAddress = 0x4601;
inline constexpr std::uint16_t kSfxValueAddress = 0x4602;
inline constexpr std::uint16_t kSfxPendingAddress = 0x4603;

// One request: LDA/LDX/LDY value; JSR routine. `reg` is the register the number is passed in ('A', 'X' or 'Y');
// that register is not cleared before the call.
inline void AppendRequest(std::vector<std::uint8_t>& code, std::uint16_t valueAddress, std::uint16_t routine, char reg,
                          bool clearX, bool clearY) {
    code.push_back(reg == 'X' ? 0xAE : reg == 'Y' ? 0xAC : 0xAD);
    code.push_back(static_cast<std::uint8_t>(valueAddress & 0xFF));
    code.push_back(static_cast<std::uint8_t>(valueAddress >> 8));
    AppendCall(code, routine, clearX && reg != 'X', clearY && reg != 'Y');
}

// per-frame code (the NMI handler): JSR for every frame routine, then (with `requestCall`) the request if one is
// pending: LDA flag; BEQ skip; LDA value; JSR requestCall; then (with `sfxCall`) the same for an effect; then RTI.
inline std::vector<std::uint8_t> BuildFrameCode(const std::vector<std::uint16_t>& frameRoutines, const CodeBanks* banks = nullptr,
                                                const std::uint16_t* requestCall = nullptr,
                                                const std::uint16_t* frameCounter = nullptr,
                                                char requestRegister = 'A',
                                                const std::uint16_t* sfxCall = nullptr) {
    std::vector<std::uint8_t> code;
    const bool clearX = banks != nullptr && !banks->vrc7;
    if (banks != nullptr && !frameRoutines.empty()) AppendCallBanks(code, *banks);
    const bool clearY = banks != nullptr && (banks->n163 || banks->fme7);
    for (std::uint16_t routine : frameRoutines) AppendCall(code, routine, clearX, clearY);
    const auto appendPending = [&](std::uint16_t pendingAddress, std::uint16_t valueAddress, std::uint16_t routine) {
        std::vector<std::uint8_t> request;
        // A VRC7 driver's update may have changed a bank by itself, so the banks it is called with are set again.
        if (banks != nullptr && banks->vrc7) AppendCallBanks(request, *banks);
        AppendRequest(request, valueAddress, routine, requestRegister, clearX, clearY);
        code.push_back(0xAD);  // LDA flag
        code.push_back(static_cast<std::uint8_t>(pendingAddress & 0xFF));
        code.push_back(static_cast<std::uint8_t>(pendingAddress >> 8));
        code.push_back(0xF0);  // BEQ over the request
        code.push_back(static_cast<std::uint8_t>(request.size()));
        code.insert(code.end(), request.begin(), request.end());
    };
    if (requestCall != nullptr) appendPending(kRequestPendingAddress, kRequestValueAddress, *requestCall);
    if (sfxCall != nullptr) appendPending(kSfxPendingAddress, kSfxValueAddress, *sfxCall);
    if (frameCounter != nullptr) {
        // INC zp / INC abs: the frame count the game's own code would keep
        if (*frameCounter < 0x100) {
            code.push_back(0xE6);
            code.push_back(static_cast<std::uint8_t>(*frameCounter));
        } else {
            code.push_back(0xEE);
            code.push_back(static_cast<std::uint8_t>(*frameCounter & 0xFF));
            code.push_back(static_cast<std::uint8_t>(*frameCounter >> 8));
        }
    }
    if (banks != nullptr && !frameRoutines.empty()) AppendIdleBanks(code, *banks);
    code.push_back(0x40);
    return code;
}

// A one-pole DC blocker, about 30 Hz at the given sample rate (the console's own output stage also removes DC).
struct DcBlocker {
    float prevInput = 0.0f;
    float prevOutput = 0.0f;
    static float Coefficient(double sampleRateHz) {
        constexpr double kPi = 3.14159265358979323846;
        return static_cast<float>(1.0 - 2.0 * kPi * 30.0 / sampleRateHz);
    }
    void Process(float* samples, std::size_t count, float r) {
        for (std::size_t i = 0; i < count; ++i) {
            const float out = samples[i] - prevInput + r * prevOutput;
            prevInput = samples[i];
            prevOutput = out;
            samples[i] = out;
        }
    }
};
