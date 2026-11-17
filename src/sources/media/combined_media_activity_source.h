// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sources/media_activity_source.h"

namespace Ambient
{
class CombinedMediaActivitySource final : public MediaActivitySource
{
public:
    CombinedMediaActivitySource(
        std::unique_ptr<MediaActivitySource> pipewire, std::unique_ptr<MediaActivitySource> v4l2);
    void setEnabled(bool enabled) override;
    void setRequiredKinds(const QSet<QString>& kinds) override;

private:
    void publish();
    std::unique_ptr<MediaActivitySource> m_pipewire, m_v4l2;
    QVariantMap m_pipewireValues, m_v4l2Values, m_values;
    QSet<QString> m_kinds;
    bool m_pipewireEnabled = false, m_v4l2Enabled = false, m_configuring = false;
};
}
