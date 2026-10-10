// license:BSD-3-Clause
// copyright-holders:Bryan McPhail
// The original files also state: "Copyright Bryan McPhail, mish@tendril.co.uk" (based, with permission, on Juergen Buchmueller's 6502 emulator).
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/h6280/h6280.cpp
//   src/devices/cpu/h6280/h6280.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill
//
// This file is a direct port of MAME's h6280.cpp (Hudson HuC6280 core).
//
// Adaptation for Chipestra (see THIRD-PARTY-NOTICES.md): the
// instruction/addressing/interrupt/timer code between the "PORTED FROM MAME" markers is
// MAME's, with only mechanical changes (class name, offs_t -> std::uint32_t, the
// logerror()/standard_irq_callback() calls and the VDC port writes of ST0/ST1/ST2 removed,
// one interrupt counter added in do_interrupt()). The device-framework glue (bus access,
// execute loop, input lines, I/O page dispatch, reset, PSG stub) is written for Chipestra in the
// last section of this file.
//
// NOTE from the MAME source (kept as a reminder of what "same as MAME" means): "This code is
// around 99% complete! Several things are unimplemented ... I am unsure if flag B is set upon
// execution of rti. Cycle counts should be quite accurate."

#include "mame_derived_h6280_adapter.h"

#include <cstdint>
#include <cstring>

#define H6280_LAZY_FLAGS 0

/* 6280 flags */
enum
{
    _fC = 0x01,
    _fZ = 0x02,
    _fI = 0x04,
    _fD = 0x08,
    _fB = 0x10,
    _fT = 0x20,
    _fV = 0x40,
    _fN = 0x80
};

namespace {
constexpr int CLEAR_LINE = 0;   // MAME line states used by the ported code
constexpr int ASSERT_LINE = 1;
}  // namespace

/* some shortcuts for improved readability (undefined again after the ported section) */
#define A   m_a
#define X   m_x
#define Y   m_y
#define P   m_p
#define S   m_sp.b.l

#define EAL m_ea.b.l
#define EAH m_ea.b.h
#define EAW m_ea.w.l
#define EAD m_ea.d

#define ZPL m_zp.b.l
#define ZPH m_zp.b.h
#define ZPW m_zp.w.l
#define ZPD m_zp.d

#define PCL m_pc.b.l
#define PCH m_pc.b.h
#define PCW m_pc.w.l
#define PCD m_pc.d

#if defined(_MSC_VER)
#pragma warning(push)
// MAME's code narrows int -> uint8_t in many places by design (compose_p(0, _fB), &= masks ...); keep it verbatim.
#pragma warning(disable : 4244)
#endif

// ===================== PORTED FROM MAME: opcode table =====================
const MameDerivedH6280Adapter::ophandler MameDerivedH6280Adapter::s_opcodetable[256] =
{
	&MameDerivedH6280Adapter::op_00, &MameDerivedH6280Adapter::op_01, &MameDerivedH6280Adapter::op_02, &MameDerivedH6280Adapter::op_03, &MameDerivedH6280Adapter::op_04, &MameDerivedH6280Adapter::op_05, &MameDerivedH6280Adapter::op_06, &MameDerivedH6280Adapter::op_07,
	&MameDerivedH6280Adapter::op_08, &MameDerivedH6280Adapter::op_09, &MameDerivedH6280Adapter::op_0a, &MameDerivedH6280Adapter::op_0b, &MameDerivedH6280Adapter::op_0c, &MameDerivedH6280Adapter::op_0d, &MameDerivedH6280Adapter::op_0e, &MameDerivedH6280Adapter::op_0f,
	&MameDerivedH6280Adapter::op_10, &MameDerivedH6280Adapter::op_11, &MameDerivedH6280Adapter::op_12, &MameDerivedH6280Adapter::op_13, &MameDerivedH6280Adapter::op_14, &MameDerivedH6280Adapter::op_15, &MameDerivedH6280Adapter::op_16, &MameDerivedH6280Adapter::op_17,
	&MameDerivedH6280Adapter::op_18, &MameDerivedH6280Adapter::op_19, &MameDerivedH6280Adapter::op_1a, &MameDerivedH6280Adapter::op_1b, &MameDerivedH6280Adapter::op_1c, &MameDerivedH6280Adapter::op_1d, &MameDerivedH6280Adapter::op_1e, &MameDerivedH6280Adapter::op_1f,
	&MameDerivedH6280Adapter::op_20, &MameDerivedH6280Adapter::op_21, &MameDerivedH6280Adapter::op_22, &MameDerivedH6280Adapter::op_23, &MameDerivedH6280Adapter::op_24, &MameDerivedH6280Adapter::op_25, &MameDerivedH6280Adapter::op_26, &MameDerivedH6280Adapter::op_27,
	&MameDerivedH6280Adapter::op_28, &MameDerivedH6280Adapter::op_29, &MameDerivedH6280Adapter::op_2a, &MameDerivedH6280Adapter::op_2b, &MameDerivedH6280Adapter::op_2c, &MameDerivedH6280Adapter::op_2d, &MameDerivedH6280Adapter::op_2e, &MameDerivedH6280Adapter::op_2f,
	&MameDerivedH6280Adapter::op_30, &MameDerivedH6280Adapter::op_31, &MameDerivedH6280Adapter::op_32, &MameDerivedH6280Adapter::op_33, &MameDerivedH6280Adapter::op_34, &MameDerivedH6280Adapter::op_35, &MameDerivedH6280Adapter::op_36, &MameDerivedH6280Adapter::op_37,
	&MameDerivedH6280Adapter::op_38, &MameDerivedH6280Adapter::op_39, &MameDerivedH6280Adapter::op_3a, &MameDerivedH6280Adapter::op_3b, &MameDerivedH6280Adapter::op_3c, &MameDerivedH6280Adapter::op_3d, &MameDerivedH6280Adapter::op_3e, &MameDerivedH6280Adapter::op_3f,
	&MameDerivedH6280Adapter::op_40, &MameDerivedH6280Adapter::op_41, &MameDerivedH6280Adapter::op_42, &MameDerivedH6280Adapter::op_43, &MameDerivedH6280Adapter::op_44, &MameDerivedH6280Adapter::op_45, &MameDerivedH6280Adapter::op_46, &MameDerivedH6280Adapter::op_47,
	&MameDerivedH6280Adapter::op_48, &MameDerivedH6280Adapter::op_49, &MameDerivedH6280Adapter::op_4a, &MameDerivedH6280Adapter::op_4b, &MameDerivedH6280Adapter::op_4c, &MameDerivedH6280Adapter::op_4d, &MameDerivedH6280Adapter::op_4e, &MameDerivedH6280Adapter::op_4f,
	&MameDerivedH6280Adapter::op_50, &MameDerivedH6280Adapter::op_51, &MameDerivedH6280Adapter::op_52, &MameDerivedH6280Adapter::op_53, &MameDerivedH6280Adapter::op_54, &MameDerivedH6280Adapter::op_55, &MameDerivedH6280Adapter::op_56, &MameDerivedH6280Adapter::op_57,
	&MameDerivedH6280Adapter::op_58, &MameDerivedH6280Adapter::op_59, &MameDerivedH6280Adapter::op_5a, &MameDerivedH6280Adapter::op_5b, &MameDerivedH6280Adapter::op_5c, &MameDerivedH6280Adapter::op_5d, &MameDerivedH6280Adapter::op_5e, &MameDerivedH6280Adapter::op_5f,
	&MameDerivedH6280Adapter::op_60, &MameDerivedH6280Adapter::op_61, &MameDerivedH6280Adapter::op_62, &MameDerivedH6280Adapter::op_63, &MameDerivedH6280Adapter::op_64, &MameDerivedH6280Adapter::op_65, &MameDerivedH6280Adapter::op_66, &MameDerivedH6280Adapter::op_67,
	&MameDerivedH6280Adapter::op_68, &MameDerivedH6280Adapter::op_69, &MameDerivedH6280Adapter::op_6a, &MameDerivedH6280Adapter::op_6b, &MameDerivedH6280Adapter::op_6c, &MameDerivedH6280Adapter::op_6d, &MameDerivedH6280Adapter::op_6e, &MameDerivedH6280Adapter::op_6f,
	&MameDerivedH6280Adapter::op_70, &MameDerivedH6280Adapter::op_71, &MameDerivedH6280Adapter::op_72, &MameDerivedH6280Adapter::op_73, &MameDerivedH6280Adapter::op_74, &MameDerivedH6280Adapter::op_75, &MameDerivedH6280Adapter::op_76, &MameDerivedH6280Adapter::op_77,
	&MameDerivedH6280Adapter::op_78, &MameDerivedH6280Adapter::op_79, &MameDerivedH6280Adapter::op_7a, &MameDerivedH6280Adapter::op_7b, &MameDerivedH6280Adapter::op_7c, &MameDerivedH6280Adapter::op_7d, &MameDerivedH6280Adapter::op_7e, &MameDerivedH6280Adapter::op_7f,
	&MameDerivedH6280Adapter::op_80, &MameDerivedH6280Adapter::op_81, &MameDerivedH6280Adapter::op_82, &MameDerivedH6280Adapter::op_83, &MameDerivedH6280Adapter::op_84, &MameDerivedH6280Adapter::op_85, &MameDerivedH6280Adapter::op_86, &MameDerivedH6280Adapter::op_87,
	&MameDerivedH6280Adapter::op_88, &MameDerivedH6280Adapter::op_89, &MameDerivedH6280Adapter::op_8a, &MameDerivedH6280Adapter::op_8b, &MameDerivedH6280Adapter::op_8c, &MameDerivedH6280Adapter::op_8d, &MameDerivedH6280Adapter::op_8e, &MameDerivedH6280Adapter::op_8f,
	&MameDerivedH6280Adapter::op_90, &MameDerivedH6280Adapter::op_91, &MameDerivedH6280Adapter::op_92, &MameDerivedH6280Adapter::op_93, &MameDerivedH6280Adapter::op_94, &MameDerivedH6280Adapter::op_95, &MameDerivedH6280Adapter::op_96, &MameDerivedH6280Adapter::op_97,
	&MameDerivedH6280Adapter::op_98, &MameDerivedH6280Adapter::op_99, &MameDerivedH6280Adapter::op_9a, &MameDerivedH6280Adapter::op_9b, &MameDerivedH6280Adapter::op_9c, &MameDerivedH6280Adapter::op_9d, &MameDerivedH6280Adapter::op_9e, &MameDerivedH6280Adapter::op_9f,
	&MameDerivedH6280Adapter::op_a0, &MameDerivedH6280Adapter::op_a1, &MameDerivedH6280Adapter::op_a2, &MameDerivedH6280Adapter::op_a3, &MameDerivedH6280Adapter::op_a4, &MameDerivedH6280Adapter::op_a5, &MameDerivedH6280Adapter::op_a6, &MameDerivedH6280Adapter::op_a7,
	&MameDerivedH6280Adapter::op_a8, &MameDerivedH6280Adapter::op_a9, &MameDerivedH6280Adapter::op_aa, &MameDerivedH6280Adapter::op_ab, &MameDerivedH6280Adapter::op_ac, &MameDerivedH6280Adapter::op_ad, &MameDerivedH6280Adapter::op_ae, &MameDerivedH6280Adapter::op_af,
	&MameDerivedH6280Adapter::op_b0, &MameDerivedH6280Adapter::op_b1, &MameDerivedH6280Adapter::op_b2, &MameDerivedH6280Adapter::op_b3, &MameDerivedH6280Adapter::op_b4, &MameDerivedH6280Adapter::op_b5, &MameDerivedH6280Adapter::op_b6, &MameDerivedH6280Adapter::op_b7,
	&MameDerivedH6280Adapter::op_b8, &MameDerivedH6280Adapter::op_b9, &MameDerivedH6280Adapter::op_ba, &MameDerivedH6280Adapter::op_bb, &MameDerivedH6280Adapter::op_bc, &MameDerivedH6280Adapter::op_bd, &MameDerivedH6280Adapter::op_be, &MameDerivedH6280Adapter::op_bf,
	&MameDerivedH6280Adapter::op_c0, &MameDerivedH6280Adapter::op_c1, &MameDerivedH6280Adapter::op_c2, &MameDerivedH6280Adapter::op_c3, &MameDerivedH6280Adapter::op_c4, &MameDerivedH6280Adapter::op_c5, &MameDerivedH6280Adapter::op_c6, &MameDerivedH6280Adapter::op_c7,
	&MameDerivedH6280Adapter::op_c8, &MameDerivedH6280Adapter::op_c9, &MameDerivedH6280Adapter::op_ca, &MameDerivedH6280Adapter::op_cb, &MameDerivedH6280Adapter::op_cc, &MameDerivedH6280Adapter::op_cd, &MameDerivedH6280Adapter::op_ce, &MameDerivedH6280Adapter::op_cf,
	&MameDerivedH6280Adapter::op_d0, &MameDerivedH6280Adapter::op_d1, &MameDerivedH6280Adapter::op_d2, &MameDerivedH6280Adapter::op_d3, &MameDerivedH6280Adapter::op_d4, &MameDerivedH6280Adapter::op_d5, &MameDerivedH6280Adapter::op_d6, &MameDerivedH6280Adapter::op_d7,
	&MameDerivedH6280Adapter::op_d8, &MameDerivedH6280Adapter::op_d9, &MameDerivedH6280Adapter::op_da, &MameDerivedH6280Adapter::op_db, &MameDerivedH6280Adapter::op_dc, &MameDerivedH6280Adapter::op_dd, &MameDerivedH6280Adapter::op_de, &MameDerivedH6280Adapter::op_df,
	&MameDerivedH6280Adapter::op_e0, &MameDerivedH6280Adapter::op_e1, &MameDerivedH6280Adapter::op_e2, &MameDerivedH6280Adapter::op_e3, &MameDerivedH6280Adapter::op_e4, &MameDerivedH6280Adapter::op_e5, &MameDerivedH6280Adapter::op_e6, &MameDerivedH6280Adapter::op_e7,
	&MameDerivedH6280Adapter::op_e8, &MameDerivedH6280Adapter::op_e9, &MameDerivedH6280Adapter::op_ea, &MameDerivedH6280Adapter::op_eb, &MameDerivedH6280Adapter::op_ec, &MameDerivedH6280Adapter::op_ed, &MameDerivedH6280Adapter::op_ee, &MameDerivedH6280Adapter::op_ef,
	&MameDerivedH6280Adapter::op_f0, &MameDerivedH6280Adapter::op_f1, &MameDerivedH6280Adapter::op_f2, &MameDerivedH6280Adapter::op_f3, &MameDerivedH6280Adapter::op_f4, &MameDerivedH6280Adapter::op_f5, &MameDerivedH6280Adapter::op_f6, &MameDerivedH6280Adapter::op_f7,
	&MameDerivedH6280Adapter::op_f8, &MameDerivedH6280Adapter::op_f9, &MameDerivedH6280Adapter::op_fa, &MameDerivedH6280Adapter::op_fb, &MameDerivedH6280Adapter::op_fc, &MameDerivedH6280Adapter::op_fd, &MameDerivedH6280Adapter::op_fe, &MameDerivedH6280Adapter::op_ff
};

// ===================== PORTED FROM MAME: helpers and opcodes =====================
inline uint32_t MameDerivedH6280Adapter::translated(uint16_t addr)
{
	return ((m_mmr[((addr) >> 13) & 7] << 13) | ((addr) & 0x1fff));
}

inline void MameDerivedH6280Adapter::h6280_cycles(int cyc)
{
	m_icount -= ((cyc) * m_clocks_per_cycle);
	m_timer_value -= ((cyc) * m_clocks_per_cycle);
}

#if H6280_LAZY_FLAGS

#define NZ  m_NZ
inline void MameDerivedH6280Adapter::set_nz(uint8_t n)
{
	P &= ~_fT;
	NZ = ((n & _fN) << 8) | n;
}

#else

inline void MameDerivedH6280Adapter::set_nz(uint8_t n)
{
	P = (P & ~(_fN|_fT|_fZ)) |
		(n & _fN) |
		((n == 0) ? _fZ : 0);
}

#endif

inline void MameDerivedH6280Adapter::clear_t()
{
	P &= ~_fT;
}

inline void MameDerivedH6280Adapter::do_interrupt(uint16_t vector)
{
	h6280_cycles(7);    /* 7 cycles for an int */
    if (vector == H6280_NMI_VEC) ++nmiServiceCount_; else ++irqServiceCount_;  // Chipestra addition (counters for tests)
	push(PCH);
	push(PCL);
	compose_p(0, _fB);
	push(P);
	P = (P & ~_fD) | _fI;   /* knock out D and set I flag */
	PCL = program_read8(vector);
	PCH = program_read8(vector + 1);
}

inline void MameDerivedH6280Adapter::check_and_take_irq_lines()
{
	if ( m_nmi_state != CLEAR_LINE ) {
		m_nmi_state = CLEAR_LINE;
		do_interrupt(H6280_NMI_VEC);
	}
	else if( !(P & _fI) )
	{
		if ( m_irq_state[2] != CLEAR_LINE &&
				!(m_irq_mask & 0x4) )
		{
			do_interrupt(H6280_TIMER_VEC);
		} else
		if ( m_irq_state[0] != CLEAR_LINE &&
				!(m_irq_mask & 0x2) )
		{
			do_interrupt(H6280_IRQ1_VEC);
		} else
		if ( m_irq_state[1] != CLEAR_LINE &&
				!(m_irq_mask & 0x1) )
		{
			do_interrupt(H6280_IRQ2_VEC);
		}
	}
}

inline void MameDerivedH6280Adapter::check_irq_lines()
{
	if (!m_irq_pending)
		m_irq_pending = 2;
}

/***************************************************************
 *  CHECK_VDC_VCE_PENALTY
 * The CPU inserts 1 clock delay when accessing the VDC or VCE
 * area.
 ***************************************************************/
inline void MameDerivedH6280Adapter::check_vdc_vce_penalty(uint16_t addr)
{
	if ( ( translated(addr) & 0x1FF800 ) == 0x1FE000 ) {
		h6280_cycles(1);
	}
}

/***************************************************************
 *  BRA  branch relative
 ***************************************************************/
inline void MameDerivedH6280Adapter::bra(bool cond)
{
	clear_t();
	if (cond)
	{
		h6280_cycles(4);
		uint8_t tmp = read_opcode_arg();
		PCW++;
		EAW = PCW + (signed char)tmp;
		PCD = EAD;
	}
	else
	{
		PCW++;
		h6280_cycles(2);
	}
}

/***************************************************************
 *
 * Helper macros to build the effective address
 *
 ***************************************************************/

/***************************************************************
 *  EA = zero page address
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_zpg()
{
	ZPL = read_opcode_arg();
	PCW++;
	EAD = ZPD;
}

/***************************************************************
 *  EA = zero page address - T flag
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_tflg()
{
	ZPL = X;
	EAD = ZPD;
}

/***************************************************************
 *  EA = zero page address + X
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_zpx()
{
	ZPL = read_opcode_arg() + X;
	PCW++;
	EAD = ZPD;
}

/***************************************************************
 *  EA = zero page address + Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_zpy()
{
	ZPL = read_opcode_arg() + Y;
	PCW++;
	EAD = ZPD;
}

/***************************************************************
 *  EA = absolute address
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_abs()
{
	EAL = read_opcode_arg();
	PCW++;
	EAH = read_opcode_arg();
	PCW++;
}

/***************************************************************
 *  EA = absolute address + X
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_abx()
{
	ea_abs();
	EAW += X;
}

/***************************************************************
 *  EA = absolute address + Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_aby()
{
	ea_abs();
	EAW += Y;
}

/***************************************************************
 *  EA = zero page indirect (65c02 pre indexed w/o X)
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_zpi()
{
	ZPL = read_opcode_arg();
	PCW++;
	EAD = program_read16z(ZPD);
}

/***************************************************************
 *  EA = zero page + X indirect (pre indexed)
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_idx()
{
	ZPL = read_opcode_arg() + X;
	PCW++;
	EAD = program_read16z(ZPD);
}

/***************************************************************
 *  EA = zero page indirect + Y (post indexed)
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_idy()
{
	ZPL = read_opcode_arg();
	PCW++;
	EAD = program_read16z(ZPD);
	EAW += Y;
}

/***************************************************************
 *  EA = indirect (only used by JMP)
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_ind()
{
	ea_abs();
	uint8_t tmp = program_read8(EAD);
	EAD++;
	EAH = program_read8(EAD);
	EAL = tmp;
}

/***************************************************************
 *  EA = indirect plus x (only used by JMP)
 ***************************************************************/
inline void MameDerivedH6280Adapter::ea_iax()
{
	ea_abs();
	EAD+=X;
	uint8_t tmp = program_read8(EAD);
	EAD++;
	EAH = program_read8(EAD);
	EAL = tmp;
}

inline uint8_t MameDerivedH6280Adapter::rd_imm()
{
	uint8_t tmp = read_opcode_arg();
	PCW++;
	return tmp;
}

inline uint8_t MameDerivedH6280Adapter::rd_zpg()
{
	ea_zpg();
	return program_read8z(EAD);
}

inline uint8_t MameDerivedH6280Adapter::rd_zpx()
{
	ea_zpx();
	return program_read8z(EAD);
}

inline uint8_t MameDerivedH6280Adapter::rd_zpy()
{
	ea_zpy();
	return program_read8z(EAD);
}

inline uint8_t MameDerivedH6280Adapter::rd_abs()
{
	ea_abs();
	return program_read8(EAD);
}

inline uint8_t MameDerivedH6280Adapter::rd_abx()
{
	ea_abx();
	return program_read8(EAD);
}

inline uint8_t MameDerivedH6280Adapter::rd_aby()
{
	ea_aby();
	return program_read8(EAD);
}

inline uint8_t MameDerivedH6280Adapter::rd_zpi()
{
	ea_zpi();
	return program_read8(EAD);
}

inline uint8_t MameDerivedH6280Adapter::rd_idx()
{
	ea_idx();
	return program_read8(EAD);
}

inline uint8_t MameDerivedH6280Adapter::rd_idy()
{
	ea_idy();
	return program_read8(EAD);
}

inline uint8_t MameDerivedH6280Adapter::rd_tfl()
{
	ea_tflg();
	return program_read8z(EAD);
}

inline void MameDerivedH6280Adapter::wr_zpg(uint8_t tmp)
{
	ea_zpg();
	wb_eaz(tmp);
}

inline void MameDerivedH6280Adapter::wr_zpx(uint8_t tmp)
{
	ea_zpx();
	wb_eaz(tmp);
}

inline void MameDerivedH6280Adapter::wr_zpy(uint8_t tmp)
{
	ea_zpy();
	wb_eaz(tmp);
}

inline void MameDerivedH6280Adapter::wr_abs(uint8_t tmp)
{
	ea_abs();
	wb_ea(tmp);
}

inline void MameDerivedH6280Adapter::wr_abx(uint8_t tmp)
{
	ea_abx();
	wb_ea(tmp);
}

inline void MameDerivedH6280Adapter::wr_aby(uint8_t tmp)
{
	ea_aby();
	wb_ea(tmp);
}

inline void MameDerivedH6280Adapter::wr_zpi(uint8_t tmp)
{
	ea_zpi();
	wb_ea(tmp);
}

inline void MameDerivedH6280Adapter::wr_idx(uint8_t tmp)
{
	ea_idx();
	wb_ea(tmp);
}

inline void MameDerivedH6280Adapter::wr_idy(uint8_t tmp)
{
	ea_idy();
	wb_ea(tmp);
}

inline void MameDerivedH6280Adapter::wb_ea(uint8_t tmp)
{
	program_write8(EAD, tmp);
}

inline void MameDerivedH6280Adapter::wb_eaz(uint8_t tmp)
{
	program_write8z(EAD, tmp);
}

/***************************************************************
 *
 * Macros to emulate the 6280 opcodes
 *
 ***************************************************************/

/***************************************************************
 * compose the real flag register by
 * including N and Z and set any
 * SET and clear any CLR bits also
 ***************************************************************/
#if H6280_LAZY_FLAGS

inline void MameDerivedH6280Adapter::compose_p(uint8_t SET, uint8_t CLR)
{
	P = (P & ~(_fN | _fZ | CLR)) |
		(NZ >> 8) |
		((NZ & 0xff) ? 0 : _fZ) |
		SET;
}

#else

inline void MameDerivedH6280Adapter::compose_p(uint8_t SET, uint8_t CLR)
{
	P = (P & ~CLR) | SET;
}

#endif

/* 6280 ********************************************************
 *  ADC Add with carry
 ***************************************************************/
inline void MameDerivedH6280Adapter::tadc(uint8_t tmp)
{
	clear_t();
	int tflagtemp = rd_tfl();
	if (P & _fD)
	{
		int c = (P & _fC);
		int lo = (tflagtemp & 0x0f) + (tmp & 0x0f) + c;
		int hi = (tflagtemp & 0xf0) + (tmp & 0xf0);
		P &= ~_fC;
		if (lo > 0x09)
		{
			hi += 0x10;
			lo += 0x06;
		}
		if (hi > 0x90)
			hi += 0x60;
		if (hi & 0xff00)
			P |= _fC;
		tflagtemp = (lo & 0x0f) + (hi & 0xf0);
		h6280_cycles(1);
	}
	else
	{
		int c = (P & _fC);
		int sum = tflagtemp + tmp + c;
		P &= ~(_fV | _fC);
		if (~(tflagtemp^tmp) & (tflagtemp^sum) & _fN)
			P |= _fV;
		if (sum & 0xff00)
			P |= _fC;
		tflagtemp = (uint8_t) sum;
	}
	set_nz(tflagtemp);
	wb_eaz(tflagtemp);
	h6280_cycles(3);
}


inline void MameDerivedH6280Adapter::adc(uint8_t tmp)
{
	if(P & _fT)
		tadc(tmp);
	else {
		if (P & _fD)
		{
			int c = (P & _fC);
			int lo = (A & 0x0f) + (tmp & 0x0f) + c;
			int hi = (A & 0xf0) + (tmp & 0xf0);
			P &= ~_fC;
			if (lo > 0x09)
			{
				hi += 0x10;
				lo += 0x06;
			}
			if (hi > 0x90)
				hi += 0x60;
			if (hi & 0xff00)
				P |= _fC;
			A = (lo & 0x0f) + (hi & 0xf0);
			h6280_cycles(1);
		}
		else
		{
			int c = (P & _fC);
			int sum = A + tmp + c;
			P &= ~(_fV | _fC);
			if (~(A^tmp) & (A^sum) & _fN)
				P |= _fV;
			if (sum & 0xff00)
				P |= _fC;
			A = (uint8_t) sum;
		}
		set_nz(A);
	}
}

/* 6280 ********************************************************
 *  AND Logical and
 ***************************************************************/
inline void MameDerivedH6280Adapter::tand(uint8_t tmp)
{
	clear_t();
	int tflagtemp = rd_tfl();
	tflagtemp = (uint8_t)(tflagtemp & tmp);
	wb_eaz(tflagtemp);
	set_nz(tflagtemp);
	h6280_cycles(3);
}

inline void MameDerivedH6280Adapter::and_a(uint8_t tmp)
{
	if(P & _fT)
		tand(tmp);
	else {
		A = (uint8_t)(A & tmp);
		set_nz(A);
	}
}

/* 6280 ********************************************************
 *  ASL Arithmetic shift left
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::asl(uint8_t tmp)
{
	clear_t();
	P = (P & ~_fC) | ((tmp >> 7) & _fC);
	tmp = (uint8_t)(tmp << 1);
	set_nz(tmp);
	return tmp;
}

/* 6280 ********************************************************
 *  BBR Branch if bit is reset
 ***************************************************************/
inline void MameDerivedH6280Adapter::bbr(int bit, uint8_t tmp)
{
	bra(!(tmp & (1<<bit)));
}

/* 6280 ********************************************************
 *  BBS Branch if bit is set
 ***************************************************************/
inline void MameDerivedH6280Adapter::bbs(int bit, uint8_t tmp)
{
	bra(tmp & (1<<bit));
}

/* 6280 ********************************************************
 *  BCC Branch if carry clear
 ***************************************************************/
inline void MameDerivedH6280Adapter::bcc()
{
	bra(!(P & _fC));
}

/* 6280 ********************************************************
 *  BCS Branch if carry set
 ***************************************************************/
inline void MameDerivedH6280Adapter::bcs()
{
	bra(P & _fC);
}

/* 6280 ********************************************************
 *  BEQ Branch if equal
 ***************************************************************/
inline void MameDerivedH6280Adapter::beq()
{
#if H6280_LAZY_FLAGS
	bra(!(NZ & 0xff));
#else
	bra(P & _fZ);
#endif
}

/* 6280 ********************************************************
 *  BIT Bit test
 ***************************************************************/
inline void MameDerivedH6280Adapter::bit(uint8_t tmp)
{
	P = (P & ~(_fN|_fV|_fT|_fZ))
		| ((tmp&0x80) ? _fN:0)
		| ((tmp&0x40) ? _fV:0)
		| ((tmp&A)  ? 0:_fZ);
}

/* 6280 ********************************************************
 *  BMI Branch if minus
 ***************************************************************/
inline void MameDerivedH6280Adapter::bmi()
{
#if H6280_LAZY_FLAGS
	bra(NZ & 0x8000);
#else
	bra(P & _fN);
#endif
}

/* 6280 ********************************************************
 *  BNE Branch if not equal
 ***************************************************************/
inline void MameDerivedH6280Adapter::bne()
{
#if H6280_LAZY_FLAGS
	bra(NZ & 0xff);
#else
	bra(!(P & _fZ));
#endif
}

/* 6280 ********************************************************
 *  BPL Branch if plus
 ***************************************************************/
inline void MameDerivedH6280Adapter::bpl()
{
#if H6280_LAZY_FLAGS
	bra(!(NZ & 0x8000));
#else
	bra(!(P & _fN));
#endif
}

/* 6280 ********************************************************
 *  BRK Break
 *  increment PC, push PC hi, PC lo, flags (with B bit set),
 *  set I flag, reset D flag and jump via IRQ vector
 ***************************************************************/
inline void MameDerivedH6280Adapter::brk()
{
	clear_t();
	PCW++;
	push(PCH);
	push(PCL);
	push(P);
	P = (P & ~_fD) | _fI;
	PCL = program_read8(H6280_IRQ2_VEC);
	PCH = program_read8(H6280_IRQ2_VEC+1);
}

/* 6280 ********************************************************
 *  BSR Branch to subroutine
 ***************************************************************/
inline void MameDerivedH6280Adapter::bsr()
{
	push(PCH);
	push(PCL);
	h6280_cycles(4); /* 4 cycles here, 4 in BRA */
	bra(1);
}

/* 6280 ********************************************************
 *  BVC Branch if overflow clear
 ***************************************************************/
inline void MameDerivedH6280Adapter::bvc()
{
	bra(!(P & _fV));
}

/* 6280 ********************************************************
 *  BVS Branch if overflow set
 ***************************************************************/
inline void MameDerivedH6280Adapter::bvs()
{
	bra(P & _fV);
}

/* 6280 ********************************************************
 *  CLA Clear accumulator
 ***************************************************************/
inline void MameDerivedH6280Adapter::cla()
{
	clear_t();
	A = 0;
}

/* 6280 ********************************************************
 *  CLC Clear carry flag
 ***************************************************************/
inline void MameDerivedH6280Adapter::clc()
{
	clear_t();
	P &= ~_fC;
}

/* 6280 ********************************************************
 *  CLD Clear decimal flag
 ***************************************************************/
inline void MameDerivedH6280Adapter::cld()
{
	clear_t();
	P &= ~_fD;
}

/* 6280 ********************************************************
 *  CLI Clear interrupt flag
 ***************************************************************/
inline void MameDerivedH6280Adapter::cli()
{
	clear_t();
	if( P & _fI )
	{
		P &= ~_fI;
		check_irq_lines();
	}
}


/* 6280 ********************************************************
 *  CLV Clear overflow flag
 ***************************************************************/
inline void MameDerivedH6280Adapter::clv()
{
	clear_t();
	P &= ~_fV;
}

/* 6280 ********************************************************
 *  CLX Clear index X
 ***************************************************************/
inline void MameDerivedH6280Adapter::clx()
{
	clear_t();
	X = 0;
}

/* 6280 ********************************************************
 *  CLY Clear index Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::cly()
{
	clear_t();
	Y = 0;
}

/* 6280 ********************************************************
 *  CMP Compare accumulator
 ***************************************************************/
inline void MameDerivedH6280Adapter::cmp(uint8_t tmp)
{
	clear_t();
	P &= ~_fC;
	if (A >= tmp)
		P |= _fC;
	set_nz((uint8_t)(A - tmp));
}

/* 6280 ********************************************************
 *  CPX Compare index X
 ***************************************************************/
inline void MameDerivedH6280Adapter::cpx(uint8_t tmp)
{
	clear_t();
	P &= ~_fC;
	if (X >= tmp)
		P |= _fC;
	set_nz((uint8_t)(X - tmp));
}

/* 6280 ********************************************************
 *  CPY Compare index Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::cpy(uint8_t tmp)
{
	clear_t();
	P &= ~_fC;
	if (Y >= tmp)
		P |= _fC;
	set_nz((uint8_t)(Y - tmp));
}

/* 6280 ********************************************************
 *  DEC Decrement memory
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::dec(uint8_t tmp)
{
	clear_t();
	tmp = (uint8_t)(tmp-1);
	set_nz(tmp);
	return tmp;
}

/* 6280 ********************************************************
 *  DEX Decrement index X
 ***************************************************************/
inline void MameDerivedH6280Adapter::dex()
{
	clear_t();
	X = (uint8_t)(X - 1);
	set_nz(X);
}

/* 6280 ********************************************************
 *  DEY Decrement index Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::dey()
{
	clear_t();
	Y = (uint8_t)(Y - 1);
	set_nz(Y);
}

/* 6280 ********************************************************
 *  EOR Logical exclusive or
 ***************************************************************/
inline void MameDerivedH6280Adapter::teor(uint8_t tmp)
{
	clear_t();
	int tflagtemp = rd_tfl();
	tflagtemp = (uint8_t)(tflagtemp ^ tmp);
	wb_eaz(tflagtemp);
	set_nz(tflagtemp);
	h6280_cycles(3);
}

inline void MameDerivedH6280Adapter::eor(uint8_t tmp)
{
	if(P & _fT)
		teor(tmp);
	else {
		A = (uint8_t)(A ^ tmp);
		set_nz(A);
	}
}

/* 6280 ********************************************************
 *  INC Increment memory
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::inc(uint8_t tmp)
{
	clear_t();
	tmp = (uint8_t)(tmp+1);
	set_nz(tmp);
	return tmp;
}

/* 6280 ********************************************************
 *  INX Increment index X
 ***************************************************************/
inline void MameDerivedH6280Adapter::inx()
{
	clear_t();
	X = (uint8_t)(X + 1);
	set_nz(X);
}

/* 6280 ********************************************************
 *  INY Increment index Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::iny()
{
	clear_t();
	Y = (uint8_t)(Y + 1);
	set_nz(Y);
}

/* 6280 ********************************************************
 *  JMP Jump to address
 *  set PC to the effective address
 ***************************************************************/
inline void MameDerivedH6280Adapter::jmp()
{
	clear_t();
	PCD = EAD;
}

/* 6280 ********************************************************
 *  JSR Jump to subroutine
 *  decrement PC (sic!) push PC hi, push PC lo and set
 *  PC to the effective address
 ***************************************************************/
inline void MameDerivedH6280Adapter::jsr()
{
	clear_t();
	PCW--;
	push(PCH);
	push(PCL);
	PCD = EAD;
}

/* 6280 ********************************************************
 *  LDA Load accumulator
 ***************************************************************/
inline void MameDerivedH6280Adapter::lda(uint8_t tmp)
{
	clear_t();
	A = (uint8_t)tmp;
	set_nz(A);
}

/* 6280 ********************************************************
 *  LDX Load index X
 ***************************************************************/
inline void MameDerivedH6280Adapter::ldx(uint8_t tmp)
{
	clear_t();
	X = (uint8_t)tmp;
	set_nz(X);
}

/* 6280 ********************************************************
 *  LDY Load index Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::ldy(uint8_t tmp)
{
	clear_t();
	Y = (uint8_t)tmp;
	set_nz(Y);
}

/* 6280 ********************************************************
 *  LSR Logic shift right
 *  0 -> [7][6][5][4][3][2][1][0] -> C
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::lsr(uint8_t tmp)
{
	clear_t();
	P = (P & ~_fC) | (tmp & _fC);
	tmp = (uint8_t)tmp >> 1;
	set_nz(tmp);
	return tmp;
}

/* 6280 ********************************************************
 *  NOP No operation
 ***************************************************************/
inline void MameDerivedH6280Adapter::nop()
{
	clear_t();
}

/* 6280 ********************************************************
 *  ORA Logical inclusive or
 ***************************************************************/

inline void MameDerivedH6280Adapter::tora(uint8_t tmp)
{
	clear_t();
	int tflagtemp = rd_tfl();
	tflagtemp = (uint8_t)(tflagtemp | tmp);
	wb_eaz(tflagtemp);
	set_nz(tflagtemp);
	h6280_cycles(3);
}

inline void MameDerivedH6280Adapter::ora(uint8_t tmp)
{
	if(P & _fT)
		tora(tmp);
	else {
		A = (uint8_t)(A | tmp);
		set_nz(A);
	}
}

/* 6280 ********************************************************
 *  PHA Push accumulator
 ***************************************************************/
inline void MameDerivedH6280Adapter::pha()
{
	clear_t();
	push(A);
}

/* 6280 ********************************************************
 *  PHP Push processor status (flags)
 ***************************************************************/
inline void MameDerivedH6280Adapter::php()
{
	clear_t();
	compose_p(0,0);
	push(P);
}

/* 6280 ********************************************************
 *  PHX Push index X
 ***************************************************************/
inline void MameDerivedH6280Adapter::phx()
{
	clear_t();
	push(X);
}

/* 6280 ********************************************************
 *  PHY Push index Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::phy()
{
	clear_t();
	push(Y);
}

/* 6280 ********************************************************
 *  PLA Pull accumulator
 ***************************************************************/
inline void MameDerivedH6280Adapter::pla()
{
	clear_t();
	pull(A);
	set_nz(A);
}

/* 6280 ********************************************************
 *  PLP Pull processor status (flags)
 ***************************************************************/
inline void MameDerivedH6280Adapter::plp()
{
#if H6280_LAZY_FLAGS
	pull(P);
	P |= _fB;
	NZ = ((P & _fN) << 8) |
			((P & _fZ) ^ _fZ);
	check_irq_lines();
#else
	pull(P);
	P |= _fB;
	check_irq_lines();
#endif
}

/* 6280 ********************************************************
 *  PLX Pull index X
 ***************************************************************/
inline void MameDerivedH6280Adapter::plx()
{
	clear_t();
	pull(X);
	set_nz(X);
}

/* 6280 ********************************************************
 *  PLY Pull index Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::ply()
{
	clear_t();
	pull(Y);
	set_nz(Y);
}

/* 6280 ********************************************************
 *  RMB Reset memory bit
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::rmb(int bit, uint8_t tmp)
{
	clear_t();
	tmp &= ~(1<<bit);
	return tmp;
}

/* 6280 ********************************************************
 *  ROL Rotate left
 *  new C <- [7][6][5][4][3][2][1][0] <- C
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::rol(uint8_t tmp)
{
	clear_t();
	int tmp9 = (tmp << 1) | (P & _fC);
	P = (P & ~_fC) | ((tmp9 >> 8) & _fC);
	tmp = (uint8_t)tmp9;
	set_nz(tmp);
	return tmp;
}

/* 6280 ********************************************************
 *  ROR Rotate right
 *  C -> [7][6][5][4][3][2][1][0] -> new C
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::ror(uint8_t tmp)
{
	clear_t();
	int tmp9 = tmp | (P & _fC) << 8;
	P = (P & ~_fC) | (tmp & _fC);
	tmp = (uint8_t)(tmp9 >> 1);
	set_nz(tmp);
	return tmp;
}

/* 6280 ********************************************************
 *  RTI Return from interrupt
 *  pull flags, pull PC lo, pull PC hi and increment PC
 ***************************************************************/
inline void MameDerivedH6280Adapter::rti()
{
#if H6280_LAZY_FLAGS
	pull(P);
	P |= _fB;
	NZ = ((P & _fN) << 8) |
			((P & _fZ) ^ _fZ);
	pull(PCL);
	pull(PCH);
	check_irq_lines();
#else
	pull(P);
	P |= _fB;
	pull(PCL);
	pull(PCH);
	check_irq_lines();
#endif
}

/* 6280 ********************************************************
 *  RTS Return from subroutine
 *  pull PC lo, PC hi and increment PC
 ***************************************************************/
inline void MameDerivedH6280Adapter::rts()
{
	clear_t();
	pull(PCL);
	pull(PCH);
	PCW++;
}

/* 6280 ********************************************************
 *  SAX Swap accumulator and index X
 ***************************************************************/
inline void MameDerivedH6280Adapter::sax()
{
	clear_t();
	uint8_t tmp = X;
	X = A;
	A = tmp;
}

/* 6280 ********************************************************
 *  SAY Swap accumulator and index Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::say()
{
	clear_t();
	uint8_t tmp = Y;
	Y = A;
	A = tmp;
}

/* 6280 ********************************************************
 *  SBC Subtract with carry
 ***************************************************************/
inline void MameDerivedH6280Adapter::tsbc(uint8_t tmp)
{
	clear_t();
	int tflagtemp = rd_tfl();
	if (P & _fD)
	{
		int c = (P & _fC) ^ _fC;
		int sum = tflagtemp - tmp -c;
		int lo = (tflagtemp & 0x0f) - (tmp & 0x0f) - c;
		int hi = (tflagtemp & 0xf0) - (tmp & 0xf0);
		P &= ~_fC;
		if (lo & 0xf0)
			lo -= 6;
		if (lo & 0x80)
			hi -= 0x10;
		if (hi & 0x0f00)
			hi -= 0x60;
		if ((sum & 0xff00) == 0)
			P |= _fC;
		tflagtemp = (lo & 0x0f) + (hi & 0xf0);
		h6280_cycles(1);
	}
	else
	{
		int c = (P & _fC) ^ _fC;
		int sum = tflagtemp - tmp - c;
		P &= ~(_fV | _fC);
		if ((tflagtemp^tmp) & (tflagtemp^sum) & _fN)
			P |= _fV;
		if ((sum & 0xff00) == 0)
			P |= _fC;
		tflagtemp = (uint8_t) sum;
	}
	set_nz(tflagtemp);
	wb_eaz(tflagtemp);
	h6280_cycles(3);
}

inline void MameDerivedH6280Adapter::sbc(uint8_t tmp)
{
	if(P & _fT)
		tsbc(tmp);
	else {
		if (P & _fD)
		{
			int c = (P & _fC) ^ _fC;
			int sum = A - tmp - c;
			int lo = (A & 0x0f) - (tmp & 0x0f) - c;
			int hi = (A & 0xf0) - (tmp & 0xf0);
			P &= ~_fC;
			if (lo & 0xf0)
				lo -= 6;
			if (lo & 0x80)
				hi -= 0x10;
			if (hi & 0x0f00)
				hi -= 0x60;
			if ((sum & 0xff00) == 0)
				P |= _fC;
			A = (lo & 0x0f) + (hi & 0xf0);
			h6280_cycles(1);
		}
		else
		{
			int c = (P & _fC) ^ _fC;
			int sum = A - tmp - c;
			P &= ~(_fV | _fC);
			if ((A^tmp) & (A^sum) & _fN)
				P |= _fV;
			if ((sum & 0xff00) == 0)
				P |= _fC;
			A = (uint8_t) sum;
		}
		set_nz(A);
	}
}

/* 6280 ********************************************************
 *  SEC Set carry flag
 ***************************************************************/
inline void MameDerivedH6280Adapter::sec()
{
	clear_t();
	P |= _fC;
}

/* 6280 ********************************************************
 *  SED Set decimal flag
 ***************************************************************/
inline void MameDerivedH6280Adapter::sed()
{
	clear_t();
	P |= _fD;
}

/* 6280 ********************************************************
 *  SEI Set interrupt flag
 ***************************************************************/
inline void MameDerivedH6280Adapter::sei()
{
	clear_t();
	P |= _fI;
}

/* 6280 ********************************************************
 *  SET Set t flag
 ***************************************************************/
inline void MameDerivedH6280Adapter::set()
{
	P |= _fT;
}

/* 6280 ********************************************************
 *  SMB Set memory bit
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::smb(int bit, uint8_t tmp)
{
	clear_t();
	tmp |= (1<<bit);
	return tmp;
}

/* 6280 ********************************************************
 *  ST0 Store at hardware address 0
 ***************************************************************/
inline void MameDerivedH6280Adapter::st0(uint8_t tmp)
{
	clear_t();
	(void)tmp; // ST0/ST1/ST2 target the VDC (AS_IO) - no VDC on this board
}

/* 6280 ********************************************************
 *  ST1 Store at hardware address 2
 ***************************************************************/
inline void MameDerivedH6280Adapter::st1(uint8_t tmp)
{
	clear_t();
	(void)tmp; // ST0/ST1/ST2 target the VDC (AS_IO) - no VDC on this board
}

/* 6280 ********************************************************
 *  ST2 Store at hardware address 3
 ***************************************************************/
inline void MameDerivedH6280Adapter::st2(uint8_t tmp)
{
	clear_t();
	(void)tmp; // ST0/ST1/ST2 target the VDC (AS_IO) - no VDC on this board
}

/* 6280 ********************************************************
 *  STA Store accumulator
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::sta()
{
	clear_t();
	return A;
}

/* 6280 ********************************************************
 *  STX Store index X
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::stx()
{
	clear_t();
	return X;
}

/* 6280 ********************************************************
 *  STY Store index Y
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::sty()
{
	clear_t();
	return Y;
}

/* 6280 ********************************************************
 * STZ  Store zero
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::stz()
{
	clear_t();
	return 0;
}

/* H6280 *******************************************************
 *  SXY Swap index X and index Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::sxy()
{
	clear_t();
	uint8_t tmp = X;
	X = Y;
	Y = tmp;
}

/* H6280 *******************************************************
 *  TAI Transfer Alternate Increment
 ***************************************************************/
inline void MameDerivedH6280Adapter::tai()
{
	clear_t();
	int from = program_read16(PCW);
	int to = program_read16(PCW + 2);
	int length = program_read16(PCW + 4);
	PCW += 6;
	int alternate = 0;
	if (!length) length = 0x10000;
	h6280_cycles( ((6 * length) + 17) );
	while ((length--) != 0)
	{
		program_write8(to, program_read8(from + alternate));
		to++;
		alternate ^= 1;
	}
}

/* H6280 *******************************************************
 *  TAM Transfer accumulator to memory mapper register(s)
 ***************************************************************/
inline void MameDerivedH6280Adapter::tam(uint8_t tmp)
{
	clear_t();
	if (tmp&0x01) m_mmr[0] = A;
	if (tmp&0x02) m_mmr[1] = A;
	if (tmp&0x04) m_mmr[2] = A;
	if (tmp&0x08) m_mmr[3] = A;
	if (tmp&0x10) m_mmr[4] = A;
	if (tmp&0x20) m_mmr[5] = A;
	if (tmp&0x40) m_mmr[6] = A;
	if (tmp&0x80) m_mmr[7] = A;
}

/* 6280 ********************************************************
 *  TAX Transfer accumulator to index X
 ***************************************************************/
inline void MameDerivedH6280Adapter::tax()
{
	clear_t();
	X = A;
	set_nz(X);
}

/* 6280 ********************************************************
 *  TAY Transfer accumulator to index Y
 ***************************************************************/
inline void MameDerivedH6280Adapter::tay()
{
	clear_t();
	Y = A;
	set_nz(Y);
}

/* 6280 ********************************************************
 *  TDD Transfer Decrement Decrement
 ***************************************************************/
inline void MameDerivedH6280Adapter::tdd()
{
	clear_t();
	int from = program_read16(PCW);
	int to = program_read16(PCW + 2);
	int length = program_read16(PCW + 4);
	PCW+=6;
	if (!length) length = 0x10000;
	h6280_cycles( ((6 * length) + 17) );
	while ((length--) != 0) {
		program_write8(to, program_read8(from));
		to--;
		from--;
	}
}

/* 6280 ********************************************************
 *  TIA Transfer Increment Alternate
 ***************************************************************/
inline void MameDerivedH6280Adapter::tia()
{
	clear_t();
	int from = program_read16(PCW);
	int to  = program_read16(PCW + 2);
	int length = program_read16(PCW + 4);
	PCW+=6;
	int alternate=0;
	if (!length) length = 0x10000;
	h6280_cycles( ((6 * length) + 17) );
	while ((length--) != 0) {
		program_write8(to + alternate, program_read8(from));
		from++;
		alternate ^= 1;
	}
}

/* 6280 ********************************************************
 *  TII Transfer Increment Increment
 ***************************************************************/
inline void MameDerivedH6280Adapter::tii()
{
	clear_t();
	int from = program_read16(PCW);
	int to = program_read16(PCW + 2);
	int length = program_read16(PCW + 4);
	PCW += 6;
	if (!length) length = 0x10000;
	h6280_cycles( ((6 * length) + 17) );
	while ((length--) != 0) {
		program_write8(to, program_read8(from));
		to++;
		from++;
	}
}

/* 6280 ********************************************************
 *  TIN Transfer block, source increments every loop
 ***************************************************************/
inline void MameDerivedH6280Adapter::tin()
{
	clear_t();
	int from = program_read16(PCW);
	int to = program_read16(PCW + 2);
	int length = program_read16(PCW + 4);
	PCW+=6;
	if (!length) length = 0x10000;
	h6280_cycles( ((6 * length) + 17) );
	while ((length--) != 0) {
		program_write8(to, program_read8(from));
		from++;
	}
}

/* 6280 ********************************************************
 *  TMA Transfer memory mapper register(s) to accumulator
 *  the highest bit set in tmp is the one that counts
 ***************************************************************/
inline void MameDerivedH6280Adapter::tma(uint8_t tmp)
{
	clear_t();
	if (tmp&0x01) A = m_mmr[0];
	if (tmp&0x02) A = m_mmr[1];
	if (tmp&0x04) A = m_mmr[2];
	if (tmp&0x08) A = m_mmr[3];
	if (tmp&0x10) A = m_mmr[4];
	if (tmp&0x20) A = m_mmr[5];
	if (tmp&0x40) A = m_mmr[6];
	if (tmp&0x80) A = m_mmr[7];
}

/* 6280 ********************************************************
 * TRB  Test and reset bits
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::trb(uint8_t tmp)
{
	clear_t();
	P = (P & ~(_fN|_fV|_fT|_fZ))
		| ((tmp&0x80) ? _fN:0)
		| ((tmp&0x40) ? _fV:0)
		| ((tmp&~A)  ? 0:_fZ);
	tmp &= ~A;
	return tmp;
}

/* 6280 ********************************************************
 * TSB  Test and set bits
 ***************************************************************/
inline uint8_t MameDerivedH6280Adapter::tsb(uint8_t tmp)
{
	clear_t();
	P = (P & ~(_fN|_fV|_fT|_fZ))
		| ((tmp&0x80) ? _fN:0)
		| ((tmp&0x40) ? _fV:0)
		| ((tmp|A)  ? 0:_fZ);
	tmp |= A;
	return tmp;
}

/* 6280 ********************************************************
 *  TSX Transfer stack LSB to index X
 ***************************************************************/
inline void MameDerivedH6280Adapter::tsx()
{
	clear_t();
	X = S;
	set_nz(X);
}

/* 6280 ********************************************************
 *  TST
 ***************************************************************/
inline void MameDerivedH6280Adapter::tst(uint8_t imm, uint8_t tmp)
{
	P = (P & ~(_fN|_fV|_fT|_fZ))
		| ((tmp&0x80) ? _fN:0)
		| ((tmp&0x40) ? _fV:0)
		| ((tmp&imm)  ? 0:_fZ);
}

/* 6280 ********************************************************
 *  TXA Transfer index X to accumulator
 ***************************************************************/
inline void MameDerivedH6280Adapter::txa()
{
	clear_t();
	A = X;
	set_nz(A);
}

/* 6280 ********************************************************
 *  TXS Transfer index X to stack LSB
 *  no flags changed (sic!)
 ***************************************************************/
inline void MameDerivedH6280Adapter::txs()
{
	clear_t();
	S = X;
}

/* 6280 ********************************************************
 *  TYA Transfer index Y to accumulator
 ***************************************************************/
inline void MameDerivedH6280Adapter::tya()
{
	clear_t();
	A = Y;
	set_nz(A);
}

/* 6280 ********************************************************
 * CSH Set CPU in high speed mode
 ***************************************************************/
inline void MameDerivedH6280Adapter::csh()
{
	m_clocks_per_cycle = 1;
}

/* 6280 ********************************************************
 * CSL Set CPU in low speed mode
 ***************************************************************/
inline void MameDerivedH6280Adapter::csl()
{
	m_clocks_per_cycle = 4;
}


#define OP(prefix,opcode)  void MameDerivedH6280Adapter::prefix##_##opcode()

/*****************************************************************************
 *****************************************************************************
 *
 *   Hu6280 opcodes
 *
 *****************************************************************************
 *    op    cycles           opc                       ***********************/
OP(op,00) { h6280_cycles(8); brk();                    } // 8 BRK
OP(op,20) { h6280_cycles(7); ea_abs(); jsr();          } // 7 JSR  ABS
OP(op,40) { h6280_cycles(7); rti();                    } // 7 RTI
OP(op,60) { h6280_cycles(7); rts();                    } // 7 RTS
OP(op,80) {                  bra(1);                   } // 4 BRA  REL
OP(op,a0) { h6280_cycles(2); ldy(rd_imm());            } // 2 LDY  IMM
OP(op,c0) { h6280_cycles(2); cpy(rd_imm());            } // 2 CPY  IMM
OP(op,e0) { h6280_cycles(2); cpx(rd_imm());            } // 2 CPX  IMM

OP(op,10) {                  bpl();                    } // 2/4 BPL  REL
OP(op,30) {                  bmi();                    } // 2/4 BMI  REL
OP(op,50) {                  bvc();                    } // 2/4 BVC  REL
OP(op,70) {                  bvs();                    } // 2/4 BVS  REL
OP(op,90) {                  bcc();                    } // 2/4 BCC  REL
OP(op,b0) {                  bcs();                    } // 2/4 BCS  REL
OP(op,d0) {                  bne();                    } // 2/4 BNE  REL
OP(op,f0) {                  beq();                    } // 2/4 BEQ  REL

OP(op,01) { h6280_cycles(7); ora(rd_idx());            } // 7 ORA  IDX
OP(op,21) { h6280_cycles(7); and_a(rd_idx());          } // 7 AND  IDX
OP(op,41) { h6280_cycles(7); eor(rd_idx());            } // 7 EOR  IDX
OP(op,61) { h6280_cycles(7); adc(rd_idx());            } // 7 ADC  IDX
OP(op,81) { h6280_cycles(7); wr_idx(sta());            } // 7 STA  IDX
OP(op,a1) { h6280_cycles(7); lda(rd_idx());            } // 7 LDA  IDX
OP(op,c1) { h6280_cycles(7); cmp(rd_idx());            } // 7 CMP  IDX
OP(op,e1) { h6280_cycles(7); sbc(rd_idx());            } // 7 SBC  IDX

OP(op,11) { h6280_cycles(7); ora(rd_idy());            } // 7 ORA  IDY
OP(op,31) { h6280_cycles(7); and_a(rd_idy());          } // 7 AND  IDY
OP(op,51) { h6280_cycles(7); eor(rd_idy());            } // 7 EOR  IDY
OP(op,71) { h6280_cycles(7); adc(rd_idy());            } // 7 ADC  AZP
OP(op,91) { h6280_cycles(7); wr_idy(sta());            } // 7 STA  IDY
OP(op,b1) { h6280_cycles(7); lda(rd_idy());            } // 7 LDA  IDY
OP(op,d1) { h6280_cycles(7); cmp(rd_idy());            } // 7 CMP  IDY
OP(op,f1) { h6280_cycles(7); sbc(rd_idy());            } // 7 SBC  IDY

OP(op,02) { h6280_cycles(3); sxy();                    } // 3 SXY
OP(op,22) { h6280_cycles(3); sax();                    } // 3 SAX
OP(op,42) { h6280_cycles(3); say();                    } // 3 SAY
OP(op,62) { h6280_cycles(2); cla();                    } // 2 CLA
OP(op,82) { h6280_cycles(2); clx();                    } // 2 CLX
OP(op,a2) { h6280_cycles(2); ldx(rd_imm());            } // 2 LDX  IMM
OP(op,c2) { h6280_cycles(2); cly();                    } // 2 CLY
OP(op,e2) { h6280_cycles(2); nop();                    } // 2 NOP

OP(op,12) { h6280_cycles(7); ora(rd_zpi());            } // 7 ORA  ZPI
OP(op,32) { h6280_cycles(7); and_a(rd_zpi());          } // 7 AND  ZPI
OP(op,52) { h6280_cycles(7); eor(rd_zpi());            } // 7 EOR  ZPI
OP(op,72) { h6280_cycles(7); adc(rd_zpi());            } // 7 ADC  ZPI
OP(op,92) { h6280_cycles(7); wr_zpi(sta());            } // 7 STA  ZPI
OP(op,b2) { h6280_cycles(7); lda(rd_zpi());            } // 7 LDA  ZPI
OP(op,d2) { h6280_cycles(7); cmp(rd_zpi());            } // 7 CMP  ZPI
OP(op,f2) { h6280_cycles(7); sbc(rd_zpi());            } // 7 SBC  ZPI

OP(op,03) { h6280_cycles(5); st0(rd_imm());            } // 4 + 1 penalty cycle ST0  IMM
OP(op,23) { h6280_cycles(5); st2(rd_imm());            } // 4 + 1 penalty cycle ST2  IMM
OP(op,43) { h6280_cycles(4); tma(rd_imm());            } // 4 TMA
OP(op,63) { h6280_cycles(4); nop();                    } // 2 NOP
OP(op,83) { h6280_cycles(7); int imm = rd_imm(); tst(imm, rd_zpg()); } // 7 TST  IMM,ZPG
OP(op,a3) { h6280_cycles(7); int imm = rd_imm(); tst(imm, rd_zpx()); } // 7 TST  IMM,ZPX
OP(op,c3) {                  tdd();                    } // 6*l+17 TDD  XFER
OP(op,e3) {                  tia();                    } // 6*l+17 TIA  XFER

OP(op,13) { h6280_cycles(5); st1(rd_imm());            } // 4 + 1 penalty cycle ST1
OP(op,33) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,53) { h6280_cycles(5); tam(rd_imm());            } // 5 TAM  IMM
OP(op,73) {                  tii();                    } // 6*l+17 TII  XFER
OP(op,93) { h6280_cycles(8); int imm = rd_imm(); tst(imm, rd_abs()); } // 8 TST  IMM,ABS
OP(op,b3) { h6280_cycles(8); int imm = rd_imm(); tst(imm, rd_abx()); } // 8 TST  IMM,ABX
OP(op,d3) {                  tin();                    } // 6*l+17 TIN  XFER
OP(op,f3) {                  tai();                    } // 6*l+17 TAI  XFER

OP(op,04) { h6280_cycles(6); wb_eaz(tsb(rd_zpg()));    } // 6 TSB  ZPG
OP(op,24) { h6280_cycles(4); bit(rd_zpg());            } // 4 BIT  ZPG
OP(op,44) {                  bsr();                    } // 8 BSR  REL
OP(op,64) { h6280_cycles(4); wr_zpg(stz());            } // 4 STZ  ZPG
OP(op,84) { h6280_cycles(4); wr_zpg(sty());            } // 4 STY  ZPG
OP(op,a4) { h6280_cycles(4); ldy(rd_zpg());            } // 4 LDY  ZPG
OP(op,c4) { h6280_cycles(4); cpy(rd_zpg());            } // 4 CPY  ZPG
OP(op,e4) { h6280_cycles(4); cpx(rd_zpg());            } // 4 CPX  ZPG

OP(op,14) { h6280_cycles(6); wb_eaz(trb(rd_zpg()));    } // 6 TRB  ZPG
OP(op,34) { h6280_cycles(4); bit(rd_zpx());            } // 4 BIT  ZPX
OP(op,54) { h6280_cycles(3); csl();                    } // 3 CSL
OP(op,74) { h6280_cycles(4); wr_zpx(stz());            } // 4 STZ  ZPX
OP(op,94) { h6280_cycles(4); wr_zpx(sty());            } // 4 STY  ZPX
OP(op,b4) { h6280_cycles(4); ldy(rd_zpx());            } // 4 LDY  ZPX
OP(op,d4) { h6280_cycles(3); csh();                    } // 3 CSH
OP(op,f4) { h6280_cycles(2); set();                    } // 2 SET

OP(op,05) { h6280_cycles(4); ora(rd_zpg());            } // 4 ORA  ZPG
OP(op,25) { h6280_cycles(4); and_a(rd_zpg());          } // 4 AND  ZPG
OP(op,45) { h6280_cycles(4); eor(rd_zpg());            } // 4 EOR  ZPG
OP(op,65) { h6280_cycles(4); adc(rd_zpg());            } // 4 ADC  ZPG
OP(op,85) { h6280_cycles(4); wr_zpg(sta());            } // 4 STA  ZPG
OP(op,a5) { h6280_cycles(4); lda(rd_zpg());            } // 4 LDA  ZPG
OP(op,c5) { h6280_cycles(4); cmp(rd_zpg());            } // 4 CMP  ZPG
OP(op,e5) { h6280_cycles(4); sbc(rd_zpg());            } // 4 SBC  ZPG

OP(op,15) { h6280_cycles(4); ora(rd_zpx());            } // 4 ORA  ZPX
OP(op,35) { h6280_cycles(4); and_a(rd_zpx());          } // 4 AND  ZPX
OP(op,55) { h6280_cycles(4); eor(rd_zpx());            } // 4 EOR  ZPX
OP(op,75) { h6280_cycles(4); adc(rd_zpx());            } // 4 ADC  ZPX
OP(op,95) { h6280_cycles(4); wr_zpx(sta());            } // 4 STA  ZPX
OP(op,b5) { h6280_cycles(4); lda(rd_zpx());            } // 4 LDA  ZPX
OP(op,d5) { h6280_cycles(4); cmp(rd_zpx());            } // 4 CMP  ZPX
OP(op,f5) { h6280_cycles(4); sbc(rd_zpx());            } // 4 SBC  ZPX

OP(op,06) { h6280_cycles(6); wb_eaz(asl(rd_zpg()));    } // 6 ASL  ZPG
OP(op,26) { h6280_cycles(6); wb_eaz(rol(rd_zpg()));    } // 6 ROL  ZPG
OP(op,46) { h6280_cycles(6); wb_eaz(lsr(rd_zpg()));    } // 6 LSR  ZPG
OP(op,66) { h6280_cycles(6); wb_eaz(ror(rd_zpg()));    } // 6 ROR  ZPG
OP(op,86) { h6280_cycles(4); wr_zpg(stx());            } // 4 STX  ZPG
OP(op,a6) { h6280_cycles(4); ldx(rd_zpg());            } // 4 LDX  ZPG
OP(op,c6) { h6280_cycles(6); wb_eaz(dec(rd_zpg()));    } // 6 DEC  ZPG
OP(op,e6) { h6280_cycles(6); wb_eaz(inc(rd_zpg()));    } // 6 INC  ZPG

OP(op,16) { h6280_cycles(6); wb_eaz(asl(rd_zpx()));    } // 6 ASL  ZPX
OP(op,36) { h6280_cycles(6); wb_eaz(rol(rd_zpx()));    } // 6 ROL  ZPX
OP(op,56) { h6280_cycles(6); wb_eaz(lsr(rd_zpx()));    } // 6 LSR  ZPX
OP(op,76) { h6280_cycles(6); wb_eaz(ror(rd_zpx()));    } // 6 ROR  ZPX
OP(op,96) { h6280_cycles(4); wr_zpy(stx());            } // 4 STX  ZPY
OP(op,b6) { h6280_cycles(4); ldx(rd_zpy());            } // 4 LDX  ZPY
OP(op,d6) { h6280_cycles(6); wb_eaz(dec(rd_zpx()));    } // 6 DEC  ZPX
OP(op,f6) { h6280_cycles(6); wb_eaz(inc(rd_zpx()));    } // 6 INC  ZPX

OP(op,07) { h6280_cycles(7); wb_eaz(rmb(0, rd_zpg())); } // 7 RMB0 ZPG
OP(op,27) { h6280_cycles(7); wb_eaz(rmb(2, rd_zpg())); } // 7 RMB2 ZPG
OP(op,47) { h6280_cycles(7); wb_eaz(rmb(4, rd_zpg())); } // 7 RMB4 ZPG
OP(op,67) { h6280_cycles(7); wb_eaz(rmb(6, rd_zpg())); } // 7 RMB6 ZPG
OP(op,87) { h6280_cycles(7); wb_eaz(smb(0, rd_zpg())); } // 7 SMB0 ZPG
OP(op,a7) { h6280_cycles(7); wb_eaz(smb(2, rd_zpg())); } // 7 SMB2 ZPG
OP(op,c7) { h6280_cycles(7); wb_eaz(smb(4, rd_zpg())); } // 7 SMB4 ZPG
OP(op,e7) { h6280_cycles(7); wb_eaz(smb(6, rd_zpg())); } // 7 SMB6 ZPG

OP(op,17) { h6280_cycles(7); wb_eaz(rmb(1, rd_zpg())); } // 7 RMB1 ZPG
OP(op,37) { h6280_cycles(7); wb_eaz(rmb(3, rd_zpg())); } // 7 RMB3 ZPG
OP(op,57) { h6280_cycles(7); wb_eaz(rmb(5, rd_zpg())); } // 7 RMB5 ZPG
OP(op,77) { h6280_cycles(7); wb_eaz(rmb(7, rd_zpg())); } // 7 RMB7 ZPG
OP(op,97) { h6280_cycles(7); wb_eaz(smb(1, rd_zpg())); } // 7 SMB1 ZPG
OP(op,b7) { h6280_cycles(7); wb_eaz(smb(3, rd_zpg())); } // 7 SMB3 ZPG
OP(op,d7) { h6280_cycles(7); wb_eaz(smb(5, rd_zpg())); } // 7 SMB5 ZPG
OP(op,f7) { h6280_cycles(7); wb_eaz(smb(7, rd_zpg())); } // 7 SMB7 ZPG

OP(op,08) { h6280_cycles(3); php();                    } // 3 PHP
OP(op,28) { h6280_cycles(4); plp();                    } // 4 PLP
OP(op,48) { h6280_cycles(3); pha();                    } // 3 PHA
OP(op,68) { h6280_cycles(4); pla();                    } // 4 PLA
OP(op,88) { h6280_cycles(2); dey();                    } // 2 DEY
OP(op,a8) { h6280_cycles(2); tay();                    } // 2 TAY
OP(op,c8) { h6280_cycles(2); iny();                    } // 2 INY
OP(op,e8) { h6280_cycles(2); inx();                    } // 2 INX

OP(op,18) { h6280_cycles(2); clc();                    } // 2 CLC
OP(op,38) { h6280_cycles(2); sec();                    } // 2 SEC
OP(op,58) { h6280_cycles(2); cli();                    } // 2 CLI
OP(op,78) { h6280_cycles(2); sei();                    } // 2 SEI
OP(op,98) { h6280_cycles(2); tya();                    } // 2 TYA
OP(op,b8) { h6280_cycles(2); clv();                    } // 2 CLV
OP(op,d8) { h6280_cycles(2); cld();                    } // 2 CLD
OP(op,f8) { h6280_cycles(2); sed();                    } // 2 SED

OP(op,09) { h6280_cycles(2); ora(rd_imm());            } // 2 ORA  IMM
OP(op,29) { h6280_cycles(2); and_a(rd_imm());          } // 2 AND  IMM
OP(op,49) { h6280_cycles(2); eor(rd_imm());            } // 2 EOR  IMM
OP(op,69) { h6280_cycles(2); adc(rd_imm());            } // 2 ADC  IMM
OP(op,89) { h6280_cycles(2); bit(rd_imm());            } // 2 BIT  IMM
OP(op,a9) { h6280_cycles(2); lda(rd_imm());            } // 2 LDA  IMM
OP(op,c9) { h6280_cycles(2); cmp(rd_imm());            } // 2 CMP  IMM
OP(op,e9) { h6280_cycles(2); sbc(rd_imm());            } // 2 SBC  IMM

OP(op,19) { h6280_cycles(5); ora(rd_aby());            } // 5 ORA  ABY
OP(op,39) { h6280_cycles(5); and_a(rd_aby());          } // 5 AND  ABY
OP(op,59) { h6280_cycles(5); eor(rd_aby());            } // 5 EOR  ABY
OP(op,79) { h6280_cycles(5); adc(rd_aby());            } // 5 ADC  ABY
OP(op,99) { h6280_cycles(5); wr_aby(sta());            } // 5 STA  ABY
OP(op,b9) { h6280_cycles(5); lda(rd_aby());            } // 5 LDA  ABY
OP(op,d9) { h6280_cycles(5); cmp(rd_aby());            } // 5 CMP  ABY
OP(op,f9) { h6280_cycles(5); sbc(rd_aby());            } // 5 SBC  ABY

OP(op,0a) { h6280_cycles(2); A = asl(A);               } // 2 ASL  A
OP(op,2a) { h6280_cycles(2); A = rol(A);               } // 2 ROL  A
OP(op,4a) { h6280_cycles(2); A = lsr(A);               } // 2 LSR  A
OP(op,6a) { h6280_cycles(2); A = ror(A);               } // 2 ROR  A
OP(op,8a) { h6280_cycles(2); txa();                    } // 2 TXA
OP(op,aa) { h6280_cycles(2); tax();                    } // 2 TAX
OP(op,ca) { h6280_cycles(2); dex();                    } // 2 DEX
OP(op,ea) { h6280_cycles(2); nop();                    } // 2 NOP

OP(op,1a) { h6280_cycles(2); A = inc(A);               } // 2 INC  A
OP(op,3a) { h6280_cycles(2); A = dec(A);               } // 2 DEC  A
OP(op,5a) { h6280_cycles(3); phy();                    } // 3 PHY
OP(op,7a) { h6280_cycles(4); ply();                    } // 4 PLY
OP(op,9a) { h6280_cycles(2); txs();                    } // 2 TXS
OP(op,ba) { h6280_cycles(2); tsx();                    } // 2 TSX
OP(op,da) { h6280_cycles(3); phx();                    } // 3 PHX
OP(op,fa) { h6280_cycles(4); plx();                    } // 4 PLX

OP(op,0b) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,2b) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,4b) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,6b) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,8b) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,ab) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,cb) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,eb) { h6280_cycles(2); nop();                    } // 2 NOP

OP(op,1b) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,3b) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,5b) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,7b) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,9b) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,bb) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,db) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,fb) { h6280_cycles(2); nop();                    } // 2 NOP

OP(op,0c) { h6280_cycles(7); wb_ea(tsb(rd_abs()));     } // 7 TSB  ABS
OP(op,2c) { h6280_cycles(5); bit(rd_abs());            } // 5 BIT  ABS
OP(op,4c) { h6280_cycles(4); ea_abs(); jmp();          } // 4 JMP  ABS
OP(op,6c) { h6280_cycles(7); ea_ind(); jmp();          } // 7 JMP  IND
OP(op,8c) { h6280_cycles(5); wr_abs(sty());            } // 5 STY  ABS
OP(op,ac) { h6280_cycles(5); ldy(rd_abs());            } // 5 LDY  ABS
OP(op,cc) { h6280_cycles(5); cpy(rd_abs());            } // 5 CPY  ABS
OP(op,ec) { h6280_cycles(5); cpx(rd_abs());            } // 5 CPX  ABS

OP(op,1c) { h6280_cycles(7); wb_ea(trb(rd_abs()));     } // 7 TRB  ABS
OP(op,3c) { h6280_cycles(5); bit(rd_abx());            } // 5 BIT  ABX
OP(op,5c) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,7c) { h6280_cycles(7); ea_iax(); jmp();          } // 7 JMP  IAX
OP(op,9c) { h6280_cycles(5); wr_abs(stz());            } // 5 STZ  ABS
OP(op,bc) { h6280_cycles(5); ldy(rd_abx());            } // 5 LDY  ABX
OP(op,dc) { h6280_cycles(2); nop();                    } // 2 NOP
OP(op,fc) { h6280_cycles(2); nop();                    } // 2 NOP

OP(op,0d) { h6280_cycles(5); ora(rd_abs());            } // 5 ORA  ABS
OP(op,2d) { h6280_cycles(5); and_a(rd_abs());          } // 5 AND  ABS
OP(op,4d) { h6280_cycles(5); eor(rd_abs());            } // 5 EOR  ABS
OP(op,6d) { h6280_cycles(5); adc(rd_abs());            } // 5 ADC  ABS
OP(op,8d) { h6280_cycles(5); wr_abs(sta());            } // 5 STA  ABS
OP(op,ad) { h6280_cycles(5); lda(rd_abs());            } // 5 LDA  ABS
OP(op,cd) { h6280_cycles(5); cmp(rd_abs());            } // 5 CMP  ABS
OP(op,ed) { h6280_cycles(5); sbc(rd_abs());            } // 5 SBC  ABS

OP(op,1d) { h6280_cycles(5); ora(rd_abx());            } // 5 ORA  ABX
OP(op,3d) { h6280_cycles(5); and_a(rd_abx());          } // 5 AND  ABX
OP(op,5d) { h6280_cycles(5); eor(rd_abx());            } // 5 EOR  ABX
OP(op,7d) { h6280_cycles(5); adc(rd_abx());            } // 5 ADC  ABX
OP(op,9d) { h6280_cycles(5); wr_abx(sta());            } // 5 STA  ABX
OP(op,bd) { h6280_cycles(5); lda(rd_abx());            } // 5 LDA  ABX
OP(op,dd) { h6280_cycles(5); cmp(rd_abx());            } // 5 CMP  ABX
OP(op,fd) { h6280_cycles(5); sbc(rd_abx());            } // 5 SBC  ABX

OP(op,0e) { h6280_cycles(7); wb_ea(asl(rd_abs()));     } // 7 ASL  ABS
OP(op,2e) { h6280_cycles(7); wb_ea(rol(rd_abs()));     } // 7 ROL  ABS
OP(op,4e) { h6280_cycles(7); wb_ea(lsr(rd_abs()));     } // 7 LSR  ABS
OP(op,6e) { h6280_cycles(7); wb_ea(ror(rd_abs()));     } // 7 ROR  ABS
OP(op,8e) { h6280_cycles(5); wr_abs(stx());            } // 5 STX  ABS
OP(op,ae) { h6280_cycles(5); ldx(rd_abs());            } // 5 LDX  ABS
OP(op,ce) { h6280_cycles(7); wb_ea(dec(rd_abs()));     } // 7 DEC  ABS
OP(op,ee) { h6280_cycles(7); wb_ea(inc(rd_abs()));     } // 7 INC  ABS

OP(op,1e) { h6280_cycles(7); wb_ea(asl(rd_abx()));     } // 7 ASL  ABX
OP(op,3e) { h6280_cycles(7); wb_ea(rol(rd_abx()));     } // 7 ROL  ABX
OP(op,5e) { h6280_cycles(7); wb_ea(lsr(rd_abx()));     } // 7 LSR  ABX
OP(op,7e) { h6280_cycles(7); wb_ea(ror(rd_abx()));     } // 7 ROR  ABX
OP(op,9e) { h6280_cycles(5); wr_abx(stz());            } // 5 STZ  ABX
OP(op,be) { h6280_cycles(5); ldx(rd_aby());            } // 5 LDX  ABY
OP(op,de) { h6280_cycles(7); wb_ea(dec(rd_abx()));     } // 7 DEC  ABX
OP(op,fe) { h6280_cycles(7); wb_ea(inc(rd_abx()));     } // 7 INC  ABX

OP(op,0f) { h6280_cycles(4); bbr(0, rd_zpg());         } // 6/8 BBR0 ZPG,REL
OP(op,2f) { h6280_cycles(4); bbr(2, rd_zpg());         } // 6/8 BBR2 ZPG,REL
OP(op,4f) { h6280_cycles(4); bbr(4, rd_zpg());         } // 6/8 BBR4 ZPG,REL
OP(op,6f) { h6280_cycles(4); bbr(6, rd_zpg());         } // 6/8 BBR6 ZPG,REL
OP(op,8f) { h6280_cycles(4); bbs(0, rd_zpg());         } // 6/8 BBS0 ZPG,REL
OP(op,af) { h6280_cycles(4); bbs(2, rd_zpg());         } // 6/8 BBS2 ZPG,REL
OP(op,cf) { h6280_cycles(4); bbs(4, rd_zpg());         } // 6/8 BBS4 ZPG,REL
OP(op,ef) { h6280_cycles(4); bbs(6, rd_zpg());         } // 6/8 BBS6 ZPG,REL

OP(op,1f) { h6280_cycles(4); bbr(1, rd_zpg());         } // 6/8 BBR1 ZPG,REL
OP(op,3f) { h6280_cycles(4); bbr(3, rd_zpg());         } // 6/8 BBR3 ZPG,REL
OP(op,5f) { h6280_cycles(4); bbr(5, rd_zpg());         } // 6/8 BBR5 ZPG,REL
OP(op,7f) { h6280_cycles(4); bbr(7, rd_zpg());         } // 6/8 BBR7 ZPG,REL
OP(op,9f) { h6280_cycles(4); bbs(1, rd_zpg());         } // 6/8 BBS1 ZPG,REL
OP(op,bf) { h6280_cycles(4); bbs(3, rd_zpg());         } // 6/8 BBS3 ZPG,REL
OP(op,df) { h6280_cycles(4); bbs(5, rd_zpg());         } // 6/8 BBS5 ZPG,REL
OP(op,ff) { h6280_cycles(4); bbs(7, rd_zpg());         } // 6/8 BBS7 ZPG,REL
// ===================== end of the ported MAME section =====================

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#undef A
#undef X
#undef Y
#undef P
#undef S
#undef EAL
#undef EAH
#undef EAW
#undef EAD
#undef ZPL
#undef ZPH
#undef ZPW
#undef ZPD
#undef PCL
#undef PCH
#undef PCW
#undef PCD

// ===================================================================================
// Chipestra glue (handwritten): everything MAME's device framework provided in h6280.cpp's
// second half - bus access, execute loop, input lines, the internal I/O page
// (h6280_device::internal_map), reset - rewritten against IH6280Core's callbacks.
// The register-level behaviour of the I/O page (irq_status_r/w, timer_r/w, port, io buffer)
// and of the execute loop / set_irq_line is copied from MAME's h6280.cpp unchanged.
// ===================================================================================

namespace {
constexpr int kAssertLine = 1;  // MAME ASSERT_LINE
constexpr int kClearLine = 0;   // MAME CLEAR_LINE
constexpr int kNmiLine = 32;    // MAME INPUT_LINE_NMI
constexpr int kIrqMaskRegister = 7;  // MAME's H6280_IRQ_MASK enum value (used as ~H6280_IRQ_MASK in irq_status_r)
}  // namespace

MameDerivedH6280Adapter::MameDerivedH6280Adapter() {
    m_ppc.d = 0;
    m_pc.d = 0;
    m_sp.d = 0x1ff;
    m_zp.d = 0;
    m_ea.d = 0;
    // build the opcode table (h6280_device::h6280_device)
    for (int op = 0; op < 256; op++) {
        m_opcode[op] = s_opcodetable[op];
    }
    // device_start(): clear pending interrupts
    for (auto& elem : m_irq_state) {
        elem = kClearLine;
    }
    m_nmi_state = kClearLine;
    Reset();
}

void MameDerivedH6280Adapter::SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) {
    memoryRead_ = std::move(read);
    memoryWrite_ = std::move(write);
}

void MameDerivedH6280Adapter::Reset() {
    // h6280_device::device_reset()
    /* wipe out the h6280 structure */
    m_ppc.d = 0;
    m_pc.d = 0;
    m_zp.d = 0;
    m_ea.d = 0;
    m_a = 0;
    m_x = 0;
    m_y = 0;
    m_p = 0;
    std::memset(m_mmr, 0, sizeof(std::uint8_t) * 8);
    m_irq_mask = 0;
    m_timer_ack = 0;
    m_timer_value = 0;
    m_io_buffer = 0;
    m_icount = 0;

    /* set I and B flags */
    m_p = _fI | _fB;

    /* stack starts at 0x01ff */
    m_sp.d = 0x1ff;

    /* read the reset vector into PC */
    m_pc.b.l = program_read8(H6280_RESET_VEC);
    m_pc.b.h = program_read8(H6280_RESET_VEC + 1);

    /* CPU starts in low speed mode */
    m_clocks_per_cycle = 4;

    /* timer off by default */
    m_timer_status = 0;
    m_timer_load = 128 * 1024 * m_timer_scale;

    m_irq_pending = 0;
    executing_ = false;
    irqServiceCount_ = 0;
    nmiServiceCount_ = 0;
}

// ---- physical address space: the core's own I/O page (h6280_device::internal_map), then the callbacks ----

std::uint8_t MameDerivedH6280Adapter::PhysicalRead(std::uint32_t address) {
    address &= 0x1FFFFF;
    std::uint8_t value;
    switch (address & 0x1FFC00) {
        case 0x1FE800:  // 0x1fe800-0x1fe80f mirror 0x3f0: PSG page - reads return the I/O buffer
            value = io_buffer_r();
            break;
        case 0x1FEC00:  // timer
            value = timer_r();
            break;
        case 0x1FF000:  // K/O port
            value = port_r();
            break;
        case 0x1FF400:  // interrupt mask/status
            value = irq_status_r(address & 3);
            break;
        default:
            value = memoryRead_ ? memoryRead_(address) : std::uint8_t{0};
            break;
    }
    if (memoryReadObserver_) {
        memoryReadObserver_(address, value);
    }
    return value;
}

void MameDerivedH6280Adapter::PhysicalWrite(std::uint32_t address, std::uint8_t data) {
    address &= 0x1FFFFF;
    switch (address & 0x1FFC00) {
        case 0x1FE800:
            psg_w(address & 0xF, data);
            return;
        case 0x1FEC00:
            timer_w(address & 1, data);
            return;
        case 0x1FF000:
            port_w(data);
            return;
        case 0x1FF400:
            irq_status_w(address & 3, data);
            return;
        default:
            break;
    }
    if (memoryWriteObserver_) {
        memoryWriteObserver_(address, data);
    }
    if (memoryWrite_) {
        memoryWrite_(address, data);
    }
}

/***************************************************************
 *  program_read8       read memory
 ***************************************************************/
std::uint8_t MameDerivedH6280Adapter::program_read8(std::uint32_t addr) {
    check_vdc_vce_penalty(static_cast<std::uint16_t>(addr));
    return PhysicalRead(translated(static_cast<std::uint16_t>(addr)));
}

/***************************************************************
 *  program_write8      write memory
 ***************************************************************/
void MameDerivedH6280Adapter::program_write8(std::uint32_t addr, std::uint8_t data) {
    check_vdc_vce_penalty(static_cast<std::uint16_t>(addr));
    PhysicalWrite(translated(static_cast<std::uint16_t>(addr)), data);
}

/***************************************************************
 *  program_read8z      read memory - zero page
 ***************************************************************/
std::uint8_t MameDerivedH6280Adapter::program_read8z(std::uint32_t addr) {
    return PhysicalRead((m_mmr[1] << 13) | (addr & 0x1fff));
}

/***************************************************************
 *  program_write8z     write memory - zero page
 ***************************************************************/
void MameDerivedH6280Adapter::program_write8z(std::uint32_t addr, std::uint8_t data) {
    PhysicalWrite((m_mmr[1] << 13) | (addr & 0x1fff), data);
}

/***************************************************************
 *  program_read16      read word from memory
 ***************************************************************/
std::uint16_t MameDerivedH6280Adapter::program_read16(std::uint32_t addr) {
    return static_cast<std::uint16_t>(PhysicalRead(translated(static_cast<std::uint16_t>(addr))) |
                                      (PhysicalRead(translated(static_cast<std::uint16_t>(addr + 1))) << 8));
}

/***************************************************************
 *  program_read16z     read a word from a zero page address
 ***************************************************************/
std::uint16_t MameDerivedH6280Adapter::program_read16z(std::uint32_t addr) {
    if ((addr & 0xff) == 0xff) {
        return static_cast<std::uint16_t>(PhysicalRead((m_mmr[1] << 13) | (addr & 0x1fff)) |
                                          (PhysicalRead((m_mmr[1] << 13) | ((addr - 0xff) & 0x1fff)) << 8));
    } else {
        return static_cast<std::uint16_t>(PhysicalRead((m_mmr[1] << 13) | (addr & 0x1fff)) |
                                          (PhysicalRead((m_mmr[1] << 13) | ((addr + 1) & 0x1fff)) << 8));
    }
}

/***************************************************************
 * push a register onto the stack
 ***************************************************************/
void MameDerivedH6280Adapter::push(std::uint8_t value) {
    PhysicalWrite((m_mmr[1] << 13) | m_sp.d, value);
    m_sp.b.l--;
}

/***************************************************************
 * pull a register from the stack
 ***************************************************************/
void MameDerivedH6280Adapter::pull(std::uint8_t& value) {
    m_sp.b.l++;
    value = PhysicalRead((m_mmr[1] << 13) | m_sp.d);
}

/***************************************************************
 *  read_opcode     read an opcode
 ***************************************************************/
std::uint8_t MameDerivedH6280Adapter::read_opcode() { return PhysicalRead(translated(m_pc.w.l)); }

/***************************************************************
 *  read_opcode_arg read an opcode argument
 ***************************************************************/
std::uint8_t MameDerivedH6280Adapter::read_opcode_arg() { return PhysicalRead(translated(m_pc.w.l)); }

//-------------------------------------------------
//  execute_run - execute a timeslice's worth of
//  opcodes (h6280_device::execute_run)
//-------------------------------------------------

std::uint64_t MameDerivedH6280Adapter::Execute(std::uint64_t clockBudget) {
    if (clockBudget == 0) {
        return 0;
    }
    // Clamp so the int32 down-counter (m_icount / m_timer_value) cannot overflow.
    constexpr std::uint64_t kMaxBudget = 0x3FFFFFFF;
    if (clockBudget > kMaxBudget) {
        clockBudget = kMaxBudget;
    }
    budgetStart_ = static_cast<std::int64_t>(clockBudget);
    m_icount = static_cast<std::int32_t>(clockBudget);
    executing_ = true;

    int in;

    if (m_irq_pending == 2) {
        m_irq_pending--;
    }

    /* Execute instructions */
    do {
        m_ppc = m_pc;

        if (opcodeFetchObserver_) {
            opcodeFetchObserver_(m_pc.w.l, PhysicalRead(translated(m_pc.w.l)));
        }

        /* Execute 1 instruction */
        in = read_opcode();
        m_pc.w.l++;
        (this->*m_opcode[in])();

        if (m_irq_pending) {
            if (m_irq_pending == 1) {
                if (!(m_p & _fI)) {
                    m_irq_pending--;
                    check_and_take_irq_lines();
                }
            } else {
                m_irq_pending--;
            }
        }

        /* Check internal timer */
        if (m_timer_status) {
            if (m_timer_value <= 0) {
                if (!m_irq_pending) {
                    m_irq_pending = 1;
                }
                while (m_timer_value <= 0) {
                    m_timer_value += m_timer_load;
                }
                set_irq_line(2, kAssertLine);
            }
        }
    } while (m_icount > 0);

    executing_ = false;
    return static_cast<std::uint64_t>(budgetStart_ - m_icount);
}

void MameDerivedH6280Adapter::SetInputLine(H6280InputLine line, H6280InputLineState state) {
    const int s = (state == H6280InputLineState::Assert) ? kAssertLine : kClearLine;
    switch (line) {
        case H6280InputLine::Irq1:
            set_irq_line(0, s);
            break;
        case H6280InputLine::Irq2:
            set_irq_line(1, s);
            break;
        case H6280InputLine::Nmi:
            set_irq_line(kNmiLine, s);
            break;
    }
}

//**************************************************************************
//  IRQ HANDLING (h6280_device::set_irq_line)
//**************************************************************************

void MameDerivedH6280Adapter::set_irq_line(int irqline, int state) {
    if (irqline == kNmiLine) {
        if (state != kAssertLine)
            return;
        m_nmi_state = static_cast<std::uint8_t>(state);
        check_irq_lines();
    } else if (irqline < 3) {
        /* If the state has not changed, just return */
        if (m_irq_state[irqline] == state)
            return;

        m_irq_state[irqline] = static_cast<std::uint8_t>(state);

        check_irq_lines();
    }
}

//**************************************************************************
//  REGISTER HANDLING (internal I/O page, copied from h6280.cpp)
//**************************************************************************

std::uint8_t MameDerivedH6280Adapter::irq_status_r(std::uint32_t offset) {
    int status;

    switch (offset & 3) {
        default:
            return m_io_buffer;
        case 3: {
            status = 0;
            if (m_irq_state[1] != kClearLine)
                status |= 1; /* IRQ 2 */
            if (m_irq_state[0] != kClearLine)
                status |= 2; /* IRQ 1 */
            if (m_irq_state[2] != kClearLine)
                status |= 4; /* TIMER */
            return static_cast<std::uint8_t>(status | (m_io_buffer & (~kIrqMaskRegister)));
        }
        case 2:
            return static_cast<std::uint8_t>(m_irq_mask | (m_io_buffer & (~kIrqMaskRegister)));
    }
}

void MameDerivedH6280Adapter::irq_status_w(std::uint32_t offset, std::uint8_t data) {
    m_io_buffer = data;
    switch (offset & 3) {
        default:
            m_io_buffer = data;
            break;

        case 2: /* Write irq mask */
            m_irq_mask = data & 0x7;
            check_irq_lines();
            break;

        case 3: /* Timer irq ack */
            set_irq_line(2, kClearLine);
            break;
    }
}

std::uint8_t MameDerivedH6280Adapter::timer_r() {
    /* only returns countdown */
    return static_cast<std::uint8_t>(((m_timer_value >> 10) & 0x7F) | (m_io_buffer & 0x80));
}

void MameDerivedH6280Adapter::timer_w(std::uint32_t offset, std::uint8_t data) {
    m_io_buffer = data;
    switch (offset & 1) {
        case 0: /* Counter preload */
            // matches HW behaviour, value is latched only with 0->1 counter enable transition
            m_timer_load = ((data & 127) + 1) * 1024 * m_timer_scale;
            return;

        case 1: /* Counter enable */
            if (data & 1) { /* stop -> start causes reload */
                if (m_timer_status == 0)
                    m_timer_value = m_timer_load;
            }
            m_timer_status = data & 1;
            return;
    }
}

// K/O port: no callback configured -> MAME returns the I/O buffer on read (m_port_in_cb unset); writes only latch.
std::uint8_t MameDerivedH6280Adapter::port_r() { return m_io_buffer; }

void MameDerivedH6280Adapter::port_w(std::uint8_t data) { m_io_buffer = data; }

std::uint8_t MameDerivedH6280Adapter::io_buffer_r() { return m_io_buffer; }

// PSG page: MAME forwards to c6280_device (removed here); the I/O buffer latch is kept.
void MameDerivedH6280Adapter::psg_w(std::uint32_t /*offset*/, std::uint8_t data) { m_io_buffer = data; }

MameDerivedH6280Adapter::RegisterSnapshot MameDerivedH6280Adapter::Snapshot() const {
    RegisterSnapshot s;
    s.pc = m_pc.w.l;
    s.a = m_a;
    s.x = m_x;
    s.y = m_y;
    s.s = m_sp.b.l;
    s.p = m_p;
    for (int i = 0; i < 8; ++i) {
        s.mmr[i] = m_mmr[i];
    }
    s.irqMask = m_irq_mask;
    s.timerStatus = m_timer_status;
    s.clocksPerCycle = m_clocks_per_cycle;
    s.timerValue = m_timer_value;
    s.timerLoad = m_timer_load;
    s.irq1 = m_irq_state[0] != kClearLine;
    s.irq2 = m_irq_state[1] != kClearLine;
    s.timerIrq = m_irq_state[2] != kClearLine;
    s.irqServiceCount = irqServiceCount_;
    s.nmiServiceCount = nmiServiceCount_;
    return s;
}
