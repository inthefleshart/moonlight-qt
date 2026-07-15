#include "input.h"

#include "streaming/session.h"

#include <Limelight.h>

#ifdef Q_OS_WIN32
#include "winpointer.h"
#endif

namespace {
bool isProfileToggle(const SDL_KeyboardEvent* event)
{
    return event->keysym.scancode == SDL_SCANCODE_P &&
           (event->keysym.mod & KMOD_CTRL) && (event->keysym.mod & KMOD_ALT) &&
           (event->keysym.mod & KMOD_SHIFT);
}

}

void SdlInputHandler::releaseRemoteInputForProfileChange()
{
    for (int slot = 0; slot < TabletMappingManager::SlotCount; ++slot) {
        if (m_TabletSourceActive[slot]) {
            const auto source = TabletMappingManager::get()->sourceForSlot(slot);
            if (source.valid) m_SuppressedTabletSourceKeyUps.insert(source.chord.virtualKey);
        }
    }

#ifdef Q_OS_WIN32
    if (m_NativePenBridge) m_NativePenBridge->cancelActivePen();
#endif
    LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL_ALL, 0, 0, 0, 0, 0, 0, 0);
    for (const char button : {BUTTON_LEFT, BUTTON_MIDDLE, BUTTON_RIGHT, BUTTON_X1, BUTTON_X2})
        LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, button);

    releaseTabletActions();
    raiseAllKeys();

    SDL_RemoveTimer(m_LongPressTimer);
    m_LongPressTimer = 0;
    SDL_RemoveTimer(m_LeftButtonReleaseTimer);
    m_LeftButtonReleaseTimer = 0;
    SDL_RemoveTimer(m_RightButtonReleaseTimer);
    m_RightButtonReleaseTimer = 0;
    SDL_RemoveTimer(m_DragTimer);
    m_DragTimer = 0;

    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        auto& state = m_GamepadState[i];
        if (!state.controller) continue;
        state.buttons = 0;
        state.lsX = state.lsY = state.rsX = state.rsY = 0;
        state.lt = state.rt = 0;
        sendGamepadState(&state);
    }
}

void SdlInputHandler::toggleProfileSelector()
{
    if (m_ProfileSelector.isOpen()) closeProfileSelector();
    else openProfileSelector();
}

void SdlInputHandler::openProfileSelector()
{
    if (m_ProfileSelector.isOpen()) return;
    SDL_RemoveTimer(m_ProfileToastTimer);
    m_ProfileToastTimer = 0;
    releaseRemoteInputForProfileChange();

    auto* manager = TabletMappingManager::get();
    m_ProfileSelector.open(manager->profiles(), manager->activeProfile(),
                           manager->favoriteProfiles());
    if (!m_ProfileSelector.isOpen()) return;
    m_ProfileSelectorPointerPressed = false;
    m_ProfileSelectorPressedRow = -1;
    updateProfileSelectorPointerReady();
#ifdef Q_OS_WIN32
    if (m_NativePenBridge) m_NativePenBridge->setLocalOverlayActive(true);
#endif
    SDL_ShowCursor(SDL_ENABLE);
    renderProfileSelector();
}

void SdlInputHandler::closeProfileSelector()
{
    if (!m_ProfileSelector.isOpen()) return;
    m_ProfileSelector.close();
    m_ProfileSelectorPointerPressed = false;
    m_ProfileSelectorPressedRow = -1;
    Session::get()->getOverlayManager().setOverlayState(Overlay::OverlayQuickKeyProfiles, false);
#ifdef Q_OS_WIN32
    if (m_NativePenBridge) m_NativePenBridge->setLocalOverlayActive(false);
#endif
}

void SdlInputHandler::applySelectedProfile()
{
    const QString profile = m_ProfileSelector.selectedProfile();
    if (profile.isEmpty()) return;
    releaseRemoteInputForProfileChange();
    closeProfileSelector();
    TabletMappingManager::get()->setActiveProfile(profile);
    showProfileToast(TabletMappingManager::get()->activeProfile());
}

void SdlInputHandler::cycleFavoriteProfile(int direction)
{
    releaseRemoteInputForProfileChange();
    const QString profile = TabletMappingManager::get()->cycleFavoriteProfile(direction);
    if (!profile.isEmpty()) showProfileToast(profile);
}

void SdlInputHandler::renderProfileSelector()
{
    if (!m_ProfileSelector.isOpen()) return;
    auto* manager = TabletMappingManager::get();
    Overlay::QuickKeyOverlayContent content;
    content.activeProfile = manager->activeProfile();
    content.selectedProfile = m_ProfileSelector.selectedProfile();
    content.firstVisibleIndex = m_ProfileSelector.firstVisibleIndex();
    content.totalProfiles = m_ProfileSelector.totalCount();
    SDL_GetWindowSize(m_Window, &content.viewportWidth, &content.viewportHeight);

    for (int row = 0; row < m_ProfileSelector.visibleCount(); ++row) {
        const QString profile = m_ProfileSelector.profileAtVisibleRow(row);
        content.profiles.append({profile,
                                 profile == content.selectedProfile,
                                 m_ProfileSelector.isActive(profile),
                                 m_ProfileSelector.isFavorite(profile)});
    }

    const auto actions = manager->actionsForProfile(content.selectedProfile);
    for (int slot = 0; slot < TabletMappingManager::SlotCount; ++slot) {
        const TabletSourceShortcut source = manager->sourceForSlot(slot);
        const TabletControlAction action = slot < actions.size() ? actions[slot] : TabletControlAction{};
        const QString displayText = action.displayText();
        QString details = displayText;
        const QString namePrefix = action.name + QStringLiteral(" — ");
        if (!action.name.isEmpty() && details.startsWith(namePrefix))
            details.remove(0, namePrefix.size());
        else if (details == action.name || action.kind == TabletActionKind::Disabled)
            details.clear();
        content.bindings.append({source.displayText(), action.name, details, action.modified, slot});
    }

    auto& overlay = Session::get()->getOverlayManager();
    overlay.updateQuickKeyOverlay(content);
    overlay.setOverlayState(Overlay::OverlayQuickKeyProfiles, true);
}

void SdlInputHandler::showProfileToast(const QString& profile)
{
    SDL_RemoveTimer(m_ProfileToastTimer);
    m_ProfileToastTimer = 0;
    const QByteArray text = QStringLiteral("QuickKey preset: %1").arg(profile).toUtf8();
    auto& overlay = Session::get()->getOverlayManager();
    int viewportWidth = 0;
    int viewportHeight = 0;
    SDL_GetWindowSize(m_Window, &viewportWidth, &viewportHeight);
    overlay.setQuickKeyViewportSize(viewportWidth, viewportHeight);
    overlay.updateOverlayText(Overlay::OverlayQuickKeyProfiles, text.constData());
    overlay.setOverlayState(Overlay::OverlayQuickKeyProfiles, true);
    m_ProfileToastShownAt = SDL_GetTicks();
    m_ProfileToastTimer = SDL_AddTimer(1500, profileToastTimerCallback, this);
}

void SdlInputHandler::hideProfileToast(Uint32 timerToken)
{
    if (static_cast<Sint32>(timerToken - m_ProfileToastShownAt) < 0) return;
    m_ProfileToastTimer = 0;
    if (!m_ProfileSelector.isOpen())
        Session::get()->getOverlayManager().setOverlayState(Overlay::OverlayQuickKeyProfiles, false);
}

Uint32 SdlInputHandler::profileToastTimerCallback(Uint32, void* context)
{
    SDL_Event event{};
    event.type = SDL_USEREVENT;
    event.user.code = SdlCodeHideProfileToast;
    event.user.data1 = context;
    event.user.data2 = reinterpret_cast<void*>(static_cast<uintptr_t>(SDL_GetTicks()));
    SDL_PushEvent(&event);
    return 0;
}

void SdlInputHandler::refreshProfileSelectorOverlay()
{
    if (m_ProfileSelector.isOpen()) renderProfileSelector();
}

void SdlInputHandler::updateProfileSelectorPointerReady()
{
    bool ready = SDL_GetMouseState(nullptr, nullptr) == 0;
#ifdef Q_OS_WIN32
    ready = ready && (!m_NativePenBridge || !m_NativePenBridge->isPenInContact());
#endif
    for (int i = 0; ready && i < SDL_GetNumTouchDevices(); ++i)
        ready = SDL_GetNumTouchFingers(SDL_GetTouchDevice(i)) == 0;
    m_ProfileSelectorPointerReady = ready;
}

int SdlInputHandler::profileSelectorRowAt(int x, int y) const
{
    int width = 0, height = 0;
    SDL_GetWindowSize(m_Window, &width, &height);
    auto& overlay = Session::get()->getOverlayManager();
    const int overlayWidth = overlay.getOverlayWidth(Overlay::OverlayQuickKeyProfiles);
    const int overlayHeight = overlay.getOverlayHeight(Overlay::OverlayQuickKeyProfiles);
    const int overlayLeft = (width - overlayWidth) / 2;
    const int overlayTop = (height - overlayHeight) / 2;
    return overlay.getQuickKeyProfileRowAt(x - overlayLeft, y - overlayTop);
}

bool SdlInputHandler::handleProfileSelectorKey(SDL_KeyboardEvent* event)
{
    const quint16 virtualKey = virtualKeyForScancode(event->keysym.scancode);
    if (event->state == SDL_RELEASED && m_SuppressedTabletSourceKeyUps.remove(virtualKey)) return true;
    if (event->state == SDL_RELEASED && m_SuppressedKeyUps.remove(event->keysym.scancode)) return true;

    if (isProfileToggle(event)) {
        if (event->state == SDL_PRESSED && !event->repeat) {
            m_SuppressedKeyUps.insert(SDL_SCANCODE_P);
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            for (const SDL_Scancode modifier : {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL,
                                                SDL_SCANCODE_LALT, SDL_SCANCODE_RALT,
                                                SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT}) {
                if (keys[modifier]) m_SuppressedKeyUps.insert(modifier);
            }
            toggleProfileSelector();
        }
        return true;
    }

    if (!m_ProfileSelector.isOpen()) return false;
    if (event->state != SDL_PRESSED || event->repeat) return true;
    if ((event->keysym.mod & KMOD_CTRL) && (event->keysym.mod & KMOD_ALT) &&
            (event->keysym.mod & KMOD_SHIFT) &&
            (event->keysym.scancode == SDL_SCANCODE_Q ||
             event->keysym.scancode == SDL_SCANCODE_E)) {
        releaseRemoteInputForProfileChange();
        closeProfileSelector();
        performSpecialKeyCombo(event->keysym.scancode == SDL_SCANCODE_Q ?
                                   KeyComboQuit : KeyComboQuitAndExit);
        return true;
    }
    switch (event->keysym.scancode) {
    case SDL_SCANCODE_ESCAPE:
        m_SuppressedKeyUps.insert(event->keysym.scancode);
        closeProfileSelector();
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: applySelectedProfile(); break;
    case SDL_SCANCODE_UP: m_ProfileSelector.moveSelection(-1); renderProfileSelector(); break;
    case SDL_SCANCODE_DOWN: m_ProfileSelector.moveSelection(1); renderProfileSelector(); break;
    case SDL_SCANCODE_PAGEUP: m_ProfileSelector.pageSelection(-1); renderProfileSelector(); break;
    case SDL_SCANCODE_PAGEDOWN: m_ProfileSelector.pageSelection(1); renderProfileSelector(); break;
    case SDL_SCANCODE_HOME: m_ProfileSelector.selectFirst(); renderProfileSelector(); break;
    case SDL_SCANCODE_END: m_ProfileSelector.selectLast(); renderProfileSelector(); break;
    default: break;
    }
    return true;
}

bool SdlInputHandler::handleProfileSelectorMouseButton(SDL_MouseButtonEvent* event)
{
    if (!m_ProfileSelector.isOpen()) return false;
    if (event->button != SDL_BUTTON_LEFT) return true;
    const int row = profileSelectorRowAt(event->x, event->y);
    if (event->state == SDL_PRESSED) {
        if (m_ProfileSelectorPointerReady) {
            m_ProfileSelectorPointerPressed = true;
            m_ProfileSelectorPressedRow = row;
            if (row >= 0 && m_ProfileSelector.setHoveredRow(row)) renderProfileSelector();
        }
    } else {
        if (!m_ProfileSelectorPointerReady) updateProfileSelectorPointerReady();
        else if (m_ProfileSelectorPointerPressed) {
            if (row >= 0 && row == m_ProfileSelectorPressedRow) applySelectedProfile();
            else closeProfileSelector();
        }
        m_ProfileSelectorPointerPressed = false;
        m_ProfileSelectorPressedRow = -1;
    }
    return true;
}

bool SdlInputHandler::handleProfileSelectorMouseMotion(SDL_MouseMotionEvent* event)
{
    if (!m_ProfileSelector.isOpen()) return false;
    const int row = profileSelectorRowAt(event->x, event->y);
    if (row >= 0 && m_ProfileSelector.setHoveredRow(row)) renderProfileSelector();
    return true;
}

bool SdlInputHandler::handleProfileSelectorMouseWheel(SDL_MouseWheelEvent* event)
{
    if (!m_ProfileSelector.isOpen()) return false;
    const float delta = event->preciseY != 0.0f ? event->preciseY : static_cast<float>(event->y);
    if (delta != 0.0f) {
        m_ProfileSelector.moveSelection(delta > 0 ? -1 : 1);
        renderProfileSelector();
    }
    return true;
}

bool SdlInputHandler::handleProfileSelectorTouch(SDL_TouchFingerEvent* event)
{
    if (!m_ProfileSelector.isOpen()) return false;
    int width = 0, height = 0;
    SDL_GetWindowSize(m_Window, &width, &height);
    const int row = profileSelectorRowAt(static_cast<int>(event->x * width),
                                         static_cast<int>(event->y * height));
    if (event->type == SDL_FINGERDOWN && m_ProfileSelectorPointerReady) {
        m_ProfileSelectorPointerPressed = true;
        m_ProfileSelectorPressedRow = row;
        if (row >= 0 && m_ProfileSelector.setHoveredRow(row)) renderProfileSelector();
    } else if (event->type == SDL_FINGERUP) {
        if (!m_ProfileSelectorPointerReady) updateProfileSelectorPointerReady();
        else if (m_ProfileSelectorPointerPressed) {
            if (row >= 0 && row == m_ProfileSelectorPressedRow) applySelectedProfile();
            else closeProfileSelector();
        }
        m_ProfileSelectorPointerPressed = false;
        m_ProfileSelectorPressedRow = -1;
    }
    return true;
}

bool SdlInputHandler::handleProfileSelectorControllerButton(SDL_ControllerButtonEvent* event)
{
    if (!m_ProfileSelector.isOpen()) return false;
    if (event->state != SDL_PRESSED) return true;
    switch (event->button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: m_ProfileSelector.moveSelection(-1); renderProfileSelector(); break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: m_ProfileSelector.moveSelection(1); renderProfileSelector(); break;
    case SDL_CONTROLLER_BUTTON_A: applySelectedProfile(); break;
    case SDL_CONTROLLER_BUTTON_B: closeProfileSelector(); break;
    default: break;
    }
    return true;
}

#ifdef Q_OS_WIN32
bool SdlInputHandler::nativeLocalPointerCallback(void* context, uint8_t eventType, float x,
                                                  float y, bool inContact)
{
    return static_cast<SdlInputHandler*>(context)->handleNativeLocalPointer(
        eventType, x, y, inContact);
}

bool SdlInputHandler::handleNativeLocalPointer(uint8_t eventType, float x, float y, bool inContact)
{
    if (!m_ProfileSelector.isOpen()) return false;
    int width = 0, height = 0;
    SDL_GetWindowSize(m_Window, &width, &height);
    const int row = profileSelectorRowAt(static_cast<int>(x * width), static_cast<int>(y * height));
    if (!inContact && !m_ProfileSelectorPointerReady) m_ProfileSelectorPointerReady = true;
    if (eventType == LI_TOUCH_EVENT_DOWN && m_ProfileSelectorPointerReady) {
        m_ProfileSelectorPointerPressed = true;
        m_ProfileSelectorPressedRow = row;
        if (row >= 0 && m_ProfileSelector.setHoveredRow(row)) renderProfileSelector();
    } else if (eventType == LI_TOUCH_EVENT_UP) {
        if (m_ProfileSelectorPointerReady && m_ProfileSelectorPointerPressed) {
            if (row >= 0 && row == m_ProfileSelectorPressedRow) applySelectedProfile();
            else closeProfileSelector();
        }
        m_ProfileSelectorPointerReady = true;
        m_ProfileSelectorPointerPressed = false;
        m_ProfileSelectorPressedRow = -1;
    } else if (!inContact && row >= 0) {
        if (m_ProfileSelector.setHoveredRow(row)) renderProfileSelector();
    }
    return true;
}
#endif
