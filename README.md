TSR-Timer
=========

Small autosplitting timer overlay for TimeSplitters Rewind.

It is portable from the `build/` folder and does not need LiveSplit installed or running.

Modes
-----

- Single story level run: one timer for the current story level.
- Full TS story run: 9 split rows for a full story route.
- Challenge run: one timer for challenge runs.
- Best times: shows saved story best times.

Full challenge run is temporarily unavailable while the route logic is being refined.

The bridge detects story difficulty from the live `TS_GameInstance_C` object:

- `TS_GameInstance_C + 0x38A = 0` = Easy
- `TS_GameInstance_C + 0x38A = 1` = Normal
- `TS_GameInstance_C + 0x38A = 2` = Hard

TSR-Timer finds `TS_GameInstance_C` first and then reads that offset inside it.

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

Single story run compares only against the saved single story best for the current level and difficulty.

Full story saves the completed 9-level total under the difficulty detected on the 9th/last story level. If the total is equal to or better than the saved full story best, TSR-Timer also saves all 9 full story PB splits.

Full story row deltas compare against those saved full story PB splits, LiveSplit-style. The bottom delta is the sum of the completed row deltas so you can see overall pace. While running full story, each completed level still updates the matching single story best time if that level is faster than its saved single-level best.

In Best times, the `>` button beside a saved Full Story Run opens that run's saved PB splits for the selected difficulty.

Challenge run does not save best times or show PB deltas.

Full challenge best times are temporarily hidden while Full challenge run is unavailable.

Route Logic
-----------

Full story:

- Row 1 can start from the normal story loading-unload signal.
- After a level split, the next row is locked until the game has shown a menu signal.
- Loading/unload signals can reset row 1, but they do not stop a running timer on row 2 onward.
- A menu cancel signal also blocks any start signal that arrives in the same poll, so the timer cannot start in the main menu.

Full challenge is temporarily unavailable in the app. The intended route behavior is:

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

Some story finish times may look biased toward the same last centisecond digits. That is the game event, not the app timer. TSR-Timer records the Windows timer moment when the game exposes `AllObjectivesComplete_Event`; if the game only raises that event on its own update cadence, the app cannot create timing precision before the event exists.

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
