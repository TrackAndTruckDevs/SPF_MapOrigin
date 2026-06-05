/**
 * @file SPF_MapOrigin.cpp
 * @brief Implementation of the SPF_MapOrigin plugin to identify map sector origins.
 */

#include "SPF_MapOrigin.hpp"



#include <cmath>

namespace SPF_MapOrigin {

const char* PLUGIN_NAME = "SPF_MapOrigin";
PluginContext g_ctx;

// =================================================================================================
// 1. Helper Functions
// =================================================================================================

/**
 * @brief Formatted output to the framework logger and file sink.
 */
void LogInfo(const char* fmt, ...) {
    if (!g_ctx.loggerHandle || !g_ctx.loadAPI) return;
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    g_ctx.loadAPI->logger->Log(g_ctx.loggerHandle, SPF_LOG_INFO, buffer);
}

/**
 * @brief Safe VFS Lookup that checks if a file exists in a specific archive.
 * 
 * @param archive Pointer to the fs_device_t (archive/mod) to check.
 * @param path Internal VFS path to the file (e.g., "/map/usa/sec+0000-0004.base").
 * @return true if the file exists in this specific archive.
 * 
 * @note This function directly calls the game's internal 'Lookup' method (vtable index 3).
 * It handles both absolute and relative (no leading slash) paths for robustness.
 */
bool IsFileInArchive(fs_device_t* archive, const char* path) {
    // Basic safety checks for valid object and vtable
    if (!archive || !archive->vtable || !archive->vtable[3]) return false;
    
    // The Lookup function is responsible for checking file presence and retrieving metadata.
    // Address check ensures we don't call into null or garbage memory.
    typedef bool(__fastcall* tLookup)(void*, const char**, void*);
    tLookup fnLookup = (tLookup)archive->vtable[3]; // Lookup is at +0x18 (index 3)

    if ((uintptr_t)fnLookup < 0x1000) return false;

    const char* pPath = path;
    char dummyRes[0x200]; // Placeholder for result metadata we don't need
    
    // Attempt lookup with absolute path
    if (fnLookup(archive, &pPath, dummyRes)) return true;

    // Attempt lookup without leading slash (some archives use relative indexing)
    if (path[0] == '/') {
        const char* pPathNoSlash = path + 1;
        if (fnLookup(archive, &pPathNoSlash, dummyRes)) return true;
    }

    return false;
}

/**
 * @brief Scans VFS pools to identify which mounted archives contain the given file.
 * 
 * Technical analysis based on Ghidra:
 * - Pool indices confirmed in VFS_Initialize (FUN_14027af00): 
 *   0: Core, 1: User, 2: Mod, 3: SCS, 4: Merged.
 * - Device mounting logic discovered in UFS_RegisterMount:
 *   New devices are inserted at the tail of the doubly linked list:
 *     plVar7[1] = *(longlong *)(lVar13 + 8);  // NewNode->prev = sentinel->prev
 *     *plVar7 = lVar13;                       // NewNode->next = sentinel
 *   This confirms that the most recently mounted (highest priority) mods 
 *   are located at the end of the list (sentinel->prev).
 * 
 * To correctly identify the "winner" (the one that actually overrides others), 
 * we iterate through the list in reverse order starting from the tail.
 * The offset to the sentinel node is found dynamically via pattern matching.
 * 
 * @param path Internal VFS path to the file.
 * @param out_sources Vector to be filled with names of all archives containing the file.
 * @param silent If true, suppresses LogInfo output (useful for scanning neighbors).
 * @return true if the file was found in at least one archive.
 */
bool IdentifyFileSources(const char* path, std::vector<std::string>& out_sources, bool silent) {
    if (!g_ctx.fnGetPoolByIndex) return false;

    // Search priority (from highest to lowest): Mod -> SCS -> User -> Core
    int poolsToCheck[] = { 2, 3, 1, 0 };
    const char* poolNames[] = { "Core", "User", "Mod", "SCS", "Merged" };
    
    for (int i = 0; i < 4; ++i) {
        int poolIdx = poolsToCheck[i];
        pool_device_t* pool = (pool_device_t*)g_ctx.fnGetPoolByIndex(poolIdx);
        if (!pool) continue;

        // The list head/sentinel is found dynamically (g_ctx.vfsSentinelOffset).
        // Since SCS appends new mounts to the tail, the highest priority device
        // is found at sentinel->prev. We iterate backwards.
        mount_node_t* sentinel = (mount_node_t*)((char*)pool + g_ctx.vfsSentinelOffset);
        mount_node_t* current = sentinel->prev; 

        int safetyCounter = 0;
        while (current != nullptr && current != sentinel && safetyCounter < 1000) {
            fs_device_t* archive = current->device;
            
            if (archive && archive->name && archive->vtable) {
                // vtable[3] (offset +0x18) is the Lookup function.
                if (IsFileInArchive(archive, path)) {
                    out_sources.push_back(archive->name);
                    if (!silent) {
                        LogInfo("[VFS] Detected '%s' in pool %s archive: %s", path, poolNames[poolIdx], archive->name);
                    }
                }
            }
            current = current->prev; // Move towards the head (lower priority)
            safetyCounter++;
        }
    }
    return !out_sources.empty();
}

// =================================================================================================
// 2. Detour Functions
// =================================================================================================

// Note: Previously, this section contained a SectorManager hook. 
// It was removed because the current logic uses a memory pattern to find the map path directly,
// making the capture of the SectorManager object unnecessary.

// =================================================================================================
// 3. Callbacks
// =================================================================================================

/**
 * @brief Updates truck coordinates from telemetry data.
 * Triggered by the framework whenever new telemetry is available.
 */
void OnTruckData(const SPF_TruckData* truck_data, void* user_data) {
    if (truck_data) {
        g_ctx.truckX = truck_data->world_placement.position.x;
        g_ctx.truckZ = truck_data->world_placement.position.z;
    }
}

/**
 * @brief Main handler for the sector origin check action.
 * Triggered by the user (Ctrl + O by default).
 * 
 * Logic flow:
 * 1. Retrieve current truck coordinates from telemetry cache.
 * 2. Calculate the theoretical map sector name (sec+XXXX-ZZZZ) using the 5120.0 unit grid.
 * 3. Dynamically discover the current map's subfolder via memory pointer.
 * 4. Scan all VFS pools (Mod, SCS, etc.) in reverse priority order.
 * 5. Collect all archives containing the sector and identify the active override (winner).
 */
void OnKeybindAction() {
    if (!g_ctx.formattingAPI) return;

    // ATS/ETS2 map sectors are organized in a 5120.0 x 5120.0 game unit grid.
    float grid = 5120.0f;
    int baseSectorX = (int)std::floor(g_ctx.truckX / grid);
    int baseSectorZ = (int)std::floor(g_ctx.truckZ / grid);

    // --- Dynamic Map Folder Discovery ---
    // Instead of hardcoding "/map/usa/" or "/map/europe/", we retrieve the current map path
    // from the game's global memory. This ensures compatibility with any map or mod.
    char mapNameOnly[128] = "unknown";
    char dynamicFolder[256] = "/map/"; // Fallback folder
    const char* fullMapPath = "unknown";

    if (g_ctx.ppMapName && *g_ctx.ppMapName) {
        fullMapPath = *g_ctx.ppMapName; // e.g., "map/usa.mbd"
        LogInfo("[VFS] Global Map Name: %s", fullMapPath);

        // Extract the map name from the path (e.g., "usa" from "map/usa.mbd")
        const char* lastSlash = strrchr(fullMapPath, '/');
        const char* mapNameStart = lastSlash ? lastSlash + 1 : fullMapPath;
        
        strncpy_s(mapNameOnly, mapNameStart, sizeof(mapNameOnly));
        char* dot = strrchr(mapNameOnly, '.');
        if (dot) *dot = '\0';

        // Reconstruct the VFS path where sector files (.base) are typically stored
        g_ctx.formattingAPI->Fmt_Format(dynamicFolder, sizeof(dynamicFolder), "/map/%s/", mapNameOnly);
        LogInfo("[VFS] Map detected: %s (Sector folder: %s)", mapNameOnly, dynamicFolder);
    }

    std::vector<std::string> allSources;
    char finalSectorName[64] = "unknown";
    bool foundAny = false;

    int currentX = 0;
    int currentZ = 0;

    // Scan a 5x5 grid around the calculated position to find the active .base file
    for (int dx = -2; dx <= 2 && !foundAny; ++dx) {
        for (int dz = -2; dz <= 2 && !foundAny; ++dz) {
            int sx = baseSectorX + dx;
            int sz = baseSectorZ + dz;
            char sName[64];
            g_ctx.formattingAPI->Fmt_Format(sName, sizeof(sName), "sec%+05d%+05d", sx, sz);

            char sPath[256];
            g_ctx.formattingAPI->Fmt_Format(sPath, sizeof(sPath), "%s%s.base", dynamicFolder, sName);
            
            if (IdentifyFileSources(sPath, allSources)) {
                strncpy_s(finalSectorName, sName, sizeof(finalSectorName));
                currentX = sx;
                currentZ = sz;
                foundAny = true;
            }
        }
    }

    // --- Report Output (Log File) ---
    LogInfo("========================= SECTOR ORIGIN REPORT =========================");
    LogInfo("Position:     X: %.1f, Z: %.1f", g_ctx.truckX, g_ctx.truckZ);
    LogInfo("Map File:     %s", fullMapPath);
    LogInfo("Sector:       %s", finalSectorName);
    LogInfo("------------------------------------------------------------------------");

    int seamCount = 0;
    std::string currentWinner = "none";

    if (foundAny) {
        currentWinner = allSources[0];
        LogInfo("Found in %llu archive(s):", (unsigned long long)allSources.size());
        for (size_t i = 0; i < allSources.size(); ++i) {
            const char* tag = (i == 0) ? " <<< ACTIVE OVERRIDE (WINNER)" : "";
            LogInfo("  [%llu] %s%s", (unsigned long long)(i + 1), allSources[i].c_str(), tag);
        }

        // --- Map Seam Detector (N, S, E, W) ---
        LogInfo("------------------------- MAP SEAM DETECTOR ----------------------------");
        struct { const char* dir; int dx; int dz; } neighbors[] = {
            {"North", 0, -1}, {"South", 0, 1}, {"East", 1, 0}, {"West", -1, 0}
        };

        for (auto& n : neighbors) {
            int sx = currentX + n.dx;
            int sz = currentZ + n.dz;
            char sName[64];
            g_ctx.formattingAPI->Fmt_Format(sName, sizeof(sName), "sec%+05d%+05d", sx, sz);
            
            std::vector<std::string> nSources;
            char nPath[256];
            g_ctx.formattingAPI->Fmt_Format(nPath, sizeof(nPath), "%s%s.base", dynamicFolder, sName);
            
            if (IdentifyFileSources(nPath, nSources, true)) {
                std::string nWinner = nSources[0];
                const char* status = "[SEAM]";
                
                // Determine if this is a base game/DLC archive
                bool isBase = (strstr(nWinner.c_str(), "dlc_") != nullptr || 
                              strstr(nWinner.c_str(), "base_map") != nullptr ||
                              strstr(nWinner.c_str(), "steamapps") != nullptr);
                
                if (nWinner == currentWinner) {
                    status = "[SAME]";
                } else if (isBase) {
                    status = "[BASE]";
                } else {
                    status = "[SEAM]";
                    seamCount++;
                }
                
                LogInfo("%s  %-5s %s | Owner: %s", status, n.dir, sName, nWinner.c_str());
            } else {
                LogInfo("[VOID]  %-5s %s", n.dir, sName);
            }
        }

        if (g_ctx.gameConsoleAPI) {
            char consoleMsg[1024];
            // Line 1: Sector and full winner path
            g_ctx.formattingAPI->Fmt_Format(consoleMsg, sizeof(consoleMsg), 
                "echo [SPF] Sector: %s | Winner: %s", finalSectorName, allSources[0].c_str());
            g_ctx.gameConsoleAPI->GCon_ExecuteCommand(consoleMsg);
            
            // Line 2: Coordinates and summary
            g_ctx.formattingAPI->Fmt_Format(consoleMsg, sizeof(consoleMsg), 
                "echo [SPF] Pos: X:%.1f Z:%.1f | Seams: %d", 
                g_ctx.truckX, g_ctx.truckZ, seamCount);
            g_ctx.gameConsoleAPI->GCon_ExecuteCommand(consoleMsg);
        }
    } else {
        LogInfo("RESULT: Sector file not found in any VFS pool.");
        if (g_ctx.gameConsoleAPI) {
            g_ctx.gameConsoleAPI->GCon_ExecuteCommand("echo [SPF] Error: Sector origin not found.");
        }
    }
    LogInfo("------------------------------------------------------------------------");
    LogInfo("[Report] Active map sector: %s (Winner: %s)", finalSectorName, currentWinner.c_str());
    LogInfo("[Report] Seams detected: %d (check the list above for details)", seamCount);
    LogInfo("========================================================================");
}

// =================================================================================================
// 4. Plugin Lifecycle
// =================================================================================================

/**
 * @brief Builds the plugin manifest required by the SPF framework.
 * Defines plugin identity, versioning, policies, and default configurations.
 */
void BuildManifest(SPF_Manifest_Builder_Handle* manifest_handle, const SPF_Manifest_Builder_API* manifest_api) {
    manifest_api->Info_SetName(manifest_handle, PLUGIN_NAME);
    manifest_api->Info_SetVersion(manifest_handle, "0.9.0");
    manifest_api->Info_SetMinFrameworkVersion(manifest_handle, "1.1.9");
    manifest_api->Info_SetAuthor(manifest_handle, "Track'n'Truck Devs");
    manifest_api->Info_SetDescriptionLiteral(manifest_handle, "Identifies the origin mod of the current map sector.");

    manifest_api->Policy_SetAllowUserConfig(manifest_handle, true);
    manifest_api->Policy_AddConfigurableSystem(manifest_handle, "logging");
    manifest_api->Policy_AddRequiredHook(manifest_handle, "GameConsole");

    manifest_api->Defaults_SetLogging(manifest_handle, "info", true);
    manifest_api->Defaults_AddKeybind(manifest_handle, "General", "CheckOrigin", "chord", "keyboard:KEY_LCONTROL+keyboard:KEY_F11", "always");
    manifest_api->Meta_AddKeybind(manifest_handle, "General", "CheckOrigin", "Check Sector Origin", "Identifies the origin of the current map sector.");
}

/**
 * @brief Called when the plugin is initially loaded into memory.
 * Used to capture basic APIs provided by the SPF loader.
 */
void OnLoad(const SPF_Load_API* load_api) {
    g_ctx.loadAPI = load_api;
    if (g_ctx.loadAPI) {
        g_ctx.loggerHandle = g_ctx.loadAPI->logger->Log_GetContext(PLUGIN_NAME);
        g_ctx.formattingAPI = g_ctx.loadAPI->formatting;
    }
}

void OnActivated(const SPF_Core_API* core_api) {
    g_ctx.coreAPI = core_api;
    LogInfo("[Init] Plugin %s activated. Setting up VFS and Memory Patterns...", PLUGIN_NAME);

    if (g_ctx.coreAPI) {
        g_ctx.hooksAPI = g_ctx.coreAPI->hooks;
        g_ctx.gameConsoleAPI = g_ctx.coreAPI->console;
        
        if (g_ctx.coreAPI->keybinds) {
            g_ctx.keybindsHandle = g_ctx.coreAPI->keybinds->Kbind_GetContext(PLUGIN_NAME);
        }
        
        if (g_ctx.coreAPI->telemetry) {
            g_ctx.telemetryHandle = g_ctx.coreAPI->telemetry->Tel_GetContext(PLUGIN_NAME);
            g_ctx.coreAPI->telemetry->Tel_RegisterForTruckData(g_ctx.telemetryHandle, OnTruckData, nullptr);
        }

        if (g_ctx.hooksAPI) {
            // Discovery of fnGetPoolByIndex:
            // This function allows us to access the game's VFS pools (Mod, SCS, Core, etc.)
            uintptr_t addrPool = g_ctx.hooksAPI->Hook_FindPattern("48 83 ec 48 48 63 d1 48 3b 15 ? ? ? ? 73 10 48 8b 05 ? ? ? ? 48 8b 04 d0");
            if (addrPool) {
                g_ctx.fnGetPoolByIndex = (PluginContext::tGetPoolByIndex)addrPool;
                
                // Calculate the address of the global Pool Array (PTR_DAT_142a28f50)
                // Instruction at addrPool + 0x10: MOV RAX, [RIP + offset]
                int32_t poolArrayOffset = *(int32_t*)(addrPool + 0x13);
                uintptr_t addrPoolArray = addrPool + 0x10 + 7 + poolArrayOffset;
                LogInfo("[Init] Pool Array Base Pointer found at 0x%p", addrPoolArray);
            }

            // Discovery of ppMapName:
            // Using the signature from the game's map-handling logic (discovered via Ghidra).
            // This pointer reveals the current map name (e.g., "map/usa.mbd") dynamically.
            uintptr_t addrMapName = g_ctx.hooksAPI->Hook_FindPattern("48 8b ? ? ? ? ? 48 8b ? ? 48 ? ? ? ? 49 ? ? ? ? c6 ? ? c7 ? ? ? ? ? ? 48");
            if (addrMapName) {
                // RIP-relative addressing calculation: Pattern + instruction length + 32-bit offset
                int32_t offset = *(int32_t*)(addrMapName + 3);
                g_ctx.ppMapName = (const char**)(addrMapName + 7 + offset);
                LogInfo("[Init] Map Name Pointer found at 0x%p", g_ctx.ppMapName);
            } else {
                LogInfo("[Init] FAILED to find Map Name Pointer pattern!");
            }

            // Discovery of VFS Sentinel Offset:
            // Using the signature from UFS_RegisterMount (Ghidra analysis).
            // Pattern matches: 49 8b 5d 78 (MOV RBX, [R13 + 0x78])
            uintptr_t addrSentinel = g_ctx.hooksAPI->Hook_FindPattern("49 8b ? ? 88 ? ? ? 0f b6 ? ? ? ? ? 88");
            if (addrSentinel) {
                // The offset is the 4th byte of the '49 8b 5d XX' instruction
                g_ctx.vfsSentinelOffset = *(uint8_t*)(addrSentinel + 3);
                LogInfo("[Init] VFS Sentinel Offset found at 0x%p with value 0x%02X", addrSentinel, g_ctx.vfsSentinelOffset);
            } else {
                LogInfo("[Init] FAILED to find VFS Sentinel Offset pattern! Using default 0x78.");
            }
        }
    }

    if (g_ctx.keybindsHandle && g_ctx.coreAPI && g_ctx.coreAPI->keybinds) {
        g_ctx.coreAPI->keybinds->Kbind_Register(g_ctx.keybindsHandle, "General.CheckOrigin", OnKeybindAction);
    }
}

/**
 * @brief Called every frame by the game engine.
 * Currently unused as all logic is triggered by keybinds.
 */
void OnUpdate() {}

/**
 * @brief Called when the plugin is being unloaded.
 * Used for cleanup of allocated resources and API pointers.
 */
void OnUnload() {
    g_ctx.coreAPI = nullptr;
    g_ctx.loadAPI = nullptr;
    g_ctx.loggerHandle = nullptr;
}

/**
 * @brief Called when the game world is fully loaded and the player has control.
 */
void OnGameWorldReady() {
    LogInfo("OnGameWorldReady! Ready to inspect sector origins.");
}

// =================================================================================================
// 5. Plugin Exports
// =================================================================================================

extern "C" {
    /**
     * @brief Required export to provide the manifest builder to the framework.
     */
    SPF_PLUGIN_EXPORT bool SPF_GetManifestAPI(SPF_Manifest_API* manifest_api_out) {
        if (manifest_api_out) { 
            manifest_api_out->BuildManifest = BuildManifest; 
            return true; 
        }
        return false;
    }

    /**
     * @brief Required export to provide the plugin's lifecycle callbacks to the framework.
     */
    SPF_PLUGIN_EXPORT bool SPF_GetPlugin(SPF_Plugin_Exports* plugin_exports_out) {
        if (plugin_exports_out) {
            plugin_exports_out->OnLoad = OnLoad;
            plugin_exports_out->OnActivated = OnActivated;
            plugin_exports_out->OnUnload = OnUnload;
            plugin_exports_out->OnUpdate = OnUpdate;
            plugin_exports_out->OnGameWorldReady = OnGameWorldReady;
            return true;
        }
        return false;
    }
}

} // namespace SPF_MapOrigin
