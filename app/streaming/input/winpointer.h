#pragma once

#include <QtGlobal>

#ifdef Q_OS_WIN32

#include <SDL_stdinc.h>
#include <windows.h>

#include <cstdint>

class WinPointerBridge
{
public:
    using GestureCallback = bool (*)(void* context, uint8_t eventType, float x, float y,
                                     bool inContact);

    WinPointerBridge(int streamWidth, int streamHeight, bool diagnosticsEnabled,
                     int cursorPolicy, void* gestureContext, GestureCallback gestureCallback);
    ~WinPointerBridge();

    void setWindow(HWND window);
    void cancelActivePen();
    bool isPenInRange() const { return m_PenInRange; }
    bool isPenInContact() const { return m_PenInContact; }
    void notifyRealMouseMotion();
    bool consumePromotedMouseMotion();
    bool consumePromotedMouseButton();
    void setDiagnosticsEnabled(bool enabled) { m_DiagnosticsEnabled = enabled; }

private:
    static void SDLCALL messageHook(void* userdata, void* hwnd, unsigned int message,
                                   Uint64 wParam, Sint64 lParam);

    void handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void handlePenInfo(const POINTER_PEN_INFO& penInfo, UINT message, const RECT& clientRect);
    uint8_t eventTypeFor(const POINTER_PEN_INFO& penInfo, UINT message) const;
    void updateCursorVisibility(bool penInRange);
    static bool isPenPromotedMouseMessage();

    HWND m_Window;
    int m_StreamWidth;
    int m_StreamHeight;
    bool m_DiagnosticsEnabled;
    int m_CursorPolicy;
    bool m_CursorHiddenForPen;
    bool m_PenInRange;
    bool m_PenInContact;
    uint8_t m_LastButtons;
    uint64_t m_SampleCount;
    uint64_t m_DroppedCount;
    uint64_t m_HistoryBatchCount;
    uint64_t m_HistoryDepthMax;
    uint64_t m_HistoryRetainedCount;
    uint64_t m_HistoryTruncatedCount;
    uint64_t m_HistoryProcessingMicros;
    uint32_t m_PromotedMouseMotionCount;
    uint32_t m_PromotedMouseButtonCount;
    void* m_GestureContext;
    GestureCallback m_GestureCallback;
};

#endif
