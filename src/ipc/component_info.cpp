// SPDX-License-Identifier: GPL-3.0-or-later
#include "component_info.h"
#include "build_identity.h"
#include "lighting_ipc.h"
#include <QDBusArgument>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QRegularExpression>

namespace Ambient
{
namespace
{
    QStringList capabilities(Component component)
    {
        switch (component) {
        case Component::Service:
            return { QStringLiteral("lighting") };
        case Component::Desktop:
            return { QStringLiteral("desktop-state"), QStringLiteral("window-class"),
                QStringLiteral("key-state"), QStringLiteral("screen-lock") };
        case Component::Capture:
            return { QStringLiteral("screen-sample"), QStringLiteral("capture-only") };
        }
        return { };
    }
}
QVariantMap componentInfo(Component component)
{
    return { { "protocol", Ipc::Version }, { "buildId", QString::fromLatin1(Build::Id) },
        { "version", QString::fromLatin1(Build::Version) }, { "capabilities", capabilities(component) } };
}
QVariantMap identityInfo(const QVariantMap& snapshot)
{
    QVariantMap info;
    for (const auto* key : { "protocol", "buildId", "version", "capabilities", "instanceId" })
        if (snapshot.contains(QLatin1String(key)))
            info.insert(QLatin1String(key), snapshot.value(QLatin1String(key)));
    static const QRegularExpression identifier(QStringLiteral("\\A[A-Za-z0-9._-]{1,128}\\z"));
    for (const auto* key : { "buildId", "instanceId" })
        if (info.contains(QLatin1String(key))
            && (info.value(QLatin1String(key)).metaType().id() != QMetaType::QString
                || !identifier.match(info.value(QLatin1String(key)).toString()).hasMatch()))
            info.remove(QLatin1String(key));
    return info;
}
bool compatibleComponent(Component component, const QVariantMap& snapshot)
{
    if (snapshot.value("protocol").toInt() != Ipc::Version)
        return false;
    // Protocol 1 predates capability advertisement; its original contract is the baseline.
    if (!snapshot.contains("capabilities"))
        return true;
    const auto offered = qdbus_cast<QStringList>(snapshot.value("capabilities"));
    // Individual desktop functions can become unavailable independently.
    const auto requiredCapabilities = component == Component::Desktop
        ? QStringList { QStringLiteral("desktop-state") }
        : capabilities(component);
    for (const auto& required : requiredCapabilities)
        if (!offered.contains(required))
            return false;
    return true;
}
InstallationPaths installationPaths()
{
    return { QString::fromUtf8(Build::ServiceMetadata), QString::fromUtf8(Build::DesktopPlugin),
        QString::fromUtf8(Build::CapturePlugin) };
}
QVariantMap installedComponentInfo(const QString& path, bool plugin)
{
    if (path.isEmpty())
        return { };
    QJsonObject info;
    if (plugin) {
        // metaData() reads the embedded JSON without dlopen or plugin construction.
        info = QPluginLoader(path).metaData().value("MetaData").toObject().value("X-KLighting").toObject();
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4096)
            return { };
        info = QJsonDocument::fromJson(file.readAll()).object();
    }
    if (!info.value("buildId").isString() || info.value("buildId").toString().isEmpty()
        || info.value("buildId").toString().size() > 128)
        return { };
    return identityInfo(info.toVariantMap());
}
}
