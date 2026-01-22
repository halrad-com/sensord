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

// Globals
HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_nid = {};
SimpleOrientationSensor g_sensor = nullptr;
std::atomic<bool> g_locked{false};
std::atomic<DWORD> g_currentOrientation{DMDO_DEFAULT};
ULONG_PTR g_gdiplusToken = 0;

HICON g_iconLocked = nullptr;
HICON g_iconUnlocked = nullptr;
HICON g_appIcon = nullptr;
Image* g_bgImage = nullptr;
Image* g_upArrow = nullptr;

const wchar_t* CLASS_NAME = L"ScreenCompassWindowClass";
const wchar_t* WINDOW_TITLE = L"ScreenCompass";

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void CreateTrayIcon(HWND hwnd);
void RemoveTrayIcon();
void UpdateTrayIcon();
void ShowTrayMenu(HWND hwnd);
void RotateDisplay(DWORD orientation);
void SetOrientation(DWORD orientation);
void Toggle90();
void AutoOrient();
void RegisterHotkeys(HWND hwnd);
void UnregisterHotkeys(HWND hwnd);
HICON LoadPngAsIcon(const wchar_t* path, int size);
DWORD SensorOrientationToDisplay(SimpleOrientation orientation);
std::wstring GetExeDir();
DWORD GetCurrentDisplayOrientation();

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
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
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    std::wstring exeDir = GetExeDir();

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
                        CreateStreamOnHGlobal(hMem, TRUE, &pStream);
                        if (pStream)
                        {
                            g_bgImage = Image::FromStream(pStream);
                            pStream->Release();
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
    RegisterClassExW(&wc);

    // Create window
    g_hwnd = CreateWindowExW(
        0,
        CLASS_NAME, WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 440,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hwnd) return 1;

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
        g_sensor.OrientationChanged([](SimpleOrientationSensor const&,
            SimpleOrientationSensorOrientationChangedEventArgs const& args)
        {
            if (!g_locked)
            {
                DWORD newOrientation = SensorOrientationToDisplay(args.Orientation());
                if (newOrientation != g_currentOrientation)
                {
                    g_currentOrientation = newOrientation;
                    RotateDisplay(g_currentOrientation);
                    PostMessage(g_hwnd, WM_USER + 2, 0, 0);
                }
            }
        });
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

    UnregisterHotkeys(g_hwnd);
    RemoveTrayIcon();

    // Cleanup
    delete g_bgImage;
    delete g_upArrow;
    // Icons loaded via LoadIcon() are shared system resources - do not call DestroyIcon
    GdiplusShutdown(g_gdiplusToken);

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
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
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
        switch (wParam)
        {
        case IDH_ROTATE_UP:
            SetOrientation(DMDO_DEFAULT);
            break;
        case IDH_ROTATE_DOWN:
            SetOrientation(DMDO_180);
            break;
        case IDH_ROTATE_LEFT:
            SetOrientation(DMDO_270);  // Top moves left
            break;
        case IDH_ROTATE_RIGHT:
            SetOrientation(DMDO_90);   // Top moves right
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
    wcscpy_s(g_nid.szTip, g_locked ? L"ScreenCompass - Locked" : L"ScreenCompass - Auto");
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
    wcscpy_s(g_nid.szTip, g_locked ? L"ScreenCompass - Locked" : L"ScreenCompass - Auto");
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
        return;

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

    ChangeDisplaySettingsExW(nullptr, &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);
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

HICON LoadPngAsIcon(const wchar_t* path, int size)
{
    Bitmap* bitmap = Bitmap::FromFile(path);
    if (!bitmap || bitmap->GetLastStatus() != Ok)
    {
        delete bitmap;
        return nullptr;
    }

    // Resize if needed
    Bitmap* resized = new Bitmap(size, size, PixelFormat32bppARGB);
    Graphics g(resized);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(bitmap, 0, 0, size, size);

    HICON hIcon = nullptr;
    resized->GetHICON(&hIcon);

    delete resized;
    delete bitmap;
    return hIcon;
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

std::wstring GetExeDir()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        dir = dir.substr(0, pos + 1);
    return dir;
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
    RegisterHotKey(hwnd, IDH_ROTATE_UP, MOD_CONTROL | MOD_ALT, VK_UP);
    RegisterHotKey(hwnd, IDH_ROTATE_DOWN, MOD_CONTROL | MOD_ALT, VK_DOWN);
    RegisterHotKey(hwnd, IDH_ROTATE_LEFT, MOD_CONTROL | MOD_ALT, VK_LEFT);
    RegisterHotKey(hwnd, IDH_ROTATE_RIGHT, MOD_CONTROL | MOD_ALT, VK_RIGHT);
}

void UnregisterHotkeys(HWND hwnd)
{
    UnregisterHotKey(hwnd, IDH_ROTATE_UP);
    UnregisterHotKey(hwnd, IDH_ROTATE_DOWN);
    UnregisterHotKey(hwnd, IDH_ROTATE_LEFT);
    UnregisterHotKey(hwnd, IDH_ROTATE_RIGHT);
}
