#pragma once

#include <QString>
#include <QVector>

#include "SDL_compat.h"
#include <SDL_ttf.h>

namespace Overlay {

enum OverlayType {
    OverlayDebug,
    OverlayStatusUpdate,
    OverlayQuickKeyProfiles,
    OverlayMax
};

struct QuickKeyProfileRow
{
    QString name;
    bool selected = false;
    bool active = false;
    bool favorite = false;
};

struct QuickKeyBindingRow
{
    QString source;
    QString actionName;
    QString actionDetails;
    bool modified = false;
};

struct QuickKeyOverlayContent
{
    QString activeProfile;
    QString selectedProfile;
    QVector<QuickKeyProfileRow> profiles;
    QVector<QuickKeyBindingRow> bindings;
    int firstVisibleIndex = 0;
    int totalProfiles = 0;
    int viewportWidth = 0;
    int viewportHeight = 0;
};

class IOverlayRenderer
{
public:
    virtual ~IOverlayRenderer() = default;

    virtual void notifyOverlayUpdated(OverlayType type) = 0;
};

class OverlayManager
{
public:
    OverlayManager();
    ~OverlayManager();

    bool isOverlayEnabled(OverlayType type);
    char* getOverlayText(OverlayType type);
    void updateOverlayText(OverlayType type, const char* text);
    void updateQuickKeyOverlay(const QuickKeyOverlayContent& content);
    int getOverlayMaxTextLength();
    void setOverlayTextUpdated(OverlayType type);
    void setOverlayState(OverlayType type, bool enabled);
    SDL_Color getOverlayColor(OverlayType type);
    int getOverlayFontSize(OverlayType type);
    int getOverlayWidth(OverlayType type);
    int getOverlayHeight(OverlayType type);
    int getOverlayLineHeight(OverlayType type);
    int getQuickKeyProfileRowAt(int x, int y) const;
    SDL_Surface* getUpdatedOverlaySurface(OverlayType type);

    void setOverlayRenderer(IOverlayRenderer* renderer);

private:
    void notifyOverlayUpdated(OverlayType type);
    SDL_Surface* RenderTextOutlinedWrapped(TTF_Font* font, const char* text, SDL_Color textColor, SDL_Color outlineColor, int outlineWidth, int wrapWidth);
    SDL_Surface* RenderQuickKeyOverlay();

    struct {
        bool enabled;
        int fontSize;
        int width;
        int height;
        SDL_Color color;
        char text[1024];

        TTF_Font* font;
        SDL_Surface* surface;
    } m_Overlays[OverlayMax];
    IOverlayRenderer* m_Renderer;
    QByteArray m_FontData;
    QByteArray m_UiFontData;
    QuickKeyOverlayContent m_QuickKeyContent;
    QVector<SDL_Rect> m_QuickKeyProfileRects;
    bool m_HasQuickKeyContent = false;
};

}
