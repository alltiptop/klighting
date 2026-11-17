// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/media/combined_media_activity_source.h"
#include <QSignalSpy>
#include <QTest>

using namespace Ambient;
namespace
{
class Source : public MediaActivitySource
{
public:
    bool enabled = false;
    void setEnabled(bool value) override
    {
        enabled = value;
        Q_EMIT changed({ });
    }
    void update(const QVariantMap& values) { Q_EMIT changed(values); }
};
}
class CombinedMediaActivityTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void cameraUsesEitherBackendAndRequiresBothForAbsence()
    {
        auto pipewire = std::make_unique<Source>(), v4l2 = std::make_unique<Source>();
        auto* pw = pipewire.get();
        auto* v4l = v4l2.get();
        CombinedMediaActivitySource source(std::move(pipewire), std::move(v4l2));
        QSignalSpy spy(&source, &MediaActivitySource::changed);
        source.setRequiredKinds({ "camera-active", "microphone-active" });
        const auto state = [&] { return spy.isEmpty() ? QVariantMap() : spy.last()[0].toMap(); };
        pw->update({ { "camera-active", false }, { "microphone-active", true } });
        QVERIFY(!state().contains("camera-active"));
        QVERIFY(state()["microphone-active"].toBool());
        v4l->update({ { "camera-active", true } });
        QCOMPARE(state()["camera-active"], QVariant(true));
        pw->update({ }); // Direct capture survives a PipeWire disconnect.
        QCOMPARE(state()["camera-active"], QVariant(true));
        QVERIFY(!state().contains("microphone-active"));
        v4l->update({ { "camera-active", false } });
        QVERIFY(!state().contains("camera-active"));
        pw->update({ { "camera-active", true } });
        QCOMPARE(state()["camera-active"], QVariant(true));
        v4l->update({ });
        QCOMPARE(state()["camera-active"], QVariant(true));
        pw->update({ { "camera-active", false } });
        QVERIFY(!state().contains("camera-active"));
        v4l->update({ { "camera-active", false } });
        QCOMPARE(state()["camera-active"], QVariant(false));
    }
    void onlyPollsV4l2ForCameraAndDropsDisabledValues()
    {
        auto pipewire = std::make_unique<Source>(), v4l2 = std::make_unique<Source>();
        auto* pw = pipewire.get();
        auto* v4l = v4l2.get();
        CombinedMediaActivitySource source(std::move(pipewire), std::move(v4l2));
        QSignalSpy spy(&source, &MediaActivitySource::changed);
        source.setRequiredKinds({ "screen-capture" });
        QVERIFY(pw->enabled);
        QVERIFY(!v4l->enabled);
        pw->update({ { "screen-capture", true } });
        source.setRequiredKinds({ "screen-capture", "camera-active" });
        QVERIFY(v4l->enabled);
        v4l->update({ { "camera-active", true } });
        source.setRequiredKinds({ "screen-capture" });
        QVERIFY(!v4l->enabled);
        QCOMPARE(spy.last()[0].toMap(), (QVariantMap { { "screen-capture", true } }));
        v4l->update({ { "camera-active", true } });
        source.setRequiredKinds({ "screen-capture", "camera-active" });
        QVERIFY(!spy.last()[0].toMap().contains("camera-active"));
        source.setEnabled(false);
        QVERIFY(!pw->enabled);
        QVERIFY(!v4l->enabled);
        QVERIFY(spy.last()[0].toMap().isEmpty());
    }
    void singleOptionalBackend_data()
    {
        QTest::addColumn<bool>("pipewire");
        QTest::newRow("pipewire-only") << true;
        QTest::newRow("v4l2-only") << false;
    }
    void singleOptionalBackend()
    {
        QFETCH(bool, pipewire);
        auto input = std::make_unique<Source>();
        auto* observed = input.get();
        CombinedMediaActivitySource source(
            pipewire ? std::move(input) : nullptr, pipewire ? nullptr : std::move(input));
        QSignalSpy spy(&source, &MediaActivitySource::changed);
        source.setRequiredKinds({ "camera-active" });
        observed->update({ { "camera-active", false } });
        QCOMPARE(spy.last()[0].toMap()["camera-active"], QVariant(false));
        observed->update({ { "camera-active", true } });
        QCOMPARE(spy.last()[0].toMap()["camera-active"], QVariant(true));
    }
};
QTEST_GUILESS_MAIN(CombinedMediaActivityTest)
#include "test_combined_media_activity.moc"
