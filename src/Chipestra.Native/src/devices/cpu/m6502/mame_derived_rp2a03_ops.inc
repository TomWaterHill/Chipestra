// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/m6502/orp2a03.lst  [copyright-holders:Olivier Galibert]
//   src/devices/cpu/m6502/om6502.lst  [copyright-holders:Olivier Galibert]
//   src/devices/cpu/m6502/drp2a03.lst  [copyright-holders:Olivier Galibert]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill
//
// The instruction bodies below are the sequences of the .lst files, in order: every Read/Write/
// ReadPc/ReadArg costs one cycle, and Prefetch() is the opcode fetch that ends an instruction.
// The RP2A03 dispatch table takes the "_nd" (decimal mode disabled) variants from orp2a03.lst
// and every other instruction from om6502.lst.
// (Included once by mame_derived_rp2a03_adapter.cpp.)

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)  // mod-2^N register arithmetic, as in the original
#endif

void MameDerivedRp2a03Adapter::Op_brk_imp() {
    // The 6502 bug when a nmi occurs in a brk is reproduced (case !irq_taken && nmi_pending)
    if (irqTaken_) {
        ReadPc();
    } else {
        ReadPc();
        pc_++;
    }
    Write(sp_, pc_ >> 8);
    DecSp();
    Write(sp_, pc_);
    DecSp();
    Write(sp_, irqTaken_ ? p_ & ~kFlagB : p_);
    DecSp();
    if (nmiPending_) {
        ++nmiServiceCount_;
        pc_ = ReadArg(0xfffa);
        pc_ = SetH(pc_, ReadArg(0xfffb));
        nmiPending_ = false;
    } else {
        if (irqTaken_)
            ++irqServiceCount_;
        pc_ = ReadArg(0xfffe);
        pc_ = SetH(pc_, ReadArg(0xffff));
    }
    irqTaken_ = false;
    p_ |= kFlagI; // Do *not* move after the prefetch
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ora_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_kil_non() {
    ReadPc();
    pc_++;
    Read(0xffff);
    Read(0xfffe);
    Read(0xfffe);
    jammed_ = true;  // the original reads 0xffff forever here
}

void MameDerivedRp2a03Adapter::Op_slo_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoAsl(tmp2_);
    Write(tmp_, tmp2_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_nop_zpg() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ora_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_asl_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoAsl(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_slo_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoAsl(tmp2_);
    Write(tmp_, tmp2_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_php_imp() {
    ReadPc();
    Write(sp_, p_);
    DecSp();
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ora_imm() {
    tmp_ = ReadPc();
    pc_++;
    a_ |= tmp_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_asl_acc() {
    ReadPc();
    a_ = DoAsl(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_anc_imm() {
    tmp_ = ReadPc();
    pc_++;
    a_ &= tmp_;
    SetNz(a_);
    if (a_ & 0x80)
        p_ |= kFlagC;
    else
        p_ &= ~kFlagC;
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_nop_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(tmp_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ora_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_asl_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoAsl(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_slo_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoAsl(tmp2_);
    Write(tmp_, tmp2_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_bpl_rel() {
    tmp_ = ReadPc();
    pc_++;
    if (!(p_ & kFlagN)) {
        ReadPc();
        if (PageChanging(pc_, std::int8_t(tmp_))) {
            ReadArg(SetL(pc_, pc_+std::int8_t(tmp_)));
        }
        pc_ += std::int8_t(tmp_);
    }
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ora_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp2_ = Read(tmp_+y_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_slo_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoAsl(tmp2_);
    Write(tmp_, tmp2_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_nop_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    Read(std::uint8_t(tmp_+x_));
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ora_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp2_ = Read(std::uint8_t(tmp_+x_));
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_asl_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoAsl(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_slo_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoAsl(tmp2_);
    Write(tmp_, tmp2_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_clc_imp() {
    ReadPc();
    p_ &= ~kFlagC;
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ora_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_nop_imp() {
    ReadPc();
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_slo_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoAsl(tmp2_);
    Write(tmp_, tmp2_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_nop_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, x_)) {
        Read(SetL(tmp_, tmp_+x_));
    }
    Read(tmp_ + x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ora_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, x_)) {
        Read(SetL(tmp_, tmp_+x_));
    }
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_asl_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoAsl(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_slo_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoAsl(tmp2_);
    Write(tmp_, tmp2_);
    a_ |= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_jsr_adr() {
    tmp_ = ReadPc();
    pc_++;
    Read(sp_);
    Write(sp_, pc_>>8);
    DecSp();
    Write(sp_, pc_);
    DecSp();
    tmp_ = SetH(tmp_, ReadPc());
    pc_ = tmp_;
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_and_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rla_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRol(tmp2_);
    Write(tmp_, tmp2_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_bit_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    DoBit(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_and_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rol_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRol(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rla_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRol(tmp2_);
    Write(tmp_, tmp2_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_plp_imp() {
    ReadPc();
    Read(sp_);
    IncSp();
    tmp_ = Read(sp_) | (kFlagB|kFlagE);
    Prefetch();
    p_ = tmp_; // Do *not* move it before the prefetch
}

void MameDerivedRp2a03Adapter::Op_and_imm() {
    tmp_ = ReadPc();
    pc_++;
    a_ &= tmp_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rol_acc() {
    ReadPc();
    a_ = DoRol(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_bit_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    DoBit(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_and_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rol_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRol(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rla_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRol(tmp2_);
    Write(tmp_, tmp2_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_bmi_rel() {
    tmp_ = ReadPc();
    pc_++;
    if (p_ & kFlagN) {
        ReadPc();
        if (PageChanging(pc_, std::int8_t(tmp_))) {
            ReadArg(SetL(pc_, pc_+std::int8_t(tmp_)));
        }
        pc_ += std::int8_t(tmp_);
    }
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_and_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp2_ = Read(tmp_+y_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rla_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRol(tmp2_);
    Write(tmp_, tmp2_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_and_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp2_ = Read(std::uint8_t(tmp_+x_));
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rol_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRol(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rla_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRol(tmp2_);
    Write(tmp_, tmp2_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sec_imp() {
    ReadPc();
    p_ |= kFlagC;
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_and_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rla_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRol(tmp2_);
    Write(tmp_, tmp2_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_and_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, x_)) {
        Read(SetL(tmp_, tmp_+x_));
    }
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rol_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRol(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rla_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRol(tmp2_);
    Write(tmp_, tmp2_);
    a_ &= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rti_imp() {
    ReadPc();
    Read(sp_);
    IncSp();
    p_ = Read(sp_) | (kFlagB|kFlagE);
    IncSp();
    pc_ = Read(sp_);
    IncSp();
    pc_ = SetH(pc_, Read(sp_));
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_eor_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sre_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoLsr(tmp2_);
    Write(tmp_, tmp2_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_eor_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lsr_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoLsr(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sre_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoLsr(tmp2_);
    Write(tmp_, tmp2_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_pha_imp() {
    ReadPc();
    Write(sp_, a_);
    DecSp();
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_eor_imm() {
    tmp_ = ReadPc();
    pc_++;
    a_ ^= tmp_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lsr_acc() {
    ReadPc();
    a_ = DoLsr(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_asr_imm() {
    tmp_ = ReadPc();
    pc_++;
    a_ = DoLsr(a_ & tmp_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_jmp_adr() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_ = tmp_;
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_eor_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lsr_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoLsr(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sre_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoLsr(tmp2_);
    Write(tmp_, tmp2_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_bvc_rel() {
    tmp_ = ReadPc();
    pc_++;
    if (!(p_ & kFlagV)) {
        ReadPc();
        if (PageChanging(pc_, std::int8_t(tmp_))) {
            ReadArg(SetL(pc_, pc_+std::int8_t(tmp_)));
        }
        pc_ += std::int8_t(tmp_);
    }
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_eor_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp2_ = Read(tmp_+y_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sre_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoLsr(tmp2_);
    Write(tmp_, tmp2_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_eor_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp2_ = Read(std::uint8_t(tmp_+x_));
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lsr_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoLsr(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sre_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoLsr(tmp2_);
    Write(tmp_, tmp2_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cli_imp() {
    ReadPc();
    Prefetch();
    p_ &= ~kFlagI; // Do *not* move it before the prefetch
}

void MameDerivedRp2a03Adapter::Op_eor_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sre_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoLsr(tmp2_);
    Write(tmp_, tmp2_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_eor_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, x_)) {
        Read(SetL(tmp_, tmp_+x_));
    }
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lsr_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoLsr(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sre_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoLsr(tmp2_);
    Write(tmp_, tmp2_);
    a_ ^= tmp2_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rts_imp() {
    ReadPc();
    Read(sp_);
    IncSp();
    pc_ = Read(sp_);
    IncSp();
    pc_ = SetH(pc_, Read(sp_));
    ReadPc();
    pc_++;
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_adc_nd_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rra_nd_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRor(tmp2_);
    Write(tmp_, tmp2_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_adc_nd_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ror_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRor(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rra_nd_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRor(tmp2_);
    Write(tmp_, tmp2_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_pla_imp() {
    ReadPc();
    Read(sp_);
    IncSp();
    a_ = Read(sp_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_adc_nd_imm() {
    tmp_ = ReadPc();
    pc_++;
    DoAdcNd(tmp_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ror_acc() {
    ReadPc();
    a_ = DoRor(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_arr_nd_imm() {
    tmp_ = ReadPc();
    a_ &= tmp_;
    pc_++;
    DoArrNd();
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_jmp_ind() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    pc_ = Read(tmp_);
    pc_ = SetH(pc_, Read(SetL(tmp_, tmp_+1)));
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_adc_nd_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ror_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRor(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rra_nd_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRor(tmp2_);
    Write(tmp_, tmp2_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_bvs_rel() {
    tmp_ = ReadPc();
    pc_++;
    if (p_ & kFlagV) {
        ReadPc();
        if (PageChanging(pc_, std::int8_t(tmp_))) {
            ReadArg(SetL(pc_, pc_+std::int8_t(tmp_)));
        }
        pc_ += std::int8_t(tmp_);
    }
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_adc_nd_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp2_ = Read(tmp_+y_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rra_nd_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRor(tmp2_);
    Write(tmp_, tmp2_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_adc_nd_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp2_ = Read(std::uint8_t(tmp_+x_));
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ror_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRor(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rra_nd_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRor(tmp2_);
    Write(tmp_, tmp2_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sei_imp() {
    ReadPc();
    Prefetch();
    p_ |= kFlagI; // Do *not* move it before the prefetch
}

void MameDerivedRp2a03Adapter::Op_adc_nd_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rra_nd_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRor(tmp2_);
    Write(tmp_, tmp2_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_adc_nd_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, x_)) {
        Read(SetL(tmp_, tmp_+x_));
    }
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ror_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRor(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_rra_nd_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_ = DoRor(tmp2_);
    Write(tmp_, tmp2_);
    DoAdcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_nop_imm() {
    ReadPc();
    pc_++;
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sta_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    Write(tmp_, a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sax_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = a_ & x_;
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sty_zpg() {
    tmp_ = ReadPc();
    pc_++;
    Write(tmp_, y_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sta_zpg() {
    tmp_ = ReadPc();
    pc_++;
    Write(tmp_, a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_stx_zpg() {
    tmp_ = ReadPc();
    pc_++;
    Write(tmp_, x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sax_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = a_ & x_;
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dey_imp() {
    ReadPc();
    y_--;
    SetNz(y_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_txa_imp() {
    ReadPc();
    a_ = x_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ane_imm() {
    tmp_ = ReadPc();
    pc_++;
    a_ &= tmp_ & x_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sty_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Write(tmp_, y_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sta_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Write(tmp_, a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_stx_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Write(tmp_, x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sax_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = a_ & x_;
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_bcc_rel() {
    tmp_ = ReadPc();
    pc_++;
    if (!(p_ & kFlagC)) {
        ReadPc();
        if (PageChanging(pc_, std::int8_t(tmp_))) {
            ReadArg(SetL(pc_, pc_+std::int8_t(tmp_)));
        }
        pc_ += std::int8_t(tmp_);
    }
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sta_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    Read(SetL(tmp_, tmp_+y_));
    Write(tmp_+y_, a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sha_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    Read(SetL(tmp_, tmp_+y_));
    tmp2_ = a_ & x_ & ((tmp_ >> 8)+1);
    if (PageChanging(tmp_, y_))
        tmp_ = SetH(tmp_+y_, tmp2_);
    else
        tmp_ += y_;
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sty_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    Write(std::uint8_t(tmp_+x_), y_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sta_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    Write(std::uint8_t(tmp_+x_), a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_stx_zpy() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    Write(std::uint8_t(tmp_+y_), x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sax_zpy() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+y_);
    tmp2_ = a_ & x_;
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_tya_imp() {
    ReadPc();
    a_ = y_;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sta_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+y_));
    Write(tmp_+y_, a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_txs_imp() {
    ReadPc();
    sp_ = SetL(sp_, x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_shs_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+y_));
    sp_ = SetL(sp_, a_ & x_);
    tmp2_ = a_ & x_ & ((tmp_ >> 8)+1);
    if (PageChanging(tmp_, y_))
        tmp_ = SetH(tmp_+y_, tmp2_);
    else
        tmp_ += y_;
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_shy_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp2_ = y_ & ((tmp_ >> 8)+1);
    if (PageChanging(tmp_, x_))
        tmp_ = SetH(tmp_+x_, tmp2_);
    else
        tmp_ += x_;
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sta_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    Write(tmp_+x_, a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_shx_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+y_));
    tmp2_ = x_ & ((tmp_ >> 8)+1);
    if (PageChanging(tmp_, y_))
        tmp_ = SetH(tmp_+y_, tmp2_);
    else
        tmp_ += y_;
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sha_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+y_));
    tmp2_ = a_ & x_ & ((tmp_ >> 8)+1);
    if (PageChanging(tmp_, y_))
        tmp_ = SetH(tmp_+y_, tmp2_);
    else
        tmp_ += y_;
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ldy_imm() {
    y_ = ReadPc();
    pc_++;
    SetNz(y_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lda_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    a_ = Read(tmp_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ldx_imm() {
    x_ = ReadPc();
    pc_++;
    SetNz(x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lax_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    a_ = x_ = Read(tmp_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ldy_zpg() {
    tmp_ = ReadPc();
    pc_++;
    y_ = Read(tmp_);
    SetNz(y_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lda_zpg() {
    tmp_ = ReadPc();
    pc_++;
    a_ = Read(tmp_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ldx_zpg() {
    tmp_ = ReadPc();
    pc_++;
    x_ = Read(tmp_);
    SetNz(x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lax_zpg() {
    tmp_ = ReadPc();
    pc_++;
    a_ = x_ = Read(tmp_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_tay_imp() {
    ReadPc();
    y_ = a_;
    SetNz(y_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lda_imm() {
    a_ = ReadPc();
    pc_++;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_tax_imp() {
    ReadPc();
    x_ = a_;
    SetNz(x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lxa_imm() {
    a_ = x_ = ReadPc();
    pc_++;
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ldy_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    y_ = Read(tmp_);
    SetNz(y_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lda_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    a_ = Read(tmp_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ldx_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    x_ = Read(tmp_);
    SetNz(x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lax_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    a_ = x_ = Read(tmp_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_bcs_rel() {
    tmp_ = ReadPc();
    pc_++;
    if (p_ & kFlagC) {
        ReadPc();
        if (PageChanging(pc_, std::int8_t(tmp_))) {
            ReadArg(SetL(pc_, pc_+std::int8_t(tmp_)));
        }
        pc_ += std::int8_t(tmp_);
    }
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lda_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    a_ = Read(tmp_+y_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lax_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    a_ = x_ = Read(tmp_+y_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ldy_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    y_ = Read(std::uint8_t(tmp_+x_));
    SetNz(y_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lda_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    a_ = Read(std::uint8_t(tmp_+x_));
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ldx_zpy() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    x_ = Read(std::uint8_t(tmp_+y_));
    SetNz(x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lax_zpy() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+y_);
    a_ = x_ = Read(tmp_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_clv_imp() {
    ReadPc();
    p_ &= ~kFlagV;
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lda_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    a_ = Read(tmp_ + y_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_tsx_imp() {
    ReadPc();
    x_ = sp_;
    SetNz(x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_las_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp2_ = Read(tmp_+y_);
    a_ = tmp2_ | 0x51;
    x_ = 0xff;
    SetNz(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ldy_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, x_)) {
        Read(SetL(tmp_, tmp_+x_));
    }
    tmp_ += x_;
    y_ = Read(tmp_);
    SetNz(y_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lda_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, x_)) {
        Read(SetL(tmp_, tmp_+x_));
    }
    a_ = Read(tmp_ + x_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_ldx_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    x_ = Read(tmp_ + y_);
    SetNz(x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_lax_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    a_ = x_ = Read(tmp_+y_);
    SetNz(a_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cpy_imm() {
    tmp_ = ReadPc();
    pc_++;
    DoCmp(y_, tmp_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cmp_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dcp_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_--;
    Write(tmp_, tmp2_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cpy_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    DoCmp(y_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cmp_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dec_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_--;
    SetNz(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dcp_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_--;
    Write(tmp_, tmp2_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_iny_imp() {
    ReadPc();
    y_++;
    SetNz(y_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cmp_imm() {
    tmp_ = ReadPc();
    pc_++;
    DoCmp(a_, tmp_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dex_imp() {
    ReadPc();
    x_--;
    SetNz(x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sbx_imm() {
    tmp2_ = ReadPc();
    pc_++;
    x_ &= a_;
    if (x_ < tmp2_)
        p_ &= ~kFlagC;
    else
        p_ |= kFlagC;
    x_ -= tmp2_;
    SetNz(x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cpy_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    DoCmp(y_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cmp_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dec_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_--;
    SetNz(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dcp_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_--;
    Write(tmp_, tmp2_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_bne_rel() {
    tmp_ = ReadPc();
    pc_++;
    if (!(p_ & kFlagZ)) {
        ReadPc();
        if (PageChanging(pc_, std::int8_t(tmp_))) {
            ReadArg(SetL(pc_, pc_+std::int8_t(tmp_)));
        }
        pc_ += std::int8_t(tmp_);
    }
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cmp_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp2_ = Read(tmp_+y_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dcp_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_--;
    Write(tmp_, tmp2_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cmp_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp2_ = Read(std::uint8_t(tmp_+x_));
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dec_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_--;
    SetNz(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dcp_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_--;
    Write(tmp_, tmp2_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cld_imp() {
    ReadPc();
    p_ &= ~kFlagD;
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cmp_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dcp_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_--;
    Write(tmp_, tmp2_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cmp_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, x_)) {
        Read(SetL(tmp_, tmp_+x_));
    }
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dec_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_--;
    SetNz(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_dcp_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_--;
    Write(tmp_, tmp2_);
    DoCmp(a_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cpx_imm() {
    tmp_ = ReadPc();
    pc_++;
    DoCmp(x_, tmp_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sbc_nd_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_isb_nd_idx() {
    tmp2_ = ReadPc();
    pc_++;
    Read(tmp2_);
    tmp2_ += x_;
    tmp_ = Read(tmp2_ & 0xff);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_++;
    Write(tmp_, tmp2_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cpx_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    DoCmp(x_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sbc_nd_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_inc_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_++;
    SetNz(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_isb_nd_zpg() {
    tmp_ = ReadPc();
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_++;
    Write(tmp_, tmp2_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_inx_imp() {
    ReadPc();
    x_++;
    SetNz(x_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sbc_nd_imm() {
    tmp_ = ReadPc();
    pc_++;
    DoSbcNd(tmp_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_cpx_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    DoCmp(x_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sbc_nd_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_inc_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_++;
    SetNz(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_isb_nd_aba() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_++;
    Write(tmp_, tmp2_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_beq_rel() {
    tmp_ = ReadPc();
    pc_++;
    if (p_ & kFlagZ) {
        ReadPc();
        if (PageChanging(pc_, std::int8_t(tmp_))) {
            ReadArg(SetL(pc_, pc_+std::int8_t(tmp_)));
        }
        pc_ += std::int8_t(tmp_);
    }
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sbc_nd_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp2_ = Read(tmp_+y_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_isb_nd_idy() {
    tmp2_ = ReadPc();
    pc_++;
    tmp_ = Read(tmp2_);
    tmp_ = SetH(tmp_, Read((tmp2_+1) & 0xff));
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_++;
    Write(tmp_, tmp2_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sbc_nd_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp2_ = Read(std::uint8_t(tmp_+x_));
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_inc_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_++;
    SetNz(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_isb_nd_zpx() {
    tmp_ = ReadPc();
    pc_++;
    Read(tmp_);
    tmp_ = std::uint8_t(tmp_+x_);
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_++;
    Write(tmp_, tmp2_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sed_imp() {
    ReadPc();
    p_ |= kFlagD;
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sbc_nd_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, y_)) {
        Read(SetL(tmp_, tmp_+y_));
    }
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_isb_nd_aby() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+y_));
    tmp_ += y_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_++;
    Write(tmp_, tmp2_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_sbc_nd_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    if (PageChanging(tmp_, x_)) {
        Read(SetL(tmp_, tmp_+x_));
    }
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_inc_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_++;
    SetNz(tmp2_);
    Write(tmp_, tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_isb_nd_abx() {
    tmp_ = ReadPc();
    pc_++;
    tmp_ = SetH(tmp_, ReadPc());
    pc_++;
    Read(SetL(tmp_, tmp_+x_));
    tmp_ += x_;
    tmp2_ = Read(tmp_);
    Write(tmp_, tmp2_);
    tmp2_++;
    Write(tmp_, tmp2_);
    DoSbcNd(tmp2_);
    Prefetch();
}

void MameDerivedRp2a03Adapter::Op_reset() {
    ReadPc();
    ReadPc();
    Read(sp_);
    DecSp();
    Read(sp_);
    DecSp();
    Read(sp_);
    DecSp();
    p_ |= kFlagI;
    pc_ = ReadArg(0xfffc);
    pc_ = SetH(pc_, ReadArg(0xfffd));
    Prefetch();
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

const MameDerivedRp2a03Adapter::OpFn MameDerivedRp2a03Adapter::kOpTable[256] = {
    &MameDerivedRp2a03Adapter::Op_brk_imp,  // 00
    &MameDerivedRp2a03Adapter::Op_ora_idx,  // 01
    &MameDerivedRp2a03Adapter::Op_kil_non,  // 02
    &MameDerivedRp2a03Adapter::Op_slo_idx,  // 03
    &MameDerivedRp2a03Adapter::Op_nop_zpg,  // 04
    &MameDerivedRp2a03Adapter::Op_ora_zpg,  // 05
    &MameDerivedRp2a03Adapter::Op_asl_zpg,  // 06
    &MameDerivedRp2a03Adapter::Op_slo_zpg,  // 07
    &MameDerivedRp2a03Adapter::Op_php_imp,  // 08
    &MameDerivedRp2a03Adapter::Op_ora_imm,  // 09
    &MameDerivedRp2a03Adapter::Op_asl_acc,  // 0A
    &MameDerivedRp2a03Adapter::Op_anc_imm,  // 0B
    &MameDerivedRp2a03Adapter::Op_nop_aba,  // 0C
    &MameDerivedRp2a03Adapter::Op_ora_aba,  // 0D
    &MameDerivedRp2a03Adapter::Op_asl_aba,  // 0E
    &MameDerivedRp2a03Adapter::Op_slo_aba,  // 0F
    &MameDerivedRp2a03Adapter::Op_bpl_rel,  // 10
    &MameDerivedRp2a03Adapter::Op_ora_idy,  // 11
    &MameDerivedRp2a03Adapter::Op_kil_non,  // 12
    &MameDerivedRp2a03Adapter::Op_slo_idy,  // 13
    &MameDerivedRp2a03Adapter::Op_nop_zpx,  // 14
    &MameDerivedRp2a03Adapter::Op_ora_zpx,  // 15
    &MameDerivedRp2a03Adapter::Op_asl_zpx,  // 16
    &MameDerivedRp2a03Adapter::Op_slo_zpx,  // 17
    &MameDerivedRp2a03Adapter::Op_clc_imp,  // 18
    &MameDerivedRp2a03Adapter::Op_ora_aby,  // 19
    &MameDerivedRp2a03Adapter::Op_nop_imp,  // 1A
    &MameDerivedRp2a03Adapter::Op_slo_aby,  // 1B
    &MameDerivedRp2a03Adapter::Op_nop_abx,  // 1C
    &MameDerivedRp2a03Adapter::Op_ora_abx,  // 1D
    &MameDerivedRp2a03Adapter::Op_asl_abx,  // 1E
    &MameDerivedRp2a03Adapter::Op_slo_abx,  // 1F
    &MameDerivedRp2a03Adapter::Op_jsr_adr,  // 20
    &MameDerivedRp2a03Adapter::Op_and_idx,  // 21
    &MameDerivedRp2a03Adapter::Op_kil_non,  // 22
    &MameDerivedRp2a03Adapter::Op_rla_idx,  // 23
    &MameDerivedRp2a03Adapter::Op_bit_zpg,  // 24
    &MameDerivedRp2a03Adapter::Op_and_zpg,  // 25
    &MameDerivedRp2a03Adapter::Op_rol_zpg,  // 26
    &MameDerivedRp2a03Adapter::Op_rla_zpg,  // 27
    &MameDerivedRp2a03Adapter::Op_plp_imp,  // 28
    &MameDerivedRp2a03Adapter::Op_and_imm,  // 29
    &MameDerivedRp2a03Adapter::Op_rol_acc,  // 2A
    &MameDerivedRp2a03Adapter::Op_anc_imm,  // 2B
    &MameDerivedRp2a03Adapter::Op_bit_aba,  // 2C
    &MameDerivedRp2a03Adapter::Op_and_aba,  // 2D
    &MameDerivedRp2a03Adapter::Op_rol_aba,  // 2E
    &MameDerivedRp2a03Adapter::Op_rla_aba,  // 2F
    &MameDerivedRp2a03Adapter::Op_bmi_rel,  // 30
    &MameDerivedRp2a03Adapter::Op_and_idy,  // 31
    &MameDerivedRp2a03Adapter::Op_kil_non,  // 32
    &MameDerivedRp2a03Adapter::Op_rla_idy,  // 33
    &MameDerivedRp2a03Adapter::Op_nop_zpx,  // 34
    &MameDerivedRp2a03Adapter::Op_and_zpx,  // 35
    &MameDerivedRp2a03Adapter::Op_rol_zpx,  // 36
    &MameDerivedRp2a03Adapter::Op_rla_zpx,  // 37
    &MameDerivedRp2a03Adapter::Op_sec_imp,  // 38
    &MameDerivedRp2a03Adapter::Op_and_aby,  // 39
    &MameDerivedRp2a03Adapter::Op_nop_imp,  // 3A
    &MameDerivedRp2a03Adapter::Op_rla_aby,  // 3B
    &MameDerivedRp2a03Adapter::Op_nop_abx,  // 3C
    &MameDerivedRp2a03Adapter::Op_and_abx,  // 3D
    &MameDerivedRp2a03Adapter::Op_rol_abx,  // 3E
    &MameDerivedRp2a03Adapter::Op_rla_abx,  // 3F
    &MameDerivedRp2a03Adapter::Op_rti_imp,  // 40
    &MameDerivedRp2a03Adapter::Op_eor_idx,  // 41
    &MameDerivedRp2a03Adapter::Op_kil_non,  // 42
    &MameDerivedRp2a03Adapter::Op_sre_idx,  // 43
    &MameDerivedRp2a03Adapter::Op_nop_zpg,  // 44
    &MameDerivedRp2a03Adapter::Op_eor_zpg,  // 45
    &MameDerivedRp2a03Adapter::Op_lsr_zpg,  // 46
    &MameDerivedRp2a03Adapter::Op_sre_zpg,  // 47
    &MameDerivedRp2a03Adapter::Op_pha_imp,  // 48
    &MameDerivedRp2a03Adapter::Op_eor_imm,  // 49
    &MameDerivedRp2a03Adapter::Op_lsr_acc,  // 4A
    &MameDerivedRp2a03Adapter::Op_asr_imm,  // 4B
    &MameDerivedRp2a03Adapter::Op_jmp_adr,  // 4C
    &MameDerivedRp2a03Adapter::Op_eor_aba,  // 4D
    &MameDerivedRp2a03Adapter::Op_lsr_aba,  // 4E
    &MameDerivedRp2a03Adapter::Op_sre_aba,  // 4F
    &MameDerivedRp2a03Adapter::Op_bvc_rel,  // 50
    &MameDerivedRp2a03Adapter::Op_eor_idy,  // 51
    &MameDerivedRp2a03Adapter::Op_kil_non,  // 52
    &MameDerivedRp2a03Adapter::Op_sre_idy,  // 53
    &MameDerivedRp2a03Adapter::Op_nop_zpx,  // 54
    &MameDerivedRp2a03Adapter::Op_eor_zpx,  // 55
    &MameDerivedRp2a03Adapter::Op_lsr_zpx,  // 56
    &MameDerivedRp2a03Adapter::Op_sre_zpx,  // 57
    &MameDerivedRp2a03Adapter::Op_cli_imp,  // 58
    &MameDerivedRp2a03Adapter::Op_eor_aby,  // 59
    &MameDerivedRp2a03Adapter::Op_nop_imp,  // 5A
    &MameDerivedRp2a03Adapter::Op_sre_aby,  // 5B
    &MameDerivedRp2a03Adapter::Op_nop_abx,  // 5C
    &MameDerivedRp2a03Adapter::Op_eor_abx,  // 5D
    &MameDerivedRp2a03Adapter::Op_lsr_abx,  // 5E
    &MameDerivedRp2a03Adapter::Op_sre_abx,  // 5F
    &MameDerivedRp2a03Adapter::Op_rts_imp,  // 60
    &MameDerivedRp2a03Adapter::Op_adc_nd_idx,  // 61
    &MameDerivedRp2a03Adapter::Op_kil_non,  // 62
    &MameDerivedRp2a03Adapter::Op_rra_nd_idx,  // 63
    &MameDerivedRp2a03Adapter::Op_nop_zpg,  // 64
    &MameDerivedRp2a03Adapter::Op_adc_nd_zpg,  // 65
    &MameDerivedRp2a03Adapter::Op_ror_zpg,  // 66
    &MameDerivedRp2a03Adapter::Op_rra_nd_zpg,  // 67
    &MameDerivedRp2a03Adapter::Op_pla_imp,  // 68
    &MameDerivedRp2a03Adapter::Op_adc_nd_imm,  // 69
    &MameDerivedRp2a03Adapter::Op_ror_acc,  // 6A
    &MameDerivedRp2a03Adapter::Op_arr_nd_imm,  // 6B
    &MameDerivedRp2a03Adapter::Op_jmp_ind,  // 6C
    &MameDerivedRp2a03Adapter::Op_adc_nd_aba,  // 6D
    &MameDerivedRp2a03Adapter::Op_ror_aba,  // 6E
    &MameDerivedRp2a03Adapter::Op_rra_nd_aba,  // 6F
    &MameDerivedRp2a03Adapter::Op_bvs_rel,  // 70
    &MameDerivedRp2a03Adapter::Op_adc_nd_idy,  // 71
    &MameDerivedRp2a03Adapter::Op_kil_non,  // 72
    &MameDerivedRp2a03Adapter::Op_rra_nd_idy,  // 73
    &MameDerivedRp2a03Adapter::Op_nop_zpx,  // 74
    &MameDerivedRp2a03Adapter::Op_adc_nd_zpx,  // 75
    &MameDerivedRp2a03Adapter::Op_ror_zpx,  // 76
    &MameDerivedRp2a03Adapter::Op_rra_nd_zpx,  // 77
    &MameDerivedRp2a03Adapter::Op_sei_imp,  // 78
    &MameDerivedRp2a03Adapter::Op_adc_nd_aby,  // 79
    &MameDerivedRp2a03Adapter::Op_nop_imp,  // 7A
    &MameDerivedRp2a03Adapter::Op_rra_nd_aby,  // 7B
    &MameDerivedRp2a03Adapter::Op_nop_abx,  // 7C
    &MameDerivedRp2a03Adapter::Op_adc_nd_abx,  // 7D
    &MameDerivedRp2a03Adapter::Op_ror_abx,  // 7E
    &MameDerivedRp2a03Adapter::Op_rra_nd_abx,  // 7F
    &MameDerivedRp2a03Adapter::Op_nop_imm,  // 80
    &MameDerivedRp2a03Adapter::Op_sta_idx,  // 81
    &MameDerivedRp2a03Adapter::Op_nop_imm,  // 82
    &MameDerivedRp2a03Adapter::Op_sax_idx,  // 83
    &MameDerivedRp2a03Adapter::Op_sty_zpg,  // 84
    &MameDerivedRp2a03Adapter::Op_sta_zpg,  // 85
    &MameDerivedRp2a03Adapter::Op_stx_zpg,  // 86
    &MameDerivedRp2a03Adapter::Op_sax_zpg,  // 87
    &MameDerivedRp2a03Adapter::Op_dey_imp,  // 88
    &MameDerivedRp2a03Adapter::Op_nop_imm,  // 89
    &MameDerivedRp2a03Adapter::Op_txa_imp,  // 8A
    &MameDerivedRp2a03Adapter::Op_ane_imm,  // 8B
    &MameDerivedRp2a03Adapter::Op_sty_aba,  // 8C
    &MameDerivedRp2a03Adapter::Op_sta_aba,  // 8D
    &MameDerivedRp2a03Adapter::Op_stx_aba,  // 8E
    &MameDerivedRp2a03Adapter::Op_sax_aba,  // 8F
    &MameDerivedRp2a03Adapter::Op_bcc_rel,  // 90
    &MameDerivedRp2a03Adapter::Op_sta_idy,  // 91
    &MameDerivedRp2a03Adapter::Op_kil_non,  // 92
    &MameDerivedRp2a03Adapter::Op_sha_idy,  // 93
    &MameDerivedRp2a03Adapter::Op_sty_zpx,  // 94
    &MameDerivedRp2a03Adapter::Op_sta_zpx,  // 95
    &MameDerivedRp2a03Adapter::Op_stx_zpy,  // 96
    &MameDerivedRp2a03Adapter::Op_sax_zpy,  // 97
    &MameDerivedRp2a03Adapter::Op_tya_imp,  // 98
    &MameDerivedRp2a03Adapter::Op_sta_aby,  // 99
    &MameDerivedRp2a03Adapter::Op_txs_imp,  // 9A
    &MameDerivedRp2a03Adapter::Op_shs_aby,  // 9B
    &MameDerivedRp2a03Adapter::Op_shy_abx,  // 9C
    &MameDerivedRp2a03Adapter::Op_sta_abx,  // 9D
    &MameDerivedRp2a03Adapter::Op_shx_aby,  // 9E
    &MameDerivedRp2a03Adapter::Op_sha_aby,  // 9F
    &MameDerivedRp2a03Adapter::Op_ldy_imm,  // A0
    &MameDerivedRp2a03Adapter::Op_lda_idx,  // A1
    &MameDerivedRp2a03Adapter::Op_ldx_imm,  // A2
    &MameDerivedRp2a03Adapter::Op_lax_idx,  // A3
    &MameDerivedRp2a03Adapter::Op_ldy_zpg,  // A4
    &MameDerivedRp2a03Adapter::Op_lda_zpg,  // A5
    &MameDerivedRp2a03Adapter::Op_ldx_zpg,  // A6
    &MameDerivedRp2a03Adapter::Op_lax_zpg,  // A7
    &MameDerivedRp2a03Adapter::Op_tay_imp,  // A8
    &MameDerivedRp2a03Adapter::Op_lda_imm,  // A9
    &MameDerivedRp2a03Adapter::Op_tax_imp,  // AA
    &MameDerivedRp2a03Adapter::Op_lxa_imm,  // AB
    &MameDerivedRp2a03Adapter::Op_ldy_aba,  // AC
    &MameDerivedRp2a03Adapter::Op_lda_aba,  // AD
    &MameDerivedRp2a03Adapter::Op_ldx_aba,  // AE
    &MameDerivedRp2a03Adapter::Op_lax_aba,  // AF
    &MameDerivedRp2a03Adapter::Op_bcs_rel,  // B0
    &MameDerivedRp2a03Adapter::Op_lda_idy,  // B1
    &MameDerivedRp2a03Adapter::Op_kil_non,  // B2
    &MameDerivedRp2a03Adapter::Op_lax_idy,  // B3
    &MameDerivedRp2a03Adapter::Op_ldy_zpx,  // B4
    &MameDerivedRp2a03Adapter::Op_lda_zpx,  // B5
    &MameDerivedRp2a03Adapter::Op_ldx_zpy,  // B6
    &MameDerivedRp2a03Adapter::Op_lax_zpy,  // B7
    &MameDerivedRp2a03Adapter::Op_clv_imp,  // B8
    &MameDerivedRp2a03Adapter::Op_lda_aby,  // B9
    &MameDerivedRp2a03Adapter::Op_tsx_imp,  // BA
    &MameDerivedRp2a03Adapter::Op_las_aby,  // BB
    &MameDerivedRp2a03Adapter::Op_ldy_abx,  // BC
    &MameDerivedRp2a03Adapter::Op_lda_abx,  // BD
    &MameDerivedRp2a03Adapter::Op_ldx_aby,  // BE
    &MameDerivedRp2a03Adapter::Op_lax_aby,  // BF
    &MameDerivedRp2a03Adapter::Op_cpy_imm,  // C0
    &MameDerivedRp2a03Adapter::Op_cmp_idx,  // C1
    &MameDerivedRp2a03Adapter::Op_nop_imm,  // C2
    &MameDerivedRp2a03Adapter::Op_dcp_idx,  // C3
    &MameDerivedRp2a03Adapter::Op_cpy_zpg,  // C4
    &MameDerivedRp2a03Adapter::Op_cmp_zpg,  // C5
    &MameDerivedRp2a03Adapter::Op_dec_zpg,  // C6
    &MameDerivedRp2a03Adapter::Op_dcp_zpg,  // C7
    &MameDerivedRp2a03Adapter::Op_iny_imp,  // C8
    &MameDerivedRp2a03Adapter::Op_cmp_imm,  // C9
    &MameDerivedRp2a03Adapter::Op_dex_imp,  // CA
    &MameDerivedRp2a03Adapter::Op_sbx_imm,  // CB
    &MameDerivedRp2a03Adapter::Op_cpy_aba,  // CC
    &MameDerivedRp2a03Adapter::Op_cmp_aba,  // CD
    &MameDerivedRp2a03Adapter::Op_dec_aba,  // CE
    &MameDerivedRp2a03Adapter::Op_dcp_aba,  // CF
    &MameDerivedRp2a03Adapter::Op_bne_rel,  // D0
    &MameDerivedRp2a03Adapter::Op_cmp_idy,  // D1
    &MameDerivedRp2a03Adapter::Op_kil_non,  // D2
    &MameDerivedRp2a03Adapter::Op_dcp_idy,  // D3
    &MameDerivedRp2a03Adapter::Op_nop_zpx,  // D4
    &MameDerivedRp2a03Adapter::Op_cmp_zpx,  // D5
    &MameDerivedRp2a03Adapter::Op_dec_zpx,  // D6
    &MameDerivedRp2a03Adapter::Op_dcp_zpx,  // D7
    &MameDerivedRp2a03Adapter::Op_cld_imp,  // D8
    &MameDerivedRp2a03Adapter::Op_cmp_aby,  // D9
    &MameDerivedRp2a03Adapter::Op_nop_imp,  // DA
    &MameDerivedRp2a03Adapter::Op_dcp_aby,  // DB
    &MameDerivedRp2a03Adapter::Op_nop_abx,  // DC
    &MameDerivedRp2a03Adapter::Op_cmp_abx,  // DD
    &MameDerivedRp2a03Adapter::Op_dec_abx,  // DE
    &MameDerivedRp2a03Adapter::Op_dcp_abx,  // DF
    &MameDerivedRp2a03Adapter::Op_cpx_imm,  // E0
    &MameDerivedRp2a03Adapter::Op_sbc_nd_idx,  // E1
    &MameDerivedRp2a03Adapter::Op_nop_imm,  // E2
    &MameDerivedRp2a03Adapter::Op_isb_nd_idx,  // E3
    &MameDerivedRp2a03Adapter::Op_cpx_zpg,  // E4
    &MameDerivedRp2a03Adapter::Op_sbc_nd_zpg,  // E5
    &MameDerivedRp2a03Adapter::Op_inc_zpg,  // E6
    &MameDerivedRp2a03Adapter::Op_isb_nd_zpg,  // E7
    &MameDerivedRp2a03Adapter::Op_inx_imp,  // E8
    &MameDerivedRp2a03Adapter::Op_sbc_nd_imm,  // E9
    &MameDerivedRp2a03Adapter::Op_nop_imp,  // EA
    &MameDerivedRp2a03Adapter::Op_sbc_nd_imm,  // EB
    &MameDerivedRp2a03Adapter::Op_cpx_aba,  // EC
    &MameDerivedRp2a03Adapter::Op_sbc_nd_aba,  // ED
    &MameDerivedRp2a03Adapter::Op_inc_aba,  // EE
    &MameDerivedRp2a03Adapter::Op_isb_nd_aba,  // EF
    &MameDerivedRp2a03Adapter::Op_beq_rel,  // F0
    &MameDerivedRp2a03Adapter::Op_sbc_nd_idy,  // F1
    &MameDerivedRp2a03Adapter::Op_kil_non,  // F2
    &MameDerivedRp2a03Adapter::Op_isb_nd_idy,  // F3
    &MameDerivedRp2a03Adapter::Op_nop_zpx,  // F4
    &MameDerivedRp2a03Adapter::Op_sbc_nd_zpx,  // F5
    &MameDerivedRp2a03Adapter::Op_inc_zpx,  // F6
    &MameDerivedRp2a03Adapter::Op_isb_nd_zpx,  // F7
    &MameDerivedRp2a03Adapter::Op_sed_imp,  // F8
    &MameDerivedRp2a03Adapter::Op_sbc_nd_aby,  // F9
    &MameDerivedRp2a03Adapter::Op_nop_imp,  // FA
    &MameDerivedRp2a03Adapter::Op_isb_nd_aby,  // FB
    &MameDerivedRp2a03Adapter::Op_nop_abx,  // FC
    &MameDerivedRp2a03Adapter::Op_sbc_nd_abx,  // FD
    &MameDerivedRp2a03Adapter::Op_inc_abx,  // FE
    &MameDerivedRp2a03Adapter::Op_isb_nd_abx,  // FF
};
