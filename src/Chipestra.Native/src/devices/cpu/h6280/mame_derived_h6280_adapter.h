// license:BSD-3-Clause
// copyright-holders:Bryan McPhail
// The original files also state: "Copyright Bryan McPhail, mish@tendril.co.uk" (based, with permission, on Juergen Buchmueller's 6502 emulator).
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/h6280/h6280.cpp
//   src/devices/cpu/h6280/h6280.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "ih6280_core.h"

#include "device_manager.h"

#include <cstdint>
#include <functional>
#include <utility>

// MameDerivedH6280Adapter - Chipestra-owned
// IH6280Core implementation ported line-for-line from MAME's h6280_device
// (src/devices/cpu/h6280/h6280.cpp/.h, mame0289,
// `// license:BSD-3-Clause`, original copyright Bryan McPhail - see
// THIRD-PARTY-NOTICES.md and the attribution block at the top of the
// .cpp). The instruction bodies, addressing helpers, interrupt/timer logic
// keep MAME's own names (m_a, m_pc, m_mmr, op_xx, ...) on purpose: this is a
// direct port so it can be diffed against the MAME source, the same way
// MameDerivedI8039Adapter mirrors mcs48. Only the device-framework coupling
// was replaced (cpu_device/address_space/devcb/debugger/PSG -> callbacks and
// the small handwritten section at the bottom of the .cpp).
//
// Removed on purpose: the internal PSG (c6280_device - this board never
// routes its output, vaportra.cpp's add_route(ALL_OUTPUTS,"mono",0)), the
// K/O joypad port callbacks (unused), disassembler, save-state/debugger state.
// The PSG register page swallows writes; the I/O buffer behaves as in MAME.
class MameDerivedH6280Adapter : public IH6280Core, public IDevice {
public:
    MameDerivedH6280Adapter();

    void SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) override;
    void SetTimerScale(int scale) override { m_timer_scale = static_cast<std::uint8_t>(scale); }
    void Reset() override;
    std::uint64_t Execute(std::uint64_t clockBudget) override;
    void SetInputLine(H6280InputLine line, H6280InputLineState state) override;

    // Clocks consumed so far by the Execute() call in progress (0 outside one) -
    // lets a Machine sync co-processors to the CPU's exact progress from inside a
    // bus callback (needed for busy-polling drivers), same purpose as
    // MameDerivedZ80Adapter::CyclesConsumedThisCall().
    std::uint64_t CyclesConsumedThisCall() const {
        return executing_ ? static_cast<std::uint64_t>(static_cast<std::int64_t>(budgetStart_) - m_icount) : 0;
    }

    // Register snapshot for tests and diagnostics only (not part of IH6280Core).
    struct RegisterSnapshot {
        std::uint16_t pc = 0;
        std::uint8_t a = 0, x = 0, y = 0, s = 0, p = 0;
        std::uint8_t mmr[8] = {};
        std::uint8_t irqMask = 0;
        std::uint8_t timerStatus = 0;
        std::uint8_t clocksPerCycle = 0;
        std::int32_t timerValue = 0;
        std::int32_t timerLoad = 0;
        bool irq1 = false, irq2 = false, timerIrq = false;
        std::uint64_t irqServiceCount = 0;    // IRQ1/IRQ2/TIQ vectors taken
        std::uint64_t nmiServiceCount = 0;
    };
    RegisterSnapshot Snapshot() const;

    // Diagnostic-only observers (zero cost when unset), same precedent as
    // MameDerivedZ80Adapter.
    using OpcodeFetchObserver = std::function<void(std::uint16_t pc, std::uint8_t opcode)>;
    void SetOpcodeFetchObserverForTest(OpcodeFetchObserver observer) { opcodeFetchObserver_ = std::move(observer); }
    using MemoryWriteObserver = std::function<void(std::uint32_t physicalAddress, std::uint8_t value)>;
    void SetMemoryWriteObserverForTest(MemoryWriteObserver observer) { memoryWriteObserver_ = std::move(observer); }
    // Fires on EVERY PhysicalRead() call - including the H6280's own internal I/O page
    // (timer/irq-status/port, this .cpp's own PhysicalRead() switch) as well as memoryRead_-routed
    // addresses, so a polling loop's read target can be identified regardless of which side it reads.
    using MemoryReadObserver = std::function<void(std::uint32_t physicalAddress, std::uint8_t value)>;
    void SetMemoryReadObserverForTest(MemoryReadObserver observer) { memoryReadObserver_ = std::move(observer); }

private:
    // MAME's PAIR (little-endian layout: b.l/b.h/b.h2/b.h3, w.l/w.h, d).
    union Pair {
        struct {
            std::uint8_t l, h, h2, h3;
        } b;
        struct {
            std::uint16_t l, h;
        } w;
        std::uint32_t d;
    };

#undef PROTOTYPES
#define PROTOTYPES(prefix) \
    void prefix##_00(); void prefix##_01(); void prefix##_02(); void prefix##_03(); \
    void prefix##_04(); void prefix##_05(); void prefix##_06(); void prefix##_07(); \
    void prefix##_08(); void prefix##_09(); void prefix##_0a(); void prefix##_0b(); \
    void prefix##_0c(); void prefix##_0d(); void prefix##_0e(); void prefix##_0f(); \
    void prefix##_10(); void prefix##_11(); void prefix##_12(); void prefix##_13(); \
    void prefix##_14(); void prefix##_15(); void prefix##_16(); void prefix##_17(); \
    void prefix##_18(); void prefix##_19(); void prefix##_1a(); void prefix##_1b(); \
    void prefix##_1c(); void prefix##_1d(); void prefix##_1e(); void prefix##_1f(); \
    void prefix##_20(); void prefix##_21(); void prefix##_22(); void prefix##_23(); \
    void prefix##_24(); void prefix##_25(); void prefix##_26(); void prefix##_27(); \
    void prefix##_28(); void prefix##_29(); void prefix##_2a(); void prefix##_2b(); \
    void prefix##_2c(); void prefix##_2d(); void prefix##_2e(); void prefix##_2f(); \
    void prefix##_30(); void prefix##_31(); void prefix##_32(); void prefix##_33(); \
    void prefix##_34(); void prefix##_35(); void prefix##_36(); void prefix##_37(); \
    void prefix##_38(); void prefix##_39(); void prefix##_3a(); void prefix##_3b(); \
    void prefix##_3c(); void prefix##_3d(); void prefix##_3e(); void prefix##_3f(); \
    void prefix##_40(); void prefix##_41(); void prefix##_42(); void prefix##_43(); \
    void prefix##_44(); void prefix##_45(); void prefix##_46(); void prefix##_47(); \
    void prefix##_48(); void prefix##_49(); void prefix##_4a(); void prefix##_4b(); \
    void prefix##_4c(); void prefix##_4d(); void prefix##_4e(); void prefix##_4f(); \
    void prefix##_50(); void prefix##_51(); void prefix##_52(); void prefix##_53(); \
    void prefix##_54(); void prefix##_55(); void prefix##_56(); void prefix##_57(); \
    void prefix##_58(); void prefix##_59(); void prefix##_5a(); void prefix##_5b(); \
    void prefix##_5c(); void prefix##_5d(); void prefix##_5e(); void prefix##_5f(); \
    void prefix##_60(); void prefix##_61(); void prefix##_62(); void prefix##_63(); \
    void prefix##_64(); void prefix##_65(); void prefix##_66(); void prefix##_67(); \
    void prefix##_68(); void prefix##_69(); void prefix##_6a(); void prefix##_6b(); \
    void prefix##_6c(); void prefix##_6d(); void prefix##_6e(); void prefix##_6f(); \
    void prefix##_70(); void prefix##_71(); void prefix##_72(); void prefix##_73(); \
    void prefix##_74(); void prefix##_75(); void prefix##_76(); void prefix##_77(); \
    void prefix##_78(); void prefix##_79(); void prefix##_7a(); void prefix##_7b(); \
    void prefix##_7c(); void prefix##_7d(); void prefix##_7e(); void prefix##_7f(); \
    void prefix##_80(); void prefix##_81(); void prefix##_82(); void prefix##_83(); \
    void prefix##_84(); void prefix##_85(); void prefix##_86(); void prefix##_87(); \
    void prefix##_88(); void prefix##_89(); void prefix##_8a(); void prefix##_8b(); \
    void prefix##_8c(); void prefix##_8d(); void prefix##_8e(); void prefix##_8f(); \
    void prefix##_90(); void prefix##_91(); void prefix##_92(); void prefix##_93(); \
    void prefix##_94(); void prefix##_95(); void prefix##_96(); void prefix##_97(); \
    void prefix##_98(); void prefix##_99(); void prefix##_9a(); void prefix##_9b(); \
    void prefix##_9c(); void prefix##_9d(); void prefix##_9e(); void prefix##_9f(); \
    void prefix##_a0(); void prefix##_a1(); void prefix##_a2(); void prefix##_a3(); \
    void prefix##_a4(); void prefix##_a5(); void prefix##_a6(); void prefix##_a7(); \
    void prefix##_a8(); void prefix##_a9(); void prefix##_aa(); void prefix##_ab(); \
    void prefix##_ac(); void prefix##_ad(); void prefix##_ae(); void prefix##_af(); \
    void prefix##_b0(); void prefix##_b1(); void prefix##_b2(); void prefix##_b3(); \
    void prefix##_b4(); void prefix##_b5(); void prefix##_b6(); void prefix##_b7(); \
    void prefix##_b8(); void prefix##_b9(); void prefix##_ba(); void prefix##_bb(); \
    void prefix##_bc(); void prefix##_bd(); void prefix##_be(); void prefix##_bf(); \
    void prefix##_c0(); void prefix##_c1(); void prefix##_c2(); void prefix##_c3(); \
    void prefix##_c4(); void prefix##_c5(); void prefix##_c6(); void prefix##_c7(); \
    void prefix##_c8(); void prefix##_c9(); void prefix##_ca(); void prefix##_cb(); \
    void prefix##_cc(); void prefix##_cd(); void prefix##_ce(); void prefix##_cf(); \
    void prefix##_d0(); void prefix##_d1(); void prefix##_d2(); void prefix##_d3(); \
    void prefix##_d4(); void prefix##_d5(); void prefix##_d6(); void prefix##_d7(); \
    void prefix##_d8(); void prefix##_d9(); void prefix##_da(); void prefix##_db(); \
    void prefix##_dc(); void prefix##_dd(); void prefix##_de(); void prefix##_df(); \
    void prefix##_e0(); void prefix##_e1(); void prefix##_e2(); void prefix##_e3(); \
    void prefix##_e4(); void prefix##_e5(); void prefix##_e6(); void prefix##_e7(); \
    void prefix##_e8(); void prefix##_e9(); void prefix##_ea(); void prefix##_eb(); \
    void prefix##_ec(); void prefix##_ed(); void prefix##_ee(); void prefix##_ef(); \
    void prefix##_f0(); void prefix##_f1(); void prefix##_f2(); void prefix##_f3(); \
    void prefix##_f4(); void prefix##_f5(); void prefix##_f6(); void prefix##_f7(); \
    void prefix##_f8(); void prefix##_f9(); void prefix##_fa(); void prefix##_fb(); \
    void prefix##_fc(); void prefix##_fd(); void prefix##_fe(); void prefix##_ff();

    PROTOTYPES(op)
#undef PROTOTYPES

    // opcode accessors (h6280.h)
    std::uint8_t program_read8(std::uint32_t addr);
    void program_write8(std::uint32_t addr, std::uint8_t data);
    std::uint8_t program_read8z(std::uint32_t addr);
    void program_write8z(std::uint32_t addr, std::uint8_t data);
    std::uint16_t program_read16(std::uint32_t addr);
    std::uint16_t program_read16z(std::uint32_t addr);
    void push(std::uint8_t value);
    void pull(std::uint8_t& value);
    std::uint8_t read_opcode();
    std::uint8_t read_opcode_arg();

    std::uint32_t translated(std::uint16_t addr);
    void h6280_cycles(int cyc);
    void set_nz(std::uint8_t n);
    void clear_t();
    void do_interrupt(std::uint16_t vector);
    void check_and_take_irq_lines();
    void check_irq_lines();
    void check_vdc_vce_penalty(std::uint16_t addr);
    void bra(bool cond);
    void ea_zpg();
    void ea_tflg();
    void ea_zpx();
    void ea_zpy();
    void ea_abs();
    void ea_abx();
    void ea_aby();
    void ea_zpi();
    void ea_idx();
    void ea_idy();
    void ea_ind();
    void ea_iax();
    std::uint8_t rd_imm();
    std::uint8_t rd_zpg();
    std::uint8_t rd_zpx();
    std::uint8_t rd_zpy();
    std::uint8_t rd_abs();
    std::uint8_t rd_abx();
    std::uint8_t rd_aby();
    std::uint8_t rd_zpi();
    std::uint8_t rd_idx();
    std::uint8_t rd_idy();
    std::uint8_t rd_tfl();
    void wr_zpg(std::uint8_t tmp);
    void wr_zpx(std::uint8_t tmp);
    void wr_zpy(std::uint8_t tmp);
    void wr_abs(std::uint8_t tmp);
    void wr_abx(std::uint8_t tmp);
    void wr_aby(std::uint8_t tmp);
    void wr_zpi(std::uint8_t tmp);
    void wr_idx(std::uint8_t tmp);
    void wr_idy(std::uint8_t tmp);
    void wb_ea(std::uint8_t tmp);
    void wb_eaz(std::uint8_t tmp);
    void compose_p(std::uint8_t set, std::uint8_t clr);
    void tadc(std::uint8_t tmp);
    void adc(std::uint8_t tmp);
    void tand(std::uint8_t tmp);
    void and_a(std::uint8_t tmp);
    std::uint8_t asl(std::uint8_t tmp);
    void bbr(int bit, std::uint8_t tmp);
    void bbs(int bit, std::uint8_t tmp);
    void bcc();
    void bcs();
    void beq();
    void bit(std::uint8_t tmp);
    void bmi();
    void bne();
    void bpl();
    void brk();
    void bsr();
    void bvc();
    void bvs();
    void cla();
    void clc();
    void cld();
    void cli();
    void clv();
    void clx();
    void cly();
    void cmp(std::uint8_t tmp);
    void cpx(std::uint8_t tmp);
    void cpy(std::uint8_t tmp);
    std::uint8_t dec(std::uint8_t tmp);
    void dex();
    void dey();
    void teor(std::uint8_t tmp);
    void eor(std::uint8_t tmp);
    std::uint8_t inc(std::uint8_t tmp);
    void inx();
    void iny();
    void jmp();
    void jsr();
    void lda(std::uint8_t tmp);
    void ldx(std::uint8_t tmp);
    void ldy(std::uint8_t tmp);
    std::uint8_t lsr(std::uint8_t tmp);
    void nop();
    void tora(std::uint8_t tmp);
    void ora(std::uint8_t tmp);
    void pha();
    void php();
    void phx();
    void phy();
    void pla();
    void plp();
    void plx();
    void ply();
    std::uint8_t rmb(int bit, std::uint8_t tmp);
    std::uint8_t rol(std::uint8_t tmp);
    std::uint8_t ror(std::uint8_t tmp);
    void rti();
    void rts();
    void sax();
    void say();
    void tsbc(std::uint8_t tmp);
    void sbc(std::uint8_t tmp);
    void sec();
    void sed();
    void sei();
    void set();
    std::uint8_t smb(int bit, std::uint8_t tmp);
    void st0(std::uint8_t tmp);
    void st1(std::uint8_t tmp);
    void st2(std::uint8_t tmp);
    std::uint8_t sta();
    std::uint8_t stx();
    std::uint8_t sty();
    std::uint8_t stz();
    void sxy();
    void tai();
    void tam(std::uint8_t tmp);
    void tax();
    void tay();
    void tdd();
    void tia();
    void tii();
    void tin();
    void tma(std::uint8_t tmp);
    std::uint8_t trb(std::uint8_t tmp);
    std::uint8_t tsb(std::uint8_t tmp);
    void tsx();
    void tst(std::uint8_t imm, std::uint8_t tmp);
    void txa();
    void txs();
    void tya();
    void csh();
    void csl();

    enum {
        H6280_RESET_VEC = 0xfffe,
        H6280_NMI_VEC = 0xfffc,
        H6280_TIMER_VEC = 0xfffa,
        H6280_IRQ1_VEC = 0xfff8,
        H6280_IRQ2_VEC = 0xfff6 /* Aka BRK vector */
    };

    // CPU registers (h6280.h)
    Pair m_ppc;                     /* previous program counter */
    Pair m_pc;                      /* program counter */
    Pair m_sp;                      /* stack pointer (always 100 - 1FF) */
    Pair m_zp;                      /* zero page address */
    Pair m_ea;                      /* effective address */
    std::uint8_t m_a = 0;           /* Accumulator */
    std::uint8_t m_x = 0;           /* X index register */
    std::uint8_t m_y = 0;           /* Y index register */
    std::uint8_t m_p = 0;           /* Processor status */
    std::uint8_t m_mmr[8] = {};     /* Hu6280 memory mapper registers */
    std::uint8_t m_irq_mask = 0;    /* interrupt enable/disable */
    std::uint8_t m_timer_status = 0;/* timer status */
    std::uint8_t m_timer_ack = 0;   /* timer acknowledge */
    std::uint8_t m_clocks_per_cycle = 4; /* 4 = low speed mode, 1 = high speed mode */
    std::int32_t m_timer_value = 0; /* timer interrupt */
    std::int32_t m_timer_load = 0;  /* reload value */
    std::uint8_t m_nmi_state = 0;
    std::uint8_t m_irq_state[3] = {};
    std::uint8_t m_irq_pending = 0;
    std::uint8_t m_io_buffer = 0;   /* last value written to the PSG, timer, and interrupt pages */
    std::int32_t m_icount = 0;
    std::uint8_t m_timer_scale = 1;

    // internal I/O page registers (h6280.cpp)
    std::uint8_t irq_status_r(std::uint32_t offset);
    void irq_status_w(std::uint32_t offset, std::uint8_t data);
    std::uint8_t timer_r();
    void timer_w(std::uint32_t offset, std::uint8_t data);
    std::uint8_t port_r();
    void port_w(std::uint8_t data);
    std::uint8_t io_buffer_r();
    void psg_w(std::uint32_t offset, std::uint8_t data);
    void set_irq_line(int irqline, int state);

    // physical-space access: internal I/O page first (MAME's internal_map), then the external callbacks.
    std::uint8_t PhysicalRead(std::uint32_t address);
    void PhysicalWrite(std::uint32_t address, std::uint8_t data);

    typedef void (MameDerivedH6280Adapter::*ophandler)();
    ophandler m_opcode[256];
    static const ophandler s_opcodetable[256];

    MemoryReadFn memoryRead_;
    MemoryWriteFn memoryWrite_;
    OpcodeFetchObserver opcodeFetchObserver_;
    MemoryWriteObserver memoryWriteObserver_;
    MemoryReadObserver memoryReadObserver_;

    bool executing_ = false;
    std::int64_t budgetStart_ = 0;
    std::uint64_t irqServiceCount_ = 0;
    std::uint64_t nmiServiceCount_ = 0;
};
