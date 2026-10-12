# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tom Water Hill
cmake_minimum_required(VERSION 3.24)
# C is needed for the vendored miniz tinfl module (thirdparty/miniz).
project(ChipestraNative LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CHIPESTRA_CORE_SOURCES
    src/test_machine.h
    src/test_machine.cpp
    src/clock.h
    src/clock.cpp
    src/scheduler.h
    src/scheduler.cpp
    src/address_space.h
    src/address_space.cpp
    src/memory_region.h
    src/memory_region.cpp
    src/device_manager.h
    src/device_manager.cpp
    src/logical_channel_sink.cpp
    src/sample_rate_converter.h
    src/sample_rate_converter.cpp
    src/soxr_sample_rate_converter.h
    src/soxr_sample_rate_converter.cpp
    src/resampling_mixer_feed.h
    src/resampling_mixer_feed.cpp
    src/mixer.h
    src/mixer.cpp
    src/audio_output.h
    src/null_audio_output.h
    src/null_audio_output.cpp
    src/bounded_frame_queue.h
    src/bounded_frame_queue.cpp
    src/wasapi_shared_output.h
    src/wasapi_shared_output.cpp
    src/endpoint_resampler.h
    src/endpoint_resampler.cpp
    src/playback_pump.h
    src/playback_pump.cpp
    src/machine_registry.h
    src/machine_registry.cpp
    src/default_machine_catalog.h
    src/default_machine_catalog.cpp
    src/loose_file_index.h
    src/loose_file_index.cpp
    src/resource_hash.h
    src/resource_hash.cpp
    src/zip_index.h
    src/zip_index.cpp
    src/resource_resolver.h
    src/resource_resolver.cpp
    src/game_definition.h
    src/game_definition.cpp
    src/xml_definition_parser.h
    src/xml_definition_parser.cpp
    src/devices/cpu/z80/iz80_core.h
    src/devices/cpu/z80/z80_registers.h
    src/devices/cpu/z80/mame_derived_z80_adapter.h
    src/devices/cpu/z80/mame_derived_z80_adapter.cpp
    src/devices/cpu/m68000/im68000_core.h
    src/devices/cpu/m68000/m68000_registers.h
    src/devices/cpu/m68000/mame_derived_m68000_adapter.h
    src/devices/cpu/m68000/mame_derived_m68000_adapter.cpp
    src/devices/cpu/i8039/i8039_core.h
    src/devices/cpu/i8039/mame_derived_i8039_adapter.h
    src/devices/cpu/i8039/mame_derived_i8039_adapter.cpp
    src/devices/cpu/h6280/ih6280_core.h
    src/devices/cpu/h6280/mame_derived_h6280_adapter.h
    src/devices/cpu/h6280/mame_derived_h6280_adapter.cpp
    src/devices/cpu/m6809/im6809_core.h
    src/devices/cpu/m6809/mame_derived_m6809_adapter.h
    src/devices/cpu/m6809/mame_derived_m6809_adapter.cpp
    src/devices/cpu/hd63701/ihd63701_core.h
    src/devices/cpu/hd63701/mame_derived_hd63701_adapter.h
    src/devices/cpu/hd63701/mame_derived_hd63701_adapter.cpp
    src/devices/konami/gyruss/gyruss_sound_memory_map.h
    src/devices/konami/gyruss/gyruss_sound_memory_map.cpp
    src/devices/konami/gyruss/gyruss_command_interface.h
    src/devices/konami/gyruss/gyruss_command_interface.cpp
    src/devices/konami/gyruss/gyruss_machine.h
    src/devices/konami/gyruss/gyruss_machine.cpp
    src/devices/konami/gyruss/gyruss_rom_layout.h
    src/devices/konami/gyruss/gyruss_rom_layout.cpp
    src/devices/sega/xboard/xboard_memory_map.h
    src/devices/sega/xboard/xboard_memory_map.cpp
    src/devices/sega/xboard/xboard_command_interface.h
    src/devices/sega/xboard/xboard_command_interface.cpp
    src/devices/sega/xboard/xboard_machine.h
    src/devices/sega/xboard/xboard_machine.cpp
    src/devices/sega/xboard/xboard_rom_layout.h
    src/devices/sega/xboard/xboard_rom_layout.cpp
    src/devices/sega/outrun/outrun_memory_map.h
    src/devices/sega/outrun/outrun_memory_map.cpp
    src/devices/sega/outrun/outrun_command_interface.h
    src/devices/sega/outrun/outrun_command_interface.cpp
    src/devices/sega/outrun/outrun_machine.h
    src/devices/sega/outrun/outrun_machine.cpp
    src/devices/sega/outrun/outrun_rom_layout.h
    src/devices/sega/outrun/outrun_rom_layout.cpp
    src/devices/sound/ym2151/ym2151_device_adapter.h
    src/devices/sound/ym2151/ym2151_device_adapter.cpp
    src/devices/sound/ym2203/ym2203_device_adapter.h
    src/devices/sound/ym2203/ym2203_device_adapter.cpp
    src/devices/sound/msm5205/msm5205_device_adapter.h
    src/devices/sound/msm5205/msm5205_device_adapter.cpp
    src/devices/sound/segapcm/segapcm_device_adapter.h
    src/devices/sound/segapcm/segapcm_device_adapter.cpp
    src/devices/sound/ay8910/ay8910_device_adapter.h
    src/devices/sound/ay8910/ay8910_device_adapter.cpp
    src/devices/sound/k005289/k005289_device_adapter.h
    src/devices/sound/k005289/k005289_device_adapter.cpp
    src/devices/sound/vlm5030/vlm5030_device_adapter.h
    src/devices/sound/vlm5030/vlm5030_device_adapter.cpp
    src/devices/sound/ym2610/ym2610_device_adapter.h
    src/devices/sound/ym2610/ym2610_device_adapter.cpp
    src/devices/sound/okim6295/okim6295_device_adapter.h
    src/devices/sound/okim6295/okim6295_device_adapter.cpp
    src/devices/sound/cus30/cus30_device_adapter.h
    src/devices/sound/cus30/cus30_device_adapter.cpp
    src/devices/sound/dac8bitr2r/dac8bitr2r_device_adapter.h
    src/devices/sound/dac8bitr2r/dac8bitr2r_device_adapter.cpp
    src/devices/namco/dragonspirit/dragon_spirit_mcu_memory_map.h
    src/devices/namco/dragonspirit/dragon_spirit_mcu_memory_map.cpp
    src/devices/konami/gradius/gradius_memory_map.h
    src/devices/konami/gradius/gradius_memory_map.cpp
    src/devices/konami/gradius/gradius_main_memory_map.h
    src/devices/konami/gradius/gradius_main_memory_map.cpp
    src/devices/konami/gradius/gradius_command_interface.h
    src/devices/konami/gradius/gradius_command_interface.cpp
    src/devices/konami/gradius/gradius_machine.h
    src/devices/konami/gradius/gradius_machine.cpp
    src/devices/konami/gradius/gradius_rom_layout.h
    src/devices/konami/gradius/gradius_rom_layout.cpp
    src/devices/konami/bubblesystem/bubble_system_main_memory_map.h
    src/devices/konami/bubblesystem/bubble_system_main_memory_map.cpp
    src/devices/konami/bubblesystem/bubble_system_machine.h
    src/devices/konami/bubblesystem/bubble_system_machine.cpp
    src/devices/konami/bubblesystem/bubble_system_rom_layout.h
    src/devices/konami/bubblesystem/bubble_system_rom_layout.cpp
    src/devices/capcom/sf/sf_memory_map.h
    src/devices/capcom/sf/sf_memory_map.cpp
    src/devices/capcom/sf/sf_rom_layout.h
    src/devices/capcom/sf/sf_rom_layout.cpp
    src/devices/capcom/sf/sf_machine.h
    src/devices/capcom/sf/sf_machine.cpp
    src/devices/taito/darius/darius_command_interface.h
    src/devices/taito/darius/darius_command_interface.cpp
    src/devices/taito/darius/darius_sound_memory_map.h
    src/devices/taito/darius/darius_sound_memory_map.cpp
    src/devices/taito/darius/darius_adpcm_sound_map.h
    src/devices/taito/darius/darius_adpcm_sound_map.cpp
    src/devices/taito/darius/darius_rom_layout.h
    src/devices/taito/darius/darius_rom_layout.cpp
    src/devices/taito/darius/darius_machine.h
    src/devices/taito/darius/darius_machine.cpp
    src/devices/taito/ninjaw/ninja_warriors_command_interface.h
    src/devices/taito/ninjaw/ninja_warriors_command_interface.cpp
    src/devices/taito/ninjaw/ninja_warriors_sound_memory_map.h
    src/devices/taito/ninjaw/ninja_warriors_sound_memory_map.cpp
    src/devices/taito/ninjaw/ninja_warriors_rom_layout.h
    src/devices/taito/ninjaw/ninja_warriors_rom_layout.cpp
    src/devices/taito/ninjaw/ninja_warriors_machine.h
    src/devices/taito/ninjaw/ninja_warriors_machine.cpp
    src/devices/taito/metalblk/metalblack_rom_layout.h
    src/devices/taito/metalblk/metalblack_rom_layout.cpp
    src/devices/dataeast/vaportra/vapor_trail_memory_map.h
    src/devices/dataeast/vaportra/vapor_trail_memory_map.cpp
    src/devices/dataeast/vaportra/vapor_trail_command_interface.h
    src/devices/dataeast/vaportra/vapor_trail_command_interface.cpp
    src/devices/dataeast/vaportra/vapor_trail_machine.h
    src/devices/dataeast/vaportra/vapor_trail_machine.cpp
    src/devices/dataeast/vaportra/vapor_trail_rom_layout.h
    src/devices/dataeast/vaportra/vapor_trail_rom_layout.cpp
    src/devices/namco/dragonspirit/dragon_spirit_memory_map.h
    src/devices/namco/dragonspirit/dragon_spirit_memory_map.cpp
    src/devices/namco/dragonspirit/dragon_spirit_command_interface.h
    src/devices/namco/dragonspirit/dragon_spirit_command_interface.cpp
    src/devices/namco/dragonspirit/dragon_spirit_machine.h
    src/devices/namco/dragonspirit/dragon_spirit_machine.cpp
    src/devices/namco/dragonspirit/dragon_spirit_rom_layout.h
    src/devices/namco/dragonspirit/dragon_spirit_rom_layout.cpp
)

# Vendored miniz tinfl module (thirdparty/miniz) - raw DEFLATE
# decompression only, used by ZipIndex::ReadEntry(). MIT license, recorded
# in THIRD-PARTY-NOTICES.md (full text: licenses/MIT.txt).
#
# miniz.h (the aggregator header) unconditionally pulls in miniz_tdef.h and
# miniz_zip.h too, so those are vendored alongside it for a complete,
# pristine copy - but only miniz_tinfl.c is compiled below. tdefl/zip
# functions are declared, never defined or called, so they add no linked
# code and no runtime attack surface.
set(CHIPESTRA_THIRDPARTY_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/miniz/miniz.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/miniz/miniz_common.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/miniz/miniz_export.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/miniz/miniz_tdef.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/miniz/miniz_tinfl.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/miniz/miniz_zip.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/miniz/miniz_tinfl.c
)

# Vendored pugixml (thirdparty/pugixml) - parsing only; DTD/external-
# entity handling and size limits are Chipestra's own code
# (xml_definition_parser.cpp), not delegated to the library. MIT license,
# recorded in THIRD-PARTY-NOTICES.md (full text: licenses/MIT.txt).
set(CHIPESTRA_PUGIXML_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/pugixml/pugiconfig.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/pugixml/pugixml.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/pugixml/pugixml.cpp
)
list(APPEND CHIPESTRA_THIRDPARTY_SOURCES ${CHIPESTRA_PUGIXML_SOURCES})

# Vendored ymfm OPM (YM2151) FM core (thirdparty/ymfm) - the FM synthesis
# math only. Timers, IRQ line, and audio routing are Chipestra's own code
# (devices/sound/ym2151/ym2151_device_adapter.cpp) behind ymfm::ymfm_interface,
# not delegated to the library. BSD-3-Clause, recorded in
# THIRD-PARTY-NOTICES.md.
#
# ymfm_opm.cpp is the only compiled translation unit; ymfm_fm.ipp is #included
# by it. Other ymfm chip families (opl/pcm) are not vendored.
set(CHIPESTRA_YMFM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_fm.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_fm.ipp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_opm.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_opm.cpp
)
list(APPEND CHIPESTRA_THIRDPARTY_SOURCES ${CHIPESTRA_YMFM_SOURCES})

# Vendored ymfm OPN (YM2203/YM2610) FM+SSG core (thirdparty/ymfm, see
# THIRD-PARTY-NOTICES.md). ymfm_opn.h/.cpp is a single upstream translation unit
# shared by the OPN family and is vendored unmodified; ymfm_adpcm.h/.cpp are needed
# because the YM2610 code in that file references the ADPCM-A/B engine types.
# ymfm_ssg.h/.cpp is the onboard SSG (AY-3-8910-compatible PSG) engine.
set(CHIPESTRA_YMFM_OPN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_adpcm.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_adpcm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_ssg.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_ssg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_opn.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_opn.cpp
)
list(APPEND CHIPESTRA_THIRDPARTY_SOURCES ${CHIPESTRA_YMFM_OPN_SOURCES})

if (MSVC)
    # Vendored code: don't hold it to our own /W4 warning level.
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/miniz/miniz_tinfl.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/pugixml/pugixml.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_opm.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_opn.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/ymfm/ymfm_ssg.cpp
        PROPERTIES COMPILE_OPTIONS "/W1")
endif()

# libsoxr (vcpkg manifest mode - vcpkg.json in this directory,
# THIRD-PARTY-NOTICES.md, LGPL-2.1-or-later, DYNAMIC linking). Every target that
# links CHIPESTRA_CORE_SOURCES needs it, so it is resolved once, globally,
# instead of per add_executable().
find_path(SOXR_INCLUDE_DIR soxr.h REQUIRED)
find_library(SOXR_LIBRARY NAMES soxr REQUIRED)
include_directories(${SOXR_INCLUDE_DIR})
link_libraries(${SOXR_LIBRARY})

add_library(chipestra_native SHARED
    include/chipestra_abi.h
    src/chipestra_api.cpp
    ${CHIPESTRA_CORE_SOURCES}
    ${CHIPESTRA_THIRDPARTY_SOURCES}
)

target_include_directories(chipestra_native PUBLIC include)
target_include_directories(chipestra_native PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty)
# PUGIXML_NO_XPATH: XPath is unused (attribute/child lookups only) - compile
# it out to shrink the vendored surface.
target_compile_definitions(chipestra_native PRIVATE CHIPESTRA_BUILD_DLL MINIZ_STATIC_DEFINE PUGIXML_NO_XPATH)

if (WIN32)
    # WasapiSharedOutput: CoCreateInstance/CoInitializeEx/CoTaskMemFree.
    # bcrypt: Sha1Hex via Windows CNG (resource_hash.cpp).
    target_link_libraries(chipestra_native PRIVATE ole32 bcrypt)
endif()

if (MSVC)
    # /utf-8: the sources are UTF-8; without it MSVC guesses the encoding from the
    # system code page, which is unreliable (C4819) on non-UTF-8 locales.
    target_compile_options(chipestra_native PRIVATE /W4 /EHsc /utf-8)
endif()

# soxr.dll has to sit next to any executable that runs chipestra_native's code,
# and vcpkg's "applocal" deployment does not trigger for this project. Every
# executable lands in the same build/native/<config>/ directory, so copying the DLL
# once, right after chipestra_native builds, is enough for all of them.
get_filename_component(SOXR_VCPKG_INSTALLED_DIR ${SOXR_INCLUDE_DIR} DIRECTORY)
add_custom_command(TARGET chipestra_native POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${SOXR_VCPKG_INSTALLED_DIR}/debug/bin/soxr.dll,${SOXR_VCPKG_INSTALLED_DIR}/bin/soxr.dll>"
        "$<TARGET_FILE_DIR:chipestra_native>"
    COMMENT "Copying soxr.dll next to chipestra_native's output"
)

# The managed projects load the DLL from this one well-known location.
set(CHIPESTRA_ARTIFACTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../artifacts/native")

add_custom_command(TARGET chipestra_native POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CHIPESTRA_ARTIFACTS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:chipestra_native>" "${CHIPESTRA_ARTIFACTS_DIR}/"
    # soxr.dll must sit next to chipestra_native.dll for the Managed test projects
    # (they stage both from here) - otherwise a fresh checkout fails to load
    # chipestra_native with DllNotFoundException.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${SOXR_VCPKG_INSTALLED_DIR}/debug/bin/soxr.dll,${SOXR_VCPKG_INSTALLED_DIR}/bin/soxr.dll>"
        "${CHIPESTRA_ARTIFACTS_DIR}/"
)

# Native-only lifecycle/determinism tests. Compiles the implementation sources
# directly rather than linking chipestra_native.dll, so it is a self-contained
# executable. Run by the Build gate (tools/verify.ps1).
add_executable(chipestra_native_tests
    src/chipestra_api.cpp
    ${CHIPESTRA_CORE_SOURCES}
    ${CHIPESTRA_THIRDPARTY_SOURCES}
    tests/native_lifecycle_tests.cpp
    tests/resource_resolver_tests.cpp
    tests/game_definition_tests.cpp
    tests/z80_adapter_tests.cpp
    tests/m68000_adapter_tests.cpp
    tests/i8039_adapter_tests.cpp
    tests/h6280_adapter_tests.cpp
    tests/m6809_adapter_tests.cpp
    tests/hd63701_adapter_tests.cpp
    tests/gyruss_machine_tests.cpp
    tests/bubble_system_bios_tests.cpp
    tests/gyruss_reference_tests.cpp
    tests/xboard_machine_tests.cpp
    tests/outrun_machine_tests.cpp
    tests/outrun_reference_tests.cpp
    tests/ym2151_adapter_tests.cpp
    tests/segapcm_adapter_tests.cpp
    tests/ay8910_adapter_tests.cpp
    tests/gradius_machine_tests.cpp
    tests/k005289_adapter_tests.cpp
    tests/okim6295_adapter_tests.cpp
    tests/vapor_trail_machine_tests.cpp
    tests/vapor_trail_reference_tests.cpp
    tests/cus30_adapter_tests.cpp
    tests/dac8bitr2r_adapter_tests.cpp
    tests/dragon_spirit_machine_tests.cpp
    tests/vlm5030_adapter_tests.cpp
    tests/gradius_reference_tests.cpp
    tests/sample_rate_converter_tests.cpp
    tests/afterburner2_reference_tests.cpp
    tests/ym2203_adapter_tests.cpp
    tests/msm5205_adapter_tests.cpp
    tests/sf_machine_tests.cpp
    tests/sf_memory_map_tests.cpp
    tests/darius_command_interface_tests.cpp
    tests/darius_machine_tests.cpp
    tests/darius_reference_tests.cpp
    tests/ym2610_adapter_tests.cpp
    tests/ninja_warriors_machine_tests.cpp
    tests/ninja_warriors_reference_tests.cpp
    tests/metalblack_machine_tests.cpp
    tests/metalblack_reference_tests.cpp
)

target_include_directories(chipestra_native_tests PRIVATE include src ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty)
target_compile_definitions(chipestra_native_tests PRIVATE CHIPESTRA_BUILD_DLL MINIZ_STATIC_DEFINE PUGIXML_NO_XPATH)

if (WIN32)
    target_link_libraries(chipestra_native_tests PRIVATE ole32 bcrypt)
endif()

if (MSVC)
    target_compile_options(chipestra_native_tests PRIVATE /W4 /EHsc /utf-8)
endif()

# Development-only diagnostic and stress tools live in tools/ with their own
# CMakeLists.txt; they are built only when that folder is present.
set(CHIPESTRA_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CHIPESTRA_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty")
set(CHIPESTRA_NATIVE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(CHIPESTRA_CORE_SOURCES_ABS ${CHIPESTRA_CORE_SOURCES})
list(TRANSFORM CHIPESTRA_CORE_SOURCES_ABS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
    add_subdirectory(tools)
endif()
