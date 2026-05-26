# Split Wars 2

> A coordinate-based speedrun timer addon for **Guild Wars 2**, built for the [Nexus](https://raidcore.gg/Nexus) addon framework.

> [!WARNING]
> **AI Notice** — Split Wars 2 was developed with heavy use of AI assistance, specifically [Claude](https://claude.ai) by Anthropic. From architecture decisions and refactoring to bug hunting and documentation, Claude was a core part of the development process.

> [!NOTE]
> **Requirements & Installation**
>
> - Requires the [Nexus](https://raidcore.gg/Nexus) addon loader and Guild Wars 2
> - Download the latest `.dll` from [Releases](../../releases) and place it in your Nexus addons folder (`Guild Wars 2/addons`)
> - In Nexus, find Split Wars 2 in the addon list and press **Load**

---

## Features

### ⏱️ Trigger-Based Timer
The timer starts and stops automatically based on your character's position in the game world — no manual input required mid-run. Define a **start** and a **goal** checkpoint, and the timer handles the rest. A secondary **Grand Total Timer** runs in parallel and tracks wall-clock time across the full session, including any load screens.

### 📍 Flexible Trigger Types
Each checkpoint (including start and goal) supports multiple trigger modes:

| Trigger | Description | In-world overlay |
|---|---|---|
| **Circle** | Fires when you leave the zone (start) or enter it (all other checkpoints including goal) | Ring |
| **Plane** | Fires when you cross a line of configurable width and angle, from either side | Line |
| **Map Change** | Fires on leaving a specified map | — |
| **Interact** | Fires when you press your interact key inside a zone | Ring |
| **Combat (Mumble)** | Arms when you enter the area; fires when you enter combat, then fires again when combat ends (2 s grace period to prevent false out-of-combat triggers) | Ring |
| **All Checkpoints** *(goal only)* | Fires once every non-start/non-goal checkpoint has been triggered | — |

### 📊 Three Timer Display Modes
Cycle through display modes with a keybind:

| Mode | Running timer | Split comparison |
|---|---|---|
| **Segment** | Current segment time | vs. best segment time |
| **Split** | Cumulative time | vs. best cumulative time |
| **LiveSplit** | Current segment time | vs. best cumulative time |

### 🗺️ Route System
- Build routes with any number of **named checkpoints** between start and goal
- Any checkpoint can be flagged as the **start** or **goal** — no fixed order required
- Routes are saved as **JSON files** — human-readable and shareable
- The route name input prevents characters that would cause file saving issues
- The route config window lets you capture your current position with the **Cap** button

### 🏆 Run History & Best Run Tracking
- Completed runs are saved to a `.history` file alongside your route
- The **history window** lists all past runs with timestamps and split breakdowns
- Promote any historical run to your **best run** reference for split comparison
- Delete individual runs from history
- Configurable history limit from 1 to 100 (default: 10)

### 📦 Compact Mode
A toggle that collapses the timer window to its minimal footprint.

### 🔁 Quick Access Hotbar Integration
Split Wars 2 registers a **Nexus Quick Access** button with a hover state icon. A right-click context menu gives fast access to all windows.

---

**Version 0.15.6.2** — initial pre-release · [Releases & Changelog](../../releases)
