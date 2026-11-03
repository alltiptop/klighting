// SPDX-License-Identifier: GPL-3.0-or-later
#include "color.h"
#include "device_driver.h"
#include <algorithm>
#include <cmath>

namespace Ambient
{
int deviceBrightnessSteps(const DeviceInstance& device)
{
    // RGB output dims in software; the hardware brightness range is independent.
    if (device.description.capabilities.testFlag(DeviceCapability::Color))
        return 0;
    const int steps = device.options.value("BrightnessSteps").toInt();
    return steps > 0 && steps <= 65535 ? steps : 0;
}
QColor deviceColor(const QColor& color, const DeviceInstance& device, const QString& channel)
{
    if (!color.isValid())
        return color;
    const auto quantize = [](double value, int steps) {
        return steps > 0 && steps <= 65535 ? qRound(value * steps) / double(steps) : value;
    };
    if (!device.description.capabilities.testFlag(DeviceCapability::Color)) {
        const double value = channel == "red" ? color.redF()
            : channel == "green"              ? color.greenF()
            : channel == "blue"               ? color.blueF()
                                              : (color.redF() + color.greenF() + color.blueF()) / 3;
        const auto level = quantize(value, deviceBrightnessSteps(device));
        return QColor::fromRgbF(level, level, level, color.alphaF());
    }
    const auto levels = device.options.value("ColorLevels").toMap();
    if (levels.isEmpty())
        return color;
    const auto component = [&](const QString& name, double value) {
        const int steps = levels.value(name).toInt();
        return steps > 0 ? quantize(value, steps) : 0.0;
    };
    return QColor::fromRgbF(component("red", color.redF()), component("green", color.greenF()),
        component("blue", color.blueF()), color.alphaF());
}
static double toLinear(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
static double toSrgb(double v) { return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055; }
static double linearComponent(unsigned char value)
{
    static const auto table = [] {
        std::array<double, 256> values;
        for (int i = 0; i < 256; ++i)
            values[i] = toLinear(i / 255.0);
        return values;
    }();
    return table[value];
}
static QColor adjustedColor(std::array<double, 3> mean, double brightness, double saturation)
{
    const double luminance = 0.2126 * mean[0] + 0.7152 * mean[1] + 0.0722 * mean[2];
    brightness = std::isfinite(brightness) ? std::clamp(brightness, 0.0, 1.0) : 1.0;
    saturation = std::isfinite(saturation) ? std::clamp(saturation, 0.0, 2.0) : 1.0;
    for (double& v : mean)
        v = toSrgb(std::clamp((luminance + saturation * (v - luminance)) * brightness, 0.0, 1.0));
    return QColor::fromRgbF(mean[0], mean[1], mean[2]);
}
QColor averageColor(QByteArrayView rgba, double brightness, double saturation)
{
    if (rgba.empty() || rgba.size() % 4 != 0)
        return { };
    std::array<double, 3> mean { };
    for (qsizetype i = 0; i < rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c)
            mean[c] += linearComponent(static_cast<unsigned char>(rgba[i + c]));
    }
    for (double& v : mean)
        v /= rgba.size() / 4;
    return adjustedColor(mean, brightness, saturation);
}
QColor DominantColorPicker::select(QByteArrayView rgba, double brightness, double saturation)
{
    if (rgba.empty() || rgba.size() % 4 != 0)
        return { };
    constexpr int Hues = 24, Saturations = 4;
    struct Bin {
        std::array<double, 3> sum { };
        qsizetype count = 0;
        void add(const Bin& other)
        {
            count += other.count;
            for (int c = 0; c < 3; ++c)
                sum[c] += other.sum[c];
        }
    };
    std::array<Bin, Hues * Saturations> bins { }; // 3 KiB, no heap allocation.
    std::array<double, 3> total { };
    const qsizetype pixels = rgba.size() / 4;
    for (qsizetype i = 0; i < rgba.size(); i += 4) {
        const int r = static_cast<unsigned char>(rgba[i]);
        const int g = static_cast<unsigned char>(rgba[i + 1]);
        const int b = static_cast<unsigned char>(rgba[i + 2]);
        const std::array<double, 3> rgb { linearComponent(r), linearComponent(g), linearComponent(b) };
        for (int c = 0; c < 3; ++c)
            total[c] += rgb[c];
        const int value = std::max({ r, g, b });
        const int chroma = value - std::min({ r, g, b });
        // Near-black noise and almost-neutral UI elements do not vote for a hue.
        if (value < 16 || chroma * 5 < value)
            continue;
        const int hue = ((QColor(r, g, b).hsvHue() + 7) / 15) % Hues;
        const int sat = std::min(Saturations - 1, chroma * Saturations / value);
        bins[hue * Saturations + sat].add({ rgb, 1 });
    }
    const auto neighborhood = [&](int index) {
        Bin result;
        const int hue = index / Saturations, sat = index % Saturations;
        // Merge adjacent bins, including red across the 359/0 degree boundary.
        for (int h = -1; h <= 1; ++h)
            for (int s = std::max(0, sat - 1); s <= std::min(Saturations - 1, sat + 1); ++s)
                result.add(bins[((hue + h + Hues) % Hues) * Saturations + s]);
        return result;
    };
    int selected = -1;
    Bin peak;
    for (int i = 0; i < Hues * Saturations; ++i) {
        const auto candidate = neighborhood(i);
        if (candidate.count > peak.count) {
            selected = i;
            peak = candidate;
        }
    }
    // A tiny icon must not color the whole keyboard. Require 5% of the sample.
    const qsizetype minimum = (pixels + 19) / 20;
    if (peak.count < minimum) {
        m_peak = -1;
        const double gray = (0.2126 * total[0] + 0.7152 * total[1] + 0.0722 * total[2]) / pixels;
        return adjustedColor({ gray, gray, gray }, brightness, saturation);
    }
    if (m_peak >= 0 && selected != m_peak) {
        const auto previous = neighborhood(m_peak);
        // Keep a near tie stable. A disappearing old hue never delays a scene cut.
        if (previous.count >= minimum && peak.count * 5 <= previous.count * 6) {
            selected = m_peak;
            peak = previous;
        }
    }
    m_peak = selected;
    for (double& v : peak.sum)
        v /= peak.count;
    return adjustedColor(peak.sum, brightness, saturation);
}
QColor mapBrightness(const QColor& color, int minimumPercent, int maximumPercent)
{
    if (!color.isValid())
        return { };
    const int maximum = (std::clamp(maximumPercent, 0, 100) * 255 + 50) / 100;
    const int minimum = std::min((std::clamp(minimumPercent, 0, 100) * 255 + 50) / 100, maximum);
    const int value = std::max({ color.red(), color.green(), color.blue() });
    if (value == 0)
        return QColor(minimum, minimum, minimum);
    // Map screen brightness into the full requested interval. The upper limit
    // therefore dims midtones too, rather than only clipping the brightest colors.
    const int mapped = minimum + qRound(value * (maximum - minimum) / 255.0);
    const double scale = double(mapped) / value;
    return QColor(qRound(color.red() * scale), qRound(color.green() * scale), qRound(color.blue() * scale));
}
QColor constrainBrightness(const QColor& color, int minimumPercent, int maximumPercent)
{
    if (!color.isValid())
        return { };
    const int maximum = (std::clamp(maximumPercent, 0, 100) * 255 + 50) / 100;
    const int minimum = std::min((std::clamp(minimumPercent, 0, 100) * 255 + 50) / 100, maximum);
    const int value = std::max({ color.red(), color.green(), color.blue() });
    if (value == 0)
        return QColor(minimum, minimum, minimum); // Black has no hue: use a neutral night light.
    const int limited = std::clamp(value, minimum, maximum);
    if (limited == value)
        return color;
    // Scale all channels equally to retain hue and saturation, within byte rounding.
    const double scale = double(limited) / value;
    return QColor(qRound(color.red() * scale), qRound(color.green() * scale), qRound(color.blue() * scale));
}
QColor adjustColorTemperature(const QColor& color, int temperature)
{
    if (!color.isValid() || temperature == 0)
        return color;
    // Relative LED white balance, not a calibrated Kelvin scale. Negative is warm.
    const double amount = std::clamp(temperature, -100, 100) / 100.0;
    const int value = std::max({ color.red(), color.green(), color.blue() });
    if (value == 0)
        return color;
    const std::array<double, 3> adjusted { color.red() * (1.0 - 0.85 * std::max(0.0, amount)),
        color.green() * (1.0 - 0.25 * std::abs(amount)),
        color.blue() * (1.0 + 0.85 * std::min(0.0, amount)) };
    // Keep the strongest channel unchanged so calibration preserves the floor
    // and ceiling. This also keeps a dim black-screen night light visible.
    const double scale = value / *std::max_element(adjusted.begin(), adjusted.end());
    return QColor(qRound(adjusted[0] * scale), qRound(adjusted[1] * scale), qRound(adjusted[2] * scale));
}
QRect sampleRegion(const QRect& screen, bool bottomOnly, int bottomPercent)
{
    if (screen.isEmpty() || !bottomOnly)
        return screen;
    const int height = std::max(1, screen.height() * std::clamp(bottomPercent, 1, 100) / 100);
    return { screen.x(), screen.y() + screen.height() - height, screen.width(), height };
}
void ColorSmoother::reset()
{
    m_target.reset();
    m_initialized = false;
}
void ColorSmoother::setTarget(const QColor& color)
{
    if (color.isValid())
        m_target = color;
}
std::optional<QColor> ColorSmoother::step(double elapsedMs, double smoothingMs)
{
    if (!m_target)
        return std::nullopt;
    const std::array<double, 3> target { m_target->redF(), m_target->greenF(), m_target->blueF() };
    const double alpha = !m_initialized || smoothingMs <= 0
        ? 1.0
        : -std::expm1(-std::clamp(elapsedMs, 0.0, 1000.0) / smoothingMs);
    for (int c = 0; c < 3; ++c)
        m_current[c] += alpha * (target[c] - m_current[c]);
    m_initialized = true;
    return QColor(qRound(m_current[0] * 255), qRound(m_current[1] * 255), qRound(m_current[2] * 255));
}
}
