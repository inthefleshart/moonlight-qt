#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QReadWriteLock>
#include <QQmlEngine>
#include <QStringList>
#include <QVector>

enum class TabletActionKind
{
    Disabled,
    PassThrough,
    KeyChord,
    KeySequence,
    PenGesture,
    MouseWheel,
    LocalAction,
    WacomRadialChord,
};

enum class TabletActivation
{
    Tap,
    Hold,
    Repeat,
};

enum class TabletLocalAction
{
    None,
    ToggleTouch,
    CycleTouchPolicy,
    ToggleDiagnostics,
    ResetStuckInput,
    OpenProfileSelector,
    NextFavoriteProfile,
    PreviousFavoriteProfile,
};

enum TabletMouseButton
{
    TabletMouseNone = 0,
    TabletMouseLeft = 1,
    TabletMouseMiddle = 2,
    TabletMouseRight = 3,
    TabletMouseX1 = 4,
    TabletMouseX2 = 5,
};

struct TabletKeyStroke
{
    quint16 virtualKey = 0;
    bool control = false;
    bool alt = false;
    bool shift = false;
    bool meta = false;
};

struct TabletControlAction
{
    TabletActionKind kind = TabletActionKind::Disabled;
    TabletActivation activation = TabletActivation::Tap;
    QString name;
    TabletKeyStroke chord;
    QVector<TabletKeyStroke> sequence;
    int mouseButton = TabletMouseNone;
    int wheelDelta = 0;
    TabletLocalAction localAction = TabletLocalAction::None;
    bool modified = false;

    bool valid() const;
    QString displayText() const;
};

struct TabletSourceShortcut
{
    TabletKeyStroke chord;
    bool valid = false;
    QString displayText() const;
};

class TabletMappingManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QStringList profiles READ profiles CONSTANT)
    Q_PROPERTY(QString activeProfile READ activeProfile WRITE setActiveProfile NOTIFY activeProfileChanged)
    Q_PROPERTY(QAbstractItemModel* bindingsModel READ bindingsModel CONSTANT)
    Q_PROPERTY(bool duplicateBindings READ duplicateBindings NOTIFY bindingsChanged)
    Q_PROPERTY(QVariantList actionOptions READ actionOptions CONSTANT)
    Q_PROPERTY(QVariantList favoriteProfileOptions READ favoriteProfileOptions
               NOTIFY favoriteProfilesChanged)
    Q_PROPERTY(int favoriteProfileCount READ favoriteProfileCount NOTIFY favoriteProfilesChanged)

public:
    static constexpr int SlotCount = 10;

    enum Roles
    {
        SlotRole = Qt::UserRole + 1,
        SourceTextRole,
        ActionNameRole,
        ActionTextRole,
        ActionKindRole,
        ActivationRole,
        ModifiedRole,
        SetupRequiredRole,
    };

    static TabletMappingManager* get(QQmlEngine* engine = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList profiles() const;
    QStringList favoriteProfiles() const;
    QString activeProfile() const;
    void setActiveProfile(const QString& profile);
    QAbstractItemModel* bindingsModel() { return this; }
    bool duplicateBindings() const;
    QVariantList actionOptions() const;
    QVariantList favoriteProfileOptions() const;
    int favoriteProfileCount() const { return m_FavoriteProfileIds.size(); }

    Q_INVOKABLE QString binding(int slot) const;
    Q_INVOKABLE QString sourceBinding(int slot) const;
    Q_INVOKABLE bool setBinding(int slot, const QString& sequence);
    Q_INVOKABLE bool setSourceBinding(int slot, const QString& sequence);
    Q_INVOKABLE bool recordKey(int slot, int qtKey, int qtModifiers, bool source,
                               bool hold = false);
    Q_INVOKABLE QString recordedKeyText(int qtKey, int qtModifiers) const;
    Q_INVOKABLE bool setActionOption(int slot, const QString& optionId);
    Q_INVOKABLE bool setPenGesture(int slot, int qtKey, int qtModifiers, int mouseButton,
                                   const QString& label);
    Q_INVOKABLE bool setKeySequence(int slot, const QStringList& portableKeys,
                                    const QString& label);
    Q_INVOKABLE void clearBinding(int slot);
    Q_INVOKABLE void resetBinding(int slot);
    Q_INVOKABLE void resetActiveProfile();
    Q_INVOKABLE bool setProfileFavorite(const QString& profile, bool favorite);
    Q_INVOKABLE bool moveFavoriteProfile(const QString& profile, int direction);
    Q_INVOKABLE void resetFavoriteProfiles();
    Q_INVOKABLE QString cycleFavoriteProfile(int direction);
    Q_INVOKABLE bool isFavoriteProfile(const QString& profile) const;
    Q_INVOKABLE bool isValidBinding(const QString& sequence) const;

    TabletControlAction actionForSlot(int slot) const;
    TabletSourceShortcut sourceForSlot(int slot) const;
    int slotForSource(quint16 virtualKey, bool control, bool alt, bool shift, bool meta) const;

    static quint16 qtKeyToVirtualKey(int key);
    static QString keyStrokeText(const TabletKeyStroke& stroke);

signals:
    void activeProfileChanged();
    void bindingsChanged();
    void favoriteProfilesChanged();

private:
    explicit TabletMappingManager(QObject* parent = nullptr);

    void buildShippedProfiles();
    void loadActiveProfile();
    void migrateLegacySettings();
    void loadFavoriteProfiles();
    void storeFavoriteProfiles();
    QString profileIdForName(const QString& name) const;
    QString profileNameForId(const QString& id) const;
    QString actionSettingsKey(int slot) const;
    QString sourceSettingsKey(int slot) const;
    TabletControlAction shippedAction(int slot) const;
    void storeAction(int slot, const TabletControlAction& action);
    void notifyRowChanged(int slot);

    static TabletKeyStroke parseStroke(const QString& sequence);
    static TabletControlAction chord(const QString& name, const QString& sequence,
                                     TabletActivation activation = TabletActivation::Tap);
    static TabletControlAction sequence(const QString& name, const QStringList& keys);
    static TabletControlAction gesture(const QString& name, const QString& keys,
                                       int mouseButton);
    static TabletControlAction local(const QString& name, TabletLocalAction action);
    static TabletControlAction radial();

    mutable QReadWriteLock m_Lock;
    QString m_ActiveProfile;
    QStringList m_ProfileOrder;
    QHash<QString, QString> m_ProfileIds;
    QHash<QString, QVector<TabletControlAction>> m_ShippedProfiles;
    QStringList m_FavoriteProfileIds;
    QVector<TabletControlAction> m_Actions;
    QVector<TabletSourceShortcut> m_Sources;
};
