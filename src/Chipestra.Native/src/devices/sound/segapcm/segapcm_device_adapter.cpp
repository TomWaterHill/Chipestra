// license:BSD-3-Clause
// copyright-holders:Hiromitsu Shioya, Olivier Galibert
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/segapcm.cpp
//   src/devices/sound/segapcm.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "devices/sound/segapcm/segapcm_device_adapter.h"

#include <algorithm>
#include <utility>

SegaPcmDeviceAdapter::BankDecode SegaPcmDeviceAdapter::DecodeBank(int bank) {
    // MAME sega_315_5218_device::set_bank(int bank):
    //   m_bankshift = bank & 0xf;
    //   m_bankmask  = 0x70 | ((bank >> 16) & 0xfc);
    return BankDecode{static_cast<std::uint32_t>(bank & 0xf),
                      0x70u | ((static_cast<std::uint32_t>(bank) >> 16) & 0xfcu)};
}

SegaPcmDeviceAdapter::SegaPcmDeviceAdapter(std::vector<std::uint8_t> sampleRom, int bank)
    : sample_rom_(std::move(sampleRom)), bank_(DecodeBank(bank)) {
    Reset();
}

void SegaPcmDeviceAdapter::Reset() {
    for (Voice& voice : voices_) {
        voice = Voice{};
    }
    std::fill(std::begin(register_ram_), std::end(register_ram_), std::uint8_t{0xFF});
    partial_master_clocks_ = 0;
    generated_samples_ = 0;
}

void SegaPcmDeviceAdapter::WriteRegister(std::uint8_t offset, std::uint8_t value) {
    ++register_write_count_;
    const std::uint32_t voiceIndex = (offset >> 3) & 0x0Fu;
    const std::uint8_t reg = offset & 0x87u;
    Voice& voice = voices_[voiceIndex];
    switch (reg) {
        case 0x02:  // volume left
            voice.lvol = value;
            break;
        case 0x03:  // volume right
            voice.rvol = value;
            break;
        case 0x04:  // loop address bits 0-7
            voice.loop = static_cast<std::uint16_t>((voice.loop & 0xFF00u) | value);
            break;
        case 0x05:  // loop address bits 8-15
            voice.loop = static_cast<std::uint16_t>((voice.loop & 0x00FFu) | (static_cast<std::uint16_t>(value) << 8));
            break;
        case 0x06:  // end address (high byte compared against addr >> 16)
            voice.end = value;
            break;
        case 0x07:  // address delta (freq)
            voice.freq = value;
            break;
        case 0x84:  // current address bits 8-15 (MAME voice_addr_w shift 8)
            voice.addr = (voice.addr & ~(std::uint32_t{0xFF} << 8)) | (static_cast<std::uint32_t>(value) << 8);
            break;
        case 0x85:  // current address bits 16-23 (shift 16)
            voice.addr = (voice.addr & ~(std::uint32_t{0xFF} << 16)) | (static_cast<std::uint32_t>(value) << 16);
            break;
        case 0x86:  // control: bit0 disable, bit1 loop disable, bits 4-6 bank
            voice.ctrl = value;
            break;
        default:
            // Everything outside the voice-register slots is plain scratchpad
            // RAM (MAME maps 0x00-0xFF as .ram() under the handlers).
            register_ram_[offset] = value;
            break;
    }
}

std::uint8_t SegaPcmDeviceAdapter::ReadRegister(std::uint8_t offset) const {
    ++register_read_count_;
    const std::uint32_t voiceIndex = (offset >> 3) & 0x0Fu;
    const std::uint8_t reg = offset & 0x87u;
    const Voice& voice = voices_[voiceIndex];
    switch (reg) {
        case 0x02:
            return voice.lvol;
        case 0x03:
            return voice.rvol;
        case 0x04:
            return static_cast<std::uint8_t>(voice.loop & 0xFFu);
        case 0x05:
            return static_cast<std::uint8_t>((voice.loop >> 8) & 0xFFu);
        case 0x06:
            return voice.end;
        case 0x07:
            return voice.freq;
        case 0x84:
            return static_cast<std::uint8_t>((voice.addr >> 8) & 0xFFu);
        case 0x85:
            return static_cast<std::uint8_t>((voice.addr >> 16) & 0xFFu);
        case 0x86:
            return voice.ctrl;
        default:
            return register_ram_[offset];
    }
}

std::uint8_t SegaPcmDeviceAdapter::Read8(std::uint64_t offset) const {
    return ReadRegister(static_cast<std::uint8_t>(offset & 0xFFu));
}

void SegaPcmDeviceAdapter::Write8(std::uint64_t offset, std::uint8_t value) {
    WriteRegister(static_cast<std::uint8_t>(offset & 0xFFu), value);
}

std::uint8_t SegaPcmDeviceAdapter::ReadSample(std::uint32_t byteAddr) const {
    if (sample_rom_.empty()) {
        return 0x80;  // unsigned-PCM midpoint == silence
    }
    return sample_rom_[byteAddr % sample_rom_.size()];
}

void SegaPcmDeviceAdapter::TickVoice(Voice& voice, std::int32_t& lout, std::int32_t& rout) const {
    // Ported from MAME segapcm_base_device::voice_t::tick(). The real address
    // counter is 24-bit; MAME leaves stale high bits in `addr` until the
    // first advance/disable masks them - we mask up front instead (see
    // THIRD-PARTY-NOTICES.md).
    voice.addr &= 0xFFFFFFu;

    lout = 0;
    rout = 0;

    if ((voice.ctrl & 0x01u) == 0) {  // BIT(~ctrl, 0): voice enabled
        if ((voice.addr >> 16) == voice.end) {
            if ((voice.ctrl & 0x02u) != 0) {  // BIT(ctrl, 1): loop disabled -> stop
                voice.ctrl |= 0x01u;
                return;
            }
            voice.addr = static_cast<std::uint32_t>(voice.loop) << 8;
        }

        const std::uint32_t bank = (voice.ctrl & bank_.mask) << bank_.shift;
        const int sample = static_cast<int>(ReadSample(bank + (voice.addr >> 8))) - 0x80;
        lout = sample * (voice.lvol & 0x7f);
        rout = sample * (voice.rvol & 0x7f);
        voice.addr = (voice.addr + voice.freq) & 0xFFFFFFu;
    } else {
        voice.addr &= 0xFFFF00u;
    }
}

std::size_t SegaPcmDeviceAdapter::Render(std::uint64_t masterClocks,
                                        std::vector<std::vector<float>>& voiceSamples) {
    const std::uint64_t available = partial_master_clocks_ + masterClocks;
    const std::size_t sampleCount = static_cast<std::size_t>(available / kOutputClockDivider);
    partial_master_clocks_ = available % kOutputClockDivider;

    for (std::size_t s = 0; s < sampleCount; ++s) {
        bool anyStereo = false;
        for (std::uint32_t v = 0; v < kVoiceCount; ++v) {
            std::int32_t lout = 0;
            std::int32_t rout = 0;
            TickVoice(voices_[v], lout, rout);
            // lout = waveform * lvol, rout = waveform * rvol (see TickVoice).
            // Divide the two back out to recover the raw 8-bit waveform
            // (centred, so range ~[-128, 127]) and normalize to [-1, 1]; the
            // Machine re-applies the L/R volumes as pan + routing gain. A voice
            // whose volumes are both 0 is silent regardless.
            const std::int32_t volSum =
                (voices_[v].lvol & 0x7f) + (voices_[v].rvol & 0x7f);
            const float mono =
                volSum > 0 ? (static_cast<float>(lout + rout) / static_cast<float>(volSum)) * (1.0f / 128.0f) : 0.0f;
            voiceSamples[v].push_back(mono);
            anyStereo = anyStereo || (lout != rout);
        }
        if (anyStereo) {
            ++stereo_samples_;
        }
        ++generated_samples_;
    }
    return sampleCount;
}

std::uint64_t SegaPcmDeviceAdapter::GeneratedSampleCount() const { return generated_samples_; }

std::uint8_t SegaPcmDeviceAdapter::VoiceVolumeLeft(std::uint32_t voice) const {
    return voice < kVoiceCount ? static_cast<std::uint8_t>(voices_[voice].lvol & 0x7f) : 0;
}

std::uint8_t SegaPcmDeviceAdapter::VoiceVolumeRight(std::uint32_t voice) const {
    return voice < kVoiceCount ? static_cast<std::uint8_t>(voices_[voice].rvol & 0x7f) : 0;
}
