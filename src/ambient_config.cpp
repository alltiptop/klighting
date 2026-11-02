// SPDX-License-Identifier: GPL-3.0-or-later
#include "ambient_config.h"
namespace Ambient
{
AmbientConfig::AmbientConfig(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
    , m_controller(new SettingsController(this))
    , m_recorder(new ConditionRecorder(this))
{
    m_nativeSection = !data.value(QStringLiteral("X-Ambient-Section")).isEmpty();
    if (m_nativeSection)
        m_controller->setSection(data.value(QStringLiteral("X-Ambient-Section")));
    setButtons(Apply | Default);
    connect(m_controller, &SettingsController::changed, this, &AmbientConfig::updateState);
    updateState();
}
void AmbientConfig::load()
{
    m_recorder->cancel();
    m_controller->load();
    updateState();
}
void AmbientConfig::save()
{
    m_controller->save();
    updateState();
}
void AmbientConfig::defaults()
{
    m_recorder->cancel();
    m_controller->defaults();
    updateState();
}
void AmbientConfig::updateState()
{
    setNeedsSave(m_controller->dirty());
    setRepresentsDefaults(m_controller->representsDefaults());
}
}
