using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace TSRTimer
{
    public static class AutoSplitBridge
    {
        const string ProcessName = "TimeSplittersRewind-Win64-Shipping";
        const int Ready = 1 << 0;
        const int Started = 1 << 1;
        const int Split = 1 << 2;
        const int Attached = 1 << 3;
        const int Loading = 1 << 4;
        const int StartCanceled = 1 << 5;
        const int ChallengeStarted = 1 << 6;
        const double ChallengePulse2MinSecondsAfterLoading = 3.0;
        const double ChallengePulseMinSecondsAfterArm = 0.75;
        const int ChallengeSplit = 1 << 7;
        const int Error = 1 << 30;
        static readonly bool EnableEventLog = false;
        const uint MEM_COMMIT = 0x1000;
        const uint MEM_RESERVE = 0x2000;
        const uint PAGE_EXECUTE_READWRITE = 0x40;

        static readonly HashSet<string> CompletedSplits = new HashSet<string>();
        static string baseDir;
        static string componentDir;
        static bool assemblyResolverAdded;
        static bool resolverWatchAdded;
        static dynamic uhara;
        static dynamic utils;
        static dynamic eventsTool;
        static dynamic resolver;
        static Assembly uharaAssembly;
        static Process process;

        static IntPtr startFlagPtr;
        static IntPtr splitFlagPtr;
        static IntPtr startParentPtr;
        static IntPtr splitParentPtr;
        static IntPtr menuMainMenuPtr;
        static IntPtr menuStorySelectPtr;
        static IntPtr menuStoryButtonPtr;
        static IntPtr menuBackgroundPtr;
        static IntPtr challengeStartPtr;
        static IntPtr countdownGoPtr;
        static IntPtr countdownFinishPtr;
        static IntPtr countdownDestructPtr;
        static IntPtr challengeMissionEndedPtr;
        static IntPtr challengeAwardWidgetPtr;
        static IntPtr challengeAwardWidgetParentPtr;
        static IntPtr challengeGameModeAnyPtr;
        static IntPtr challengeGameModeParentPtr;
        static IntPtr challengeMayhemParentPtr;
        static IntPtr hudTimeCounterParentPtr;
        static IntPtr gameStateParentPtr;
        static IntPtr tsGameStateParentPtr;
        static IntPtr gameInstanceParentPtr;
        static IntPtr broadStateParentPtr;
        static IntPtr characterMovementParentPtr;
        static IntPtr[] challengeFinishProbePtrs;
        static IntPtr[] challengeFinishProbeParentPtrs;
        static string[] challengeFinishProbeLabels;
        static uint[] lastChallengeFinishProbeValues;
        static bool[] challengeFinishProbeSeen;
        static IntPtr[] playerMovementProbePtrs;
        static IntPtr[] playerMovementProbeParentPtrs;
        static string[] playerMovementProbeLabels;
        static uint[] lastPlayerMovementProbeValues;
        static IntPtr[] gameStateProbePtrs;
        static IntPtr[] gameStateProbeParentPtrs;
        static string[] gameStateProbeLabels;
        static uint[] lastGameStateProbeValues;
        static IntPtr[] difficultyProbePtrs;
        static IntPtr[] difficultyProbeParentPtrs;
        static string[] difficultyProbeLabels;
        static uint[] lastDifficultyProbeValues;
        static IntPtr[] inGameMenuProbePtrs;
        static IntPtr[] inGameMenuProbeParentPtrs;
        static string[] inGameMenuProbeLabels;
        static uint[] lastInGameMenuProbeValues;
        static IntPtr pauseMenuStateObjectPtr;
        static byte[] pauseMenuStateSnapshot;
        static IntPtr inGameMenuStateObjectPtr;
        static byte[] inGameMenuStateSnapshot;
        static bool pauseMenuStateActive;
        static DateTime lastPauseMenuStateActiveAt;

        static uint lastStartFlagValue;
        static uint lastSplitFlagValue;
        static uint lastMenuMainMenuValue;
        static uint lastMenuStorySelectValue;
        static uint lastMenuStoryButtonValue;
        static uint lastMenuBackgroundValue;
        static uint lastChallengeStartValue;
        static uint lastCountdownGoValue;
        static uint lastCountdownFinishValue;
        static uint lastCountdownDestructValue;
        static uint lastChallengeMissionEndedValue;
        static uint lastChallengeAwardWidgetValue;
        static uint lastChallengeGameModeAnyValue;
        static uint lastCharMove2F0Value;
        static bool charMove2F0Ready;
        static DateTime challengeCountdownArmedAt;
        static DateTime lastLoadingUnloadAt;
        static int challengeCountdownFinishCount;
        static DateTime pendingChallengeCountdownArmedAt;
        static int pendingChallengeCountdownFinishCount;
        static DateTime challengeProbeStartedAt;
        static bool challengeProbeActive;
        static int challengeProbeLogCount;
        static DateTime lastInGameMenuEventAt;
        static DateTime playerMovementProbeStartedAt;
        static int playerMovementProbeLogCount;
        static byte[] challengeStateSnapshot;
        static DateTime challengeStateProbeStartedAt;
        static DateTime lastChallengeStateProbeAt;
        static int challengeStateProbeLogCount;
        static bool challengeStateStartSignal;
        static bool playerMovementStartSignal;
        static DeepScanTarget[] deepScanTargets;
        static uint lastGameModeFinishValue;
        static bool gameModeFinishValueReady;
        static DateTime lastGameModeFinishValueChangedAt;
        static bool gameModeFinishStallLogged;
        static uint lastHudTimeCounterValue;
        static bool hudTimeCounterReady;
        static DateTime lastHudTimeCounterChangedAt;
        static bool hudTimeCounterStallLogged;
        static uint lastGameState300Value;
        static uint lastGameState318Value;
        static bool gameStateStopReady;
        static bool gameModeStateFinishArmed;
        static bool challengeSplitIgnoresPause;
        static byte[] lastGraphicsSample;
        static DateTime lastGraphicsSampleAt;
        static bool graphicsSampleReady;
        static int graphicsFrameLogCount;
        static string lastAwardWidgetParentText;
        static string worldName = "";
        static string lastPublishedWorldName = "";
        static string lastStartParentDescription = "0x0";
        static string lastSplitParentDescription = "0x0";
        static string lastChallengeGameModeDescription = "0x0";
        static string lastChallengeMayhemDescription = "0x0";
        static string lastDifficultyDescription = "Unknown";
        static int currentStoryDifficultyIndex = -1;
        static IntPtr storyGameInstancePtr;
        static byte[] storyGameInstanceSnapshot;
        static int lastGameInstanceDifficultyByte = -1;
        static string lastError = "";
        static string logPath = "";
        static string statePath = "";
        static bool logInitialized;

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesRead);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, UIntPtr dwSize, uint flAllocationType, uint flProtect);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesWritten);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, out uint lpThreadId);

        [DllImport("user32.dll")]
        static extern IntPtr GetDC(IntPtr hWnd);

        [DllImport("user32.dll")]
        static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

        [DllImport("user32.dll")]
        static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);

        [DllImport("user32.dll")]
        static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

        [DllImport("gdi32.dll")]
        static extern IntPtr CreateCompatibleDC(IntPtr hdc);

        [DllImport("gdi32.dll")]
        static extern IntPtr CreateCompatibleBitmap(IntPtr hdc, int cx, int cy);

        [DllImport("gdi32.dll")]
        static extern IntPtr SelectObject(IntPtr hdc, IntPtr h);

        [DllImport("gdi32.dll")]
        static extern bool DeleteObject(IntPtr ho);

        [DllImport("gdi32.dll")]
        static extern bool DeleteDC(IntPtr hdc);

        [DllImport("gdi32.dll")]
        static extern int SetStretchBltMode(IntPtr hdc, int mode);

        [DllImport("gdi32.dll")]
        static extern bool StretchBlt(IntPtr hdcDest, int xDest, int yDest, int wDest, int hDest,
                                      IntPtr hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, uint rop);

        [DllImport("gdi32.dll")]
        static extern int GetDIBits(IntPtr hdc, IntPtr hbmp, uint start, uint cLines, byte[] lpvBits,
                                    ref BITMAPINFOHEADER lpbmi, uint usage);

        [StructLayout(LayoutKind.Sequential)]
        struct RECT
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct BITMAPINFOHEADER
        {
            public uint biSize;
            public int biWidth;
            public int biHeight;
            public ushort biPlanes;
            public ushort biBitCount;
            public uint biCompression;
            public uint biSizeImage;
            public int biXPelsPerMeter;
            public int biYPelsPerMeter;
            public uint biClrUsed;
            public uint biClrImportant;
        }

        class DeepScanTarget
        {
            public string Name;
            public IntPtr ParentPtr;
            public IntPtr ObjectPtr;
            public byte[] Snapshot;
            public bool LogBeforeFinish;
            public int LogLimit;
        }

        public static int Poll(string config)
        {
            try
            {
                Configure();
                EnsureLoaded();
                EnsureProcess();

                if (process == null || process.HasExited)
                    return Ready;

                EnsureTools();
                AddWorldWatcher();
                uhara.Update();
                UpdateWorldName();
                UpdateStoryGameInstanceFromParent(startParentPtr);
                PollStoryGameInstanceScan();
                PublishState();

                int flags = Ready | Attached;
                DateTime now = DateTime.UtcNow;

                if (ReadEventFlag(startFlagPtr, ref lastStartFlagValue))
                {
                    UpdateStoryDifficultyFromParent("storyStart", startParentPtr);
                    lastLoadingUnloadAt = now;
                    flags |= Loading;
                    Log("LOADING_UNLOAD world=" + worldName);
                    if (IsStartWorld(worldName))
                    {
                        CompletedSplits.Clear();
                        playerMovementStartSignal = false;
                        Log("LOADING_UNLOAD full_single_start world=" + worldName);
                        flags |= Started;
                    }
                }

                if (ReadEventFlag(splitFlagPtr, ref lastSplitFlagValue))
                {
                    UpdateStoryDifficultyFromParent("storySplit", splitParentPtr);
                    if (CompletedSplits.Add(worldName ?? ""))
                        flags |= Split;
                }

                bool challengeStartEvent = ReadEventFlag(challengeStartPtr, ref lastChallengeStartValue);
                bool countdownGoEvent = ReadEventFlag(countdownGoPtr, ref lastCountdownGoValue);
                bool countdownFinishEvent = ReadEventFlag(countdownFinishPtr, ref lastCountdownFinishValue);
                bool countdownDestructEvent = ReadEventFlag(countdownDestructPtr, ref lastCountdownDestructValue);
                if (challengeProbeActive)
                {
                    if (challengeStartEvent)
                    {
                        bool alreadyPending =
                            pendingChallengeCountdownArmedAt != DateTime.MinValue &&
                            (now - pendingChallengeCountdownArmedAt).TotalSeconds <= 8.0;
                        if (!alreadyPending)
                        {
                            pendingChallengeCountdownArmedAt = now;
                            pendingChallengeCountdownFinishCount = 0;
                            Log("CHALLENGE_RESTART_EVENT pending_while_timer_active world=" + worldName);
                        }
                    }

                }

                bool recentPendingRestart =
                    pendingChallengeCountdownArmedAt != DateTime.MinValue &&
                    (now - pendingChallengeCountdownArmedAt).TotalSeconds <= 8.0;
                if (countdownDestructEvent && recentPendingRestart)
                {
                    Log("CHALLENGE_RESTART_COUNTDOWN_DESTRUCT pending world=" + worldName);
                }
                else if (countdownFinishEvent && recentPendingRestart)
                {
                    pendingChallengeCountdownFinishCount++;
                    Log("CHALLENGE_RESTART_COUNTDOWN pending count=" +
                        pendingChallengeCountdownFinishCount + " world=" + worldName);
                    challengeCountdownArmedAt = pendingChallengeCountdownArmedAt;
                    challengeCountdownFinishCount = pendingChallengeCountdownFinishCount;
                    pendingChallengeCountdownArmedAt = DateTime.MinValue;
                    pendingChallengeCountdownFinishCount = 0;
                    challengeProbeActive = false;
                    challengeProbeStartedAt = DateTime.MinValue;
                    challengeProbeLogCount = 0;
                    challengeStateStartSignal = false;
                    playerMovementStartSignal = false;
                    Log("CHALLENGE_RESTART_COUNTDOWN promoted_start_arm world=" + worldName);
                }

                if (challengeStartEvent && !challengeProbeActive)
                {
                    bool alreadyArmed =
                        challengeCountdownArmedAt != DateTime.MinValue &&
                        (now - challengeCountdownArmedAt).TotalSeconds <= 8.0;
                    if (!alreadyArmed)
                    {
                        challengeCountdownArmedAt = now;
                        challengeCountdownFinishCount = 0;
                        IntPtr charMovePtr = ReadPointerValue(characterMovementParentPtr);
                        lastCharMove2F0Value = ReadUInt32(IntPtr.Add(charMovePtr, 0x2F0));
                        charMove2F0Ready = charMovePtr != IntPtr.Zero;
                        ArmPlayerMovementProbe("challenge_start_event");
                        ArmChallengeStateProbe("challenge_start_event");
                    }
                }

                bool recentChallengeStart =
                    !challengeProbeActive &&
                    challengeCountdownArmedAt != DateTime.MinValue &&
                    (now - challengeCountdownArmedAt).TotalSeconds <= 8.0;
                bool loadingReadyForChallengeStart = IsChallengeLoadingReadyForStart();

                if (recentChallengeStart)
                    PollChallengeStartFromCharMove2F0();
                PollChallengeStateProbe("pre_start", recentChallengeStart);

                if (challengeStateStartSignal &&
                    recentChallengeStart)
                {
                    challengeStateStartSignal = false;
                    challengeCountdownArmedAt = DateTime.MinValue;
                    challengeCountdownFinishCount = 0;
                    pendingChallengeCountdownArmedAt = DateTime.MinValue;
                    pendingChallengeCountdownFinishCount = 0;
                    Log("CHALLENGE_START gamemode_timer_tick fallback_started world=" + worldName);
                    ArmChallengeFinishProbe();
                    flags |= ChallengeStarted | Started;
                }
                else if (countdownGoEvent &&
                    (challengeStartEvent || recentChallengeStart) &&
                    loadingReadyForChallengeStart)
                {
                    challengeCountdownArmedAt = DateTime.MinValue;
                    challengeCountdownFinishCount = 0;
                    pendingChallengeCountdownArmedAt = DateTime.MinValue;
                    pendingChallengeCountdownFinishCount = 0;
                    Log("CHALLENGE_START Countdown_C *Go* world=" + worldName);
                    ArmChallengeFinishProbe();
                    flags |= ChallengeStarted;
                }
                else if (countdownDestructEvent &&
                    (challengeStartEvent || recentChallengeStart) &&
                    loadingReadyForChallengeStart)
                {
                    Log("CHALLENGE_COUNTDOWN_DESTRUCT ignored_go_only world=" + worldName);
                }
                else if (countdownFinishEvent &&
                    (challengeStartEvent || recentChallengeStart) &&
                    loadingReadyForChallengeStart)
                {
                    double secondsAfterLoading = (now - lastLoadingUnloadAt).TotalSeconds;
                    double secondsAfterArm = (now - challengeCountdownArmedAt).TotalSeconds;
                    float gameModeSeconds = ReadChallengeGameModeTimerSeconds();
                    if (secondsAfterLoading >= ChallengePulse2MinSecondsAfterLoading &&
                        secondsAfterArm >= ChallengePulseMinSecondsAfterArm &&
                        gameModeSeconds <= 44.5f)
                    {
                        challengeCountdownArmedAt = DateTime.MinValue;
                        challengeCountdownFinishCount = 0;
                        pendingChallengeCountdownArmedAt = DateTime.MinValue;
                        pendingChallengeCountdownFinishCount = 0;
                        Log("CHALLENGE_START Countdown Finish pulse2_window t=" +
                            secondsAfterLoading.ToString("0.000") + " world=" + worldName);
                        ArmChallengeFinishProbe();
                        flags |= ChallengeStarted;
                    }
                    else
                    {
                        Log("CHALLENGE_COUNTDOWN_FINISH ignored_before_pulse2_window t=" +
                            secondsAfterLoading.ToString("0.000") +
                            " arm_t=" + secondsAfterArm.ToString("0.000") +
                            " game_t=" + gameModeSeconds.ToString("0.000") +
                            " world=" + worldName);
                    }
                }
                else if (challengeStartEvent)
                {
                    Log("CHALLENGE_START_EVENT armed_for_countdown_finish world=" + worldName);
                }
                else if (countdownFinishEvent)
                {
                    Log((challengeStartEvent || recentChallengeStart)
                        ? "CHALLENGE_COUNTDOWN_FINISH ignored_before_loading_unload world=" + worldName
                        : "CHALLENGE_COUNTDOWN_FINISH ignored_without_challenge_start world=" + worldName);
                }
                else if (countdownGoEvent)
                {
                    Log((challengeStartEvent || recentChallengeStart)
                        ? "CHALLENGE_COUNTDOWN_GO ignored_before_loading_unload world=" + worldName
                        : "CHALLENGE_COUNTDOWN_GO ignored_without_challenge_start world=" + worldName);
                }
                else if (countdownDestructEvent)
                {
                    Log("CHALLENGE_COUNTDOWN_DESTRUCT ignored_without_challenge_start world=" + worldName);
                }

                PollPlayerMovementProbe();
                if (playerMovementStartSignal && recentChallengeStart)
                {
                    playerMovementStartSignal = false;
                    Log("PLAYER_MOVEMENT_START_SIGNAL ignored_no_fallback world=" + worldName);
                }

                // MissionEnded fires when the result menu appears, so keep it logged as a suspect
                // but do not use it to stop the challenge timer.
                ReadEventFlag(challengeMissionEndedPtr, ref lastChallengeMissionEndedValue);

                if (ReadEventFlag(challengeGameModeAnyPtr, ref lastChallengeGameModeAnyValue))
                {
                    double challengeElapsed = challengeProbeActive
                        ? (DateTime.UtcNow - challengeProbeStartedAt).TotalSeconds
                        : 0.0;
                    Log("CHALLENGE_GAMEMODE_ANY t=" + challengeElapsed.ToString("0.000") + " world=" + worldName);
                }

                bool menuEventThisPoll = false;
                if (ReadEventFlag(menuMainMenuPtr, ref lastMenuMainMenuValue))
                {
                    menuEventThisPoll = true;
                    Log("MENU_EVENT MainMenu value=" + lastMenuMainMenuValue + " world=" + worldName);
                }
                if (ReadEventFlag(menuStorySelectPtr, ref lastMenuStorySelectValue))
                {
                    menuEventThisPoll = true;
                    Log("MENU_EVENT StoryLevelSelect value=" + lastMenuStorySelectValue + " world=" + worldName);
                }
                if (ReadEventFlag(menuStoryButtonPtr, ref lastMenuStoryButtonValue))
                {
                    menuEventThisPoll = true;
                    Log("MENU_EVENT StoryLevelButton value=" + lastMenuStoryButtonValue + " world=" + worldName);
                }
                if (ReadEventFlag(menuBackgroundPtr, ref lastMenuBackgroundValue))
                {
                    menuEventThisPoll = true;
                    Log("MENU_EVENT MenuBackground value=" + lastMenuBackgroundValue + " world=" + worldName);
                }
                PollDeepStateScan();
                PollDifficultyProbes();
                PollInGameMenuProbes();
                PollPauseMenuStateScan();
                challengeSplitIgnoresPause = false;
                bool restartCountdownPending =
                    pendingChallengeCountdownArmedAt != DateTime.MinValue &&
                    (now - pendingChallengeCountdownArmedAt).TotalSeconds <= 8.0;
                if (restartCountdownPending)
                {
                    Log("CHALLENGE_SPLIT_SUPPRESSED_RESTART_PENDING world=" + worldName);
                }
                else if (PollChallengeFinishProbes())
                {
                    if (!challengeSplitIgnoresPause && IsInGameMenuRecentlyActive())
                        Log("CHALLENGE_SPLIT_SUPPRESSED_INGAME_MENU world=" + worldName);
                    else
                        flags |= ChallengeSplit;
                }

                if (menuEventThisPoll || IsMenuWorld(worldName))
                    flags |= StartCanceled;

                return flags;
            }
            catch (Exception ex)
            {
                lastError = ex.GetType().Name + ": " + ex.Message;
                return Error;
            }
        }

        public static int ResetTimerState(string unused)
        {
            try
            {
                DateTime now = DateTime.UtcNow;
                bool forceClear = string.Equals(unused, "clear", StringComparison.OrdinalIgnoreCase);
                bool preserveCountdownArm =
                    !forceClear &&
                    challengeCountdownArmedAt != DateTime.MinValue &&
                    (now - challengeCountdownArmedAt).TotalSeconds <= 8.0;
                bool promotePendingCountdownArm =
                    !forceClear &&
                    pendingChallengeCountdownArmedAt != DateTime.MinValue &&
                    (now - pendingChallengeCountdownArmedAt).TotalSeconds <= 8.0;
                DateTime preservedCountdownArmedAt = challengeCountdownArmedAt;
                int preservedCountdownFinishCount = challengeCountdownFinishCount;

                if (!preserveCountdownArm && promotePendingCountdownArm)
                {
                    preserveCountdownArm = true;
                    preservedCountdownArmedAt = pendingChallengeCountdownArmedAt;
                    preservedCountdownFinishCount = pendingChallengeCountdownFinishCount;
                }

                if (!preserveCountdownArm)
                {
                    challengeCountdownArmedAt = DateTime.MinValue;
                    challengeCountdownFinishCount = 0;
                    charMove2F0Ready = false;
                    lastCharMove2F0Value = 0;
                }
                pendingChallengeCountdownArmedAt = DateTime.MinValue;
                pendingChallengeCountdownFinishCount = 0;
                challengeProbeStartedAt = DateTime.MinValue;
                challengeProbeActive = false;
                challengeProbeLogCount = 0;
                playerMovementProbeStartedAt = DateTime.MinValue;
                playerMovementProbeLogCount = 0;
                challengeStateStartSignal = false;
                playerMovementStartSignal = false;
                challengeStateProbeStartedAt = DateTime.MinValue;
                lastChallengeStateProbeAt = DateTime.MinValue;
                challengeStateProbeLogCount = 0;
                challengeStateSnapshot = null;
                lastAwardWidgetParentText = "";
                CompletedSplits.Clear();
                if (forceClear)
                {
                    lastChallengeGameModeDescription = "0x0";
                    lastChallengeMayhemDescription = "0x0";
                    lastStartParentDescription = "0x0";
                    lastSplitParentDescription = "0x0";
                    lastPublishedWorldName = "";
                }

                if (process != null && !process.HasExited)
                {
                    if (!preserveCountdownArm && challengeStartPtr != IntPtr.Zero)
                        lastChallengeStartValue = ReadFlagValue(challengeStartPtr);
                    if (!preserveCountdownArm && countdownGoPtr != IntPtr.Zero)
                        lastCountdownGoValue = ReadFlagValue(countdownGoPtr);
                    if (!preserveCountdownArm && countdownFinishPtr != IntPtr.Zero)
                        lastCountdownFinishValue = ReadFlagValue(countdownFinishPtr);
                    if (!preserveCountdownArm && countdownDestructPtr != IntPtr.Zero)
                        lastCountdownDestructValue = ReadFlagValue(countdownDestructPtr);
                    if (challengeAwardWidgetPtr != IntPtr.Zero)
                        lastChallengeAwardWidgetValue = ReadFlagValue(challengeAwardWidgetPtr);
                    if (challengeMissionEndedPtr != IntPtr.Zero)
                        lastChallengeMissionEndedValue = ReadFlagValue(challengeMissionEndedPtr);
                    if (challengeGameModeAnyPtr != IntPtr.Zero)
                        lastChallengeGameModeAnyValue = ReadFlagValue(challengeGameModeAnyPtr);
                }

                if (preserveCountdownArm)
                {
                    challengeCountdownArmedAt = preservedCountdownArmedAt;
                    challengeCountdownFinishCount = preservedCountdownFinishCount;
                    Log("BRIDGE_TIMER_RESET preserved challenge start arm count=" + challengeCountdownFinishCount);
                }
                else
                {
                    Log(forceClear
                        ? "BRIDGE_TIMER_RESET force cleared challenge start arm"
                        : "BRIDGE_TIMER_RESET cleared challenge start arm");
                }
                return 1;
            }
            catch (Exception ex)
            {
                lastError = ex.GetType().Name + ": " + ex.Message;
                return 0;
            }
        }

        public static int LastErrorLength(string unused)
        {
            return string.IsNullOrEmpty(lastError) ? 0 : lastError.Length;
        }

        public static int GetStoryDifficulty(string unused)
        {
            try
            {
                Configure();
                EnsureLoaded();
                EnsureProcess();
                if (process == null || process.HasExited)
                    return currentStoryDifficultyIndex;

                EnsureTools();
                AddWorldWatcher();
                uhara.Update();
                UpdateWorldName();
                UpdateStoryDifficultyFromParent("storyStart", startParentPtr);
                UpdateStoryDifficultyFromParent("storySplit", splitParentPtr);
                PollDifficultyProbes();
                UpdateStoryGameInstanceFromParent(startParentPtr);
                PollStoryGameInstanceScan();
                return currentStoryDifficultyIndex;
            }
            catch (Exception ex)
            {
                lastError = ex.GetType().Name + ": " + ex.Message;
                return currentStoryDifficultyIndex;
            }
        }

        static void Configure()
        {
            if (baseDir != null)
                return;

            baseDir = AssemblyDirectory();
            componentDir = Path.Combine(baseDir, "Components");
            logPath = Path.Combine(baseDir, "TSR-Timer-events.log");
            statePath = Path.Combine(baseDir, "TSR-Timer-state.txt");
            ResetLog();
            if (!assemblyResolverAdded)
            {
                AppDomain.CurrentDomain.AssemblyResolve += ResolveAssembly;
                assemblyResolverAdded = true;
            }
        }

        static string AssemblyDirectory()
        {
            string location = typeof(AutoSplitBridge).Assembly.Location;
            if (!string.IsNullOrEmpty(location))
                return Path.GetDirectoryName(location);

            return AppDomain.CurrentDomain.BaseDirectory;
        }

        static Assembly ResolveAssembly(object sender, ResolveEventArgs args)
        {
            string dllName = new AssemblyName(args.Name).Name + ".dll";
            string componentPath = Path.Combine(componentDir, dllName);
            if (File.Exists(componentPath))
                return Assembly.LoadFrom(componentPath);

            string basePath = Path.Combine(baseDir, dllName);
            if (File.Exists(basePath))
                return Assembly.LoadFrom(basePath);

            return null;
        }

        static void EnsureLoaded()
        {
            if (uhara != null)
                return;

            string uharaPath = Path.Combine(componentDir, "uhara10");
            uharaAssembly = Assembly.Load(File.ReadAllBytes(uharaPath));
            uhara = uharaAssembly.CreateInstance("Main");
            resolver = uharaAssembly.CreateInstance("PtrResolver");
            WireMemoryCallbacks();
        }

        static void EnsureProcess()
        {
            if (process != null && !process.HasExited)
                return;

            Process[] found = Process.GetProcessesByName(ProcessName);
            if (found.Length == 0)
            {
                process = null;
                resolverWatchAdded = false;
                return;
            }

            process = found[0];
            uhara.SetProcess(process);
            SetMainProcess(process);
            ResetTools();
        }

        static void ResetTools()
        {
            utils = null;
            eventsTool = null;
            resolverWatchAdded = false;
            worldName = "";
            lastPublishedWorldName = "";
            lastChallengeGameModeDescription = "0x0";
            lastChallengeMayhemDescription = "0x0";
            lastDifficultyDescription = "Unknown";
            storyGameInstancePtr = IntPtr.Zero;
            storyGameInstanceSnapshot = null;
            lastGameInstanceDifficultyByte = -1;
            CompletedSplits.Clear();

            startFlagPtr = IntPtr.Zero;
            splitFlagPtr = IntPtr.Zero;
            startParentPtr = IntPtr.Zero;
            splitParentPtr = IntPtr.Zero;
            menuMainMenuPtr = IntPtr.Zero;
            menuStorySelectPtr = IntPtr.Zero;
            menuStoryButtonPtr = IntPtr.Zero;
            menuBackgroundPtr = IntPtr.Zero;
            challengeStartPtr = IntPtr.Zero;
            countdownGoPtr = IntPtr.Zero;
            countdownFinishPtr = IntPtr.Zero;
            countdownDestructPtr = IntPtr.Zero;
            challengeMissionEndedPtr = IntPtr.Zero;
            challengeAwardWidgetPtr = IntPtr.Zero;
            challengeAwardWidgetParentPtr = IntPtr.Zero;
            challengeGameModeAnyPtr = IntPtr.Zero;
            challengeGameModeParentPtr = IntPtr.Zero;
            challengeMayhemParentPtr = IntPtr.Zero;
            hudTimeCounterParentPtr = IntPtr.Zero;
            gameStateParentPtr = IntPtr.Zero;
            tsGameStateParentPtr = IntPtr.Zero;
            gameInstanceParentPtr = IntPtr.Zero;
            broadStateParentPtr = IntPtr.Zero;
            characterMovementParentPtr = IntPtr.Zero;
            challengeFinishProbePtrs = null;
            challengeFinishProbeParentPtrs = null;
            challengeFinishProbeLabels = null;
            lastChallengeFinishProbeValues = null;
            challengeFinishProbeSeen = null;
            playerMovementProbePtrs = null;
            playerMovementProbeParentPtrs = null;
            playerMovementProbeLabels = null;
            lastPlayerMovementProbeValues = null;
            gameStateProbePtrs = null;
            gameStateProbeParentPtrs = null;
            gameStateProbeLabels = null;
            lastGameStateProbeValues = null;
            difficultyProbePtrs = null;
            difficultyProbeParentPtrs = null;
            difficultyProbeLabels = null;
            lastDifficultyProbeValues = null;
            inGameMenuProbePtrs = null;
            inGameMenuProbeParentPtrs = null;
            inGameMenuProbeLabels = null;
            lastInGameMenuProbeValues = null;
            pauseMenuStateObjectPtr = IntPtr.Zero;
            pauseMenuStateSnapshot = null;
            inGameMenuStateObjectPtr = IntPtr.Zero;
            inGameMenuStateSnapshot = null;

            lastStartFlagValue = 0;
            lastSplitFlagValue = 0;
            lastMenuMainMenuValue = 0;
            lastMenuStorySelectValue = 0;
            lastMenuStoryButtonValue = 0;
            lastMenuBackgroundValue = 0;
            lastChallengeStartValue = 0;
            lastCountdownGoValue = 0;
            lastCountdownFinishValue = 0;
            lastCountdownDestructValue = 0;
            lastChallengeMissionEndedValue = 0;
            lastChallengeAwardWidgetValue = 0;
            lastChallengeGameModeAnyValue = 0;
            lastCharMove2F0Value = 0;
            charMove2F0Ready = false;
            challengeCountdownArmedAt = DateTime.MinValue;
            lastLoadingUnloadAt = DateTime.MinValue;
            challengeCountdownFinishCount = 0;
            pendingChallengeCountdownArmedAt = DateTime.MinValue;
            pendingChallengeCountdownFinishCount = 0;
            challengeProbeStartedAt = DateTime.MinValue;
            challengeProbeActive = false;
            challengeProbeLogCount = 0;
            lastInGameMenuEventAt = DateTime.MinValue;
            pauseMenuStateActive = false;
            lastPauseMenuStateActiveAt = DateTime.MinValue;
            playerMovementProbeStartedAt = DateTime.MinValue;
            playerMovementProbeLogCount = 0;
            challengeStateSnapshot = null;
            challengeStateProbeStartedAt = DateTime.MinValue;
            lastChallengeStateProbeAt = DateTime.MinValue;
            challengeStateProbeLogCount = 0;
            challengeStateStartSignal = false;
            deepScanTargets = null;
            lastGameModeFinishValue = 0;
            gameModeFinishValueReady = false;
            lastGameModeFinishValueChangedAt = DateTime.MinValue;
            gameModeFinishStallLogged = false;
            lastHudTimeCounterValue = 0;
            hudTimeCounterReady = false;
            lastHudTimeCounterChangedAt = DateTime.MinValue;
            hudTimeCounterStallLogged = false;
            lastGameState300Value = 0;
            lastGameState318Value = 0;
            gameStateStopReady = false;
            gameModeStateFinishArmed = false;
            challengeSplitIgnoresPause = false;
            lastGraphicsSample = null;
            lastGraphicsSampleAt = DateTime.MinValue;
            graphicsSampleReady = false;
            graphicsFrameLogCount = 0;
            lastAwardWidgetParentText = "";
        }

        static void EnsureTools()
        {
            if (utils != null && eventsTool != null)
                return;

            utils = uharaAssembly.CreateInstance("Tools+UnrealEngine+Default+Utilities");
            eventsTool = uharaAssembly.CreateInstance("Tools+UnrealEngine+Default+Events");

            startFlagPtr = eventsTool.FunctionFlag("LOADING_C", "", "Destruct");
            splitFlagPtr = eventsTool.FunctionFlag("*Timer_C", "", "AllObjectivesComplete_Event");
            startParentPtr = eventsTool.FunctionParentPtr("LOADING_C", "", "Destruct");
            splitParentPtr = eventsTool.FunctionParentPtr("*Timer_C", "", "AllObjectivesComplete_Event");
            menuMainMenuPtr = eventsTool.FunctionFlag("MainMenu_C", null, "*");
            menuStorySelectPtr = eventsTool.FunctionFlag("StoryLevelSelect_C", null, "*");
            menuStoryButtonPtr = eventsTool.FunctionFlag("StoryLevelButton_C", null, "*");
            menuBackgroundPtr = eventsTool.FunctionFlag("Menu_Background_C", null, "*");
            challengeStartPtr = eventsTool.FunctionFlag("TS_ChallengeGameMode_C", null, "*Start*");
            countdownGoPtr = eventsTool.FunctionFlag("Countdown_C", null, "*Go*");
            countdownFinishPtr = eventsTool.FunctionFlag("Countdown_C", null, "*Finish*");
            countdownDestructPtr = eventsTool.FunctionFlag("Countdown_C", "", "Destruct");
            challengeMissionEndedPtr = eventsTool.FunctionFlag("MissionEnded_C", null, "*");
            challengeAwardWidgetPtr = eventsTool.FunctionFlag("AwardWidget_C", null, "*");
            challengeAwardWidgetParentPtr = eventsTool.FunctionParentPtr("AwardWidget_C", null, "*");
            challengeGameModeAnyPtr = eventsTool.FunctionFlag("TS_ChallengeGameMode_C", null, "*");
            challengeGameModeParentPtr = eventsTool.FunctionParentPtr("TS_ChallengeGameMode_C", null, "*");
            challengeMayhemParentPtr = eventsTool.FunctionParentPtr("TS_ChallengeMayhem_C", null, "*");
            hudTimeCounterParentPtr = eventsTool.FunctionParentPtr("TS_HudTimeCounter_C", "TS_TimeCounter", "*");
            gameStateParentPtr = eventsTool.FunctionParentPtr("*GameState*", null, "*");
            tsGameStateParentPtr = eventsTool.FunctionParentPtr("TS_GameState_C", null, "*");
            gameInstanceParentPtr = eventsTool.FunctionParentPtr("TS_GameInstance_C", null, "*");
            broadStateParentPtr = eventsTool.FunctionParentPtr("*State*", null, "*");
            characterMovementParentPtr = eventsTool.FunctionParentPtr("CharacterMovementComponent", "CharMoveComp", "*");
            challengeFinishProbeLabels = new string[] {
                "AWARD_VISIBILITY_EVENT",
                "AWARD_TEXT_EVENT",
                "AWARD_COLOR_EVENT",
                "AWARD_HUD_COUNTER_EVENT",
                "BROAD_EVENT_NULL_NAME",
                "BROAD_EVENT_EMPTY_NAME",
                "BROAD_EVENT_ANY_NAME"
            };
            challengeFinishProbePtrs = new IntPtr[] {
                eventsTool.FunctionFlag("VisibilityBinding", null, "*"),
                eventsTool.FunctionFlag("TextBinding", null, "*"),
                eventsTool.FunctionFlag("ColorBinding", null, "*"),
                eventsTool.FunctionFlag("TS_HudPointCounter_C", null, "*"),
                eventsTool.FunctionFlag("*", null, "*"),
                eventsTool.FunctionFlag("*", "", "*"),
                eventsTool.FunctionFlag("*", "*", "*")
            };
            challengeFinishProbeParentPtrs = new IntPtr[] {
                eventsTool.FunctionParentPtr("VisibilityBinding", null, "*"),
                eventsTool.FunctionParentPtr("TextBinding", null, "*"),
                eventsTool.FunctionParentPtr("ColorBinding", null, "*"),
                eventsTool.FunctionParentPtr("TS_HudPointCounter_C", null, "*"),
                eventsTool.FunctionParentPtr("*", null, "*"),
                eventsTool.FunctionParentPtr("*", "", "*"),
                eventsTool.FunctionParentPtr("*", "*", "*")
            };
            lastChallengeFinishProbeValues = new uint[challengeFinishProbePtrs.Length];
            challengeFinishProbeSeen = new bool[challengeFinishProbePtrs.Length];
            playerMovementProbeLabels = new string[] {
                "PLAYER_ANY_EVENT",
                "PLAYER_CONTROLLER_EVENT",
                "PLAYER_CHARACTER_EVENT",
                "CHARACTER_ANY_EVENT",
                "MOVEMENT_ANY_EVENT",
                "PAWN_ANY_EVENT",
                "CONTROLLER_ANY_EVENT",
                "MOVE_FUNCTION_EVENT",
                "INPUT_FUNCTION_EVENT",
                "POSSESS_FUNCTION_EVENT",
                "FROZEN_FUNCTION_EVENT",
                "ACTIVE_FUNCTION_EVENT"
            };
            playerMovementProbePtrs = new IntPtr[] {
                eventsTool.FunctionFlag("*Player*", null, "*"),
                eventsTool.FunctionFlag("*PlayerController*", null, "*"),
                eventsTool.FunctionFlag("*PlayerCharacter*", null, "*"),
                eventsTool.FunctionFlag("*Character*", null, "*"),
                eventsTool.FunctionFlag("*Movement*", null, "*"),
                eventsTool.FunctionFlag("*Pawn*", null, "*"),
                eventsTool.FunctionFlag("*Controller*", null, "*"),
                eventsTool.FunctionFlag("*", null, "*Move*"),
                eventsTool.FunctionFlag("*", null, "*Input*"),
                eventsTool.FunctionFlag("*", null, "*Possess*"),
                eventsTool.FunctionFlag("*", null, "*Frozen*"),
                eventsTool.FunctionFlag("*", null, "*Active*")
            };
            playerMovementProbeParentPtrs = new IntPtr[] {
                eventsTool.FunctionParentPtr("*Player*", null, "*"),
                eventsTool.FunctionParentPtr("*PlayerController*", null, "*"),
                eventsTool.FunctionParentPtr("*PlayerCharacter*", null, "*"),
                eventsTool.FunctionParentPtr("*Character*", null, "*"),
                eventsTool.FunctionParentPtr("*Movement*", null, "*"),
                eventsTool.FunctionParentPtr("*Pawn*", null, "*"),
                eventsTool.FunctionParentPtr("*Controller*", null, "*"),
                eventsTool.FunctionParentPtr("*", null, "*Move*"),
                eventsTool.FunctionParentPtr("*", null, "*Input*"),
                eventsTool.FunctionParentPtr("*", null, "*Possess*"),
                eventsTool.FunctionParentPtr("*", null, "*Frozen*"),
                eventsTool.FunctionParentPtr("*", null, "*Active*")
            };
            lastPlayerMovementProbeValues = new uint[playerMovementProbePtrs.Length];
            gameStateProbeLabels = new string[] {
                "GAMESTATE_ANY_EVENT",
                "TS_GAMESTATE_ANY_EVENT",
                "BROAD_STATE_ANY_EVENT",
                "CHALLENGE_GAMEMODE_ANY_EVENT",
                "CHALLENGE_MAYHEM_ANY_EVENT"
            };
            gameStateProbePtrs = new IntPtr[] {
                eventsTool.FunctionFlag("*GameState*", null, "*"),
                eventsTool.FunctionFlag("TS_GameState_C", null, "*"),
                eventsTool.FunctionFlag("*State*", null, "*"),
                eventsTool.FunctionFlag("TS_ChallengeGameMode_C", null, "*"),
                eventsTool.FunctionFlag("TS_ChallengeMayhem_C", null, "*")
            };
            gameStateProbeParentPtrs = new IntPtr[] {
                gameStateParentPtr,
                tsGameStateParentPtr,
                broadStateParentPtr,
                challengeGameModeParentPtr,
                challengeMayhemParentPtr
            };
            lastGameStateProbeValues = new uint[gameStateProbePtrs.Length];
            difficultyProbeLabels = new string[] {
                "EASY_EVENT",
                "NORMAL_EVENT",
                "HARD_EVENT",
                "DIFFICULTY_EVENT",
                "GAME_DIFFICULTY_EVENT"
            };
            difficultyProbePtrs = new IntPtr[] {
                eventsTool.FunctionFlag("*Easy*", null, "*"),
                eventsTool.FunctionFlag("*Normal*", null, "*"),
                eventsTool.FunctionFlag("*Hard*", null, "*"),
                eventsTool.FunctionFlag("*Difficulty*", null, "*"),
                eventsTool.FunctionFlag("*GameDifficulty*", null, "*")
            };
            difficultyProbeParentPtrs = new IntPtr[] {
                eventsTool.FunctionParentPtr("*Easy*", null, "*"),
                eventsTool.FunctionParentPtr("*Normal*", null, "*"),
                eventsTool.FunctionParentPtr("*Hard*", null, "*"),
                eventsTool.FunctionParentPtr("*Difficulty*", null, "*"),
                eventsTool.FunctionParentPtr("*GameDifficulty*", null, "*")
            };
            lastDifficultyProbeValues = new uint[difficultyProbePtrs.Length];
            inGameMenuProbeLabels = new string[] {
                "PAUSE_ANY_EVENT",
                "PAUSE_MENU_EVENT",
                "INGAME_MENU_EVENT",
                "INGAME_ANY_EVENT",
                "RETRY_EVENT",
                "RESUME_EVENT",
                "QUIT_EVENT",
                "OPTIONS_EVENT"
            };
            inGameMenuProbePtrs = new IntPtr[] {
                eventsTool.FunctionFlag("*Pause*", null, "*"),
                eventsTool.FunctionFlag("*PauseMenu*", null, "*"),
                eventsTool.FunctionFlag("*InGameMenu*", null, "*"),
                eventsTool.FunctionFlag("*InGame*", null, "*"),
                eventsTool.FunctionFlag("*Retry*", null, "*"),
                eventsTool.FunctionFlag("*Resume*", null, "*"),
                eventsTool.FunctionFlag("*Quit*", null, "*"),
                eventsTool.FunctionFlag("*Options*", null, "*")
            };
            inGameMenuProbeParentPtrs = new IntPtr[] {
                eventsTool.FunctionParentPtr("*Pause*", null, "*"),
                eventsTool.FunctionParentPtr("*PauseMenu*", null, "*"),
                eventsTool.FunctionParentPtr("*InGameMenu*", null, "*"),
                eventsTool.FunctionParentPtr("*InGame*", null, "*"),
                eventsTool.FunctionParentPtr("*Retry*", null, "*"),
                eventsTool.FunctionParentPtr("*Resume*", null, "*"),
                eventsTool.FunctionParentPtr("*Quit*", null, "*"),
                eventsTool.FunctionParentPtr("*Options*", null, "*")
            };
            lastInGameMenuProbeValues = new uint[inGameMenuProbePtrs.Length];

            lastStartFlagValue = ReadFlagValue(startFlagPtr);
            lastSplitFlagValue = ReadFlagValue(splitFlagPtr);
            lastMenuMainMenuValue = ReadFlagValue(menuMainMenuPtr);
            lastMenuStorySelectValue = ReadFlagValue(menuStorySelectPtr);
            lastMenuStoryButtonValue = ReadFlagValue(menuStoryButtonPtr);
            lastMenuBackgroundValue = ReadFlagValue(menuBackgroundPtr);
            lastChallengeStartValue = ReadFlagValue(challengeStartPtr);
            lastCountdownGoValue = ReadFlagValue(countdownGoPtr);
            lastCountdownFinishValue = ReadFlagValue(countdownFinishPtr);
            lastCountdownDestructValue = ReadFlagValue(countdownDestructPtr);
            lastChallengeMissionEndedValue = ReadFlagValue(challengeMissionEndedPtr);
            lastChallengeAwardWidgetValue = ReadFlagValue(challengeAwardWidgetPtr);
            lastChallengeGameModeAnyValue = ReadFlagValue(challengeGameModeAnyPtr);
            deepScanTargets = new DeepScanTarget[] {
                new DeepScanTarget { Name = "TS_ChallengeMayhem_C", ParentPtr = challengeMayhemParentPtr, LogBeforeFinish = true, LogLimit = 48 },
                new DeepScanTarget { Name = "TS_ChallengeGameMode_C", ParentPtr = challengeGameModeParentPtr, LogBeforeFinish = true, LogLimit = 64 },
                new DeepScanTarget { Name = "TS_HudTimeCounter_C/TS_TimeCounter", ParentPtr = hudTimeCounterParentPtr, LogBeforeFinish = true, LogLimit = 48 },
                new DeepScanTarget { Name = "GameState", ParentPtr = gameStateParentPtr, LogBeforeFinish = true, LogLimit = 96 },
                new DeepScanTarget { Name = "TS_GameState_C", ParentPtr = tsGameStateParentPtr, LogBeforeFinish = true, LogLimit = 96 },
                new DeepScanTarget { Name = "TS_GameInstance_C", ParentPtr = gameInstanceParentPtr, LogBeforeFinish = true },
                new DeepScanTarget { Name = "BroadState", ParentPtr = broadStateParentPtr },
                new DeepScanTarget { Name = "CharacterMovementComponent/CharMoveComp", ParentPtr = characterMovementParentPtr, LogBeforeFinish = true },
                new DeepScanTarget { Name = "AwardWidget_C", ParentPtr = challengeAwardWidgetParentPtr, LogBeforeFinish = true, LogLimit = 48 }
            };
            for (int i = 0; i < challengeFinishProbePtrs.Length; ++i)
                lastChallengeFinishProbeValues[i] = ReadFlagValue(challengeFinishProbePtrs[i]);
            for (int i = 0; i < playerMovementProbePtrs.Length; ++i)
                lastPlayerMovementProbeValues[i] = ReadFlagValue(playerMovementProbePtrs[i]);
            for (int i = 0; i < gameStateProbePtrs.Length; ++i)
                lastGameStateProbeValues[i] = ReadFlagValue(gameStateProbePtrs[i]);
            for (int i = 0; i < difficultyProbePtrs.Length; ++i)
                lastDifficultyProbeValues[i] = ReadFlagValue(difficultyProbePtrs[i]);
            for (int i = 0; i < inGameMenuProbePtrs.Length; ++i)
                lastInGameMenuProbeValues[i] = ReadFlagValue(inGameMenuProbePtrs[i]);
            Log("registered challenge finish probes world=" + worldName);
        }

        static void SetMainProcess(Process target)
        {
            Type mainType = uhara.GetType();
            const BindingFlags flags = BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public;

            PropertyInfo prop = mainType.GetProperty("ProcessInstance", flags);
            if (prop != null)
                prop.SetValue(null, target, null);

            FieldInfo field = mainType.GetField("bf_ProcessInstance", flags);
            if (field != null)
                field.SetValue(null, target);
        }

        static void WireMemoryCallbacks()
        {
            Type mainType = uhara.GetType();
            const BindingFlags flags = BindingFlags.Static | BindingFlags.NonPublic;
            Type bridgeType = typeof(AutoSplitBridge);

            SetMethod(mainType, flags, "_RefAllocateMemory", bridgeType.GetMethod("RefAllocateMemory", BindingFlags.Static | BindingFlags.NonPublic));
            SetMethod(mainType, flags, "_RefReadBytes", bridgeType.GetMethod("RefReadBytes", BindingFlags.Static | BindingFlags.NonPublic));
            SetMethod(mainType, flags, "_RefWriteBytes", bridgeType.GetMethod("RefWriteBytes", BindingFlags.Static | BindingFlags.NonPublic));
            SetMethod(mainType, flags, "_RefCreateThread", bridgeType.GetMethod("RefCreateThread", BindingFlags.Static | BindingFlags.NonPublic));
        }

        static void SetMethod(Type targetType, BindingFlags flags, string fieldName, MethodInfo method)
        {
            FieldInfo field = targetType.GetField(fieldName, flags);
            if (field != null && method != null)
                field.SetValue(null, method);
        }

        static IntPtr RefAllocateMemory(Process target, int size)
        {
            return VirtualAllocEx(target.Handle, IntPtr.Zero, new UIntPtr((uint)size), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        }

        static byte[] RefReadBytes(Process target, IntPtr address, int count)
        {
            byte[] buffer = new byte[count];
            IntPtr read;
            ReadProcessMemory(target.Handle, address, buffer, count, out read);
            return buffer;
        }

        static void RefWriteBytes(Process target, IntPtr address, byte[] bytes)
        {
            IntPtr written;
            WriteProcessMemory(target.Handle, address, bytes, bytes.Length, out written);
        }

        static object RefCreateThread(Process target, IntPtr address)
        {
            uint threadId;
            return CreateRemoteThread(target.Handle, IntPtr.Zero, 0, address, IntPtr.Zero, 0, out threadId);
        }

        static void AddWorldWatcher()
        {
            if (resolverWatchAdded)
                return;

            resolver.Watch<uint>("WorldFName", utils.GWorld, 0x18);
            resolverWatchAdded = true;
        }

        static void UpdateWorldName()
        {
            object fName = resolver["WorldFName"];
            string name = Convert.ToString(utils.FNameToString(fName));
            if (!string.IsNullOrEmpty(name) && name != "None")
                worldName = name;
        }

        static void PublishState()
        {
            if (string.IsNullOrEmpty(statePath))
                return;

            IntPtr challengeGameModePtr = ReadPointerValue(challengeGameModeParentPtr);
            IntPtr challengeMayhemPtr = ReadPointerValue(challengeMayhemParentPtr);
            IntPtr startParent = ReadPointerValue(startParentPtr);
            IntPtr splitParent = ReadPointerValue(splitParentPtr);
            if (startParent != IntPtr.Zero)
                lastStartParentDescription = DescribeUObject(startParent);
            if (splitParent != IntPtr.Zero)
                lastSplitParentDescription = DescribeUObject(splitParent);
            if (challengeGameModePtr != IntPtr.Zero)
                lastChallengeGameModeDescription = DescribeUObject(challengeGameModePtr);
            if (challengeMayhemPtr != IntPtr.Zero)
                lastChallengeMayhemDescription = DescribeUObject(challengeMayhemPtr);
            string difficultyDescription = lastDifficultyDescription;
            if (storyGameInstancePtr != IntPtr.Zero)
                difficultyDescription += " gameInstance=" + DescribeUObject(storyGameInstancePtr);

            string text =
                "world=" + (worldName ?? "") + Environment.NewLine +
                "difficulty=" + DifficultyName(currentStoryDifficultyIndex) + Environment.NewLine +
                "difficultySource=" + difficultyDescription + Environment.NewLine +
                "storyStart=" + lastStartParentDescription + Environment.NewLine +
                "storySplit=" + lastSplitParentDescription + Environment.NewLine +
                "challengeGameMode=" + lastChallengeGameModeDescription + Environment.NewLine +
                "challengeMayhem=" + lastChallengeMayhemDescription + Environment.NewLine +
                ChallengeCandidateState(challengeGameModePtr) +
                ChallengeCandidateState(challengeMayhemPtr);
            if (text == lastPublishedWorldName)
                return;

            lastPublishedWorldName = text;
            try
            {
                File.WriteAllText(statePath, text);
            }
            catch
            {
            }
        }

        static string ChallengeCandidateState(IntPtr objectPtr)
        {
            if (objectPtr == IntPtr.Zero)
                return "";

            StringBuilder text = new StringBuilder();
            HashSet<long> seen = new HashSet<long>();
            AppendChallengeCandidate(text, seen, objectPtr);

            const int scanSize = 0x900;
            byte[] bytes = ReadBytes(objectPtr, scanSize);
            if (bytes == null)
                return text.ToString();

            for (int offset = 0x30; offset + IntPtr.Size <= bytes.Length && seen.Count < 40; offset += IntPtr.Size)
            {
                long raw = IntPtr.Size == 8
                    ? BitConverter.ToInt64(bytes, offset)
                    : BitConverter.ToInt32(bytes, offset);
                if (raw < 0x10000)
                    continue;

                AppendChallengeCandidate(text, seen, new IntPtr(raw));
            }

            return text.ToString();
        }

        static void AppendChallengeCandidate(StringBuilder text, HashSet<long> seen, IntPtr objectPtr)
        {
            long address = objectPtr.ToInt64();
            if (address == 0 || seen.Contains(address))
                return;

            string description = DescribeUObject(objectPtr);
            if (!LooksLikeChallengeCandidate(description))
                return;

            seen.Add(address);
            text.Append("challengeCandidate=").Append(description).Append(Environment.NewLine);
        }

        static bool LooksLikeChallengeCandidate(string description)
        {
            if (string.IsNullOrEmpty(description) || description == "0x0")
                return false;

            return description.IndexOf("Challenge", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   description.IndexOf("Barrel", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   description.IndexOf("Roll", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   description.IndexOf("Mayhem", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        static uint ReadFlagValue(IntPtr ptr)
        {
            if (process == null || process.HasExited || ptr == IntPtr.Zero)
                return 0;

            byte[] buffer = new byte[4];
            IntPtr read;
            if (!ReadProcessMemory(process.Handle, ptr, buffer, buffer.Length, out read) || read.ToInt64() != buffer.Length)
                return 0;

            return BitConverter.ToUInt32(buffer, 0);
        }

        static uint ReadUInt32(IntPtr ptr)
        {
            return ReadFlagValue(ptr);
        }

        static bool ReadEventFlag(IntPtr ptr, ref uint lastValue)
        {
            uint value = ReadFlagValue(ptr);
            if (value == lastValue)
                return false;

            lastValue = value;
            return value != 0;
        }

        static bool PollChallengeStartFromCharMove2F0()
        {
            if (characterMovementParentPtr == IntPtr.Zero ||
                challengeCountdownArmedAt == DateTime.MinValue)
                return false;

            double armedSeconds = (DateTime.UtcNow - challengeCountdownArmedAt).TotalSeconds;
            if (armedSeconds < 2.5 || armedSeconds > 8.0)
                return false;

            IntPtr charMovePtr = ReadPointerValue(characterMovementParentPtr);
            if (charMovePtr == IntPtr.Zero)
                return false;

            uint value = ReadUInt32(IntPtr.Add(charMovePtr, 0x2F0));
            if (!charMove2F0Ready)
            {
                lastCharMove2F0Value = value;
                charMove2F0Ready = true;
                return false;
            }

            uint previous = lastCharMove2F0Value;
            lastCharMove2F0Value = value;
            if (previous == value)
                return false;

            float gameModeSeconds = ReadChallengeGameModeTimerSeconds();
            Log("CHARMOVE_2F0_START_CANDIDATE t=" + armedSeconds.ToString("0.000") +
                " " + previous + " -> " + value +
                " gm430=" + gameModeSeconds.ToString("0.###") +
                " world=" + worldName);
            return previous == 3 && value == 0 && gameModeSeconds >= 60.0f;
        }

        static bool IsChallengeLoadingReadyForStart()
        {
            if (challengeCountdownArmedAt == DateTime.MinValue ||
                lastLoadingUnloadAt == DateTime.MinValue)
                return false;

            DateTime now = DateTime.UtcNow;
            double armedSeconds = (now - challengeCountdownArmedAt).TotalSeconds;
            double loadingSeconds = (now - lastLoadingUnloadAt).TotalSeconds;
            if (armedSeconds < 0.0 || armedSeconds > 8.0 ||
                loadingSeconds < 0.0 || loadingSeconds > 8.0)
                return false;

            // The game can report the challenge start arm just before or just after loading unload.
            return lastLoadingUnloadAt >= challengeCountdownArmedAt.AddSeconds(-1.0);
        }

        static float ReadChallengeGameModeTimerSeconds()
        {
            IntPtr gameModePtr = ReadPointerValue(challengeGameModeParentPtr);
            if (gameModePtr == IntPtr.Zero)
                return 0.0f;

            uint raw = ReadUInt32(IntPtr.Add(gameModePtr, 0x430));
            try
            {
                float value = BitConverter.ToSingle(BitConverter.GetBytes(raw), 0);
                if (float.IsNaN(value) || float.IsInfinity(value))
                    return 0.0f;
                return value;
            }
            catch
            {
                return 0.0f;
            }
        }

        static void ArmChallengeFinishProbe()
        {
            if (challengeFinishProbePtrs == null || lastChallengeFinishProbeValues == null)
                return;

            challengeProbeActive = true;
            challengeProbeStartedAt = DateTime.UtcNow;
            challengeProbeLogCount = 0;
            ArmPlayerMovementProbe("timer_started");
            ArmChallengeStateProbe("timer_started");

            for (int i = 0; i < challengeFinishProbePtrs.Length; ++i)
            {
                lastChallengeFinishProbeValues[i] = ReadFlagValue(challengeFinishProbePtrs[i]);
                if (challengeFinishProbeSeen != null && i < challengeFinishProbeSeen.Length)
                    challengeFinishProbeSeen[i] = false;
            }

            lastChallengeAwardWidgetValue = ReadFlagValue(challengeAwardWidgetPtr);
            ResetDeepScanSnapshots();
            IntPtr gameModePtr = ReadPointerValue(challengeGameModeParentPtr);
            lastGameModeFinishValue = ReadUInt32(IntPtr.Add(gameModePtr, 0x430));
            gameModeFinishValueReady = gameModePtr != IntPtr.Zero;
            lastGameModeFinishValueChangedAt = DateTime.UtcNow;
            gameModeFinishStallLogged = false;
            IntPtr hudTimePtr = ReadPointerValue(hudTimeCounterParentPtr);
            lastHudTimeCounterValue = ReadUInt32(IntPtr.Add(hudTimePtr, 0x240));
            hudTimeCounterReady = hudTimePtr != IntPtr.Zero;
            lastHudTimeCounterChangedAt = DateTime.UtcNow;
            hudTimeCounterStallLogged = false;
            IntPtr gameStatePtr = ReadPointerValue(gameStateParentPtr);
            lastGameState300Value = ReadUInt32(IntPtr.Add(gameStatePtr, 0x300));
            lastGameState318Value = ReadUInt32(IntPtr.Add(gameStatePtr, 0x318));
            gameStateStopReady = gameStatePtr != IntPtr.Zero;
            gameModeStateFinishArmed = false;
            lastGraphicsSample = null;
            lastGraphicsSampleAt = DateTime.MinValue;
            graphicsSampleReady = false;
            graphicsFrameLogCount = 0;
            lastAwardWidgetParentText = "";
            Log("CHALLENGE_GAMEMODE_430 armed value=" + lastGameModeFinishValue);
            Log("CHALLENGE_HUD_TIMER_240 armed value=" + lastHudTimeCounterValue);
            Log("CHALLENGE_GAMESTATE_STOP armed 0x300=" + lastGameState300Value +
                " 0x318=" + lastGameState318Value);
            Log("CHALLENGE_BROAD_LOG armed world=" + worldName);
        }

        static void ArmChallengeStateProbe(string reason)
        {
            IntPtr gameModePtr = ReadPointerValue(challengeGameModeParentPtr);
            challengeStateSnapshot = gameModePtr != IntPtr.Zero
                ? ReadBytes(IntPtr.Add(gameModePtr, 0x250), 0x230)
                : null;
            challengeStateProbeStartedAt = DateTime.UtcNow;
            lastChallengeStateProbeAt = DateTime.MinValue;
            challengeStateProbeLogCount = 0;
            Log("CHALLENGE_STATE_PROBE armed reason=" + reason +
                " ptr=0x" + gameModePtr.ToInt64().ToString("X") +
                " world=" + worldName);
        }

        static void PollChallengeStateProbe(string phase, bool enabled)
        {
            if (!enabled || challengeStateProbeStartedAt == DateTime.MinValue)
                return;

            DateTime now = DateTime.UtcNow;
            if (lastChallengeStateProbeAt != DateTime.MinValue &&
                (now - lastChallengeStateProbeAt).TotalMilliseconds < 16.0)
                return;
            lastChallengeStateProbeAt = now;

            IntPtr gameModePtr = ReadPointerValue(challengeGameModeParentPtr);
            if (gameModePtr == IntPtr.Zero)
                return;

            byte[] bytes = ReadBytes(IntPtr.Add(gameModePtr, 0x250), 0x230);
            if (bytes == null || bytes.Length < 0x230)
                return;

            if (challengeStateSnapshot == null || challengeStateSnapshot.Length != bytes.Length)
            {
                challengeStateSnapshot = bytes;
                Log("CHALLENGE_STATE_PROBE snapshot_reset phase=" + phase + " world=" + worldName);
                return;
            }

            double elapsed = (now - challengeStateProbeStartedAt).TotalSeconds;
            if (phase == "pre_start" && !challengeStateStartSignal)
            {
                int elapsedIndex = 0x428 - 0x250;
                int remainingIndex = 0x430 - 0x250;
                float oldElapsed = BitConverter.ToSingle(challengeStateSnapshot, elapsedIndex);
                float newElapsed = BitConverter.ToSingle(bytes, elapsedIndex);
                float oldRemaining = BitConverter.ToSingle(challengeStateSnapshot, remainingIndex);
                float newRemaining = BitConverter.ToSingle(bytes, remainingIndex);
                if (oldElapsed >= 0.0f &&
                    newElapsed > oldElapsed &&
                    oldRemaining > newRemaining &&
                    (oldRemaining - newRemaining) <= 2.0f &&
                    oldRemaining >= 1.0f &&
                    oldRemaining <= 1000.0f)
                {
                    challengeStateStartSignal = true;
                    Log("CHALLENGE_STATE_START_SIGNAL t=" + elapsed.ToString("0.000") +
                        " mode=timer_tick" +
                        " elapsed=0x428 " + oldElapsed.ToString("0.000") +
                        "->" + newElapsed.ToString("0.000") +
                        " remaining=0x430 " + oldRemaining.ToString("0.000") +
                        "->" + newRemaining.ToString("0.000") +
                        " world=" + worldName);
                }
            }

            int logged = 0;
            for (int index = 0; index + 4 <= bytes.Length; index += 4)
            {
                int offset = 0x250 + index;
                uint oldValue = BitConverter.ToUInt32(challengeStateSnapshot, index);
                uint newValue = BitConverter.ToUInt32(bytes, index);
                if (oldValue == newValue)
                    continue;

                if (!ShouldLogChallengeStateOffset(offset, oldValue, newValue))
                    continue;

                if (challengeStateProbeLogCount >= 260 || logged >= 16)
                    break;

                challengeStateProbeLogCount++;
                logged++;
                Log("CHALLENGE_STATE_CHANGE phase=" + phase +
                    " t=" + elapsed.ToString("0.000") +
                    " n=" + challengeStateProbeLogCount +
                    " off=0x" + offset.ToString("X") +
                    " u32=" + oldValue + "->" + newValue +
                    " i32=" + ((int)oldValue).ToString() + "->" + ((int)newValue).ToString() +
                    " f32=" + SafeFloat(challengeStateSnapshot, index) + "->" + SafeFloat(bytes, index));
            }

            challengeStateSnapshot = bytes;
        }

        static bool ShouldLogChallengeStateOffset(int offset, uint oldValue, uint newValue)
        {
            if (offset >= 0x420 && offset <= 0x438)
                return true;

            if (offset >= 0x2A0 && offset <= 0x2D0)
                return true;

            if (offset >= 0x2E8 && offset <= 0x310)
                return true;

            if (oldValue <= 8 || newValue <= 8)
                return true;

            return false;
        }

        static void ArmPlayerMovementProbe(string reason)
        {
            playerMovementProbeStartedAt = DateTime.UtcNow;
            playerMovementProbeLogCount = 0;
            playerMovementStartSignal = false;

            if (playerMovementProbePtrs != null && lastPlayerMovementProbeValues != null)
            {
                for (int i = 0; i < playerMovementProbePtrs.Length; ++i)
                    lastPlayerMovementProbeValues[i] = ReadFlagValue(playerMovementProbePtrs[i]);
            }

            Log("PLAYER_MOVEMENT_PROBE armed reason=" + reason + " world=" + worldName);
        }

        static void PollPlayerMovementProbe()
        {
            if (playerMovementProbePtrs == null ||
                playerMovementProbeParentPtrs == null ||
                playerMovementProbeLabels == null ||
                lastPlayerMovementProbeValues == null ||
                playerMovementProbeStartedAt == DateTime.MinValue)
                return;

            DateTime now = DateTime.UtcNow;
            double elapsed = (now - playerMovementProbeStartedAt).TotalSeconds;
            bool startWindow =
                challengeCountdownArmedAt != DateTime.MinValue &&
                (now - challengeCountdownArmedAt).TotalSeconds <= 8.0;
            bool activeWindow = challengeProbeActive && elapsed <= 12.0;
            if (!startWindow && !activeWindow)
                return;

            for (int i = 0; i < playerMovementProbePtrs.Length; ++i)
            {
                uint value = ReadFlagValue(playerMovementProbePtrs[i]);
                if (value == lastPlayerMovementProbeValues[i])
                    continue;

                uint previous = lastPlayerMovementProbeValues[i];
                lastPlayerMovementProbeValues[i] = value;

                string parent = i < playerMovementProbeParentPtrs.Length
                    ? DescribeUObject(ReadPointerValue(playerMovementProbeParentPtrs[i]))
                    : "";
                if (!ShouldLogPlayerMovementProbe(playerMovementProbeLabels[i], parent))
                    continue;

                if (playerMovementProbeLogCount >= 220)
                    return;

                playerMovementProbeLogCount++;
                string phase = challengeProbeActive ? "timer_active" : "pre_start";
                Log("PLAYER_MOVEMENT_EVENT " + playerMovementProbeLabels[i] +
                    " phase=" + phase +
                    " t=" + elapsed.ToString("0.000") +
                    " n=" + playerMovementProbeLogCount +
                    " " + previous + " -> " + value +
                    " world=" + worldName +
                    " parent=" + parent);

                if (startWindow &&
                    !challengeProbeActive &&
                    !playerMovementStartSignal &&
                    ShouldUsePlayerMovementStart(playerMovementProbeLabels[i], parent))
                {
                    playerMovementStartSignal = true;
                    Log("PLAYER_MOVEMENT_START_SIGNAL " + playerMovementProbeLabels[i] +
                        " t=" + elapsed.ToString("0.000") +
                        " " + previous + " -> " + value +
                        " world=" + worldName +
                        " parent=" + parent);
                    return;
                }
            }
        }

        static bool ShouldUsePlayerMovementStart(string label, string parent)
        {
            if (string.IsNullOrEmpty(parent))
                return false;

            if (parent.IndexOf("TS_PlayerController_C", StringComparison.OrdinalIgnoreCase) < 0)
                return false;

            return label.IndexOf("MOVE", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   label.IndexOf("INPUT", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   label.IndexOf("PLAYER_CONTROLLER", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        static bool ShouldLogPlayerMovementProbe(string label, string parent)
        {
            if (string.IsNullOrEmpty(parent))
                return false;

            if (parent.IndexOf("AwardWidget", StringComparison.OrdinalIgnoreCase) >= 0 ||
                parent.IndexOf("TextBinding", StringComparison.OrdinalIgnoreCase) >= 0 ||
                parent.IndexOf("VisibilityBinding", StringComparison.OrdinalIgnoreCase) >= 0 ||
                parent.IndexOf("ColorBinding", StringComparison.OrdinalIgnoreCase) >= 0 ||
                parent.IndexOf("Menu", StringComparison.OrdinalIgnoreCase) >= 0)
                return false;

            return parent.IndexOf("Player", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("Pawn", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("Character", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("Movement", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("Controller", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("Input", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   label.IndexOf("MOVE", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   label.IndexOf("INPUT", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   label.IndexOf("POSSESS", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        static bool PollChallengeFinishProbes()
        {
            if (!challengeProbeActive ||
                challengeFinishProbePtrs == null ||
                lastChallengeFinishProbeValues == null ||
                challengeFinishProbeLabels == null)
                return false;

            bool challengeSplit = false;

            if (ReadEventFlag(challengeAwardWidgetPtr, ref lastChallengeAwardWidgetValue))
            {
                double awardElapsed = (DateTime.UtcNow - challengeProbeStartedAt).TotalSeconds;
                string awardParent = DescribeUObject(ReadPointerValue(challengeAwardWidgetParentPtr));
                if (awardParent != lastAwardWidgetParentText)
                {
                    lastAwardWidgetParentText = awardParent;
                    Log("AWARD_WIDGET_PARENT_CHANGE t=" + awardElapsed.ToString("0.000") +
                        " world=" + worldName +
                        " parent=" + awardParent);
                }
            }

            if (PollGameStateStop())
                return true;

            PollHudTimeCounterStop();

            PollGameModeFinishOffset();
            PollGameStateProbes();

            PollChallengeStateProbe("active", challengeProbeActive);
            // Graphics logging was useful for correlation, but event logging is the cleaner
            // path for finding what causes the award widget to become visible.
            // PollGraphicsChange();

            for (int i = 0; i < challengeFinishProbePtrs.Length; ++i)
            {
                uint value = ReadFlagValue(challengeFinishProbePtrs[i]);
                if (value == lastChallengeFinishProbeValues[i])
                    continue;

                uint previous = lastChallengeFinishProbeValues[i];
                lastChallengeFinishProbeValues[i] = value;

                bool first = challengeFinishProbeSeen != null &&
                             i < challengeFinishProbeSeen.Length &&
                             !challengeFinishProbeSeen[i];
                if (challengeFinishProbeSeen != null && i < challengeFinishProbeSeen.Length)
                    challengeFinishProbeSeen[i] = true;

                challengeProbeLogCount++;
                double elapsed = (DateTime.UtcNow - challengeProbeStartedAt).TotalSeconds;
                string parent = (challengeFinishProbeParentPtrs != null && i < challengeFinishProbeParentPtrs.Length)
                    ? DescribeUObject(ReadPointerValue(challengeFinishProbeParentPtrs[i]))
                    : "";
                if (!ShouldLogAwardAppearProbe(challengeFinishProbeLabels[i], parent))
                    continue;

                string prefix = first ? "FIRST " : "";
                Log(prefix + "AWARD_APPEAR_RELATED " + challengeFinishProbeLabels[i] +
                    " t=" + elapsed.ToString("0.000") +
                    " n=" + challengeProbeLogCount +
                    " " + previous + " -> " + value +
                    " world=" + worldName +
                    " parent=" + parent);

                // AwardWidget can already be visible in some challenges, so it is only
                // logged for correlation. Do not split the timer from award UI events.
            }

            PollDeepStateScan();
            return challengeSplit;
        }

        static void PollGameStateProbes()
        {
            if (!challengeProbeActive ||
                gameStateProbePtrs == null ||
                lastGameStateProbeValues == null ||
                gameStateProbeLabels == null)
                return;

            double elapsed = (DateTime.UtcNow - challengeProbeStartedAt).TotalSeconds;
            for (int i = 0; i < gameStateProbePtrs.Length; ++i)
            {
                if (!ReadEventFlag(gameStateProbePtrs[i], ref lastGameStateProbeValues[i]))
                    continue;

                string parent = (gameStateProbeParentPtrs != null && i < gameStateProbeParentPtrs.Length)
                    ? DescribeUObject(ReadPointerValue(gameStateProbeParentPtrs[i]))
                    : "";
                Log("CHALLENGE_GAMESTATE_EVENT " + gameStateProbeLabels[i] +
                    " t=" + elapsed.ToString("0.000") +
                    " value=" + lastGameStateProbeValues[i] +
                    " world=" + worldName +
                    " parent=" + parent);
            }
        }

        static void PollDifficultyProbes()
        {
            if (difficultyProbePtrs == null ||
                difficultyProbeParentPtrs == null ||
                difficultyProbeLabels == null ||
                lastDifficultyProbeValues == null)
                return;

            for (int i = 0; i < difficultyProbePtrs.Length; ++i)
            {
                if (!ReadEventFlag(difficultyProbePtrs[i], ref lastDifficultyProbeValues[i]))
                    continue;

                string parent = i < difficultyProbeParentPtrs.Length
                    ? DescribeUObject(ReadPointerValue(difficultyProbeParentPtrs[i]))
                    : "";
                int detected = DifficultyFromTextOrCurrent(parent);
                if (detected >= 0)
                {
                    currentStoryDifficultyIndex = detected;
                    lastDifficultyDescription = difficultyProbeLabels[i] + " parent=" + parent;
                    Log("DIFFICULTY_DETECTED " + DifficultyName(detected) +
                        " source=" + lastDifficultyDescription);
                }
                else
                {
                    lastDifficultyDescription = difficultyProbeLabels[i] + " parent=" + parent;
                    Log("DIFFICULTY_EVENT unknown source=" + lastDifficultyDescription);
                }
            }
        }

        static void PollInGameMenuProbes()
        {
            if (inGameMenuProbePtrs == null ||
                inGameMenuProbeParentPtrs == null ||
                inGameMenuProbeLabels == null ||
                lastInGameMenuProbeValues == null)
                return;

            for (int i = 0; i < inGameMenuProbePtrs.Length; ++i)
            {
                if (!ReadEventFlag(inGameMenuProbePtrs[i], ref lastInGameMenuProbeValues[i]))
                    continue;

                string parent = i < inGameMenuProbeParentPtrs.Length
                    ? DescribeUObject(ReadPointerValue(inGameMenuProbeParentPtrs[i]))
                    : "";
                if (IsInGameMenuCooldownProbe(inGameMenuProbeLabels[i]) && IsInGameMenuProbeParent(parent))
                    lastInGameMenuEventAt = DateTime.UtcNow;
                if (inGameMenuProbeLabels[i] != "PAUSE_ANY_EVENT" &&
                    inGameMenuProbeLabels[i] != "PAUSE_MENU_EVENT")
                {
                    Log("INGAME_MENU_PROBE " + inGameMenuProbeLabels[i] +
                        " value=" + lastInGameMenuProbeValues[i] +
                        " world=" + worldName +
                        " parent=" + parent);
                }
            }
        }

        static void PollPauseMenuStateScan()
        {
            if (inGameMenuProbeParentPtrs == null || inGameMenuProbeParentPtrs.Length < 2)
                return;

            IntPtr pauseMenuPtr = ReadPointerValue(inGameMenuProbeParentPtrs[1]);
            if (pauseMenuPtr == IntPtr.Zero)
                SetPauseMenuStateActive(false, "ptr_zero");
            PollPauseStateObject("PAUSE_MENU_STATE", pauseMenuPtr,
                ref pauseMenuStateObjectPtr, ref pauseMenuStateSnapshot);

            if (inGameMenuProbeParentPtrs.Length >= 3)
            {
                PollPauseStateObject("INGAME_MENU_STATE", ReadPointerValue(inGameMenuProbeParentPtrs[2]),
                    ref inGameMenuStateObjectPtr, ref inGameMenuStateSnapshot);
            }
        }

        static void PollPauseStateObject(string name, IntPtr objectPtr, ref IntPtr knownObjectPtr, ref byte[] snapshot)
        {
            if (objectPtr == IntPtr.Zero)
                return;

            const int scanSize = 0x500;
            byte[] bytes = ReadBytes(objectPtr, scanSize);
            if (bytes == null)
                return;

            if (knownObjectPtr != objectPtr || snapshot == null)
            {
                knownObjectPtr = objectPtr;
                snapshot = bytes;
                UpdatePauseMenuStateActive(name, bytes, "target");
                Log(name + "_TARGET ptr=0x" + objectPtr.ToInt64().ToString("X") +
                    " " + DescribeUObject(objectPtr));
                return;
            }

            UpdatePauseMenuStateActive(name, bytes, "poll");

            int logged = 0;
            int logLimit = string.Equals(name, "PAUSE_MENU_STATE", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(name, "INGAME_MENU_STATE", StringComparison.OrdinalIgnoreCase)
                    ? 160
                    : 32;
            for (int offset = 0x30; offset < bytes.Length && logged < logLimit; ++offset)
            {
                byte oldByte = snapshot[offset];
                byte newByte = bytes[offset];
                if (oldByte == newByte)
                    continue;

                uint oldU32 = 0;
                uint newU32 = 0;
                if (offset + 4 <= bytes.Length)
                {
                    oldU32 = BitConverter.ToUInt32(snapshot, offset);
                    newU32 = BitConverter.ToUInt32(bytes, offset);
                }

                Log(name + "_CHANGE off=0x" + offset.ToString("X") +
                    " byte=" + oldByte + "->" + newByte +
                    " u32=" + oldU32 + "->" + newU32);
                logged++;
            }

            if (logged >= logLimit)
                Log(name + "_CHANGE_LIMIT more_changes_suppressed");

            snapshot = bytes;
        }

        static void UpdatePauseMenuStateActive(string name, byte[] bytes, string source)
        {
            if (bytes == null)
                return;

            if (string.Equals(name, "INGAME_MENU_STATE", StringComparison.OrdinalIgnoreCase))
            {
                if (bytes.Length <= 0x2B7)
                    return;

                ulong childPtr = BitConverter.ToUInt64(bytes, 0x2B0);
                bool active = childPtr != 0;
                SetPauseMenuStateActive(active,
                    source + " InGameMenu+2B0=0x" + childPtr.ToString("X"));
            }
        }

        static void SetPauseMenuStateActive(bool active, string reason)
        {
            if (pauseMenuStateActive == active)
            {
                if (active)
                    lastPauseMenuStateActiveAt = DateTime.UtcNow;
                return;
            }

            pauseMenuStateActive = active;
            if (active)
                lastPauseMenuStateActiveAt = DateTime.UtcNow;
            Log("PAUSE_MENU_ACTIVE " + (active ? "1" : "0") + " " + reason);
        }

        static bool IsInGameMenuProbeParent(string parent)
        {
            if (string.IsNullOrEmpty(parent))
                return false;

            return parent.IndexOf("TestPauseMenu_C", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("InGameMenu_C", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        static bool IsInGameMenuCooldownProbe(string label)
        {
            return string.Equals(label, "PAUSE_MENU_EVENT", StringComparison.OrdinalIgnoreCase) ||
                   string.Equals(label, "INGAME_MENU_EVENT", StringComparison.OrdinalIgnoreCase);
        }

        static bool IsInGameMenuRecentlyActive()
        {
            if (ChallengeHudTimerRecentlyMoved())
                return false;

            return pauseMenuStateActive ||
                   (lastPauseMenuStateActiveAt != DateTime.MinValue &&
                    (DateTime.UtcNow - lastPauseMenuStateActiveAt).TotalSeconds <= 0.10);
        }

        static bool ChallengeHudTimerRecentlyMoved()
        {
            return ChallengeHudTimerMovedWithin(0.10);
        }

        static bool ChallengeHudTimerMovedWithin(double seconds)
        {
            return lastHudTimeCounterChangedAt != DateTime.MinValue &&
                   (DateTime.UtcNow - lastHudTimeCounterChangedAt).TotalSeconds <= seconds;
        }

        static void UpdateStoryDifficultyFromParent(string source, IntPtr parentPtr)
        {
            IntPtr objectPtr = ReadPointerValue(parentPtr);
            if (objectPtr == IntPtr.Zero)
                return;

            string parent = DescribeUObject(objectPtr);
            int detected = DifficultyFromTextOrCurrent(parent);
            if (detected < 0)
                return;

            currentStoryDifficultyIndex = detected;
            lastDifficultyDescription = source + " parent=" + parent;
            Log("DIFFICULTY_DETECTED " + DifficultyName(detected) +
                " source=" + lastDifficultyDescription);
        }

        static void UpdateStoryGameInstanceFromParent(IntPtr parentPtr)
        {
            IntPtr objectPtr = ReadPointerValue(parentPtr);
            if (objectPtr == IntPtr.Zero)
                return;

            IntPtr outer = ReadPointerValue(IntPtr.Add(objectPtr, 0x20));
            if (outer == IntPtr.Zero)
                return;

            string description = DescribeUObject(outer);
            if (description.IndexOf("TS_GameInstance_C", StringComparison.OrdinalIgnoreCase) < 0)
                return;

            if (storyGameInstancePtr != outer)
            {
                storyGameInstancePtr = outer;
                storyGameInstanceSnapshot = null;
                Log("GAMEINSTANCE_TARGET " + description);
            }
        }

        static void PollStoryGameInstanceScan()
        {
            if (storyGameInstancePtr == IntPtr.Zero)
                return;

            const int scanSize = 0x900;
            byte[] bytes = ReadBytes(storyGameInstancePtr, scanSize);
            if (bytes == null)
                return;

            if (storyGameInstanceSnapshot == null || storyGameInstanceSnapshot.Length != bytes.Length)
            {
                storyGameInstanceSnapshot = bytes;
                Log("GAMEINSTANCE_SNAPSHOT ptr=0x" + storyGameInstancePtr.ToInt64().ToString("X") +
                    " size=0x" + scanSize.ToString("X"));
                UpdateStoryDifficultyFromGameInstance(bytes);
                return;
            }

            UpdateStoryDifficultyFromGameInstance(bytes);

            int logged = 0;
            for (int offset = 0x30; offset + 4 <= scanSize; offset += 4)
            {
                uint oldValue = BitConverter.ToUInt32(storyGameInstanceSnapshot, offset);
                uint newValue = BitConverter.ToUInt32(bytes, offset);
                if (oldValue == newValue)
                    continue;

                if (logged < 24)
                {
                    Log("GAMEINSTANCE_CHANGE off=0x" + offset.ToString("X") +
                        " u32=" + oldValue + "->" + newValue +
                        " i32=" + ((int)oldValue).ToString() + "->" + ((int)newValue).ToString() +
                        " f32=" + SafeFloat(storyGameInstanceSnapshot, offset) + "->" + SafeFloat(bytes, offset) +
                        " difficulty=" + DifficultyName(currentStoryDifficultyIndex));
                    logged++;
                }
            }

            if (logged >= 24)
                Log("GAMEINSTANCE_CHANGE_LIMIT more_changes_suppressed");

            storyGameInstanceSnapshot = bytes;
        }

        static void UpdateStoryDifficultyFromGameInstance(byte[] bytes)
        {
            const int difficultyOffset = 0x38A;
            if (bytes == null || bytes.Length <= difficultyOffset)
                return;

            int raw = bytes[difficultyOffset];
            if (raw != lastGameInstanceDifficultyByte)
            {
                string nearby = "";
                for (int i = 0; i < 8 && difficultyOffset + i < bytes.Length; ++i)
                {
                    if (i > 0)
                        nearby += " ";
                    nearby += bytes[difficultyOffset + i].ToString("X2");
                }

                Log("GAMEINSTANCE_DIFFICULTY_BYTE off=0x" + difficultyOffset.ToString("X") +
                    " raw=" + raw +
                    " bytes=" + nearby +
                    " mapped=" + DifficultyName(DifficultyFromGameInstanceByte(raw)));
                lastGameInstanceDifficultyByte = raw;
            }

            int detected = DifficultyFromGameInstanceByte(raw);
            if (detected < 0)
                return;

            currentStoryDifficultyIndex = detected;
            lastDifficultyDescription = "GAMEINSTANCE_BYTE off=0x" + difficultyOffset.ToString("X") +
                " raw=" + raw +
                " parent=" + DescribeUObject(storyGameInstancePtr);
        }

        static int DifficultyFromGameInstanceByte(int raw)
        {
            switch (raw)
            {
                case 0: return 0;
                case 1: return 1;
                case 2: return 2;
                default: return -1;
            }
        }

        static int DifficultyFromText(string text)
        {
            if (string.IsNullOrEmpty(text))
                return -1;

            if (text.IndexOf("_StoryAIHard", StringComparison.OrdinalIgnoreCase) >= 0 ||
                text.IndexOf("StoryAIHard", StringComparison.OrdinalIgnoreCase) >= 0 ||
                text.IndexOf("_StoryHard", StringComparison.OrdinalIgnoreCase) >= 0 ||
                text.IndexOf("StoryHard", StringComparison.OrdinalIgnoreCase) >= 0)
                return 2;
            if (text.IndexOf("Docks_StoryEasyOnly", StringComparison.OrdinalIgnoreCase) >= 0)
                return 0;
            if (text.IndexOf("Docks_Story", StringComparison.OrdinalIgnoreCase) >= 0)
                return 1;
            if (text.IndexOf("_StoryNormal", StringComparison.OrdinalIgnoreCase) >= 0 ||
                text.IndexOf("StoryNormal", StringComparison.OrdinalIgnoreCase) >= 0)
                return 1;
            if (text.IndexOf("StoryAI", StringComparison.OrdinalIgnoreCase) >= 0)
                return -1;
            if (text.IndexOf("_Story", StringComparison.OrdinalIgnoreCase) >= 0)
                return 0;

            return -1;
        }

        static int DifficultyFromTextOrCurrent(string text)
        {
            int detected = DifficultyFromText(text);
            if (detected >= 0)
                return detected;

            if (currentStoryDifficultyIndex >= 0 && IsPlainStoryMapDescriptor(text))
                return currentStoryDifficultyIndex;

            return -1;
        }

        static bool IsPlainStoryMapDescriptor(string text)
        {
            if (string.IsNullOrEmpty(text))
                return false;

            return text.IndexOf("/Game/Maps/", StringComparison.OrdinalIgnoreCase) >= 0 &&
                   text.IndexOf("/Game/Maps/Menu", StringComparison.OrdinalIgnoreCase) < 0 &&
                   text.IndexOf("Challenge", StringComparison.OrdinalIgnoreCase) < 0;
        }

        static string DifficultyName(int index)
        {
            switch (index)
            {
                case 0: return "Easy";
                case 1: return "Normal";
                case 2: return "Hard";
                default: return "Unknown";
            }
        }

        static bool ShouldLogAwardAppearProbe(string label, string parent)
        {
            if (string.IsNullOrEmpty(parent))
                return false;

            bool awardRelated =
                parent.IndexOf("AwardWidget_C", StringComparison.OrdinalIgnoreCase) >= 0 ||
                parent.IndexOf("MissionEnded_C", StringComparison.OrdinalIgnoreCase) >= 0 ||
                parent.IndexOf("TS_HudPointCounter_C", StringComparison.OrdinalIgnoreCase) >= 0 ||
                parent.IndexOf("TS_ChallengeGameMode_C", StringComparison.OrdinalIgnoreCase) >= 0 ||
                parent.IndexOf("TS_ChallengeMayhem_C", StringComparison.OrdinalIgnoreCase) >= 0 ||
                parent.IndexOf("ChallengeHUD_C", StringComparison.OrdinalIgnoreCase) >= 0;

            if (!awardRelated)
                return false;

            if (label.StartsWith("AWARD_", StringComparison.Ordinal))
                return true;

            return parent.IndexOf("VisibilityBinding", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("TextBinding", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("ColorBinding", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("AwardWidget_C", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("TS_ChallengeGameMode_C", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("TS_ChallengeMayhem_C", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   parent.IndexOf("ChallengeHUD_C", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        static void ResetDeepScanSnapshots()
        {
            if (deepScanTargets == null)
                return;

            for (int i = 0; i < deepScanTargets.Length; ++i)
            {
                deepScanTargets[i].ObjectPtr = IntPtr.Zero;
                deepScanTargets[i].Snapshot = null;
            }
        }

        static void PollDeepStateScan()
        {
            if (deepScanTargets == null)
                return;

            DateTime now = DateTime.UtcNow;
            bool startWindow =
                playerMovementProbeStartedAt != DateTime.MinValue &&
                (now - playerMovementProbeStartedAt).TotalSeconds <= 8.0;
            if (!challengeProbeActive && !startWindow)
                return;

            double elapsed = challengeProbeActive
                ? (now - challengeProbeStartedAt).TotalSeconds
                : (now - playerMovementProbeStartedAt).TotalSeconds;
            for (int i = 0; i < deepScanTargets.Length; ++i)
                PollDeepStateTarget(deepScanTargets[i], elapsed);
        }

        static void PollDeepStateTarget(DeepScanTarget target, double elapsed)
        {
            if (target == null || target.ParentPtr == IntPtr.Zero)
                return;

            IntPtr objectPtr = ReadPointerValue(target.ParentPtr);
            if (objectPtr == IntPtr.Zero)
                return;

            const int scanSize = 0x600;
            byte[] bytes = ReadBytes(objectPtr, scanSize);
            if (bytes == null)
                return;

            if (target.ObjectPtr != objectPtr || target.Snapshot == null)
            {
                target.ObjectPtr = objectPtr;
                target.Snapshot = bytes;
                Log("DEEP_SCAN_TARGET " + target.Name +
                    " t=" + elapsed.ToString("0.000") +
                    " ptr=0x" + objectPtr.ToInt64().ToString("X") +
                    " " + DescribeUObject(objectPtr));
                return;
            }

            if (elapsed < 24.0 && !target.LogBeforeFinish)
            {
                target.Snapshot = bytes;
                return;
            }

            int logged = 0;
            int logLimit = target.LogLimit > 0 ? target.LogLimit : 12;
            for (int offset = 0x30; offset + 4 <= scanSize; offset += 4)
            {
                uint oldValue = BitConverter.ToUInt32(target.Snapshot, offset);
                uint newValue = BitConverter.ToUInt32(bytes, offset);
                if (oldValue == newValue)
                    continue;

                if (logged < logLimit)
                {
                    Log("DEEP_CHANGE " + target.Name +
                        " t=" + elapsed.ToString("0.000") +
                        " off=0x" + offset.ToString("X") +
                        " u32=" + oldValue + "->" + newValue +
                        " i32=" + ((int)oldValue).ToString() + "->" + ((int)newValue).ToString() +
                        " f32=" + SafeFloat(target.Snapshot, offset) + "->" + SafeFloat(bytes, offset));
                    logged++;
                }
            }

            if (logged >= logLimit)
            {
                Log("DEEP_CHANGE_LIMIT " + target.Name +
                    " t=" + elapsed.ToString("0.000") +
                    " more_changes_suppressed");
            }

            target.Snapshot = bytes;
        }

        static bool PollGameModeFinishOffset()
        {
            if (!challengeProbeActive)
                return false;

            IntPtr gameModePtr = ReadPointerValue(challengeGameModeParentPtr);
            if (gameModePtr == IntPtr.Zero)
                return false;

            uint value = ReadUInt32(IntPtr.Add(gameModePtr, 0x430));
            if (!gameModeFinishValueReady)
            {
                lastGameModeFinishValue = value;
                gameModeFinishValueReady = true;
                lastGameModeFinishValueChangedAt = DateTime.UtcNow;
                return false;
            }

            double elapsed = (DateTime.UtcNow - challengeProbeStartedAt).TotalSeconds;
            if (value == lastGameModeFinishValue)
            {
                if (lastGameModeFinishValueChangedAt != DateTime.MinValue &&
                    elapsed >= 23.0 &&
                    (DateTime.UtcNow - lastGameModeFinishValueChangedAt).TotalSeconds >= 1.12 &&
                    !gameModeFinishStallLogged)
                {
                    gameModeFinishStallLogged = true;
                    Log("CHALLENGE_GAMEMODE_430_STALLED t=" + elapsed.ToString("0.000") +
                        " since_change=" + (DateTime.UtcNow - lastGameModeFinishValueChangedAt).TotalSeconds.ToString("0.000") +
                        " f32=" + UIntToFloatText(value) +
                        " world=" + worldName);
                }
                return false;
            }

            uint previous = lastGameModeFinishValue;
            lastGameModeFinishValue = value;
            lastGameModeFinishValueChangedAt = DateTime.UtcNow;
            gameModeFinishStallLogged = false;
            Log("CHALLENGE_GAMEMODE_430 t=" + elapsed.ToString("0.000") +
                " u32=" + previous + "->" + value +
                " i32=" + ((int)previous).ToString() + "->" + ((int)value).ToString() +
                " f32=" + UIntToFloatText(previous) + "->" + UIntToFloatText(value));
            return false;
        }

        static bool PollHudTimeCounterStop()
        {
            if (!challengeProbeActive)
                return false;

            IntPtr hudTimePtr = ReadPointerValue(hudTimeCounterParentPtr);
            if (hudTimePtr == IntPtr.Zero)
                return false;

            uint value = ReadUInt32(IntPtr.Add(hudTimePtr, 0x240));
            if (!hudTimeCounterReady)
            {
                lastHudTimeCounterValue = value;
                hudTimeCounterReady = true;
                lastHudTimeCounterChangedAt = DateTime.UtcNow;
                return false;
            }

            double elapsed = (DateTime.UtcNow - challengeProbeStartedAt).TotalSeconds;
            if (value == lastHudTimeCounterValue)
            {
                double sinceChange = (DateTime.UtcNow - lastHudTimeCounterChangedAt).TotalSeconds;
                if (lastHudTimeCounterChangedAt != DateTime.MinValue &&
                    elapsed >= 3.0 &&
                    sinceChange >= 1.12 &&
                    !hudTimeCounterStallLogged)
                {
                    if (IsInGameMenuRecentlyActive())
                    {
                        Log("CHALLENGE_SPLIT_PENDING_INGAME_MENU HUD_TIMER_240_STALLED t=" +
                            elapsed.ToString("0.000") +
                            " since_change=" + sinceChange.ToString("0.000") +
                            " f32=" + UIntToFloatText(value) +
                            " world=" + worldName);
                        return false;
                    }

                    hudTimeCounterStallLogged = true;
                    Log("CHALLENGE_HUD_TIMER_240_STALLED t=" + elapsed.ToString("0.000") +
                        " since_change=" + sinceChange.ToString("0.000") +
                        " f32=" + UIntToFloatText(value) +
                        " world=" + worldName);
                    return false;
                }
                return false;
            }

            uint previous = lastHudTimeCounterValue;
            lastHudTimeCounterValue = value;
            lastHudTimeCounterChangedAt = DateTime.UtcNow;
            hudTimeCounterStallLogged = false;
            if (pauseMenuStateActive)
                SetPauseMenuStateActive(false, "hud_timer_resumed");
            Log("CHALLENGE_HUD_TIMER_240 t=" + elapsed.ToString("0.000") +
                " u32=" + previous + "->" + value +
                " i32=" + ((int)previous).ToString() + "->" + ((int)value).ToString() +
                " f32=" + UIntToFloatText(previous) + "->" + UIntToFloatText(value));
            return false;
        }

        static bool PollGameStateStop()
        {
            if (!challengeProbeActive)
                return false;

            IntPtr gameStatePtr = ReadPointerValue(gameStateParentPtr);
            if (gameStatePtr == IntPtr.Zero)
                return false;

            uint value300 = ReadUInt32(IntPtr.Add(gameStatePtr, 0x300));
            uint value318 = ReadUInt32(IntPtr.Add(gameStatePtr, 0x318));
            if (!gameStateStopReady)
            {
                lastGameState300Value = value300;
                lastGameState318Value = value318;
                gameStateStopReady = true;
                return false;
            }

            double elapsed = (DateTime.UtcNow - challengeProbeStartedAt).TotalSeconds;
            bool stopFlip =
                lastGameState318Value == 0 &&
                value318 == 256 &&
                value300 == lastGameState300Value + 1;

            if (stopFlip)
            {
                challengeSplitIgnoresPause = ChallengeHudTimerMovedWithin(1.15);
                Log("CHALLENGE_SPLIT GAMESTATE_STOP t=" + elapsed.ToString("0.000") +
                    " 0x300=" + lastGameState300Value + "->" + value300 +
                    " 0x318=" + lastGameState318Value + "->" + value318 +
                    " ignore_pause=" + (challengeSplitIgnoresPause ? "1" : "0") +
                    " world=" + worldName);
                challengeProbeActive = false;
                return true;
            }

            if (value300 != lastGameState300Value || value318 != lastGameState318Value)
            {
                Log("CHALLENGE_GAMESTATE_STOP_PROBE t=" + elapsed.ToString("0.000") +
                    " 0x300=" + lastGameState300Value + "->" + value300 +
                    " 0x318=" + lastGameState318Value + "->" + value318 +
                    " world=" + worldName);
                lastGameState300Value = value300;
                lastGameState318Value = value318;
            }

            return false;
        }

        static bool PollGameModeFinishState()
        {
            if (!challengeProbeActive)
                return false;

            IntPtr gameModePtr = ReadPointerValue(challengeGameModeParentPtr);
            if (gameModePtr == IntPtr.Zero)
                return false;

            uint state = ReadUInt32(IntPtr.Add(gameModePtr, 0x2B8));
            uint phase = ReadUInt32(IntPtr.Add(gameModePtr, 0x2BC));
            double elapsed = (DateTime.UtcNow - challengeProbeStartedAt).TotalSeconds;

            if (state == 0 || phase != 4)
            {
                gameModeStateFinishArmed = true;
                return false;
            }

            if (!gameModeStateFinishArmed)
                return false;

            Log("CHALLENGE_SPLIT GAMEMODE_STATE t=" + elapsed.ToString("0.000") +
                " state=0x2B8:" + state +
                " phase=0x2BC:" + phase +
                " world=" + worldName);
            challengeProbeActive = false;
            gameModeStateFinishArmed = false;
            return true;
        }

        static void PollGraphicsChange()
        {
            if (!challengeProbeActive || process == null || process.HasExited)
                return;

            DateTime now = DateTime.UtcNow;
            if (lastGraphicsSampleAt != DateTime.MinValue &&
                (now - lastGraphicsSampleAt).TotalMilliseconds < 80.0)
                return;

            lastGraphicsSampleAt = now;
            byte[] sample = CaptureWindowSample(process.MainWindowHandle);
            if (sample == null)
                return;

            double elapsed = (now - challengeProbeStartedAt).TotalSeconds;
            if (!graphicsSampleReady || lastGraphicsSample == null || lastGraphicsSample.Length != sample.Length)
            {
                lastGraphicsSample = sample;
                graphicsSampleReady = true;
                Log("GRAPHICS_SAMPLE armed t=" + elapsed.ToString("0.000") +
                    " bytes=" + sample.Length);
                return;
            }

            long total = 0;
            int max = 0;
            int changed = 0;
            for (int i = 0; i < sample.Length; ++i)
            {
                int diff = Math.Abs(sample[i] - lastGraphicsSample[i]);
                total += diff;
                if (diff > max)
                    max = diff;
                if (diff >= 18)
                    changed++;
            }

            double mean = (double)total / sample.Length;
            double changedPct = (double)changed * 100.0 / sample.Length;
            bool finishWindow = elapsed >= 24.0 && elapsed <= 32.0;
            if (finishWindow || mean >= 1.0 || changedPct >= 2.0 || max >= 35)
            {
                graphicsFrameLogCount++;
                Log((finishWindow ? "GRAPHICS_FRAME" : "GRAPHICS_CHANGE") +
                    " t=" + elapsed.ToString("0.000") +
                    " n=" + graphicsFrameLogCount +
                    " mean=" + mean.ToString("0.00") +
                    " max=" + max +
                    " changed=" + changedPct.ToString("0.0") + "%");
            }

            lastGraphicsSample = sample;
        }

        static byte[] CaptureWindowSample(IntPtr hwnd)
        {
            if (hwnd == IntPtr.Zero)
                return null;

            RECT rect;
            if (!GetWindowRect(hwnd, out rect))
                return null;

            int sourceW = rect.Right - rect.Left;
            int sourceH = rect.Bottom - rect.Top;
            if (sourceW <= 0 || sourceH <= 0)
                return null;

            const int sampleW = 64;
            const int sampleH = 36;
            const int SRCCOPY = 0x00CC0020;
            const int COLORONCOLOR = 3;
            const uint BI_RGB = 0;
            const uint DIB_RGB_COLORS = 0;

            IntPtr sourceDc = GetDC(IntPtr.Zero);
            if (sourceDc == IntPtr.Zero)
                return null;

            IntPtr memoryDc = IntPtr.Zero;
            IntPtr bitmap = IntPtr.Zero;
            IntPtr oldObject = IntPtr.Zero;
            try
            {
                memoryDc = CreateCompatibleDC(sourceDc);
                if (memoryDc == IntPtr.Zero)
                    return null;

                bitmap = CreateCompatibleBitmap(sourceDc, sampleW, sampleH);
                if (bitmap == IntPtr.Zero)
                    return null;

                oldObject = SelectObject(memoryDc, bitmap);
                SetStretchBltMode(memoryDc, COLORONCOLOR);
                if (!StretchBlt(memoryDc, 0, 0, sampleW, sampleH, sourceDc, rect.Left, rect.Top, sourceW, sourceH, SRCCOPY))
                    return null;

                byte[] bgra = new byte[sampleW * sampleH * 4];
                BITMAPINFOHEADER header = new BITMAPINFOHEADER();
                header.biSize = (uint)Marshal.SizeOf(typeof(BITMAPINFOHEADER));
                header.biWidth = sampleW;
                header.biHeight = -sampleH;
                header.biPlanes = 1;
                header.biBitCount = 32;
                header.biCompression = BI_RGB;
                header.biSizeImage = (uint)bgra.Length;

                int got = GetDIBits(memoryDc, bitmap, 0, sampleH, bgra, ref header, DIB_RGB_COLORS);
                if (got == 0)
                    return null;

                byte[] gray = new byte[sampleW * sampleH];
                for (int i = 0, p = 0; i < gray.Length; ++i, p += 4)
                {
                    int b = bgra[p];
                    int g = bgra[p + 1];
                    int r = bgra[p + 2];
                    gray[i] = (byte)((r * 30 + g * 59 + b * 11) / 100);
                }

                return gray;
            }
            finally
            {
                if (oldObject != IntPtr.Zero && memoryDc != IntPtr.Zero)
                    SelectObject(memoryDc, oldObject);
                if (bitmap != IntPtr.Zero)
                    DeleteObject(bitmap);
                if (memoryDc != IntPtr.Zero)
                    DeleteDC(memoryDc);
                ReleaseDC(IntPtr.Zero, sourceDc);
            }
        }

        static IntPtr ReadPointerValue(IntPtr ptr)
        {
            if (process == null || process.HasExited || ptr == IntPtr.Zero)
                return IntPtr.Zero;

            byte[] buffer = new byte[IntPtr.Size];
            IntPtr read;
            if (!ReadProcessMemory(process.Handle, ptr, buffer, buffer.Length, out read) || read.ToInt64() != buffer.Length)
                return IntPtr.Zero;

            if (IntPtr.Size == 8)
                return new IntPtr(BitConverter.ToInt64(buffer, 0));

            return new IntPtr(BitConverter.ToInt32(buffer, 0));
        }

        static byte[] ReadBytes(IntPtr ptr, int count)
        {
            if (process == null || process.HasExited || ptr == IntPtr.Zero || count <= 0)
                return null;

            byte[] buffer = new byte[count];
            IntPtr read;
            if (!ReadProcessMemory(process.Handle, ptr, buffer, buffer.Length, out read) || read.ToInt64() != buffer.Length)
                return null;

            return buffer;
        }

        static string SafeFloat(byte[] bytes, int offset)
        {
            try
            {
                float value = BitConverter.ToSingle(bytes, offset);
                if (float.IsNaN(value) || float.IsInfinity(value) || Math.Abs(value) > 1000000.0f)
                    return "-";

                return value.ToString("0.###");
            }
            catch
            {
                return "-";
            }
        }

        static string UIntToFloatText(uint value)
        {
            try
            {
                byte[] bytes = BitConverter.GetBytes(value);
                float floatValue = BitConverter.ToSingle(bytes, 0);
                if (float.IsNaN(floatValue) || float.IsInfinity(floatValue) || Math.Abs(floatValue) > 1000000.0f)
                    return "-";

                return floatValue.ToString("0.###");
            }
            catch
            {
                return "-";
            }
        }

        static string DescribeUObject(IntPtr objectPtr)
        {
            if (objectPtr == IntPtr.Zero)
                return "0x0";

            try
            {
                string objectName = ReadUObjectName(objectPtr);
                IntPtr classPtr = ReadPointerValue(IntPtr.Add(objectPtr, 0x10));
                string className = ReadUObjectName(classPtr);
                string outer = DescribeOuterChain(objectPtr);
                return "0x" + objectPtr.ToInt64().ToString("X") + " class=" + className + " name=" + objectName + outer;
            }
            catch
            {
                return "0x" + objectPtr.ToInt64().ToString("X");
            }
        }

        static string DescribeOuterChain(IntPtr objectPtr)
        {
            string text = "";
            IntPtr current = ReadPointerValue(IntPtr.Add(objectPtr, 0x20));
            for (int depth = 0; depth < 4 && current != IntPtr.Zero; ++depth)
            {
                string objectName = ReadUObjectName(current);
                IntPtr classPtr = ReadPointerValue(IntPtr.Add(current, 0x10));
                string className = ReadUObjectName(classPtr);
                if (string.IsNullOrEmpty(objectName) && string.IsNullOrEmpty(className))
                    break;

                text += " outer" + depth + "=0x" + current.ToInt64().ToString("X") + ":" + className + "/" + objectName;
                current = ReadPointerValue(IntPtr.Add(current, 0x20));
            }

            return text;
        }

        static string ReadUObjectName(IntPtr objectPtr)
        {
            if (objectPtr == IntPtr.Zero)
                return "";

            byte[] buffer = new byte[4];
            IntPtr read;
            IntPtr namePtr = IntPtr.Add(objectPtr, 0x18);
            if (!ReadProcessMemory(process.Handle, namePtr, buffer, buffer.Length, out read) || read.ToInt64() != buffer.Length)
                return "";

            uint fName = BitConverter.ToUInt32(buffer, 0);
            return Convert.ToString(utils.FNameToString(fName));
        }

        static void ResetLog()
        {
            if (!EnableEventLog)
                return;

            if (logInitialized)
                return;

            logInitialized = true;
            try
            {
                if (!string.IsNullOrEmpty(logPath))
                    File.WriteAllText(logPath, "TSR-Timer event log started " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + Environment.NewLine);
            }
            catch
            {
            }
        }

        static void Log(string message)
        {
            if (!EnableEventLog)
                return;

            try
            {
                if (string.IsNullOrEmpty(logPath))
                    return;

                File.AppendAllText(logPath, DateTime.Now.ToString("HH:mm:ss.fff") + " " + message + Environment.NewLine);
            }
            catch
            {
            }
        }

        static bool IsStartWorld(string name)
        {
            return !IsMenuWorld(name) &&
                   name != "SPobby" &&
                   name != "Entry" &&
                   name != "Startup" &&
                   name != "Introduction";
        }

        static bool IsMenuWorld(string name)
        {
            if (string.IsNullOrEmpty(name))
                return false;

            return name.IndexOf("StoryLevelSelect", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("StoryLevelButton", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("Story_Category_Selection_Button", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("MainStoryMenu", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("MainMenu", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("MainChild", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("Menu_Background", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("World:Startup", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("World:Menu_Background", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("/Game/Maps/Menus/", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("TestSPLobby", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("SPLobbyGmode", StringComparison.OrdinalIgnoreCase) >= 0;
        }
    }
}
