// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// GyrussMachine native tests. All ROM bytes below are hand-written
// synthetic programs (no real Gyruss ROM data).
//
// Synthetic Z80 (audiocpu) ROM layout shared by the boot/command tests:
//   0x0000  LD SP,7F00h     ; 31 00 7F (10 cycles)
//   0x0003  IM 1            ; ED 56    ( 8 cycles)
//   0x0005  EI              ; FB       ( 4 cycles)
//   0x0006  LD A,CDh        ; 3E CD    ( 7 cycles)
//   0x0008  LD (6010h),A    ; 32 10 60 (13 cycles) - proves boot ran (0xCD,
//                                                     not RAM's zero default)
//   0x000B  HALT            ; 76       ( 4 cycles) - waits for the IRQ pulse
//   0x000C  JR 0x000C       ; 18 FE    (12 cycles) - landing spot after RETI
//   0x0038  IM 1's fixed vector - the command-latch IRQ handler:
//     PUSH AF          ; F5
//     LD A,(8000h)     ; 3A 00 80 - read the soundlatch
//     LD (6011h),A     ; 32 11 60 - store where the test can observe it
//     ; AY1: select+write tone period fine (reg 0), volume (reg 8), enable
//     ; (reg 7) - an audible, non-DC-biased Channel A tone.
//     LD A,00h ; OUT (00h),A ; LD A,08h ; OUT (02h),A   - tone period fine = 8
//     LD A,08h ; OUT (00h),A ; LD A,0Fh ; OUT (02h),A   - volume = 15 (max)
//     LD A,07h ; OUT (00h),A ; LD A,3Eh ; OUT (02h),A   - enable ch.A tone only
//     ; Trigger the i8039's own "SFX channel": soundlatch2 then i8039_irq_w.
//     LD A,55h ; OUT (18h),A ; OUT (14h),A
//     POP AF           ; F1
//     EI               ; FB
//     RETI             ; ED 4D
//
// Synthetic i8039 (audio2) ROM layout:
//   0x000  EN I              ; 05       - enable the external interrupt
//   0x001  JMP 001h          ; 04 01    - idle loop (interrupt-driven)
//   0x003  (external IRQ vector, CALL 003h per mcs48's own check_irqs())
//     MOVX A,@R0     ; 80  - reads soundlatch2 (any bus address returns the
//                            same value; R0 is 0 after reset's RAM clear)
//     OUTL P1,A      ; 39  - DAC output (this file's own OnDacWrite capture)
//     OUTL P2,A      ; 3A  - clears the i8039's own IRQ (GyrussMachine::
//                            OnI8039Port2Write)
//     RETR           ; 93

#include "gyruss_machine_tests.h"

#include "default_machine_catalog.h"
#include "devices/konami/gyruss/gyruss_machine.h"
#include "machine_registry.h"
#include "test_support.h"
#include "chipestra_abi.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

std::vector<std::uint8_t> BuildSyntheticZ80Rom() {
    std::vector<std::uint8_t> rom(0x6000, 0x00);
    std::size_t i = 0;
    rom[i++] = 0x31;  // LD SP,7F00h
    rom[i++] = 0x00;
    rom[i++] = 0x7F;
    rom[i++] = 0xED;  // IM 1
    rom[i++] = 0x56;
    rom[i++] = 0xFB;  // EI
    rom[i++] = 0x3E;  // LD A,CDh
    rom[i++] = 0xCD;
    rom[i++] = 0x32;  // LD (6010h),A
    rom[i++] = 0x10;
    rom[i++] = 0x60;
    rom[i++] = 0x76;  // HALT
    rom[i++] = 0x18;  // JR $ (-> 0x000C)
    rom[i++] = 0xFE;

    i = 0x0038;
    rom[i++] = 0xF5;  // PUSH AF
    rom[i++] = 0x3A;  // LD A,(8000h)
    rom[i++] = 0x00;
    rom[i++] = 0x80;
    rom[i++] = 0x32;  // LD (6011h),A
    rom[i++] = 0x11;
    rom[i++] = 0x60;
    rom[i++] = 0x3E;  // LD A,00h
    rom[i++] = 0x00;
    rom[i++] = 0xD3;  // OUT (00h),A  (ay1 address_w = tone A fine)
    rom[i++] = 0x00;
    rom[i++] = 0x3E;  // LD A,08h
    rom[i++] = 0x08;
    rom[i++] = 0xD3;  // OUT (02h),A  (ay1 data_w = 8)
    rom[i++] = 0x02;
    rom[i++] = 0x3E;  // LD A,08h
    rom[i++] = 0x08;
    rom[i++] = 0xD3;  // OUT (00h),A  (ay1 address_w = volume A)
    rom[i++] = 0x00;
    rom[i++] = 0x3E;  // LD A,0Fh
    rom[i++] = 0x0F;
    rom[i++] = 0xD3;  // OUT (02h),A  (ay1 data_w = 15)
    rom[i++] = 0x02;
    rom[i++] = 0x3E;  // LD A,07h
    rom[i++] = 0x07;
    rom[i++] = 0xD3;  // OUT (00h),A  (ay1 address_w = enable)
    rom[i++] = 0x00;
    rom[i++] = 0x3E;  // LD A,3Eh
    rom[i++] = 0x3E;
    rom[i++] = 0xD3;  // OUT (02h),A  (ay1 data_w = 0x3E: ch.A tone enabled only)
    rom[i++] = 0x02;
    rom[i++] = 0x3E;  // LD A,55h
    rom[i++] = 0x55;
    rom[i++] = 0xD3;  // OUT (18h),A  (soundlatch2 = 0x55)
    rom[i++] = 0x18;
    rom[i++] = 0xD3;  // OUT (14h),A  (i8039_irq_w)
    rom[i++] = 0x14;
    rom[i++] = 0xF1;  // POP AF
    rom[i++] = 0xFB;  // EI
    rom[i++] = 0xED;  // RETI
    rom[i++] = 0x4D;

    return rom;
}

std::vector<std::uint8_t> BuildSyntheticI8039Rom() {
    std::vector<std::uint8_t> rom(0x1000, 0x00);
    rom[0x000] = 0x05;  // EN I
    rom[0x001] = 0x04;  // JMP 001h
    rom[0x002] = 0x01;
    rom[0x003] = 0x80;  // MOVX A,@R0
    rom[0x004] = 0x39;  // OUTL P1,A
    rom[0x005] = 0x3A;  // OUTL P2,A
    rom[0x006] = 0x93;  // RETR
    return rom;
}

}  // namespace

void GyrussMachineRegisteredInDefaultCatalog() {
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("KONAMI_GYRUSS"));
    Check(def != nullptr, "catalog: KONAMI_GYRUSS is registered in the default MachineRegistry");
    if (def != nullptr) {
        Check(def->Category == MachineCategory::Arcade, "catalog: KONAMI_GYRUSS Category == Arcade");
        Check(def->Manufacturer == "Konami", "catalog: KONAMI_GYRUSS Manufacturer == Konami");
        Check(!def->Variants.empty(), "catalog: KONAMI_GYRUSS has at least one Variant");
    }
}

void GyrussMachineExposes16LogicalChannels() {
    GyrussMachine machine(BuildSyntheticZ80Rom(), BuildSyntheticI8039Rom());
    machine.Initialize();
    Check(machine.LogicalChannels().size() == 16, "channels: GyrussMachine exposes 16 LogicalChannels (5x3 AY8910 + 1 DAC)");
    Check(machine.LogicalChannels()[0].DeviceName == "AY8910", "channels: id 0 is an AY8910 channel");
    Check(machine.LogicalChannels()[15].DeviceName == "i8039", "channels: id 15 is the DAC channel");
}

void GyrussMachineBootSequenceReadsAndWrites() {
    GyrussMachine machine(BuildSyntheticZ80Rom(), BuildSyntheticI8039Rom());
    machine.Initialize();

    Check(machine.ReadZ80Memory(0x6010) == 0xCD, "boot: RAM marker (0xCD) written during boot, proving reset ran");
    const auto snap = machine.Z80Snapshot();
    Check(snap.halted, "boot: Z80 reached HALT after boot (waiting for the command-latch IRQ)");
}

void GyrussMachineCommandInjectionWritesAy1Registers() {
    GyrussMachine machine(BuildSyntheticZ80Rom(), BuildSyntheticI8039Rom());
    machine.Initialize();

    MusicCommand cmd;
    cmd.Kind = MusicCommandKind::Raw;
    cmd.Value = 0x42;
    machine.SendCommand(cmd);

    // 40 blocks (~53ms at 192kHz canonical) is ample for the IRQ-priming
    // loop to catch the HALTed Z80 and run the whole handler (a few dozen
    // T-states) well within one block.
    for (int i = 0; i < 40; ++i) {
        machine.Render(EmuDuration{256}, nullptr);
    }

    Check(machine.ReadZ80Memory(0x6011) == 0x42, "command: the IRQ handler's own soundlatch read observed 0x42");
    Check(machine.Ay8910ForTest(0).RegisterForTest(0x00) == 0x08, "command: ay1 tone A period fine == 8");
    Check(machine.Ay8910ForTest(0).RegisterForTest(0x08) == 0x0F, "command: ay1 channel A volume == 15 (max)");
    Check(machine.Ay8910ForTest(0).RegisterForTest(0x07) == 0x3E,
          "command: ay1 enable register == 0x3E (channel A tone enabled, everything else disabled)");
}

void GyrussMachineCommandDispatchTriggersI8039Dac() {
    GyrussMachine machine(BuildSyntheticZ80Rom(), BuildSyntheticI8039Rom());
    machine.Initialize();

    MusicCommand cmd;
    cmd.Kind = MusicCommandKind::Raw;
    cmd.Value = 0x11;
    machine.SendCommand(cmd);

    for (int i = 0; i < 40; ++i) {
        machine.Render(EmuDuration{256}, nullptr);
    }

    // The i8039's own IRQ-service count is the clearest sign its ISR
    // actually ran (soundlatch2 + i8039_irq_w handoff) - the Z80 handler
    // unconditionally triggers it every time a command is dispatched.
    const auto i8039Snap = machine.I8039Snapshot();
    Check(i8039Snap.irqServiceCount >= 1, "i8039: the external interrupt (soundlatch2 handoff) was serviced");
    Check(!i8039Snap.irqInProgress, "i8039: RETR completed (Port 2 write cleared its own IRQ, matching real hardware)");
}

// ALL RESET must restore Pan (matches DariusMachine's OnPanOverrideCleared
// test): GyrussMachine sets its hard-L/R Pan once, in Initialize(), so
// clearing PanOverridden alone would leave a channel's Pan stuck at
// whatever a prior UI override had set it to.
void GyrussMachineOnPanOverrideClearedResyncsPan() {
    GyrussMachine machine(BuildSyntheticZ80Rom(), BuildSyntheticI8039Rom());
    machine.Initialize();

    std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    Check(channels.size() == 16, "OnPanOverrideCleared: channels exist");
    if (channels.size() != 16) return;

    // ay1 (channels 0-2) is hard-panned right; the DAC (channel 15) is hard
    // left - Initialize()'s own DISCRETE_MIXER5-derived assignment.
    Check(channels[0].Pan == 1.0f, "OnPanOverrideCleared: ay1 ch0's fixed Pan starts at +1.0 (hard right)");
    Check(channels[15].Pan == -1.0f, "OnPanOverrideCleared: the DAC channel's fixed Pan starts at -1.0 (hard left)");

    channels[0].Pan = 0.25f;
    channels[0].PanOverridden = true;
    channels[15].Pan = 0.25f;
    channels[15].PanOverridden = true;
    Check(channels[0].Pan == 0.25f && channels[15].Pan == 0.25f, "OnPanOverrideCleared: the override itself took effect");

    channels[0].PanOverridden = false;
    machine.OnPanOverrideCleared(0);
    channels[15].PanOverridden = false;
    machine.OnPanOverrideCleared(15);

    Check(channels[0].Pan == 1.0f,
          "OnPanOverrideCleared: ay1 ch0's Pan is immediately re-synced back to +1.0, not left stuck at the old "
          "override");
    Check(channels[15].Pan == -1.0f,
          "OnPanOverrideCleared: the DAC channel's Pan is immediately re-synced back to -1.0, not left stuck at "
          "the old override");
}

// ---------------------------------------------------------------------------
// ay1/ay2 RC filter (gyruss_ay_rc_filter.h): component values from MAME's
// gyruss.cpp - 1356 ohm series, 40 ohm switch, 0.047 uF / 0.22 uF.
// ---------------------------------------------------------------------------

namespace {

constexpr double kFilterTestRateHz = 1789773.0 / 8.0;

// Steady-state gain (output rms / input rms, in dB) of the filter for a sine
// at `freqHz` with the given switch setting.
double FilterGainDb(std::uint8_t switches, double freqHz) {
    GyrussAyRcFilter filter(kFilterTestRateHz);
    filter.SetSwitches(switches);
    constexpr std::size_t kSamples = 60000;
    constexpr std::size_t kSkip = 30000;
    std::vector<float> in(kSamples);
    const double w = 2.0 * 3.14159265358979323846 * freqHz / kFilterTestRateHz;
    for (std::size_t i = 0; i < kSamples; ++i) in[i] = static_cast<float>(0.5 * std::sin(w * static_cast<double>(i)));
    std::vector<float> out = in;
    filter.Process(out.data(), out.size());
    double sumIn = 0.0;
    double sumOut = 0.0;
    for (std::size_t i = kSkip; i < kSamples; ++i) {
        sumIn += static_cast<double>(in[i]) * in[i];
        sumOut += static_cast<double>(out[i]) * out[i];
    }
    return 10.0 * std::log10(sumOut / sumIn);
}

// Frequency where the gain crosses -3.01 dB, by bisection.
double FilterCutoffHz(std::uint8_t switches, double lowHz, double highHz) {
    for (int i = 0; i < 20; ++i) {
        const double mid = std::sqrt(lowHz * highHz);
        if (FilterGainDb(switches, mid) > -3.0103) lowHz = mid;
        else highHz = mid;
    }
    return std::sqrt(lowHz * highHz);
}

}  // namespace

void GyrussAyRcFilterCutoffsMatchComponentValues() {
    constexpr double kPi = 3.14159265358979323846;
    const double r = GyrussAyRcFilter::kSeriesResistanceOhms + GyrussAyRcFilter::kSwitchResistanceOhms;
    const double fcLow = 1.0 / (2.0 * kPi * r * GyrussAyRcFilter::kLowCapacitorFarads);    // 0.047 uF
    const double fcHigh = 1.0 / (2.0 * kPi * r * GyrussAyRcFilter::kHighCapacitorFarads);  // 0.22 uF

    // Nominal cut-offs from the component values: about 2.4 kHz and 518 Hz.
    Check(std::fabs(fcLow - 2426.0) < 5.0, "ay rc filter: 0.047 uF cut-off is about 2.4 kHz");
    Check(std::fabs(fcHigh - 518.0) < 2.0, "ay rc filter: 0.22 uF cut-off is about 518 Hz");

    // -3 dB at the cut-off the component values give (bit 0 = 0.047 uF, bit 1 = 0.22 uF).
    const double gainLow = FilterGainDb(1, fcLow);
    const double gainHigh = FilterGainDb(2, fcHigh);
    Check(std::fabs(gainLow + 3.01) < 0.15, "ay rc filter: 0.047 uF alone is -3 dB at its cut-off");
    Check(std::fabs(gainHigh + 3.01) < 0.15, "ay rc filter: 0.22 uF alone is -3 dB at its cut-off");

    // The measured -3 dB points land on the nominal 2.5 kHz / 533 Hz / 440 Hz within 5%.
    const double measLow = FilterCutoffHz(1, 1000.0, 5000.0);
    const double measHigh = FilterCutoffHz(2, 200.0, 1000.0);
    const double measBoth = FilterCutoffHz(3, 200.0, 1000.0);
    Check(std::fabs(measLow / 2497.0 - 1.0) < 0.05, "ay rc filter: bit 0 measured cut-off within 5% of 2.5 kHz");
    Check(std::fabs(measHigh / 533.0 - 1.0) < 0.05, "ay rc filter: bit 1 measured cut-off within 5% of 533 Hz");
    Check(std::fabs(measBoth / 440.0 - 1.0) < 0.05, "ay rc filter: both bits measured cut-off within 5% of 440 Hz");
    Check(std::fabs(measLow / fcLow - 1.0) < 0.01, "ay rc filter: bit 0 measured cut-off equals the component-value formula");
    Check(std::fabs(measHigh / fcHigh - 1.0) < 0.01, "ay rc filter: bit 1 measured cut-off equals the component-value formula");
    Check(measBoth < measHigh, "ay rc filter: both capacitors cut lower than either alone");

    // Well above the cut-off it is a low-pass (about -20 dB a decade, floor from the switch feed-through).
    Check(FilterGainDb(2, fcHigh * 10.0) < -18.0, "ay rc filter: one decade above the cut-off it is down about 20 dB");
    Check(FilterGainDb(1, fcLow * 0.1) > -0.1, "ay rc filter: one decade below the cut-off it passes");
}

void GyrussAyRcFilterBypassDcAndReset() {
    // No switch bit: the signal is not filtered at all.
    {
        GyrussAyRcFilter filter(kFilterTestRateHz);
        std::vector<float> in(2000);
        for (std::size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float>(std::sin(0.37 * static_cast<double>(i)) * 0.4);
        std::vector<float> out = in;
        filter.Process(out.data(), out.size());
        Check(out == in, "ay rc filter: no capacitor selected leaves the signal untouched");
    }
    // DC gain is 1 for every capacitor choice.
    for (std::uint8_t sw = 1; sw <= 3; ++sw) {
        GyrussAyRcFilter filter(kFilterTestRateHz);
        filter.SetSwitches(sw);
        std::vector<float> dc(200000, 0.5f);
        filter.Process(dc.data(), dc.size());
        Check(std::fabs(dc.back() - 0.5f) < 1e-4f, "ay rc filter: DC passes with gain 1");
    }
    // Reset puts the switches and the capacitors back to the start state.
    {
        GyrussAyRcFilter filter(kFilterTestRateHz);
        filter.SetSwitches(3);
        std::vector<float> noise(5000, 0.8f);
        filter.Process(noise.data(), noise.size());
        filter.Reset();
        Check(filter.Switches() == 0, "ay rc filter: Reset() switches every capacitor out");
        filter.SetSwitches(3);
        std::vector<float> zeros(100, 0.0f);
        filter.Process(zeros.data(), zeros.size());
        bool allZero = true;
        for (float v : zeros) allZero = allZero && v == 0.0f;
        Check(allZero, "ay rc filter: Reset() discharged the capacitors (zero in, zero out)");
    }
    // A capacitor that is switched out keeps its charge (MAME's per-capacitor state).
    {
        GyrussAyRcFilter filter(kFilterTestRateHz);
        filter.SetSwitches(1);
        std::vector<float> a(3000, 1.0f);
        filter.Process(a.data(), a.size());
        filter.SetSwitches(2);
        std::vector<float> b(10, 0.0f);
        filter.Process(b.data(), b.size());
        filter.SetSwitches(1);
        std::vector<float> c(1, 0.0f);
        filter.Process(c.data(), c.size());
        Check(c[0] > 0.9f, "ay rc filter: a switched-out capacitor keeps its charge");
        filter.ResetState();
        std::vector<float> d(1, 0.0f);
        filter.Process(d.data(), d.size());
        Check(d[0] == 0.0f, "ay rc filter: ResetState() empties the capacitors and keeps the switch setting");
    }
}

void GyrussMachineAyFilterFollowsPortB() {
    GyrussMachine machine(BuildSyntheticZ80Rom(), BuildSyntheticI8039Rom());
    machine.Initialize();

    // AY reset reports Port B as an input (0xFF): every channel starts with both capacitors in.
    for (int chip = 0; chip < 2; ++chip) {
        for (int ch = 0; ch < 3; ++ch) {
            Check(machine.AyFilterSwitchesForTest(chip, ch) == 3,
                  "port b: after reset ay1/ay2 channels start with both capacitors switched in");
        }
    }

    // ay1: make Port B an output, then write 0b00'01'10'11: channel A = both, B = 0.22 uF, C = 0.047 uF.
    Ay8910DeviceAdapter& ay1 = machine.Ay8910ForTest(0);
    ay1.WriteAddress(0x07);
    ay1.WriteData(0x80);  // Port B = output; its current (zero) value goes out
    Check(machine.AyFilterSwitchesForTest(0, 0) == 0, "port b: output mode with Port B = 0 switches every capacitor out");
    ay1.WriteAddress(0x0F);
    ay1.WriteData(0x1B);
    Check(machine.AyFilterSwitchesForTest(0, 0) == 3, "port b: bits 0-1 select channel A's capacitors");
    Check(machine.AyFilterSwitchesForTest(0, 1) == 2, "port b: bits 2-3 select channel B's capacitors");
    Check(machine.AyFilterSwitchesForTest(0, 2) == 1, "port b: bits 4-5 select channel C's capacitors");
    Check(machine.AyFilterSwitchesForTest(1, 0) == 3, "port b: ay2's filters are independent of ay1's Port B");
    Check(!machine.AyPortBValuesForTest(0).empty() && machine.AyPortBValuesForTest(0).back() == 0x1B,
          "port b: the last value ay1 saw is recorded");

    // Machine Reset() goes back to the start state.
    machine.Reset();
    for (int ch = 0; ch < 3; ++ch) {
        Check(machine.AyFilterSwitchesForTest(0, ch) == 3, "port b: Machine::Reset() restores the start-up filter setting");
    }
    Check(machine.AyPortBValuesForTest(0).size() == 1 && machine.AyPortBValuesForTest(0)[0] == 0xFF,
          "port b: Machine::Reset() starts the Port B history over");
}

// ---------------------------------------------------------------------------
// Mixer-output low-pass (gyruss_mixer_lowpass.h): MAME's gyruss.cpp mixers have a
// 200 ohm feedback resistor and a 0.1 uF feedback capacitor.
// ---------------------------------------------------------------------------

namespace {

double LowPassGainDb(double busOhms, double rateHz, double freqHz) {
    GyrussMixerLowPass lpf(busOhms, rateHz);
    constexpr std::size_t kSamples = 40000;
    constexpr std::size_t kSkip = 20000;
    std::vector<float> in(kSamples);
    const double w = 2.0 * 3.14159265358979323846 * freqHz / rateHz;
    for (std::size_t i = 0; i < kSamples; ++i) in[i] = static_cast<float>(0.5 * std::sin(w * static_cast<double>(i)));
    std::vector<float> out = in;
    lpf.Process(out.data(), out.size());
    double sumIn = 0.0;
    double sumOut = 0.0;
    for (std::size_t i = kSkip; i < kSamples; ++i) {
        sumIn += static_cast<double>(in[i]) * in[i];
        sumOut += static_cast<double>(out[i]) * out[i];
    }
    return 10.0 * std::log10(sumOut / sumIn);
}

}  // namespace

void GyrussMixerLowPassMatchesMixerDescription() {
    const double right = GyrussMixerLowPass::RightBusOhms();
    const double left = GyrussMixerLowPass::LeftBusOhms();
    const double fcRight = GyrussMixerLowPass::CutoffHz(right);
    const double fcLeft = GyrussMixerLowPass::CutoffHz(left);
    Check(std::fabs(right - 122.2) < 0.1 && std::fabs(left - 133.6) < 0.1,
          "mixer low-pass: bus resistances are 200 ohm in parallel with the bus inputs (122.2 / 133.6 ohm)");
    Check(std::fabs(fcRight - 13020.0) < 30.0, "mixer low-pass: right bus corner is about 13.0 kHz");
    Check(std::fabs(fcLeft - 11910.0) < 30.0, "mixer low-pass: left bus corner is about 11.9 kHz");

    // -3 dB at the corner, at the AY output rate and at the 192 kHz DAC rate.
    const double ayRate = 1789773.0 / 8.0;
    Check(std::fabs(LowPassGainDb(right, ayRate, fcRight) + 3.01) < 0.3, "mixer low-pass: right bus is -3 dB at its corner (AY rate)");
    Check(std::fabs(LowPassGainDb(left, ayRate, fcLeft) + 3.01) < 0.3, "mixer low-pass: left bus is -3 dB at its corner (AY rate)");
    Check(std::fabs(LowPassGainDb(left, 192000.0, fcLeft) + 3.01) < 0.3, "mixer low-pass: left bus is -3 dB at its corner (DAC rate)");

    // First-order shape, checked with a lower corner (about 1 kHz) so a decade above is below Nyquist.
    const double slowOhms = 1.0 / (2.0 * 3.14159265358979323846 * 1000.0 * GyrussMixerLowPass::kFeedbackFarads);
    Check(std::fabs(LowPassGainDb(slowOhms, ayRate, 1000.0) + 3.01) < 0.1, "mixer low-pass: -3 dB at the corner of a 1 kHz bus");
    Check(std::fabs(LowPassGainDb(slowOhms, ayRate, 10000.0) + 20.0) < 0.6, "mixer low-pass: about -20 dB one decade above the corner");
    Check(LowPassGainDb(slowOhms, ayRate, 100.0) > -0.1, "mixer low-pass: passes one decade below the corner");

    // DC gain 1.
    GyrussMixerLowPass lpf(left, ayRate);
    std::vector<float> dc(4000, 0.5f);
    lpf.Process(dc.data(), dc.size());
    Check(std::fabs(dc.back() - 0.5f) < 1e-5f, "mixer low-pass: DC passes with gain 1");
}

void GyrussMixerLowPassResetClearsState() {
    GyrussMixerLowPass lpf(GyrussMixerLowPass::RightBusOhms(), 1789773.0 / 8.0);
    std::vector<float> loud(2000, 0.9f);
    lpf.Process(loud.data(), loud.size());
    lpf.Reset();
    std::vector<float> zeros(100, 0.0f);
    lpf.Process(zeros.data(), zeros.size());
    bool allZero = true;
    for (float v : zeros) allZero = allZero && v == 0.0f;
    Check(allZero, "mixer low-pass: Reset() empties the filter state (zero in, zero out)");
}