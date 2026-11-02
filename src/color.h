// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArrayView>
#include <QColor>
#include <QRect>
#include <array>
#include <optional>

namespace Ambient
{
QColor averageColor(QByteArrayView rgba, double brightness = 1.0, double saturation = 1.0);
QColor mapBrightness(const QColor& color, int minimumPercent, int maximumPercent);
QColor constrainBrightness(const QColor& color, int minimumPercent, int maximumPercent);
QColor adjustColorTemperature(const QColor& color, int temperature);
QRect sampleRegion(const QRect& screen, bool bottomOnly, int bottomPercent);

class DominantColorPicker
{
public:
    QColor select(QByteArrayView rgba, double brightness = 1.0, double saturation = 1.0);
    void reset() { m_peak = -1; }

private:
    int m_peak = -1;
};

class ColorSmoother
{
public:
    void reset();
    void setTarget(const QColor& color);
    std::optional<QColor> step(double elapsedMs, double smoothingMs);

private:
    std::optional<QColor> m_target;
    std::array<double, 3> m_current { };
    bool m_initialized = false;
};
}
