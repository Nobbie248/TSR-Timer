TSR-Timer
=========

Small autosplitting timer overlay for TimeSplitters Rewind.

It is portable from the `build/` folder and does not need LiveSplit installed or running.

Modes
-----

- Single story level run: one timer for the current story level.
- Full TS story run: 9 split rows for a full story route.
- Challenge timer: one timer for challenge runs.
- Best times: shows saved story best times.

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

The challenge timer starts when the game mode timer begins ticking after the countdown.

Challenge finish:

GameState stop probe:

- `GameState + 0x300` increments
- `GameState + 0x318` changes from `0` to `256`

The challenge timer stops when that GameState stop transition is detected.

Best Times
----------

Best times are stored beside the exe:

`build/TSR-Timer-best-times.txt`

Single story saves one best time per story level.

Full story saves only the completed 9-level total under `Full TS story run | Total`. While running full story, each individual level split also updates the matching `Single story level run` best time.

Challenge timer does not save best times.

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
