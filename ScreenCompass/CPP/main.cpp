#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#include <atomic>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Sensors.h>
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace Windows::Devices::Sensors;
using namespace Gdiplus;

// Logging - output visible in DebugView or Visual Studio debugger
namespace Log
{
    void Info(const wchar_t* msg)
    {
        wchar_t buf[512];
        swprintf_s(buf, L"[ScreenCompass] INFO: %s\n", msg);
        OutputDebugStringW(buf);
    }

    void Warn(const wchar_t* msg)
    {
        wchar_t buf[512];
        swprintf_s(buf, L"[ScreenCompass] WARN: %s\n", msg);
        OutputDebugStringW(buf);
    }

    void Error(const wchar_t* msg)
    {
        wchar_t buf[512];
        swprintf_s(buf, L"[ScreenCompass] ERROR: %s\n", msg);
        OutputDebugStringW(buf);
    }

    void Debug(const wchar_t* msg)
    {
        wchar_t buf[512];
        swprintf_s(buf, L"[ScreenCompass] DEBUG: %s\n", msg);
        OutputDebugStringW(buf);
    }
}

// Globals
std::atomic<HWND> g_hwnd{nullptr};
NOTIFYICONDATAW g_nid = {};
SimpleOrientationSensor g_sensor = nullptr;
winrt::event_token g_sensorToken{};
std::atomic<bool> g_locked{false};
std::atomic<DWORD> g_currentOrientation{DMDO_DEFAULT};
ULONG_PTR g_gdiplusToken = 0;

HICON g_iconLocked = nullptr;
HICON g_iconUnlocked = nullptr;
HICON g_appIcon = nullptr;
Image* g_bgImage = nullptr;

// Track which hotkeys registered successfully
bool g_hotkeyRegistered[4] = {false, false, false, false};

const wchar_t* CLASS_NAME = L"ScreenCompassWindowClass";
const wchar_t* WINDOW_TITLE = L"HALRAD ScreenCompass - Always the right angle.";

// Cursor restore after rotation (Windows resets cursor async after display change)
POINT g_cursorRestore = {0, 0};

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void CreateTrayIcon(HWND hwnd);
void RemoveTrayIcon();
void UpdateTrayIcon();
void ShowTrayMenu(HWND hwnd);
void RotateDisplay(DWORD orientation);
void RotateDisplayByName(const wchar_t* deviceName, DWORD orientation);
void SetOrientation(DWORD orientation);
void SetOrientationAtCursor(DWORD orientation);
void Toggle90();
void AutoOrient();
void RegisterHotkeys(HWND hwnd);
void UnregisterHotkeys(HWND hwnd);
DWORD SensorOrientationToDisplay(SimpleOrientation orientation);
DWORD GetCurrentDisplayOrientation();
DWORD GetDisplayOrientationByName(const wchar_t* deviceName);
std::wstring GetMonitorAtCursor();

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    Log::Info(L"Starting ScreenCompass");
    winrt::init_apartment();

    // Check for -m or /m to start minimized
    bool startMinimized = false;
    if (pCmdLine && pCmdLine[0])
    {
        std::wstring cmdLine(pCmdLine);
        if (cmdLine.find(L"-m") != std::wstring::npos ||
            cmdLine.find(L"/m") != std::wstring::npos ||
            cmdLine.find(L"-minimized") != std::wstring::npos ||
            cmdLine.find(L"/minimized") != std::wstring::npos)
        {
            startMinimized = true;
        }
    }

    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Ok)
    {
        Log::Error(L"GDI+ initialization failed");
        winrt::uninit_apartment();
        return 1;
    }

    // Load icons from embedded resources
    g_iconLocked = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LOCKED));
    g_iconUnlocked = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_UNLOCKED));
    g_appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));

    // Load background image from embedded resource
    HRSRC hRes = FindResource(hInstance, MAKEINTRESOURCE(IDR_BACKGROUND), RT_RCDATA);
    if (hRes)
    {
        HGLOBAL hData = LoadResource(hInstance, hRes);
        if (hData)
        {
            void* pData = LockResource(hData);
            DWORD size = SizeofResource(hInstance, hRes);
            if (pData && size)
            {
                IStream* pStream = nullptr;
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
                if (hMem)
                {
                    void* pMem = GlobalLock(hMem);
                    if (pMem)
                    {
                        memcpy(pMem, pData, size);
                        GlobalUnlock(hMem);
                        HRESULT hr = CreateStreamOnHGlobal(hMem, TRUE, &pStream);
                        if (SUCCEEDED(hr) && pStream)
                        {
                            g_bgImage = Image::FromStream(pStream);
                            pStream->Release();
                            // Validate image loaded successfully
                            if (g_bgImage && g_bgImage->GetLastStatus() != Ok)
                            {
                                delete g_bgImage;
                                g_bgImage = nullptr;
                            }
                        }
                        else
                        {
                            // Stream creation failed - we still own hMem
                            GlobalFree(hMem);
                        }
                    }
                    else
                    {
                        GlobalFree(hMem);
                    }
                }
            }
        }
    }

    // Get current display orientation
    g_currentOrientation = GetCurrentDisplayOrientation();

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // We'll paint our own background
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = g_appIcon;
    wc.hIconSm = g_appIcon;
    if (!RegisterClassExW(&wc))
    {
        Log::Error(L"Failed to register window class");
        GdiplusShutdown(g_gdiplusToken);
        winrt::uninit_apartment();
        return 1;
    }

    // Create window
    g_hwnd = CreateWindowExW(
        0,
        CLASS_NAME, WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 440,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hwnd)
    {
        Log::Error(L"Failed to create window");
        GdiplusShutdown(g_gdiplusToken);
        winrt::uninit_apartment();
        return 1;
    }

    // Set window icon
    if (g_appIcon)
    {
        SendMessage(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
        SendMessage(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_appIcon);
    }

    // Initialize sensor
    g_sensor = SimpleOrientationSensor::GetDefault();
    if (g_sensor)
    {
        Log::Info(L"Orientation sensor found");
        g_sensorToken = g_sensor.OrientationChanged([](SimpleOrientationSensor const&,
            SimpleOrientationSensorOrientationChangedEventArgs const& args)
        {
            if (!g_locked)
            {
                DWORD newOrientation = SensorOrientationToDisplay(args.Orientation());
                HWND hwnd = g_hwnd.load();
                // Verify window is still valid before posting
                if (hwnd && IsWindow(hwnd) && newOrientation != g_currentOrientation.load())
                {
                    // Post to UI thread - don't call RotateDisplay from sensor thread
                    PostMessage(hwnd, WM_SENSOR_ORIENT, static_cast<WPARAM>(newOrientation), 0);
                }
            }
        });
    }
    else
    {
        Log::Info(L"No orientation sensor found - manual rotation only");
    }

    CreateTrayIcon(g_hwnd);
    RegisterHotkeys(g_hwnd);

    // Show window on startup (unless -m flag)
    if (!startMinimized)
    {
        ShowWindow(g_hwnd, nCmdShow);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Log::Info(L"Shutting down");
    UnregisterHotkeys(g_hwnd);
    RemoveTrayIcon();

    // Release WinRT sensor before uninit_apartment
    if (g_sensor)
    {
        if (g_sensorToken)
        {
            g_sensor.OrientationChanged(g_sensorToken);
            g_sensorToken = {};
        }
        g_sensor = nullptr;
    }

    // Cleanup
    delete g_bgImage;
    // Icons loaded via LoadIcon() are shared system resources - do not call DestroyIcon
    GdiplusShutdown(g_gdiplusToken);
    winrt::uninit_apartment();

    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        // Create buffer for double-buffering
        HDC memDC = CreateCompatibleDC(hdc);
        if (!memDC)
        {
            EndPaint(hwnd, &ps);
            return 0;
        }

        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        if (!memBitmap)
        {
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            return 0;
        }

        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        // Fill background
        HBRUSH bgBrush = CreateSolidBrush(RGB(76, 175, 80)); // Green like the icon
        FillRect(memDC, &rc, bgBrush);
        DeleteObject(bgBrush);

        // Draw background image centered
        if (g_bgImage)
        {
            Graphics graphics(memDC);
            int imgW = g_bgImage->GetWidth();
            int imgH = g_bgImage->GetHeight();
            int x = (rc.right - imgW) / 2;
            int y = (rc.bottom - imgH) / 2;
            graphics.DrawImage(g_bgImage, x, y, imgW, imgH);
        }

        // Draw orientation indicator
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(255, 255, 255));

        const wchar_t* arrow = L"\x2191"; // Up arrow - always points to top of screen
        HFONT font = CreateFontW(72, 0, 0, 0,
            FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, L"Segoe UI Symbol");
        HFONT oldFont = (HFONT)SelectObject(memDC, font);

        DrawTextW(memDC, arrow, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(memDC, oldFont);
        DeleteObject(font);

        // Copy to screen
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_USER + 2: // Update UI
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_SENSOR_ORIENT: // Sensor orientation change (posted from sensor thread)
    {
        DWORD newOrientation = static_cast<DWORD>(wParam);
        // Validate orientation is one of the four valid values
        if (newOrientation != DMDO_DEFAULT && newOrientation != DMDO_90 &&
            newOrientation != DMDO_180 && newOrientation != DMDO_270)
        {
            Log::Warn(L"Invalid orientation value from sensor");
            return 0;
        }
        if (newOrientation != g_currentOrientation.load())
        {
            Log::Debug(L"Sensor orientation change");
            g_currentOrientation = newOrientation;
            RotateDisplay(newOrientation);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_USER + 3: // Restore cursor position after rotation
        SetCursorPos(g_cursorRestore.x, g_cursorRestore.y);
        return 0;

    case WM_LBUTTONDOWN:
        // Allow dragging by clicking anywhere
        ReleaseCapture();
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;

    case WM_LBUTTONDBLCLK:
        // Double-click to rotate
        Toggle90();
        return 0;

    case WM_RBUTTONUP:
        // Right-click shows same menu as tray
        ShowTrayMenu(hwnd);
        return 0;

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_LBUTTONUP)
        {
            // Toggle lock on click
            g_locked = !g_locked;
            Log::Info(g_locked ? L"Rotation locked" : L"Rotation unlocked (auto)");
            UpdateTrayIcon();
            if (!g_locked && g_sensor)
            {
                AutoOrient();
            }
        }
        else if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            ShowTrayMenu(hwnd);
        }
        return 0;

    case WM_HOTKEY:
        // Follow mouse - rotate whichever monitor the cursor is on
        switch (wParam)
        {
        case IDH_ROTATE_UP:
            SetOrientationAtCursor(DMDO_DEFAULT);
            break;
        case IDH_ROTATE_DOWN:
            SetOrientationAtCursor(DMDO_180);
            break;
        case IDH_ROTATE_LEFT:
            SetOrientationAtCursor(DMDO_90);   // Top moves left
            break;
        case IDH_ROTATE_RIGHT:
            SetOrientationAtCursor(DMDO_270);  // Top moves right
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_SHOW:
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            break;
        case IDM_TOGGLE:
            Toggle90();
            break;
        case IDM_AUTO:
            AutoOrient();
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            // Minimize to tray - hide window completely
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_CLOSE:
        // Shift+Click X button to actually close, otherwise minimize to tray
        if (GetKeyState(VK_SHIFT) & 0x8000)
        {
            DestroyWindow(hwnd);
        }
        else
        {
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;

    case WM_DESTROY:
        // Unregister sensor before window destruction to prevent callback posting to dead hwnd
        if (g_sensor && g_sensorToken)
        {
            g_sensor.OrientationChanged(g_sensorToken);
            g_sensorToken = {};
        }
        g_hwnd = nullptr;  // Signal to any pending callbacks
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void CreateTrayIcon(HWND hwnd)
{
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_locked ? g_iconLocked : g_iconUnlocked;
    if (!g_nid.hIcon) g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, g_locked ? L"HALRAD ScreenCompass - Locked" : L"HALRAD ScreenCompass - Auto");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void RemoveTrayIcon()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

void UpdateTrayIcon()
{
    g_nid.hIcon = g_locked ? g_iconLocked : g_iconUnlocked;
    if (!g_nid.hIcon) g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, g_locked ? L"HALRAD ScreenCompass - Locked" : L"HALRAD ScreenCompass - Auto");
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void ShowTrayMenu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();

    // Show "Show" option if window is hidden
    if (!IsWindowVisible(hwnd))
    {
        AppendMenuW(hMenu, MF_STRING, IDM_SHOW, L"Show");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }

    AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE, L"Toggle 90\x00B0");
    AppendMenuW(hMenu, MF_STRING, IDM_AUTO, L"Auto");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);
}

void RotateDisplay(DWORD orientation)
{
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);

    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm))
    {
        Log::Error(L"Failed to get current display settings");
        return;
    }

    bool wasPortrait = (dm.dmDisplayOrientation == DMDO_90 || dm.dmDisplayOrientation == DMDO_270);
    bool willBePortrait = (orientation == DMDO_90 || orientation == DMDO_270);

    if (wasPortrait != willBePortrait)
    {
        DWORD temp = dm.dmPelsWidth;
        dm.dmPelsWidth = dm.dmPelsHeight;
        dm.dmPelsHeight = temp;
    }

    dm.dmDisplayOrientation = orientation;
    dm.dmFields = DM_DISPLAYORIENTATION | DM_PELSWIDTH | DM_PELSHEIGHT;

    LONG result = ChangeDisplaySettingsExW(nullptr, &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);
    if (result != DISP_CHANGE_SUCCESSFUL)
    {
        Log::Warn(L"ChangeDisplaySettings failed");
    }
}

void Toggle90()
{
    // Rotate clockwise: 0 -> 90 -> 180 -> 270 -> 0
    switch (g_currentOrientation)
    {
    case DMDO_DEFAULT: g_currentOrientation = DMDO_90; break;
    case DMDO_90: g_currentOrientation = DMDO_180; break;
    case DMDO_180: g_currentOrientation = DMDO_270; break;
    case DMDO_270: g_currentOrientation = DMDO_DEFAULT; break;
    default: g_currentOrientation = DMDO_DEFAULT; break;
    }
    RotateDisplay(g_currentOrientation);
    PostMessage(g_hwnd, WM_USER + 2, 0, 0);
}

void AutoOrient()
{
    if (g_sensor)
    {
        auto orientation = g_sensor.GetCurrentOrientation();
        g_currentOrientation = SensorOrientationToDisplay(orientation);
        RotateDisplay(g_currentOrientation);
        PostMessage(g_hwnd, WM_USER + 2, 0, 0);
    }
}

DWORD SensorOrientationToDisplay(SimpleOrientation orientation)
{
    // Sensor reports device orientation; we need opposite rotation for content
    switch (orientation)
    {
    case SimpleOrientation::NotRotated:
        return DMDO_DEFAULT;
    case SimpleOrientation::Rotated90DegreesCounterclockwise:
        return DMDO_270;  // Device rotated CCW -> content rotates CW
    case SimpleOrientation::Rotated180DegreesCounterclockwise:
        return DMDO_180;
    case SimpleOrientation::Rotated270DegreesCounterclockwise:
        return DMDO_90;   // Device rotated CW -> content rotates CCW
    default:
        return g_currentOrientation;
    }
}

DWORD GetCurrentDisplayOrientation()
{
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm))
    {
        return dm.dmDisplayOrientation;
    }
    return DMDO_DEFAULT;
}

void SetOrientation(DWORD orientation)
{
    if (orientation != g_currentOrientation)
    {
        g_currentOrientation = orientation;
        RotateDisplay(orientation);
        PostMessage(g_hwnd, WM_USER + 2, 0, 0);
    }
}

void RegisterHotkeys(HWND hwnd)
{
    // Ctrl+Alt+Arrow keys (NVIDIA/Intel style)
    // These may fail if another app (GPU driver) has them registered
    g_hotkeyRegistered[0] = RegisterHotKey(hwnd, IDH_ROTATE_UP, MOD_CONTROL | MOD_ALT, VK_UP) != 0;
    g_hotkeyRegistered[1] = RegisterHotKey(hwnd, IDH_ROTATE_DOWN, MOD_CONTROL | MOD_ALT, VK_DOWN) != 0;
    g_hotkeyRegistered[2] = RegisterHotKey(hwnd, IDH_ROTATE_LEFT, MOD_CONTROL | MOD_ALT, VK_LEFT) != 0;
    g_hotkeyRegistered[3] = RegisterHotKey(hwnd, IDH_ROTATE_RIGHT, MOD_CONTROL | MOD_ALT, VK_RIGHT) != 0;

    int registered = (g_hotkeyRegistered[0] ? 1 : 0) + (g_hotkeyRegistered[1] ? 1 : 0) +
                     (g_hotkeyRegistered[2] ? 1 : 0) + (g_hotkeyRegistered[3] ? 1 : 0);
    if (registered == 4)
    {
        Log::Info(L"All hotkeys registered (Ctrl+Alt+Arrow)");
    }
    else if (registered > 0)
    {
        Log::Warn(L"Some hotkeys failed to register (may be claimed by GPU driver)");
    }
    else
    {
        Log::Warn(L"No hotkeys registered - all claimed by another application");
    }
}

void UnregisterHotkeys(HWND hwnd)
{
    // Only unregister hotkeys that were successfully registered
    if (g_hotkeyRegistered[0]) UnregisterHotKey(hwnd, IDH_ROTATE_UP);
    if (g_hotkeyRegistered[1]) UnregisterHotKey(hwnd, IDH_ROTATE_DOWN);
    if (g_hotkeyRegistered[2]) UnregisterHotKey(hwnd, IDH_ROTATE_LEFT);
    if (g_hotkeyRegistered[3]) UnregisterHotKey(hwnd, IDH_ROTATE_RIGHT);
}

// Multi-monitor support: Get the device name of the monitor under the cursor
std::wstring GetMonitorAtCursor()
{
    POINT pt;
    GetCursorPos(&pt);

    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (!hMon) return L"";

    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi))
    {
        return mi.szDevice;
    }
    return L"";
}

// Get orientation of a specific monitor by device name
DWORD GetDisplayOrientationByName(const wchar_t* deviceName)
{
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(deviceName, ENUM_CURRENT_SETTINGS, &dm))
    {
        return dm.dmDisplayOrientation;
    }
    return DMDO_DEFAULT;
}

// Rotate a specific monitor by device name
void RotateDisplayByName(const wchar_t* deviceName, DWORD orientation)
{
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);

    if (!EnumDisplaySettingsW(deviceName, ENUM_CURRENT_SETTINGS, &dm))
    {
        Log::Error(L"Failed to get display settings for monitor");
        return;
    }

    bool wasPortrait = (dm.dmDisplayOrientation == DMDO_90 || dm.dmDisplayOrientation == DMDO_270);
    bool willBePortrait = (orientation == DMDO_90 || orientation == DMDO_270);

    if (wasPortrait != willBePortrait)
    {
        DWORD temp = dm.dmPelsWidth;
        dm.dmPelsWidth = dm.dmPelsHeight;
        dm.dmPelsHeight = temp;
    }

    dm.dmDisplayOrientation = orientation;
    dm.dmFields = DM_DISPLAYORIENTATION | DM_PELSWIDTH | DM_PELSHEIGHT;

    LONG result = ChangeDisplaySettingsExW(deviceName, &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);
    if (result != DISP_CHANGE_SUCCESSFUL)
    {
        Log::Warn(L"ChangeDisplaySettings failed for monitor");
    }
}

// Set orientation for the monitor under the cursor (follow-mouse)
void SetOrientationAtCursor(DWORD orientation)
{
    // Save cursor position before rotation
    GetCursorPos(&g_cursorRestore);

    std::wstring deviceName = GetMonitorAtCursor();
    if (deviceName.empty()) return;

    DWORD currentOrientation = GetDisplayOrientationByName(deviceName.c_str());
    if (orientation != currentOrientation)
    {
        RotateDisplayByName(deviceName.c_str(), orientation);

        // Restore cursor position after Windows finishes display change
        PostMessage(g_hwnd, WM_USER + 3, 0, 0);
        PostMessage(g_hwnd, WM_USER + 2, 0, 0);
    }
}
