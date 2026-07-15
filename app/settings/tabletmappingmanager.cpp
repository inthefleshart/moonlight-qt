#include "tabletmappingmanager.h"

#include <QGlobalStatic>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QSettings>
#include <QSet>

#include <algorithm>

namespace {
Q_GLOBAL_STATIC(QReadWriteLock, s_TabletMappingLock)
TabletMappingManager* s_TabletMappingManager = nullptr;

constexpr int kSettingsSchema = 2;

QJsonObject strokeToJson(const TabletKeyStroke& stroke)
{
    return {{"key", stroke.virtualKey}, {"control", stroke.control}, {"alt", stroke.alt},
            {"shift", stroke.shift}, {"meta", stroke.meta}};
}

TabletKeyStroke strokeFromJson(const QJsonObject& object)
{
    TabletKeyStroke stroke;
    stroke.virtualKey = static_cast<quint16>(object.value("key").toInt());
    stroke.control = object.value("control").toBool();
    stroke.alt = object.value("alt").toBool();
    stroke.shift = object.value("shift").toBool();
    stroke.meta = object.value("meta").toBool();
    return stroke;
}

QByteArray actionToJson(const TabletControlAction& action)
{
    QJsonArray sequence;
    for (const auto& stroke : action.sequence) {
        sequence.append(strokeToJson(stroke));
    }
    QJsonObject object{{"schema", kSettingsSchema},
                       {"kind", static_cast<int>(action.kind)},
                       {"activation", static_cast<int>(action.activation)},
                       {"name", action.name},
                       {"chord", strokeToJson(action.chord)},
                       {"sequence", sequence},
                       {"mouseButton", action.mouseButton},
                       {"wheelDelta", action.wheelDelta},
                       {"localAction", static_cast<int>(action.localAction)}};
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

TabletControlAction actionFromJson(const QByteArray& json, bool* ok)
{
    TabletControlAction action;
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        *ok = false;
        return action;
    }
    const auto object = document.object();
    const int kind = object.value("kind").toInt(-1);
    const int activation = object.value("activation").toInt(-1);
    if (object.value("schema").toInt() != kSettingsSchema ||
            kind < static_cast<int>(TabletActionKind::Disabled) ||
            kind > static_cast<int>(TabletActionKind::WacomRadialChord) ||
            activation < static_cast<int>(TabletActivation::Tap) ||
            activation > static_cast<int>(TabletActivation::Repeat)) {
        *ok = false;
        return action;
    }

    action.kind = static_cast<TabletActionKind>(kind);
    action.activation = static_cast<TabletActivation>(activation);
    action.name = object.value("name").toString();
    action.chord = strokeFromJson(object.value("chord").toObject());
    for (const auto& value : object.value("sequence").toArray()) {
        if (action.sequence.size() >= 32) {
            break;
        }
        action.sequence.append(strokeFromJson(value.toObject()));
    }
    action.mouseButton = std::clamp(object.value("mouseButton").toInt(),
                                    static_cast<int>(TabletMouseNone),
                                    static_cast<int>(TabletMouseX2));
    action.wheelDelta = std::clamp(object.value("wheelDelta").toInt(), -1200, 1200);
    action.localAction = static_cast<TabletLocalAction>(std::clamp(
        object.value("localAction").toInt(), static_cast<int>(TabletLocalAction::None),
        static_cast<int>(TabletLocalAction::ResetStuckInput)));
    action.modified = true;
    *ok = action.valid() || action.kind == TabletActionKind::Disabled;
    return action;
}

QString virtualKeyText(quint16 key)
{
    if (key >= 0x41 && key <= 0x5A) return QString(QChar('A' + key - 0x41));
    if (key >= 0x30 && key <= 0x39) return QString(QChar('0' + key - 0x30));
    if (key >= 0x70 && key <= 0x87) return QString("F%1").arg(key - 0x70 + 1);
    switch (key) {
    case 0x08: return "Backspace"; case 0x09: return "Tab"; case 0x0D: return "Enter";
    case 0x1B: return "Esc"; case 0x20: return "Space"; case 0x21: return "Page Up";
    case 0x22: return "Page Down"; case 0x23: return "End"; case 0x24: return "Home";
    case 0x25: return "Left"; case 0x26: return "Up"; case 0x27: return "Right";
    case 0x28: return "Down"; case 0x2D: return "Insert"; case 0x2E: return "Delete";
    case 0x60: return "Numpad 0"; case 0x61: return "Numpad 1";
    case 0x62: return "Numpad 2"; case 0x63: return "Numpad 3";
    case 0x64: return "Numpad 4"; case 0x65: return "Numpad 5";
    case 0x66: return "Numpad 6"; case 0x67: return "Numpad 7";
    case 0x68: return "Numpad 8"; case 0x69: return "Numpad 9";
    case 0x6E: return "Numpad Decimal"; case 0xA0: return "Shift";
    case 0xA2: return "Ctrl"; case 0xA4: return "Alt"; case 0x5B: return "Win";
    case 0xBA: return ";"; case 0xBB: return "="; case 0xBC: return ",";
    case 0xBD: return "-"; case 0xBE: return "."; case 0xBF: return "/";
    case 0xC0: return "`"; case 0xDB: return "["; case 0xDC: return "\\";
    case 0xDD: return "]"; case 0xDE: return "'";
    default: return {};
    }
}
}

bool TabletControlAction::valid() const
{
    switch (kind) {
    case TabletActionKind::Disabled: return false;
    case TabletActionKind::PassThrough: return true;
    case TabletActionKind::KeyChord:
    case TabletActionKind::WacomRadialChord:
        return chord.virtualKey != 0 || chord.control || chord.alt || chord.shift || chord.meta;
    case TabletActionKind::KeySequence: return !sequence.isEmpty();
    case TabletActionKind::PenGesture:
        return chord.virtualKey != 0 || chord.control || chord.alt || chord.shift || chord.meta ||
               mouseButton != TabletMouseNone;
    case TabletActionKind::MouseWheel: return wheelDelta != 0;
    case TabletActionKind::LocalAction: return localAction != TabletLocalAction::None;
    }
    return false;
}

QString TabletControlAction::displayText() const
{
    if (kind == TabletActionKind::Disabled) return QStringLiteral("Unassigned");
    if (kind == TabletActionKind::PassThrough) return QStringLiteral("Pass through");
    QString details;
    if (kind == TabletActionKind::KeyChord || kind == TabletActionKind::WacomRadialChord) {
        details = TabletMappingManager::keyStrokeText(chord);
        if (activation == TabletActivation::Hold && !details.isEmpty()) details += " (hold)";
    }
    if (kind == TabletActionKind::KeySequence) {
        QStringList steps;
        for (const auto& step : sequence) steps.append(TabletMappingManager::keyStrokeText(step));
        details = steps.join(QStringLiteral(" → "));
    }
    if (kind == TabletActionKind::PenGesture) {
        details = TabletMappingManager::keyStrokeText(chord);
        const QString button = mouseButton == TabletMouseLeft ? "Left Mouse" :
                               mouseButton == TabletMouseMiddle ? "Middle Mouse" :
                               mouseButton == TabletMouseRight ? "Right Mouse" :
                               mouseButton == TabletMouseX1 ? "Mouse X1" :
                               mouseButton == TabletMouseX2 ? "Mouse X2" : QString();
        if (!button.isEmpty()) details += (details.isEmpty() ? "" : " + ") + button;
        details += (details.isEmpty() ? "" : " + ") + QStringLiteral("Pen Drag");
    }
    if (details.isEmpty()) return name.isEmpty() ? QStringLiteral("Configured action") : name;
    return name.isEmpty() || name == details ? details : name + QStringLiteral(" — ") + details;
}

QString TabletSourceShortcut::displayText() const
{
    return valid ? TabletMappingManager::keyStrokeText(chord) : QStringLiteral("Learn source");
}

TabletMappingManager::TabletMappingManager(QObject* parent)
    : QAbstractListModel(parent), m_Actions(SlotCount), m_Sources(SlotCount)
{
    buildShippedProfiles();
    migrateLegacySettings();

    QSettings settings;
    m_ActiveProfile = settings.value("tabletMappings/v2/activeProfile", "Default").toString();
    if (!m_ProfileOrder.contains(m_ActiveProfile)) m_ActiveProfile = "Default";

    for (int slot = 0; slot < SlotCount; ++slot) {
        const QString configured = settings.value(sourceSettingsKey(slot)).toString();
        if (!configured.isEmpty()) {
            const auto stroke = parseStroke(configured);
            m_Sources[slot] = {stroke, stroke.virtualKey != 0};
        } else {
            TabletKeyStroke stroke;
            stroke.virtualKey = static_cast<quint16>(0x70 + slot); // F1-F10
            m_Sources[slot] = {stroke, true};
        }
    }
    loadActiveProfile();
}

TabletMappingManager* TabletMappingManager::get(QQmlEngine* engine)
{
    QWriteLocker guard(s_TabletMappingLock);
    if (!s_TabletMappingManager) s_TabletMappingManager = new TabletMappingManager(engine);
    if (engine) QJSEngine::setObjectOwnership(s_TabletMappingManager, QJSEngine::CppOwnership);
    return s_TabletMappingManager;
}

int TabletMappingManager::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : SlotCount;
}

QVariant TabletMappingManager::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= SlotCount) return {};
    QReadLocker guard(&m_Lock);
    const auto& action = m_Actions[index.row()];
    const auto& source = m_Sources[index.row()];
    switch (role) {
    case SlotRole: return index.row();
    case SourceTextRole: return source.displayText();
    case ActionNameRole: return action.name;
    case ActionTextRole: return action.displayText();
    case ActionKindRole: return static_cast<int>(action.kind);
    case ActivationRole: return static_cast<int>(action.activation);
    case ModifiedRole: return action.modified;
    case SetupRequiredRole:
        return action.kind == TabletActionKind::WacomRadialChord && !action.chord.virtualKey;
    default: return {};
    }
}

QHash<int, QByteArray> TabletMappingManager::roleNames() const
{
    return {{SlotRole, "slot"}, {SourceTextRole, "sourceText"}, {ActionNameRole, "actionName"},
            {ActionTextRole, "actionText"}, {ActionKindRole, "actionKind"},
            {ActivationRole, "activation"}, {ModifiedRole, "userModified"},
            {SetupRequiredRole, "setupRequired"}};
}

QStringList TabletMappingManager::profiles() const { return m_ProfileOrder; }
QString TabletMappingManager::activeProfile() const { return m_ActiveProfile; }

void TabletMappingManager::setActiveProfile(const QString& profile)
{
    if (!m_ProfileOrder.contains(profile) || profile == m_ActiveProfile) return;
    beginResetModel();
    m_ActiveProfile = profile;
    QSettings().setValue("tabletMappings/v2/activeProfile", profile);
    loadActiveProfile();
    endResetModel();
    emit activeProfileChanged();
    emit bindingsChanged();
}

QString TabletMappingManager::binding(int slot) const
{
    return actionForSlot(slot).displayText();
}

QString TabletMappingManager::sourceBinding(int slot) const
{
    return sourceForSlot(slot).displayText();
}

bool TabletMappingManager::setBinding(int slot, const QString& text)
{
    if (slot < 0 || slot >= SlotCount) return false;
    if (text.trimmed().isEmpty()) { clearBinding(slot); return true; }
    const auto stroke = parseStroke(text);
    if (!stroke.virtualKey) return false;
    TabletControlAction action;
    action.kind = TabletActionKind::KeyChord;
    action.activation = TabletActivation::Tap;
    action.name = keyStrokeText(stroke);
    action.chord = stroke;
    action.modified = true;
    storeAction(slot, action);
    return true;
}

bool TabletMappingManager::setSourceBinding(int slot, const QString& text)
{
    if (slot < 0 || slot >= SlotCount) return false;
    const auto stroke = parseStroke(text);
    if (!text.trimmed().isEmpty() && !stroke.virtualKey) return false;
    {
        QWriteLocker guard(&m_Lock);
        m_Sources[slot] = {stroke, stroke.virtualKey != 0};
    }
    QSettings settings;
    if (stroke.virtualKey) settings.setValue(sourceSettingsKey(slot), keyStrokeText(stroke));
    else settings.remove(sourceSettingsKey(slot));
    notifyRowChanged(slot);
    return true;
}

bool TabletMappingManager::recordKey(int slot, int qtKey, int qtModifiers, bool source, bool hold)
{
    TabletKeyStroke stroke;
    stroke.virtualKey = qtKeyToVirtualKey(qtKey);
    if (!stroke.virtualKey) return false;
    const auto modifiers = static_cast<Qt::KeyboardModifiers>(qtModifiers);
    stroke.control = modifiers.testFlag(Qt::ControlModifier) && stroke.virtualKey != 0xA2;
    stroke.alt = modifiers.testFlag(Qt::AltModifier) && stroke.virtualKey != 0xA4;
    stroke.shift = modifiers.testFlag(Qt::ShiftModifier) && stroke.virtualKey != 0xA0;
    stroke.meta = modifiers.testFlag(Qt::MetaModifier) && stroke.virtualKey != 0x5B;
    if (source) return setSourceBinding(slot, keyStrokeText(stroke));

    TabletControlAction action;
    const auto current = actionForSlot(slot);
    action.kind = current.kind == TabletActionKind::WacomRadialChord ?
                      TabletActionKind::WacomRadialChord : TabletActionKind::KeyChord;
    action.activation = hold ? TabletActivation::Hold : TabletActivation::Tap;
    action.chord = stroke;
    action.name = action.kind == TabletActionKind::WacomRadialChord ?
                      QStringLiteral("Wacom Radial Menu") : keyStrokeText(stroke);
    action.modified = true;
    storeAction(slot, action);
    return true;
}

QString TabletMappingManager::recordedKeyText(int qtKey, int qtModifiers) const
{
    TabletKeyStroke stroke;
    stroke.virtualKey = qtKeyToVirtualKey(qtKey);
    if (!stroke.virtualKey) return {};
    const auto modifiers = static_cast<Qt::KeyboardModifiers>(qtModifiers);
    stroke.control = modifiers.testFlag(Qt::ControlModifier) && stroke.virtualKey != 0xA2;
    stroke.alt = modifiers.testFlag(Qt::AltModifier) && stroke.virtualKey != 0xA4;
    stroke.shift = modifiers.testFlag(Qt::ShiftModifier) && stroke.virtualKey != 0xA0;
    stroke.meta = modifiers.testFlag(Qt::MetaModifier) && stroke.virtualKey != 0x5B;
    return keyStrokeText(stroke);
}

bool TabletMappingManager::setActionOption(int slot, const QString& id)
{
    TabletControlAction action;
    if (id == "disabled") action = {};
    else if (id == "pass") { action.kind = TabletActionKind::PassThrough; action.name = "Pass through"; }
    else if (id == "touch") action = local("Touch forwarding on/off", TabletLocalAction::ToggleTouch);
    else if (id == "cycleTouch") action = local("Cycle touch policy", TabletLocalAction::CycleTouchPolicy);
    else if (id == "diagnostics") action = local("Toggle diagnostics", TabletLocalAction::ToggleDiagnostics);
    else if (id == "reset") action = local("Reset stuck input", TabletLocalAction::ResetStuckInput);
    else if (id == "explorer") action = chord("File Explorer", "Meta+E");
    else if (id == "win10Action") action = chord("Windows 10 Action Center", "Meta+A");
    else if (id == "win11Quick") action = chord("Windows 11 Quick Settings", "Meta+A");
    else if (id == "win11Notifications") action = chord("Windows 11 Notifications", "Meta+N");
    else if (id == "osk") action = chord("On-Screen Keyboard", "Meta+Ctrl+O");
    else if (id == "desktop") action = chord("Show desktop", "Meta+D");
    else if (id == "taskNext") action = chord("Next application", "Alt+Tab");
    else if (id == "taskPrevious") action = chord("Previous application", "Alt+Shift+Tab");
    else if (id == "middleGesture") action = gesture("Middle-click pen gesture", {}, TabletMouseMiddle);
    else if (id == "rightGesture") action = gesture("Right-click pen gesture", {}, TabletMouseRight);
    else if (id == "radial") action = radial();
    else return false;
    action.modified = true;
    storeAction(slot, action);
    return true;
}

bool TabletMappingManager::setPenGesture(int slot, int qtKey, int qtModifiers, int mouseButton,
                                         const QString& label)
{
    if (slot < 0 || slot >= SlotCount || mouseButton < TabletMouseNone || mouseButton > TabletMouseX2)
        return false;
    TabletControlAction action;
    action.kind = TabletActionKind::PenGesture;
    action.activation = TabletActivation::Hold;
    action.name = label.trimmed();
    action.chord.virtualKey = qtKeyToVirtualKey(qtKey);
    const auto modifiers = static_cast<Qt::KeyboardModifiers>(qtModifiers);
    action.chord.control = modifiers.testFlag(Qt::ControlModifier);
    action.chord.alt = modifiers.testFlag(Qt::AltModifier);
    action.chord.shift = modifiers.testFlag(Qt::ShiftModifier);
    action.chord.meta = modifiers.testFlag(Qt::MetaModifier);
    action.mouseButton = mouseButton;
    action.modified = true;
    if (!action.valid()) return false;
    if (action.name.isEmpty()) action.name = action.displayText();
    storeAction(slot, action);
    return true;
}

bool TabletMappingManager::setKeySequence(int slot, const QStringList& portableKeys,
                                           const QString& label)
{
    if (slot < 0 || slot >= SlotCount || portableKeys.isEmpty() || portableKeys.size() > 32)
        return false;
    TabletControlAction action;
    action.kind = TabletActionKind::KeySequence;
    action.name = label.trimmed();
    for (const auto& key : portableKeys) {
        const auto stroke = parseStroke(key);
        if (!stroke.virtualKey) return false;
        action.sequence.append(stroke);
    }
    if (action.name.isEmpty()) action.name = portableKeys.join(", ");
    action.modified = true;
    storeAction(slot, action);
    return true;
}

void TabletMappingManager::clearBinding(int slot)
{
    if (slot < 0 || slot >= SlotCount) return;
    TabletControlAction action;
    action.modified = true;
    storeAction(slot, action);
}

void TabletMappingManager::resetBinding(int slot)
{
    if (slot < 0 || slot >= SlotCount) return;
    QSettings().remove(actionSettingsKey(slot));
    {
        QWriteLocker guard(&m_Lock);
        m_Actions[slot] = shippedAction(slot);
    }
    notifyRowChanged(slot);
}

void TabletMappingManager::resetActiveProfile()
{
    QSettings settings;
    settings.remove(QString("tabletMappings/v2/profiles/%1").arg(profileIdForName(m_ActiveProfile)));
    beginResetModel();
    loadActiveProfile();
    endResetModel();
    emit bindingsChanged();
}

bool TabletMappingManager::isValidBinding(const QString& text) const
{
    return parseStroke(text).virtualKey != 0;
}

bool TabletMappingManager::duplicateBindings() const
{
    QReadLocker guard(&m_Lock);
    QSet<QString> actions;
    QSet<QString> sources;
    for (int slot = 0; slot < SlotCount; ++slot) {
        if (m_Sources[slot].valid) {
            const QString source = m_Sources[slot].displayText().toLower();
            if (sources.contains(source)) return true;
            sources.insert(source);
        }
        if (m_Actions[slot].valid()) {
            const QString action = m_Actions[slot].displayText().toLower();
            if (actions.contains(action)) return true;
            actions.insert(action);
        }
    }
    return false;
}

QVariantList TabletMappingManager::actionOptions() const
{
    const QList<QPair<QString, QString>> options = {
        {"disabled", "Unassigned"}, {"pass", "Pass through"},
        {"touch", "Touch forwarding on/off"}, {"cycleTouch", "Cycle touch policy"},
        {"diagnostics", "Toggle diagnostics"}, {"reset", "Reset stuck input"},
        {"middleGesture", "Middle-click pen gesture"}, {"rightGesture", "Right-click pen gesture"},
        {"explorer", "File Explorer"}, {"win10Action", "Windows 10 Action Center"},
        {"win11Quick", "Windows 11 Quick Settings"},
        {"win11Notifications", "Windows 11 Notifications"},
        {"osk", "On-Screen Keyboard"}, {"desktop", "Show desktop"},
        {"taskNext", "Next application"}, {"taskPrevious", "Previous application"},
        {"radial", "Wacom Radial Menu (setup required)"},
    };
    QVariantList result;
    for (const auto& option : options) {
        result.append(QVariantMap{{"id", option.first}, {"text", option.second}});
    }
    return result;
}

TabletControlAction TabletMappingManager::actionForSlot(int slot) const
{
    if (slot < 0 || slot >= SlotCount) return {};
    QReadLocker guard(&m_Lock);
    return m_Actions[slot];
}

TabletSourceShortcut TabletMappingManager::sourceForSlot(int slot) const
{
    if (slot < 0 || slot >= SlotCount) return {};
    QReadLocker guard(&m_Lock);
    return m_Sources[slot];
}

int TabletMappingManager::slotForSource(quint16 key, bool control, bool alt, bool shift, bool meta) const
{
    QReadLocker guard(&m_Lock);
    for (int slot = 0; slot < SlotCount; ++slot) {
        const auto& source = m_Sources[slot];
        if (source.valid && source.chord.virtualKey == key && source.chord.control == control &&
                source.chord.alt == alt && source.chord.shift == shift && source.chord.meta == meta)
            return slot;
    }
    return -1;
}

quint16 TabletMappingManager::qtKeyToVirtualKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return static_cast<quint16>(0x41 + key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return static_cast<quint16>(0x30 + key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return static_cast<quint16>(0x70 + key - Qt::Key_F1);
    switch (key) {
    case Qt::Key_Backspace: return 0x08; case Qt::Key_Tab: return 0x09;
    case Qt::Key_Return: case Qt::Key_Enter: return 0x0D; case Qt::Key_Escape: return 0x1B;
    case Qt::Key_Space: return 0x20; case Qt::Key_PageUp: return 0x21;
    case Qt::Key_PageDown: return 0x22; case Qt::Key_End: return 0x23; case Qt::Key_Home: return 0x24;
    case Qt::Key_Left: return 0x25; case Qt::Key_Up: return 0x26; case Qt::Key_Right: return 0x27;
    case Qt::Key_Down: return 0x28; case Qt::Key_Insert: return 0x2D; case Qt::Key_Delete: return 0x2E;
    case Qt::Key_Control: return 0xA2; case Qt::Key_Alt: return 0xA4;
    case Qt::Key_Shift: return 0xA0; case Qt::Key_Meta: return 0x5B;
    case Qt::Key_Semicolon: return 0xBA; case Qt::Key_Equal: return 0xBB;
    case Qt::Key_Comma: return 0xBC; case Qt::Key_Minus: return 0xBD;
    case Qt::Key_Period: return 0xBE; case Qt::Key_Slash: return 0xBF;
    case Qt::Key_QuoteLeft: return 0xC0; case Qt::Key_BracketLeft: return 0xDB;
    case Qt::Key_Backslash: return 0xDC; case Qt::Key_BracketRight: return 0xDD;
    case Qt::Key_Apostrophe: return 0xDE;
    default: return 0;
    }
}

QString TabletMappingManager::keyStrokeText(const TabletKeyStroke& stroke)
{
    QStringList parts;
    if (stroke.control) parts << "Ctrl";
    if (stroke.alt) parts << "Alt";
    if (stroke.shift) parts << "Shift";
    if (stroke.meta) parts << "Meta";
    const QString key = virtualKeyText(stroke.virtualKey);
    if (!key.isEmpty() && !(stroke.virtualKey == 0xA2 && stroke.control) &&
            !(stroke.virtualKey == 0xA4 && stroke.alt) &&
            !(stroke.virtualKey == 0xA0 && stroke.shift) &&
            !(stroke.virtualKey == 0x5B && stroke.meta)) parts << key;
    return parts.join('+');
}

TabletKeyStroke TabletMappingManager::parseStroke(const QString& text)
{
    const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (sequence.count() == 1) {
        const auto combination = sequence[0];
        TabletKeyStroke stroke;
        stroke.virtualKey = qtKeyToVirtualKey(combination.key());
        stroke.control = combination.keyboardModifiers().testFlag(Qt::ControlModifier);
        stroke.alt = combination.keyboardModifiers().testFlag(Qt::AltModifier);
        stroke.shift = combination.keyboardModifiers().testFlag(Qt::ShiftModifier);
        stroke.meta = combination.keyboardModifiers().testFlag(Qt::MetaModifier);
        if (stroke.virtualKey) return stroke;
    }

    TabletKeyStroke modifiersOnly;
    const QStringList parts = text.split('+', Qt::SkipEmptyParts);
    for (const QString& rawPart : parts) {
        const QString part = rawPart.trimmed().toLower();
        if (part == "ctrl" || part == "control") modifiersOnly.control = true;
        else if (part == "alt") modifiersOnly.alt = true;
        else if (part == "shift") modifiersOnly.shift = true;
        else if (part == "meta" || part == "win" || part == "windows") modifiersOnly.meta = true;
        else return {};
    }
    const int modifierCount = static_cast<int>(modifiersOnly.control) +
                              static_cast<int>(modifiersOnly.alt) +
                              static_cast<int>(modifiersOnly.shift) +
                              static_cast<int>(modifiersOnly.meta);
    if (modifierCount == 1) {
        if (modifiersOnly.control) { modifiersOnly.virtualKey = 0xA2; modifiersOnly.control = false; }
        else if (modifiersOnly.alt) { modifiersOnly.virtualKey = 0xA4; modifiersOnly.alt = false; }
        else if (modifiersOnly.shift) { modifiersOnly.virtualKey = 0xA0; modifiersOnly.shift = false; }
        else { modifiersOnly.virtualKey = 0x5B; modifiersOnly.meta = false; }
    }
    return modifierCount > 0 ? modifiersOnly : TabletKeyStroke{};
}

TabletControlAction TabletMappingManager::chord(const QString& name, const QString& text,
                                                 TabletActivation activation)
{
    TabletControlAction action;
    action.kind = TabletActionKind::KeyChord;
    action.activation = activation;
    action.name = name;
    action.chord = parseStroke(text);
    return action;
}

TabletControlAction TabletMappingManager::sequence(const QString& name, const QStringList& keys)
{
    TabletControlAction action;
    action.kind = TabletActionKind::KeySequence;
    action.name = name;
    for (const auto& key : keys) action.sequence.append(parseStroke(key));
    return action;
}

TabletControlAction TabletMappingManager::gesture(const QString& name, const QString& keys,
                                                   int mouseButton)
{
    TabletControlAction action;
    action.kind = TabletActionKind::PenGesture;
    action.activation = TabletActivation::Hold;
    action.name = name;
    if (!keys.isEmpty()) action.chord = parseStroke(keys);
    action.mouseButton = mouseButton;
    return action;
}

TabletControlAction TabletMappingManager::local(const QString& name, TabletLocalAction localAction)
{
    TabletControlAction action;
    action.kind = TabletActionKind::LocalAction;
    action.name = name;
    action.localAction = localAction;
    return action;
}

TabletControlAction TabletMappingManager::radial()
{
    TabletControlAction action;
    action.kind = TabletActionKind::WacomRadialChord;
    action.name = "Wacom Radial Menu — setup required";
    return action;
}

void TabletMappingManager::buildShippedProfiles()
{
    auto add = [this](const QString& name, const QString& id,
                      std::initializer_list<TabletControlAction> actions) {
        QVector<TabletControlAction> profile(SlotCount);
        int slot = 0;
        for (const auto& action : actions) {
            if (slot >= SlotCount) break;
            profile[slot++] = action;
        }
        m_ProfileOrder.append(name);
        m_ProfileIds.insert(name, id);
        m_ShippedProfiles.insert(name, profile);
    };

    add("Default", "default", {chord("Undo", "Ctrl+Z"), chord("Redo", "Ctrl+Y"),
        chord("Save", "Ctrl+S"), chord("Cut", "Ctrl+X"), chord("Copy", "Ctrl+C"),
        chord("Paste", "Ctrl+V"), chord("File Explorer", "Meta+E"),
        chord("Next application", "Alt+Tab"), chord("On-Screen Keyboard", "Meta+Ctrl+O"),
        chord("Show desktop", "Meta+D")});
    add("Blank / Pass-Through", "blank", {});
    add("Krita", "krita", {chord("Undo", "Ctrl+Z"), chord("Redo", "Ctrl+Shift+Z"),
        chord("Save", "Ctrl+S"), chord("Freehand Brush", "B"), chord("Eraser", "E"),
        chord("Color Picker", "Ctrl", TabletActivation::Hold), chord("Decrease brush size", "["),
        chord("Increase brush size", "]"), gesture("Pan", "Space", TabletMouseLeft),
        chord("Canvas only", "Tab"), chord("Mirror canvas", "M")});
    add("Photoshop", "photoshop", {chord("Undo", "Ctrl+Z"), chord("Redo", "Ctrl+Shift+Z"),
        chord("Save", "Ctrl+S"), chord("Brush", "B"), chord("Eraser", "E"),
        chord("Eyedropper", "Alt", TabletActivation::Hold), chord("Decrease brush size", "["),
        chord("Increase brush size", "]"), gesture("Pan", "Space", TabletMouseLeft),
        chord("Fit on screen", "Ctrl+0"), chord("Hide extras", "Ctrl+H")});
    add("Substance 3D Painter", "substance", {chord("Undo", "Ctrl+Z"),
        chord("Redo", "Ctrl+Y"), chord("Save", "Ctrl+S"), chord("Paint", "1"),
        chord("Eraser", "2"), chord("Color Picker", "P"),
        gesture("Orbit", "Alt", TabletMouseLeft), gesture("Pan", "Alt", TabletMouseMiddle),
        gesture("Zoom", "Alt", TabletMouseRight), chord("Focus", "F")});
    add("ZBrush — Right-Click Navigation", "zbrush", {chord("Undo", "Ctrl+Z"),
        chord("Redo", "Ctrl+Shift+Z"), chord("Save Project", "Ctrl+S"),
        chord("Quick Menu", "Space"), chord("Temporary Smooth", "Shift", TabletActivation::Hold),
        chord("Invert brush", "Alt", TabletActivation::Hold), gesture("Rotate", {}, TabletMouseRight),
        gesture("Pan", "Alt", TabletMouseRight), gesture("Scale / Zoom", "Ctrl", TabletMouseRight),
        chord("Frame", "F")});
    add("Mudbox", "mudbox", {chord("Undo", "Ctrl+Z"), chord("Redo", "Ctrl+Y"),
        chord("Save", "Ctrl+S"), gesture("Brush size", "B", TabletMouseLeft),
        gesture("Brush strength", "M", TabletMouseLeft), chord("Temporary Smooth", "Shift", TabletActivation::Hold),
        chord("Invert sculpting", "Ctrl", TabletActivation::Hold), gesture("Orbit", "Alt", TabletMouseLeft),
        gesture("Track / Pan", "Alt", TabletMouseMiddle), gesture("Dolly", "Alt", TabletMouseRight),
        gesture("Camera roll", "Alt+Shift", TabletMouseMiddle),
        gesture("Camera 2D pan", "Ctrl+Alt", TabletMouseMiddle)});
    add("Mari", "mari", {chord("Undo", "Ctrl+Z"), chord("Redo", "Ctrl+Shift+Z"),
        chord("Save", "Ctrl+S"), chord("Paint", "P"), chord("Eraser", "E"),
        chord("Color Picker", "C"),
        gesture("Orbit", "Alt", TabletMouseLeft), gesture("Pan", "Alt", TabletMouseMiddle),
        gesture("Zoom", "Alt", TabletMouseRight), chord("Focus", "A")});
    add("Daz Studio — Keyboard Navigation", "daz", {chord("Undo", "Ctrl+Z"),
        chord("Redo", "Ctrl+Y"), chord("Save", "Ctrl+S"), chord("Move forward", "W", TabletActivation::Hold),
        chord("Move backward", "S", TabletActivation::Hold), chord("Move left", "A", TabletActivation::Hold),
        chord("Move right", "D", TabletActivation::Hold), chord("Move up", "E", TabletActivation::Hold),
        chord("Move down", "Q", TabletActivation::Hold), chord("Look up", "I", TabletActivation::Hold),
        chord("Look down", "K", TabletActivation::Hold), chord("Look left", "J", TabletActivation::Hold),
        chord("Look right", "L", TabletActivation::Hold), chord("Bank left", "U", TabletActivation::Hold),
        chord("Bank right", "O", TabletActivation::Hold)});
    add("3DCoat — Default Navigation", "3dcoat", {chord("Undo", "Ctrl+Z"),
        chord("Redo", "Ctrl+Y"), chord("Save", "Ctrl+S"), chord("Tool / color popup", "Space"),
        chord("Temporary Smooth", "Shift", TabletActivation::Hold),
        chord("Alternate operation", "Ctrl", TabletActivation::Hold), gesture("Orbit", "Alt", TabletMouseLeft),
        gesture("Pan", "Alt", TabletMouseMiddle), gesture("Zoom", "Alt", TabletMouseRight),
        chord("Set navigation pivot", "F")});
    add("Maya", "maya", {chord("Undo", "Ctrl+Z"), chord("Redo", "Ctrl+Y"),
        chord("Save", "Ctrl+S"), chord("Select", "Q"), chord("Move", "W"),
        chord("Rotate", "E"), chord("Scale", "R"), gesture("Tumble", "Alt", TabletMouseLeft),
        gesture("Track", "Alt", TabletMouseMiddle), gesture("Dolly", "Alt", TabletMouseRight)});
    add("3ds Max — Standard Interaction", "3dsmax", {chord("Undo", "Ctrl+Z"),
        chord("Redo", "Ctrl+Y"), chord("Save", "Ctrl+S"), chord("Select", "Q"),
        chord("Move", "W"), chord("Rotate", "E"), chord("Scale", "R"),
        gesture("Pan", {}, TabletMouseMiddle), gesture("Orbit", "Alt", TabletMouseMiddle),
        chord("Frame selection", "Z"),
        chord("Pan mode", "Ctrl+P"), chord("Orbit mode", "Ctrl+R")});
    add("Blender — Default Keymap", "blender", {chord("Undo", "Ctrl+Z"),
        chord("Redo", "Ctrl+Shift+Z"), chord("Save", "Ctrl+S"), chord("Quick Favorites", "Q"),
        chord("Edit / Object mode", "Tab"), gesture("Brush size", "F", TabletMouseNone),
        gesture("Brush strength", "Shift+F", TabletMouseNone), gesture("Orbit", {}, TabletMouseMiddle),
        gesture("Pan", "Shift", TabletMouseMiddle), gesture("Dolly / Zoom", "Ctrl", TabletMouseMiddle)});
    add("Marmoset Toolbag", "marmoset", {chord("Undo", "Ctrl+Z"),
        chord("Redo", "Ctrl+Shift+Z"), chord("Save", "Ctrl+S"), chord("Paint", "B"),
        chord("Erase / Rotate", "E"), chord("Color Picker", "P"),
        gesture("Orbit", "Alt", TabletMouseLeft), gesture("Pan", "Alt", TabletMouseMiddle),
        gesture("Zoom", "Alt", TabletMouseRight), chord("Frame selection", "Ctrl+F")});

    add("Windows 10 Remote", "windows10", {chord("Undo", "Ctrl+Z"), chord("Redo", "Ctrl+Y"),
        chord("Copy", "Ctrl+C"), chord("Paste", "Ctrl+V"),
        chord("Next application", "Alt+Tab"), chord("Previous application", "Alt+Shift+Tab"),
        chord("File Explorer", "Meta+E"), chord("Action Center", "Meta+A"),
        chord("On-Screen Keyboard", "Meta+Ctrl+O"),
        gesture("Middle-click pen gesture", {}, TabletMouseMiddle)});
    add("Windows 11 Remote", "windows11", {chord("Undo", "Ctrl+Z"), chord("Redo", "Ctrl+Y"),
        chord("Copy", "Ctrl+C"), chord("Paste", "Ctrl+V"),
        chord("Next application", "Alt+Tab"), chord("Previous application", "Alt+Shift+Tab"),
        chord("File Explorer", "Meta+E"), chord("Quick Settings", "Meta+A"),
        chord("Notifications", "Meta+N"), chord("On-Screen Keyboard", "Meta+Ctrl+O")});
    add("Maya-Style 3D Navigation", "maya-style", {chord("Undo", "Ctrl+Z"),
        chord("Redo", "Ctrl+Y"), chord("Save", "Ctrl+S"), chord("Select", "Q"),
        chord("Move", "W"), chord("Rotate", "E"), chord("Scale", "R"),
        gesture("Orbit", "Alt", TabletMouseLeft), gesture("Pan", "Alt", TabletMouseMiddle),
        gesture("Zoom", "Alt", TabletMouseRight), gesture("Brush size", "B", TabletMouseMiddle),
        gesture("Brush strength", "M", TabletMouseMiddle), chord("Smooth", "Shift", TabletActivation::Hold),
        chord("Invert", "Ctrl", TabletActivation::Hold)});
    add("3ds Max-Style 3D Navigation", "max-style", {chord("Undo", "Ctrl+Z"),
        chord("Redo", "Ctrl+Y"), chord("Save", "Ctrl+S"), chord("Select", "Q"),
        chord("Move", "W"), chord("Rotate", "E"), chord("Scale", "R"),
        gesture("Pan", {}, TabletMouseMiddle), gesture("Orbit", "Alt", TabletMouseMiddle),
        chord("Frame", "Z"), chord("Wireframe / smooth", "F3"),
        chord("Edged faces", "F4"), chord("Maximize viewport", "Alt+W"),
        chord("Pan mode", "Ctrl+P"), chord("Orbit mode", "Ctrl+R")});
    add("Generic Sculpting", "sculpting", {chord("Undo", "Ctrl+Z"),
        chord("Redo", "Ctrl+Shift+Z"), chord("Save", "Ctrl+S"),
        chord("Decrease brush size", "["), chord("Increase brush size", "]"),
        chord("Smooth", "Shift", TabletActivation::Hold), chord("Invert", "Ctrl", TabletActivation::Hold),
        gesture("Orbit", "Alt", TabletMouseLeft), gesture("Pan", "Alt", TabletMouseMiddle),
        gesture("Zoom", "Alt", TabletMouseRight),
        gesture("Middle-click pen gesture", {}, TabletMouseMiddle),
        local("Toggle diagnostics", TabletLocalAction::ToggleDiagnostics)});
    add("Generic Texture Painting", "texture-painting", {chord("Undo", "Ctrl+Z"),
        chord("Redo", "Ctrl+Shift+Z"), chord("Save", "Ctrl+S"), chord("Paint", "B"),
        chord("Erase", "E"), chord("Picker", "P"), gesture("Orbit", "Alt", TabletMouseLeft),
        gesture("Pan", "Alt", TabletMouseMiddle), gesture("Zoom", "Alt", TabletMouseRight),
        chord("Frame", "F"), local("Toggle diagnostics", TabletLocalAction::ToggleDiagnostics)});

    for (const QString& name : {QStringLiteral("Blank / Pass-Through")}) {
        QVector<TabletControlAction> passThrough(SlotCount);
        for (auto& action : passThrough) {
            action.kind = TabletActionKind::PassThrough;
            action.name = QStringLiteral("Pass through");
        }
        m_ShippedProfiles[name] = passThrough;
    }

    m_ProfileOrder.sort(Qt::CaseInsensitive);
}

void TabletMappingManager::loadActiveProfile()
{
    QVector<TabletControlAction> loaded = m_ShippedProfiles.value(m_ActiveProfile,
                                                                  QVector<TabletControlAction>(SlotCount));
    QSettings settings;
    for (int slot = 0; slot < SlotCount; ++slot) {
        const QByteArray json = settings.value(actionSettingsKey(slot)).toByteArray();
        if (json.isEmpty()) continue;
        bool ok = false;
        const auto action = actionFromJson(json, &ok);
        if (ok) loaded[slot] = action;
        else settings.setValue(actionSettingsKey(slot) + "/corruptBackup", json);
    }
    QWriteLocker guard(&m_Lock);
    m_Actions = loaded;
}

void TabletMappingManager::migrateLegacySettings()
{
    QSettings settings;
    if (settings.value("tabletMappings/schema", 1).toInt() >= kSettingsSchema) return;
    const QStringList legacyProfiles = {"Default", "Krita", "Photoshop", "Substance 3D Painter"};
    for (const auto& profile : legacyProfiles) {
        for (int slot = 0; slot < SlotCount; ++slot) {
            const QString oldKey = QString("tabletMappings/profiles/%1/slot%2").arg(profile).arg(slot + 1);
            const QString value = settings.value(oldKey).toString();
            if (value.isEmpty()) continue;
            settings.setValue(QString("tabletMappings/migrationBackup/v1/%1/slot%2")
                                  .arg(profile).arg(slot + 1), value);
            const auto action = chord(value, value);
            if (action.valid()) {
                const QString id = m_ProfileIds.value(profile, "default");
                settings.setValue(QString("tabletMappings/v2/profiles/%1/slot%2")
                                      .arg(id).arg(slot + 1), actionToJson(action));
            }
        }
    }
    settings.setValue("tabletMappings/schema", kSettingsSchema);
}

QString TabletMappingManager::profileIdForName(const QString& name) const
{
    return m_ProfileIds.value(name, "default");
}

QString TabletMappingManager::actionSettingsKey(int slot) const
{
    return QString("tabletMappings/v2/profiles/%1/slot%2")
        .arg(profileIdForName(m_ActiveProfile)).arg(slot + 1);
}

QString TabletMappingManager::sourceSettingsKey(int slot) const
{
    return QString("tabletMappings/v2/sources/slot%1").arg(slot + 1);
}

TabletControlAction TabletMappingManager::shippedAction(int slot) const
{
    const auto profile = m_ShippedProfiles.value(m_ActiveProfile);
    return slot >= 0 && slot < profile.size() ? profile[slot] : TabletControlAction{};
}

void TabletMappingManager::storeAction(int slot, const TabletControlAction& action)
{
    if (slot < 0 || slot >= SlotCount) return;
    QSettings().setValue(actionSettingsKey(slot), actionToJson(action));
    {
        QWriteLocker guard(&m_Lock);
        m_Actions[slot] = action;
    }
    notifyRowChanged(slot);
}

void TabletMappingManager::notifyRowChanged(int slot)
{
    const QModelIndex row = index(slot, 0);
    emit dataChanged(row, row);
    emit bindingsChanged();
}
