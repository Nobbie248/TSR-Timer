#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <metahost.h>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "mscoree.lib")

enum AutoSplitFlags
{
    AUTO_READY = 1 << 0,
    AUTO_STARTED = 1 << 1,
    AUTO_SPLIT = 1 << 2,
    AUTO_ATTACHED = 1 << 3,
    AUTO_LOADING = 1 << 4,
    AUTO_START_CANCELED = 1 << 5,
    AUTO_CHALLENGE_STARTED = 1 << 6,
    AUTO_CHALLENGE_SPLIT = 1 << 7,
    AUTO_ERROR = 1 << 30
};

static LARGE_INTEGER gFreq;
static double gRunTimerSeconds = 0.0;
static std::vector<double> gFinishedRunTimes;
static std::vector<double> gFinishedRunBestDiffs;
static std::vector<bool> gFinishedRunBestDiffKnown;
static std::string gCurrentGameName;
static bool gRunTimerLineActive = false;
static bool gSingleTimerBestDiffKnown = false;
static double gSingleTimerBestDiff = 0.0;
static bool gFullStoryBestDiffKnown = false;
static double gFullStoryBestDiff = 0.0;
static bool gResetTimerRequested = false;
static bool gForceClearResetRequested = false;
static bool gFullStoryMenuAfterFinish = false;
static bool gFullChallengePendingSplit = false;
static double gFullChallengePendingSeconds = 0.0;
static bool gFullChallengeReadyAfterLoading = false;
static bool gFullChallengeWaitingForReadyLoading = false;
static int gOverlayScalePercent = 100;
static bool gOverlayScaleChanged = false;

static RECT gResetButtonRect = { 0, 0, 0, 0 };
static RECT gScaleDownButtonRect = { 0, 0, 0, 0 };
static RECT gScaleUpButtonRect = { 0, 0, 0, 0 };
static RECT gCloseButtonRect = { 0, 0, 0, 0 };
static RECT gBackButtonRect = { 0, 0, 0, 0 };
static RECT gModeButtonRects[5] = {};
static RECT gModeCloseButtonRect = { 0, 0, 0, 0 };
static RECT gBestSplitButtonRects[3] = {};

static const int kOverlayTimerOnlyW = 236;
static const int kOverlayTimerOnlyH = 302;
static const int kFullChallengeTimerH = 876;
static const int kBestTimesH = 780;
static const int kModeSelectorW = kOverlayTimerOnlyW;
static const int kModeSelectorH = 248;
static const char* kDifficultyNames[3] = { "Easy", "Normal", "Hard" };
static const char* kRouteNames[9] = {
    "Tomb",
    "Chinese",
    "Cyberden",
    "Village",
    "Chemical Plant",
    "Planet X",
    "Mansion",
    "Docks",
    "Spaceways"
};
static const char* kChallengeRouteNames[32] = {
    "Barrel Blast",
    "Barrel Roll",
    "Behead The Undead",
    "Putrid Punchout",
    "Dusk of the Dead",
    "FOTLD",
    "A Grave Mistake",
    "Sergio's Last Stand",
    "Day of the Damned",
    "Heist",
    "Shipyard Takedown",
    "Space Boarders",
    "Banana Republic",
    "Gibbon No Chances",
    "Claim to Flame",
    "SISGB",
    "Don't Wait Around",
    "Avec Le Brique",
    "Pane in the Neck",
    "Bricking it",
    "One In a Melon",
    "Dessert Storm",
    "Pier Pressure",
    "Simian Shootout",
    "Dam Bursters",
    "Pick Yer Piece",
    "NGO",
    "Card-io Exercise",
    "Silent but Deadly",
    "Silent Night",
    "Cortez Can't Jump!",
    "TSUG"
};
static const int kModeCount = 4;
static const bool kBestTimesEnabled = true;
static const char* kModeNames[4] = {
    "Single story level run",
    "Full TS story run",
    "Challenge run",
    "Best times"
};

enum class AppScreen
{
    ModeSelector,
    SingleLevelTimer,
    FullStoryTimer,
    FullChallengeTimer,
    BestTimes,
    BestStorySplits
};

static AppScreen gAppScreen = AppScreen::ModeSelector;
static bool gScreenChanged = false;
static char gModeStatusText[96] = "";
static bool gChallengeModeActive = false;
static double gChallengeEnteredAt = -1.0;
static int gStoryDifficultyIndex = -1;
static int gBestSplitsDifficultyIndex = 0;
static char gExeDir[MAX_PATH] = {};

struct BestTimeRecord
{
    std::string mode;
    std::string name;
    double seconds = 0.0;
};

static std::vector<BestTimeRecord> gBestTimes;

static double GetTimeSeconds()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)gFreq.QuadPart;
}

static int ScaleUi(int value)
{
    return MulDiv(value, gOverlayScalePercent, 100);
}

static int CurrentScreenWidth()
{
    return ScaleUi(kOverlayTimerOnlyW);
}

static int CurrentScreenHeight()
{
    if (gAppScreen == AppScreen::SingleLevelTimer)
        return ScaleUi(100);
    if (gAppScreen == AppScreen::ModeSelector)
        return ScaleUi(kModeSelectorH);
    if (gAppScreen == AppScreen::FullChallengeTimer)
        return ScaleUi(kFullChallengeTimerH);
    if (gAppScreen == AppScreen::BestTimes || gAppScreen == AppScreen::BestStorySplits)
        return ScaleUi(kBestTimesH);

    return ScaleUi(kOverlayTimerOnlyH);
}

static void SetOverlayScale(int scalePercent)
{
    if (scalePercent < 75) scalePercent = 75;
    if (scalePercent > 200) scalePercent = 200;
    if (scalePercent == gOverlayScalePercent) return;

    gOverlayScalePercent = scalePercent;
    gOverlayScaleChanged = true;
}

static bool PointInRect(const RECT& r, int x, int y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

static std::string AppPath(const char* name)
{
    std::string path = gExeDir;
    if (!path.empty() && path.back() != '\\' && path.back() != '/')
        path += "\\";
    path += name;
    return path;
}

static void InitExeDir()
{
    DWORD len = GetModuleFileNameA(nullptr, gExeDir, _countof(gExeDir));
    if (len == 0 || len >= _countof(gExeDir))
    {
        gExeDir[0] = '\0';
        return;
    }

    for (DWORD i = len; i > 0; --i)
    {
        if (gExeDir[i - 1] == '\\' || gExeDir[i - 1] == '/')
        {
            gExeDir[i - 1] = '\0';
            return;
        }
    }
}

static std::string TrimName(std::string text)
{
    size_t first = text.find_first_not_of(" \t\r\n");
    size_t last = text.find_last_not_of(" \t\r\n");
    if (first == std::string::npos || last == std::string::npos)
        return "";
    text = text.substr(first, last - first + 1);

    size_t slash = text.find_last_of("/\\");
    if (slash != std::string::npos && slash + 1 < text.size())
        text = text.substr(slash + 1);

    const char* suffixes[] = { "_Story", "_Challenge", "_C" };
    for (const char* suffix : suffixes)
    {
        size_t suffixLen = std::strlen(suffix);
        if (text.size() >= suffixLen &&
            _stricmp(text.c_str() + text.size() - suffixLen, suffix) == 0)
        {
            text.resize(text.size() - suffixLen);
        }
    }

    for (char& ch : text)
    {
        if (ch == '_' || ch == '-')
            ch = ' ';
    }

    return text.empty() ? "Unknown" : text;
}

static std::string SplitCamelCase(std::string text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        char ch = text[i];
        if (i > 0 &&
            ch >= 'A' && ch <= 'Z' &&
            ((text[i - 1] >= 'a' && text[i - 1] <= 'z') ||
             (i + 1 < text.size() && text[i - 1] >= 'A' && text[i - 1] <= 'Z' && text[i + 1] >= 'a' && text[i + 1] <= 'z')))
        {
            out += ' ';
        }
        out += ch;
    }
    return out;
}

static std::string CleanChallengeName(std::string text)
{
    text = TrimName(text);
    const char* prefixes[] = {
        "TS Challenge",
        "TS_Challenge",
        "Challenge"
    };

    for (const char* prefix : prefixes)
    {
        size_t prefixLen = std::strlen(prefix);
        if (text.size() >= prefixLen && _strnicmp(text.c_str(), prefix, prefixLen) == 0)
        {
            text = text.substr(prefixLen);
            break;
        }
    }

    text = TrimName(SplitCamelCase(text));
    return text.empty() || text == "Unknown" ? "" : text;
}

static bool IsChallengeModeName(const std::string& name)
{
    return name == "Monkey Madness" ||
           name == "Challenge Game Mode" ||
           name == "Challenge Mayhem" ||
           name.find("Game Mode") != std::string::npos ||
           name.find("Mayhem") != std::string::npos;
}

static std::string ChallengeNameFromHandlerName(const std::string& handlerName)
{
    static const std::map<std::string, std::string> kKnownHandlers = {
        { "TS_TestBarrelChallenge1", "Barrel Blast" },
        { "TS_TestBarrelChallenge2", "Barrel Roll" }
    };

    auto it = kKnownHandlers.find(handlerName);
    if (it != kKnownHandlers.end())
        return it->second;

    return "";
}

static std::string ValueAfterKey(const std::string& line, const char* key)
{
    size_t keyLen = std::strlen(key);
    if (line.compare(0, keyLen, key) == 0)
        return line.substr(keyLen);
    return "";
}

static std::string HandlerNameFromDescriptor(const std::string& text)
{
    if (text.find("class=TS_ChallengeHandler_C") == std::string::npos)
        return "";

    size_t pos = text.find("name=");
    if (pos == std::string::npos)
        return "";

    std::string value = text.substr(pos + 5);
    size_t end = value.find_first_of(" \t\r\n");
    if (end != std::string::npos)
        value.resize(end);

    return TrimName(value);
}

static std::string StoryNameFromDescriptor(const std::string& text)
{
    static const std::map<std::string, std::string> kStoryNames = {
        { "Tomb", "Tomb" },
        { "Chinese", "Chinese" },
        { "Cyberden", "Cyberden" },
        { "Village", "Village" },
        { "Chemical", "Chemical Plant" },
        { "ChemicalPlant", "Chemical Plant" },
        { "Planet", "Planet X" },
        { "PlanetX", "Planet X" },
        { "Mansion", "Mansion" },
        { "Docks", "Docks" },
        { "Spaceways", "Spaceways" }
    };

    for (const auto& entry : kStoryNames)
    {
        if (text.find(entry.first) != std::string::npos)
            return entry.second;
    }

    return "";
}

static std::string NameFromDescriptor(std::string text)
{
    if (text.empty() || text == "0x0")
        return "";

    const char* markers[] = { "name=", "Package//Game/", "World/" };
    for (const char* marker : markers)
    {
        size_t pos = text.rfind(marker);
        if (pos == std::string::npos)
            continue;

        std::string value = text.substr(pos + std::strlen(marker));
        size_t end = value.find_first_of(" \t\r\n");
        if (end != std::string::npos)
            value.resize(end);
        size_t slash = value.find_last_of("/\\");
        if (slash != std::string::npos && slash + 1 < value.size())
            value = value.substr(slash + 1);

        value = TrimName(value);
        if (std::strcmp(marker, "name=") == 0)
            value = CleanChallengeName(value);

        if (!value.empty() && value != "Unknown" &&
            !IsChallengeModeName(value) &&
            value.find("ChallengeGameMode") == std::string::npos &&
            value.find("ChallengeMayhem") == std::string::npos &&
            value.find("TS Challenge") == std::string::npos &&
            value.find("TS_Challenge") == std::string::npos)
            return value;
    }

    return "";
}

static void UpdateCurrentGameName(bool challengeMode)
{
    std::ifstream in(AppPath("TSR-Timer-state.txt"));
    if (!in)
        return;

    std::string worldName;
    std::string storyStart;
    std::string storySplit;
    std::string challengeGameMode;
    std::string challengeMayhem;
    std::vector<std::string> challengeCandidates;
    std::string line;
    while (std::getline(in, line))
    {
        std::string value = ValueAfterKey(line, "world=");
        if (!value.empty())
        {
            worldName = value;
            continue;
        }

        value = ValueAfterKey(line, "challengeGameMode=");
        if (!value.empty())
        {
            challengeGameMode = value;
            continue;
        }

        value = ValueAfterKey(line, "storyStart=");
        if (!value.empty())
        {
            storyStart = value;
            continue;
        }

        value = ValueAfterKey(line, "storySplit=");
        if (!value.empty())
        {
            storySplit = value;
            continue;
        }

        value = ValueAfterKey(line, "challengeMayhem=");
        if (!value.empty())
        {
            challengeMayhem = value;
            continue;
        }

        value = ValueAfterKey(line, "challengeCandidate=");
        if (!value.empty())
            challengeCandidates.push_back(value);
    }

    std::string name;
    if (challengeMode)
    {
        for (const std::string& candidate : challengeCandidates)
        {
            std::string handlerName = HandlerNameFromDescriptor(candidate);
            name = ChallengeNameFromHandlerName(handlerName);
            if (!name.empty())
                break;
        }

        for (const std::string& candidate : challengeCandidates)
        {
            if (!name.empty())
                break;

            if (!HandlerNameFromDescriptor(candidate).empty())
                continue;

            name = NameFromDescriptor(candidate);
            if (!name.empty())
                break;
        }
    }
    if (name.empty() && !challengeMode)
        name = StoryNameFromDescriptor(storySplit);
    if (name.empty() && !challengeMode)
        name = StoryNameFromDescriptor(storyStart);
    if (name.empty() && !challengeMode)
        name = TrimName(worldName);
    if (name.empty() && challengeMode)
        name = "Unknown Challenge";
    if (!name.empty())
        gCurrentGameName = name;
}

static const char* CurrentModeName()
{
    if (gChallengeModeActive)
        return (gAppScreen == AppScreen::FullChallengeTimer) ? "Full challenge run" : "Challenge run";
    if (gAppScreen == AppScreen::FullStoryTimer)
        return "Full TS story run";
    return "Single story level run";
}

static std::string BestTimeFilePath()
{
    return AppPath("TSR-Timer-best-times.txt");
}

static std::string OldBestTimeFilePath()
{
    return AppPath("TSR-Timer-bests.txt");
}

static const char* StoryDifficultyName()
{
    if (gStoryDifficultyIndex < 0 || gStoryDifficultyIndex >= 3)
        return "Unknown";
    return kDifficultyNames[gStoryDifficultyIndex];
}

static const char* StoryDifficultyNameForIndex(int difficultyIndex)
{
    if (difficultyIndex < 0 || difficultyIndex >= 3)
        return "Unknown";
    return kDifficultyNames[difficultyIndex];
}

static std::string StoryModeNameForDifficulty(const char* baseMode, int difficultyIndex)
{
    if (difficultyIndex < 0 || difficultyIndex >= 3)
        return "";
    return std::string(baseMode) + " " + StoryDifficultyNameForIndex(difficultyIndex);
}

static std::string FullStorySplitName(int index)
{
    if (index < 0 || index >= 9)
        return "";
    char name[64];
    std::snprintf(name, sizeof(name), "Split %d %s", index + 1, kRouteNames[index]);
    return name;
}

static std::string StoryModeName(const char* baseMode)
{
    return StoryModeNameForDifficulty(baseMode, gStoryDifficultyIndex);
}

static void LoadBestTimes()
{
    gBestTimes.clear();
    if (!kBestTimesEnabled)
        return;

    std::ifstream in(BestTimeFilePath());
    if (!in)
    {
        std::ifstream oldIn(OldBestTimeFilePath());
        if (oldIn)
        {
            std::ofstream migrated(BestTimeFilePath(), std::ios::trunc);
            migrated << oldIn.rdbuf();
            oldIn.close();
            migrated.close();
            DeleteFileA(OldBestTimeFilePath().c_str());
            in.open(BestTimeFilePath());
        }
    }
    if (!in)
        return;

    std::string line;
    while (std::getline(in, line))
    {
        std::stringstream ss(line);
        std::string mode;
        std::string name;
        std::string secondsText;
        if (!std::getline(ss, mode, '|') ||
            !std::getline(ss, name, '|') ||
            !std::getline(ss, secondsText))
            continue;

        if (mode == "Challenge timer" || mode == "Challenge run")
            continue;
        if (mode == "Full TS story run" && name != "Total" && name.rfind("Split ", 0) != 0)
            continue;
        if (mode.rfind("Full TS story run ", 0) == 0 && name != "Total" && name.rfind("Split ", 0) != 0)
            continue;
        if (mode == "Full challenge run" && name != "Total")
            continue;
        if (name == "Unknown" || name == "Unknown Challenge")
            continue;

        BestTimeRecord record;
        record.mode = mode;
        record.name = name;
        record.seconds = std::atof(secondsText.c_str());
        if (!record.mode.empty() && !record.name.empty() && record.seconds > 0.0)
            gBestTimes.push_back(record);
    }
}

static void SaveBestTimes()
{
    std::ofstream out(BestTimeFilePath(), std::ios::trunc);
    if (!out)
        return;

    for (const BestTimeRecord& record : gBestTimes)
        out << record.mode << "|" << record.name << "|" << record.seconds << "\n";
}

static double QuantizeRunTimer(double seconds)
{
    if (seconds < 0.0)
        seconds = 0.0;

    return (double)((int)(seconds * 100.0)) / 100.0;
}

static double QuantizeDelta(double seconds)
{
    if (seconds < 0.0)
        return -((double)((int)((-seconds) * 100.0 + 0.0001)) / 100.0);
    return (double)((int)(seconds * 100.0 + 0.0001)) / 100.0;
}

static void RecordBestTime(const std::string& mode, const std::string& name, double seconds)
{
    if (!kBestTimesEnabled)
        return;

    if (mode.empty() || name.empty() || seconds <= 0.0)
        return;
    seconds = QuantizeRunTimer(seconds);
    if (mode == "Challenge timer" || mode == "Challenge run")
        return;
    if (mode == "Full TS story run" && name != "Total" && name.rfind("Split ", 0) != 0)
        return;
    if (mode.rfind("Full TS story run ", 0) == 0 && name != "Total" && name.rfind("Split ", 0) != 0)
        return;
    if (mode == "Full challenge run" && name != "Total")
        return;
    if (name == "Unknown" || name == "Unknown Challenge")
        return;

    for (BestTimeRecord& record : gBestTimes)
    {
        if (record.mode == mode && record.name == name)
        {
            if (seconds < record.seconds || record.seconds <= 0.0)
            {
                record.seconds = seconds;
                SaveBestTimes();
            }
            return;
        }
    }

    BestTimeRecord record;
    record.mode = mode;
    record.name = name;
    record.seconds = seconds;
    gBestTimes.push_back(record);
    SaveBestTimes();
}

static void SetBestTime(const std::string& mode, const std::string& name, double seconds)
{
    if (!kBestTimesEnabled)
        return;

    if (mode.empty() || name.empty() || seconds <= 0.0)
        return;
    seconds = QuantizeRunTimer(seconds);
    if (name == "Unknown" || name == "Unknown Challenge")
        return;

    for (BestTimeRecord& record : gBestTimes)
    {
        if (record.mode == mode && record.name == name)
        {
            record.seconds = seconds;
            SaveBestTimes();
            return;
        }
    }

    BestTimeRecord record;
    record.mode = mode;
    record.name = name;
    record.seconds = seconds;
    gBestTimes.push_back(record);
    SaveBestTimes();
}

static void RecordFullChallengeTotalIfComplete()
{
    if (gFinishedRunTimes.size() != 32)
        return;

    double total = 0.0;
    for (double splitSeconds : gFinishedRunTimes)
        total += QuantizeRunTimer(splitSeconds);
    RecordBestTime("Full challenge run", "Total", total);
}

static void CommitFullChallengePendingSplit()
{
    if (!gFullChallengePendingSplit)
        return;

    gFinishedRunTimes.push_back(gFullChallengePendingSeconds);
    gFinishedRunBestDiffs.push_back(0.0);
    gFinishedRunBestDiffKnown.push_back(false);
    if (gFinishedRunTimes.size() > 32)
    {
        gFinishedRunTimes.erase(gFinishedRunTimes.begin());
        if (!gFinishedRunBestDiffs.empty())
            gFinishedRunBestDiffs.erase(gFinishedRunBestDiffs.begin());
        if (!gFinishedRunBestDiffKnown.empty())
            gFinishedRunBestDiffKnown.erase(gFinishedRunBestDiffKnown.begin());
    }

    gFullChallengePendingSplit = false;
    gFullChallengePendingSeconds = 0.0;
    if (gFinishedRunTimes.size() < 32)
        gFullChallengeWaitingForReadyLoading = true;
    gRunTimerSeconds = 0.0;
    RecordFullChallengeTotalIfComplete();
}

static double FindBestTimeSeconds(const std::string& mode, const std::string& name)
{
    for (const BestTimeRecord& record : gBestTimes)
    {
        if (record.mode == mode && record.name == name)
            return record.seconds;
    }

    return 0.0;
}

static void FormatRunTimer(double seconds, char* out, size_t outSize)
{
    if (!out || outSize == 0)
        return;

    if (seconds < 0.0)
        seconds = 0.0;

    int totalCentis = (int)(seconds * 100.0);
    int centis = totalCentis % 100;
    int totalSeconds = totalCentis / 100;
    int secs = totalSeconds % 60;
    int mins = (totalSeconds / 60) % 60;
    int hours = totalSeconds / 3600;

    if (hours > 0)
        std::snprintf(out, outSize, "%d:%02d:%02d.%02d", hours, mins, secs, centis);
    else
        std::snprintf(out, outSize, "%d:%02d.%02d", mins, secs, centis);
}

static void FormatRunDelta(double seconds, char* out, size_t outSize)
{
    if (!out || outSize == 0)
        return;

    seconds = QuantizeDelta(seconds);
    double absSeconds = seconds < 0.0 ? -seconds : seconds;
    int totalCentis = (int)(absSeconds * 100.0);
    int centis = totalCentis % 100;
    int totalSeconds = totalCentis / 100;
    int secs = totalSeconds % 60;
    int mins = (totalSeconds / 60) % 60;
    int hours = totalSeconds / 3600;
    char sign = seconds < 0.0 ? '-' : '+';

    if (hours > 0)
        std::snprintf(out, outSize, "%c%d:%02d:%02d.%02d", sign, hours, mins, secs, centis);
    else if (mins > 0)
        std::snprintf(out, outSize, "%c%d:%02d.%02d", sign, mins, secs, centis);
    else
        std::snprintf(out, outSize, "%c%d.%02d", sign, secs, centis);
}

struct AutoSplitBridge
{
    ICLRRuntimeHost* runtime = nullptr;
    wchar_t bridgePath[MAX_PATH] = {};
    bool initialized = false;
    bool available = false;
    ULONGLONG nextPollAt = 0;
    ULONGLONG nextDifficultyPollAt = 0;

    ~AutoSplitBridge()
    {
        if (runtime)
            runtime->Release();
    }

    bool Init()
    {
        if (initialized) return available;
        initialized = true;

        ICLRMetaHost* metaHost = nullptr;
        ICLRRuntimeInfo* runtimeInfo = nullptr;

        HRESULT hr = CLRCreateInstance(CLSID_CLRMetaHost, IID_PPV_ARGS(&metaHost));
        if (FAILED(hr)) goto cleanup;

        if (!ResolveBridgePath()) goto cleanup;

        hr = metaHost->GetRuntime(L"v4.0.30319", IID_PPV_ARGS(&runtimeInfo));
        if (FAILED(hr)) goto cleanup;

        BOOL loadable = FALSE;
        hr = runtimeInfo->IsLoadable(&loadable);
        if (FAILED(hr) || !loadable) goto cleanup;

        hr = runtimeInfo->GetInterface(CLSID_CLRRuntimeHost, IID_PPV_ARGS(&runtime));
        if (FAILED(hr)) goto cleanup;

        hr = runtime->Start();
        if (FAILED(hr)) goto cleanup;

        available = true;

    cleanup:
        if (runtimeInfo) runtimeInfo->Release();
        if (metaHost) metaHost->Release();
        if (!available && runtime)
        {
            runtime->Release();
            runtime = nullptr;
        }
        return available;
    }

    bool ResolveBridgePath()
    {
        if (bridgePath[0]) return true;

        DWORD len = GetModuleFileNameW(nullptr, bridgePath, _countof(bridgePath));
        if (len == 0 || len >= _countof(bridgePath)) return false;

        for (DWORD i = len; i > 0; --i)
        {
            if (bridgePath[i - 1] == L'\\' || bridgePath[i - 1] == L'/')
            {
                bridgePath[i] = L'\0';
                break;
            }
        }

        const wchar_t name[] = L"TSRAutosplitBridge.dll";
        size_t curLen = wcslen(bridgePath);
        if (curLen + _countof(name) >= _countof(bridgePath)) return false;
        wcscat_s(bridgePath, name);
        return GetFileAttributesW(bridgePath) != INVALID_FILE_ATTRIBUTES;
    }

    DWORD Poll(ULONGLONG nowMs)
    {
        if (nowMs < nextPollAt) return 0;
        nextPollAt = nowMs + 4;

        if (!Init() || !runtime) return 0;

        DWORD ret = 0;
        HRESULT hr = runtime->ExecuteInDefaultAppDomain(
            bridgePath,
            L"TSRTimer.AutoSplitBridge",
            L"Poll",
            L"",
            &ret
        );

        if (FAILED(hr) || (ret & AUTO_ERROR)) return 0;
        return ret;
    }

    void ResetTimerState(bool forceClear = false)
    {
        if (!Init() || !runtime) return;

        DWORD ret = 0;
        runtime->ExecuteInDefaultAppDomain(
            bridgePath,
            L"TSRTimer.AutoSplitBridge",
            L"ResetTimerState",
            forceClear ? L"clear" : L"",
            &ret
        );
    }

    int GetStoryDifficulty(ULONGLONG nowMs)
    {
        if (nowMs < nextDifficultyPollAt)
            return -1;

        nextDifficultyPollAt = nowMs + 250;
        if (!Init() || !runtime) return -1;

        DWORD ret = 0;
        HRESULT hr = runtime->ExecuteInDefaultAppDomain(
            bridgePath,
            L"TSRTimer.AutoSplitBridge",
            L"GetStoryDifficulty",
            L"",
            &ret
        );
        if (FAILED(hr) || ret > 2) return -1;
        return (int)ret;
    }
};

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;

    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    if (msg == WM_SETCURSOR)
    {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
    }

    if (msg == WM_LBUTTONDOWN)
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (gAppScreen == AppScreen::ModeSelector)
        {
            if (PointInRect(gScaleDownButtonRect, x, y))
            {
                SetOverlayScale(gOverlayScalePercent - 10);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (PointInRect(gScaleUpButtonRect, x, y))
            {
                SetOverlayScale(gOverlayScalePercent + 10);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (PointInRect(gModeCloseButtonRect, x, y))
            {
                DestroyWindow(hwnd);
                return 0;
            }

            for (int i = 0; i < kModeCount; ++i)
            {
                if (PointInRect(gModeButtonRects[i], x, y))
                {
                    if (i >= 0 && i <= 2)
                    {
                        if (i == 1)
                            gAppScreen = AppScreen::FullStoryTimer;
                        else
                            gAppScreen = AppScreen::SingleLevelTimer;
                        gChallengeModeActive = (i == 2);
                        gChallengeEnteredAt = gChallengeModeActive ? GetTimeSeconds() : -1.0;
                        gScreenChanged = true;
                        gFinishedRunTimes.clear();
                        gFinishedRunBestDiffs.clear();
                        gFinishedRunBestDiffKnown.clear();
                        gRunTimerSeconds = 0.0;
                        gRunTimerLineActive = false;
                        gSingleTimerBestDiffKnown = false;
                        gSingleTimerBestDiff = 0.0;
                        gFullStoryBestDiffKnown = false;
                        gFullStoryBestDiff = 0.0;
                        gFullStoryMenuAfterFinish = false;
                        gFullChallengePendingSplit = false;
                        gFullChallengePendingSeconds = 0.0;
                        gFullChallengeReadyAfterLoading = false;
                        gFullChallengeWaitingForReadyLoading = false;
                        gForceClearResetRequested = true;
                        gResetTimerRequested = true;
                    }
                    else
                    {
                        gAppScreen = AppScreen::BestTimes;
                        gChallengeModeActive = false;
                        gChallengeEnteredAt = -1.0;
                        gScreenChanged = true;
                        LoadBestTimes();
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            ReleaseCapture();
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }

        if (gAppScreen == AppScreen::BestTimes)
        {
            for (int i = 0; i < 3; ++i)
            {
                if (PointInRect(gBestSplitButtonRects[i], x, y))
                {
                    gBestSplitsDifficultyIndex = i;
                    gAppScreen = AppScreen::BestStorySplits;
                    gScreenChanged = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
        }

        if (PointInRect(gResetButtonRect, x, y))
        {
            gFinishedRunTimes.clear();
            gFinishedRunBestDiffs.clear();
            gFinishedRunBestDiffKnown.clear();
            gRunTimerSeconds = 0.0;
            gRunTimerLineActive = false;
            gSingleTimerBestDiffKnown = false;
            gSingleTimerBestDiff = 0.0;
            gFullStoryBestDiffKnown = false;
            gFullStoryBestDiff = 0.0;
            gFullStoryMenuAfterFinish = false;
            gFullChallengePendingSplit = false;
            gFullChallengePendingSeconds = 0.0;
            gFullChallengeReadyAfterLoading = false;
            gFullChallengeWaitingForReadyLoading = false;
            gForceClearResetRequested = true;
            gResetTimerRequested = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (PointInRect(gBackButtonRect, x, y))
        {
            if (gAppScreen == AppScreen::BestStorySplits)
            {
                gAppScreen = AppScreen::BestTimes;
                gScreenChanged = true;
                LoadBestTimes();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            gAppScreen = AppScreen::ModeSelector;
            gScreenChanged = true;
            gChallengeModeActive = false;
            gChallengeEnteredAt = -1.0;
            gFinishedRunTimes.clear();
            gFinishedRunBestDiffs.clear();
            gFinishedRunBestDiffKnown.clear();
            gRunTimerSeconds = 0.0;
            gRunTimerLineActive = false;
            gSingleTimerBestDiffKnown = false;
            gSingleTimerBestDiff = 0.0;
            gFullStoryBestDiffKnown = false;
            gFullStoryBestDiff = 0.0;
            gFullStoryMenuAfterFinish = false;
            gFullChallengePendingSplit = false;
            gFullChallengePendingSeconds = 0.0;
            gFullChallengeReadyAfterLoading = false;
            gFullChallengeWaitingForReadyLoading = false;
            gForceClearResetRequested = true;
            gResetTimerRequested = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (PointInRect(gScaleDownButtonRect, x, y))
        {
            SetOverlayScale(gOverlayScalePercent - 10);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (PointInRect(gScaleUpButtonRect, x, y))
        {
            SetOverlayScale(gOverlayScalePercent + 10);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (PointInRect(gCloseButtonRect, x, y))
        {
            DestroyWindow(hwnd);
            return 0;
        }

        ReleaseCapture();
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void DrawModeSelector(HWND hwnd)
{
    RECT rect;
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;

    HDC hdc = GetDC(hwnd);

    static HDC memDC = nullptr;
    static HBITMAP memBmp = nullptr;
    static HBITMAP oldBmp = nullptr;
    static int bufW = 0;
    static int bufH = 0;

    if (!memDC) memDC = CreateCompatibleDC(hdc);

    if (!memBmp || bufW != w || bufH != h)
    {
        if (memBmp)
        {
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
        }

        memBmp = CreateCompatibleBitmap(hdc, w, h);
        oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
        bufW = w;
        bufH = h;
    }

    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memDC, &rect, bg);
    DeleteObject(bg);
    SetBkMode(memDC, TRANSPARENT);

    HFONT titleFont = CreateFontA(
        ScaleUi(22), 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        "Arial"
    );
    HFONT itemFont = CreateFontA(
        ScaleUi(17), 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        "Arial"
    );
    HGDIOBJ oldFont = SelectObject(memDC, titleFont);

    RECT title = rect;
    title.top += ScaleUi(34);
    SetTextColor(memDC, RGB(220, 220, 220));
    DrawTextA(memDC, "TSR-Timer", -1, &title, DT_CENTER | DT_TOP | DT_SINGLELINE);

    SelectObject(memDC, itemFont);

    auto DrawButton = [&](RECT& r, const char* text, bool primary, bool symbolButton = false) {
        HBRUSH brush = CreateSolidBrush(primary ? RGB(70, 50, 55) : RGB(35, 35, 43));
        HPEN pen = CreatePen(PS_SOLID, 1, primary ? RGB(220, 110, 110) : RGB(120, 120, 130));
        HGDIOBJ oldBrush = SelectObject(memDC, brush);
        HGDIOBJ oldPen = SelectObject(memDC, pen);
        RoundRect(memDC, r.left, r.top, r.right, r.bottom, ScaleUi(6), ScaleUi(6));
        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);

        RECT textRect = r;
        if (!symbolButton)
            textRect.left += ScaleUi(10);
        SetTextColor(memDC, primary ? RGB(255, 235, 235) : RGB(220, 220, 220));
        DrawTextA(memDC, text, -1, &textRect,
            (symbolButton ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE);
    };

    int top = ScaleUi(64);
    for (int i = 0; i < kModeCount; ++i)
    {
        gModeButtonRects[i] = { ScaleUi(12), top + (i * ScaleUi(34)), w - ScaleUi(12), top + (i * ScaleUi(34)) + ScaleUi(26) };
        DrawButton(gModeButtonRects[i], kModeNames[i], false);
    }

    if (gModeStatusText[0])
    {
        RECT status = rect;
        status.left += ScaleUi(12);
        status.right -= ScaleUi(12);
        status.top = ScaleUi(178);
        SetTextColor(memDC, RGB(220, 110, 110));
        DrawTextA(memDC, gModeStatusText, -1, &status, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }

    int symbolSize = ScaleUi(20);
    int buttonTop = ScaleUi(8);
    gScaleDownButtonRect = { ScaleUi(12), buttonTop, ScaleUi(12) + symbolSize, buttonTop + symbolSize };
    gScaleUpButtonRect = { gScaleDownButtonRect.right + ScaleUi(4), buttonTop, gScaleDownButtonRect.right + ScaleUi(4) + symbolSize, buttonTop + symbolSize };
    gModeCloseButtonRect = { w - ScaleUi(12) - symbolSize, buttonTop, w - ScaleUi(12), buttonTop + symbolSize };
    DrawButton(gScaleDownButtonRect, "-", false, true);
    DrawButton(gScaleUpButtonRect, "+", false, true);
    DrawButton(gModeCloseButtonRect, "X", true, true);

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldFont);
    DeleteObject(titleFont);
    DeleteObject(itemFont);
    ReleaseDC(hwnd, hdc);
}

static void DrawOverlay(HWND hwnd)
{
    RECT rect;
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;

    HDC hdc = GetDC(hwnd);

    static HDC memDC = nullptr;
    static HBITMAP memBmp = nullptr;
    static HBITMAP oldBmp = nullptr;
    static int bufW = 0;
    static int bufH = 0;

    if (!memDC) memDC = CreateCompatibleDC(hdc);

    if (!memBmp || bufW != w || bufH != h)
    {
        if (memBmp)
        {
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
        }

        memBmp = CreateCompatibleBitmap(hdc, w, h);
        oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
        bufW = w;
        bufH = h;
    }

    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memDC, &rect, bg);
    DeleteObject(bg);
    SetBkMode(memDC, TRANSPARENT);

    HFONT font = CreateFontA(
        ScaleUi(18), 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        "Arial"
    );
    HFONT buttonFont = CreateFontA(
        ScaleUi(17), 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        "Arial"
    );
    HFONT captionFont = CreateFontA(
        ScaleUi(11), 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        "Arial"
    );
    HGDIOBJ oldFont = SelectObject(memDC, font);

    const int padL = ScaleUi(12);
    const int padR = ScaleUi(10);
    const int padT = ScaleUi(7);
    const int padB = ScaleUi(7);
    const int lineStep = ScaleUi(25);

    RECT content = rect;
    content.left += padL;
    content.right -= padR;
    content.top += padT + ScaleUi(26);
    content.bottom -= padB;

    char timerText[32];
    char deltaText[32];
    char line[64];

    auto DrawTimerLine = [&](int rowNumber, double seconds, int rowY, COLORREF timeColor) {
        FormatRunTimer(seconds, timerText, sizeof(timerText));
        std::snprintf(line, sizeof(line), "%d  %s", rowNumber, timerText);

        RECT rTime = content;
        rTime.right = content.left + ScaleUi(gAppScreen == AppScreen::FullStoryTimer ? 74 :
            (gAppScreen == AppScreen::FullChallengeTimer ? 70 : 88));
        rTime.top = rowY;
        SetTextColor(memDC, timeColor);
        DrawTextA(memDC, line, -1, &rTime, DT_LEFT | DT_TOP | DT_SINGLELINE);

        if (gAppScreen == AppScreen::FullStoryTimer && rowNumber >= 1 && rowNumber <= 9)
        {
            int rowIndex = rowNumber - 1;
            RECT rName = content;
            rName.left = rTime.right + ScaleUi(2);
            if (rowIndex >= 0 &&
                rowIndex < (int)gFinishedRunBestDiffKnown.size() &&
                gFinishedRunBestDiffKnown[rowIndex] &&
                rowIndex < (int)gFinishedRunBestDiffs.size())
            {
                FormatRunDelta(gFinishedRunBestDiffs[rowIndex], deltaText, sizeof(deltaText));
                RECT rDelta = content;
                rDelta.left = rTime.right + ScaleUi(2);
                rDelta.right = rDelta.left + ScaleUi(48);
                rDelta.top = rowY;
                SetTextColor(memDC, gFinishedRunBestDiffs[rowIndex] > 0.0 ? RGB(220, 110, 110) : RGB(110, 220, 140));
                DrawTextA(memDC, deltaText, -1, &rDelta, DT_LEFT | DT_TOP | DT_SINGLELINE);
                rName.left = rDelta.right + ScaleUi(2);
            }

            rName.top = rowY;
            SetTextColor(memDC, RGB(220, 220, 220));
            DrawTextA(memDC, kRouteNames[rowNumber - 1], -1, &rName, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        else if (gAppScreen == AppScreen::FullChallengeTimer && rowNumber >= 1 && rowNumber <= 32)
        {
            RECT rName = content;
            rName.left = rTime.right - ScaleUi(2);
            rName.top = rowY;
            SetTextColor(memDC, RGB(220, 220, 220));
            DrawTextA(memDC, kChallengeRouteNames[rowNumber - 1], -1, &rName, DT_RIGHT | DT_TOP | DT_SINGLELINE);
        }
    };

    double totalSeconds = 0.0;
    for (size_t i = 0; i < gFinishedRunTimes.size(); ++i)
        totalSeconds += QuantizeRunTimer(gFinishedRunTimes[i]);

    const size_t maxRows = (gAppScreen == AppScreen::FullChallengeTimer) ? 32 : 9;
    bool showRunningLine = gRunTimerLineActive ||
        (!gFullChallengePendingSplit && gFinishedRunTimes.empty());
    bool showPendingLine = gFullChallengePendingSplit;
    bool showReadyLine =
        gAppScreen == AppScreen::FullChallengeTimer &&
        gFullChallengeReadyAfterLoading &&
        !gRunTimerLineActive &&
        gFinishedRunTimes.size() + (gFullChallengePendingSplit ? 1 : 0) < maxRows;
    size_t visibleExtraRows = (showRunningLine ? 1 : 0) +
        (showPendingLine ? 1 : 0) +
        (showReadyLine ? 1 : 0);
    size_t maxFinishedRows = visibleExtraRows >= maxRows ? 0 : maxRows - visibleExtraRows;
    size_t firstFinished = 0;
    if (gFinishedRunTimes.size() > maxFinishedRows)
        firstFinished = gFinishedRunTimes.size() - maxFinishedRows;

    int rowY = content.top;
    int rowNumber = (int)firstFinished + 1;
    for (size_t i = firstFinished; i < gFinishedRunTimes.size(); ++i)
    {
        DrawTimerLine(rowNumber++, gFinishedRunTimes[i], rowY, RGB(255, 255, 255));
        rowY += lineStep;
    }

    if (showPendingLine)
    {
        DrawTimerLine(rowNumber++, gFullChallengePendingSeconds, rowY, RGB(255, 255, 255));
        totalSeconds += QuantizeRunTimer(gFullChallengePendingSeconds);
        rowY += lineStep;
    }

    if (showReadyLine)
    {
        DrawTimerLine(rowNumber++, 0.0, rowY, RGB(220, 110, 110));
        rowY += lineStep;
    }

    if (showRunningLine)
    {
        DrawTimerLine(rowNumber++, gRunTimerSeconds, rowY, RGB(220, 110, 110));
        if (gRunTimerLineActive)
            totalSeconds += QuantizeRunTimer(gRunTimerSeconds);
    }

    FormatRunTimer(totalSeconds, timerText, sizeof(timerText));
    std::snprintf(line, sizeof(line), "Total %s", timerText);
    RECT rTotal = content;
    rTotal.top = content.bottom - ScaleUi(31);
    SetTextColor(memDC, RGB(220, 220, 220));
    DrawTextA(memDC, line, -1, &rTotal, DT_LEFT | DT_TOP | DT_SINGLELINE);

    if (gAppScreen == AppScreen::FullStoryTimer)
    {
        double cumulativeDiff = 0.0;
        bool cumulativeDiffKnown = false;
        size_t diffCount = gFinishedRunBestDiffKnown.size();
        if (gFinishedRunBestDiffs.size() < diffCount)
            diffCount = gFinishedRunBestDiffs.size();
        if (gFinishedRunTimes.size() < diffCount)
            diffCount = gFinishedRunTimes.size();

        for (size_t i = 0; i < diffCount; ++i)
        {
            if (!gFinishedRunBestDiffKnown[i])
                continue;
            cumulativeDiff += QuantizeDelta(gFinishedRunBestDiffs[i]);
            cumulativeDiffKnown = true;
        }

        if (cumulativeDiffKnown)
        {
            cumulativeDiff = QuantizeDelta(cumulativeDiff);
            FormatRunDelta(cumulativeDiff, deltaText, sizeof(deltaText));
            RECT rTotalDelta = content;
            rTotalDelta.left += ScaleUi(98);
            rTotalDelta.right = rTotalDelta.left + ScaleUi(58);
            rTotalDelta.top = rTotal.top;
            SetTextColor(memDC, cumulativeDiff > 0.0 ? RGB(220, 110, 110) : RGB(110, 220, 140));
            DrawTextA(memDC, deltaText, -1, &rTotalDelta, DT_LEFT | DT_TOP | DT_SINGLELINE);
        }
    }

    SelectObject(memDC, buttonFont);
    auto DrawButton = [&](RECT& r, const char* text, COLORREF fill, COLORREF edge) {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, edge);
        HGDIOBJ oldBrush = SelectObject(memDC, brush);
        HGDIOBJ oldPen = SelectObject(memDC, pen);
        RoundRect(memDC, r.left, r.top, r.right, r.bottom, ScaleUi(6), ScaleUi(6));
        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);

        SetTextColor(memDC, fill == RGB(70, 50, 55) ? RGB(255, 235, 235) : RGB(220, 220, 220));
        DrawTextA(memDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    int buttonTop = ScaleUi(8);
    gScaleDownButtonRect = { ScaleUi(12), buttonTop, ScaleUi(32), buttonTop + ScaleUi(20) };
    gScaleUpButtonRect = { gScaleDownButtonRect.right + ScaleUi(4), buttonTop, gScaleDownButtonRect.right + ScaleUi(24), buttonTop + ScaleUi(20) };
    gBackButtonRect = { gScaleUpButtonRect.right + ScaleUi(4), buttonTop, gScaleUpButtonRect.right + ScaleUi(48), buttonTop + ScaleUi(20) };
    gResetButtonRect = { gBackButtonRect.right + ScaleUi(4), buttonTop, gBackButtonRect.right + ScaleUi(50), buttonTop + ScaleUi(20) };
    gCloseButtonRect = { w - ScaleUi(32), buttonTop, w - ScaleUi(12), buttonTop + ScaleUi(20) };

    DrawButton(gScaleDownButtonRect, "-", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gScaleUpButtonRect, "+", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gBackButtonRect, "Back", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gResetButtonRect, "Reset", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gCloseButtonRect, "X", RGB(70, 50, 55), RGB(220, 110, 110));

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldFont);
    DeleteObject(font);
    DeleteObject(buttonFont);
    DeleteObject(captionFont);
    ReleaseDC(hwnd, hdc);
}

static void DrawSingleTimer(HWND hwnd)
{
    RECT rect;
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;

    HDC hdc = GetDC(hwnd);

    static HDC memDC = nullptr;
    static HBITMAP memBmp = nullptr;
    static HBITMAP oldBmp = nullptr;
    static int bufW = 0;
    static int bufH = 0;

    if (!memDC) memDC = CreateCompatibleDC(hdc);

    if (!memBmp || bufW != w || bufH != h)
    {
        if (memBmp)
        {
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
        }

        memBmp = CreateCompatibleBitmap(hdc, w, h);
        oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
        bufW = w;
        bufH = h;
    }

    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memDC, &rect, bg);
    DeleteObject(bg);
    SetBkMode(memDC, TRANSPARENT);

    HFONT timerFont = CreateFontA(
        ScaleUi(28), 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        "Arial"
    );
    HFONT buttonFont = CreateFontA(
        ScaleUi(17), 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        "Arial"
    );
    HGDIOBJ oldFont = SelectObject(memDC, timerFont);

    char timerText[32];
    FormatRunTimer(gRunTimerSeconds, timerText, sizeof(timerText));

    RECT timerRect = rect;
    timerRect.top += ScaleUi(45);
    SetTextColor(memDC, gRunTimerLineActive ? RGB(220, 110, 110) : RGB(220, 220, 220));
    DrawTextA(memDC, timerText, -1, &timerRect, DT_CENTER | DT_TOP | DT_SINGLELINE);

    if (gSingleTimerBestDiffKnown)
    {
        char deltaText[32];
        FormatRunDelta(gSingleTimerBestDiff, deltaText, sizeof(deltaText));
        RECT deltaRect = rect;
        deltaRect.top += ScaleUi(70);
        SetTextColor(memDC, gSingleTimerBestDiff > 0.0 ? RGB(220, 110, 110) : RGB(110, 220, 140));
        DrawTextA(memDC, deltaText, -1, &deltaRect, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }

    SelectObject(memDC, buttonFont);
    auto DrawButton = [&](RECT& r, const char* text, COLORREF fill, COLORREF edge) {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, edge);
        HGDIOBJ oldBrush = SelectObject(memDC, brush);
        HGDIOBJ oldPen = SelectObject(memDC, pen);
        RoundRect(memDC, r.left, r.top, r.right, r.bottom, ScaleUi(6), ScaleUi(6));
        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);

        SetTextColor(memDC, fill == RGB(70, 50, 55) ? RGB(255, 235, 235) : RGB(220, 220, 220));
        DrawTextA(memDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    int buttonTop = ScaleUi(8);
    gScaleDownButtonRect = { ScaleUi(12), buttonTop, ScaleUi(32), buttonTop + ScaleUi(20) };
    gScaleUpButtonRect = { gScaleDownButtonRect.right + ScaleUi(4), buttonTop, gScaleDownButtonRect.right + ScaleUi(24), buttonTop + ScaleUi(20) };
    gBackButtonRect = { gScaleUpButtonRect.right + ScaleUi(4), buttonTop, gScaleUpButtonRect.right + ScaleUi(48), buttonTop + ScaleUi(20) };
    gResetButtonRect = { gBackButtonRect.right + ScaleUi(4), buttonTop, gBackButtonRect.right + ScaleUi(50), buttonTop + ScaleUi(20) };
    gCloseButtonRect = { w - ScaleUi(32), buttonTop, w - ScaleUi(12), buttonTop + ScaleUi(20) };

    DrawButton(gScaleDownButtonRect, "-", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gScaleUpButtonRect, "+", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gBackButtonRect, "Back", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gResetButtonRect, "Reset", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gCloseButtonRect, "X", RGB(70, 50, 55), RGB(220, 110, 110));

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldFont);
    DeleteObject(timerFont);
    DeleteObject(buttonFont);
    ReleaseDC(hwnd, hdc);
}

static void DrawBestTimes(HWND hwnd)
{
    RECT rect;
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;

    HDC hdc = GetDC(hwnd);

    static HDC memDC = nullptr;
    static HBITMAP memBmp = nullptr;
    static HBITMAP oldBmp = nullptr;
    static int bufW = 0;
    static int bufH = 0;

    if (!memDC) memDC = CreateCompatibleDC(hdc);

    if (!memBmp || bufW != w || bufH != h)
    {
        if (memBmp)
        {
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
        }

        memBmp = CreateCompatibleBitmap(hdc, w, h);
        oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
        bufW = w;
        bufH = h;
    }

    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memDC, &rect, bg);
    DeleteObject(bg);
    SetBkMode(memDC, TRANSPARENT);

    HFONT titleFont = CreateFontA(ScaleUi(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Arial");
    HFONT itemFont = CreateFontA(ScaleUi(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Arial");
    HFONT buttonFont = CreateFontA(ScaleUi(17), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Arial");
    HGDIOBJ oldFont = SelectObject(memDC, titleFont);

    RECT title = rect;
    title.top += ScaleUi(31);
    SetTextColor(memDC, RGB(220, 220, 220));
    DrawTextA(memDC, "Best Times", -1, &title, DT_CENTER | DT_TOP | DT_SINGLELINE);

    SelectObject(memDC, itemFont);
    int y = ScaleUi(56);
    int lineStep = ScaleUi(20);
    char timeText[32];
    char line[192];
    for (RECT& r : gBestSplitButtonRects)
        r = { 0, 0, 0, 0 };

    auto DrawHeader = [&](const char* text) {
        RECT row = rect;
        row.left += ScaleUi(12);
        row.right -= ScaleUi(12);
        row.top = y;
        SetTextColor(memDC, RGB(220, 110, 110));
        DrawTextA(memDC, text, -1, &row, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += lineStep;
    };

    auto DrawBestRow = [&](const std::string& mode, const std::string& name, const char* displayName) {
        double seconds = FindBestTimeSeconds(mode, name);
        FormatRunTimer(seconds, timeText, sizeof(timeText));
        std::snprintf(line, sizeof(line), "%s  %s", timeText, displayName);

        RECT row = rect;
        row.left += ScaleUi(12);
        row.right -= ScaleUi(12);
        row.top = y;
        SetTextColor(memDC, seconds > 0.0 ? RGB(220, 220, 220) : RGB(150, 150, 150));
        DrawTextA(memDC, line, -1, &row, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += lineStep;
    };

    auto DrawFullStoryRow = [&](const std::string& mode, int difficultyIndex) {
        double seconds = FindBestTimeSeconds(mode, "Total");
        FormatRunTimer(seconds, timeText, sizeof(timeText));
        std::snprintf(line, sizeof(line), "%s  Full Story Run", timeText);

        RECT row = rect;
        row.left += ScaleUi(12);
        row.right -= ScaleUi(42);
        row.top = y;
        SetTextColor(memDC, seconds > 0.0 ? RGB(220, 220, 220) : RGB(150, 150, 150));
        DrawTextA(memDC, line, -1, &row, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (difficultyIndex >= 0 && difficultyIndex < 3 && seconds > 0.0)
        {
            gBestSplitButtonRects[difficultyIndex] = {
                rect.right - ScaleUi(34),
                y - ScaleUi(1),
                rect.right - ScaleUi(12),
                y + ScaleUi(18)
            };
            RECT button = gBestSplitButtonRects[difficultyIndex];
            HBRUSH brush = CreateSolidBrush(RGB(35, 35, 43));
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(120, 120, 130));
            HGDIOBJ oldBrush = SelectObject(memDC, brush);
            HGDIOBJ oldPen = SelectObject(memDC, pen);
            RoundRect(memDC, button.left, button.top, button.right, button.bottom, ScaleUi(5), ScaleUi(5));
            SelectObject(memDC, oldBrush);
            SelectObject(memDC, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
            SetTextColor(memDC, RGB(220, 220, 220));
            DrawTextA(memDC, ">", -1, &button, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        y += lineStep;
    };

    const char* difficulties[] = { "Easy", "Normal", "Hard" };
    for (int difficultyIndex = 0; difficultyIndex < 3; ++difficultyIndex)
    {
        const char* difficulty = difficulties[difficultyIndex];
        DrawHeader(difficulty);
        std::string singleMode = std::string("Single story level run ") + difficulty;
        std::string fullMode = std::string("Full TS story run ") + difficulty;
        for (const char* routeName : kRouteNames)
            DrawBestRow(singleMode, routeName, routeName);
        DrawFullStoryRow(fullMode, difficultyIndex);
    }
    SelectObject(memDC, buttonFont);
    auto DrawButton = [&](RECT& r, const char* text, COLORREF fill, COLORREF edge) {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, edge);
        HGDIOBJ oldBrush = SelectObject(memDC, brush);
        HGDIOBJ oldPen = SelectObject(memDC, pen);
        RoundRect(memDC, r.left, r.top, r.right, r.bottom, ScaleUi(6), ScaleUi(6));
        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);

        SetTextColor(memDC, fill == RGB(70, 50, 55) ? RGB(255, 235, 235) : RGB(220, 220, 220));
        DrawTextA(memDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    int buttonTop = ScaleUi(8);
    gScaleDownButtonRect = { ScaleUi(12), buttonTop, ScaleUi(32), buttonTop + ScaleUi(20) };
    gScaleUpButtonRect = { gScaleDownButtonRect.right + ScaleUi(4), buttonTop, gScaleDownButtonRect.right + ScaleUi(24), buttonTop + ScaleUi(20) };
    gBackButtonRect = { gScaleUpButtonRect.right + ScaleUi(4), buttonTop, gScaleUpButtonRect.right + ScaleUi(48), buttonTop + ScaleUi(20) };
    gResetButtonRect = { 0, 0, 0, 0 };
    gCloseButtonRect = { w - ScaleUi(32), buttonTop, w - ScaleUi(12), buttonTop + ScaleUi(20) };

    DrawButton(gScaleDownButtonRect, "-", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gScaleUpButtonRect, "+", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gBackButtonRect, "Back", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gCloseButtonRect, "X", RGB(70, 50, 55), RGB(220, 110, 110));

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldFont);
    DeleteObject(titleFont);
    DeleteObject(itemFont);
    DeleteObject(buttonFont);
    ReleaseDC(hwnd, hdc);
}

static void DrawBestStorySplits(HWND hwnd)
{
    RECT rect;
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;

    HDC hdc = GetDC(hwnd);

    static HDC memDC = nullptr;
    static HBITMAP memBmp = nullptr;
    static HBITMAP oldBmp = nullptr;
    static int bufW = 0;
    static int bufH = 0;

    if (!memDC) memDC = CreateCompatibleDC(hdc);
    if (!memBmp || bufW != w || bufH != h)
    {
        if (memBmp)
        {
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
        }
        memBmp = CreateCompatibleBitmap(hdc, w, h);
        oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
        bufW = w;
        bufH = h;
    }

    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memDC, &rect, bg);
    DeleteObject(bg);
    SetBkMode(memDC, TRANSPARENT);

    HFONT titleFont = CreateFontA(ScaleUi(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Arial");
    HFONT itemFont = CreateFontA(ScaleUi(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Arial");
    HFONT buttonFont = CreateFontA(ScaleUi(17), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Arial");
    HGDIOBJ oldFont = SelectObject(memDC, titleFont);

    RECT title = rect;
    title.top += ScaleUi(31);
    SetTextColor(memDC, RGB(220, 220, 220));
    std::string titleText = std::string("Full Story PB Splits - ") + StoryDifficultyNameForIndex(gBestSplitsDifficultyIndex);
    DrawTextA(memDC, titleText.c_str(), -1, &title, DT_CENTER | DT_TOP | DT_SINGLELINE);

    SelectObject(memDC, itemFont);
    int y = ScaleUi(56);
    int lineStep = ScaleUi(24);
    char timeText[32];
    char line[192];
    std::string fullMode = StoryModeNameForDifficulty("Full TS story run", gBestSplitsDifficultyIndex);
    for (int i = 0; i < 9; ++i)
    {
        double seconds = FindBestTimeSeconds(fullMode, FullStorySplitName(i));
        FormatRunTimer(seconds, timeText, sizeof(timeText));
        std::snprintf(line, sizeof(line), "%d  %s  %s", i + 1, timeText, kRouteNames[i]);
        RECT row = rect;
        row.left += ScaleUi(12);
        row.right -= ScaleUi(12);
        row.top = y;
        SetTextColor(memDC, seconds > 0.0 ? RGB(220, 220, 220) : RGB(150, 150, 150));
        DrawTextA(memDC, line, -1, &row, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += lineStep;
    }

    double total = FindBestTimeSeconds(fullMode, "Total");
    FormatRunTimer(total, timeText, sizeof(timeText));
    std::snprintf(line, sizeof(line), "Total %s", timeText);
    RECT totalRow = rect;
    totalRow.left += ScaleUi(12);
    totalRow.right -= ScaleUi(12);
    totalRow.top = y + ScaleUi(8);
    SetTextColor(memDC, RGB(220, 220, 220));
    DrawTextA(memDC, line, -1, &totalRow, DT_LEFT | DT_TOP | DT_SINGLELINE);

    SelectObject(memDC, buttonFont);
    auto DrawButton = [&](RECT& r, const char* text, COLORREF fill, COLORREF edge) {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, edge);
        HGDIOBJ oldBrush = SelectObject(memDC, brush);
        HGDIOBJ oldPen = SelectObject(memDC, pen);
        RoundRect(memDC, r.left, r.top, r.right, r.bottom, ScaleUi(6), ScaleUi(6));
        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
        SetTextColor(memDC, fill == RGB(70, 50, 55) ? RGB(255, 235, 235) : RGB(220, 220, 220));
        DrawTextA(memDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    int buttonTop = ScaleUi(8);
    gScaleDownButtonRect = { ScaleUi(12), buttonTop, ScaleUi(32), buttonTop + ScaleUi(20) };
    gScaleUpButtonRect = { gScaleDownButtonRect.right + ScaleUi(4), buttonTop, gScaleDownButtonRect.right + ScaleUi(24), buttonTop + ScaleUi(20) };
    gBackButtonRect = { gScaleUpButtonRect.right + ScaleUi(4), buttonTop, gScaleUpButtonRect.right + ScaleUi(48), buttonTop + ScaleUi(20) };
    gResetButtonRect = { 0, 0, 0, 0 };
    gCloseButtonRect = { w - ScaleUi(32), buttonTop, w - ScaleUi(12), buttonTop + ScaleUi(20) };

    DrawButton(gScaleDownButtonRect, "-", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gScaleUpButtonRect, "+", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gBackButtonRect, "Back", RGB(35, 35, 43), RGB(120, 120, 130));
    DrawButton(gCloseButtonRect, "X", RGB(70, 50, 55), RGB(220, 110, 110));

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldFont);
    DeleteObject(titleFont);
    DeleteObject(itemFont);
    DeleteObject(buttonFont);
    ReleaseDC(hwnd, hdc);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    QueryPerformanceFrequency(&gFreq);
    InitExeDir();
    LoadBestTimes();

    const char* className = "TSRTimerOverlayClass";
    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_APPWINDOW,
        className, "TSR-Timer",
        WS_POPUP | WS_VISIBLE,
        100, 100, CurrentScreenWidth(), CurrentScreenHeight(),
        nullptr, nullptr, hInst, nullptr
    );
    if (!hwnd) return 0;

    SetLayeredWindowAttributes(hwnd, 0, 200, LWA_ALPHA);

    std::atomic<bool> autoPollRunning(true);
    std::atomic<DWORD> pendingAutoFlags(0);
    std::atomic<bool> bridgeResetRequested(false);
    std::atomic<bool> bridgeForceClearRequested(false);
    std::atomic<bool> bridgeResetCompleted(false);
    std::atomic<int> bridgeStoryDifficulty(-1);
    std::thread autoPollThread([&]() {
        AutoSplitBridge autoSplitWorker;
        while (autoPollRunning.load())
        {
            if (bridgeResetRequested.exchange(false))
            {
                bool forceClear = bridgeForceClearRequested.exchange(false);
                pendingAutoFlags.exchange(0);
                autoSplitWorker.ResetTimerState(forceClear);
                pendingAutoFlags.exchange(0);
                bridgeResetCompleted.store(true);
                Sleep(4);
                continue;
            }

            ULONGLONG tick = GetTickCount64();
            DWORD flags = autoSplitWorker.Poll(tick);
            int difficulty = autoSplitWorker.GetStoryDifficulty(tick);
            if (difficulty >= 0 && difficulty <= 2)
                bridgeStoryDifficulty.store(difficulty);
            if (flags)
                pendingAutoFlags.fetch_or(flags);
            Sleep(4);
        }
    });

    bool appRunning = true;
    bool timerRunning = false;
    double timerStart = 0.0;
    double timerElapsed = 0.0;

    MSG msg{};
    while (appRunning)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                appRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!appRunning)
            break;

        if (gOverlayScaleChanged)
        {
            gOverlayScaleChanged = false;
            RECT winRect{};
            GetWindowRect(hwnd, &winRect);
            SetWindowPos(hwnd, nullptr,
                         winRect.left, winRect.top,
                         CurrentScreenWidth(),
                         CurrentScreenHeight(),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }

        int liveDifficulty = bridgeStoryDifficulty.load();
        if (liveDifficulty >= 0 && liveDifficulty <= 2)
            gStoryDifficultyIndex = liveDifficulty;

        double now = GetTimeSeconds();
        DWORD autoFlags = pendingAutoFlags.exchange(0);

        if (gResetTimerRequested)
        {
            gResetTimerRequested = false;
            bool forceClear = gForceClearResetRequested;
            gForceClearResetRequested = false;
            bridgeForceClearRequested.store(forceClear);
            bridgeResetCompleted.store(false);
            bridgeResetRequested.store(true);
            while (!bridgeResetCompleted.load())
                Sleep(1);
            pendingAutoFlags.exchange(0);
            autoFlags = 0;
            gFinishedRunTimes.clear();
            gFinishedRunBestDiffs.clear();
            gFinishedRunBestDiffKnown.clear();
            timerRunning = false;
            timerElapsed = 0.0;
            gRunTimerSeconds = 0.0;
            gRunTimerLineActive = false;
            gSingleTimerBestDiffKnown = false;
            gSingleTimerBestDiff = 0.0;
            gFullStoryBestDiffKnown = false;
            gFullStoryBestDiff = 0.0;
            gFullStoryMenuAfterFinish = false;
            gFullChallengePendingSplit = false;
            gFullChallengePendingSeconds = 0.0;
            gFullChallengeReadyAfterLoading = false;
            gFullChallengeWaitingForReadyLoading = false;
        }

        if (gScreenChanged)
        {
            gScreenChanged = false;
            RECT winRect{};
            GetWindowRect(hwnd, &winRect);
            SetWindowPos(hwnd, nullptr,
                         winRect.left, winRect.top,
                         CurrentScreenWidth(), CurrentScreenHeight(),
                         SWP_NOZORDER | SWP_NOACTIVATE);
            pendingAutoFlags.exchange(0);
            autoFlags = 0;
        }

        if (gAppScreen == AppScreen::ModeSelector)
        {
            DrawModeSelector(hwnd);
            Sleep(10);
            continue;
        }

        if (gAppScreen == AppScreen::BestTimes)
        {
            DrawBestTimes(hwnd);
            Sleep(10);
            continue;
        }
        if (gAppScreen == AppScreen::BestStorySplits)
        {
            DrawBestStorySplits(hwnd);
            Sleep(10);
            continue;
        }

        if ((autoFlags & AUTO_START_CANCELED) && !gChallengeModeActive)
        {
            autoFlags &= ~AUTO_STARTED;
            if (gAppScreen == AppScreen::FullStoryTimer)
            {
                if (!timerRunning && !gFinishedRunTimes.empty())
                    gFullStoryMenuAfterFinish = true;

                if (gFinishedRunTimes.empty())
                {
                    timerRunning = false;
                    timerElapsed = 0.0;
                    gRunTimerSeconds = 0.0;
                    gRunTimerLineActive = false;
                }
            }
            else
            {
                timerRunning = false;
                timerElapsed = 0.0;
                gRunTimerSeconds = 0.0;
                gRunTimerLineActive = false;
                gSingleTimerBestDiffKnown = false;
                gSingleTimerBestDiff = 0.0;
                gFullStoryBestDiffKnown = false;
                gFullStoryBestDiff = 0.0;
            }
        }
        else if ((autoFlags & AUTO_START_CANCELED) && gAppScreen == AppScreen::FullChallengeTimer)
        {
            autoFlags &= ~AUTO_CHALLENGE_STARTED;
            CommitFullChallengePendingSplit();
        }

        if ((autoFlags & AUTO_LOADING) && gChallengeModeActive)
        {
            pendingAutoFlags.exchange(0);
            autoFlags = 0;
            if (gAppScreen == AppScreen::FullChallengeTimer &&
                (gFullChallengePendingSplit || gFullChallengeWaitingForReadyLoading))
            {
                gFullChallengeReadyAfterLoading = true;
                gFullChallengeWaitingForReadyLoading = false;
            }
            bool allowRunningLoadingReset =
                timerRunning &&
                gAppScreen == AppScreen::FullChallengeTimer &&
                gFinishedRunTimes.empty() &&
                !gFullChallengePendingSplit;
            if (!timerRunning || allowRunningLoadingReset)
            {
                if (gAppScreen != AppScreen::FullChallengeTimer)
                {
                    gFinishedRunTimes.clear();
                    gFinishedRunBestDiffs.clear();
                    gFinishedRunBestDiffKnown.clear();
                }
                timerRunning = false;
                timerElapsed = 0.0;
                if (!gFullChallengePendingSplit)
                    gRunTimerSeconds = 0.0;
                gRunTimerLineActive = false;
            }
        }
        else if ((autoFlags & AUTO_LOADING) && !gChallengeModeActive)
        {
            bool keepRunningFullStory =
                gAppScreen == AppScreen::FullStoryTimer &&
                !gFinishedRunTimes.empty() &&
                timerRunning;
            if (!keepRunningFullStory &&
                (gAppScreen != AppScreen::FullStoryTimer || gFinishedRunTimes.empty()))
            {
                timerRunning = false;
                timerElapsed = 0.0;
                gRunTimerSeconds = 0.0;
                gRunTimerLineActive = false;
            }
        }

        DWORD startFlag = gChallengeModeActive ? AUTO_CHALLENGE_STARTED : AUTO_STARTED;
        DWORD splitFlag = gChallengeModeActive ? AUTO_CHALLENGE_SPLIT : AUTO_SPLIT;

        if ((autoFlags & startFlag) &&
            gAppScreen == AppScreen::FullChallengeTimer &&
            gFullChallengePendingSplit)
        {
            gFullChallengeReadyAfterLoading = true;
        }

        bool startAllowed = !gChallengeModeActive || (now - gChallengeEnteredAt) >= 0.10;
        bool fullStoryStartAllowed =
            gAppScreen != AppScreen::FullStoryTimer ||
            gFinishedRunTimes.empty() ||
            gFullStoryMenuAfterFinish;
        if ((autoFlags & startFlag) && startAllowed && fullStoryStartAllowed && !timerRunning)
        {
            if (gAppScreen == AppScreen::FullChallengeTimer)
            {
                gFullChallengePendingSplit = false;
                gFullChallengePendingSeconds = 0.0;
                gFullChallengeReadyAfterLoading = false;
                gFullChallengeWaitingForReadyLoading = false;
            }
            if (gAppScreen == AppScreen::FullStoryTimer)
                gFullStoryMenuAfterFinish = false;
            timerRunning = true;
            timerStart = now;
            timerElapsed = 0.0;
            gRunTimerSeconds = 0.0;
            gRunTimerLineActive = true;
            if (gAppScreen == AppScreen::SingleLevelTimer)
            {
                gSingleTimerBestDiffKnown = false;
                gSingleTimerBestDiff = 0.0;
            }
            if (gAppScreen == AppScreen::FullStoryTimer && gFinishedRunTimes.empty())
            {
                gFullStoryBestDiffKnown = false;
                gFullStoryBestDiff = 0.0;
            }
        }

        if ((autoFlags & splitFlag) && timerRunning)
        {
            timerElapsed = now - timerStart;
            if (timerElapsed < 0.0)
                timerElapsed = 0.0;
            timerElapsed = QuantizeRunTimer(timerElapsed);

            gRunTimerSeconds = timerElapsed;
            UpdateCurrentGameName(gChallengeModeActive);
            int splitDifficultyIndex = gStoryDifficultyIndex;
            bool splitBestDiffKnown = false;
            double splitBestDiff = 0.0;
            if (gAppScreen == AppScreen::FullStoryTimer)
            {
                gFullStoryMenuAfterFinish = false;
                std::string name = gCurrentGameName.empty() ? "Unknown" : gCurrentGameName;
                std::string singleMode = StoryModeNameForDifficulty("Single story level run", splitDifficultyIndex);
                int splitIndex = (int)gFinishedRunTimes.size();
                std::string fullStoryMode = StoryModeNameForDifficulty("Full TS story run", splitDifficultyIndex);
                double previousBest = FindBestTimeSeconds(fullStoryMode, FullStorySplitName(splitIndex));
                if (previousBest > 0.0)
                {
                    splitBestDiffKnown = true;
                    splitBestDiff = QuantizeRunTimer(timerElapsed) - QuantizeRunTimer(previousBest);
                }
                RecordBestTime(singleMode, name, timerElapsed);
            }
            else if (gAppScreen == AppScreen::SingleLevelTimer)
            {
                std::string name = gCurrentGameName.empty() ? "Unknown" : gCurrentGameName;
                std::string singleMode = StoryModeNameForDifficulty("Single story level run", splitDifficultyIndex);
                double previousBest = FindBestTimeSeconds(singleMode, name);
                gSingleTimerBestDiffKnown = previousBest > 0.0;
                if (gSingleTimerBestDiffKnown)
                    gSingleTimerBestDiff = QuantizeRunTimer(timerElapsed) - QuantizeRunTimer(previousBest);
                else
                    gSingleTimerBestDiff = 0.0;
                RecordBestTime(singleMode, name, timerElapsed);
            }

            if (gAppScreen == AppScreen::FullChallengeTimer)
            {
                gFullChallengePendingSplit = true;
                gFullChallengePendingSeconds = timerElapsed;
                gFullChallengeReadyAfterLoading = false;
                gFullChallengeWaitingForReadyLoading = true;
                if (gFinishedRunTimes.size() + 1 >= 32)
                    CommitFullChallengePendingSplit();
            }
            else
            {
                gFinishedRunTimes.push_back(timerElapsed);
                gFinishedRunBestDiffs.push_back(splitBestDiff);
                gFinishedRunBestDiffKnown.push_back(gAppScreen == AppScreen::FullStoryTimer && splitBestDiffKnown);
                size_t maxStoredRows = 9;
                if (gFinishedRunTimes.size() > maxStoredRows)
                {
                    gFinishedRunTimes.erase(gFinishedRunTimes.begin());
                    if (!gFinishedRunBestDiffs.empty())
                        gFinishedRunBestDiffs.erase(gFinishedRunBestDiffs.begin());
                    if (!gFinishedRunBestDiffKnown.empty())
                        gFinishedRunBestDiffKnown.erase(gFinishedRunBestDiffKnown.begin());
                }
            }

            if (gAppScreen == AppScreen::FullStoryTimer && gFinishedRunTimes.size() == 9)
            {
                double total = 0.0;
                for (double splitSeconds : gFinishedRunTimes)
                    total += splitSeconds;
                std::string fullStoryMode = StoryModeNameForDifficulty("Full TS story run", splitDifficultyIndex);
                double previousTotalBest = FindBestTimeSeconds(fullStoryMode, "Total");
                gFullStoryBestDiffKnown = previousTotalBest > 0.0;
                if (gFullStoryBestDiffKnown)
                    gFullStoryBestDiff = QuantizeRunTimer(total) - QuantizeRunTimer(previousTotalBest);
                else
                    gFullStoryBestDiff = 0.0;
                if (previousTotalBest <= 0.0 || QuantizeRunTimer(total) <= QuantizeRunTimer(previousTotalBest))
                {
                    SetBestTime(fullStoryMode, "Total", total);
                    for (int i = 0; i < 9; ++i)
                        SetBestTime(fullStoryMode, FullStorySplitName(i), gFinishedRunTimes[i]);
                }
            }

            timerRunning = false;
            gRunTimerLineActive = false;
        }

        if (timerRunning)
            gRunTimerSeconds = now - timerStart;
        else
            gRunTimerSeconds = timerElapsed;

        if (gAppScreen == AppScreen::SingleLevelTimer)
            DrawSingleTimer(hwnd);
        else
            DrawOverlay(hwnd);
        Sleep(10);
    }

    autoPollRunning.store(false);
    if (autoPollThread.joinable())
        autoPollThread.join();

    return 0;
}
