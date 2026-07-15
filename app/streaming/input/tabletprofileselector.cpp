#include "tabletprofileselector.h"

#include <algorithm>

void TabletProfileSelector::open(const QStringList& profiles, const QString& activeProfile,
                                 const QStringList& favoriteProfiles)
{
    m_Profiles = profiles;
    m_Favorites = QSet<QString>(favoriteProfiles.cbegin(), favoriteProfiles.cend());
    m_ActiveProfile = activeProfile;
    m_SelectedIndex = std::max(0, static_cast<int>(m_Profiles.indexOf(activeProfile)));
    m_FirstVisibleIndex = 0;
    m_Open = !m_Profiles.isEmpty();
    ensureSelectionVisible();
}

void TabletProfileSelector::close()
{
    m_Open = false;
}

void TabletProfileSelector::moveSelection(int delta)
{
    if (!m_Open || m_Profiles.isEmpty() || delta == 0) return;
    const int count = static_cast<int>(m_Profiles.size());
    m_SelectedIndex = (m_SelectedIndex + delta % count + count) % count;
    ensureSelectionVisible();
}

void TabletProfileSelector::pageSelection(int pages)
{
    if (!m_Open || pages == 0) return;
    m_SelectedIndex = std::clamp(m_SelectedIndex + pages * PageSize,
                                 0, static_cast<int>(m_Profiles.size()) - 1);
    ensureSelectionVisible();
}

void TabletProfileSelector::selectFirst()
{
    if (!m_Open || m_Profiles.isEmpty()) return;
    m_SelectedIndex = 0;
    ensureSelectionVisible();
}

void TabletProfileSelector::selectLast()
{
    if (!m_Open || m_Profiles.isEmpty()) return;
    m_SelectedIndex = static_cast<int>(m_Profiles.size()) - 1;
    ensureSelectionVisible();
}

bool TabletProfileSelector::setHoveredRow(int row)
{
    if (!m_Open || row < 0 || row >= visibleCount()) return false;
    const int selectedIndex = m_FirstVisibleIndex + row;
    if (m_SelectedIndex == selectedIndex) return false;
    m_SelectedIndex = selectedIndex;
    return true;
}

int TabletProfileSelector::visibleCount() const
{
    return std::max(0, std::min(PageSize,
        static_cast<int>(m_Profiles.size()) - m_FirstVisibleIndex));
}

QString TabletProfileSelector::profileAtVisibleRow(int row) const
{
    const int index = m_FirstVisibleIndex + row;
    return row >= 0 && row < visibleCount() && index < m_Profiles.size() ?
               m_Profiles[index] : QString();
}

QString TabletProfileSelector::selectedProfile() const
{
    return m_SelectedIndex >= 0 && m_SelectedIndex < m_Profiles.size() ?
               m_Profiles[m_SelectedIndex] : QString();
}

QString TabletProfileSelector::renderText() const
{
    if (!m_Open) return {};
    QStringList lines;
    lines << QStringLiteral("QuickKey preset selector")
          << QStringLiteral("Active: %1").arg(m_ActiveProfile);
    for (int row = 0; row < visibleCount(); ++row) {
        const int index = m_FirstVisibleIndex + row;
        const QString& profile = m_Profiles[index];
        lines << QStringLiteral("%1 %2 %3")
                     .arg(index == m_SelectedIndex ? '>' : ' ')
                     .arg(m_Favorites.contains(profile) ? '*' : ' ')
                     .arg(profile);
    }
    lines << QStringLiteral("Arrows / wheel: move   Enter / tap: select")
          << QStringLiteral("Esc / outside tap: cancel   * favorite");
    return lines.join('\n');
}

int TabletProfileSelector::rowForPoint(int x, int y, int windowWidth, int windowHeight,
                                       int overlayWidth, int overlayHeight, int lineHeight) const
{
    if (!m_Open || windowWidth <= 0 || windowHeight <= 0 || overlayWidth <= 0 ||
            overlayHeight <= 0 || lineHeight <= 0) return -1;
    const int left = (windowWidth - overlayWidth) / 2;
    const int top = (windowHeight - overlayHeight) / 2;
    if (x < left || x >= left + overlayWidth || y < top || y >= top + overlayHeight) return -1;
    const int line = (y - top) / lineHeight;
    const int row = line - HeaderLines;
    return row >= 0 && row < visibleCount() ? row : -1;
}

void TabletProfileSelector::ensureSelectionVisible()
{
    if (m_SelectedIndex < m_FirstVisibleIndex) m_FirstVisibleIndex = m_SelectedIndex;
    else if (m_SelectedIndex >= m_FirstVisibleIndex + PageSize)
        m_FirstVisibleIndex = m_SelectedIndex - PageSize + 1;
}
