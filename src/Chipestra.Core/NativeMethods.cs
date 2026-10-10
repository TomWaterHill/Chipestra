// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>This class must not be called directly by UI code.</summary>
    internal static class NativeMethods
    {
        private const string NativeLibrary = "chipestra_native";

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint chipestra_get_api_version();

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_create_instance(out IntPtr outHandle);

        // Raw-pointer overload of chipestra_create_instance, used only to exercise the
        // native out_handle == nullptr safety path from tests; `out IntPtr` cannot
        // itself carry a null pointer through P/Invoke marshaling.
        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl, EntryPoint = "chipestra_create_instance")]
        public static extern ChipestraResult chipestra_create_instance_raw(IntPtr outHandlePtr);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern void chipestra_destroy_instance(IntPtr handle);

        // chipestra_load_machine takes `resources`/`resourceCount`: the resolved ROM bytes to hand
        // to the matched MachineDefinition's Factory. `resources` may be null when resourceCount
        // is 0 (a Machine with nothing to resolve).
        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern ChipestraResult chipestra_load_machine(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPStr)] string? machineId,
            [MarshalAs(UnmanagedType.LPStr)] string? variantId,
            [MarshalAs(UnmanagedType.LPArray)] ChipestraResolvedResourceNative[]? resources,
            uint resourceCount);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_unload_machine(IntPtr handle);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_reset_machine(IntPtr handle);

        // Restricted natively to MachineLoaded/Ready/Paused - see
        // chipestra_get_machine_channel_count's comment in chipestra_abi.h.
        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_get_machine_channel_count(IntPtr handle, out uint outCount);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_send_command(IntPtr handle, byte kind, byte value, byte auxiliary);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_select_null_output(IntPtr handle);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_start_audio_output(IntPtr handle);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_stop_audio_output(IntPtr handle);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_select_wasapi_shared_output(IntPtr handle, uint requestedRateHz);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_get_audio_output_format(IntPtr handle, out AudioOutputFormat format);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_start_playback(IntPtr handle);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_pause_playback(IntPtr handle);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_resume_playback(IntPtr handle);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_stop_playback(IntPtr handle);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_get_audio_output_diagnostics(IntPtr handle, out AudioOutputDiagnostics diagnostics);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint chipestra_get_machine_count();

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_get_machine_info(uint index, out MachineInfo info);

        // --- GameDefinition / ResourceResolver group ---
        //
        // `xmlUtf8`/`romRoot` use LPUTF8Str (not the LPStr/ANSI marshaling
        // machineId/variantId above use) because both can legitimately carry
        // non-ASCII text - a game definition's <game title="..."> or a ROM
        // Root path on a non-English Windows install - and native's own
        // parameter name (`xml_utf8`, chipestra_abi.h) is explicit that it expects
        // UTF-8 bytes, not the system ANSI code page.

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern ChipestraResult chipestra_parse_game_definition(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? xmlUtf8,
            uint xmlLength,
            out IntPtr outHandle,
            IntPtr outDetail,
            uint outDetailSize);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern void chipestra_free_game_definition(IntPtr handle);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_game_definition_get_info(IntPtr handle, out ChipestraGameInfo outInfo);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_game_definition_get_resource(IntPtr handle, uint index, out ChipestraRequiredResourceInfo outInfo);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_game_definition_get_track(IntPtr handle, uint index, out ChipestraTrackInfo outInfo);

        // Deliberately takes the resource list directly rather than a GameDefinition handle,
        // so a caller need not keep one alive just to resolve against it.
        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern ChipestraResult chipestra_resolve_resources(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? romRoot,
            [MarshalAs(UnmanagedType.LPArray)] ChipestraRequiredResourceInfo[]? resources,
            uint resourceCount,
            out IntPtr outHandle);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern void chipestra_free_resource_resolution(IntPtr handle);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_resource_resolution_get_summary(IntPtr handle, out ChipestraResolutionSummary outSummary);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_resource_resolution_get_failure(IntPtr handle, uint index, out ChipestraResolutionFailure outFailure);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_resource_resolution_get_resolved(IntPtr handle, uint index, out ChipestraResolvedResourceNative outResolved);

        // --- Channel Meter group ---

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_get_logical_channel_info(IntPtr handle, uint index, out ChipestraLogicalChannelInfo outInfo);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_set_logical_channel_muted(IntPtr handle, uint index, byte muted);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_set_logical_channel_pan(IntPtr handle, uint index, float pan);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_clear_logical_channel_pan_override(IntPtr handle, uint index);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_set_logical_channel_gain(IntPtr handle, uint index, float gain);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_get_master_state(IntPtr handle, out ChipestraMasterState outState);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_set_master_gain(IntPtr handle, float gain);

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChipestraResult chipestra_set_master_muted(IntPtr handle, byte muted);
    }
}
