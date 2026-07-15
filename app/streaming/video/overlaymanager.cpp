#include "overlaymanager.h"
#include "path.h"
#include "quickkeyoverlaylayout.h"

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
    const QString fontsPath = qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows")) +
                              QStringLiteral("/Fonts/");
    QFile uiFont(fontsPath + QStringLiteral("segoeui.ttf"));
    if (uiFont.open(QIODevice::ReadOnly)) m_UiFontData = uiFont.readAll();
    QFile semiboldFont(fontsPath + QStringLiteral("seguisb.ttf"));
    if (semiboldFont.open(QIODevice::ReadOnly))
        m_UiSemiboldFontData = semiboldFont.readAll();
#endif
    if (m_UiFontData.isEmpty()) m_UiFontData = m_FontData;
    if (m_UiSemiboldFontData.isEmpty()) m_UiSemiboldFontData = m_UiFontData;

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

void OverlayManager::setQuickKeyViewportSize(int width, int height)
{
    m_QuickKeyContent.viewportWidth = width;
    m_QuickKeyContent.viewportHeight = height;
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
    const bool renderQuickKeyToast = type == OverlayQuickKeyProfiles && !m_HasQuickKeyContent;

    // Construct the required font to render ordinary text overlays. The QuickKey
    // panel creates scaled UI fonts as part of its structured rendering path.
    if (!renderQuickKeyPanel && !renderQuickKeyToast && m_Overlays[type].font == nullptr) {
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
        newSurface = renderQuickKeyPanel ? RenderQuickKeyOverlayThreePanel() :
            renderQuickKeyToast ? RenderQuickKeyToast() :
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

SDL_Surface* OverlayManager::RenderQuickKeyOverlayThreePanel()
{
    m_QuickKeyProfileRects.clear();
    if (m_UiFontData.isEmpty() || m_QuickKeyContent.profiles.isEmpty()) return nullptr;

    const QuickKeyOverlayLayout layout = QuickKeyOverlayLayout::calculate(
        m_QuickKeyContent.viewportWidth,
        m_QuickKeyContent.viewportHeight,
        static_cast<int>(m_QuickKeyContent.profiles.size()));
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, layout.panelWidth + layout.shadowSize, layout.panelHeight + layout.shadowSize,
        32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 0, 0, 0, 0));

    auto openFont = [this](int size, bool semibold = false) {
        const QByteArray& data = semibold ? m_UiSemiboldFontData : m_UiFontData;
        TTF_Font* font = TTF_OpenFontRW(
            SDL_RWFromConstMem(data.constData(), data.size()), 1, size);
        if (font) TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
        return font;
    };
    const int profileRowHeight = layout.profileRows.isEmpty() ? 18 :
                                 layout.profileRows.first().height();
    const int bindingRowHeight = layout.bindingRows.front().height();
    const int modeRowHeight = layout.leftModeSwitch.height();
    TTF_Font* titleFont = openFont(std::max(20, static_cast<int>(28 * layout.scale)), true);
    TTF_Font* bodyFont = openFont(std::max(6, std::min(
        static_cast<int>(17 * layout.scale), profileRowHeight - 2)));
    TTF_Font* actionFont = openFont(std::max(6, std::min(
        static_cast<int>(16 * layout.scale), bindingRowHeight - 2)), true);
    TTF_Font* smallFont = openFont(std::max(10, static_cast<int>(13 * layout.scale)));
    TTF_Font* modeFont = openFont(std::max(5, std::min(
        static_cast<int>(12 * layout.scale), modeRowHeight - 2)), true);
    TTF_Font* sectionFont = openFont(std::max(10, static_cast<int>(13 * layout.scale)), true);
    if (!titleFont || !bodyFont || !actionFont || !smallFont || !modeFont || !sectionFont) {
        TTF_CloseFont(titleFont);
        TTF_CloseFont(bodyFont);
        TTF_CloseFont(actionFont);
        TTF_CloseFont(smallFont);
        TTF_CloseFont(modeFont);
        TTF_CloseFont(sectionFont);
        SDL_FreeSurface(surface);
        return nullptr;
    }

    const SDL_Color shadow{0, 0, 0, 140};
    const SDL_Color border{68, 74, 82, 255};       // #444A52
    const SDL_Color background{24, 26, 29, 248};   // #181A1D
    const SDL_Color chrome{32, 35, 40, 255};       // #202328
    const SDL_Color card{36, 39, 44, 255};         // #24272C
    const SDL_Color alternate{43, 47, 53, 255};    // #2B2F35
    const SDL_Color primary{243, 244, 246, 255};   // #F3F4F6
    const SDL_Color secondary{183, 188, 196, 255}; // #B7BCC4
    const SDL_Color blue{59, 130, 246, 255};       // #3B82F6
    const SDL_Color selected{38, 59, 85, 255};     // #263B55
    const SDL_Color green{78, 203, 141, 255};      // #4ECB8D
    const SDL_Color amber{242, 193, 78, 255};      // #F2C14E
    const SDL_Color violet{167, 139, 250, 255};    // #A78BFA
    const SDL_Color badge{52, 56, 63, 255};

    auto asSdlRect = [](const QRect& rect) {
        return SDL_Rect{rect.x(), rect.y(), rect.width(), rect.height()};
    };
    auto strokeRect = [&](const QRect& rect, SDL_Color color) {
        fillRect(surface, {rect.x(), rect.y(), rect.width(), 1}, color);
        fillRect(surface, {rect.x(), rect.bottom(), rect.width(), 1}, color);
        fillRect(surface, {rect.x(), rect.y(), 1, rect.height()}, color);
        fillRect(surface, {rect.right(), rect.y(), 1, rect.height()}, color);
    };
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

    fillRect(surface, {layout.shadowSize, layout.shadowSize,
                       layout.panelWidth, layout.panelHeight}, shadow);
    fillRect(surface, {0, 0, layout.panelWidth, layout.panelHeight}, border);
    fillRect(surface, {2, 2, layout.panelWidth - 4, layout.panelHeight - 4}, background);
    fillRect(surface, {2, 2, layout.panelWidth - 4, layout.headerHeight - 2}, chrome);
    fillRect(surface, {2, layout.panelHeight - layout.footerHeight,
                       layout.panelWidth - 4, layout.footerHeight - 2}, chrome);

    const int titleY = std::max(9, static_cast<int>(13 * layout.scale));
    drawText(surface, titleFont, QStringLiteral("QuickKey Presets"), primary,
             layout.padding, titleY, static_cast<int>(layout.panelWidth * 0.55f));
    drawText(surface, smallFont,
             QStringLiteral("Mappings for: %1").arg(m_QuickKeyContent.selectedProfile),
             secondary, layout.padding,
             titleY + TTF_FontHeight(titleFont) + std::max(2, static_cast<int>(4 * layout.scale)),
             static_cast<int>(layout.panelWidth * 0.60f));

    const QString activeLabel = QStringLiteral("ACTIVE  %1").arg(m_QuickKeyContent.activeProfile);
    const int activePad = std::max(9, static_cast<int>(12 * layout.scale));
    const int activeHeight = TTF_FontHeight(sectionFont) + std::max(8, static_cast<int>(10 * layout.scale));
    const int activeWidth = std::min(layout.panelWidth / 3,
                                     textWidth(sectionFont, activeLabel) + activePad * 2);
    const QRect activeBadge(layout.panelWidth - layout.padding - activeWidth,
                            titleY + std::max(2, static_cast<int>(4 * layout.scale)),
                            activeWidth, activeHeight);
    fillRect(surface, asSdlRect(activeBadge), {38, 55, 48, 255});
    strokeRect(activeBadge, green);
    drawText(surface, sectionFont, activeLabel, green,
             activeBadge.x() + activePad,
             activeBadge.y() + (activeBadge.height() - TTF_FontHeight(sectionFont)) / 2,
             activeBadge.width() - activePad * 2);

    for (const QRect& panel : {layout.leftPanel, layout.presetPanel, layout.rightPanel}) {
        fillRect(surface, asSdlRect(panel), card);
        strokeRect(panel, border);
        fillRect(surface, {panel.x(), panel.y() + layout.sectionHeight - 1,
                           panel.width(), 1}, border);
    }
    const int headingY = layout.leftPanel.y() +
                         (layout.sectionHeight - TTF_FontHeight(sectionFont)) / 2;
    const int headingPad = std::max(8, static_cast<int>(11 * layout.scale));
    drawText(surface, sectionFont, QStringLiteral("LEFT QUICKKEYS · F1–F5"), secondary,
             layout.leftPanel.x() + headingPad, headingY,
             layout.leftPanel.width() - headingPad * 2);
    drawText(surface, sectionFont, QStringLiteral("PRESETS"), secondary,
             layout.presetPanel.x() + headingPad, headingY,
             layout.presetPanel.width() - headingPad * 2);
    drawText(surface, sectionFont, QStringLiteral("RIGHT QUICKKEYS · F6–F10"), secondary,
             layout.rightPanel.x() + headingPad, headingY,
             layout.rightPanel.width() - headingPad * 2);

    const QString pageLabel = QStringLiteral("%1–%2 OF %3")
        .arg(m_QuickKeyContent.firstVisibleIndex + 1)
        .arg(m_QuickKeyContent.firstVisibleIndex + m_QuickKeyContent.profiles.size())
        .arg(m_QuickKeyContent.totalProfiles);
    if (layout.presetPanel.width() >= 320) {
        drawText(surface, smallFont, pageLabel, secondary,
                 layout.presetPanel.right() - headingPad - textWidth(smallFont, pageLabel),
                 layout.presetPanel.y() + (layout.sectionHeight - TTF_FontHeight(smallFont)) / 2);
    }

    for (int row = 0; row < m_QuickKeyContent.profiles.size() &&
                            row < layout.profileRows.size(); ++row) {
        const auto& profile = m_QuickKeyContent.profiles[row];
        const QRect& qRow = layout.profileRows[row];
        const SDL_Rect rowRect = asSdlRect(qRow);
        m_QuickKeyProfileRects.append(rowRect);
        if (profile.selected) {
            fillRect(surface, rowRect, selected);
            fillRect(surface, {rowRect.x, rowRect.y,
                               std::max(3, static_cast<int>(4 * layout.scale)), rowRect.h}, blue);
        } else if (row % 2) {
            fillRect(surface, rowRect, alternate);
        }

        const int centerY = rowRect.y + rowRect.h / 2;
        if (profile.selected) {
            drawText(surface, sectionFont, QStringLiteral(">"), primary,
                     rowRect.x + std::max(5, static_cast<int>(7 * layout.scale)),
                     rowRect.y + (rowRect.h - TTF_FontHeight(sectionFont)) / 2);
        }
        const int favoriteX = rowRect.x + std::max(20, static_cast<int>(24 * layout.scale));
        if (profile.favorite)
            drawDiamond(favoriteX, centerY, std::max(3, static_cast<int>(4 * layout.scale)), amber);
        const int activeX = rowRect.x + std::max(37, static_cast<int>(44 * layout.scale));
        if (profile.active)
            drawDot(activeX, centerY, std::max(3, static_cast<int>(4 * layout.scale)), green);
        const int nameX = rowRect.x + std::max(51, static_cast<int>(60 * layout.scale));
        drawText(surface, bodyFont, profile.name, primary, nameX,
                 rowRect.y + (rowRect.h - TTF_FontHeight(bodyFont)) / 2,
                 rowRect.x + rowRect.w - nameX - headingPad);
    }

    QVector<int> bindingSlots;
    bindingSlots.reserve(m_QuickKeyContent.bindings.size());
    for (const auto& binding : m_QuickKeyContent.bindings)
        bindingSlots.append(binding.slot);
    const auto bindingIndexes = QuickKeyOverlayLayout::bindingIndexesBySlot(bindingSlots);

    auto drawModeSwitch = [&](const QRect& rect) {
        fillRect(surface, asSdlRect(rect), chrome);
        const QString label = rect.width() >= 260 ?
                                  QStringLiteral("MODE SWITCH · HARDWARE ONLY") :
                                  QStringLiteral("MODE · HW ONLY");
        const int labelArea = std::max(1, rect.width() - headingPad * 2);
        const int labelWidth = std::min(textWidth(modeFont, label), labelArea);
        drawText(surface, modeFont, label, secondary,
                 rect.x() + (rect.width() - labelWidth) / 2,
                 rect.y() + (rect.height() - TTF_FontHeight(modeFont)) / 2,
                 labelArea);
    };
    drawModeSwitch(layout.leftModeSwitch);
    drawModeSwitch(layout.rightModeSwitch);

    for (int slot = 0; slot < QuickKeyOverlayLayout::BindingCount; ++slot) {
        const int bindingIndex = bindingIndexes[slot];
        const QuickKeyBindingRow* binding = bindingIndex >= 0 ?
                                                &m_QuickKeyContent.bindings[bindingIndex] : nullptr;
        const QRect& qRow = layout.bindingRows[slot];
        const SDL_Rect rowRect = asSdlRect(qRow);
        const int localSlot = slot % QuickKeyOverlayLayout::BindingsPerSide;
        if (localSlot % 2) fillRect(surface, rowRect, alternate);
        if (binding && binding->modified) {
            fillRect(surface, {rowRect.x, rowRect.y,
                               std::max(3, static_cast<int>(4 * layout.scale)), rowRect.h}, violet);
        }

        const int rowPad = std::max(6, static_cast<int>(9 * layout.scale));
        const int badgeWidth = std::max(1, std::min(rowRect.w / 4,
                                                    std::max(42, static_cast<int>(58 * layout.scale))));
        const int badgeHeight = std::max(1, std::min(std::max(1, rowRect.h - 8),
                                                     std::max(23, static_cast<int>(30 * layout.scale))));
        const QRect badgeRect(rowRect.x + rowPad,
                              rowRect.y + (rowRect.h - badgeHeight) / 2,
                              badgeWidth, badgeHeight);
        fillRect(surface, asSdlRect(badgeRect), badge);
        strokeRect(badgeRect, border);
        const QString fallbackSource = QStringLiteral("F%1").arg(slot + 1);
        const QString source = binding && !binding->source.isEmpty() ?
                                   binding->source : fallbackSource;
        const int sourceArea = std::max(1, badgeRect.width() - 4);
        const int sourceWidth = std::min(textWidth(actionFont, source), sourceArea);
        drawText(surface, actionFont, source, primary,
                 badgeRect.x() + (badgeRect.width() - sourceWidth) / 2,
                 badgeRect.y() + (badgeRect.height() - TTF_FontHeight(actionFont)) / 2,
                 sourceArea);

        const int actionX = badgeRect.right() + 1 + rowPad;
        const int actionWidth = std::max(1, rowRect.x + rowRect.w - actionX - rowPad);
        QString actionName = binding && !binding->actionName.isEmpty() ?
                                 binding->actionName : QStringLiteral("Unassigned");
        if (binding && binding->modified) actionName += QStringLiteral("  [CUSTOM]");
        const bool showDetails = layout.showActionDetails && binding &&
                                 !binding->actionDetails.isEmpty() && rowRect.h >= 42;
        drawText(surface, actionFont, actionName, primary, actionX,
                 showDetails ? rowRect.y + std::max(3, static_cast<int>(5 * layout.scale)) :
                               rowRect.y + (rowRect.h - TTF_FontHeight(actionFont)) / 2,
                 actionWidth);
        if (showDetails) {
            drawText(surface, smallFont, binding->actionDetails, secondary, actionX,
                     rowRect.y + rowRect.h - TTF_FontHeight(smallFont) -
                         std::max(3, static_cast<int>(5 * layout.scale)),
                     actionWidth);
        }
    }

    const int footerY = layout.panelHeight - layout.footerHeight;
    const QString instructions = QStringLiteral(
        "Arrow keys / wheel  Navigate     Enter / tap  Select     Esc  Cancel");
    drawText(surface, smallFont, instructions, primary, layout.padding,
             footerY + (layout.footerHeight - TTF_FontHeight(smallFont)) / 2,
             layout.compact ? layout.panelWidth - layout.padding * 2 :
                              static_cast<int>(layout.panelWidth * 0.62f));

    if (!layout.compact) {
        const QString favoriteLabel = QStringLiteral("Favorite");
        const QString activeText = QStringLiteral("Active");
        const QString customText = QStringLiteral("Custom");
        const int gap = std::max(12, static_cast<int>(16 * layout.scale));
        const int markerGap = std::max(7, static_cast<int>(9 * layout.scale));
        const int legendWidth = textWidth(smallFont, favoriteLabel) +
                                textWidth(smallFont, activeText) +
                                textWidth(smallFont, customText) + gap * 2 + markerGap * 3 + 18;
        int x = layout.panelWidth - layout.padding - legendWidth;
        const int textY = footerY + (layout.footerHeight - TTF_FontHeight(smallFont)) / 2;
        const int centerY = footerY + layout.footerHeight / 2;
        drawDiamond(x + 4, centerY, 4, amber);
        x += markerGap;
        drawText(surface, smallFont, favoriteLabel, secondary, x, textY);
        x += textWidth(smallFont, favoriteLabel) + gap;
        drawDot(x + 4, centerY, 4, green);
        x += markerGap;
        drawText(surface, smallFont, activeText, secondary, x, textY);
        x += textWidth(smallFont, activeText) + gap;
        fillRect(surface, {x, centerY - 6, 3, 12}, violet);
        x += markerGap;
        drawText(surface, smallFont, customText, secondary, x, textY);
    }

    TTF_CloseFont(titleFont);
    TTF_CloseFont(bodyFont);
    TTF_CloseFont(actionFont);
    TTF_CloseFont(smallFont);
    TTF_CloseFont(modeFont);
    TTF_CloseFont(sectionFont);
    return surface;
}

SDL_Surface* OverlayManager::RenderQuickKeyToast()
{
    if (m_UiSemiboldFontData.isEmpty() || m_Overlays[OverlayQuickKeyProfiles].text[0] == '\0')
        return nullptr;

    const int viewportHeight = std::max(1, m_QuickKeyContent.viewportHeight);
    const float scale = std::clamp(viewportHeight / 1440.0f, 0.78f, 1.50f);
    TTF_Font* font = TTF_OpenFontRW(
        SDL_RWFromConstMem(m_UiSemiboldFontData.constData(), m_UiSemiboldFontData.size()),
        1, std::max(16, static_cast<int>(20 * scale)));
    if (!font) return nullptr;
    TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
    const QString text = QString::fromUtf8(m_Overlays[OverlayQuickKeyProfiles].text);
    const int paddingX = std::max(18, static_cast<int>(24 * scale));
    const int paddingY = std::max(10, static_cast<int>(14 * scale));
    const int accentWidth = std::max(4, static_cast<int>(5 * scale));
    const int shadowSize = std::max(5, static_cast<int>(7 * scale));
    const int width = textWidth(font, text) + paddingX * 2 + accentWidth;
    const int height = TTF_FontHeight(font) + paddingY * 2;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, width + shadowSize, height + shadowSize, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        TTF_CloseFont(font);
        return nullptr;
    }
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 0, 0, 0, 0));
    fillRect(surface, {shadowSize, shadowSize, width, height}, {0, 0, 0, 140});
    fillRect(surface, {0, 0, width, height}, {68, 74, 82, 255});
    fillRect(surface, {1, 1, width - 2, height - 2}, {32, 35, 40, 250});
    fillRect(surface, {1, 1, accentWidth, height - 2}, {78, 203, 141, 255});
    drawText(surface, font, text, {243, 244, 246, 255},
             accentWidth + paddingX, paddingY);
    TTF_CloseFont(font);
    return surface;
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


