<h1 align="center">SPF Map Origin & Seam Detector Plugin</h1>

<p align="center">
    <a href=""><img src="https://github.com/user-attachments/assets/59abbd2c-414a-4745-a3cd-5cff4cc8901e" alt="Logo SPF Framework" height="222px" /></a>
</p>

<p align="center">
    <a href="https://github.com/TrackAndTruckDevs/SPF_MapOrigin/releases/latest/" target="_blank" title="SPF Map Origin Plugin"><img alt="GitHub Release" src="https://img.shields.io/github/v/release/TrackAndTruckDevs/SPF_MapOrigin"></a>
    <a href="/LICENSE" title="SPF Map Origin Plugin license"><img alt="GitHub License" src="https://img.shields.io/github/license/TrackAndTruckDevs/SPF_MapOrigin"></a>
</p>

<p align="center">
    <a href="https://www.patreon.com/TrackAndTruckDevs" target="_blank" title="Support us on Patreon"><img alt="Patreon" src="https://img.shields.io/badge/patreon-Becoming a patron-3404021712?style=flat&logo=patreon"></a>
    <a href="https://github.com/TrackAndTruckDevs/SPF_MapOrigin/stargazers" title="Liked it? Starred"><img src="https://img.shields.io/github/stars/TrackAndTruckDevs/SPF_MapOrigin?style=flat&logo=github" alt="Stars" /></a>
    <a href="https://discord.gg/kadd8AQuMt" target="_blank" title="Join our Discord"><img alt="Discord" src="https://img.shields.io/badge/discord-join-7289da?style=flat&logo=discord&logoColor=white"></a>
    <a href="https://youtube.com/@TrackAndTruck" target="_blank" title="Subscribe to our channel"><img alt="Youtube" src="https://img.shields.io/badge/youtube-subscribe-orange?logo=youtube&style=flat"></a>
</p>

---

A specialized diagnostic plugin for American Truck Simulator and Euro Truck Simulator 2 that identifies the source (Mod or DLC) of the current map sector. It features an advanced **Map Seam Detector** to visualize transitions between different map mods.

## 🎓 Demonstration Purpose

This project demonstrates advanced reverse engineering techniques for the game's Virtual File System (VFS):
*   **VFS Pool Iteration**: Scanning Mod, SCS, and Base pools to resolve file priority.
*   **Reverse Priority Identification**: Implementing "Winner-takes-all" logic to find the active sector override.
*   **Dynamic Sector Mapping**: Real-time calculation of map coordinates into the game's 5120x5120 sector grid.
*   **Pattern-based Memory Discovery**: Finding VFS sentinels and map path pointers without hardcoded addresses.

## Features

*   **Sector Winner Identification**: Instantly reveals which mod or DLC "owns" the sector you are currently in.
*   **Full Archive Stack**: Lists every single archive (SCS or Mod) that contains the current sector file.
*   **4-Way Seam Detector**: Scans North, South, East, and West neighbors to find transitions between mods.
*   **Visual Status Indicators**:
    *   `[SAME]`: Neighbor belongs to the same mod.
    *   `[BASE]`: Neighbor is part of the original game or official DLC.
    *   `[SEAM]`: Neighbor belongs to a **different mod** (Seam detected!).
    *   `[VOID]`: Neighboring sector is empty (Edge of the map).
*   **Universal Map Support**: Automatically detects map names (usa, europe, promods, etc.) directly from memory.

## Support the Project

If you find this tool useful for map development or mod troubleshooting, consider supporting us on Patreon.

► **[Support on Patreon](https://www.patreon.com/TrackAndTruckDevs)**

## How to Build 🛠️

This is a standard CMake project. To build it from source:

1.  Clone this repository.
2.  Ensure you have **CMake** and **Visual Studio (MSVC)** installed.
3.  Create a `build` directory.
4.  Run `cmake ..` and then `cmake --build .`.

## Installation

### Prerequisites

You must have the **SPF Framework** installed.
*   **[Download the SPF-Framework here](https://github.com/TrackAndTruckDevs/SPF-Framework)**

### Steps

1.  Download the latest release.
2.  Copy the `SPF_MapOrigin` folder into `\bin\win_x64\plugins\spfPlugins\`.

## How to Use

1.  Start the game and drive to any location.
2.  Press **`Ctrl + F11`** to trigger the Origin Report.
3.  **Check the Log**: Open `Documents\American Truck Simulator\game.log.txt` (or the in-game console) to see the detailed report, including the active winner and the seam detector results.

<h2 align="center">🙏 Acknowledgements</h2>

This project was created using the **[SPF-Framework](https://github.com/TrackAndTruckDevs/SPF-Framework)**, the foundation for modern plugin development in ATS/ETS2.
