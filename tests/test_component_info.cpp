// SPDX-License-Identifier: GPL-3.0-or-later
#include "component_info.h"
#include "lighting_ipc.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace Ambient;
class ComponentInfoTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void compatibilityDoesNotRequireTheSameBuild()
    {
        for (const auto component : { Component::Service, Component::Desktop, Component::Capture }) {
            auto info = componentInfo(component);
            QVERIFY(!info.value("buildId").toString().isEmpty());
            info["buildId"] = "another-build";
            info["version"] = "another-release";
            QVERIFY(compatibleComponent(component, info));
            info["capabilities"] = QStringList { "unrelated-feature" };
            QVERIFY(!compatibleComponent(component, info));
            info.remove("capabilities");
            QVERIFY(compatibleComponent(component, info)); // Original protocol-1 peers.
            info["protocol"] = Ipc::Version + 1;
            QVERIFY(!compatibleComponent(component, info));
        }
    }
    void metadataReadFailuresAreUnknown()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("service.json");
        QVERIFY(installedComponentInfo(path, false).isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{broken");
        file.close();
        QVERIFY(installedComponentInfo(path, false).isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{\"buildId\":\"installed\",\"version\":\"0.27.0\"}");
        file.close();
        QCOMPARE(installedComponentInfo(path, false).value("buildId").toString(), "installed");
        QVERIFY(installedComponentInfo(path, true).isEmpty());
    }
    void buildIdentifiersCannotContainMarkup()
    {
        QCOMPARE(identityInfo({ { "buildId", "valid-build.123" } }).value("buildId").toString(),
            "valid-build.123");
        for (const auto& id : { QString("<b>build</b>"), QString("build\n1"), QString(129, 'a') })
            QVERIFY(!identityInfo({ { "buildId", id } }).contains("buildId"));
    }
    void generatedInstallationRecordMatchesThisBuild()
    {
        const auto record = installedComponentInfo(SERVICE_METADATA_PATH, false);
        QCOMPARE(record.value("buildId"), componentInfo(Component::Service).value("buildId"));
        QCOMPARE(record.value("version"), componentInfo(Component::Service).value("version"));
    }
    void providerInstanceIdentityMustBeABoundedString()
    {
        QCOMPARE(identityInfo({ { "instanceId", "provider-123" } }).value("instanceId").toString(),
            "provider-123");
        for (const auto& id : { QVariant(123), QVariant(QString()), QVariant(QString(129, 'a')),
                 QVariant(QString("<b>provider</b>")) })
            QVERIFY(!identityInfo({ { "instanceId", id } }).contains("instanceId"));
    }
};
QTEST_GUILESS_MAIN(ComponentInfoTest)
#include "test_component_info.moc"
