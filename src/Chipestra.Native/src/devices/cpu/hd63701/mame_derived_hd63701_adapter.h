// license:BSD-3-Clause
// copyright-holders:Aaron Giles
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/m6800/m6800.cpp
//   src/devices/cpu/m6800/m6800.h
//   src/devices/cpu/m6800/m6801.cpp
//   src/devices/cpu/m6800/m6801.h
//   src/devices/cpu/m6800/6800ops.hxx
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "ihd63701_core.h"

#include "device_manager.h"

#include <cstdint>
#include <functional>
#include <utility>

// MameDerivedHd63701Adapter - Chipestra-owned IHd63701Core implementation
// ported from MAME's m6800/m6801 family (src/devices/cpu/m6800/{m6800,m6801}.{cpp,h},
// 6800ops.hxx; BSD-3-Clause, copyright Aaron Giles - see THIRD-PARTY-NOTICES.md).
// HD63701V0 only: `hd6301_cpu_device` with the `hd63701_insn`/`cycles_63701`
// tables and 128 bytes of internal RAM. The plain M6800/M6802/M6808/M6801/M6803/
// NSC8105 variants and the X0/Y0 branches (ports 5-7, Timer 2, different
// port 3/6 handshakes) are out of scope.
//
// The dispatcher is MAME's flat 256-entry function-pointer table; every op_func
// body in 6800ops.hxx is transcribed as an `Op*()` method with the same
// register/flag logic, memory access order and addressing-mode expansions
// (DIRECT/INDEXED/EXTENDED -> ModeDirect()/ModeIndexed()/ModeExtended();
// immediate operands are selected with OperandMode::Immediate).
//
// Cycle accounting follows MAME: a static per-opcode table (`cycles_63701[256]`)
// is charged as one lump sum after the instruction dispatches, rather than
// counting bus accesses. Port/timer/SCI register accesses still go through
// ReadMemory()/WriteMemory(), so a Machine that needs mid-instruction device
// sync should drive Execute() in small budgets and resync after each call.
//
// Interrupt model (check_irq_lines()/check_irq2() in m6801.cpp): NMI (edge,
// vector 0xFFFC) beats IRQ1 (level, vector 0xFFF8, masked by CC bit I), which
// beats the internal "IRQ2" raised by the timer (ICF/OCF/TOF in TCSR) and SCI
// (RDRF/ORFE/TDRE in TRCSR), also masked by CC bit I and vectored per source
// (ICI 0xFFF6, OCI 0xFFF4, TOI 0xFFF2, SCI 0xFFF0). The SCI register file never
// sets its flags here (see below), so only the timer sources can fire IRQ2.
// TRAP (illegal opcode, vector 0xFFEE) is a synchronous exception, always taken
// regardless of the I mask, like MAME's trap() op_func.
//
// TAP/CLI delay the effect on the I mask by one instruction (as in MAME, they
// run execute_one() then check_irq_lines() inside their own handler); this is
// done with the same DispatchOneInstruction()/CheckPendingInterrupts()
// primitives the Execute() loop uses.
//
// WAI/SLP idle: see IHd63701Core::Execute(). WAI pre-pushes the full state
// (PC/X/A/B/CC, 7 bytes) so the eventual interrupt entry skips the push (MAME's
// "wai_state already set" shortcut, 4 cycles instead of 12); SLP does not
// pre-push (normal 12-cycle entry, as in take_irq2()).
//
// Peripheral scope:
// - Ports 1-4: four uniform DDR+data GPIO ports with m6801_cpu_device's p1/p2
//   read/write side effects. Port 2's serial-Tx overlay and port 3's
//   LE/OSS/IS3 handshake are not ported: nothing on Namco System 1 is known to
//   use them and the core has no external lines for those pins. Ports 3/4
//   behave like plain ports 1/2.
// - Timer (TCSR/counter/output-compare/input-capture): ported, including
//   IRQ2 generation. The input-capture pin has no external line, so ICRH/ICRL
//   never update from it.
// - SCI (RMCR/TRCSR/RDR/TDR): register read/write side effects are ported, but
//   the bit-clocked transmit/receive state machine (a MAME timer_device) is not,
//   so RDRF/ORFE/TDRE never change on their own.
// - RCR (RAM control register): plain read/write, no side effects.
//
// The reset vector (0xFFFE/0xFFFF) is read from memory, unlike Z80/M68000. The
// constructor calls Reset() before any callbacks exist (the vector then reads
// as 0x00), so a Machine MUST call SetMemoryCallbacks() and then Reset() again.
class MameDerivedHd63701Adapter : public IHd63701Core, public IDevice {
public:
    MameDerivedHd63701Adapter();

    void SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) override;
    void Reset() override;
    std::uint64_t Execute(std::uint64_t cycleBudget) override;
    void SetInputLine(Hd63701InputLine line, Hd63701InputLineState state) override;

    using PortReadFn = std::function<std::uint8_t()>;
    using PortWriteFn = std::function<void(std::uint8_t value)>;

    // Port 1-4 pin callbacks (see class comment for the port 2/3 scoping
    // decisions). `read` supplies the externally-driven level for bits
    // configured as inputs (DDR bit clear); unset means "nothing external
    // connected" and reads as all-1s (pullup), matching I8039Core's own
    // documented default. `write` is called with the MAME-computed
    // bus-visible byte (output bits show the data latch, input bits show
    // as 1) whenever the DDR or data register write changes it.
    void SetPort1Callbacks(PortReadFn read, PortWriteFn write) { port_[0].SetCallbacks(std::move(read), std::move(write)); }
    void SetPort2Callbacks(PortReadFn read, PortWriteFn write) { port_[1].SetCallbacks(std::move(read), std::move(write)); }
    void SetPort3Callbacks(PortReadFn read, PortWriteFn write) { port_[2].SetCallbacks(std::move(read), std::move(write)); }
    void SetPort4Callbacks(PortReadFn read, PortWriteFn write) { port_[3].SetCallbacks(std::move(read), std::move(write)); }

    // Cycles consumed so far by the Execute() call in progress (0 outside one).
    std::uint64_t CyclesConsumedThisCall() const { return executing_ ? cyclesConsumedThisCall_ : 0; }

    // Register snapshot for tests and diagnostics only (not part of IHd63701Core).
    struct RegisterSnapshot {
        std::uint16_t pc = 0;
        std::uint16_t s = 0, x = 0;
        std::uint8_t a = 0, b = 0, cc = 0;
        bool nmiLine = false, irq1Line = false;
        bool waiWaiting = false, slpWaiting = false;
        std::uint64_t nmiServiceCount = 0;
        std::uint64_t irq1ServiceCount = 0;
        std::uint64_t irq2ServiceCount = 0;
        std::uint64_t trapCount = 0;
    };
    RegisterSnapshot Snapshot() const;

    // Diagnostic-only observer (zero cost when unset).
    using OpcodeFetchObserver = std::function<void(std::uint16_t pc, std::uint8_t opcode)>;
    void SetOpcodeFetchObserverForTest(OpcodeFetchObserver observer) { opcodeFetchObserver_ = std::move(observer); }

    // Diagnostic-only accessor: the 128-byte on-chip RAM (0x0080-0x00FF) is intercepted by the
    // core and never reaches the external memory callbacks, so reading it through the external
    // memory map returns the unmapped default instead. These public duplicates of
    // kInternalRamBase/kInternalRamSize let a caller range-check an address and then use
    // InternalRamForTest(address - kInternalRamBaseForTest).
    static constexpr std::uint16_t kInternalRamBaseForTest = 0x0080;
    static constexpr std::uint16_t kInternalRamSizeForTest = 128;
    // Zeroes the 128-byte on-chip RAM (power-on state). Reset() deliberately leaves it alone (a real
    // /RESET does not clear RAM); a Machine calls this to make a mid-session reset equal to a fresh start.
    void ClearInternalRam() { for (auto& b : internalRam_) b = 0; }
    std::uint8_t InternalRamForTest(std::uint16_t offsetFrom0x80) const { return internalRam_[offsetFrom0x80]; }

private:
    // CC (condition code) bits - MAME's layout: bits 7/6 are unused and read as 1; there are
    // no E/F bits like the 6809.
    static constexpr std::uint8_t CC_C = 0x01;  // Carry
    static constexpr std::uint8_t CC_V = 0x02;  // Overflow
    static constexpr std::uint8_t CC_Z = 0x04;  // Zero
    static constexpr std::uint8_t CC_N = 0x08;  // Negative
    static constexpr std::uint8_t CC_I = 0x10;  // Interrupt mask
    static constexpr std::uint8_t CC_H = 0x20;  // Half carry
    static constexpr std::uint8_t CC_UNUSED = 0xC0; // always read as 1, device_reset() sets them

    // Interrupt/reset vectors (m6800.cpp/m6801.cpp).
    static constexpr std::uint16_t VECTOR_SCI = 0xFFF0;
    static constexpr std::uint16_t VECTOR_TOI = 0xFFF2;
    static constexpr std::uint16_t VECTOR_OCI = 0xFFF4;
    static constexpr std::uint16_t VECTOR_ICI = 0xFFF6;
    static constexpr std::uint16_t VECTOR_IRQ1 = 0xFFF8;
    static constexpr std::uint16_t VECTOR_SWI = 0xFFFA;
    static constexpr std::uint16_t VECTOR_NMI = 0xFFFC;
    static constexpr std::uint16_t VECTOR_RESET = 0xFFFE;
    static constexpr std::uint16_t VECTOR_TRAP = 0xFFEE; // hd6301_cpu_device::take_trap()

    // Timer Control/Status Register bits (m6801.cpp).
    static constexpr std::uint8_t TCSR_OLVL = 0x01;
    static constexpr std::uint8_t TCSR_IEDG = 0x02;
    static constexpr std::uint8_t TCSR_ETOI = 0x04;
    static constexpr std::uint8_t TCSR_EOCI = 0x08;
    static constexpr std::uint8_t TCSR_EICI = 0x10;
    static constexpr std::uint8_t TCSR_TOF = 0x20;
    static constexpr std::uint8_t TCSR_OCF = 0x40;
    static constexpr std::uint8_t TCSR_ICF = 0x80;

    // Transmit/Receive Control and Status Register bits (m6801.cpp).
    static constexpr std::uint8_t TRCSR_RDRF = 0x80;
    static constexpr std::uint8_t TRCSR_ORFE = 0x40;
    static constexpr std::uint8_t TRCSR_TDRE = 0x20;
    static constexpr std::uint8_t TRCSR_RIE = 0x10;
    static constexpr std::uint8_t TRCSR_RE = 0x08;
    static constexpr std::uint8_t TRCSR_TIE = 0x04;
    static constexpr std::uint8_t TRCSR_TE = 0x02;
    static constexpr std::uint8_t TRCSR_WU = 0x01;

    static constexpr std::uint16_t kInternalRamBase = 0x0080;
    static constexpr std::uint16_t kInternalRamSize = 128;  // hd63701v0_cpu_device's own nvram_bytes
    std::uint8_t internalRam_[kInternalRamSize] = {};

    // ---- bus access (m6800.cpp's RM()/WM()/M_RDOP()/M_RDOP_ARG()) ----
    void Eat(int cycles) { cyclesConsumedThisCall_ += static_cast<std::uint64_t>(cycles); }
    std::uint8_t ReadMemory(std::uint16_t address);
    void WriteMemory(std::uint16_t address, std::uint8_t value);
    std::uint8_t ReadOpcode();     // fetch at pc_, bumps pc_ (M_RDOP)
    std::uint8_t ReadOpcodeArg();  // fetch at pc_, bumps pc_ (M_RDOP_ARG; no decrypted-opcode map,
                                    // both route through the same callback)
    std::uint16_t ReadMemory16(std::uint16_t address);   // RM16()
    void WriteMemory16(std::uint16_t address, std::uint16_t value);  // WM16()

    // ---- on-chip register/RAM decode ----
    bool IsInternalRegister(std::uint16_t address) const { return address <= 0x0014; }
    bool IsInternalRam(std::uint16_t address) const {
        return address >= kInternalRamBase && address < kInternalRamBase + kInternalRamSize;
    }
    std::uint8_t ReadInternalRegister(std::uint16_t address);
    void WriteInternalRegister(std::uint16_t address, std::uint8_t value);

    // ---- addressing modes (DIRECT/INDEXED/EXTENDED/IMM8/IMM16 macros) ----
    std::uint16_t ModeDirect();     // DIRBYTE/DIRWORD: DIRECT -> ea in page 0
    std::uint16_t ModeExtended();   // EXTBYTE/EXTWORD: EXTENDED -> 16-bit absolute ea
    std::uint16_t ModeIndexed();    // IDXBYTE/IDXWORD: INDEXED -> X + unsigned 8-bit displacement

    // ---- flags (SET_FLAGS8/16, SET_H, SET_NZ8/16, flags8i/flags8d) ----
    void SetFlags8(std::uint8_t a, std::uint8_t b, std::uint32_t r);   // SET_N8/Z8/V8/C8, r kept at full
                                                                         // width (see .cpp)
    void SetFlags16(std::uint16_t a, std::uint16_t b, std::uint32_t r);
    void SetH(std::uint8_t a, std::uint8_t b, std::uint32_t r) { cc_ |= static_cast<std::uint8_t>((((a) ^ (b) ^ (r)) & 0x10) << 1); }
    void SetNz8(std::uint8_t a);
    void SetNz16(std::uint16_t a);
    void SetFlags8Inc(std::uint8_t a);  // SET_FLAGS8I - flags8i[] lookup table
    void SetFlags8Dec(std::uint8_t a);  // SET_FLAGS8D - flags8d[] lookup table
    bool NxorC() const { return static_cast<bool>((cc_ & CC_N) ^ ((cc_ & CC_C) << 3)); }
    bool NxorV() const { return static_cast<bool>((cc_ & CC_N) ^ ((cc_ & CC_V) << 2)); }

    // ---- interrupts ----
    // Returns true if an interrupt (NMI/IRQ1/IRQ2) was actually taken (state pushed, PC
    // redirected to a vector). Execute() uses this to skip dispatching a normal instruction in
    // the same iteration: interrupt entry and the handler's first instruction are separate
    // turns. The op handlers that call this directly (Tap()/Cli()/Wai()/Slp()/Rti(), matching
    // MAME's explicit check_irq_lines() calls) ignore the return value.
    bool CheckPendingInterrupts();  // check_irq_lines()
    void CheckIrq2();               // m6801_cpu_device::check_irq2() (ICI/OCI/TOI/SCI priority; the
                                     // SCI branch is dead code here, see the class comment)
    void TakeIrq2(std::uint16_t vector);  // m6801_cpu_device::take_irq2()
    void EnterInterrupt(std::uint16_t vector);  // m6800_cpu_device::enter_interrupt()
    void TakeTrap();                // hd6301_cpu_device::take_trap()
    bool irq2TakenThisCheck_ = false;  // scratch flag CheckIrq2()/TakeIrq2() use to report back to
                                        // CheckPendingInterrupts() whether the IRQ2 branch fired

    // ---- one instruction (execute_one(), transcribed as calls) ----
    void DispatchOneInstruction();  // fetch + m_insn[ireg]() + Eat(m_cycles[ireg]) - what both the
                                     // Execute() loop and Tap()/Cli() call
    void Dispatch(std::uint8_t opcode);  // the 256-entry hd63701_insn[] switch

    // ---- op_func group: inherent/accumulator single-register ops ----
    void OpNop() {}
    void OpTrap();     // illegal opcode / HD6301 TRAP vector
    void OpUndoc1();   // $12 undocumented: X += RM(S+1)
    void OpUndoc2();   // $13 undocumented (same as $12 on real silicon)
    void OpLsrd();
    void OpAsld();
    void OpTap();      // one-instruction interrupt delay (see class comment)
    void OpTpa() { a_ = cc_; }
    void OpInx();
    void OpDex();
    void OpClv() { cc_ &= static_cast<std::uint8_t>(~CC_V); }
    void OpSev() { cc_ |= CC_V; }
    void OpClc() { cc_ &= static_cast<std::uint8_t>(~CC_C); }
    void OpSec() { cc_ |= CC_C; }
    void OpCli();      // one-instruction interrupt delay (see class comment)
    void OpSei() { cc_ |= CC_I; }
    void OpSba();
    void OpCba();
    void OpXgdx();     // $18: swap X and D
    void OpDaa();
    void OpSlp();      // idle until interrupt, no state pre-push (see class comment)
    void OpAba();
    void OpBranch(bool taken);
    void OpTsx() { x_ = static_cast<std::uint16_t>(s_ + 1); }
    void OpIns() { ++s_; }
    void OpPula() { PullByte(a_); }
    void OpPulb() { PullByte(b_); }
    void OpDes() { --s_; }
    void OpTxs() { s_ = static_cast<std::uint16_t>(x_ - 1); }
    void OpPsha() { PushByte(a_); }
    void OpPshb() { PushByte(b_); }
    void OpPulx();
    void OpRts();
    void OpAbx() { x_ = static_cast<std::uint16_t>(x_ + b_); }
    void OpRti();
    void OpPshx();
    void OpMul();
    void OpWai();      // pre-pushes state, then idles (see class comment)
    void OpSwi();

    // 8-bit inherent RMW (A/B accumulator forms) and their indexed/extended twins
    void OpNegA();
    void OpComA();
    void OpLsrA();
    void OpRorA();
    void OpAsrA();
    void OpAslA();
    void OpRolA();
    void OpDecA();
    void OpIncA();
    void OpTstA();
    void OpClrA();
    void OpNegB();
    void OpComB();
    void OpLsrB();
    void OpRorB();
    void OpAsrB();
    void OpAslB();
    void OpRolB();
    void OpDecB();
    void OpIncB();
    void OpTstB();
    void OpClrB();

    enum class RmwMode { Indexed, Extended };
    void OpNegM(RmwMode mode);
    void OpComM(RmwMode mode);
    void OpLsrM(RmwMode mode);
    void OpRorM(RmwMode mode);
    void OpAsrM(RmwMode mode);
    void OpAslM(RmwMode mode);
    void OpRolM(RmwMode mode);
    void OpDecM(RmwMode mode);
    void OpIncM(RmwMode mode);
    void OpTstM(RmwMode mode);
    void OpJmpM(RmwMode mode);
    void OpClrM(RmwMode mode);

    // $61/$71 AIM, $62/$72 OIM, $65/$75 EIM, $6B/$7B TIM (TIM: no write-back, flags only).
    // The "$7_" row of this group is DIRECT addressing, not extended like other RMW opcodes
    // (an encoding quirk of these HD6301 opcodes; 6800ops.hxx names them aim_di/oim_di/...).
    enum class BitOpMode { Indexed, Direct };
    void OpAimM(BitOpMode mode);
    void OpOimM(BitOpMode mode);
    void OpEimM(BitOpMode mode);
    void OpTimM(BitOpMode mode);

    // 8-bit accumulator register-op group (immediate/direct/indexed/extended), A and B forms
    enum class OperandMode { Immediate, Direct, Indexed, Extended };
    std::uint8_t ReadOperand8(OperandMode mode, std::uint16_t& eaOut);
    void OpSubA(OperandMode mode);
    void OpCmpA(OperandMode mode);
    void OpSbcA(OperandMode mode);
    void OpAndA(OperandMode mode);
    void OpBitA(OperandMode mode);
    void OpLdaA(OperandMode mode);
    void OpStaA(OperandMode mode);  // direct/indexed/extended only (immediate traps on this core)
    void OpEorA(OperandMode mode);
    void OpAdcA(OperandMode mode);
    void OpOraA(OperandMode mode);
    void OpAddA(OperandMode mode);
    void OpSubB(OperandMode mode);
    void OpCmpB(OperandMode mode);
    void OpSbcB(OperandMode mode);
    void OpAndB(OperandMode mode);
    void OpBitB(OperandMode mode);
    void OpLdaB(OperandMode mode);
    void OpStaB(OperandMode mode);
    void OpEorB(OperandMode mode);
    void OpAdcB(OperandMode mode);
    void OpOraB(OperandMode mode);
    void OpAddB(OperandMode mode);

    // 16-bit register-op group (D/X/S), immediate/direct/indexed/extended
    std::uint16_t ReadOperand16(OperandMode mode, std::uint16_t& eaOut);
    void OpSubD(OperandMode mode);
    void OpAddD(OperandMode mode);
    void OpCpx(OperandMode mode);   // 6803-style CPX (full 16-bit flags, not the base-6800 CMPX bug)
    void OpLdd(OperandMode mode);
    void OpStd(OperandMode mode);   // direct/indexed/extended only (immediate traps)
    void OpLdx(OperandMode mode);
    void OpStx(OperandMode mode);   // direct/indexed/extended only (immediate traps)
    void OpLds(OperandMode mode);
    void OpSts(OperandMode mode);   // direct/indexed/extended only (immediate traps)

    void OpBsr();
    enum class JsrMode { Direct, Indexed, Extended };
    void OpJsrM(JsrMode mode);

    void PushByte(std::uint8_t v) { WriteMemory(s_, v); --s_; }
    void PushWord(std::uint16_t v) {
        WriteMemory(s_, static_cast<std::uint8_t>(v & 0xFF));
        --s_;
        WriteMemory(s_, static_cast<std::uint8_t>(v >> 8));
        --s_;
    }
    void PullByte(std::uint8_t& v) { ++s_; v = ReadMemory(s_); }
    void PullWord(std::uint16_t& v) {
        ++s_;
        std::uint16_t hi = ReadMemory(s_);
        ++s_;
        v = static_cast<std::uint16_t>((hi << 8) | ReadMemory(s_));
    }

    // ---- port GPIO (see class comment) ----
    struct Port {
        std::uint8_t ddr = 0;
        std::uint8_t data = 0;
        PortReadFn read;
        PortWriteFn write;
        void SetCallbacks(PortReadFn r, PortWriteFn w) { read = std::move(r); write = std::move(w); }
        std::uint8_t ReadRegister() const {
            if (ddr == 0xFF) return data;
            std::uint8_t external = read ? read() : 0xFF;
            return static_cast<std::uint8_t>((external & ~ddr) | (data & ddr));
        }
        void NotifyWrite() const {
            if (write) write(static_cast<std::uint8_t>((data & ddr) | static_cast<std::uint8_t>(~ddr)));
        }
    };
    Port port_[4];

    // ---- timer (free-running counter, output-compare, input-capture, TCSR) ----
    // counter_/outputCompare_/timerOver_/timerNext_ mirror MAME's CTD/OCD/TOD/m_timer_next
    // (m6801.cpp). MAME keeps them at 32 bits and folds them down in cleanup_counters() only to
    // bound the high word; comparisons are always relative. Here they are 64-bit, which never
    // wraps in a realistic session, so cleanup_counters() is not ported. OCRH/OCRL/CH/CL only
    // read/write the low 16 bits.
    std::uint8_t tcsr_ = 0;
    std::uint8_t pendingTcsr_ = 0;
    std::uint64_t counter_ = 0;               // CTD
    std::uint64_t outputCompare_ = 0xFFFF;    // OCD
    std::uint64_t timerOver_ = 0xFFFF;        // TOD
    std::uint64_t timerNext_ = 0xFFFF;
    std::uint16_t inputCapture_ = 0;
    std::uint8_t latch09_ = 0;          // m_latch09, HD6301-only ch_w() staging byte

    // ---- SCI register file (register-level only, see class comment) ----
    std::uint8_t rmcr_ = 0;
    std::uint8_t trcsr_ = 0;
    std::uint8_t rdr_ = 0;
    std::uint8_t tdr_ = 0;
    bool trcsrReadTdre_ = false;
    bool trcsrReadOrfe_ = false;
    bool trcsrReadRdrf_ = false;

    std::uint8_t ramControl_ = 0;

    void TcsrWrite(std::uint8_t data);
    std::uint8_t TcsrRead();
    std::uint8_t ChRead();
    std::uint8_t ClRead() const { return static_cast<std::uint8_t>(counter_ & 0xFF); }
    void ChWrite(std::uint8_t data);
    void ClWrite(std::uint8_t data);
    std::uint8_t OcrhRead() const { return static_cast<std::uint8_t>(outputCompare_ >> 8); }
    std::uint8_t OcrlRead() const { return static_cast<std::uint8_t>(outputCompare_ & 0xFF); }
    void OcrhWrite(std::uint8_t data);
    void OcrlWrite(std::uint8_t data);
    std::uint8_t IcrhRead();
    std::uint8_t IcrlRead() const { return static_cast<std::uint8_t>(inputCapture_ & 0xFF); }
    void ModifiedCounters();  // set OCH-vs-CTH high byte, recompute timerNext_
    void SetTimerEvent() { timerNext_ = std::min(outputCompare_, timerOver_); }
    void CheckTimerEvent();  // OCI/TOI flag-setting + CheckIrq2()
    void IncrementCounter(std::uint32_t amount);  // charges Eat() cost through into the counter too

    std::uint16_t GetD() const { return static_cast<std::uint16_t>((a_ << 8) | b_); }
    void SetD(std::uint16_t v) { a_ = static_cast<std::uint8_t>(v >> 8); b_ = static_cast<std::uint8_t>(v & 0xFF); }

    // ---- CPU registers ----
    std::uint16_t pc_ = 0;
    std::uint16_t s_ = 0;
    std::uint16_t x_ = 0;
    std::uint8_t a_ = 0, b_ = 0;
    std::uint8_t cc_ = 0;

    bool nmiLine_ = false;
    bool nmiPending_ = false;
    bool irq1Line_ = false;

    static constexpr std::uint8_t kWaiFlag = 0x08;
    static constexpr std::uint8_t kSlpFlag = 0x10;
    std::uint8_t waiState_ = 0;  // m_wai_state (WAI/SLP bits)

    MemoryReadFn memoryRead_;
    MemoryWriteFn memoryWrite_;
    OpcodeFetchObserver opcodeFetchObserver_;

    bool executing_ = false;
    std::uint64_t cyclesConsumedThisCall_ = 0;
    std::uint64_t nmiServiceCount_ = 0;
    std::uint64_t irq1ServiceCount_ = 0;
    std::uint64_t irq2ServiceCount_ = 0;
    std::uint64_t trapCount_ = 0;
};
