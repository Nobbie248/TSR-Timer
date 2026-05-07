TSR-Timer
=========

Small autosplitting timer overlay for TimeSplitters Rewind.

It is portable from the `build/` folder and does not need LiveSplit installed or running.

Modes
-----

- Single story level run: one timer for the current story level.
- Full TS story run: 9 split rows for a full story route.
- Challenge run: one timer for challenge runs.
- Full challenge run: 32 split rows for a full challenge route.
- Best times: shows saved story best times.

The bridge detects story difficulty from the live `TS_GameInstance_C` object:

- `TS_GameInstance_C + 0x38A = 0` = Easy
- `TS_GameInstance_C + 0x38A = 1` = Normal
- `TS_GameInstance_C + 0x38A = 2` = Hard

The full address changes every game boot, so TSR-Timer finds `TS_GameInstance_C` first and then reads that offset inside it.

TSR-Timer does not read the game's save file.

Autosplit Signals
-----------------

Story start:

`FunctionFlag("LOADING_C", "", "Destruct")`

The timer starts when the loading screen unloads.

Story finish:

`FunctionFlag("*Timer_C", "", "AllObjectivesComplete_Event")`

The timer stops/splits when all objectives complete.

Challenge start:

`TS_ChallengeGameMode_C` timer state probe:

- elapsed timer field at `0x428`
- remaining timer field at `0x430`
- logged internally as `mode=timer_tick`

The Challenge run starts when the game mode timer begins ticking after the countdown.

Challenge finish:

GameState stop probe:

- `GameState + 0x300` increments
- `GameState + 0x318` changes from `0` to `256`

The Challenge run stops when that GameState stop transition is detected.

Best Times
----------

Best times are stored beside the exe:

`build/TSR-Timer-best-times.txt`

Single story saves one best time per story level for the detected difficulty.

Full story saves only the completed 9-level total. The total is saved under the difficulty detected on the 9th/last story level. While running full story, each individual level split also updates the matching single story level best time for that level's detected difficulty.

Challenge run does not save best times.

Full challenge run saves only the completed 32-challenge total under `Full challenge run | Total`.

Route Logic
-----------

Full story:

- Row 1 can start from the normal story loading-unload signal.
- After a level split, the next row is locked until the game has shown a menu signal.
- Loading/unload signals can reset row 1, but they do not stop a running timer on row 2 onward.
- A menu cancel signal also blocks any start signal that arrives in the same poll, so the timer cannot start in the main menu.

Full challenge:

- A finished challenge is shown immediately and included in the displayed total.
- The next row is not committed until the challenge/menu screen appears.
- If the player retries before returning to menu, the same row is reused.
- Row 32 commits and saves without needing another menu screen.
- Pause menu events are protected by a short cooldown so pause/menu UI noise does not stop the timer.

Runtime Files
-------------

Keep these files together in `build/`:

- `TSR-Timer.exe`
- `TSRAutosplitBridge.dll`
- `LiveSplit.Core.dll`
- `LiveSplit.View.dll`
- `SharpDisasm.dll`
- `Components/uhara10`

`TSR-Timer.exe` loads `TSRAutosplitBridge.dll` from the same folder as the exe. The bridge loads its support files from the same folder and from `Components/` beside the exe.

LiveSplit Notes
---------------

This app does not launch LiveSplit and does not depend on a LiveSplit install.

It packages `LiveSplit.Core.dll` and `LiveSplit.View.dll` from the open-source LiveSplit project because the Unreal event helper uses those libraries. `SharpDisasm.dll` is also included as a helper dependency.

Timing Accuracy
---------------

TSR-Timer uses Windows `QueryPerformanceCounter`, the same high-resolution timer style used by LiveSplit-style timers.

The game event bridge polls every 4 ms. This keeps start/stop detection accurate while staying light on CPU.

Display Mode
------------

Run TimeSplitters Rewind in windowed fullscreen / borderless fullscreen so the overlay can stay visible on top.

True exclusive fullscreen may hide normal desktop overlays.

Logging
-------

Event logging is off by default.

To debug event timing, set `EnableEventLog` in `TSRAutosplitBridge.cs` to `true`, then rebuild.

Build
-----

Install Visual Studio 2022 Build Tools with the C++ desktop workload.

From this folder, run:

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" TSR-Timer.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

The finished portable app is written to `build/`.

by Nobbie
