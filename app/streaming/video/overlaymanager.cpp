#include "overlaymanager.h"
#include "path.h"

#include <QFile>

#include <algorithm>
#include <cmath>

namespace {
void fillRect(SDL_Surface* surface, const SDL_Rect& rect, SDL_Color color)
{
    SDL_Rect clipped = rect;
    SDL_FillRect(surface, &clipped,
                 SDL_MapRGBA(surface->format, color.r, color.g, color.b, color.a));
}

int textWidth(TTF_Font* font, const QString& text)
{
    int width = 0;
    int height = 0;
    const QByteArray utf8 = text.toUtf8();
    return TTF_SizeUTF8(font, utf8.constData(), &width, &height) == 0 ? width : 0;
}

QString elideText(TTF_Font* font, const QString& text, int maximumWidth)
{
    if (maximumWidth <= 0 || textWidth(font, text) <= maximumWidth) return text;
    const QString suffix = QStringLiteral("...");
    int low = 0;
    int high = text.size();
    while (low < high) {
        const int middle = (low + high + 1) / 2;
        if (textWidth(font, text.left(middle) + suffix) <= maximumWidth) low = middle;
        else high = middle - 1;
    }
    return text.left(low).trimmed() + suffix;
}

void drawText(SDL_Surface* destination, TTF_Font* font, const QString& text,
              SDL_Color color, int x, int y, int maximumWidth = 0)
{
    const QString visibleText = maximumWidth > 0 ? elideText(font, text, maximumWidth) : text;
    if (visibleText.isEmpty()) return;
    const QByteArray utf8 = visibleText.toUtf8();
    SDL_Surface* rendered = TTF_RenderUTF8_Blended(font, utf8.constData(), color);
    if (!rendered) return;
    SDL_Rect destinationRect{x, y, rendered->w, rendered->h};
    SDL_BlitSurface(rendered, nullptr, destination, &destinationRect);
    SDL_FreeSurface(rendered);
}
}

using namespace Overlay;

OverlayManager::OverlayManager() :
    m_Renderer(nullptr),
    m_FontData(Path::readDataFile("ModeSeven.ttf"))
{
#ifdef Q_OS_WIN32
    QFile uiFont(qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows")) +
                 QStringLiteral("/Fonts/segoeui.ttf"));
    if (uiFont.open(QIODevice::ReadOnly)) m_UiFontData = uiFont.readAll();
#endif
    if (m_UiFontData.isEmpty()) m_UiFontData = m_FontData;

    memset(m_Overlays, 0, sizeof(m_Overlays));

    m_Overlays[OverlayType::OverlayDebug].color = {0xD0, 0xD0, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayDebug].fontSize = 20;

    m_Overlays[OverlayType::OverlayStatusUpdate].color = {0xCC, 0x00, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayStatusUpdate].fontSize = 36;

    m_Overlays[OverlayType::OverlayQuickKeyProfiles].color = {0xF4, 0xF4, 0xF4, 0xFF};
    m_Overlays[OverlayType::OverlayQuickKeyProfiles].fontSize = 28;

    // While TTF will usually not be initialized here, it is valid for that not to
    // be the case, since Session destruction is deferred and could overlap with
    // the lifetime of a new Session object.
    //SDL_assert(TTF_WasInit() == 0);

    if (TTF_Init() != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TTF_Init() failed: %s",
                    TTF_GetError());
        return;
    }
}

OverlayManager::~OverlayManager()
{
    for (int i = 0; i < OverlayType::OverlayMax; i++) {
        if (m_Overlays[i].surface != nullptr) {
            SDL_FreeSurface(m_Overlays[i].surface);
        }
        if (m_Overlays[i].font != nullptr) {
            TTF_CloseFont(m_Overlays[i].font);
        }
    }

    TTF_Quit();

    // For similar reasons to the comment in the constructor, this will usually,
    // but not always, deinitialize TTF. In the cases where Session objects overlap
    // in lifetime, there may be an additional reference on TTF for the new Session
    // that means it will not be cleaned up here.
    //SDL_assert(TTF_WasInit() == 0);
}

bool OverlayManager::isOverlayEnabled(OverlayType type)
{
    return m_Overlays[type].enabled;
}

char* OverlayManager::getOverlayText(OverlayType type)
{
    return m_Overlays[type].text;
}

void OverlayManager::updateOverlayText(OverlayType type, const char* text)
{
    if (type == OverlayQuickKeyProfiles) m_HasQuickKeyContent = false;
    SDL_utf8strlcpy(m_Overlays[type].text, text, sizeof(m_Overlays[0].text));
    setOverlayTextUpdated(type);
}

void OverlayManager::updateQuickKeyOverlay(const QuickKeyOverlayContent& content)
{
    m_QuickKeyContent = content;
    m_HasQuickKeyContent = true;
    setOverlayTextUpdated(OverlayQuickKeyProfiles);
}

int OverlayManager::getOverlayMaxTextLength()
{
    return sizeof(m_Overlays[0].text);
}

int OverlayManager::getOverlayFontSize(OverlayType type)
{
    return m_Overlays[type].fontSize;
}

int OverlayManager::getOverlayWidth(OverlayType type)
{
    return m_Overlays[type].width;
}

int OverlayManager::getOverlayHeight(OverlayType type)
{
    return m_Overlays[type].height;
}

int OverlayManager::getOverlayLineHeight(OverlayType type)
{
    return m_Overlays[type].font ? TTF_FontLineSkip(m_Overlays[type].font) :
                                  m_Overlays[type].fontSize + 4;
}

int OverlayManager::getQuickKeyProfileRowAt(int x, int y) const
{
    for (int row = 0; row < m_QuickKeyProfileRects.size(); ++row) {
        const SDL_Rect& rect = m_QuickKeyProfileRects[row];
        if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h)
            return row;
    }
    return -1;
}

SDL_Surface* OverlayManager::getUpdatedOverlaySurface(OverlayType type)
{
    // If a new surface is available, return it. If not, return nullptr.
    // Caller must free the surface on success.
    return (SDL_Surface*)SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, nullptr);
}

void OverlayManager::setOverlayTextUpdated(OverlayType type)
{
    // Only update the overlay state if it's enabled. If it's not enabled,
    // the renderer has already been notified by setOverlayState().
    if (m_Overlays[type].enabled) {
        notifyOverlayUpdated(type);
    }
}

void OverlayManager::setOverlayState(OverlayType type, bool enabled)
{
    bool stateChanged = m_Overlays[type].enabled != enabled;

    m_Overlays[type].enabled = enabled;

    if (stateChanged) {
        if (!enabled) {
            // Set the text to empty string on disable
            m_Overlays[type].text[0] = 0;
            m_Overlays[type].width = 0;
            m_Overlays[type].height = 0;
            if (type == OverlayQuickKeyProfiles) m_QuickKeyProfileRects.clear();
        }

        notifyOverlayUpdated(type);
    }
}

SDL_Color OverlayManager::getOverlayColor(OverlayType type)
{
    return m_Overlays[type].color;
}

void OverlayManager::setOverlayRenderer(IOverlayRenderer* renderer)
{
    m_Renderer = renderer;
}

void OverlayManager::notifyOverlayUpdated(OverlayType type)
{
    if (m_Renderer == nullptr) {
        return;
    }

    const bool renderQuickKeyPanel = type == OverlayQuickKeyProfiles && m_HasQuickKeyContent;

    // Construct the required font to render ordinary text overlays. The QuickKey
    // panel creates scaled UI fonts as part of its structured rendering path.
    if (!renderQuickKeyPanel && m_Overlays[type].font == nullptr) {
        const QByteArray& fontData = type == OverlayQuickKeyProfiles ? m_UiFontData : m_FontData;
        if (fontData.isEmpty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL overlay font failed to load");
            return;
        }

        // The selected font data must stay around until the font is closed.
        m_Overlays[type].font = TTF_OpenFontRW(SDL_RWFromConstMem(fontData.constData(), fontData.size()),
                                               1,
                                               m_Overlays[type].fontSize);
        if (m_Overlays[type].font == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TTF_OpenFont() failed: %s",
                        TTF_GetError());

            // Can't proceed without a font
            return;
        }
    }

    // Exchange the old surface with the new one
    SDL_Surface* newSurface = nullptr;
    if (m_Overlays[type].enabled) {
        newSurface = renderQuickKeyPanel ? RenderQuickKeyOverlay() :
            // The _Wrapped variant is required for line breaks to work.
            RenderTextOutlinedWrapped(m_Overlays[type].font,
                                      m_Overlays[type].text,
                                      m_Overlays[type].color,
                                      {0, 0, 0, 255},
                                      4,
                                      1024);
    }
    m_Overlays[type].width = newSurface ? newSurface->w : 0;
    m_Overlays[type].height = newSurface ? newSurface->h : 0;
    SDL_Surface* oldSurface = (SDL_Surface*)SDL_AtomicSetPtr(
        (void**)&m_Overlays[type].surface, newSurface);

    // Notify the renderer
    m_Renderer->notifyOverlayUpdated(type);

    // Free the old surface
    if (oldSurface != nullptr) {
        SDL_FreeSurface(oldSurface);
    }
}

SDL_Surface* OverlayManager::RenderQuickKeyOverlay()
{
    m_QuickKeyProfileRects.clear();
    if (m_UiFontData.isEmpty() || m_QuickKeyContent.profiles.isEmpty()) return nullptr;

    const int viewportWidth = std::max(640, m_QuickKeyContent.viewportWidth);
    const int viewportHeight = std::max(480, m_QuickKeyContent.viewportHeight);
    const float scale = std::clamp(viewportHeight / 1440.0f, 0.78f, 1.40f);
    const int panelWidth = std::max(620, std::min(static_cast<int>(1220 * scale),
                                                  static_cast<int>(viewportWidth * 0.88f)));
    const int panelHeight = std::max(520, std::min(static_cast<int>(900 * scale),
                                                   static_cast<int>(viewportHeight * 0.88f)));
    const int shadowSize = std::max(6, static_cast<int>(10 * scale));
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, panelWidth + shadowSize, panelHeight + shadowSize, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 0, 0, 0, 0));

    auto openFont = [this](int size) {
        TTF_Font* font = TTF_OpenFontRW(
            SDL_RWFromConstMem(m_UiFontData.constData(), m_UiFontData.size()), 1, size);
        if (font) TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
        return font;
    };
    TTF_Font* titleFont = openFont(std::max(21, static_cast<int>(29 * scale)));
    TTF_Font* bodyFont = openFont(std::max(14, static_cast<int>(18 * scale)));
    TTF_Font* smallFont = openFont(std::max(11, static_cast<int>(14 * scale)));
    TTF_Font* sectionFont = openFont(std::max(11, static_cast<int>(13 * scale)));
    if (!titleFont || !bodyFont || !smallFont || !sectionFont) {
        TTF_CloseFont(titleFont);
        TTF_CloseFont(bodyFont);
        TTF_CloseFont(smallFont);
        TTF_CloseFont(sectionFont);
        SDL_FreeSurface(surface);
        return nullptr;
    }
    TTF_SetFontStyle(titleFont, TTF_STYLE_BOLD);
    TTF_SetFontStyle(sectionFont, TTF_STYLE_BOLD);

    const SDL_Color shadow{0, 0, 0, 115};
    const SDL_Color border{62, 174, 255, 255};
    const SDL_Color panel{15, 23, 36, 248};
    const SDL_Color header{21, 38, 58, 255};
    const SDL_Color footer{10, 17, 28, 255};
    const SDL_Color divider{52, 72, 94, 255};
    const SDL_Color primaryText{242, 247, 252, 255};
    const SDL_Color secondaryText{157, 177, 198, 255};
    const SDL_Color accent{72, 187, 255, 255};
    const SDL_Color selected{28, 105, 164, 235};
    const SDL_Color rowAlternate{23, 34, 50, 220};
    const SDL_Color sourceBadge{41, 57, 77, 255};
    const SDL_Color favorite{255, 197, 72, 255};
    const SDL_Color active{68, 211, 139, 255};

    fillRect(surface, {shadowSize, shadowSize, panelWidth, panelHeight}, shadow);
    fillRect(surface, {0, 0, panelWidth, panelHeight}, border);
    fillRect(surface, {2, 2, panelWidth - 4, panelHeight - 4}, panel);

    const int padding = std::max(18, static_cast<int>(26 * scale));
    const int headerHeight = std::max(68, static_cast<int>(86 * scale));
    const int sectionHeight = std::max(34, static_cast<int>(42 * scale));
    const int footerHeight = std::max(42, static_cast<int>(54 * scale));
    fillRect(surface, {2, 2, panelWidth - 4, headerHeight - 2}, header);
    fillRect(surface, {2, panelHeight - footerHeight, panelWidth - 4, footerHeight - 2}, footer);
    fillRect(surface, {2, headerHeight - 2, panelWidth - 4, 2}, accent);

    drawText(surface, titleFont, QStringLiteral("QuickKey Presets"), primaryText,
             padding, std::max(10, static_cast<int>(14 * scale)));
    drawText(surface, smallFont, QStringLiteral("Switch tools without leaving your stream"),
             secondaryText, padding, std::max(42, static_cast<int>(51 * scale)));

    const QString activeLabel = QStringLiteral("ACTIVE  %1").arg(m_QuickKeyContent.activeProfile);
    const int activeLabelWidth = textWidth(sectionFont, activeLabel);
    const int activePadding = std::max(10, static_cast<int>(14 * scale));
    const int activeHeight = TTF_FontHeight(sectionFont) + std::max(8, static_cast<int>(10 * scale));
    SDL_Rect activeBadge{panelWidth - padding - activeLabelWidth - activePadding * 2,
                         std::max(15, static_cast<int>(22 * scale)),
                         activeLabelWidth + activePadding * 2, activeHeight};
    fillRect(surface, activeBadge, {23, 74, 62, 255});
    drawText(surface, sectionFont, activeLabel, active,
             activeBadge.x + activePadding, activeBadge.y + (activeHeight - TTF_FontHeight(sectionFont)) / 2);

    const int contentWidth = panelWidth - padding * 2;
    const int leftWidth = static_cast<int>(contentWidth * 0.40f);
    const int columnGap = std::max(20, static_cast<int>(28 * scale));
    const int dividerX = padding + leftWidth + columnGap / 2;
    const int rightX = padding + leftWidth + columnGap;
    const int rightWidth = panelWidth - padding - rightX;
    const int sectionY = headerHeight;
    const int rowsTop = sectionY + sectionHeight;
    const int rowsBottom = panelHeight - footerHeight;
    const int rowCount = std::max(1, std::max(
        static_cast<int>(m_QuickKeyContent.profiles.size()),
        static_cast<int>(m_QuickKeyContent.bindings.size())));
    const int rowHeight = std::max(34, (rowsBottom - rowsTop) / rowCount);
    fillRect(surface, {dividerX, sectionY + 8, 1, rowsBottom - sectionY - 16}, divider);

    drawText(surface, sectionFont, QStringLiteral("PRESETS"), accent,
             padding, sectionY + (sectionHeight - TTF_FontHeight(sectionFont)) / 2);
    const QString pageLabel = QStringLiteral("%1-%2 OF %3")
        .arg(m_QuickKeyContent.firstVisibleIndex + 1)
        .arg(m_QuickKeyContent.firstVisibleIndex + m_QuickKeyContent.profiles.size())
        .arg(m_QuickKeyContent.totalProfiles);
    drawText(surface, smallFont, pageLabel, secondaryText,
             padding + leftWidth - textWidth(smallFont, pageLabel),
             sectionY + (sectionHeight - TTF_FontHeight(smallFont)) / 2);
    drawText(surface, sectionFont, QStringLiteral("QUICKKEY REFERENCE"), accent,
             rightX, sectionY + 5);
    drawText(surface, smallFont, m_QuickKeyContent.selectedProfile, primaryText,
             rightX, sectionY + sectionHeight - TTF_FontHeight(smallFont) - 3, rightWidth);

    auto drawDiamond = [surface](int centerX, int centerY, int radius, SDL_Color color) {
        for (int offset = -radius; offset <= radius; ++offset) {
            const int halfWidth = radius - std::abs(offset);
            fillRect(surface, {centerX - halfWidth, centerY + offset,
                               halfWidth * 2 + 1, 1}, color);
        }
    };
    auto drawDot = [surface](int centerX, int centerY, int radius, SDL_Color color) {
        fillRect(surface, {centerX - radius + 1, centerY - radius,
                           radius * 2 - 1, radius * 2 + 1}, color);
        fillRect(surface, {centerX - radius, centerY - radius + 1,
                           radius * 2 + 1, radius * 2 - 1}, color);
    };

    const int rowGap = std::max(2, static_cast<int>(4 * scale));
    for (int row = 0; row < m_QuickKeyContent.profiles.size(); ++row) {
        const auto& profile = m_QuickKeyContent.profiles[row];
        SDL_Rect rowRect{padding, rowsTop + row * rowHeight,
                         leftWidth, rowHeight - rowGap};
        m_QuickKeyProfileRects.append(rowRect);
        if (profile.selected) {
            fillRect(surface, rowRect, selected);
            fillRect(surface, {rowRect.x, rowRect.y, std::max(3, static_cast<int>(4 * scale)), rowRect.h}, accent);
        } else if (row % 2) {
            fillRect(surface, rowRect, rowAlternate);
        }
        const int textY = rowRect.y + (rowRect.h - TTF_FontHeight(bodyFont)) / 2;
        const int markerCenterY = rowRect.y + rowRect.h / 2;
        const int favoriteX = rowRect.x + std::max(14, static_cast<int>(17 * scale));
        if (profile.favorite)
            drawDiamond(favoriteX, markerCenterY, std::max(3, static_cast<int>(4 * scale)), favorite);
        const int activeX = rowRect.x + std::max(34, static_cast<int>(42 * scale));
        if (profile.active)
            drawDot(activeX, markerCenterY, std::max(3, static_cast<int>(4 * scale)), active);
        const int nameX = rowRect.x + std::max(54, static_cast<int>(66 * scale));
        drawText(surface, bodyFont, profile.name, primaryText, nameX, textY,
                 rowRect.x + rowRect.w - nameX - std::max(8, static_cast<int>(10 * scale)));
    }

    for (int row = 0; row < m_QuickKeyContent.bindings.size(); ++row) {
        const auto& binding = m_QuickKeyContent.bindings[row];
        SDL_Rect rowRect{rightX, rowsTop + row * rowHeight, rightWidth, rowHeight - rowGap};
        if (row % 2) fillRect(surface, rowRect, rowAlternate);
        if (binding.modified)
            fillRect(surface, {rowRect.x, rowRect.y, std::max(2, static_cast<int>(3 * scale)), rowRect.h}, accent);

        const int badgeWidth = std::max(54, static_cast<int>(68 * scale));
        const int badgeHeight = std::min(rowRect.h - 8, std::max(25, static_cast<int>(32 * scale)));
        SDL_Rect badge{rowRect.x + std::max(7, static_cast<int>(10 * scale)),
                       rowRect.y + (rowRect.h - badgeHeight) / 2, badgeWidth, badgeHeight};
        fillRect(surface, badge, sourceBadge);
        const QString source = binding.source.isEmpty() ? QStringLiteral("?") : binding.source;
        drawText(surface, sectionFont, source, primaryText,
                 badge.x + (badge.w - textWidth(sectionFont, source)) / 2,
                 badge.y + (badge.h - TTF_FontHeight(sectionFont)) / 2, badge.w - 6);

        const int actionX = badge.x + badge.w + std::max(10, static_cast<int>(14 * scale));
        const int actionWidth = rowRect.x + rowRect.w - actionX - 8;
        TTF_SetFontStyle(bodyFont, TTF_STYLE_BOLD);
        drawText(surface, bodyFont,
                 binding.actionName.isEmpty() ? QStringLiteral("Unassigned") : binding.actionName,
                 primaryText, actionX, rowRect.y + std::max(3, static_cast<int>(5 * scale)), actionWidth);
        TTF_SetFontStyle(bodyFont, TTF_STYLE_NORMAL);
        if (!binding.actionDetails.isEmpty()) {
            drawText(surface, smallFont, binding.actionDetails, secondaryText,
                     actionX, rowRect.y + rowRect.h - TTF_FontHeight(smallFont) -
                         std::max(3, static_cast<int>(5 * scale)), actionWidth);
        }
    }

    const int footerY = panelHeight - footerHeight;
    const QString instructions = QStringLiteral(
        "Arrow keys / wheel  Navigate     Enter / tap  Select     Esc  Cancel");
    drawText(surface, smallFont,
             instructions,
             primaryText, padding,
             footerY + (footerHeight - TTF_FontHeight(smallFont)) / 2,
             static_cast<int>(panelWidth * 0.60f));

    const QString favoriteLabel = QStringLiteral("Favorite");
    const QString activeLabelText = QStringLiteral("Active");
    const QString customLabel = QStringLiteral("Custom");
    const int legendGap = std::max(14, static_cast<int>(18 * scale));
    const int markerGap = std::max(7, static_cast<int>(9 * scale));
    const int legendWidth = textWidth(smallFont, favoriteLabel) +
                            textWidth(smallFont, activeLabelText) +
                            textWidth(smallFont, customLabel) + legendGap * 2 + markerGap * 3 + 18;
    int legendX = panelWidth - padding - legendWidth;
    const int legendY = footerY + (footerHeight - TTF_FontHeight(smallFont)) / 2;
    const int legendCenterY = footerY + footerHeight / 2;
    drawDiamond(legendX + 4, legendCenterY, 4, favorite);
    legendX += markerGap;
    drawText(surface, smallFont, favoriteLabel, secondaryText, legendX, legendY);
    legendX += textWidth(smallFont, favoriteLabel) + legendGap;
    drawDot(legendX + 4, legendCenterY, 4, active);
    legendX += markerGap;
    drawText(surface, smallFont, activeLabelText, secondaryText, legendX, legendY);
    legendX += textWidth(smallFont, activeLabelText) + legendGap;
    fillRect(surface, {legendX, legendCenterY - 6, 3, 12}, accent);
    legendX += markerGap;
    drawText(surface, smallFont, customLabel, secondaryText, legendX, legendY);

    TTF_CloseFont(titleFont);
    TTF_CloseFont(bodyFont);
    TTF_CloseFont(smallFont);
    TTF_CloseFont(sectionFont);
    return surface;
}

SDL_Surface* OverlayManager::RenderTextOutlinedWrapped(TTF_Font* font, const char* text, SDL_Color textColor, SDL_Color outlineColor, int outlineWidth, int wrapWidth) {
    if (text == nullptr || text[0] == '\0') {
        return nullptr;
    }

    int oldOutline = TTF_GetFontOutline(font);
    TTF_SetFontOutline(font, outlineWidth);

    // Verify that the string won't require wrapping (which could cause the outline and the text
    // to diverge due to different wrapping positions).
    //
    // FIXME: We do this rather than just disabling wrapping entirely (wrapWidth = 0) because we
    // need further testing to ensure that all renderers can handle non-NPOT overlay textures.
    for (const QString& line : QString(text).split('\n')) {
        int extent, count;
        if (TTF_MeasureUTF8(font, line.toUtf8(), wrapWidth, &extent, &count) == 0 && count < line.size()) {
            // If it requires wrapping, render it without the outline
            TTF_SetFontOutline(font, oldOutline);
            return TTF_RenderUTF8_Blended_Wrapped(font, text, textColor, wrapWidth);
        }
    }

    // Draw text twice, but outline is a bit bigger
    auto outlineSurface = TTF_RenderUTF8_Blended_Wrapped(font, text, outlineColor, wrapWidth);
    TTF_SetFontOutline(font, 0);
    auto textSurface = TTF_RenderUTF8_Blended_Wrapped(font, text, textColor, wrapWidth);
    TTF_SetFontOutline(font, oldOutline);

    if (outlineSurface == nullptr || textSurface == nullptr) {
        SDL_FreeSurface(outlineSurface);
        SDL_FreeSurface(textSurface);
        return nullptr;
    }

    // Merge the texts
    SDL_Rect dst = { outlineWidth, outlineWidth, textSurface->w, textSurface->h };
    SDL_BlitSurface(textSurface, nullptr, outlineSurface, &dst);

    SDL_FreeSurface(textSurface);
    return outlineSurface;
}


