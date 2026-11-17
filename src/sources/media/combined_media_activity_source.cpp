// SPDX-License-Identifier: GPL-3.0-or-later
#include "combined_media_activity_source.h"

namespace Ambient
{
CombinedMediaActivitySource::CombinedMediaActivitySource(
    std::unique_ptr<MediaActivitySource> pipewire, std::unique_ptr<MediaActivitySource> v4l2)
    : m_pipewire(std::move(pipewire))
    , m_v4l2(std::move(v4l2))
{
    if (m_pipewire)
        connect(m_pipewire.get(), &MediaActivitySource::changed, this, [this](const QVariantMap& values) {
            if (m_pipewireEnabled) {
                m_pipewireValues = values;
                publish();
            }
        });
    if (m_v4l2)
        connect(m_v4l2.get(), &MediaActivitySource::changed, this, [this](const QVariantMap& values) {
            if (m_v4l2Enabled) {
                m_v4l2Values = values;
                publish();
            }
        });
}
void CombinedMediaActivitySource::setEnabled(bool enabled)
{
    setRequiredKinds(
        enabled ? QSet<QString> { "microphone-active", "camera-active", "screen-capture" } : QSet<QString>());
}
void CombinedMediaActivitySource::setRequiredKinds(const QSet<QString>& kinds)
{
    if (m_kinds == kinds)
        return;
    m_kinds = kinds;
    m_configuring = true;
    const bool pipewire = m_pipewire && !kinds.isEmpty();
    const bool v4l2 = m_v4l2 && kinds.contains("camera-active");
    if (pipewire != m_pipewireEnabled) {
        m_pipewireEnabled = pipewire;
        m_pipewireValues.clear();
        m_pipewire->setEnabled(pipewire);
    }
    if (v4l2 != m_v4l2Enabled) {
        m_v4l2Enabled = v4l2;
        m_v4l2Values.clear();
        m_v4l2->setEnabled(v4l2);
    }
    m_configuring = false;
    publish();
}
void CombinedMediaActivitySource::publish()
{
    if (m_configuring)
        return;
    QVariantMap values;
    for (const auto& kind : m_kinds) {
        bool supported = false, known = true, active = false;
        const auto include = [&](const QVariant& value) {
            supported = true;
            known &= value.isValid();
            active |= value.isValid() && value.toBool();
        };
        if (m_pipewireEnabled)
            include(m_pipewireValues.value(kind));
        if (m_v4l2Enabled && kind == "camera-active")
            include(m_v4l2Values.value(kind));
        // Presence from either source is enough; absence needs every enabled source.
        if (supported && (active || known))
            values.insert(kind, active);
    }
    if (values != m_values) {
        m_values = values;
        Q_EMIT changed(values);
    }
}
}
