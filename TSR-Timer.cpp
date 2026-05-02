#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <metahost.h>
#include <atomic>
#include <cstdio>
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
    AUTO_ERROR = 1 << 30
};

static LARGE_INTEGER gFreq;
static double gRunTimerSeconds = 0.0;
static std::vector<double> gFinishedRunTimes;
static bool gRunTimerLineActive = false;
static bool gResetTimerRequested = false;
static int gOverlayScalePercent = 100;
static bool gOverlayScaleChanged = false;

static RECT gResetButtonRect = { 0, 0, 0, 0 };
static RECT gScaleDownButtonRect = { 0, 0, 0, 0 };
static RECT gScaleUpButtonRect = { 0, 0, 0, 0 };
static RECT gCloseButtonRect = { 0, 0, 0, 0 };

static const int kOverlayTimerOnlyW = 224;
static const int kOverlayTimerOnlyH = 302;
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

static void FormatRunTimer(double seconds, char* out, size_t outSize)
{
    if (!out || outSize == 0)
        return;

    if (seconds < 0.0)
        seconds = 0.0;

    int totalMillis = (int)(seconds * 1000.0 + 0.5);
    int millis = totalMillis % 1000;
    int totalSeconds = totalMillis / 1000;
    int secs = totalSeconds % 60;
    int mins = (totalSeconds / 60) % 60;
    int hours = totalSeconds / 3600;

    if (hours > 0)
        std::snprintf(out, outSize, "%d:%02d:%02d.%03d", hours, mins, secs, millis);
    else
        std::snprintf(out, outSize, "%d:%02d.%03d", mins, secs, millis);
}

struct AutoSplitBridge
{
    ICLRRuntimeHost* runtime = nullptr;
    wchar_t bridgePath[MAX_PATH] = {};
    bool initialized = false;
    bool available = false;
    ULONGLONG nextPollAt = 0;

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
        if (GetFileAttributesW(bridgePath) != INVALID_FILE_ATTRIBUTES) return true;

        DWORD cwdLen = GetCurrentDirectoryW(_countof(bridgePath), bridgePath);
        if (cwdLen == 0 || cwdLen >= _countof(bridgePath)) return false;
        curLen = wcslen(bridgePath);
        if (curLen > 0 && bridgePath[curLen - 1] != L'\\')
            wcscat_s(bridgePath, L"\\");
        curLen = wcslen(bridgePath);
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

        if (PointInRect(gResetButtonRect, x, y))
        {
            gFinishedRunTimes.clear();
            gRunTimerSeconds = 0.0;
            gRunTimerLineActive = false;
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
    HGDIOBJ oldFont = SelectObject(memDC, font);

    const int padL = ScaleUi(12);
    const int padR = ScaleUi(10);
    const int padT = ScaleUi(7);
    const int padB = ScaleUi(7);
    const int lineStep = ScaleUi(25);

    RECT content = rect;
    content.left += padL;
    content.right -= padR;
    content.top += padT;
    content.bottom -= padB;

    char timerText[32];
    char line[64];

    auto DrawTimerLine = [&](int rowNumber, double seconds, int rowY, COLORREF timeColor) {
        FormatRunTimer(seconds, timerText, sizeof(timerText));
        std::snprintf(line, sizeof(line), "%d  %s", rowNumber, timerText);

        RECT rTime = content;
        rTime.right = content.left + ScaleUi(88);
        rTime.top = rowY;
        SetTextColor(memDC, timeColor);
        DrawTextA(memDC, line, -1, &rTime, DT_LEFT | DT_TOP | DT_SINGLELINE);

        if (rowNumber >= 1 && rowNumber <= 9)
        {
            RECT rName = content;
            rName.left = rTime.right + ScaleUi(2);
            rName.top = rowY;
            SetTextColor(memDC, RGB(220, 220, 220));
            DrawTextA(memDC, kRouteNames[rowNumber - 1], -1, &rName, DT_RIGHT | DT_TOP | DT_SINGLELINE);
        }
    };

    double totalSeconds = 0.0;
    for (size_t i = 0; i < gFinishedRunTimes.size(); ++i)
        totalSeconds += gFinishedRunTimes[i];

    size_t maxFinishedRows = gRunTimerLineActive ? 8 : 9;
    size_t firstFinished = 0;
    if (gFinishedRunTimes.size() > maxFinishedRows)
        firstFinished = gFinishedRunTimes.size() - maxFinishedRows;

    int rowY = content.top;
    int rowNumber = 1;
    for (size_t i = firstFinished; i < gFinishedRunTimes.size(); ++i)
    {
        DrawTimerLine(rowNumber++, gFinishedRunTimes[i], rowY, RGB(255, 255, 255));
        rowY += lineStep;
    }

    if (gRunTimerLineActive || gFinishedRunTimes.empty())
    {
        DrawTimerLine(rowNumber++, gRunTimerSeconds, rowY, RGB(220, 110, 110));
        if (gRunTimerLineActive)
            totalSeconds += gRunTimerSeconds;
    }

    FormatRunTimer(totalSeconds, timerText, sizeof(timerText));
    std::snprintf(line, sizeof(line), "Total %s", timerText);
    RECT rTotal = content;
    rTotal.top = content.top + (lineStep * 9);
    SetTextColor(memDC, RGB(220, 220, 220));
    DrawTextA(memDC, line, -1, &rTotal, DT_LEFT | DT_TOP | DT_SINGLELINE);

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

        SetTextColor(memDC, RGB(255, 255, 255));
        DrawTextA(memDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    int buttonTop = content.bottom - ScaleUi(24);
    gResetButtonRect = { content.left, buttonTop, content.left + ScaleUi(58), buttonTop + ScaleUi(20) };
    gScaleDownButtonRect = { gResetButtonRect.right + ScaleUi(4), buttonTop, gResetButtonRect.right + ScaleUi(24), buttonTop + ScaleUi(20) };
    gScaleUpButtonRect = { gScaleDownButtonRect.right + ScaleUi(4), buttonTop, gScaleDownButtonRect.right + ScaleUi(24), buttonTop + ScaleUi(20) };
    gCloseButtonRect = { gScaleUpButtonRect.right + ScaleUi(4), buttonTop, gScaleUpButtonRect.right + ScaleUi(33), buttonTop + ScaleUi(20) };

    DrawButton(gResetButtonRect, "Reset", RGB(55, 55, 65), RGB(140, 140, 150));
    DrawButton(gScaleDownButtonRect, "-", RGB(45, 45, 55), RGB(125, 125, 135));
    DrawButton(gScaleUpButtonRect, "+", RGB(45, 45, 55), RGB(125, 125, 135));
    DrawButton(gCloseButtonRect, "X", RGB(90, 35, 35), RGB(220, 110, 110));

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldFont);
    DeleteObject(font);
    ReleaseDC(hwnd, hdc);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    QueryPerformanceFrequency(&gFreq);

    const char* className = "TSRTimerOverlayClass";
    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        className, "",
        WS_POPUP | WS_VISIBLE,
        100, 100, kOverlayTimerOnlyW, kOverlayTimerOnlyH,
        nullptr, nullptr, hInst, nullptr
    );
    if (!hwnd) return 0;

    SetLayeredWindowAttributes(hwnd, 0, 200, LWA_ALPHA);

    std::atomic<bool> autoPollRunning(true);
    std::atomic<DWORD> pendingAutoFlags(0);
    std::thread autoPollThread([&]() {
        AutoSplitBridge autoSplitWorker;
        while (autoPollRunning.load())
        {
            DWORD flags = autoSplitWorker.Poll(GetTickCount64());
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
                         ScaleUi(kOverlayTimerOnlyW),
                         ScaleUi(kOverlayTimerOnlyH),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }

        double now = GetTimeSeconds();
        DWORD autoFlags = pendingAutoFlags.exchange(0);

        if (gResetTimerRequested)
        {
            gResetTimerRequested = false;
            gFinishedRunTimes.clear();
            timerRunning = false;
            timerElapsed = 0.0;
            gRunTimerSeconds = 0.0;
            gRunTimerLineActive = false;
        }

        if (autoFlags & AUTO_START_CANCELED)
        {
            timerRunning = false;
            timerElapsed = 0.0;
            gRunTimerSeconds = 0.0;
            gRunTimerLineActive = false;
        }

        if (autoFlags & AUTO_STARTED)
        {
            timerRunning = true;
            timerStart = now;
            timerElapsed = 0.0;
            gRunTimerSeconds = 0.0;
            gRunTimerLineActive = true;
        }

        if ((autoFlags & AUTO_SPLIT) && timerRunning)
        {
            timerElapsed = now - timerStart;
            if (timerElapsed < 0.0)
                timerElapsed = 0.0;

            gRunTimerSeconds = timerElapsed;
            gFinishedRunTimes.push_back(timerElapsed);
            if (gFinishedRunTimes.size() > 9)
                gFinishedRunTimes.erase(gFinishedRunTimes.begin());

            timerRunning = false;
            gRunTimerLineActive = false;
        }

        if (timerRunning)
            gRunTimerSeconds = now - timerStart;
        else
            gRunTimerSeconds = timerElapsed;

        DrawOverlay(hwnd);
        Sleep(10);
    }

    autoPollRunning.store(false);
    if (autoPollThread.joinable())
        autoPollThread.join();

    return 0;
}
