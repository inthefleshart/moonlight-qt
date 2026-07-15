#include "winpointer.h"

#ifdef Q_OS_WIN32

#include "inputgeometry.h"
#include "penconversion.h"
#include "pencursorvisibility.h"
#include "pointerhistory.h"

#include <Limelight.h>
#include <SDL_hints.h>
#include <SDL_log.h>
#include <SDL_mouse.h>
#include <SDL_system.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

namespace {
constexpr ULONG_PTR kPointerMessageSignature = 0xFF515700;
constexpr ULONG_PTR kPointerMessageSignatureMask = 0xFFFFFF00;
constexpr ULONG_PTR kTouchMessageFlag = 0x80;
}

WinPointerBridge::WinPointerBridge(int streamWidth, int streamHeight, bool diagnosticsEnabled,
                                   int cursorPolicy, void* gestureContext,
                                   GestureCallback gestureCallback)
    : m_Window(nullptr),
      m_StreamWidth(streamWidth),
      m_StreamHeight(streamHeight),
      m_DiagnosticsEnabled(diagnosticsEnabled),
      m_CursorPolicy(cursorPolicy),
      m_CursorHiddenForPen(false),
      m_PenInRange(false),
      m_PenInContact(false),
      m_LastButtons(0),
      m_SampleCount(0),
      m_DroppedCount(0),
      m_HistoryBatchCount(0),
      m_HistoryDepthMax(0),
      m_HistoryRetainedCount(0),
      m_HistoryTruncatedCount(0),
      m_HistoryProcessingMicros(0),
      m_PromotedMouseMotionCount(0),
      m_PromotedMouseButtonCount(0),
      m_GestureContext(gestureContext),
      m_GestureCallback(gestureCallback)
{
    SDL_SetWindowsMessageHook(messageHook, this);
}

WinPointerBridge::~WinPointerBridge()
{
    cancelActivePen();
    SDL_SetWindowsMessageHook(nullptr, nullptr);

    if (m_DiagnosticsEnabled) {
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                    "Native pen: %llu samples, %llu dropped, %llu batches, max history %llu, "
                    "%llu retained, %llu truncated, %llu us total",
                    static_cast<unsigned long long>(m_SampleCount),
                    static_cast<unsigned long long>(m_DroppedCount),
                    static_cast<unsigned long long>(m_HistoryBatchCount),
                    static_cast<unsigned long long>(m_HistoryDepthMax),
                    static_cast<unsigned long long>(m_HistoryRetainedCount),
                    static_cast<unsigned long long>(m_HistoryTruncatedCount),
                    static_cast<unsigned long long>(m_HistoryProcessingMicros));
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
    case WM_MOUSEMOVE:
        if ((!SDL_GetHint("SDL_PEN_MOUSE_EVENTS") ||
                std::strcmp(SDL_GetHint("SDL_PEN_MOUSE_EVENTS"), "0") != 0) &&
                isPenPromotedMouseMessage()) {
            ++m_PromotedMouseMotionCount;
        }
        return;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        if ((!SDL_GetHint("SDL_PEN_MOUSE_EVENTS") ||
                std::strcmp(SDL_GetHint("SDL_PEN_MOUSE_EVENTS"), "0") != 0) &&
                isPenPromotedMouseMessage()) {
            ++m_PromotedMouseButtonCount;
        }
        return;
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

    RECT clientRect{};
    if (!GetClientRect(m_Window, &clientRect)) {
        ++m_DroppedCount;
        return;
    }

    // ENTER/LEAVE are range transitions rather than sample batches. Processing
    // their history with the transition message would manufacture duplicate
    // enter/leave events.
    if (message == WM_POINTERENTER || message == WM_POINTERLEAVE) {
        POINTER_PEN_INFO current{};
        if (GetPointerPenInfo(pointerId, &current)) {
            handlePenInfo(current, message, clientRect);
        } else {
            ++m_DroppedCount;
        }
        return;
    }

    const auto batchStart = std::chrono::steady_clock::now();
    std::array<POINTER_PEN_INFO, PointerHistory::BufferCapacity> history{};
    UINT32 count = static_cast<UINT32>(history.size());
    if (GetPointerPenInfoHistory(pointerId, &count, history.data()) && count > 0) {
        const UINT32 boundedCount = std::min<UINT32>(count, static_cast<UINT32>(history.size()));
        ++m_HistoryBatchCount;
        m_HistoryDepthMax = std::max<uint64_t>(m_HistoryDepthMax, count);
        m_HistoryTruncatedCount += count - boundedCount;

        std::array<PointerHistory::SampleState, PointerHistory::BufferCapacity> states{};
        for (UINT32 chronological = 0; chronological < boundedCount; ++chronological) {
            const auto& sample = history[boundedCount - chronological - 1];
            states[chronological] = {sample.pointerInfo.pointerFlags, sample.penFlags,
                                     sample.penMask, sample.pressure};
        }
        const auto selection = PointerHistory::select(states.data(), boundedCount);
        m_HistoryRetainedCount += selection.count;
        for (std::size_t i = 0; i < selection.count; ++i) {
            const UINT32 historyIndex = boundedCount - static_cast<UINT32>(selection.indices[i]) - 1;
            handlePenInfo(history[historyIndex], message, clientRect);
        }
        m_HistoryProcessingMicros += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - batchStart).count());
        return;
    }

    POINTER_PEN_INFO current{};
    if (GetPointerPenInfo(pointerId, &current)) {
        handlePenInfo(current, message, clientRect);
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

void WinPointerBridge::handlePenInfo(const POINTER_PEN_INFO& penInfo, UINT message,
                                     const RECT& clientRect)
{
    POINT clientPoint = penInfo.pointerInfo.ptPixelLocation;
    if (!ScreenToClient(m_Window, &clientPoint)) {
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
            const bool consumedByGesture = m_GestureCallback &&
                m_GestureCallback(m_GestureContext, LI_TOUCH_EVENT_HOVER_LEAVE,
                                  point.x, point.y, false);
            if (!consumedByGesture) {
                LiSendPenEvent(LI_TOUCH_EVENT_HOVER_LEAVE, LI_TOOL_TYPE_UNKNOWN, m_LastButtons,
                               0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                               LI_ROT_UNKNOWN, LI_TILT_UNKNOWN);
            }
            m_PenInRange = false;
            m_PenInContact = false;
            m_LastButtons = 0;
            updateCursorVisibility(false, false);
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

    const bool consumedByGesture = m_GestureCallback &&
        m_GestureCallback(m_GestureContext, eventType, point.x, point.y, isContact);
    if (!consumedByGesture) {
        LiSendPenEvent(eventType, toolType, buttons, point.x, point.y, pressure,
                       0.0f, 0.0f, tilt.rotation, tilt.tilt);
    }

    ++m_SampleCount;
    m_LastButtons = buttons;
    m_PenInRange = eventType != LI_TOUCH_EVENT_HOVER_LEAVE;
    m_PenInContact = isContact && eventType != LI_TOUCH_EVENT_UP;
    updateCursorVisibility(m_PenInRange, m_PenInContact);
}

void WinPointerBridge::cancelActivePen()
{
    if (!m_PenInRange && !m_PenInContact && m_LastButtons == 0) {
        return;
    }

    const bool consumedByGesture = m_GestureCallback &&
        m_GestureCallback(m_GestureContext, LI_TOUCH_EVENT_CANCEL_ALL,
                          0.0f, 0.0f, false);
    if (!consumedByGesture) {
        LiSendPenEvent(LI_TOUCH_EVENT_CANCEL_ALL, LI_TOOL_TYPE_UNKNOWN, 0,
                       0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                       LI_ROT_UNKNOWN, LI_TILT_UNKNOWN);
    }
    m_PenInRange = false;
    m_PenInContact = false;
    m_LastButtons = 0;
    updateCursorVisibility(false, false);
}

void WinPointerBridge::notifyRealMouseMotion()
{
    if (m_CursorHiddenForPen) {
        SDL_ShowCursor(1);
        m_CursorHiddenForPen = false;
    }
}

bool WinPointerBridge::consumePromotedMouseMotion()
{
    if (m_PromotedMouseMotionCount == 0) {
        return false;
    }
    --m_PromotedMouseMotionCount;
    return true;
}

bool WinPointerBridge::consumePromotedMouseButton()
{
    if (m_PromotedMouseButtonCount == 0) {
        return false;
    }
    --m_PromotedMouseButtonCount;
    return true;
}

void WinPointerBridge::updateCursorVisibility(bool penInRange, bool penInContact)
{
    // Automatic keeps a useful local pointer during hover for navigating normal
    // Windows UI, then gets it out of the way while a pen-aware application is
    // drawing its own cursor during contact. The explicit hide-in-range policy
    // retains the old behaviour for users who never want the local cursor shown.
    const bool shouldHide = PenCursorVisibility::shouldHide(
        m_CursorPolicy, penInRange, penInContact);
    if (shouldHide == m_CursorHiddenForPen) {
        return;
    }
    SDL_ShowCursor(shouldHide ? 0 : 1);
    m_CursorHiddenForPen = shouldHide;
}

bool WinPointerBridge::isPenPromotedMouseMessage()
{
    const ULONG_PTR extraInfo = static_cast<ULONG_PTR>(GetMessageExtraInfo());
    return (extraInfo & kPointerMessageSignatureMask) == kPointerMessageSignature &&
           (extraInfo & kTouchMessageFlag) == 0;
}

#endif
