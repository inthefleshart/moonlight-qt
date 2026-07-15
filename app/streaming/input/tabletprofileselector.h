#pragma once

#include <QSet>
#include <QStringList>

class TabletProfileSelector
{
public:
    static constexpr int PageSize = 10;
    static constexpr int HeaderLines = 2;

    void open(const QStringList& profiles, const QString& activeProfile,
              const QStringList& favoriteProfiles);
    void close();
    bool isOpen() const { return m_Open; }

    void moveSelection(int delta);
    void pageSelection(int pages);
    void selectFirst();
    void selectLast();
    bool setHoveredRow(int row);

    int selectedIndex() const { return m_SelectedIndex; }
    int firstVisibleIndex() const { return m_FirstVisibleIndex; }
    int visibleCount() const;
    QString selectedProfile() const;
    QString renderText() const;

    int rowForPoint(int x, int y, int windowWidth, int windowHeight,
                    int overlayWidth, int overlayHeight, int lineHeight) const;

private:
    void ensureSelectionVisible();

    bool m_Open = false;
    QStringList m_Profiles;
    QSet<QString> m_Favorites;
    QString m_ActiveProfile;
    int m_SelectedIndex = 0;
    int m_FirstVisibleIndex = 0;
};
