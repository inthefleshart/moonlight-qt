#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

struct TabletMappedShortcut
{
    quint16 virtualKey = 0;
    bool control = false;
    bool alt = false;
    bool shift = false;
    bool meta = false;
    bool valid = false;
};

class TabletMappingManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList profiles READ profiles CONSTANT)
    Q_PROPERTY(QString activeProfile READ activeProfile WRITE setActiveProfile NOTIFY activeProfileChanged)
    Q_PROPERTY(QStringList bindings READ bindings NOTIFY bindingsChanged)
    Q_PROPERTY(bool duplicateBindings READ duplicateBindings NOTIFY bindingsChanged)

public:
    static TabletMappingManager* get(QQmlEngine* engine = nullptr);

    QStringList profiles() const;
    QString activeProfile() const;
    void setActiveProfile(const QString& profile);
    QStringList bindings() const;
    bool duplicateBindings() const;

    Q_INVOKABLE QString binding(int slot) const;
    Q_INVOKABLE bool setBinding(int slot, const QString& sequence);
    Q_INVOKABLE bool isValidBinding(const QString& sequence) const;
    Q_INVOKABLE void resetActiveProfile();

    TabletMappedShortcut mappedShortcut(int slot) const;

signals:
    void activeProfileChanged();
    void bindingsChanged();

private:
    explicit TabletMappingManager(QObject* parent = nullptr);

    QString settingsKey(int slot) const;
    static quint16 qtKeyToVirtualKey(int key);

    QString m_ActiveProfile;
};
