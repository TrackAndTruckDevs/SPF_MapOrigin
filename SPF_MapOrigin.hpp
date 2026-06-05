/**
 * @file SPF_MapOrigin.hpp
 * @brief Internal header for the SPF_MapOrigin plugin.
 */
#pragma once

#include <SPF_Plugin.h>
#include <SPF_Manifest_API.h>
#include <SPF_Logger_API.h>
#include <SPF_Formatting_API.h>
#include <SPF_Hooks_API.h>
#include <SPF_GameConsole_API.h>
#include <SPF_KeyBinds_API.h>
#include <SPF_Telemetry_API.h>
#include <SPF_TelemetryData.h>

#include <cstdint>
#include <string>
#include <vector>

namespace SPF_MapOrigin {

// --- SCS VFS Structures ---

struct fs_device_t {
    void** vtable;      // +0x0
    void*  padding;     // +0x8
    const char* name;   // +0x10
};

struct mount_node_t {
    mount_node_t* next; // +0x0
    mount_node_t* prev; // +0x8
    fs_device_t*  device; // +0x10
};

struct pool_device_t : public fs_device_t {
    char pad_pool[0x60];
    mount_node_t sentinel; // +0x78 (this is the sentinel node)
};

// --- Plugin Context ---

/**
 * @struct PluginContext
 * @brief Global state container for the plugin.
 * Stores API pointers and addresses discovered via memory patterns.
 */
struct PluginContext {
    const SPF_Load_API* loadAPI = nullptr;
    const SPF_Core_API* coreAPI = nullptr;

    SPF_Logger_Handle* loggerHandle = nullptr;
    const SPF_Formatting_API* formattingAPI = nullptr;

    SPF_Hooks_API* hooksAPI = nullptr;
    SPF_GameConsole_API* gameConsoleAPI = nullptr;
    SPF_KeyBinds_Handle* keybindsHandle = nullptr;
    SPF_Telemetry_Handle* telemetryHandle = nullptr;

    // VFS discovery
    // Function VFS_Initialize (FUN_14027af00) reveals 5 pools:
    // 0: Core, 1: User, 2: Mod, 3: SCS, 4: Merged
    typedef void* (__fastcall* tGetPoolByIndex)(int index);
    tGetPoolByIndex fnGetPoolByIndex = nullptr;
    uint32_t vfsSentinelOffset = 0x78; // Default fallback, but will be found via pattern

    // Map discovery
    // Points to the game's global string pointer that holds the current map path (e.g., "map/usa.mbd")
    // This allows the plugin to work on any map (ATS, ETS2, ProMods) without hardcoded paths.
    const char** ppMapName = nullptr; 

    // Telemetry Cache
    float truckX = 0.0f;
    float truckZ = 0.0f;
};

extern PluginContext g_ctx;

// Helpers
void LogInfo(const char* fmt, ...);
bool IdentifyFileSources(const char* path, std::vector<std::string>& out_sources, bool silent = false);

// Lifecycle
void BuildManifest(SPF_Manifest_Builder_Handle* manifest_handle, const SPF_Manifest_Builder_API* manifest_api);
void OnLoad(const SPF_Load_API* load_api);
void OnActivated(const SPF_Core_API* core_api);
void OnUpdate();
void OnUnload();
void OnGameWorldReady();

// Callbacks
void OnKeybindAction();
void OnTruckData(const SPF_TruckData* truck_data, void* user_data);

} // namespace SPF_MapOrigin
