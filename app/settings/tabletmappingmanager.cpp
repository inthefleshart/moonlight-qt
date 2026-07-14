#include "tabletmappingmanager.h"

#include <QGlobalStatic>
#include <QKeySequence>
#include <QReadWriteLock>
#include <QSettings>
#include <QSet>

Q_GLOBAL_STATIC(QReadWriteLock, s_TabletMappingLock)
static TabletMappingManager* s_TabletMappingManager = nullptr;

TabletMappingManager::TabletMappingManager(QObject* parent)
    : QObject(parent)
{
    QSettings settings;
    m_ActiveProfile = settings.value("tabletMappings/activeProfile", "Default").toString();
    if (!profiles().contains(m_ActiveProfile)) {
        m_ActiveProfile = "Default";
    }
}

TabletMappingManager* TabletMappingManager::get(QQmlEngine* engine)
{
    QWriteLocker guard(s_TabletMappingLock);
    if (!s_TabletMappingManager) {
        s_TabletMappingManager = new TabletMappingManager(engine);
    }
    if (engine) {
        QJSEngine::setObjectOwnership(s_TabletMappingManager, QJSEngine::CppOwnership);
    }
    return s_TabletMappingManager;
}

QStringList TabletMappingManager::profiles() const
{
    return {"Default", "Krita", "Photoshop", "Substance 3D Painter"};
}

QString TabletMappingManager::activeProfile() const
{
    return m_ActiveProfile;
}

void TabletMappingManager::setActiveProfile(const QString& profile)
{
    if (!profiles().contains(profile) || profile == m_ActiveProfile) {
        return;
    }

    m_ActiveProfile = profile;
    QSettings().setValue("tabletMappings/activeProfile", m_ActiveProfile);
    emit activeProfileChanged();
    emit bindingsChanged();
}

QString TabletMappingManager::settingsKey(int slot) const
{
    return QString("tabletMappings/profiles/%1/slot%2").arg(m_ActiveProfile).arg(slot + 1);
}

QString TabletMappingManager::binding(int slot) const
{
    if (slot < 0 || slot >= 12) {
        return {};
    }
    return QSettings().value(settingsKey(slot)).toString();
}

QStringList TabletMappingManager::bindings() const
{
    QStringList result;
    for (int slot = 0; slot < 12; ++slot) {
        result.append(binding(slot));
    }
    return result;
}

bool TabletMappingManager::setBinding(int slot, const QString& sequence)
{
    if (slot < 0 || slot >= 12) {
        return false;
    }

    const QString normalized = sequence.trimmed();
    if (!normalized.isEmpty() && !isValidBinding(normalized)) {
        return false;
    }

    QSettings settings;
    if (normalized.isEmpty()) {
        settings.remove(settingsKey(slot));
    } else {
        settings.setValue(settingsKey(slot), QKeySequence(normalized).toString(QKeySequence::PortableText));
    }
    emit bindingsChanged();
    return true;
}

bool TabletMappingManager::isValidBinding(const QString& sequence) const
{
    QKeySequence parsed = QKeySequence::fromString(sequence, QKeySequence::PortableText);
    return parsed.count() == 1 && qtKeyToVirtualKey(parsed[0].key()) != 0;
}

bool TabletMappingManager::duplicateBindings() const
{
    QSet<QString> seen;
    for (const QString& value : bindings()) {
        const QString normalized = value.trimmed().toLower();
        if (normalized.isEmpty()) {
            continue;
        }
        if (seen.contains(normalized)) {
            return true;
        }
        seen.insert(normalized);
    }
    return false;
}

void TabletMappingManager::resetActiveProfile()
{
    QSettings settings;
    settings.remove(QString("tabletMappings/profiles/%1").arg(m_ActiveProfile));
    emit bindingsChanged();
}

TabletMappedShortcut TabletMappingManager::mappedShortcut(int slot) const
{
    TabletMappedShortcut result;
    if (slot < 0 || slot >= 12) {
        return result;
    }

    const QString value = binding(slot);
    if (value.isEmpty()) {
        return result;
    }

    QKeySequence sequence = QKeySequence::fromString(value, QKeySequence::PortableText);
    if (sequence.count() != 1) {
        return result;
    }

    const QKeyCombination combination = sequence[0];
    result.virtualKey = qtKeyToVirtualKey(combination.key());
    result.control = combination.keyboardModifiers().testFlag(Qt::ControlModifier);
    result.alt = combination.keyboardModifiers().testFlag(Qt::AltModifier);
    result.shift = combination.keyboardModifiers().testFlag(Qt::ShiftModifier);
    result.meta = combination.keyboardModifiers().testFlag(Qt::MetaModifier);
    result.valid = result.virtualKey != 0;
    return result;
}

quint16 TabletMappingManager::qtKeyToVirtualKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<quint16>(0x41 + key - Qt::Key_A);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<quint16>(0x30 + key - Qt::Key_0);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return static_cast<quint16>(0x70 + key - Qt::Key_F1);
    }

    switch (key) {
    case Qt::Key_Backspace: return 0x08;
    case Qt::Key_Tab: return 0x09;
    case Qt::Key_Return:
    case Qt::Key_Enter: return 0x0D;
    case Qt::Key_Escape: return 0x1B;
    case Qt::Key_Space: return 0x20;
    case Qt::Key_PageUp: return 0x21;
    case Qt::Key_PageDown: return 0x22;
    case Qt::Key_End: return 0x23;
    case Qt::Key_Home: return 0x24;
    case Qt::Key_Left: return 0x25;
    case Qt::Key_Up: return 0x26;
    case Qt::Key_Right: return 0x27;
    case Qt::Key_Down: return 0x28;
    case Qt::Key_Insert: return 0x2D;
    case Qt::Key_Delete: return 0x2E;
    default: return 0;
    }
}
