#include "streaming/session.h"

#include <Limelight.h>
#include "SDL_compat.h"

#ifdef Q_OS_WIN32
#include "winpointer.h"
#endif

#define VK_0 0x30
#define VK_A 0x41

namespace {
quint16 virtualKeyForScancode(SDL_Scancode scancode)
{
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
        return static_cast<quint16>((scancode - SDL_SCANCODE_1) + 0x31);
    if (scancode == SDL_SCANCODE_0) return 0x30;
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
        return static_cast<quint16>((scancode - SDL_SCANCODE_A) + 0x41);
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F24)
        return static_cast<quint16>((scancode - SDL_SCANCODE_F1) + 0x70);
    if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
        return static_cast<quint16>((scancode - SDL_SCANCODE_KP_1) + 0x61);
    switch (scancode) {
    case SDL_SCANCODE_KP_0: return 0x60; case SDL_SCANCODE_KP_PERIOD: return 0x6E;
    case SDL_SCANCODE_BACKSPACE: return 0x08; case SDL_SCANCODE_TAB: return 0x09;
    case SDL_SCANCODE_CLEAR: return 0x0C; case SDL_SCANCODE_PAUSE: return 0x13;
    case SDL_SCANCODE_CAPSLOCK: return 0x14;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x0D;
    case SDL_SCANCODE_ESCAPE: return 0x1B; case SDL_SCANCODE_SPACE: return 0x20;
    case SDL_SCANCODE_PAGEUP: return 0x21; case SDL_SCANCODE_PAGEDOWN: return 0x22;
    case SDL_SCANCODE_END: return 0x23; case SDL_SCANCODE_HOME: return 0x24;
    case SDL_SCANCODE_LEFT: return 0x25; case SDL_SCANCODE_UP: return 0x26;
    case SDL_SCANCODE_RIGHT: return 0x27; case SDL_SCANCODE_DOWN: return 0x28;
    case SDL_SCANCODE_SELECT: return 0x29; case SDL_SCANCODE_EXECUTE: return 0x2B;
    case SDL_SCANCODE_INSERT: return 0x2D; case SDL_SCANCODE_DELETE: return 0x2E;
    case SDL_SCANCODE_PRINTSCREEN: return 0x2C; case SDL_SCANCODE_HELP: return 0x2F;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x6A; case SDL_SCANCODE_KP_PLUS: return 0x6B;
    case SDL_SCANCODE_KP_COMMA: return 0x6C; case SDL_SCANCODE_KP_MINUS: return 0x6D;
    case SDL_SCANCODE_KP_DIVIDE: return 0x6F; case SDL_SCANCODE_NUMLOCKCLEAR: return 0x90;
    case SDL_SCANCODE_SCROLLLOCK: return 0x91;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 0xA0;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0xA2;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0xA4;
    case SDL_SCANCODE_LGUI: return 0x5B; case SDL_SCANCODE_RGUI: return 0x5C;
    case SDL_SCANCODE_APPLICATION: return 0x5D;
    case SDL_SCANCODE_AC_BACK: return 0xA6; case SDL_SCANCODE_AC_FORWARD: return 0xA7;
    case SDL_SCANCODE_AC_REFRESH: return 0xA8; case SDL_SCANCODE_AC_STOP: return 0xA9;
    case SDL_SCANCODE_AC_SEARCH: return 0xAA; case SDL_SCANCODE_AC_BOOKMARKS: return 0xAB;
    case SDL_SCANCODE_AC_HOME: return 0xAC;
    case SDL_SCANCODE_SEMICOLON: return 0xBA; case SDL_SCANCODE_EQUALS: return 0xBB;
    case SDL_SCANCODE_COMMA: return 0xBC; case SDL_SCANCODE_MINUS: return 0xBD;
    case SDL_SCANCODE_PERIOD: return 0xBE; case SDL_SCANCODE_SLASH: return 0xBF;
    case SDL_SCANCODE_GRAVE: return 0xC0; case SDL_SCANCODE_LEFTBRACKET: return 0xDB;
    case SDL_SCANCODE_BACKSLASH: return 0xDC; case SDL_SCANCODE_RIGHTBRACKET: return 0xDD;
    case SDL_SCANCODE_APOSTROPHE: case SDL_SCANCODE_INTERNATIONAL3: return 0xDE;
    case SDL_SCANCODE_NONUSBACKSLASH: case SDL_SCANCODE_INTERNATIONAL1: return 0xE2;
    case SDL_SCANCODE_LANG1: return 0x1C; case SDL_SCANCODE_LANG2: return 0x1D;
    default: return 0;
    }
}

char modifierMask(const TabletKeyStroke& stroke)
{
    return (stroke.control ? MODIFIER_CTRL : 0) | (stroke.alt ? MODIFIER_ALT : 0) |
           (stroke.shift ? MODIFIER_SHIFT : 0) | (stroke.meta ? MODIFIER_META : 0);
}
}

bool SdlInputHandler::handleTabletMappedKey(SDL_KeyboardEvent* event)
{
    const quint16 virtualKey = virtualKeyForScancode(event->keysym.scancode);
    if (!virtualKey) return false;

    if (event->state == SDL_RELEASED) {
        for (int slot = 0; slot < TabletMappingManager::SlotCount; ++slot) {
            const auto source = TabletMappingManager::get()->sourceForSlot(slot);
            if (source.valid && source.chord.virtualKey == virtualKey && m_TabletSourceActive[slot]) {
                executeTabletAction(slot, m_ActiveTabletMappings[slot], false);
                m_ActiveTabletMappings[slot] = {};
                m_TabletSourceActive[slot] = false;
                return true;
            }
        }
        return false;
    }

    const bool control = (event->keysym.mod & KMOD_CTRL) != 0;
    const bool alt = (event->keysym.mod & KMOD_ALT) != 0;
    const bool shift = (event->keysym.mod & KMOD_SHIFT) != 0;
    const bool meta = (event->keysym.mod & KMOD_GUI) != 0;
    const int slot = TabletMappingManager::get()->slotForSource(virtualKey, control, alt, shift, meta);
    if (slot < 0) return false;

    const auto action = TabletMappingManager::get()->actionForSlot(slot);
    if (action.kind == TabletActionKind::PassThrough) return false;
    if (event->repeat) return true;
    m_ActiveTabletMappings[slot] = action;
    m_TabletSourceActive[slot] = true;
    executeTabletAction(slot, action, true);
    return true;
}

void SdlInputHandler::executeTabletAction(int slot, const TabletControlAction& action, bool pressed)
{
    switch (action.kind) {
    case TabletActionKind::Disabled:
    case TabletActionKind::PassThrough:
        return;
    case TabletActionKind::KeyChord:
    case TabletActionKind::WacomRadialChord:
        if (!action.chord.virtualKey) return;
        if (action.activation == TabletActivation::Hold) {
            sendTabletKeyStroke(action.chord, pressed);
        } else if (pressed) {
            sendTabletKeyStroke(action.chord, true);
            sendTabletKeyStroke(action.chord, false);
        }
        return;
    case TabletActionKind::KeySequence:
        if (pressed) {
            for (const auto& stroke : action.sequence) {
                sendTabletKeyStroke(stroke, true);
                sendTabletKeyStroke(stroke, false);
            }
        }
        return;
    case TabletActionKind::MouseWheel:
        if (pressed && action.wheelDelta) LiSendHighResScrollEvent(static_cast<short>(action.wheelDelta));
        return;
    case TabletActionKind::LocalAction:
        if (!pressed) return;
        switch (action.localAction) {
        case TabletLocalAction::ToggleTouch:
            m_TouchForwardingEnabled = !m_TouchForwardingEnabled;
            if (!m_TouchForwardingEnabled) {
                LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL_ALL, 0, 0, 0, 0, 0, 0, 0);
                m_BlockedTouchIds.clear();
            }
            break;
        case TabletLocalAction::CycleTouchPolicy:
            m_TouchPolicy = static_cast<StreamingPreferences::TouchPolicy>(
                (static_cast<int>(m_TouchPolicy) + 1) % 3);
            break;
        case TabletLocalAction::ToggleDiagnostics:
            m_RuntimeDiagnosticsEnabled = !m_RuntimeDiagnosticsEnabled;
#ifdef Q_OS_WIN32
            if (m_NativePenBridge) m_NativePenBridge->setDiagnosticsEnabled(m_RuntimeDiagnosticsEnabled);
#endif
            break;
        case TabletLocalAction::ResetStuckInput:
            releaseTabletActions();
            raiseAllKeys();
            break;
        default: break;
        }
        return;
    case TabletActionKind::PenGesture:
        if (pressed) {
            if (m_ActivePenGestureSlot >= 0 && m_ActivePenGestureSlot != slot) return;
#ifdef Q_OS_WIN32
            if (m_NativePenBridge && m_NativePenBridge->isPenInContact())
                m_NativePenBridge->cancelActivePen();
#endif
            m_ActivePenGestureSlot = slot;
            m_PenGestureContactActive = false;
            m_PenGestureKeysDown = false;
            if (action.mouseButton == TabletMouseNone) {
                sendTabletKeyStroke(action.chord, true);
                m_PenGestureKeysDown = true;
            }
        } else if (m_ActivePenGestureSlot == slot) {
            releasePenGesture();
        }
        return;
    }
}

void SdlInputHandler::sendTabletKeyStroke(const TabletKeyStroke& stroke, bool pressed)
{
    const char modifiers = modifierMask(stroke);
    const struct { bool enabled; short key; } modifierKeys[] = {
        {stroke.control, 0xA2}, {stroke.alt, 0xA4}, {stroke.shift, 0xA0}, {stroke.meta, 0x5B},
    };
    if (pressed) {
        for (const auto& modifier : modifierKeys) {
            if (modifier.enabled && modifier.key != stroke.virtualKey) {
                LiSendKeyboardEvent2(0x8000 | modifier.key, KEY_ACTION_DOWN, modifiers, 0);
                m_KeysDown.insert(modifier.key);
            }
        }
        if (stroke.virtualKey) {
            LiSendKeyboardEvent2(0x8000 | stroke.virtualKey, KEY_ACTION_DOWN, modifiers, 0);
            m_KeysDown.insert(stroke.virtualKey);
        }
    } else {
        if (stroke.virtualKey) {
            LiSendKeyboardEvent2(0x8000 | stroke.virtualKey, KEY_ACTION_UP, modifiers, 0);
            m_KeysDown.remove(stroke.virtualKey);
        }
        for (int i = 3; i >= 0; --i) {
            if (modifierKeys[i].enabled && modifierKeys[i].key != stroke.virtualKey) {
                LiSendKeyboardEvent2(0x8000 | modifierKeys[i].key, KEY_ACTION_UP, 0, 0);
                m_KeysDown.remove(modifierKeys[i].key);
            }
        }
    }
}

void SdlInputHandler::releasePenGesture()
{
    if (m_ActivePenGestureSlot < 0) return;
    const auto action = m_ActiveTabletMappings[m_ActivePenGestureSlot];
    if (m_PenGestureContactActive && action.mouseButton != TabletMouseNone)
        LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, static_cast<char>(action.mouseButton));
    if (m_PenGestureKeysDown) sendTabletKeyStroke(action.chord, false);
    m_PenGestureContactActive = false;
    m_PenGestureKeysDown = false;
    m_ActivePenGestureSlot = -1;
}

void SdlInputHandler::releaseTabletActions()
{
    releasePenGesture();
    for (int slot = 0; slot < TabletMappingManager::SlotCount; ++slot) {
        const auto action = m_ActiveTabletMappings[slot];
        if (action.kind == TabletActionKind::KeyChord && action.activation == TabletActivation::Hold)
            sendTabletKeyStroke(action.chord, false);
        m_ActiveTabletMappings[slot] = {};
        m_TabletSourceActive[slot] = false;
    }
}

#ifdef Q_OS_WIN32
bool SdlInputHandler::nativePenGestureCallback(void* context, uint8_t eventType, float x, float y,
                                               bool inContact)
{
    return static_cast<SdlInputHandler*>(context)->handleNativePenGesture(eventType, x, y, inContact);
}

bool SdlInputHandler::handleNativePenGesture(uint8_t eventType, float x, float y, bool inContact)
{
    if (m_ActivePenGestureSlot < 0) return false;
    const auto action = m_ActiveTabletMappings[m_ActivePenGestureSlot];
    if (eventType != LI_TOUCH_EVENT_HOVER_LEAVE && eventType != LI_TOUCH_EVENT_CANCEL &&
            eventType != LI_TOUCH_EVENT_CANCEL_ALL) {
        LiSendMousePositionEvent(static_cast<short>(x * m_StreamWidth),
                                 static_cast<short>(y * m_StreamHeight),
                                 m_StreamWidth, m_StreamHeight);
    }

    if (inContact && !m_PenGestureContactActive) {
        if (!m_PenGestureKeysDown) {
            sendTabletKeyStroke(action.chord, true);
            m_PenGestureKeysDown = true;
        }
        if (action.mouseButton != TabletMouseNone)
            LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, static_cast<char>(action.mouseButton));
        m_PenGestureContactActive = true;
    }
    if ((!inContact || eventType == LI_TOUCH_EVENT_UP || eventType == LI_TOUCH_EVENT_CANCEL ||
            eventType == LI_TOUCH_EVENT_CANCEL_ALL || eventType == LI_TOUCH_EVENT_HOVER_LEAVE) &&
            m_PenGestureContactActive) {
        if (action.mouseButton != TabletMouseNone)
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, static_cast<char>(action.mouseButton));
        if (m_PenGestureKeysDown && action.mouseButton != TabletMouseNone) {
            sendTabletKeyStroke(action.chord, false);
            m_PenGestureKeysDown = false;
        }
        m_PenGestureContactActive = false;
    }
    return true;
}
#endif

// These are real Windows VK_* codes
#ifndef VK_F1
#define VK_F1 0x70
#define VK_F13 0x7C
#define VK_NUMPAD0 0x60
#endif

void SdlInputHandler::performSpecialKeyCombo(KeyCombo combo)
{
    switch (combo) {
    case KeyComboQuit:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Detected quit key combo");
        { SDL_Event event{}; event.type = SDL_QUIT; event.quit.timestamp = SDL_GetTicks(); SDL_PushEvent(&event); }
        break;
    case KeyComboUngrabInput:
        setCaptureActive(!isCaptureActive()); raiseAllKeys(); break;
    case KeyComboToggleFullScreen:
        Session::s_ActiveSession->toggleFullscreen(); raiseAllKeys(); break;
    case KeyComboToggleStatsOverlay:
        Session::get()->getOverlayManager().setOverlayState(Overlay::OverlayDebug,
            !Session::get()->getOverlayManager().isOverlayEnabled(Overlay::OverlayDebug)); break;
    case KeyComboToggleMouseMode:
        setCaptureActive(false); m_AbsoluteMouseMode = !m_AbsoluteMouseMode; setCaptureActive(true); break;
    case KeyComboToggleCursorHide:
        if (!SDL_GetRelativeMouseMode()) { m_MouseCursorCapturedVisibilityState = !m_MouseCursorCapturedVisibilityState; SDL_ShowCursor(m_MouseCursorCapturedVisibilityState); }
        break;
    case KeyComboToggleMinimize:
        SDL_MinimizeWindow(m_Window); break;
    case KeyComboPasteText:
    {
        raiseAllKeys();
        char* text = nullptr;
        if (SDL_HasClipboardText() && (text = SDL_GetClipboardText()) != nullptr) {
            for (char* c = text; *c != 0; ++c) {
                if (*c == '\r' && *(c + 1) == '\n') memmove(c, c + 1, strlen(c));
            }
            LiSendUtf8TextEvent(text, static_cast<unsigned int>(strlen(text)));
            SDL_free(text);
        }
        break;
    }
    case KeyComboTogglePointerRegionLock:
        m_PointerRegionLockActive = !m_PointerRegionLockActive;
        m_PointerRegionLockToggledByUser = true; updatePointerRegionLock(); break;
    case KeyComboQuitAndExit:
        Session::get()->setShouldExit(true);
        { SDL_Event event{}; event.type = SDL_QUIT; event.quit.timestamp = SDL_GetTicks(); SDL_PushEvent(&event); }
        break;
    case KeyComboToggleKeyboardGrab:
        m_CaptureSystemKeysMode = isSystemKeyCaptureActive() ? StreamingPreferences::CSK_OFF : StreamingPreferences::CSK_ALWAYS;
        updateKeyboardGrabState(); break;
    default: Q_UNREACHABLE();
    }
}

void SdlInputHandler::handleKeyEvent(SDL_KeyboardEvent* event)
{
    short keyCode;
    char modifiers;
    bool shouldNotConvertToScanCodeOnServer = false;

    if (handleTabletMappedKey(event)) return;
    if (event->repeat) return;

    if ((event->state == SDL_PRESSED) && (event->keysym.mod & KMOD_CTRL) &&
            (event->keysym.mod & KMOD_ALT) && (event->keysym.mod & KMOD_SHIFT)) {
        for (int i = 0; i < KeyComboMax; ++i) {
            if (m_SpecialKeyCombos[i].enabled && event->keysym.sym == m_SpecialKeyCombos[i].keyCode) {
                performSpecialKeyCombo(m_SpecialKeyCombos[i].keyCombo); return;
            }
        }
        for (int i = 0; i < KeyComboMax; ++i) {
            if (m_SpecialKeyCombos[i].enabled && event->keysym.scancode == m_SpecialKeyCombos[i].scanCode) {
                performSpecialKeyCombo(m_SpecialKeyCombos[i].keyCombo); return;
            }
        }
    }

    modifiers = 0;
    if (event->keysym.mod & KMOD_CTRL) modifiers |= MODIFIER_CTRL;
    if (event->keysym.mod & KMOD_ALT) modifiers |= MODIFIER_ALT;
    if (event->keysym.mod & KMOD_SHIFT) modifiers |= MODIFIER_SHIFT;
    if ((event->keysym.mod & KMOD_GUI) && isSystemKeyCaptureActive()) modifiers |= MODIFIER_META;

    keyCode = static_cast<short>(virtualKeyForScancode(event->keysym.scancode));
    if (!keyCode) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Unhandled scancode: %d", event->keysym.scancode);
        return;
    }

    if ((event->keysym.scancode == SDL_SCANCODE_LGUI || event->keysym.scancode == SDL_SCANCODE_RGUI) &&
            !isSystemKeyCaptureActive()) return;

    if (event->keysym.scancode == SDL_SCANCODE_RALT || event->keysym.scancode == SDL_SCANCODE_RCTRL ||
            event->keysym.scancode == SDL_SCANCODE_RSHIFT || event->keysym.scancode == SDL_SCANCODE_RGUI)
        keyCode = virtualKeyForScancode(event->keysym.scancode) == 0x5C ? 0x5C : keyCode + 1;

    if (event->keysym.scancode == SDL_SCANCODE_LGUI || event->keysym.scancode == SDL_SCANCODE_RGUI ||
            event->keysym.scancode == SDL_SCANCODE_INTERNATIONAL1 ||
            event->keysym.scancode == SDL_SCANCODE_INTERNATIONAL3)
        shouldNotConvertToScanCodeOnServer = true;

    LiSendKeyboardEvent2(0x8000 | keyCode,
                         event->state == SDL_PRESSED ? KEY_ACTION_DOWN : KEY_ACTION_UP,
                         modifiers, shouldNotConvertToScanCodeOnServer ? SS_KBE_FLAG_NON_NORMALIZED : 0);
    if (event->state == SDL_PRESSED) m_KeysDown.insert(keyCode);
    else m_KeysDown.remove(keyCode);
}
