// license:BSD-3-Clause
// copyright-holders:Tatsuyuki Satoh
// copyright-holders:Frank Palazzolo, Couriersud, Jonathan Gevaryahu
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/vlm5030.cpp  [copyright-holders:Tatsuyuki Satoh]
//   src/devices/sound/vlm5030.h  [copyright-holders:Tatsuyuki Satoh]
//   src/devices/sound/tms5110r.hxx  [copyright-holders:Frank Palazzolo, Couriersud, Jonathan Gevaryahu]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "vlm5030_device_adapter.h"

#include <algorithm>
#include <utility>

namespace {

// VLM5030's own LPC coefficient tables (src/devices/sound/
// tms5110r.hxx's `vlm5030_coeff`, mame0289, BSD-3-Clause - see
// THIRD-PARTY-NOTICES.md). Transcribed verbatim; kbits[i]/ktable[i]
// correspond to K(i+1) (kbits[0]/ktable[0] = K1 ... kbits[9]/ktable[9] =
// K10), matching ParseFrame() indexing. The chirp table and interpolation fields of the shared
// tms5100_coeffs struct are omitted: the VLM5030 algorithm does not use them.
constexpr int kEnergyBits = 5;
constexpr int kPitchBits = 5;
constexpr int kKBits[10] = {6, 5, 4, 4, 3, 3, 3, 3, 3, 3};

constexpr std::uint16_t kEnergyTable[32] = {0,  1,  2,  3,  5,  6,  7,  9,  11, 13, 15, 17, 19, 22, 24, 27,
                                             31, 34, 38, 42, 47, 51, 57, 62, 68, 75, 82, 89, 98, 107, 116, 127};

constexpr std::uint16_t kPitchTable[32] = {0,  21, 22, 23, 24, 25, 26, 27, 28, 29, 31, 33, 35, 37, 39, 41,
                                            43, 45, 49, 53, 57, 61, 65, 69, 73, 77, 85, 93, 101, 109, 117, 125};

// K1 (64 entries - the only table wide enough to need the full 6-bit index).
constexpr std::int32_t kTableK1[64] = {
    390, 403, 414, 425, 434, 443, 450, 457, 463, 469, 474, 478, 482, 485, 488, 491,
    494, 496, 498, 499, 501, 502, 503, 504, 505, 506, 507, 507, 508, 508, 509, 509,
    -390, -376, -360, -344, -325, -305, -284, -261, -237, -211, -183, -155, -125, -95, -64, -32,
    0, 32, 64, 95, 125, 155, 183, 211, 237, 261, 284, 305, 325, 344, 360, 376};

// K2 (32 entries). Entry 16 (index 0x10) is 0 in MAME's own table, with a
// comment noting it may be a manufacturing quirk or special function of
// unknown purpose on real silicon - ported verbatim, not "corrected".
constexpr std::int32_t kTableK2[32] = {0,    50,   100,  149,  196,  241,  284,  325,  362,  396,  426,
                                        452,  473,  490,  502,  510,  0,    -510, -502, -490, -473, -452,
                                        -426, -396, -362, -325, -284, -241, -196, -149, -100, -50};

// K3/K4 (16 entries each, identical tables).
constexpr std::int32_t kTableK3K4[16] = {0,    64,   128,  192,  256,  320,  384,  448,
                                          -512, -448, -384, -320, -256, -192, -128, -64};

// K5-K10 (8 entries each, all identical).
constexpr std::int32_t kTableK5to10[8] = {0, 128, 256, 384, -512, -384, -256, -128};

const std::int32_t* KTable(int kIndex) {
    switch (kIndex) {
        case 0:
            return kTableK1;
        case 1:
            return kTableK2;
        case 2:
        case 3:
            return kTableK3K4;
        default:
            return kTableK5to10;
    }
}

// vlm5030_speed_table (vlm5030.cpp) - IP_SIZE_x = x/FR_SIZE(4).
constexpr int kSpeedTable[8] = {40, 30, 20, 20, 40, 60, 50, 50};

}  // namespace

Vlm5030DeviceAdapter::Vlm5030DeviceAdapter(ReadVoiceRamFn readVoiceRam) : read_voice_ram_(std::move(readVoiceRam)) {
    Reset();
    // device_start() steps past device_reset(): construction lands in kPhaseIdle, not kPhaseReset.
    phase_ = kPhaseIdle;
}

void Vlm5030DeviceAdapter::Reset() {
    phase_ = kPhaseReset;
    address_ = 0;
    vcu_addr_h_ = 0;
    busy_pin_ = 0;

    old_energy_ = 0;
    old_pitch_ = 0;
    new_energy_ = 0;
    new_pitch_ = 0;
    current_energy_ = 0;
    current_pitch_ = 0;
    target_energy_ = 0;
    target_pitch_ = 0;
    for (int i = 0; i < 10; ++i) {
        old_k_[i] = new_k_[i] = current_k_[i] = target_k_[i] = 0;
    }
    interp_count_ = sample_count_ = pitch_count_ = 0;
    for (int i = 0; i < 10; ++i) x_[i] = 0;

    SetupParameter(0x00);
}

void Vlm5030DeviceAdapter::DataW(std::uint8_t data) {
    latch_data_ = data;
}

void Vlm5030DeviceAdapter::RstPin(bool state) {
    const std::uint8_t next = state ? 1 : 0;
    if (rst_pin_ == next) return;
    rst_pin_ = next;
    if (!next) {
        // H -> L: latch parameters.
        SetupParameter(latch_data_);
    } else {
        // L -> H: reset chip, but only while busy (matches vlm5030.cpp's
        // own guard - a rising RST edge while idle is a no-op).
        if (busy_pin_) {
            Reset();
        }
    }
}

void Vlm5030DeviceAdapter::VcuPin(bool state) {
    vcu_pin_ = state ? 1 : 0;
}

void Vlm5030DeviceAdapter::StPin(bool state) {
    const std::uint8_t next = state ? 1 : 0;
    if (st_pin_ == next) return;
    st_pin_ = next;
    if (!next) {
        // H -> L.
        if (vcu_pin_) {
            vcu_addr_h_ = (static_cast<std::uint16_t>(latch_data_) << 8) + 0x01;
        } else {
            // Start speech - resolve the address.
            if (vcu_addr_h_) {
                // Direct access mode.
                address_ = static_cast<std::uint16_t>((vcu_addr_h_ & 0xFF00u) + latch_data_);
                vcu_addr_h_ = 0;
            } else {
                // Indirect access mode: latch_data_ selects a 2-byte
                // pointer table entry (even/odd byte split matches
                // vlm5030.cpp's own bit-swizzled `table` computation).
                const int table = (latch_data_ & 0xFE) + ((latch_data_ & 1) << 8);
                address_ = static_cast<std::uint16_t>((static_cast<std::uint16_t>(ReadByte(static_cast<std::uint16_t>(table)))
                                                        << 8) |
                                                       ReadByte(static_cast<std::uint16_t>(table + 1)));
            }
            sample_count_ = static_cast<std::uint8_t>(frame_size_);
            interp_count_ = kFrameSize;
            phase_ = kPhaseRun;
        }
    } else {
        // L -> H: setup, BSY on.
        phase_ = kPhaseSetup;
        sample_count_ = 1;
        busy_pin_ = 1;
    }
}

std::uint8_t Vlm5030DeviceAdapter::ReadByte(std::uint16_t offset) const {
    if (!read_voice_ram_) return 0;
    return read_voice_ram_(offset & 0x07FFu);
}

int Vlm5030DeviceAdapter::GetBits(int startBit, int bits) const {
    const std::uint16_t offset = static_cast<std::uint16_t>(address_ + (startBit >> 3));
    int data = ReadByte(offset) | (ReadByte(static_cast<std::uint16_t>(offset + 1)) << 8);
    data >>= (startBit & 7);
    data &= (0xFF >> (8 - bits));
    return data;
}

int Vlm5030DeviceAdapter::ParseFrame() {
    old_energy_ = new_energy_;
    old_pitch_ = new_pitch_;
    for (int i = 0; i < 10; ++i) old_k_[i] = new_k_[i];

    const std::uint8_t cmd = ReadByte(address_);
    if (cmd & 0x01) {
        new_energy_ = 0;
        new_pitch_ = 0;
        for (int i = 0; i < 10; ++i) new_k_[i] = 0;
        ++address_;
        if (cmd & 0x02) {
            return 0;  // end of speech
        }
        const int frames = ((cmd >> 2) + 1) * 2;  // silent frame
        return frames * kFrameSize;
    }

    new_pitch_ = static_cast<std::uint8_t>(kPitchTable[GetBits(1, kPitchBits)]);
    if (new_pitch_ > 0) {
        new_pitch_ = static_cast<std::uint8_t>(new_pitch_ + pitch_offset_);
    }
    new_energy_ = kEnergyTable[GetBits(6, kEnergyBits)];

    new_k_[9] = KTable(9)[GetBits(11, kKBits[9])];
    new_k_[8] = KTable(8)[GetBits(14, kKBits[8])];
    new_k_[7] = KTable(7)[GetBits(17, kKBits[7])];
    new_k_[6] = KTable(6)[GetBits(20, kKBits[6])];
    new_k_[5] = KTable(5)[GetBits(23, kKBits[5])];
    new_k_[4] = KTable(4)[GetBits(26, kKBits[4])];
    new_k_[3] = KTable(3)[GetBits(29, kKBits[3])];
    new_k_[2] = KTable(2)[GetBits(33, kKBits[2])];
    new_k_[1] = KTable(1)[GetBits(37, kKBits[1])];
    new_k_[0] = KTable(0)[GetBits(42, kKBits[0])];

    address_ = static_cast<std::uint16_t>(address_ + 6);
    return kFrameSize;
}

void Vlm5030DeviceAdapter::SetupParameter(std::uint8_t param) {
    parameter_ = param;

    if (param & 2) {
        interp_step_ = 4;  // 9600bps: no interpolation
    } else if (param & 1) {
        interp_step_ = 2;  // 4800bps
    } else {
        interp_step_ = 1;  // 2400bps
    }

    frame_size_ = kSpeedTable[(param >> 3) & 7];

    if (param & 0x80) {
        pitch_offset_ = -8;
    } else if (param & 0x40) {
        pitch_offset_ = 8;
    } else {
        pitch_offset_ = 0;
    }
}

void Vlm5030DeviceAdapter::TickNoiseRng() {
    // xorshift32 in place of MAME's machine().rand() (deterministic; see the header).
    noise_rng_ ^= noise_rng_ << 13;
    noise_rng_ ^= noise_rng_ >> 17;
    noise_rng_ ^= noise_rng_ << 5;
}

std::size_t Vlm5030DeviceAdapter::Render(std::uint64_t inputClocks, std::vector<float>& samples) {
    partial_input_clocks_ += inputClocks;
    const std::uint64_t sampleCount = partial_input_clocks_ / kInputClocksPerSample;
    partial_input_clocks_ -= sampleCount * kInputClocksPerSample;

    std::uint64_t produced = 0;

    if (phase_ == kPhaseRun || phase_ == kPhaseStop) {
        for (; produced < sampleCount; ++produced) {
            if (sample_count_ == 0) {
                if (phase_ == kPhaseStop) {
                    phase_ = kPhaseEnd;
                    sample_count_ = 1;
                    break;  // matches vlm5030.cpp's `goto phase_stop`
                }
                sample_count_ = static_cast<std::uint8_t>(frame_size_);

                if (interp_count_ == 0) {
                    interp_count_ = static_cast<std::uint8_t>(ParseFrame());
                    if (interp_count_ == 0) {
                        interp_count_ = kFrameSize;
                        sample_count_ = static_cast<std::uint8_t>(frame_size_);
                        phase_ = kPhaseStop;
                    }

                    current_energy_ = old_energy_;
                    current_pitch_ = old_pitch_;
                    for (int i = 0; i < 10; ++i) current_k_[i] = old_k_[i];

                    if (current_energy_ == 0) {
                        target_energy_ = 0;
                        target_pitch_ = static_cast<std::uint8_t>(current_pitch_);
                        for (int i = 0; i < 10; ++i) target_k_[i] = current_k_[i];
                    } else {
                        target_energy_ = new_energy_;
                        target_pitch_ = new_pitch_;
                        for (int i = 0; i < 10; ++i) target_k_[i] = new_k_[i];
                    }
                }

                interp_count_ = static_cast<std::uint8_t>(interp_count_ - interp_step_);
                const int interpEffect = kFrameSize - (interp_count_ % kFrameSize);
                // Signed arithmetic on purpose: MAME holds m_old_energy/m_target_energy as uint16_t, promoted to
                // int, so a falling energy interpolates downward; unsigned members would wrap to ~1e9.
                current_energy_ = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(old_energy_) +
                    (static_cast<std::int32_t>(target_energy_) - static_cast<std::int32_t>(old_energy_)) * interpEffect /
                        kFrameSize);
                if (old_pitch_ > 1) {
                    current_pitch_ = old_pitch_ + (target_pitch_ - old_pitch_) * interpEffect / kFrameSize;
                }
                for (int i = 0; i < 10; ++i) {
                    current_k_[i] = old_k_[i] + (target_k_[i] - old_k_[i]) * interpEffect / kFrameSize;
                }
            }

            std::int32_t currentVal;
            if (old_energy_ == 0) {
                currentVal = 0;  // silent
            } else if (old_pitch_ <= 1) {
                TickNoiseRng();
                const bool bit = (noise_rng_ & 1) != 0;
                currentVal = bit ? static_cast<std::int32_t>(current_energy_) : -static_cast<std::int32_t>(current_energy_);
            } else {
                currentVal = (pitch_count_ == 0) ? static_cast<std::int32_t>(current_energy_) : 0;  // voiced
            }

            // 10th-order lattice (PARCOR) filter - ported verbatim from
            // vlm5030.cpp's own u[]/x[] recurrence.
            std::int32_t u[11];
            u[10] = currentVal;
            for (int i = 9; i >= 0; --i) {
                u[i] = u[i + 1] - ((-current_k_[i] * x_[i]) / 512);
            }
            for (int i = 9; i >= 1; --i) {
                x_[i] = x_[i - 1] + ((-current_k_[i - 1] * u[i - 1]) / 512);
            }
            x_[0] = u[0];

            const std::int32_t clamped = std::clamp(u[0], -512, 511);
            samples.push_back(static_cast<float>(clamped) / 512.0f);

            --sample_count_;
            ++pitch_count_;
            if (pitch_count_ >= current_pitch_) {
                pitch_count_ = 0;
            }
        }
    }

    // Phase transitions that don't produce samples (SETUP/END) operate on the full requested sampleCount,
    // as in MAME's phase_stop switch (reached by falling through or via the early break once STOP->END
    // triggers mid-loop); this reproduces MAME's quirk of using the call's total sample count.
    switch (phase_) {
        case kPhaseSetup:
            if (sample_count_ <= sampleCount) {
                sample_count_ = 0;
                phase_ = kPhaseWait;
            } else {
                sample_count_ = static_cast<std::uint8_t>(sample_count_ - sampleCount);
            }
            break;
        case kPhaseEnd:
            if (sample_count_ <= sampleCount) {
                sample_count_ = 0;
                busy_pin_ = 0;
                phase_ = kPhaseIdle;
            } else {
                sample_count_ = static_cast<std::uint8_t>(sample_count_ - sampleCount);
            }
            break;
        default:
            break;
    }

    // Any sample not pushed above still owes the caller `sampleCount` entries (the channel-buffer contract
    // every adapter honors); silence fills the remainder.
    samples.resize(samples.size() + static_cast<std::size_t>(sampleCount - produced), 0.0f);

    return static_cast<std::size_t>(sampleCount);
}
