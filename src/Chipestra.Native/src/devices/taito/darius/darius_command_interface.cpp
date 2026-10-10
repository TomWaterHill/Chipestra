// license:BSD-3-Clause
// copyright-holders:Philip Bennett
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/mame/shared/taitosnd.cpp
//   src/mame/shared/taitosnd.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "devices/taito/darius/darius_command_interface.h"

#include <utility>

DariusCommandInterface::DariusCommandInterface(NmiHandler nmiHandler, ResetHandler resetHandler)
    : nmi_handler_(std::move(nmiHandler)), reset_handler_(std::move(resetHandler)) {}

void DariusCommandInterface::Reset() {
    for (auto& b : slave_data_) b = 0;
    for (auto& b : master_data_) b = 0;
    main_mode_ = 0;
    sub_mode_ = 0;
    status_ = 0;
    nmi_enabled_ = false;
    // Like the real device_reset(), only the internal state is cleared; the NMI
    // line is not driven (UpdateNmi() is not called).
    nmi_asserted_ = false;
}

void DariusCommandInterface::UpdateNmi() {
    const bool pending = (status_ & (kPort01Full | kPort23Full)) != 0;
    const bool asserted = pending && nmi_enabled_;
    if (asserted != nmi_asserted_) {
        nmi_asserted_ = asserted;
        if (nmi_handler_) {
            nmi_handler_(asserted);
        }
    }
}

void DariusCommandInterface::MasterPortW(std::uint8_t data) { main_mode_ = data & 0x0F; }

void DariusCommandInterface::MasterCommW(std::uint8_t data) {
    data &= 0x0F;
    switch (main_mode_) {
        case 0x00:
            slave_data_[main_mode_++] = data;
            break;
        case 0x01:
            slave_data_[main_mode_++] = data;
            status_ |= kPort01Full;
            UpdateNmi();
            break;
        case 0x02:
            slave_data_[main_mode_++] = data;
            break;
        case 0x03:
            slave_data_[main_mode_++] = data;
            status_ |= kPort23Full;
            UpdateNmi();
            break;
        case 0x04:
            // Hi->lo transition on the port-status "mode" resets the sound
            // CPU - matches taitosnd.cpp's own comment verbatim.
            if (reset_handler_) {
                reset_handler_(data != 0);
            }
            break;
        default:
            break;
    }
}

std::uint8_t DariusCommandInterface::MasterCommR() {
    std::uint8_t res = 0;
    switch (main_mode_) {
        case 0x00:
            res = master_data_[main_mode_++];
            break;
        case 0x01:
            res = master_data_[main_mode_++];
            status_ &= static_cast<std::uint8_t>(~kPort01FullMaster);
            break;
        case 0x02:
            res = master_data_[main_mode_++];
            break;
        case 0x03:
            res = master_data_[main_mode_++];
            status_ &= static_cast<std::uint8_t>(~kPort23FullMaster);
            break;
        case 0x04:
            res = status_;
            break;
        default:
            break;
    }
    return res;
}

void DariusCommandInterface::SlavePortW(std::uint8_t data) { sub_mode_ = data & 0x0F; }

std::uint8_t DariusCommandInterface::SlaveCommR() {
    std::uint8_t res = 0;
    switch (sub_mode_) {
        case 0x00:
            res = slave_data_[sub_mode_++];
            break;
        case 0x01:
            res = slave_data_[sub_mode_++];
            status_ &= static_cast<std::uint8_t>(~kPort01Full);
            UpdateNmi();
            break;
        case 0x02:
            res = slave_data_[sub_mode_++];
            break;
        case 0x03:
            res = slave_data_[sub_mode_++];
            status_ &= static_cast<std::uint8_t>(~kPort23Full);
            UpdateNmi();
            break;
        case 0x04:
            res = status_;
            break;
        default:
            break;
    }
    return res;
}

void DariusCommandInterface::SlaveCommW(std::uint8_t data) {
    data &= 0x0F;
    switch (sub_mode_) {
        case 0x00:
            master_data_[sub_mode_++] = data;
            break;
        case 0x01:
            master_data_[sub_mode_++] = data;
            status_ |= kPort01FullMaster;
            break;
        case 0x02:
            master_data_[sub_mode_++] = data;
            break;
        case 0x03:
            master_data_[sub_mode_++] = data;
            status_ |= kPort23FullMaster;
            break;
        case 0x04:
            break;
        case 0x05:  // NMI disable
            nmi_enabled_ = false;
            UpdateNmi();
            break;
        case 0x06:  // NMI enable
            nmi_enabled_ = true;
            UpdateNmi();
            break;
        default:
            break;
    }
}

void DariusCommandInterface::Send(std::uint8_t value) {
    // LOW nibble first, then HIGH nibble (the order the audiocpu NMI handler expects).
    MasterPortW(0x00);
    MasterCommW(static_cast<std::uint8_t>(value & 0x0F));
    MasterCommW(static_cast<std::uint8_t>((value >> 4) & 0x0F));
}
