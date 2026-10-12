// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "default_machine_catalog.h"

#include "devices/dataeast/vaportra/vapor_trail_machine.h"
#include "devices/dataeast/vaportra/vapor_trail_rom_layout.h"
#include "devices/konami/bubblesystem/bubble_system_machine.h"
#include "devices/konami/bubblesystem/bubble_system_rom_layout.h"
#include "devices/konami/gradius/gradius_machine.h"
#include "devices/konami/gradius/gradius_rom_layout.h"
#include "devices/konami/gyruss/gyruss_machine.h"
#include "devices/konami/gyruss/gyruss_rom_layout.h"
#include "devices/namco/dragonspirit/dragon_spirit_machine.h"
#include "devices/namco/dragonspirit/dragon_spirit_rom_layout.h"
#include "devices/sega/outrun/outrun_machine.h"
#include "devices/sega/outrun/outrun_rom_layout.h"
#include "devices/sega/xboard/xboard_machine.h"
#include "devices/sega/xboard/xboard_rom_layout.h"
#include "devices/capcom/sf/sf_machine.h"
#include "devices/capcom/sf/sf_rom_layout.h"
#include "devices/taito/darius/darius_machine.h"
#include "devices/taito/darius/darius_rom_layout.h"
#include "devices/taito/metalblk/metalblack_rom_layout.h"
#include "devices/taito/ninjaw/ninja_warriors_machine.h"
#include "devices/taito/ninjaw/ninja_warriors_rom_layout.h"
#include "test_machine.h"

namespace {

MachineRegistry BuildDefaultRegistry() {
    MachineRegistry registry;

    MachineDefinition test_machine_def;
    test_machine_def.Id = MachineId("TEST_MACHINE");
    test_machine_def.DisplayName = "Test Machine (synthetic, no ROM)";
    test_machine_def.Category = MachineCategory::Arcade;
    test_machine_def.Manufacturer = "Chipestra (synthetic)";
    test_machine_def.Variants.push_back(MachineVariant{VariantId("STANDARD"), "Standard"});
    // No RequiredResources: TestMachine needs no ROM by design.
    test_machine_def.Factory = [](const VariantId&, const MachineResources&) -> std::unique_ptr<IMachine> {
        return std::make_unique<TestMachine>();
    };
    registry.Register(std::move(test_machine_def));

    MachineDefinition xboard_def;
    xboard_def.Id = MachineId("SEGA_XBOARD");
    xboard_def.DisplayName = "X-Board";
    xboard_def.Category = MachineCategory::Arcade;
    xboard_def.Manufacturer = "SEGA";
    xboard_def.Variants.push_back(MachineVariant{VariantId("XBOARD_BASE"), "Base"});
    // Sound ROMs are declared by the game XML, not the Machine; the resolved resources
    // arrive through `resources`. With an empty `resources` this still constructs a
    // not-yet-playable XBoardMachine (ROM buffers left empty).
    xboard_def.Factory = [](const VariantId&, const MachineResources& resources) -> std::unique_ptr<IMachine> {
        XBoardSoundRoms roms = AssembleXBoardSoundRoms(resources);
        return std::make_unique<XBoardMachine>(std::move(roms.Z80Program), std::move(roms.SegaPcmSamples));
    };
    registry.Register(std::move(xboard_def));

    MachineDefinition outrun_def;
    outrun_def.Id = MachineId("SEGA_OUTRUN");
    outrun_def.DisplayName = "Out Run";
    outrun_def.Category = MachineCategory::Arcade;
    outrun_def.Manufacturer = "SEGA";
    outrun_def.Variants.push_back(MachineVariant{VariantId("OUTRUN_BASE"), "Base"});
    // Same pattern as SEGA_XBOARD: the game XML declares the resources
    // ("soundcpu"/"pcm1".."pcm6"); an empty `resources` constructs a
    // not-yet-playable OutRunMachine.
    outrun_def.Factory = [](const VariantId&, const MachineResources& resources) -> std::unique_ptr<IMachine> {
        OutRunSoundRoms roms = AssembleOutRunSoundRoms(resources);
        return std::make_unique<OutRunMachine>(std::move(roms.Z80Program), std::move(roms.SegaPcmSamples));
    };
    registry.Register(std::move(outrun_def));

    MachineDefinition gradius_def;
    gradius_def.Id = MachineId("KONAMI_GRADIUS_BUBBLESYSTEM");
    // "GX400" names the Mask ROM/GX400 revision this Machine targets, as opposed to the
    // original Bubble System revision. The MachineId is kept as is.
    gradius_def.DisplayName = "GX400";
    gradius_def.Category = MachineCategory::Arcade;
    gradius_def.Manufacturer = "Konami";
    gradius_def.Variants.push_back(MachineVariant{VariantId("GRADIUS_BASE"), "Base"});
    // The game XML declares "soundcpu"/"wave0"/"wave1" and the main 68000 ROMs
    // ("maincpu_hi"/"maincpu_lo"/"maindata_hi"/"maindata_lo"). An entirely empty
    // `resources` constructs a not-yet-playable GradiusMachine. A non-empty `resources`
    // missing the main-CPU names makes AssembleGradiusMainCpuRoms() throw
    // std::runtime_error (an incomplete game XML must fail loudly), which
    // chipestra_load_machine turns into CHIPESTRA_E_INTERNAL.
    gradius_def.Factory = [](const VariantId&, const MachineResources& resources) -> std::unique_ptr<IMachine> {
        GradiusSoundRoms roms = AssembleGradiusSoundRoms(resources);
        GradiusMainCpuRoms mainRoms = AssembleGradiusMainCpuRoms(resources);
        return std::make_unique<GradiusMachine>(std::move(roms.Z80Program), std::move(roms.K005289WaveformProm),
                                                  std::move(mainRoms.MainProgram), std::move(mainRoms.MainData));
    };
    registry.Register(std::move(gradius_def));

    MachineDefinition bubblesystem_def;
    bubblesystem_def.Id = MachineId("KONAMI_BUBBLESYSTEM_GRADIUS");
    // Distinct from KONAMI_GRADIUS_BUBBLESYSTEM (the GX400/Mask ROM revision): this is the
    // real Bubble System mainboard (BubbleSystemMachine). The display name is the board
    // itself (K005297 bubble memory controller; many game cartridges share it), not a game.
    bubblesystem_def.DisplayName = "Bubble System";
    bubblesystem_def.Category = MachineCategory::Arcade;
    bubblesystem_def.Manufacturer = "Konami";
    bubblesystem_def.Variants.push_back(MachineVariant{VariantId("BUBBLESYSTEM_GRADIUS_BASE"), "Base"});
    // BIOS-only: the bare mainboard in its bubble-memory warm-up phase - no cartridge,
    // no 68000 execution; the BIOS sound Z80 alone plays the countdown and morning music.
    // A variant of the same Machine, not a new MachineId.
    bubblesystem_def.Variants.push_back(MachineVariant{VariantId("BUBBLESYSTEM_BIOS_BASE"), "BIOS only (no cartridge)"});
    // The game XML declares "soundcpu"/"wave0"/"wave1" plus "bootcode"/"bubblememory"
    // (main-CPU side). AssembleBubbleSystemMainCpuRoms() throws std::runtime_error if
    // `resources` is non-empty but missing "bootcode"/"bubblememory".
    bubblesystem_def.Factory = [](const VariantId& variant, const MachineResources& resources) -> std::unique_ptr<IMachine> {
        BubbleSystemSoundRoms soundRoms = AssembleBubbleSystemSoundRoms(resources);
        if (variant.Value() == "BUBBLESYSTEM_BIOS_BASE") {
            // No boot program / cartridge: the main 68000 never runs in this configuration
            // (BubbleSystemBoardMode::BiosOnly), so those resources are neither needed
            // nor validated.
            return std::make_unique<BubbleSystemMachine>(std::move(soundRoms.Z80Program),
                                                           std::move(soundRoms.K005289WaveformProm),
                                                           std::vector<std::uint8_t>{}, std::vector<std::uint8_t>{},
                                                           BubbleSystemBoardMode::BiosOnly);
        }
        BubbleSystemMainCpuRoms mainRoms = AssembleBubbleSystemMainCpuRoms(resources);
        return std::make_unique<BubbleSystemMachine>(std::move(soundRoms.Z80Program),
                                                       std::move(soundRoms.K005289WaveformProm),
                                                       std::move(mainRoms.BootCode), std::move(mainRoms.BubbleMemory));
    };
    registry.Register(std::move(bubblesystem_def));

    MachineDefinition gyruss_def;
    gyruss_def.Id = MachineId("KONAMI_GYRUSS");
    gyruss_def.DisplayName = "Gyruss";
    gyruss_def.Category = MachineCategory::Arcade;
    gyruss_def.Manufacturer = "Konami";
    gyruss_def.Variants.push_back(MachineVariant{VariantId("GYRUSS_BASE"), "Base"});
    // The game XML declares "soundcpu"/"audio2"; an entirely empty `resources`
    // constructs a not-yet-playable GyrussMachine.
    gyruss_def.Factory = [](const VariantId&, const MachineResources& resources) -> std::unique_ptr<IMachine> {
        GyrussSoundRoms roms = AssembleGyrussSoundRoms(resources);
        return std::make_unique<GyrussMachine>(std::move(roms.Z80Program), std::move(roms.I8039Program));
    };
    registry.Register(std::move(gyruss_def));

    MachineDefinition darius_def;
    darius_def.Id = MachineId("TAITO_DARIUS");
    darius_def.DisplayName = "Darius";
    darius_def.Category = MachineCategory::Arcade;
    darius_def.Manufacturer = "Taito";
    darius_def.Variants.push_back(MachineVariant{VariantId("DARIUS_BASE"), "Base"});
    // The game XML declares "soundcpu"/"adpcm"; an empty `resources` constructs a
    // not-yet-playable DariusMachine.
    darius_def.Factory = [](const VariantId&, const MachineResources& resources) -> std::unique_ptr<IMachine> {
        DariusSoundRoms roms = AssembleDariusSoundRoms(resources);
        return std::make_unique<DariusMachine>(std::move(roms.AudioCpuProgram), std::move(roms.AdpcmProgram));
    };
    registry.Register(std::move(darius_def));

    MachineDefinition sf_def;
    sf_def.Id = MachineId("CAPCOM_68000");
    sf_def.DisplayName = "68000 Based";
    sf_def.Category = MachineCategory::Arcade;
    sf_def.Manufacturer = "Capcom";
    sf_def.Variants.push_back(MachineVariant{VariantId("SF_BASE"), "Base"});
    // The game XML declares "soundcpu"/"samples0"/"samples1"; an empty `resources` constructs a
    // not-yet-playable SfMachine.
    sf_def.Factory = [](const VariantId&, const MachineResources& resources) -> std::unique_ptr<IMachine> {
        SfSoundRoms roms = AssembleSfSoundRoms(resources);
        return std::make_unique<SfMachine>(std::move(roms.AudioCpuProgram), std::move(roms.SamplesProgram));
    };
    registry.Register(std::move(sf_def));

    MachineDefinition ninjaw_def;
    ninjaw_def.Id = MachineId("TAITO_NINJAWARRIORS");
    ninjaw_def.DisplayName = "The Ninja Warriors";
    ninjaw_def.Category = MachineCategory::Arcade;
    ninjaw_def.Manufacturer = "Taito";
    ninjaw_def.Variants.push_back(MachineVariant{VariantId("NINJAWARRIORS_BASE"), "Base"});
    // The game XML declares "soundcpu"/"adpcma1"/"adpcma2"/"adpcma3"/"adpcmb"; an empty
    // `resources` constructs a not-yet-playable NinjaWarriorsMachine.
    ninjaw_def.Factory = [](const VariantId&, const MachineResources& resources) -> std::unique_ptr<IMachine> {
        NinjaWarriorsSoundRoms roms = AssembleNinjaWarriorsSoundRoms(resources);
        return std::make_unique<NinjaWarriorsMachine>(std::move(roms.AudioCpuProgram), std::move(roms.AdpcmASamples),
                                                       std::move(roms.AdpcmBSamples));
    };
    registry.Register(std::move(ninjaw_def));

    MachineDefinition metalblack_def;
    // "TAITO_F2SYSTEM" names the shared board family (Taito F2 System), not one game:
    // several games' <game machine="..."> XML can point at it, and the Library groups
    // them by DisplayName.
    metalblack_def.Id = MachineId("TAITO_F2SYSTEM");
    metalblack_def.DisplayName = "F2 System";
    metalblack_def.Category = MachineCategory::Arcade;
    metalblack_def.Manufacturer = "Taito";
    metalblack_def.Variants.push_back(MachineVariant{VariantId("METALBLACK_BASE"), "Base"});
    // Metal Black (Taito F2 System) is played through an unmodified NinjaWarriorsMachine:
    // its sound board is wired identically. Only the ROM assembly differs (ADPCM-A is one
    // 1MB file, not three 512KB files).
    metalblack_def.Factory = [](const VariantId&, const MachineResources& resources) -> std::unique_ptr<IMachine> {
        NinjaWarriorsSoundRoms roms = AssembleMetalBlackSoundRoms(resources);
        return std::make_unique<NinjaWarriorsMachine>(std::move(roms.AudioCpuProgram),
                                                       std::move(roms.AdpcmASamples), std::move(roms.AdpcmBSamples));
    };
    registry.Register(std::move(metalblack_def));

    MachineDefinition vaportrail_def;
    vaportrail_def.Id = MachineId("DATAEAST_VAPORTRAIL");
    vaportrail_def.DisplayName = "Vapor Trail";
    vaportrail_def.Category = MachineCategory::Arcade;
    vaportrail_def.Manufacturer = "Data East";
    vaportrail_def.Variants.push_back(MachineVariant{VariantId("VAPORTRAIL_BASE"), "Base"});
    // The game XML declares "soundcpu"/"oki1"/"oki2"; an entirely empty `resources`
    // constructs a not-yet-playable VaporTrailMachine.
    vaportrail_def.Factory = [](const VariantId&, const MachineResources& resources) -> std::unique_ptr<IMachine> {
        VaporTrailSoundRoms roms = AssembleVaporTrailSoundRoms(resources);
        return std::make_unique<VaporTrailMachine>(std::move(roms.SoundCpuProgram), std::move(roms.Oki1Program),
                                                    std::move(roms.Oki2Program));
    };
    registry.Register(std::move(vaportrail_def));

    MachineDefinition dragonspirit_def;
    // Board-based id: Namco System 1 is shared by many titles beyond Dragon Spirit, so the
    // MachineId/DisplayName name the board. A further System 1 title's XML only needs
    // machine="NAMCO_SYSTEM1" variant="SYSTEM1_BASE" and its own soundcpu0/soundcpu1 resources.
    dragonspirit_def.Id = MachineId("NAMCO_SYSTEM1");
    dragonspirit_def.DisplayName = "System 1";
    dragonspirit_def.Category = MachineCategory::Arcade;
    dragonspirit_def.Manufacturer = "Namco";
    dragonspirit_def.Variants.push_back(MachineVariant{VariantId("SYSTEM1_BASE"), "Base"});
    // The game XML declares "soundcpu0"/"soundcpu1"; an entirely empty `resources`
    // constructs a not-yet-playable DragonSpiritMachine.
    dragonspirit_def.Factory = [](const VariantId&, const MachineResources& resources) -> std::unique_ptr<IMachine> {
        DragonSpiritSoundRoms roms = AssembleDragonSpiritSoundRoms(resources);
        return std::make_unique<DragonSpiritMachine>(std::move(roms.SoundCpuProgram), std::move(roms.McuProgram),
                                                      std::move(roms.VoiceRom));
    };
    registry.Register(std::move(dragonspirit_def));

    return registry;
}

}  // namespace

MachineRegistry& DefaultMachineRegistry() {
    static MachineRegistry registry = BuildDefaultRegistry();
    return registry;
}
