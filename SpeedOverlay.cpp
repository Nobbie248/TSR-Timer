#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static bool gIsDead = false;
static double gFreeze6Until = 0.0;
static bool   gFreeze6HavePrev = false;
static uint8_t gFreeze6PrevByte = 0;
static bool    gFreeze6HavePrevB = false;
static uint8_t gFreeze6PrevByteB = 0;
static int     gFreeze6WinA = 0;
static int     gFreeze6WinB = 0;
static double gFreeze6ResetBlockUntil = 0.0;

static const char* PROCESS_NAME = "TimeSplittersRewind-Win64-Shipping.exe";
static HANDLE    hProcess   = nullptr;
static DWORD     pid        = 0;
static uintptr_t moduleBase = 0;

static uintptr_t ADDR_X = 0;
static uintptr_t ADDR_Y = 0;
static uintptr_t ADDR_Z = 0;
static uintptr_t ADDR_X_ALT[3] = { 0, 0, 0 };
static uintptr_t ADDR_Z_ALT[3] = { 0, 0, 0 };
static uintptr_t ADDR_DIR_A = 0;
static uintptr_t ADDR_DIR_B = 0;

static uintptr_t ADDR_FREEZE = 0;
static uintptr_t ADDR_FREEZE2 = 0; 
static uintptr_t ADDR_FREEZE6 = 0; 
static uintptr_t ADDR_FREEZE6B = 0; 
static bool      gFreezeStats = false;

struct Sample { float x, y, z; double t; };
static Sample hist[8];
static int histCount = 0;
static int histHead  = 0;

static void ResetHistory()
{
    histCount = 0;
    histHead  = 0;
}

static void PushSample(float x, float y, float z, double t)
{
    hist[histHead] = { x, y, z, t };
    histHead = (histHead + 1) % 8;
    if (histCount < 8) histCount++;
}

static bool GetSpeedOverTicks(int ticksBack, float& outSpeed)
{
    if (ticksBack < 1) ticksBack = 1;
    if (ticksBack > 7) ticksBack = 7;
    if (histCount < ticksBack + 1) return false;

    int newest = (histHead - 1 + 8) % 8;
    int older  = (histHead - 1 - ticksBack + 8 * 10) % 8;

    const Sample& a = hist[older];
    const Sample& b = hist[newest];

    // safety
    auto sane = [](float v) {
        return std::isfinite(v) && std::fabs(v) < 1e7f;
    };
    if (!sane(a.x) || !sane(a.y) || !sane(a.z) || !sane(b.x) || !sane(b.y) || !sane(b.z))
        return false;

    double dt = b.t - a.t;
    if (dt <= 0.0005 || dt > 0.50) return false;

    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dz = b.z - a.z;

    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    // fake speed
    if (!std::isfinite(dist) || dist > 50000.0f) return false;

    outSpeed = dist / (float)dt;
    return true;
}

// display smoothing
static float gDisplayAlpha  = 0.22f;
static bool  gHaveSmoothed  = false;
static float gSmoothedSpeed = 0.0f;

static void ResetDisplaySmoothing()
{
    gHaveSmoothed = false;
    gSmoothedSpeed = 0.0f;
}

static float SmoothDisplay(float input)
{
    if (!gHaveSmoothed)
    {
        gHaveSmoothed = true;
        gSmoothedSpeed = input;
        return input;
    }
    gSmoothedSpeed = gSmoothedSpeed + gDisplayAlpha * (input - gSmoothedSpeed);
    return gSmoothedSpeed;
}

static LARGE_INTEGER gFreq;
static double GetTimeSeconds()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)gFreq.QuadPart;
}

static double clampd(double v, double lo, double hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static bool IsZeroVec(float x, float y, float z)
{
    const float eps = 0.001f;
    return (std::fabs(x) < eps) && (std::fabs(y) < eps) && (std::fabs(z) < eps);
}

// stretch raw speed
static float StretchSpeed(float raw)
{
    const float rawTop  = 750.0f;   // rough real top speed
    const float dispTop = 1025.0f;
    return raw * (dispTop / rawTop);
}

static int ClampAndSnapToMax(float value)
{
    if (value >= 975.0f) return 1000;
    if (value < 0.0f) value = 0.0f;
    if (value > 1000.0f) value = 1000.0f;
    return (int)(value + 0.5f);
}

// process helpers
static DWORD FindProcessId(const char* name)
{
    PROCESSENTRY32 pe32{};
    pe32.dwSize = sizeof(pe32);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    if (Process32First(snap, &pe32))
    {
        do {
            if (_stricmp(pe32.szExeFile, name) == 0)
            {
                DWORD out = pe32.th32ProcessID;
                CloseHandle(snap);
                return out;
            }
        } while (Process32Next(snap, &pe32));
    }

    CloseHandle(snap);
    return 0;
}

static uintptr_t GetModuleBase(const char* moduleName, DWORD pidIn)
{
    MODULEENTRY32 me32{};
    me32.dwSize = sizeof(me32);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pidIn);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    if (Module32First(snap, &me32))
    {
        do {
            if (_stricmp(me32.szModule, moduleName) == 0)
            {
                CloseHandle(snap);
                return (uintptr_t)me32.modBaseAddr;
            }
        } while (Module32Next(snap, &me32));
    }

    CloseHandle(snap);
    return 0;
}

static bool ReadPtr(uintptr_t addr, uintptr_t& out)
{
    SIZE_T br = 0;
    return hProcess &&
           ReadProcessMemory(hProcess, (LPCVOID)addr, &out, sizeof(out), &br) &&
           br == sizeof(out) &&
           out != 0;
}

static bool ReadFloat(uintptr_t addr, float& out)
{
    SIZE_T br = 0;
    return hProcess &&
           ReadProcessMemory(hProcess, (LPCVOID)addr, &out, sizeof(out), &br) &&
           br == sizeof(out);
}

static bool ReadFloatFallback(uintptr_t& primary,
                             const uintptr_t* alts,
                             size_t altCount,
                             float& out)
{
    if (primary && ReadFloat(primary, out)) return true;
    for (size_t i = 0; i < altCount; i++)
    {
        uintptr_t a = alts[i];
        if (a && ReadFloat(a, out))
        {
            primary = a; // promote working address
            return true;
        }
    }
    return false;
}

static bool ReadInt32(uintptr_t addr, int32_t& out)
{
    SIZE_T br = 0;
    return hProcess &&
           ReadProcessMemory(hProcess, (LPCVOID)addr, &out, sizeof(out), &br) &&
           br == sizeof(out);
}

static bool ReadU8(uintptr_t addr, uint8_t& out)
{
    SIZE_T br = 0;
    return hProcess &&
           ReadProcessMemory(hProcess, (LPCVOID)addr, &out, sizeof(out), &br) &&
           br == sizeof(out);
}

static bool ResolvePointerChain(uintptr_t baseAddr,
                               const uintptr_t* offsets,
                               size_t count,
                               uintptr_t& outFinalFloatAddr)
{
    uintptr_t cur = 0;
    if (!ReadPtr(baseAddr, cur)) return false;

    for (size_t i = 0; i < count; i++)
    {
        uintptr_t next = cur + offsets[i];

        if (i == count - 1)
        {
            outFinalFloatAddr = next;
            return true;
        }

        if (!ReadPtr(next, cur)) return false;
    }
    return false;
}

static bool IsDeadAtZero(uintptr_t moduleBaseIn)
{
    if (!moduleBaseIn) return false;
    const uintptr_t offs[] = { 0x48, 0x700, 0x50, 0x2E0 };
    uintptr_t addr = 0;
    if (!ResolvePointerChain(moduleBaseIn + 0x04464638, offs, _countof(offs), addr)) return false;
    int32_t v = 0;
    if (!ReadInt32(addr, v)) return false;
    return v == 0;
}

struct SpeedPoint { float v; double t; };
static std::vector<SpeedPoint> gSpeedPts;
struct DistStep { float d3; float dxz; double t; };
static std::vector<DistStep> gDistSteps;
static double gTotalDist3D = 0.0;
static double gTotalDistXZ = 0.0;
static int    gMaxSpeedInt = 0;
static bool  gHaveFacing = false;
static float gFacingDeg  = 0.0f;
static float gTurnRateDps = 0.0f;

static void ResetExtras()
{
    gSpeedPts.clear();
    gDistSteps.clear();
    gTotalDist3D = 0.0;
    gTotalDistXZ = 0.0;
    gMaxSpeedInt = 0;

    gHaveFacing = false;
    gFacingDeg = 0.0f;
    gTurnRateDps = 0.0f;
}

static void PruneByTime(std::vector<SpeedPoint>& v, double now, double keepSeconds)
{
    size_t cut = 0;
    while (cut < v.size() && (now - v[cut].t) > keepSeconds) cut++;
    if (cut > 0) v.erase(v.begin(), v.begin() + (ptrdiff_t)cut);
}

static void PruneByTimeDist(std::vector<DistStep>& v, double now, double keepSeconds)
{
    size_t cut = 0;
    while (cut < v.size() && (now - v[cut].t) > keepSeconds) cut++;
    if (cut > 0) v.erase(v.begin(), v.begin() + (ptrdiff_t)cut);
}

static float RollingAvgSpeed(double now, double windowSeconds)
{
    double sum = 0.0;
    int n = 0;
    for (int i = (int)gSpeedPts.size() - 1; i >= 0; --i)
    {
        if ((now - gSpeedPts[(size_t)i].t) > windowSeconds) break;
        sum += gSpeedPts[(size_t)i].v;
        n++;
    }
    return (n > 0) ? (float)(sum / (double)n) : 0.0f;
}

static void RollingDist(double now, double windowSeconds, float& outD3, float& outDXZ)
{
    double s3 = 0.0, sxz = 0.0;
    for (int i = (int)gDistSteps.size() - 1; i >= 0; --i)
    {
        if ((now - gDistSteps[(size_t)i].t) > windowSeconds) break;
        s3  += gDistSteps[(size_t)i].d3;
        sxz += gDistSteps[(size_t)i].dxz;
    }
    outD3  = (float)s3;
    outDXZ = (float)sxz;
}

// window
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void DrawSparkline(HDC dc, const RECT& r, double now)
{
    HPEN penBorder = CreatePen(PS_SOLID, 1, RGB(80,80,80));
    HGDIOBJ oldPen = SelectObject(dc, penBorder);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(penBorder);

    if (gSpeedPts.size() < 2) return;

    const double window = 4.0;
    const int w = (r.right - r.left);
    const int h = (r.bottom - r.top);
    if (w <= 2 || h <= 2) return;

    HPEN penLine = CreatePen(PS_SOLID, 1, RGB(150,150,255));
    oldPen = SelectObject(dc, penLine);

    size_t start = 0;
    while (start < gSpeedPts.size() && (now - gSpeedPts[start].t) > window) start++;
    if (start >= gSpeedPts.size())
    {
        SelectObject(dc, oldPen);
        DeleteObject(penLine);
    }

    auto mapX = [&](double t) -> int {
        double a = (t - (now - window)) / window;
        if (a < 0) a = 0;
        if (a > 1) a = 1;
        return r.left + 1 + (int)((w - 2) * a + 0.5);
    };
    auto mapY = [&](float v) -> int {
        if (v < 0) v = 0;
        if (v > 1000) v = 1000;
        double a = (double)v / 1000.0;
        return r.bottom - 1 - (int)((h - 2) * a + 0.5);
    };

    MoveToEx(dc, mapX(gSpeedPts[start].t), mapY(gSpeedPts[start].v), nullptr);
    for (size_t i = start + 1; i < gSpeedPts.size(); ++i)
        LineTo(dc, mapX(gSpeedPts[i].t), mapY(gSpeedPts[i].v));

    SelectObject(dc, oldPen);
    DeleteObject(penLine);
}

// draw
static void DrawOverlay(HWND hwnd, HFONT fontBig, HFONT fontSmall,
                        int speedInt, COLORREF speedColor,
                        float x, float y, float z,
                        float avg5, float avg30, int maxSpeed,
                        float facingDeg, float turnRateDps,
                        double totalDist3D, double totalDistXZ,
                        float dist10_3d, float dist10_xz,
                        double now,
                        const char* status)
{
    RECT rect;
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;

    HDC hdc = GetDC(hwnd);

    static HDC     memDC  = nullptr;
    static HBITMAP memBmp = nullptr;
    static HBITMAP oldBmp = nullptr;
    static int     bufW   = 0;
    static int     bufH   = 0;

    if (!memDC) memDC = CreateCompatibleDC(hdc);

    if (!memBmp || bufW != w || bufH != h)
    {
        if (memBmp)
        {
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            memBmp = nullptr;
        }
        memBmp = CreateCompatibleBitmap(hdc, w, h);
        oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
        bufW = w; bufH = h;
    }

    HBRUSH bg = CreateSolidBrush(RGB(0,0,0));
    FillRect(memDC, &rect, bg);
    DeleteObject(bg);

    SetBkMode(memDC, TRANSPARENT);

    if (status)
    {
        SelectObject(memDC, fontBig);
        SetTextColor(memDC, RGB(255,255,255));
        DrawTextA(memDC, status, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        ReleaseDC(hwnd, hdc);
    }

    const int PAD_L = 14;
    const int PAD_R = 12;
    const int PAD_T = 8;
    const int PAD_B = 8;
    const int ROW1_H = 58;
    const int GAP = 10;

    RECT rTop = rect;
    rTop.top += PAD_T;
    rTop.left += PAD_L;
    rTop.right -= PAD_R;
    rTop.bottom = rect.top + ROW1_H;

    RECT rBot = rect;
    rBot.top = rTop.bottom;
    rBot.left += PAD_L;
    rBot.right -= PAD_R;
    rBot.bottom = rect.bottom - PAD_B;

    const int sparkW = 165;
    RECT rSpark = rect;
    rSpark.left = rect.right - PAD_R - sparkW;
    rSpark.right = rect.right - PAD_R;
    rSpark.top = rect.top + PAD_T;
    rSpark.bottom = rect.bottom - PAD_B;

    RECT rContentTop = rTop;
    rContentTop.right = rSpark.left - GAP;

    RECT rContentBot = rBot;
    rContentBot.right = rSpark.left - GAP;

    SelectObject(memDC, fontSmall);
    SetTextColor(memDC, RGB(255,255,255));
    RECT rLabel = rContentTop;
    rLabel.right = rLabel.left + 150;
    DrawTextA(memDC, "Player Speed:", -1, &rLabel, DT_LEFT | DT_TOP | DT_SINGLELINE);

    SelectObject(memDC, fontBig);
    SetTextColor(memDC, speedColor);
    RECT rSpeed = rContentTop;
    rSpeed.left = rLabel.right + 10;
    rSpeed.right = rContentTop.right;
    rSpeed.top -= 6;
    std::string s = std::to_string(speedInt);
    DrawTextA(memDC, s.c_str(), -1, &rSpeed, DT_LEFT | DT_TOP | DT_SINGLELINE);

    SelectObject(memDC, fontSmall);
    SetTextColor(memDC, RGB(220,220,220));
    char stats1[256];
    std::snprintf(stats1, sizeof(stats1),
                  "Avg 5s:%4d   Avg 30s:%4d   Max:%4d   Facing:%6.1f  Turn:%7.1f",
                  (int)(avg5 + 0.5f), (int)(avg30 + 0.5f), maxSpeed,
                  facingDeg, turnRateDps);
    RECT rStats1 = rContentTop;
    rStats1.top += 26;
    DrawTextA(memDC, stats1, -1, &rStats1, DT_LEFT | DT_TOP | DT_SINGLELINE);

    SelectObject(memDC, fontSmall);
    SetTextColor(memDC, RGB(255,255,255));

    char line2a[256];
    std::snprintf(line2a, sizeof(line2a),
                  "X:%7d  Y:%7d  Z:%7d",
                  (int)x, (int)y, (int)z);

    RECT rLine2a = rContentBot;
    rLine2a.top += 4;
    DrawTextA(memDC, line2a, -1, &rLine2a, DT_LEFT | DT_TOP | DT_SINGLELINE);

    char line2c[256];
    std::snprintf(line2c, sizeof(line2c),
                  "Dist 10s: %6.1f    Total: %7.1f", 
                  dist10_3d, (float)totalDist3D);

    RECT rLine2c = rContentBot;
    rLine2c.top += 28;
    DrawTextA(memDC, line2c, -1, &rLine2c, DT_LEFT | DT_TOP | DT_SINGLELINE);

    char line2d[256];
    std::snprintf(line2d, sizeof(line2d),
                  "TSR Speed Profiler By Nobbie                                           F10 to Exit");

    RECT rLine2d = rContentBot;
    rLine2d.top += 64;
    DrawTextA(memDC, line2d, -1, &rLine2d, DT_LEFT | DT_TOP | DT_SINGLELINE);

    DrawSparkline(memDC, rSpark, now);

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    ReleaseDC(hwnd, hdc);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    QueryPerformanceFrequency(&gFreq);

    const char* CLASS_NAME = "SpeedOverlayClass";
    WNDCLASS wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        CLASS_NAME, "",
        WS_POPUP | WS_VISIBLE,
        100, 100, 680, 150,
        nullptr, nullptr, hInst, nullptr
    );
    if (!hwnd) return 0;

    SetLayeredWindowAttributes(hwnd, 0, 200, LWA_ALPHA);

    HFONT fontBig = CreateFontA(
        30, 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        "Arial"
    );

    HFONT fontSmall = CreateFontA(
        18, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        "Arial"
    );
    // x y z floats
    const uintptr_t vecOffsets[] = { 0x8, 0x110, 0x280, 0x3E8, 0xB0 };
    bool okVec = false;

    const uintptr_t xAlt1Offsets[] = { 0x30, 0x2A0, 0x280, 0x2E8, 0xB4 };
    const uintptr_t xAlt2Offsets[] = { 0x0, 0x240, 0x730, 0x20, 0x3B8, 0xB4 };
    const uintptr_t xAlt3Offsets[] = { 0x30, 0x5C8, 0x290, 0xD0, 0x0, 0x3E8, 0xB4 };
    const uintptr_t zAlt1Offsets[] = { 0x8, 0x160, 0x0, 0x280, 0x3E8, 0xB0 };
    const uintptr_t zAlt2Offsets[] = { 0x10, 0x240, 0x518, 0x40, 0x8, 0x3B8, 0xB0 };
    const uintptr_t zAlt3Offsets[] = { 0x8, 0x110, 0x280, 0x3E8, 0xB0 };

    // direction pointer
    const uintptr_t dirOffsets[] = { 0x0, 0x8, 0x790 };

    // freezy
    const uintptr_t freeze2Offsets[] = { 0x1E8, 0xC0, 0x480, 0xBF0, 0x28, 0x0, 0xB44 };
    bool okFreeze2 = false;

    // freeze 6
    const uintptr_t freeze6Offsets[] = { 0x38, 0x194 };
    bool okFreeze6 = false;
    // freeze 6b
    const uintptr_t freeze6bOffsets[] = { 0x8 };
    bool okFreeze6B = false;
    bool okDir = false;

    double lastTime = GetTimeSeconds();
    ULONGLONG nextResolveAt = 0;

    // last position for distance
    bool  haveLastPos = false;
    float lastX = 0, lastY = 0, lastZ = 0;

    // only reset totals if xyz is really gone
    double xyzBadSince = -1.0;
    float  lastGoodX = 0, lastGoodY = 0, lastGoodZ = 0;
    bool   haveLastGood = false;

    // cache so brief glitches don't wipe stats
    int      lastSpeedInt   = 0;
    float    lastAvg5       = 0.0f;
    float    lastAvg30      = 0.0f;
    float    lastFacingDeg  = 0.0f;
    float    lastTurnRate   = 0.0f;
    float    lastDist10_3d  = 0.0f;
    float    lastDist10_xz  = 0.0f;

    MSG msg{};

    while (true)
    {
        if (GetAsyncKeyState(VK_F10) & 0x8000)
        {
            DestroyWindow(hwnd);
            break;
        }

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!hProcess)
        {
            pid = FindProcessId(PROCESS_NAME);
            if (!pid)
            {
                DrawOverlay(hwnd, fontBig, fontSmall, 0, RGB(255,255,255),
                            0,0,0, 0,0,0,
                            0,0,
                            0,0, 0,0, GetTimeSeconds(),
                            "WAITING FOR GAME");
                Sleep(150);
                continue;
            }

            hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
            if (!hProcess)
            {
                DrawOverlay(hwnd, fontBig, fontSmall, 0, RGB(255,255,255),
                            0,0,0, 0,0,0,
                            0,0,
                            0,0, 0,0, GetTimeSeconds(),
                            "NO PERMISSION");
                Sleep(150);
                continue;
            }

            moduleBase = 0;
            okVec = false;
            okDir = false;
            ADDR_X = ADDR_Y = ADDR_Z = 0;
            ADDR_X_ALT[0] = ADDR_X_ALT[1] = ADDR_X_ALT[2] = 0;
            ADDR_Z_ALT[0] = ADDR_Z_ALT[1] = ADDR_Z_ALT[2] = 0;
            ADDR_DIR_A = ADDR_DIR_B = 0;
            ADDR_FREEZE = 0;
            ADDR_FREEZE2 = 0;
            ADDR_FREEZE6 = 0;
            ADDR_FREEZE6 = 0;
            okFreeze2 = false;
            okFreeze6 = false;
            okFreeze6 = false;
            ADDR_FREEZE6B = 0;
            okFreeze6B = false;
            gFreezeStats = false;

            ResetHistory();
            ResetDisplaySmoothing();
            ResetExtras();

            haveLastPos = false;
            xyzBadSince = -1.0;
            haveLastGood = false;

            lastSpeedInt = 0;
            lastAvg5 = lastAvg30 = 0.0f;
            lastFacingDeg = lastTurnRate = 0.0f;
            lastDist10_3d = lastDist10_xz = 0.0f;
        }

        // module base
        if (!moduleBase)
        {
            moduleBase = GetModuleBase(PROCESS_NAME, pid);
            if (!moduleBase)
            {
                DrawOverlay(hwnd, fontBig, fontSmall, 0, RGB(255,255,255),
                            0,0,0, 0,0,0,
                            0,0,
                            0,0, 0,0, GetTimeSeconds(),
                            "MODULE FAIL");
                Sleep(150);
                continue;
            }
            nextResolveAt = 0;
            okVec = false;
            okDir = false;
            ADDR_FREEZE = moduleBase + 0x4487154;
            ResetHistory();
            ResetDisplaySmoothing();
            haveLastPos = false;
        }

        bool deadNow = IsDeadAtZero(moduleBase);
        ULONGLONG nowMs = GetTickCount64();
        if (nowMs >= nextResolveAt || !okVec || !okDir)
        {
            nextResolveAt = nowMs + 40;
            {
                {
                    uintptr_t out = 0;
                    if (ResolvePointerChain(moduleBase + 0x044747C0, xAlt1Offsets, _countof(xAlt1Offsets), out)) ADDR_X_ALT[0] = out;
                    if (ResolvePointerChain(moduleBase + 0x043FF338, xAlt2Offsets, _countof(xAlt2Offsets), out)) ADDR_X_ALT[1] = out;
                    if (ResolvePointerChain(moduleBase + 0x044747C0, xAlt3Offsets, _countof(xAlt3Offsets), out)) ADDR_X_ALT[2] = out;

                    if (ResolvePointerChain(moduleBase + 0x04486308, zAlt1Offsets, _countof(zAlt1Offsets), out)) ADDR_Z_ALT[0] = out;
                    if (ResolvePointerChain(moduleBase + 0x043FF338, zAlt2Offsets, _countof(zAlt2Offsets), out)) ADDR_Z_ALT[1] = out;
                    if (ResolvePointerChain(moduleBase + 0x043FF338, zAlt3Offsets, _countof(zAlt3Offsets), out)) ADDR_Z_ALT[2] = out;
                }

                uintptr_t baseFloatAddr = 0;
                bool newOk = ResolvePointerChain(moduleBase + 0x043FF338, vecOffsets, _countof(vecOffsets), baseFloatAddr);

                if (newOk)
                {
                    uintptr_t newZ = baseFloatAddr;
                    uintptr_t newX = baseFloatAddr + 0x4;
                    uintptr_t newY = baseFloatAddr + 0x8;

                    if (!okVec || newX != ADDR_X || newY != ADDR_Y || newZ != ADDR_Z)
                    {
                        ADDR_Z = newZ;
                        ADDR_X = newX;
                        ADDR_Y = newY;
                        okVec = true;

                        // only reset stats on death at 0.
                        if (deadNow)
                        {
                            ResetHistory();
                            ResetDisplaySmoothing();
                            haveLastPos = false;
                        }
                    }
                }
                else
                {
                    // keep last stats unless dead at 0.
                    if (okVec)
                    {
                        ADDR_X = ADDR_Y = ADDR_Z = 0;
                        okVec = false;

                        if (deadNow)
                        {
                            ResetHistory();
                            ResetDisplaySmoothing();
                            ResetExtras();

                            haveLastPos = false;
                            xyzBadSince = -1.0;
                            haveLastGood = false;

                            lastSpeedInt = 0;
                            lastAvg5 = lastAvg30 = 0.0f;
                            lastFacingDeg = lastTurnRate = 0.0f;
                            lastDist10_3d = lastDist10_xz = 0.0f;
                        }
                    }
                    else
                    {
                        okVec = false;
                    }
                }
            }
            {
                uintptr_t dirA = 0;
                bool newOk = ResolvePointerChain(moduleBase + 0x043FF338, dirOffsets, _countof(dirOffsets), dirA);

                if (newOk)
                {
                    uintptr_t newA = dirA;
                    uintptr_t newB = dirA + 0x4;

                    if (!okDir || newA != ADDR_DIR_A || newB != ADDR_DIR_B)
                    {
                        ADDR_DIR_A = newA;
                        ADDR_DIR_B = newB;
                        okDir = true;
                        gHaveFacing = false;
                        gFacingDeg = 0.0f;
                        gTurnRateDps = 0.0f;
                    }
                }
                else
                {
                    okDir = false;
                }
            }

            // freeze2
            {
                uintptr_t freezeAddr = 0;
                bool newOk = ResolvePointerChain(moduleBase + 0x04494050, freeze2Offsets, _countof(freeze2Offsets), freezeAddr);

                if (newOk)
                {
                    if (!okFreeze2 || freezeAddr != ADDR_FREEZE2)
                    {
                        ADDR_FREEZE2 = freezeAddr;
                        okFreeze2 = true;
                    }
                }
                else
                {
                    okFreeze2 = false;
                }
            }
            // freeze6
            {
                uintptr_t freezeAddr = 0;
                bool newOk = ResolvePointerChain(moduleBase + 0x041FD538, freeze6Offsets, _countof(freeze6Offsets), freezeAddr);

                if (newOk)
                {
                    if (!okFreeze6 || freezeAddr != ADDR_FREEZE6)
                    {
                        ADDR_FREEZE6 = freezeAddr;
                        okFreeze6 = true;
                    }
                }
                else
                {
                    okFreeze6 = false;
                }
            }
            // freeze 6b
            {
                uintptr_t freezeAddr = 0;
                bool newOk = ResolvePointerChain(moduleBase + 0x04350418, freeze6bOffsets, _countof(freeze6bOffsets), freezeAddr);

                if (newOk)
                {
                    if (!okFreeze6B || freezeAddr != ADDR_FREEZE6B)
                    {
                        ADDR_FREEZE6B = freezeAddr;
                        okFreeze6B = true;
                    }
                }
                else
                {
                    okFreeze6B = false;
                }
            }


        }

        double now = GetTimeSeconds();
        double dt = clampd(now - lastTime, 0.001, 0.100);
        lastTime = now;
// reset stats only when dead
static bool sWasDead = false;
bool justDied = deadNow && !sWasDead;
sWasDead = deadNow;

if (justDied)
{
    ResetHistory();
    ResetDisplaySmoothing();
    ResetExtras();
    haveLastPos = false;
    haveLastGood = false;

    lastSpeedInt = 0;
    lastAvg5 = lastAvg30 = 0.0f;
    lastFacingDeg = lastTurnRate = 0.0f;
    lastDist10_3d = lastDist10_xz = 0.0f;

    xyzBadSince = -1.0;
}


        // more freezy
        if (moduleBase)
        {
            ADDR_FREEZE = moduleBase + 0x4487154;
            int32_t pv = 0;
            if (ReadInt32(ADDR_FREEZE, pv))
                gFreezeStats = (pv == 3);
            else
                gFreezeStats = false;

            int32_t pv2 = 0;
            bool freeze2 = false;
            if (okFreeze2 && ADDR_FREEZE2 && ReadInt32(ADDR_FREEZE2, pv2))
                freeze2 = (pv2 == 3 || pv2 == 11);

            gFreezeStats = gFreezeStats || freeze2;

            static double gLastDeadAt = -1e9;

            // freeze 3 dead
            const uintptr_t freeze3Offsets[] = { 0x48, 0x700, 0x50, 0x2E0 };
            uintptr_t freezeAddr3 = 0;
            bool freeze3 = false;
            if (ResolvePointerChain(moduleBase + 0x04464638, freeze3Offsets, _countof(freeze3Offsets), freezeAddr3)) {
                int pv3 = 0;
                ReadProcessMemory(hProcess, (LPCVOID)freezeAddr3, &pv3, sizeof(pv3), nullptr);
                freeze3 = (pv3 == 0);
            }
            if (freeze3) {
                gLastDeadAt = GetTimeSeconds();
                gFreeze6Until = 0.0;
                gFreeze6HavePrev = false;
                gFreeze6PrevByte = 0;
                gFreeze6HavePrevB = false;
                gFreeze6PrevByteB = 0;
                gFreeze6ResetBlockUntil = 0.0;
            }
            gFreezeStats = gFreezeStats || freeze3;

            if (okFreeze6 && okFreeze6B && ADDR_FREEZE6 && ADDR_FREEZE6B)
            {
                const double tNow = GetTimeSeconds();
                const double kDeadBlock = 3.0;

                // if currently dead or recently dead check
                if (freeze3 || (tNow - gLastDeadAt) < kDeadBlock)
                {
                    gFreeze6HavePrev = false;
                    gFreeze6HavePrevB = false;
                    gFreeze6WinA = 0;
                    gFreeze6WinB = 0;
                }
                else
                {
                    uint8_t a = 0, b = 0;
                    bool okA = ReadU8(ADDR_FREEZE6, a);
                    bool okB = ReadU8(ADDR_FREEZE6B, b);

                    if (okA && okB)
                    {
                        if (!gFreeze6HavePrev || !gFreeze6HavePrevB)
                        {
                            gFreeze6PrevByte = a;
                            gFreeze6PrevByteB = b;
                            gFreeze6HavePrev = true;
                            gFreeze6HavePrevB = true;
                        }
                        else
                        {
                            const bool changedA = (a != gFreeze6PrevByte);
                            const bool changedB = (b != gFreeze6PrevByteB);

                            if (changedA) gFreeze6PrevByte = a;
                            if (changedB) gFreeze6PrevByteB = b;

                            // ticks window between freeze 6 check
                            if (changedA) gFreeze6WinA = 6;
                            else if (gFreeze6WinA > 0) gFreeze6WinA--;

                            if (changedB) gFreeze6WinB = 6;
                            else if (gFreeze6WinB > 0) gFreeze6WinB--;

                            if (gFreeze6WinA > 0 && gFreeze6WinB > 0 && tNow >= gFreeze6Until)
                            {
                                gFreeze6Until = tNow + 3.0;
                                gFreeze6ResetBlockUntil = tNow + 3.0;
                                gFreeze6WinA = 0;
                                gFreeze6WinA = 0;
                                gFreeze6WinB = 0;
                            }
                        }
                    }
                }

                if (tNow < gFreeze6Until)
                    gFreezeStats = true;
            }
}
        else
        {
            gFreezeStats = false;
        }

        if (gFreezeStats)
        {
            // freeze states
            COLORREF speedColor =
                (lastSpeedInt < 400) ? RGB(255, 0, 0) :
                (lastSpeedInt < 850) ? RGB(255, 255, 0) :
                                       RGB(0, 255, 0);

            DrawOverlay(hwnd, fontBig, fontSmall,
                        lastSpeedInt, speedColor,
                        haveLastGood ? lastGoodX : 0.0f,
                        haveLastGood ? lastGoodY : 0.0f,
                        haveLastGood ? lastGoodZ : 0.0f,
                        lastAvg5, lastAvg30, gMaxSpeedInt,
                        lastFacingDeg, lastTurnRate,
                        gTotalDist3D, gTotalDistXZ,
                        lastDist10_3d, lastDist10_xz,
                        now, nullptr);
         
            Sleep(5);
            continue;
        }

        float x = 0.0f, y = 0.0f, z = 0.0f;

        if (!okVec)
        {
            float tx = 0.0f, ty = 0.0f, tz = 0.0f;
            bool okX = ReadFloatFallback(ADDR_X, ADDR_X_ALT, _countof(ADDR_X_ALT), tx);
            bool okZ = ReadFloatFallback(ADDR_Z, ADDR_Z_ALT, _countof(ADDR_Z_ALT), tz);
            bool okY = (ADDR_Y && ReadFloat(ADDR_Y, ty));
            if (!okY) ty = haveLastGood ? lastGoodY : 0.0f;

            if (okX && okZ)
            {
                okVec = true;
                x = tx; y = ty; z = tz;
            }
            else
            {
                if (xyzBadSince < 0.0) xyzBadSince = now;

                COLORREF speedColor =
                    (lastSpeedInt < 400) ? RGB(255, 0, 0) :
                    (lastSpeedInt < 850) ? RGB(255, 255, 0) :
                                           RGB(0, 255, 0);

                DrawOverlay(hwnd, fontBig, fontSmall,
                            lastSpeedInt, speedColor,
                            haveLastGood ? lastGoodX : 0.0f,
                            haveLastGood ? lastGoodY : 0.0f,
                            haveLastGood ? lastGoodZ : 0.0f,
                            lastAvg5, lastAvg30, gMaxSpeedInt,
                            lastFacingDeg, lastTurnRate,
                            gTotalDist3D, gTotalDistXZ,
                            lastDist10_3d, lastDist10_xz,
                            now,
                            "Waiting For Pointers...");
                Sleep(40);
                continue;
            }
        }

        // read coords
        bool okX = ReadFloatFallback(ADDR_X, ADDR_X_ALT, _countof(ADDR_X_ALT), x);
        bool okY = (ADDR_Y && ReadFloat(ADDR_Y, y));
        if (!okY) { y = haveLastGood ? lastGoodY : 0.0f; okY = true; }
        bool okZ = ReadFloatFallback(ADDR_Z, ADDR_Z_ALT, _countof(ADDR_Z_ALT), z);
        bool okReadXYZ = okX && okY && okZ;
        //fall back
        auto saneF = [](float v) {
            return std::isfinite(v) && std::fabs(v) < 1e7f;
        };
        if (okReadXYZ && (!saneF(x) || !saneF(y) || !saneF(z)))
            okReadXYZ = false;
        bool xyzIsZero = okReadXYZ && IsZeroVec(x, y, z);

        if (!okReadXYZ || xyzIsZero)
        {
            DWORD exitCode = 0;
            if (!GetExitCodeProcess(hProcess, &exitCode) || exitCode != STILL_ACTIVE)
            {
                // game closed
                CloseHandle(hProcess);
                hProcess = nullptr;
                pid = 0;
                moduleBase = 0;
                Sleep(100);
                continue;
            }

            if (xyzBadSince < 0.0) xyzBadSince = now;
            if (haveLastGood)
            {
                x = lastGoodX; y = lastGoodY; z = lastGoodZ;
            }

            nextResolveAt = 0;
            okVec = false;

            COLORREF speedColor =
                (lastSpeedInt < 400) ? RGB(255, 0, 0) :
                (lastSpeedInt < 850) ? RGB(255, 255, 0) :
                                       RGB(0, 255, 0);

            DrawOverlay(hwnd, fontBig, fontSmall,
                        lastSpeedInt, speedColor,
                        x, y, z,
                        lastAvg5, lastAvg30, gMaxSpeedInt,
                        lastFacingDeg, lastTurnRate,
                        gTotalDist3D, gTotalDistXZ,
                        lastDist10_3d, lastDist10_xz,
                        now,
                        nullptr);

            Sleep(5);
            continue;
        }

        // xyz check for big jumps
        if (haveLastGood)
        {
            float dxg = x - lastGoodX;
            float dyg = y - lastGoodY;
            float dzg = z - lastGoodZ;
            float jump = std::sqrt(dxg*dxg + dyg*dyg + dzg*dzg);

            if (std::isfinite(jump) && jump > 5000.0f)
            {
                ResetHistory();
                ResetDisplaySmoothing();
                ResetExtras();

                haveLastPos = false;

                lastSpeedInt = 0;
                lastAvg5 = lastAvg30 = 0.0f;
                lastFacingDeg = lastTurnRate = 0.0f;
                lastDist10_3d = lastDist10_xz = 0.0f;
            }
        }

        xyzBadSince = -1.0;
        lastGoodX = x; lastGoodY = y; lastGoodZ = z;
        haveLastGood = true;

        PushSample(x, y, z, now);

        float rawSpeed = 0.0f;
        if (!GetSpeedOverTicks(6, rawSpeed))
            rawSpeed = 0.0f;

        float stretched = StretchSpeed(rawSpeed);
        float smoothed  = SmoothDisplay(stretched);

        int speedInt = ClampAndSnapToMax(smoothed);

        if (speedInt > gMaxSpeedInt) gMaxSpeedInt = speedInt;
        gSpeedPts.push_back({ (float)speedInt, now });
        PruneByTime(gSpeedPts, now, 35.0);
        float avg5  = RollingAvgSpeed(now, 5.0);
        float avg30 = RollingAvgSpeed(now, 30.0);

        if (!haveLastPos)
        {
            haveLastPos = true;
            lastX = x; lastY = y; lastZ = z;
        }
        else
        {
            float dxp = x - lastX;
            float dyp = y - lastY;
            float dzp = z - lastZ;

            float d3  = std::sqrt(dxp*dxp + dyp*dyp + dzp*dzp);
            float dxz = std::sqrt(dxp*dxp + dzp*dzp);

            if (d3 < 50000.0f)
            {
                gTotalDist3D += (double)d3;
                gTotalDistXZ += (double)dxz;

                gDistSteps.push_back({ d3, dxz, now });
                PruneByTimeDist(gDistSteps, now, 12.0);
            }

            lastX = x; lastY = y; lastZ = z;
        }

        float dist10_3d = 0.0f, dist10_xz = 0.0f;
        RollingDist(now, 10.0, dist10_3d, dist10_xz);

        float facingDeg = gFacingDeg;
        float turnRate  = gTurnRateDps;

        if (okDir)
        {
            float a = 0.0f, b = 0.0f;
            if (ReadFloat(ADDR_DIR_A, a) && ReadFloat(ADDR_DIR_B, b))
            {
                float newFacing = (float)(std::atan2((double)b, (double)a) * (180.0 / 3.141592653589793));
                if (newFacing < 0.0f) newFacing += 360.0f;

                if (!gHaveFacing)
                {
                    gHaveFacing = true;
                    gFacingDeg = newFacing;
                    gTurnRateDps = 0.0f;
                }
                else
                {
                    float delta = newFacing - gFacingDeg;
                    while (delta <= -180.0f) delta += 360.0f;
                    while (delta >   180.0f) delta -= 360.0f;

                    gTurnRateDps = delta / (float)dt;
                    gFacingDeg = newFacing;
                }

                facingDeg = gFacingDeg;
                turnRate  = gTurnRateDps;
            }
            else
            {
                okDir = false;
            }
        }

        COLORREF speedColor =
            (speedInt < 400) ? RGB(255, 0, 0) :
            (speedInt < 850) ? RGB(255, 255, 0) :
                               RGB(0, 255, 0);

        DrawOverlay(hwnd, fontBig, fontSmall,
                    speedInt, speedColor,
                    x, y, z,
                    avg5, avg30, gMaxSpeedInt,
                    facingDeg, turnRate,
                    gTotalDist3D, gTotalDistXZ,
                    dist10_3d, dist10_xz,
                    now,
                    nullptr);

        // cache last good computed so no wipe
        lastSpeedInt  = speedInt;
        lastAvg5      = avg5;
        lastAvg30     = avg30;
        lastFacingDeg = facingDeg;
        lastTurnRate  = turnRate;
        lastDist10_3d = dist10_3d;
        lastDist10_xz = dist10_xz;

        Sleep(5);
    }

    DeleteObject(fontBig);
    DeleteObject(fontSmall);
    if (hProcess) CloseHandle(hProcess);
    return 0;
}
