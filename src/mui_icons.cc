#include "mui_icons.h"

#include <ctype.h>
#include <stdlib.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

namespace fallout {

namespace {

    constexpr float kPi = 3.14159265f;

    // Draws in icon units: the icon square is -0.5..0.5 on both axes, y goes
    // down, angles go clockwise from +x (degrees).
    class Pen {
    public:
        Pen(float x, float y, float size, float thickness, MuiColor color)
            : cx(x)
            , cy(y)
            , size(size)
            , thickness(thickness)
            , color(color)
        {
        }

        SDL_FPoint p(float x, float y) const { return { cx + x * size, cy + y * size }; }

        void line(float x1, float y1, float x2, float y2) const
        {
            poly({ { x1, y1 }, { x2, y2 } });
        }

        void poly(std::initializer_list<SDL_FPoint> units, bool closed = false) const
        {
            std::vector<SDL_FPoint> points;
            for (const SDL_FPoint& unit : units) {
                points.push_back(p(unit.x, unit.y));
            }
            muiDrawPolyline(points, thickness, color, closed);
        }

        void fill(std::initializer_list<SDL_FPoint> units) const
        {
            std::vector<SDL_FPoint> points;
            for (const SDL_FPoint& unit : units) {
                points.push_back(p(unit.x, unit.y));
            }
            muiFillConvex(points, color);
        }

        std::vector<SDL_FPoint> arcPoints(float x, float y, float radius, float from, float to) const
        {
            int segments = std::max(static_cast<int>(std::fabs(to - from) / 15.0f), 2);
            std::vector<SDL_FPoint> points;
            for (int step = 0; step <= segments; step++) {
                float angle = (from + (to - from) * step / segments) * kPi / 180.0f;
                points.push_back(p(x + std::cos(angle) * radius, y + std::sin(angle) * radius));
            }
            return points;
        }

        void arc(float x, float y, float radius, float from, float to) const
        {
            muiDrawPolyline(arcPoints(x, y, radius, from, to), thickness, color);
        }

        // Arc with an arrow head at its end, pointing along the arc.
        void arcArrow(float x, float y, float radius, float from, float to) const
        {
            arc(x, y, radius, from, to);

            float angle = to * kPi / 180.0f;
            float direction = to > from ? 1.0f : -1.0f;
            float endX = x + std::cos(angle) * radius;
            float endY = y + std::sin(angle) * radius;
            // Tangent in the direction of travel.
            float tangentX = -std::sin(angle) * direction;
            float tangentY = std::cos(angle) * direction;
            float head = 0.2f;
            float spread = 0.6f;
            float leftX = -tangentX * head + -tangentY * head * spread;
            float leftY = -tangentY * head + tangentX * head * spread;
            float rightX = -tangentX * head + tangentY * head * spread;
            float rightY = -tangentY * head + -tangentX * head * spread;
            poly({ { endX + leftX, endY + leftY }, { endX, endY }, { endX + rightX, endY + rightY } });
        }

        void circle(float x, float y, float radius) const
        {
            muiStrokeCircle(cx + x * size, cy + y * size, radius * size + thickness / 2.0f, thickness, color);
        }

        void dot(float x, float y, float radius) const
        {
            muiFillCircle(cx + x * size, cy + y * size, radius * size, color);
        }

        void rect(float x, float y, float w, float h, float radius) const
        {
            MuiRect rect = { cx + x * size - thickness / 2.0f, cy + y * size - thickness / 2.0f, w * size + thickness, h * size + thickness };
            muiStrokeRoundRect(rect, radius * size + thickness / 2.0f, thickness, color);
        }

        // Quadratic curve.
        void curve(SDL_FPoint from, SDL_FPoint control, SDL_FPoint to) const
        {
            std::vector<SDL_FPoint> points;
            constexpr int kSegments = 10;
            for (int step = 0; step <= kSegments; step++) {
                float t = static_cast<float>(step) / kSegments;
                float a = (1 - t) * (1 - t);
                float b = 2 * (1 - t) * t;
                float c = t * t;
                points.push_back(p(a * from.x + b * control.x + c * to.x, a * from.y + b * control.y + c * to.y));
            }
            muiDrawPolyline(points, thickness, color);
        }

        float cx;
        float cy;
        float size;
        float thickness;
        MuiColor color;
    };

    // MARK: HUD

    void drawMenu(const Pen& pen)
    {
        for (int line = -1; line <= 1; line++) {
            pen.line(-0.42f, line * 0.3f, 0.42f, line * 0.3f);
        }
    }

    // Floppy disk with an arrow into (save) or out of (load) it.
    void drawFloppy(const Pen& pen, bool save)
    {
        pen.poly({ { -0.4f, -0.32f }, { 0.24f, -0.32f }, { 0.4f, -0.16f }, { 0.4f, 0.48f }, { -0.4f, 0.48f } }, true);
        pen.poly({ { -0.22f, 0.48f }, { -0.22f, 0.2f }, { 0.22f, 0.2f }, { 0.22f, 0.48f } });

        pen.line(0.0f, -0.52f, 0.0f, 0.02f);
        if (save) {
            pen.poly({ { -0.16f, -0.14f }, { 0.0f, 0.02f }, { 0.16f, -0.14f } });
        } else {
            pen.poly({ { -0.16f, -0.36f }, { 0.0f, -0.52f }, { 0.16f, -0.36f } });
        }
    }

    // Backpack.
    void drawInventory(const Pen& pen)
    {
        pen.arc(0.0f, -0.28f, 0.17f, 180.0f, 360.0f);
        pen.rect(-0.42f, -0.28f, 0.84f, 0.76f, 0.16f);
        pen.line(-0.42f, 0.0f, 0.42f, 0.0f);
        pen.poly({ { -0.18f, 0.0f }, { -0.18f, 0.2f }, { 0.18f, 0.2f }, { 0.18f, 0.0f } });
    }

    // Head and shoulders.
    void drawCharacter(const Pen& pen)
    {
        pen.circle(0.0f, -0.22f, 0.17f);
        pen.arc(0.0f, 0.46f, 0.4f, 180.0f, 360.0f);
        pen.line(-0.4f, 0.46f, 0.4f, 0.46f);
    }

    // Wrist computer: screen, knob and straps.
    void drawPipboy(const Pen& pen)
    {
        pen.rect(-0.46f, -0.3f, 0.92f, 0.6f, 0.08f);
        pen.rect(-0.34f, -0.18f, 0.46f, 0.36f, 0.04f);
        pen.circle(0.3f, -0.06f, 0.06f);
        pen.line(0.26f, 0.16f, 0.34f, 0.16f);
        for (float x : { -0.26f, 0.04f }) {
            pen.line(x, -0.3f, x, -0.46f);
            pen.line(x, 0.3f, x, 0.46f);
        }
    }

    // Folded paper map.
    void drawMap(const Pen& pen)
    {
        pen.poly({ { -0.46f, -0.32f }, { -0.16f, -0.42f }, { 0.16f, -0.32f }, { 0.46f, -0.42f }, { 0.46f, 0.32f }, { 0.16f, 0.42f }, { -0.16f, 0.32f }, { -0.46f, 0.42f } }, true);
        pen.line(-0.16f, -0.42f, -0.16f, 0.32f);
        pen.line(0.16f, -0.32f, 0.16f, 0.42f);
    }

    // Open book (Skilldex).
    void drawSkills(const Pen& pen)
    {
        pen.poly({ { 0.0f, -0.28f }, { -0.46f, -0.38f }, { -0.46f, 0.34f }, { 0.0f, 0.44f }, { 0.46f, 0.34f }, { 0.46f, -0.38f } }, true);
        pen.line(0.0f, -0.28f, 0.0f, 0.44f);
        pen.line(-0.34f, -0.14f, -0.12f, -0.1f);
        pen.line(-0.34f, 0.04f, -0.12f, 0.08f);
        pen.line(0.34f, -0.14f, 0.12f, -0.1f);
        pen.line(0.34f, 0.04f, 0.12f, 0.08f);
    }

    // Light bulb.
    void drawHighlight(const Pen& pen)
    {
        pen.arc(0.0f, -0.14f, 0.3f, 135.0f, 405.0f);
        pen.line(-0.21f, 0.07f, -0.13f, 0.24f);
        pen.line(0.21f, 0.07f, 0.13f, 0.24f);
        pen.line(-0.13f, 0.24f, 0.13f, 0.24f);
        pen.line(-0.11f, 0.36f, 0.11f, 0.36f);
        pen.line(-0.06f, 0.47f, 0.06f, 0.47f);
    }

    void drawEye(const Pen& pen)
    {
        pen.curve({ -0.48f, 0.0f }, { 0.0f, -0.46f }, { 0.48f, 0.0f });
        pen.curve({ -0.48f, 0.0f }, { 0.0f, 0.46f }, { 0.48f, 0.0f });
        pen.circle(0.0f, 0.0f, 0.1f);
    }

    // Eye crossed out: not seen.
    void drawSneak(const Pen& pen)
    {
        drawEye(pen);
        pen.line(-0.38f, 0.38f, 0.38f, -0.38f);
    }

    // Terminal prompt.
    void drawLog(const Pen& pen)
    {
        pen.rect(-0.46f, -0.36f, 0.92f, 0.72f, 0.08f);
        pen.poly({ { -0.28f, -0.14f }, { -0.1f, 0.02f }, { -0.28f, 0.18f } });
        pen.line(0.0f, 0.18f, 0.26f, 0.18f);
    }

    // Play to the end.
    void drawEndTurn(const Pen& pen)
    {
        pen.poly({ { -0.36f, -0.34f }, { 0.16f, 0.0f }, { -0.36f, 0.34f } }, true);
        pen.line(0.32f, -0.34f, 0.32f, 0.34f);
    }

    // Three tiles of the hex grid (flat as the game's).
    void drawTacticalView(const Pen& pen)
    {
        auto hex = [&](float x, float y) {
            const float w = 0.24f;
            const float h = 0.14f;
            pen.poly({ { x - w, y }, { x - w / 2.0f, y - h }, { x + w / 2.0f, y - h }, { x + w, y }, { x + w / 2.0f, y + h }, { x - w / 2.0f, y + h } }, true);
        };
        hex(-0.18f, 0.0f);
        hex(0.18f, -0.14f);
        hex(0.18f, 0.14f);
    }

    // Truce flag.
    void drawEndCombat(const Pen& pen)
    {
        pen.line(-0.34f, -0.46f, -0.34f, 0.48f);
        pen.poly({ { -0.34f, -0.42f }, { 0.42f, -0.42f }, { 0.24f, -0.18f }, { 0.42f, 0.06f }, { -0.34f, 0.06f } });
    }

    // Two arrows chasing each other.
    void drawSwapHands(const Pen& pen)
    {
        pen.arcArrow(0.0f, 0.0f, 0.36f, 200.0f, 330.0f);
        pen.arcArrow(0.0f, 0.0f, 0.36f, 20.0f, 150.0f);
    }

    // Lightning bolt (filled).
    void drawActionPoints(const Pen& pen)
    {
        pen.fill({ { 0.16f, -0.5f }, { -0.3f, 0.08f }, { 0.08f, 0.08f } });
        pen.fill({ { -0.08f, -0.08f }, { 0.3f, -0.08f }, { -0.16f, 0.5f } });
    }

    // Heart (filled).
    void drawHitPoints(const Pen& pen)
    {
        pen.dot(-0.2f, -0.14f, 0.22f);
        pen.dot(0.2f, -0.14f, 0.22f);
        pen.fill({ { -0.41f, -0.06f }, { 0.41f, -0.06f }, { 0.0f, 0.44f } });
    }

    // Crosshair.
    void drawAim(const Pen& pen)
    {
        pen.circle(0.0f, 0.0f, 0.28f);
        pen.line(0.0f, -0.48f, 0.0f, -0.16f);
        pen.line(0.0f, 0.16f, 0.0f, 0.48f);
        pen.line(-0.48f, 0.0f, -0.16f, 0.0f);
        pen.line(0.16f, 0.0f, 0.48f, 0.0f);
        pen.dot(0.0f, 0.0f, 0.05f);
    }

    // Bullet.
    void drawAmmo(const Pen& pen, float x, float y, float scale)
    {
        float half = 0.17f * scale;
        float top = y - 0.15f * scale;
        float bottom = y + 0.5f * scale;
        float tip = y - 0.5f * scale;
        pen.poly({ { x - half, bottom }, { x - half, top }, { x, tip }, { x + half, top }, { x + half, bottom } }, true);
    }

    // Circular arrow around a bullet.
    void drawReload(const Pen& pen)
    {
        pen.arcArrow(0.0f, 0.0f, 0.4f, -60.0f, 230.0f);
        drawAmmo(pen, 0.0f, 0.02f, 0.44f);
    }

    // Fist from the front: four knuckles, thumb across the fingers.
    void drawFist(const Pen& pen)
    {
        for (int finger = 0; finger < 4; finger++) {
            float x = -0.27f + finger * 0.18f;
            pen.arc(x, -0.14f, 0.09f, 180.0f, 360.0f);
            pen.line(x + 0.09f, -0.14f, x + 0.09f, finger == 3 ? 0.2f : 0.02f);
        }
        pen.poly({ { -0.36f, -0.14f }, { -0.36f, 0.24f }, { -0.2f, 0.44f }, { 0.24f, 0.44f }, { 0.36f, 0.24f }, { 0.36f, 0.2f } });
        pen.poly({ { -0.36f, 0.08f }, { 0.08f, 0.08f }, { 0.16f, 0.2f } });
    }

    // Kettlebell.
    void drawWeight(const Pen& pen)
    {
        pen.arc(0.0f, -0.24f, 0.2f, 160.0f, 380.0f);
        pen.circle(0.0f, 0.16f, 0.3f);
        pen.line(-0.22f, 0.46f, 0.22f, 0.46f);
    }

    // Front part of a wide flat ellipse with heads at both ends, pointing
    // sideways.
    void drawTurn(const Pen& pen)
    {
        constexpr float kRadiusX = 0.8f;
        constexpr float kRadiusY = 0.3f;
        constexpr float kCenterY = -0.2f;
        constexpr float kFrom = 55.0f;
        constexpr float kTo = 125.0f;

        std::vector<SDL_FPoint> points;
        for (int step = 0; step <= 12; step++) {
            float angle = (kFrom + (kTo - kFrom) * step / 12.0f) * kPi / 180.0f;
            points.push_back(pen.p(std::cos(angle) * kRadiusX, kCenterY + std::sin(angle) * kRadiusY));
        }
        muiDrawPolyline(points, pen.thickness, pen.color);

        // Heads point along the ellipse, away from its middle.
        for (float end : { kFrom, kTo }) {
            float angle = end * kPi / 180.0f;
            float x = std::cos(angle) * kRadiusX;
            float y = kCenterY + std::sin(angle) * kRadiusY;
            float direction = end == kFrom ? -1.0f : 1.0f;
            float tangentX = -std::sin(angle) * kRadiusX * direction;
            float tangentY = std::cos(angle) * kRadiusY * direction;
            float length = std::sqrt(tangentX * tangentX + tangentY * tangentY);
            tangentX /= length;
            tangentY /= length;
            float head = 0.14f;
            pen.poly({ { x - tangentX * head - tangentY * head * 0.8f, y - tangentY * head + tangentX * head * 0.8f },
                { x, y },
                { x - tangentX * head + tangentY * head * 0.8f, y - tangentY * head - tangentX * head * 0.8f } });
        }
    }

    void drawBack(const Pen& pen)
    {
        pen.line(-0.42f, 0.0f, 0.42f, 0.0f);
        pen.poly({ { -0.06f, -0.36f }, { -0.42f, 0.0f }, { -0.06f, 0.36f } });
    }

    // MARK: Dialog

    // Speech bubble: one outline, the tail opens out of the bottom edge.
    void drawTalk(const Pen& pen)
    {
        constexpr float kLeft = -0.5f;
        constexpr float kTop = -0.5f;
        constexpr float kRight = 0.5f;
        constexpr float kBottom = 0.22f;
        constexpr float kRadius = 0.18f;

        std::vector<SDL_FPoint> points;
        auto arc = [&](float x, float y, float from, float to) {
            std::vector<SDL_FPoint> arcPoints = pen.arcPoints(x, y, kRadius, from, to);
            points.insert(points.end(), arcPoints.begin(), arcPoints.end());
        };

        // From the tail's right side clockwise round the corners.
        points.push_back(pen.p(-0.02f, kBottom));
        arc(kRight - kRadius, kBottom - kRadius, 90.0f, 0.0f);
        arc(kRight - kRadius, kTop + kRadius, 0.0f, -90.0f);
        arc(kLeft + kRadius, kTop + kRadius, -90.0f, -180.0f);
        arc(kLeft + kRadius, kBottom - kRadius, 180.0f, 90.0f);
        points.push_back(pen.p(-0.25f, kBottom));
        points.push_back(pen.p(-0.38f, 0.5f));
        muiDrawPolyline(points, pen.thickness, pen.color, true);
    }

    // Two opposite arrows.
    void drawBarter(const Pen& pen)
    {
        float head = 0.28f;
        pen.line(-0.5f, -0.22f, 0.5f, -0.22f);
        pen.poly({ { 0.5f - head, -0.22f - head }, { 0.5f, -0.22f }, { 0.5f - head, -0.22f + head } });
        pen.line(0.5f, 0.22f, -0.5f, 0.22f);
        pen.poly({ { -0.5f + head, 0.22f - head }, { -0.5f, 0.22f }, { -0.5f + head, 0.22f + head } });
    }

    // Clock.
    void drawReview(const Pen& pen)
    {
        pen.circle(0.0f, 0.0f, 0.46f);
        pen.poly({ { 0.0f, -0.28f }, { 0.0f, 0.0f }, { 0.2f, 0.08f } });
    }

    // MARK: Filters

    // Pistol.
    void drawWeapon(const Pen& pen, float x, float y, float scale)
    {
        float top = y - 0.22f * scale;
        pen.line(x - 0.5f * scale, top, x + 0.5f * scale, top);
        pen.line(x - 0.28f * scale, top, x - 0.4f * scale, y + 0.5f * scale);
        pen.line(x - 0.05f * scale, top, x - 0.05f * scale, y + 0.12f * scale);
    }

    // Shield.
    void drawArmor(const Pen& pen)
    {
        pen.poly({ { -0.4f, -0.5f }, { 0.4f, -0.5f }, { 0.4f, 0.08f }, { 0.0f, 0.5f }, { -0.4f, 0.08f } }, true);
    }

    // Medical cross.
    void drawDrugs(const Pen& pen)
    {
        Pen thick = pen;
        thick.thickness *= 2.0f;
        thick.line(-0.5f, 0.0f, 0.5f, 0.0f);
        thick.line(0.0f, -0.5f, 0.0f, 0.5f);
    }

    // Box.
    void drawMisc(const Pen& pen)
    {
        pen.rect(-0.5f, -0.28f, 1.0f, 0.78f, 0.04f);
        pen.line(-0.5f, -0.03f, 0.5f, -0.03f);
    }

    // MARK: Action menu

    void drawCancel(const Pen& pen)
    {
        pen.line(-0.34f, -0.34f, 0.34f, 0.34f);
        pen.line(-0.34f, 0.34f, 0.34f, -0.34f);
    }

    void drawPlus(const Pen& pen)
    {
        pen.line(-0.36f, 0.0f, 0.36f, 0.0f);
        pen.line(0.0f, -0.36f, 0.0f, 0.36f);
    }

    void drawMinus(const Pen& pen)
    {
        pen.line(-0.36f, 0.0f, 0.36f, 0.0f);
    }

    void drawPlay(const Pen& pen)
    {
        pen.fill({ { -0.26f, -0.4f }, { 0.4f, 0.0f }, { -0.26f, 0.4f } });
    }

    void drawPause(const Pen& pen)
    {
        pen.fill({ { -0.34f, -0.38f }, { -0.1f, -0.38f }, { -0.1f, 0.38f }, { -0.34f, 0.38f } });
        pen.fill({ { 0.1f, -0.38f }, { 0.34f, -0.38f }, { 0.34f, 0.38f }, { 0.1f, 0.38f } });
    }

    void drawStop(const Pen& pen)
    {
        pen.fill({ { -0.32f, -0.32f }, { 0.32f, -0.32f }, { 0.32f, 0.32f }, { -0.32f, 0.32f } });
    }

    // Corners out (full screen) or in (back from it).
    void drawCorners(const Pen& pen, bool out)
    {
        float a = out ? 0.42f : 0.12f;
        float b = out ? 0.12f : 0.42f;
        for (float sx : { -1.0f, 1.0f }) {
            for (float sy : { -1.0f, 1.0f }) {
                pen.poly({ { sx * a, sy * b }, { sx * a, sy * a }, { sx * b, sy * a } });
            }
        }
    }

    // Pencil.
    void drawEdit(const Pen& pen)
    {
        pen.poly({ { -0.36f, 0.4f }, { -0.36f, 0.2f }, { 0.22f, -0.38f }, { 0.42f, -0.18f }, { -0.16f, 0.4f } }, true);
        pen.line(0.08f, -0.24f, 0.28f, -0.04f);
    }

    // Arrow down to the ground.
    void drawDrop(const Pen& pen)
    {
        pen.line(0.0f, -0.46f, 0.0f, 0.16f);
        pen.poly({ { -0.22f, -0.06f }, { 0.0f, 0.16f }, { 0.22f, -0.06f } });
        pen.line(-0.4f, 0.42f, 0.4f, 0.42f);
    }

    // Circular arrow around the object.
    void drawRotate(const Pen& pen)
    {
        pen.arcArrow(0.0f, 0.0f, 0.4f, 120.0f, 400.0f);
        pen.dot(0.0f, 0.0f, 0.09f);
    }

    // Open hand.
    void drawUse(const Pen& pen)
    {
        pen.rect(-0.26f, -0.02f, 0.5f, 0.48f, 0.16f);
        pen.line(-0.18f, -0.02f, -0.18f, -0.3f);
        pen.line(-0.04f, -0.02f, -0.04f, -0.44f);
        pen.line(0.1f, -0.02f, 0.1f, -0.42f);
        pen.line(0.24f, 0.04f, 0.24f, -0.26f);
        pen.line(-0.26f, 0.2f, -0.44f, -0.02f);
    }

    // Magazine with a bullet going out.
    void drawUnload(const Pen& pen)
    {
        pen.rect(-0.22f, 0.02f, 0.44f, 0.46f, 0.06f);
        pen.line(-0.22f, 0.18f, 0.22f, 0.18f);
        pen.line(0.0f, -0.08f, 0.0f, -0.48f);
        pen.poly({ { -0.18f, -0.3f }, { 0.0f, -0.48f }, { 0.18f, -0.3f } });
    }

    // Arrow into a wall.
    void drawPush(const Pen& pen)
    {
        pen.line(-0.46f, 0.0f, 0.12f, 0.0f);
        pen.poly({ { -0.08f, -0.2f }, { 0.12f, 0.0f }, { -0.08f, 0.2f } });
        pen.line(0.34f, -0.4f, 0.34f, 0.4f);
    }

    // MARK: Party orders

    // Two people, the front one bigger; the one behind shows only what the
    // front one doesn't cover.
    void drawPartyOrders(const Pen& pen)
    {
        pen.circle(0.2f, -0.26f, 0.12f);
        pen.arc(0.2f, 0.3f, 0.28f, 250.0f, 340.0f);
        pen.circle(-0.14f, -0.12f, 0.15f);
        pen.arc(-0.14f, 0.48f, 0.34f, 180.0f, 360.0f);
        pen.line(-0.48f, 0.48f, 0.2f, 0.48f);
    }

    // Pistol going down.
    void drawHolster(const Pen& pen)
    {
        drawWeapon(pen, -0.1f, -0.14f, 0.7f);
        pen.line(0.36f, -0.46f, 0.36f, 0.42f);
        pen.poly({ { 0.2f, 0.26f }, { 0.36f, 0.42f }, { 0.52f, 0.26f } });
    }

    // Four arrows to the center (or from it).
    void drawCrossArrows(const Pen& pen, bool inward)
    {
        for (int direction = 0; direction < 4; direction++) {
            float dx = direction == 0 ? 1.0f : (direction == 1 ? -1.0f : 0.0f);
            float dy = direction == 2 ? 1.0f : (direction == 3 ? -1.0f : 0.0f);
            float outer = 0.48f;
            float inner = 0.14f;
            pen.line(dx * outer, dy * outer, dx * inner, dy * inner);
            float tip = inward ? inner : outer;
            float back = inward ? inner + 0.14f : outer - 0.14f;
            pen.poly({ { dx * back - dy * 0.12f, dy * back - dx * 0.12f }, { dx * tip, dy * tip }, { dx * back + dy * 0.12f, dy * back + dx * 0.12f } });
        }
    }

    // Three bullets.
    void drawBurst(const Pen& pen)
    {
        drawAmmo(pen, -0.3f, 0.02f, 0.62f);
        drawAmmo(pen, 0.0f, 0.02f, 0.62f);
        drawAmmo(pen, 0.3f, 0.02f, 0.62f);
    }

    // Hand in a circular arrow: picking up by itself.
    void drawAutoLoot(const Pen& pen)
    {
        pen.arcArrow(0.0f, 0.0f, 0.46f, -70.0f, 220.0f);
        Pen small = pen;
        small.size *= 0.55f;
        small.cy += pen.size * 0.04f;
        drawUse(small);
    }

    // MARK: Skills

    // Open padlock with a keyhole.
    void drawLockpick(const Pen& pen)
    {
        pen.rect(-0.32f, -0.02f, 0.64f, 0.46f, 0.06f);
        pen.arc(0.0f, -0.12f, 0.2f, 180.0f, 330.0f);
        pen.line(-0.2f, -0.12f, -0.2f, -0.02f);
        pen.dot(0.0f, 0.16f, 0.06f);
        pen.line(0.0f, 0.18f, 0.0f, 0.3f);
    }

    // Hand taking a coin.
    void drawSteal(const Pen& pen)
    {
        Pen hand = pen;
        hand.size *= 0.82f;
        hand.cx -= pen.size * 0.08f;
        hand.cy += pen.size * 0.08f;
        drawUse(hand);
        pen.circle(0.32f, -0.3f, 0.12f);
    }

    // Mine with spikes.
    void drawTraps(const Pen& pen)
    {
        pen.arc(0.0f, 0.24f, 0.34f, 180.0f, 360.0f);
        pen.line(-0.46f, 0.24f, 0.46f, 0.24f);
        pen.rect(-0.08f, -0.24f, 0.16f, 0.14f, 0.02f);
        pen.line(-0.3f, -0.34f, -0.18f, -0.2f);
        pen.line(0.3f, -0.34f, 0.18f, -0.2f);
        pen.line(0.0f, -0.46f, 0.0f, -0.34f);
    }

    // Medical bag.
    void drawDoctor(const Pen& pen)
    {
        pen.arc(0.0f, -0.2f, 0.16f, 180.0f, 360.0f);
        pen.rect(-0.42f, -0.2f, 0.84f, 0.62f, 0.08f);
        pen.line(-0.14f, 0.11f, 0.14f, 0.11f);
        pen.line(0.0f, -0.03f, 0.0f, 0.25f);
    }

    // Computer screen.
    void drawScience(const Pen& pen)
    {
        pen.rect(-0.44f, -0.38f, 0.88f, 0.58f, 0.06f);
        pen.poly({ { -0.28f, -0.04f }, { -0.14f, -0.04f }, { -0.06f, -0.2f }, { 0.06f, 0.08f }, { 0.14f, -0.04f }, { 0.28f, -0.04f } });
        pen.line(0.0f, 0.2f, 0.0f, 0.36f);
        pen.line(-0.2f, 0.42f, 0.2f, 0.42f);
    }

    // Wrench.
    void drawRepair(const Pen& pen)
    {
        pen.line(-0.38f, 0.38f, 0.12f, -0.12f);
        pen.arc(0.24f, -0.24f, 0.17f, 225.0f, 495.0f);
    }

    // Crosshair in a frame corner marks: choose a target.
    void drawTarget(const Pen& pen)
    {
        pen.circle(0.0f, 0.0f, 0.2f);
        pen.dot(0.0f, 0.0f, 0.05f);
        pen.poly({ { -0.46f, -0.24f }, { -0.46f, -0.46f }, { -0.24f, -0.46f } });
        pen.poly({ { 0.24f, -0.46f }, { 0.46f, -0.46f }, { 0.46f, -0.24f } });
        pen.poly({ { 0.46f, 0.24f }, { 0.46f, 0.46f }, { 0.24f, 0.46f } });
        pen.poly({ { -0.24f, 0.46f }, { -0.46f, 0.46f }, { -0.46f, 0.24f } });
    }

} // namespace

// Draws the icon's lines and shapes.
static void drawIconShapes(MuiIcon icon, float x, float y, float size, float thickness, MuiColor color)
{
    Pen pen(x, y, size, thickness, color);

    switch (icon) {
    case MuiIcon::Menu:
    case MuiIcon::FilterAll:
        drawMenu(pen);
        break;
    case MuiIcon::QuickSave:
        drawFloppy(pen, true);
        break;
    case MuiIcon::QuickLoad:
        drawFloppy(pen, false);
        break;
    case MuiIcon::Inventory:
        drawInventory(pen);
        break;
    case MuiIcon::Character:
        drawCharacter(pen);
        break;
    case MuiIcon::Pipboy:
        drawPipboy(pen);
        break;
    case MuiIcon::Map:
        drawMap(pen);
        break;
    case MuiIcon::Skills:
        drawSkills(pen);
        break;
    case MuiIcon::Highlight:
        drawHighlight(pen);
        break;
    case MuiIcon::Sneak:
        drawSneak(pen);
        break;
    case MuiIcon::Log:
        drawLog(pen);
        break;
    case MuiIcon::EndTurn:
        drawEndTurn(pen);
        break;
    case MuiIcon::EndCombat:
        drawEndCombat(pen);
        break;
    case MuiIcon::SwapHands:
        drawSwapHands(pen);
        break;
    case MuiIcon::ActionPoints:
        drawActionPoints(pen);
        break;
    case MuiIcon::HitPoints:
        drawHitPoints(pen);
        break;
    case MuiIcon::Aim:
        drawAim(pen);
        break;
    case MuiIcon::Reload:
        drawReload(pen);
        break;
    case MuiIcon::Fist:
        drawFist(pen);
        break;
    case MuiIcon::Weight:
        drawWeight(pen);
        break;
    case MuiIcon::Back:
        drawBack(pen);
        break;
    case MuiIcon::Turn:
        drawTurn(pen);
        break;
    case MuiIcon::PartyOrders:
        drawPartyOrders(pen);
        break;
    case MuiIcon::TacticalView:
        drawTacticalView(pen);
        break;
    case MuiIcon::Holster:
        drawHolster(pen);
        break;
    case MuiIcon::Regroup:
        drawCrossArrows(pen, true);
        break;
    case MuiIcon::Spread:
        drawCrossArrows(pen, false);
        break;
    case MuiIcon::Burst:
        drawBurst(pen);
        break;
    case MuiIcon::AutoLoot:
        drawAutoLoot(pen);
        break;
    case MuiIcon::Lockpick:
        drawLockpick(pen);
        break;
    case MuiIcon::Steal:
        drawSteal(pen);
        break;
    case MuiIcon::Traps:
        drawTraps(pen);
        break;
    case MuiIcon::Doctor:
        drawDoctor(pen);
        break;
    case MuiIcon::Science:
        drawScience(pen);
        break;
    case MuiIcon::Repair:
        drawRepair(pen);
        break;
    case MuiIcon::Target:
        drawTarget(pen);
        break;
    case MuiIcon::Talk:
        drawTalk(pen);
        break;
    case MuiIcon::Barter:
        drawBarter(pen);
        break;
    case MuiIcon::Review:
        drawReview(pen);
        break;
    case MuiIcon::FilterWeapons:
        drawWeapon(pen, 0.0f, 0.0f, 1.0f);
        break;
    case MuiIcon::FilterWeaponsAndAmmo:
        drawWeapon(pen, -0.2f, 0.0f, 0.7f);
        drawAmmo(pen, 0.38f, 0.0f, 0.6f);
        break;
    case MuiIcon::FilterArmor:
        drawArmor(pen);
        break;
    case MuiIcon::FilterDrugs:
        drawDrugs(pen);
        break;
    case MuiIcon::FilterAmmo:
        drawAmmo(pen, 0.0f, 0.0f, 1.0f);
        break;
    case MuiIcon::FilterMisc:
        drawMisc(pen);
        break;
    case MuiIcon::Cancel:
        drawCancel(pen);
        break;
    case MuiIcon::Drop:
        drawDrop(pen);
        break;
    case MuiIcon::Look:
        drawEye(pen);
        break;
    case MuiIcon::Rotate:
        drawRotate(pen);
        break;
    case MuiIcon::Use:
        drawUse(pen);
        break;
    case MuiIcon::Unload:
        drawUnload(pen);
        break;
    case MuiIcon::Push:
        drawPush(pen);
        break;
    case MuiIcon::Plus:
        drawPlus(pen);
        break;
    case MuiIcon::Minus:
        drawMinus(pen);
        break;
    case MuiIcon::Edit:
        drawEdit(pen);
        break;
    case MuiIcon::Play:
        drawPlay(pen);
        break;
    case MuiIcon::Pause:
        drawPause(pen);
        break;
    case MuiIcon::Stop:
        drawStop(pen);
        break;
    case MuiIcon::Expand:
        drawCorners(pen, true);
        break;
    case MuiIcon::Collapse:
        drawCorners(pen, false);
        break;
    // Textures only (`rasterizeBrandMark`).
    case MuiIcon::Telegram:
    case MuiIcon::YouTube:
    case MuiIcon::GitHub:
    case MuiIcon::Count:
        break;
    }
}

// MARK: Brand marks

namespace {

    // The port author's pages (the main menu): Simple Icons' marks (CC0,
    // simpleicons.org) on their 24 unit square, filled, with their holes as
    // paths of their own. Drawn from their outlines, not with the pen, as
    // the next versions draw every icon.
    struct BrandMark {
        const char* fill;
        const char* hole;
    };

    const BrandMark kBrandMarks[] = {
        // Telegram
        { "M12 0a12 12 0 0 1 0 24 12 12 0 0 1 0-24z",
            "M16.906 7.224c.1-.002.321.023.465.14a.506.506 0 0 1 .171.325c.016.093.036.306.02.472-.18 1.898-.962 6.502-1.36 8.627-.168.9-.499 1.201-.82 1.23-.696.065-1.225-.46-1.9-.902-1.056-.693-1.653-1.124-2.678-1.8-1.185-.78-.417-1.21.258-1.91.177-.184 3.247-2.977 3.307-3.23.007-.032.014-.15-.056-.212s-.174-.041-.249-.024c-.106.024-1.793 1.14-5.061 3.345-.48.33-.913.49-1.302.48-.428-.008-1.252-.241-1.865-.44-.752-.245-1.349-.374-1.297-.789.027-.216.325-.437.893-.663 3.498-1.524 5.83-2.529 6.998-3.014 3.332-1.386 4.025-1.627 4.476-1.635z" },
        // YouTube
        { "M23.498 6.186a3.016 3.016 0 0 0-2.122-2.136C19.505 3.545 12 3.545 12 3.545s-7.505 0-9.377.505A3.017 3.017 0 0 0 .502 6.186C0 8.07 0 12 0 12s0 3.93.502 5.814a3.016 3.016 0 0 0 2.122 2.136c1.871.505 9.376.505 9.376.505s7.505 0 9.377-.505a3.015 3.015 0 0 0 2.122-2.136C24 15.93 24 12 24 12s0-3.93-.502-5.814z",
            "M9.545 15.568V8.432L15.818 12z" },
        // GitHub
        { "M12 .297c-6.63 0-12 5.373-12 12 0 5.303 3.438 9.8 8.205 11.385.6.113.82-.258.82-.577 0-.285-.01-1.04-.015-2.04-3.338.724-4.042-1.61-4.042-1.61C4.422 18.07 3.633 17.7 3.633 17.7c-1.087-.744.084-.729.084-.729 1.205.084 1.838 1.236 1.838 1.236 1.07 1.835 2.809 1.305 3.495.998.108-.776.417-1.305.76-1.605-2.665-.3-5.466-1.332-5.466-5.93 0-1.31.465-2.38 1.235-3.22-.135-.303-.54-1.523.105-3.176 0 0 1.005-.322 3.3 1.23.96-.267 1.98-.399 3-.405 1.02.006 2.04.138 3 .405 2.28-1.552 3.285-1.23 3.285-1.23.645 1.653.24 2.873.12 3.176.765.84 1.23 1.91 1.23 3.22 0 4.61-2.805 5.625-5.475 5.92.42.36.81 1.096.81 2.22 0 1.606-.015 2.896-.015 3.286 0 .315.21.69.825.57C20.565 22.092 24 17.592 24 12.297c0-6.627-5.373-12-12-12",
            nullptr },
    };

    const BrandMark* brandMark(MuiIcon icon)
    {
        switch (icon) {
        case MuiIcon::Telegram:
            return &kBrandMarks[0];
        case MuiIcon::YouTube:
            return &kBrandMarks[1];
        case MuiIcon::GitHub:
            return &kBrandMarks[2];
        default:
            return nullptr;
        }
    }

    bool readNumber(const char** cursor, float* value)
    {
        const char* p = *cursor;
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') {
            p++;
        }
        char* end;
        float result = strtof(p, &end);
        if (end == p) {
            return false;
        }
        *value = result;
        *cursor = end;
        return true;
    }

    // A flag of an arc: a single 0 or 1, maybe with no separator after it.
    bool readFlag(const char** cursor, bool* value)
    {
        const char* p = *cursor;
        while (*p == ' ' || *p == ',') {
            p++;
        }
        if (*p != '0' && *p != '1') {
            return false;
        }
        *value = *p == '1';
        *cursor = p + 1;
        return true;
    }

    void addCubic(std::vector<SDL_FPoint>& points, SDL_FPoint from, SDL_FPoint c1, SDL_FPoint c2, SDL_FPoint to)
    {
        constexpr int kSegments = 16;
        for (int step = 1; step <= kSegments; step++) {
            float t = static_cast<float>(step) / kSegments;
            float u = 1.0f - t;
            float a = u * u * u;
            float b = 3.0f * u * u * t;
            float c = 3.0f * u * t * t;
            float d = t * t * t;
            points.push_back({ a * from.x + b * c1.x + c * c2.x + d * to.x, a * from.y + b * c1.y + c * c2.y + d * to.y });
        }
    }

    void addQuadratic(std::vector<SDL_FPoint>& points, SDL_FPoint from, SDL_FPoint control, SDL_FPoint to)
    {
        constexpr int kSegments = 12;
        for (int step = 1; step <= kSegments; step++) {
            float t = static_cast<float>(step) / kSegments;
            float u = 1.0f - t;
            points.push_back({ u * u * from.x + 2.0f * u * t * control.x + t * t * to.x, u * u * from.y + 2.0f * u * t * control.y + t * t * to.y });
        }
    }

    // SVG's endpoint arc through its center form (SVG 1.1 F.6.5).
    void addArc(std::vector<SDL_FPoint>& points, SDL_FPoint from, float rx, float ry, float rotation, bool large, bool sweep, SDL_FPoint to)
    {
        rx = std::fabs(rx);
        ry = std::fabs(ry);
        if (rx < 1e-4f || ry < 1e-4f) {
            points.push_back(to);
            return;
        }

        float phi = rotation * kPi / 180.0f;
        float cosPhi = std::cos(phi);
        float sinPhi = std::sin(phi);
        float dx = (from.x - to.x) / 2.0f;
        float dy = (from.y - to.y) / 2.0f;
        float x1 = cosPhi * dx + sinPhi * dy;
        float y1 = -sinPhi * dx + cosPhi * dy;

        float lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
        if (lambda > 1.0f) {
            rx *= std::sqrt(lambda);
            ry *= std::sqrt(lambda);
        }

        float numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
        float denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
        float coefficient = denominator > 0.0f ? std::sqrt(std::max(numerator / denominator, 0.0f)) : 0.0f;
        if (large == sweep) {
            coefficient = -coefficient;
        }
        float cx1 = coefficient * rx * y1 / ry;
        float cy1 = -coefficient * ry * x1 / rx;
        float cx = cosPhi * cx1 - sinPhi * cy1 + (from.x + to.x) / 2.0f;
        float cy = sinPhi * cx1 + cosPhi * cy1 + (from.y + to.y) / 2.0f;

        auto angle = [](float ux, float uy, float vx, float vy) {
            return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
        };
        float start = angle(1.0f, 0.0f, (x1 - cx1) / rx, (y1 - cy1) / ry);
        float delta = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
        if (!sweep && delta > 0.0f) {
            delta -= 2.0f * kPi;
        } else if (sweep && delta < 0.0f) {
            delta += 2.0f * kPi;
        }

        int segments = std::max(static_cast<int>(std::ceil(std::fabs(delta) / (10.0f * kPi / 180.0f))), 2);
        for (int step = 1; step <= segments; step++) {
            float t = start + delta * step / segments;
            float px = rx * std::cos(t);
            float py = ry * std::sin(t);
            points.push_back({ cosPhi * px - sinPhi * py + cx, sinPhi * px + cosPhi * py + cy });
        }
    }

    float signedArea(const std::vector<SDL_FPoint>& points)
    {
        float area = 0.0f;
        for (size_t index = 0; index < points.size(); index++) {
            const SDL_FPoint& a = points[index];
            const SDL_FPoint& b = points[(index + 1) % points.size()];
            area += a.x * b.y - b.x * a.y;
        }
        return area;
    }

    // A finished subpath as a closed contour.
    void addContour(std::vector<std::vector<SDL_FPoint>>* contours, std::vector<SDL_FPoint>& points)
    {
        if (points.size() >= 3) {
            contours->push_back(points);
        }
        points.clear();
    }

    void parsePath(const char* data, std::vector<std::vector<SDL_FPoint>>* contours)
    {
        std::vector<SDL_FPoint> points;
        SDL_FPoint current = { 0.0f, 0.0f };
        SDL_FPoint start = { 0.0f, 0.0f };
        // The last curve's second control point, for S and T.
        SDL_FPoint control = { 0.0f, 0.0f };
        char previous = 0;
        char command = 0;
        const char* p = data;
        while (true) {
            while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') {
                p++;
            }
            if (*p == '\0') {
                break;
            }
            if (isalpha(static_cast<unsigned char>(*p))) {
                command = *p++;
            } else if (command == 0) {
                break;
            }

            bool relative = islower(static_cast<unsigned char>(command)) != 0;
            char upper = static_cast<char>(toupper(static_cast<unsigned char>(command)));
            float ox = relative ? current.x : 0.0f;
            float oy = relative ? current.y : 0.0f;
            if (upper != 'M' && upper != 'Z' && points.empty()) {
                points.push_back(current);
            }

            bool ok = true;
            switch (upper) {
            case 'M': {
                float x;
                float y;
                ok = readNumber(&p, &x) && readNumber(&p, &y);
                if (ok) {
                    addContour(contours, points);
                    current = { ox + x, oy + y };
                    start = current;
                    points.push_back(current);
                    // Pairs after it are lines.
                    command = relative ? 'l' : 'L';
                }
                break;
            }
            case 'L': {
                float x;
                float y;
                ok = readNumber(&p, &x) && readNumber(&p, &y);
                if (ok) {
                    current = { ox + x, oy + y };
                    points.push_back(current);
                }
                break;
            }
            case 'H': {
                float x;
                ok = readNumber(&p, &x);
                if (ok) {
                    current.x = ox + x;
                    points.push_back(current);
                }
                break;
            }
            case 'V': {
                float y;
                ok = readNumber(&p, &y);
                if (ok) {
                    current.y = oy + y;
                    points.push_back(current);
                }
                break;
            }
            case 'C':
            case 'S': {
                float v[6];
                SDL_FPoint c1;
                if (upper == 'C') {
                    for (int index = 0; index < 6 && ok; index++) {
                        ok = readNumber(&p, &v[index]);
                    }
                    c1 = { ox + v[0], oy + v[1] };
                } else {
                    for (int index = 2; index < 6 && ok; index++) {
                        ok = readNumber(&p, &v[index]);
                    }
                    c1 = previous == 'C' || previous == 'S' ? SDL_FPoint { 2.0f * current.x - control.x, 2.0f * current.y - control.y } : current;
                }
                if (ok) {
                    SDL_FPoint c2 = { ox + v[2], oy + v[3] };
                    SDL_FPoint to = { ox + v[4], oy + v[5] };
                    addCubic(points, current, c1, c2, to);
                    control = c2;
                    current = to;
                }
                break;
            }
            case 'Q':
            case 'T': {
                float v[4];
                SDL_FPoint c;
                if (upper == 'Q') {
                    for (int index = 0; index < 4 && ok; index++) {
                        ok = readNumber(&p, &v[index]);
                    }
                    c = { ox + v[0], oy + v[1] };
                } else {
                    for (int index = 2; index < 4 && ok; index++) {
                        ok = readNumber(&p, &v[index]);
                    }
                    c = previous == 'Q' || previous == 'T' ? SDL_FPoint { 2.0f * current.x - control.x, 2.0f * current.y - control.y } : current;
                }
                if (ok) {
                    SDL_FPoint to = { ox + v[2], oy + v[3] };
                    addQuadratic(points, current, c, to);
                    control = c;
                    current = to;
                }
                break;
            }
            case 'A': {
                float rx;
                float ry;
                float rotation;
                bool large;
                bool sweep;
                float x;
                float y;
                ok = readNumber(&p, &rx) && readNumber(&p, &ry) && readNumber(&p, &rotation)
                    && readFlag(&p, &large) && readFlag(&p, &sweep) && readNumber(&p, &x) && readNumber(&p, &y);
                if (ok) {
                    SDL_FPoint to = { ox + x, oy + y };
                    addArc(points, current, rx, ry, rotation, large, sweep, to);
                    current = to;
                }
                break;
            }
            case 'Z':
                addContour(contours, points);
                current = start;
                break;
            default:
                ok = false;
                break;
            }

            if (!ok) {
                break;
            }
            previous = upper;
        }
        addContour(contours, points);
    }

    // [mark]'s coverage in an [extent] square: the 24 unit square scaled
    // to [scale] px a unit, centered; the mark takes 3..21 of it.
    std::vector<unsigned char> rasterizeBrandMark(const BrandMark& mark, int extent, float scale)
    {
        float origin = extent / 2.0f - 12.0f * scale;
        std::vector<std::vector<SDL_FPoint>> contours;
        auto add = [&](const char* data, bool hole) {
            std::vector<std::vector<SDL_FPoint>> parsed;
            parsePath(data, &parsed);
            for (std::vector<SDL_FPoint>& points : parsed) {
                for (SDL_FPoint& point : points) {
                    point = { origin + (3.0f + point.x * 0.75f) * scale, origin + (3.0f + point.y * 0.75f) * scale };
                }
                // Fills all one way round, holes the other way.
                if ((signedArea(points) < 0.0f) != hole) {
                    std::reverse(points.begin(), points.end());
                }
                contours.push_back(std::move(points));
            }
        };
        add(mark.fill, false);
        if (mark.hole != nullptr) {
            add(mark.hole, true);
        }
        return muiRasterizeContours(contours, extent, extent);
    }

} // namespace

namespace {

    // An icon drawn in white into a texture (alpha premultiplied), tinted
    // when drawn. Icons are made of many lines and round joins; drawing them
    // again every frame was most of the HUD's time.
    struct IconTexture {
        SDL_Texture* texture = nullptr;
        int extent = 0;
    };

    using IconKey = std::tuple<int, int, int>;
    std::map<IconKey, IconTexture> gIconTextures;

    // Sizes change during animations; the cache is dropped when it grows
    // past this.
    constexpr size_t kMaxIconTextures = 256;

    const IconTexture* iconTexture(SDL_Renderer* renderer, MuiIcon icon, float size, float thickness)
    {
        IconKey key(static_cast<int>(icon), static_cast<int>(lroundf(size * 4.0f)), static_cast<int>(lroundf(thickness * 4.0f)));
        auto it = gIconTextures.find(key);
        if (it != gIconTextures.end()) {
            return it->second.texture != nullptr ? &(it->second) : nullptr;
        }

        if (gIconTextures.size() >= kMaxIconTextures) {
            muiIconsReset();
        }

        IconTexture& entry = gIconTextures[key];

        if (const BrandMark* mark = brandMark(icon)) {
            // The pen's icons take 21.5 of the 24 units across [size].
            float scale = size / 21.5f;
            entry.extent = static_cast<int>(std::ceil(24.0f * scale)) + 4;
            std::vector<unsigned char> coverage = rasterizeBrandMark(*mark, entry.extent, scale);
            std::vector<Uint32> pixels(coverage.size());
            for (size_t index = 0; index < coverage.size(); index++) {
                Uint32 value = coverage[index];
                pixels[index] = (value << 24) | (value << 16) | (value << 8) | value;
            }
            entry.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, entry.extent, entry.extent);
            if (entry.texture == nullptr) {
                return nullptr;
            }
            SDL_UpdateTexture(entry.texture, nullptr, pixels.data(), entry.extent * static_cast<int>(sizeof(Uint32)));
            SDL_SetTextureBlendMode(entry.texture, muiPremultipliedBlendMode());
            SDL_SetTextureScaleMode(entry.texture, SDL_ScaleModeNearest);
            return &entry;
        }

        // Room for strokes and round joins sticking out of the icon square.
        entry.extent = static_cast<int>(std::ceil(size + thickness * 2.0f)) + 4;
        entry.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, entry.extent, entry.extent);
        if (entry.texture == nullptr) {
            return nullptr;
        }

        SDL_Texture* previousTarget = SDL_GetRenderTarget(renderer);
        if (SDL_SetRenderTarget(renderer, entry.texture) != 0) {
            SDL_DestroyTexture(entry.texture);
            entry.texture = nullptr;
            return nullptr;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        // White with alpha blended over transparent black is premultiplied.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        float center = entry.extent / 2.0f;
        drawIconShapes(icon, center, center, size, thickness, muiRgb(0xFFFFFF));
        SDL_SetRenderTarget(renderer, previousTarget);

        SDL_SetTextureBlendMode(entry.texture, muiPremultipliedBlendMode());
        SDL_SetTextureScaleMode(entry.texture, SDL_ScaleModeNearest);
        return &entry;
    }

} // namespace

void muiDrawIcon(MuiIcon icon, float x, float y, float size, float thickness, MuiColor color)
{
    SDL_Renderer* renderer = muiDrawGetRenderer();
    const IconTexture* entry = renderer != nullptr ? iconTexture(renderer, icon, size, thickness) : nullptr;
    if (entry == nullptr) {
        drawIconShapes(icon, x, y, size, thickness, color);
        return;
    }

    // Premultiplied: the tint's alpha scales the color too.
    SDL_SetTextureColorMod(entry->texture, color.r * color.a / 255, color.g * color.a / 255, color.b * color.a / 255);
    SDL_SetTextureAlphaMod(entry->texture, color.a);

    // Whole pixels keep the icon as sharp as drawn.
    SDL_Rect dest = {
        static_cast<int>(lroundf(x - entry->extent / 2.0f)),
        static_cast<int>(lroundf(y - entry->extent / 2.0f)),
        entry->extent,
        entry->extent,
    };
    SDL_RenderCopy(renderer, entry->texture, nullptr, &dest);
}

void muiIconsReset()
{
    for (auto& entry : gIconTextures) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    gIconTextures.clear();
}

} // namespace fallout
