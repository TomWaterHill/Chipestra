// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <functional>

// Interrupt pins of the Z80 core (Chipestra-owned names, not MAME's
// INPUT_LINE_* constants). Only IRQ and NMI are modeled; the bus-arbitration
// pins (BUSREQ/BUSACK/WAIT) are not, since no supported board has more than
// one bus master on the Z80's memory map.
enum class Z80InputLine {
    Irq,  // maskable interrupt request (~INT pin)
    Nmi,  // non-maskable interrupt (~NMI pin, rising-edge triggered)
};

enum class Z80InputLineState {
    Clear,
    Assert,
};

// IZ80Core - stable, MAME-independent Z80 CPU execution interface.
// No MAME type (device_t, address_space, machine_config, devcb_*, etc.)
// appears in this header or is reachable through it.
//
// An implementation owns no memory/IO state of its own - every bus access
// goes through the callbacks below; the Machine wires them to its own
// AddressSpace.
class IZ80Core {
public:
    virtual ~IZ80Core() = default;

    using MemoryReadFn = std::function<std::uint8_t(std::uint16_t address)>;
    using MemoryWriteFn = std::function<void(std::uint16_t address, std::uint8_t value)>;

    // Installs the callbacks used for every memory access this core makes -
    // opcode fetch, operand fetch, and general data read/write alike. MAME's
    // separate opcode address space is not modeled.
    virtual void SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) = 0;

    using IoReadFn = std::function<std::uint8_t(std::uint8_t port)>;
    using IoWriteFn = std::function<void(std::uint8_t port, std::uint8_t value)>;

    // Installs the callbacks used for IN/OUT and the ED-prefixed port
    // instructions. Ports are 8-bit addressed; the upper address byte that
    // real hardware also drives (B or A, depending on the instruction) is not
    // passed through.
    virtual void SetIoCallbacks(IoReadFn read, IoWriteFn write) = 0;

    using InterruptAckFn = std::function<std::uint32_t()>;

    // Installs the callback invoked during a maskable-interrupt acknowledge
    // cycle to obtain what the interrupting device places on the data bus.
    //
    // The return value is packed the same way MAME's z80_device packs
    // m_tmp_irq_vector - bits 16-23 hold an opcode byte, bits 0-15 hold
    // a little-endian 16-bit address operand, used only when that opcode is
    // CALL nnnn (0xCD) or JP nnnn (0xC3). A single-byte injection (RST xx,
    // EI, or any other byte) is returned with bits 8-31 all zero.
    //   - IM 0: the byte(s) become the injected instruction. As in MAME, only
    //     CALL nnnn, JP nnnn, RST xx and EI are executed; any other byte is
    //     ignored.
    //   - IM 2: only the low 8 bits are used, as the vector table's low byte.
    //   - Never called in IM 1 (always vectors to 0x0038 regardless of bus
    //     content) or for NMI.
    virtual void SetInterruptAcknowledgeCallback(InterruptAckFn callback) = 0;

    // Resets registers/flags to power-on-equivalent state (PC=0, IFF1/IFF2
    // cleared, IM=0, halt cleared - matches the real Z80's RESET pin).
    virtual void Reset() = 0;

    // Executes instructions until at least `cycleBudget` T-states have
    // elapsed. Instructions are not preemptible, so the final instruction may
    // run slightly past the budget. Returns the number of T-states actually
    // consumed.
    virtual std::uint64_t Execute(std::uint64_t cycleBudget) = 0;

    // Sets the level (Nmi: edge; the transition itself is what's latched)
    // or state (Irq: level) of an input line.
    virtual void SetInputLine(Z80InputLine line, Z80InputLineState state) = 0;
};
