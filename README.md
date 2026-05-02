TSR-Timer
=========

A small timer for TimeSplitters Rewind.

What it does:
- Starts from the game's Unreal loading event: `FunctionFlag("LOADING_C", "", "Destruct")`.
- Stops and records a row from the all-objectives-complete event: `FunctionFlag("*Timer_C", "", "AllObjectivesComplete_Event")`.
- Shows 9 rows plus a total timer.

The overlay is self-contained in this folder. It does not need LiveSplit running.
It borrows `LiveSplit.Core.dll` and `LiveSplit.View.dll` from LiveSplit as packaged libraries, plus `SharpDisasm.dll` as a third-party dependency.

Timing accuracy:
TSR-Timer uses Windows `QueryPerformanceCounter`, the same high-resolution timer approach used by LiveSplit-style timers. Game events are polled every 4ms, so start and stop detection is designed to be as close as possible while staying lightweight.

Display mode:
Run TimeSplitters Rewind in windowed fullscreen / borderless fullscreen so the overlay can stay visible on top. True exclusive fullscreen may hide normal desktop overlays.

Runtime files:
- build/TSR-Timer.exe
- build/TSRAutosplitBridge.dll
- build/LiveSplit.Core.dll
- build/LiveSplit.View.dll
- build/SharpDisasm.dll
- build/Components/uhara10

Build:
1. Install Visual Studio 2022 Build Tools with the C++ desktop workload.
2. Open a terminal in this folder.
3. Run:

   `"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" TSR-Timer.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64 /m`

The finished portable app is written to `build/`.

by Nobbie
