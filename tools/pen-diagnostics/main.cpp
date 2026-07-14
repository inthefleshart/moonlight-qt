#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace {
constexpr int kClearButton = 100;
constexpr UINT_PTR kRateTimer = 1;

struct AppState
{
    std::wstring phase = L"Waiting for pen input";
    UINT32 pressure = 0;
    INT32 tiltX = 0;
    INT32 tiltY = 0;
    bool hover = false;
    bool eraser = false;
    bool barrel = false;
    bool contact = false;
    POINT lastPoint{};
    bool hasLastPoint = false;
    std::vector<POINT> stroke;
    std::array<UINT32, 240> pressureGraph{};
    size_t graphIndex = 0;
    UINT64 eventCounter = 0;
    UINT64 lastRateCounter = 0;
    UINT32 eventsPerSecond = 0;
    std::set<UINT32> touchContacts;
};

AppState g_State;

void clearCanvas(HWND window)
{
    g_State.stroke.clear();
    g_State.pressureGraph.fill(0);
    g_State.graphIndex = 0;
    g_State.hasLastPoint = false;
    InvalidateRect(window, nullptr, TRUE);
}

void paint(HWND window)
{
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(window, &ps);
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(25, 25, 25));
    const std::wstring status = L"Phase: " + g_State.phase +
        L"    Pressure: " + std::to_wstring(g_State.pressure) + L" / 1024" +
        L"    Tilt: " + std::to_wstring(g_State.tiltX) + L", " + std::to_wstring(g_State.tiltY) +
        L"    Eraser: " + (g_State.eraser ? L"yes" : L"no") +
        L"    Barrel: " + (g_State.barrel ? L"down" : L"up");
    TextOutW(dc, 20, 20, status.c_str(), static_cast<int>(status.size()));

    const std::wstring rate = L"Events/sec: " + std::to_wstring(g_State.eventsPerSecond) +
        L"    Active touch contacts: " + std::to_wstring(g_State.touchContacts.size()) +
        L"    Memory-only diagnostics; nothing is logged.";
    TextOutW(dc, 20, 48, rate.c_str(), static_cast<int>(rate.size()));

    RECT graph{20, 110, client.right - 20, 240};
    Rectangle(dc, graph.left, graph.top, graph.right, graph.bottom);
    TextOutW(dc, graph.left + 8, graph.top + 6, L"Pressure history", 16);
    HPEN pressurePen = CreatePen(PS_SOLID, 2, RGB(35, 115, 210));
    HGDIOBJ oldPen = SelectObject(dc, pressurePen);
    const int width = std::max(1L, graph.right - graph.left - 2);
    for (size_t i = 1; i < g_State.pressureGraph.size(); ++i) {
        const size_t previous = (g_State.graphIndex + i - 1) % g_State.pressureGraph.size();
        const size_t current = (g_State.graphIndex + i) % g_State.pressureGraph.size();
        const int x1 = graph.left + 1 + static_cast<int>((i - 1) * width / (g_State.pressureGraph.size() - 1));
        const int x2 = graph.left + 1 + static_cast<int>(i * width / (g_State.pressureGraph.size() - 1));
        const int y1 = graph.bottom - 1 - static_cast<int>(g_State.pressureGraph[previous] * (graph.bottom - graph.top - 2) / 1024);
        const int y2 = graph.bottom - 1 - static_cast<int>(g_State.pressureGraph[current] * (graph.bottom - graph.top - 2) / 1024);
        MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2);
    }

    SelectObject(dc, oldPen);
    DeleteObject(pressurePen);
    RECT canvas{20, 260, client.right - 20, client.bottom - 20};
    Rectangle(dc, canvas.left, canvas.top, canvas.right, canvas.bottom);
    TextOutW(dc, canvas.left + 8, canvas.top + 6, L"Draw here with the remote pen", 29);
    HPEN strokePen = CreatePen(PS_SOLID, 2, RGB(20, 20, 20));
    oldPen = SelectObject(dc, strokePen);
    for (size_t i = 1; i < g_State.stroke.size(); ++i) {
        if (g_State.stroke[i - 1].x < 0 || g_State.stroke[i].x < 0) continue;
        MoveToEx(dc, g_State.stroke[i - 1].x, g_State.stroke[i - 1].y, nullptr);
        LineTo(dc, g_State.stroke[i].x, g_State.stroke[i].y);
    }
    SelectObject(dc, oldPen);
    DeleteObject(strokePen);
    EndPaint(window, &ps);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        CreateWindowW(L"BUTTON", L"Clear / Reset", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      20, 70, 120, 28, window,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClearButton)),
                      GetModuleHandleW(nullptr), nullptr);
        SetTimer(window, kRateTimer, 1000, nullptr);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == kClearButton) { clearCanvas(window); return 0; }
        break;
    case WM_TIMER:
        if (wParam == kRateTimer) {
            g_State.eventsPerSecond = static_cast<UINT32>(g_State.eventCounter - g_State.lastRateCounter);
            g_State.lastRateCounter = g_State.eventCounter;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_POINTERENTER:
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    case WM_POINTERLEAVE: {
        const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
        POINTER_INPUT_TYPE type = PT_POINTER;
        if (!GetPointerType(pointerId, &type)) break;
        if (type == PT_TOUCH) {
            if (message == WM_POINTERDOWN) g_State.touchContacts.insert(pointerId);
            if (message == WM_POINTERUP || message == WM_POINTERLEAVE) g_State.touchContacts.erase(pointerId);
            ++g_State.eventCounter;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (type != PT_PEN) break;
        POINTER_PEN_INFO pen{};
        if (!GetPointerPenInfo(pointerId, &pen)) break;
        POINT point = pen.pointerInfo.ptPixelLocation;
        ScreenToClient(window, &point);
        g_State.contact = (pen.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
        g_State.hover = !g_State.contact && (pen.pointerInfo.pointerFlags & POINTER_FLAG_INRANGE);
        g_State.eraser = (pen.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED)) != 0;
        g_State.barrel = (pen.penFlags & PEN_FLAG_BARREL) != 0;
        g_State.pressure = (pen.penMask & PEN_MASK_PRESSURE) ? std::min<UINT32>(pen.pressure, 1024) : 0;
        g_State.tiltX = (pen.penMask & PEN_MASK_TILT_X) ? pen.tiltX : 0;
        g_State.tiltY = (pen.penMask & PEN_MASK_TILT_Y) ? pen.tiltY : 0;
        g_State.phase = message == WM_POINTERDOWN ? L"Down" : message == WM_POINTERUP ? L"Up" :
                        message == WM_POINTERENTER ? L"Enter" : message == WM_POINTERLEAVE ? L"Leave" :
                        g_State.contact ? L"Move" : L"Hover";
        g_State.pressureGraph[g_State.graphIndex++ % g_State.pressureGraph.size()] = g_State.pressure;
        if (message == WM_POINTERDOWN || (g_State.contact && !g_State.hasLastPoint)) {
            g_State.stroke.push_back({-1, -1});
            g_State.hasLastPoint = true;
        }
        if (g_State.contact) g_State.stroke.push_back(point);
        if (message == WM_POINTERUP || message == WM_POINTERLEAVE) g_State.hasLastPoint = false;
        if (g_State.stroke.size() > 20000) g_State.stroke.erase(g_State.stroke.begin(), g_State.stroke.begin() + 5000);
        ++g_State.eventCounter;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_PAINT: paint(window); return 0;
    case WM_DESTROY: KillTimer(window, kRateTimer); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const wchar_t* className = L"MoonlightPenDiagnosticsWindow";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass)) return 1;
    HWND window = CreateWindowExW(0, className, L"Moonlight Pen Diagnostics",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
