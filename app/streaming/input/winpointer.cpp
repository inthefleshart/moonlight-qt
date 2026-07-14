#include "winpointer.h"

#ifdef Q_OS_WIN32

#include "inputgeometry.h"
#include "penconversion.h"

#include <Limelight.h>
#include <SDL_log.h>
#include <SDL_system.h>

#include <algorithm>
#include <array>

namespace {
constexpr UINT32 kMaxHistorySamples = 128;
}

WinPointerBridge::WinPointerBridge(int streamWidth, int streamHeight, bool diagnosticsEnabled)
    : m_Window(nullptr),
      m_StreamWidth(streamWidth),
      m_StreamHeight(streamHeight),
      m_DiagnosticsEnabled(diagnosticsEnabled),
      m_PenInRange(false),
      m_PenInContact(false),
      m_LastButtons(0),
      m_SampleCount(0),
      m_DroppedCount(0)
{
    SDL_SetWindowsMessageHook(messageHook, this);
}

WinPointerBridge::~WinPointerBridge()
{
    cancelActivePen();
    SDL_SetWindowsMessageHook(nullptr, nullptr);

    if (m_DiagnosticsEnabled) {
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                    "Native pen session ended: %llu samples, %llu dropped",
                    static_cast<unsigned long long>(m_SampleCount),
                    static_cast<unsigned long long>(m_DroppedCount));
    }
}

void WinPointerBridge::setWindow(HWND window)
{
    if (m_Window != window) {
        cancelActivePen();
        m_Window = window;
    }
}

void WinPointerBridge::messageHook(void* userdata, void* hwnd, unsigned int message,
                                   Uint64 wParam, Sint64 lParam)
{
    static_cast<WinPointerBridge*>(userdata)->handleMessage(
        static_cast<HWND>(hwnd), message, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam));
}

void WinPointerBridge::handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM)
{
    if (!m_Window || hwnd != m_Window || !(LiGetHostFeatureFlags() & LI_FF_PEN_TOUCH_EVENTS)) {
        return;
    }

    switch (message) {
    case WM_POINTERENTER:
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    case WM_POINTERLEAVE:
        break;
    case WM_POINTERCAPTURECHANGED:
        cancelActivePen();
        return;
    default:
        return;
    }

    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINTER_INPUT_TYPE pointerType = PT_POINTER;
    if (!GetPointerType(pointerId, &pointerType) || pointerType != PT_PEN) {
        return;
    }

    // ENTER/LEAVE are range transitions rather than sample batches. Processing
    // their history with the transition message would manufacture duplicate
    // enter/leave events.
    if (message == WM_POINTERENTER || message == WM_POINTERLEAVE) {
        POINTER_PEN_INFO current{};
        if (GetPointerPenInfo(pointerId, &current)) {
            handlePenInfo(current, message);
        } else {
            ++m_DroppedCount;
        }
        return;
    }

    std::array<POINTER_PEN_INFO, kMaxHistorySamples> history{};
    UINT32 count = static_cast<UINT32>(history.size());
    if (GetPointerPenInfoHistory(pointerId, &count, history.data()) && count > 0) {
        // Win32 returns newest first. Preserve the digitizer's chronological sample order.
        for (UINT32 i = count; i > 0; --i) {
            handlePenInfo(history[i - 1], message);
        }
        return;
    }

    POINTER_PEN_INFO current{};
    if (GetPointerPenInfo(pointerId, &current)) {
        handlePenInfo(current, message);
    } else {
        ++m_DroppedCount;
    }
}

uint8_t WinPointerBridge::eventTypeFor(const POINTER_PEN_INFO& penInfo, UINT message) const
{
    const UINT32 flags = penInfo.pointerInfo.pointerFlags;
    if (flags & POINTER_FLAG_CANCELED) {
        return LI_TOUCH_EVENT_CANCEL;
    }
    if (flags & POINTER_FLAG_DOWN) {
        return LI_TOUCH_EVENT_DOWN;
    }
    if (flags & POINTER_FLAG_UP) {
        return LI_TOUCH_EVENT_UP;
    }
    if (message == WM_POINTERLEAVE || !(flags & POINTER_FLAG_INRANGE)) {
        return LI_TOUCH_EVENT_HOVER_LEAVE;
    }
    return (flags & POINTER_FLAG_INCONTACT) ? LI_TOUCH_EVENT_MOVE : LI_TOUCH_EVENT_HOVER;
}

void WinPointerBridge::handlePenInfo(const POINTER_PEN_INFO& penInfo, UINT message)
{
    POINT clientPoint = penInfo.pointerInfo.ptPixelLocation;
    if (!ScreenToClient(m_Window, &clientPoint)) {
        ++m_DroppedCount;
        return;
    }

    RECT clientRect{};
    if (!GetClientRect(m_Window, &clientRect)) {
        ++m_DroppedCount;
        return;
    }

    const bool isContact = (penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
    // Ignore a new contact outside the video rectangle, but keep an existing
    // stroke clamped if it crosses a letterbox or window boundary.
    const bool clamp = m_PenInContact;
    const auto point = InputGeometry::mapClientPoint(
        clientPoint.x, clientPoint.y,
        clientRect.right - clientRect.left, clientRect.bottom - clientRect.top,
        m_StreamWidth, m_StreamHeight, clamp);

    if (!point.inside && !clamp) {
        if (m_PenInRange) {
            LiSendPenEvent(LI_TOUCH_EVENT_HOVER_LEAVE, LI_TOOL_TYPE_UNKNOWN, m_LastButtons,
                           0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                           LI_ROT_UNKNOWN, LI_TILT_UNKNOWN);
            m_PenInRange = false;
        }
        return;
    }

    const uint8_t eventType = eventTypeFor(penInfo, message);
    uint8_t toolType = LI_TOOL_TYPE_PEN;
    if (penInfo.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED)) {
        toolType = LI_TOOL_TYPE_ERASER;
    }

    uint8_t buttons = 0;
    if (penInfo.penFlags & PEN_FLAG_BARREL) {
        buttons |= LI_PEN_BUTTON_PRIMARY;
    }

    float pressure = 0.0f;
    if (isContact) {
        pressure = (penInfo.penMask & PEN_MASK_PRESSURE) ?
                       std::clamp(penInfo.pressure / 1024.0f, 0.0f, 1.0f) : (1.0f / 1024.0f);
    }

    const auto tilt = PenConversion::toPolarTilt(
        penInfo.tiltX, penInfo.tiltY,
        (penInfo.penMask & (PEN_MASK_TILT_X | PEN_MASK_TILT_Y)) ==
            (PEN_MASK_TILT_X | PEN_MASK_TILT_Y));

    LiSendPenEvent(eventType, toolType, buttons, point.x, point.y, pressure,
                   0.0f, 0.0f, tilt.rotation, tilt.tilt);

    ++m_SampleCount;
    m_LastButtons = buttons;
    m_PenInRange = eventType != LI_TOUCH_EVENT_HOVER_LEAVE;
    m_PenInContact = isContact && eventType != LI_TOUCH_EVENT_UP;
}

void WinPointerBridge::cancelActivePen()
{
    if (!m_PenInRange && !m_PenInContact && m_LastButtons == 0) {
        return;
    }

    LiSendPenEvent(LI_TOUCH_EVENT_CANCEL_ALL, LI_TOOL_TYPE_UNKNOWN, 0,
                   0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                   LI_ROT_UNKNOWN, LI_TILT_UNKNOWN);
    m_PenInRange = false;
    m_PenInContact = false;
    m_LastButtons = 0;
}

#endif
