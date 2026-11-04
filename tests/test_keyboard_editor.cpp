// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>
class KeyboardFixture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList keys MEMBER keys CONSTANT)
    Q_PROPERTY(QVariantMap device MEMBER device CONSTANT)
    Q_PROPERTY(QVariantList strips MEMBER strips CONSTANT)
    Q_PROPERTY(bool editableKeys MEMBER editable NOTIFY changed)
    Q_PROPERTY(bool editableLightbar MEMBER editableLightbar CONSTANT)
public:
    QVariantList keys;
    QVariantList strips, stripPreview;
    QVariantMap device { { "width", 14.0 }, { "height", 8.0 }, { "x", 0.0 }, { "y", 0.0 } };
    bool editable = true;
    bool editableLightbar = true;
    QString keyLighting = "#ff9900";
    int keyMode = 1;
    KeyboardFixture()
    {
        for (int i = 0; i < 104; ++i)
            keys << QVariantMap { { "id", 1000 + i * 17 }, { "label", QString::number(i) },
                { "x", double(i % 14) }, { "y", double(i / 14) }, { "width", 0.9 }, { "height", 0.9 } };
    }
    Q_INVOKABLE QVariantList keyPreview(int) const
    {
        QVariantList result;
        for (const auto& key : keys)
            result << QVariantMap { { "id", key.toMap()["id"] }, { "mode", keyMode },
                { "color", keyLighting }, { "inherited", false }, { "notificationId", "test-notify" },
                { "microphoneId", "test-microphone" } };
        return result + stripPreview;
    }
Q_SIGNALS:
    void changed();
    void contentChanged();
    void previewChanged();
};
class KeyboardEditorTest : public QObject
{
    Q_OBJECT
    static QQuickItem* itemNamed(QQuickItem* root, const QString& name)
    {
        if (root->objectName() == name)
            return root;
        for (auto* child : root->childItems())
            if (auto* found = itemNamed(child, name))
                return found;
        return nullptr;
    }
private Q_SLOTS:
    void stripPreviewAndSelection_data()
    {
        QTest::addColumn<bool>("positioned");
        QTest::addColumn<bool>("mirroredHost");
        QTest::newRow("default-line") << false << false;
        QTest::newRow("driver-contour") << true << false;
        QTest::newRow("default-line-rtl") << false << true;
        QTest::newRow("driver-contour-rtl") << true << true;
    }
    void stripPreviewAndSelection()
    {
        QFETCH(bool, positioned);
        QFETCH(bool, mirroredHost);
        KeyboardFixture controller;
        controller.device = { { "width", 100 }, { "height", 60 }, { "x", 0 }, { "y", 0 },
            { "perStrip", true }, { "bodyColor", "#333333" }, { "keyColor", "#111111" } };
        controller.keys = { QVariantMap { { "id", 1000 }, { "label", "Key" }, { "x", 30 }, { "y", 20 },
            { "width", 20 }, { "height", 20 } } };
        const QVariantMap strip { { "id", 65536 }, { "name", "Strip" }, { "pathWidth", 4 },
            { "elements",
                QVariantList { QVariantMap { { "id", 50001 }, { "label", "1" } },
                    QVariantMap { { "id", 50003 }, { "label", "2" } },
                    QVariantMap { { "id", 50009 }, { "label", "3" } } } },
            { "path",
                positioned ? QVariantList { QVariantMap { { "id", 50001 }, { "x", 10 }, { "y", 10 } },
                                 QVariantMap { { "id", 50003 }, { "x", 10 }, { "y", 50 } },
                                 QVariantMap { { "id", 50009 }, { "x", 90 }, { "y", 50 } } }
                           : QVariantList { } } };
        controller.strips = { strip };
        controller.stripPreview = { QVariantMap { { "id", 50001 }, { "mode", 1 }, { "color", "#ff0000" } },
            QVariantMap { { "id", 50003 }, { "mode", 1 }, { "color", "#00ff00" } },
            QVariantMap { { "id", 50009 }, { "mode", 1 }, { "color", "#0000ff" } } };
        QQmlEngine engine;
        QStringList errors;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& warnings) {
            for (const auto& e : warnings)
                errors << e.toString();
        });
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(UI_SOURCE_DIR) + "/KeyboardView.qml"));
        std::unique_ptr<QObject> object(
            component.createWithInitialProperties({ { "controller", QVariant::fromValue(&controller) } }));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* keyboard = qobject_cast<QQuickItem*>(object.get());
        QQuickWindow window;
        window.resize(400, 284);
        QQmlComponent hostComponent(&engine);
        hostComponent.setData(
            "import QtQuick\nItem { property bool mirroredHost: false; "
            "LayoutMirroring.enabled: mirroredHost; LayoutMirroring.childrenInherit: true }",
            QUrl());
        std::unique_ptr<QObject> hostObject(
            hostComponent.createWithInitialProperties({ { "mirroredHost", mirroredHost } }));
        auto* mirroredParent = qobject_cast<QQuickItem*>(hostObject.get());
        QVERIFY(mirroredParent);
        mirroredParent->setParentItem(window.contentItem());
        mirroredParent->setSize(QSizeF(400, 284));
        keyboard->setParentItem(mirroredParent);
        keyboard->setWidth(400);
        keyboard->setHeight(keyboard->implicitHeight());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto capture = [&] { return window.grabWindow(); };
        QTRY_VERIFY(!capture().isNull());
        const QPoint red = positioned ? QPoint(46, 46) : QPoint(25, 264);
        const QPoint green = positioned ? QPoint(46, 200) : QPoint(200, 264);
        const QPoint blue = positioned ? QPoint(354, 200) : QPoint(375, 264);
        QTRY_VERIFY(capture().pixelColor(red).red() > 245 && capture().pixelColor(red).green() < 10);
        const auto image = capture();
        QVERIFY(image.pixelColor(green).green() > 245);
        QVERIFY(image.pixelColor(blue).blue() > 245);
        const auto blend = image.pixelColor(positioned ? QPoint(46, 123) : QPoint(137, 264));
        QVERIFY(blend.red() > 80 && blend.red() < 180);
        QVERIFY(blend.green() > 80 && blend.green() < 180);
        QVERIFY(blend.blue() < 10);
        QCOMPARE(image.pixelColor(10, 10), QColor("#333333"));
        QCOMPARE(image.pixelColor(135, 100), QColor("#111111"));
        if (!positioned)
            QCOMPARE(image.pixelColor(10, 275), QColor("#333333")); // The body includes the fallback strip.
        // Only the outer key border carries its color, on all four sides.
        for (const QPoint p : { QPoint(124, 123), QPoint(161, 86), QPoint(198, 123), QPoint(161, 160) })
            QCOMPARE(image.pixelColor(p), QColor("#ff9900"));
        auto* border = itemNamed(keyboard, "KeyColorBorder");
        QVERIFY(border);
        QCOMPARE(border->property("border").value<QObject*>()->property("width").toInt(), 1);
        auto* label = itemNamed(keyboard, "KeyLabel");
        QVERIFY(label);
        QCOMPARE(label->property("color").value<QColor>(), QColor("#ff9900"));
        QVERIFY(label->property("font").value<QFont>().bold());
        auto* icon = itemNamed(keyboard, "KeyNotificationIcon");
        QVERIFY(icon && icon->isVisible());
        QVERIFY(icon->property("isMask").toBool());
        QCOMPARE(icon->property("color").value<QColor>(), QColor("#ff9900"));
        QCOMPARE(icon->opacity(), 0.65);
        auto* microphone = itemNamed(keyboard, "KeyMicrophoneIcon");
        QVERIFY(microphone && microphone->isVisible());
        QVERIFY(microphone->property("isMask").toBool());
        QCOMPARE(microphone->property("source").toString(), QString("audio-input-microphone"));
        QCOMPARE(microphone->property("color").value<QColor>(), QColor("#ff9900"));
        for (int i = 0; i < 3; ++i) {
            auto* number = itemNamed(keyboard, "StripElementLabel" + QString::number(i));
            QVERIFY(number && number->isVisible());
            QCOMPARE(number->property("text").toString(), QString::number(i + 1));
            QCOMPARE(number->property("color").value<QColor>(), QColor(Qt::white));
            QCOMPARE(number->property("styleColor").value<QColor>(), QColor("#111111"));
        }
        auto selection
            = [&] { return keyboard->property("selection").value<QJSValue>().toVariant().toList(); };
        auto click = [&](int index, Qt::KeyboardModifiers modifiers) {
            auto* button = itemNamed(keyboard, "StripElement" + QString::number(index));
            Q_ASSERT(button);
            QTest::mouseClick(&window, Qt::LeftButton, modifiers,
                button->mapToScene(button->boundingRect().center()).toPoint());
        };
        click(0, Qt::NoModifier);
        QCOMPARE(selection(), QVariantList { 50001 });
        click(2, Qt::ControlModifier);
        QCOMPARE(selection(), (QVariantList { 50001, 50009 }));
        click(0, Qt::NoModifier);
        click(2, Qt::ShiftModifier);
        QCOMPARE(selection(), (QVariantList { 50001, 50003, 50009 }));
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(selection(), (QVariantList { 50001, 50003 }));
        // The contour's empty center must pass mouse events to keyboard keys.
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(160, 120));
        QCOMPARE(selection(), QVariantList { 1000 });
        auto* selected = itemNamed(keyboard, "KeySelectionBorder");
        QVERIFY(selected && selected->isVisible());
        QTRY_COMPARE(capture().pixelColor(122, 123), QColor(Qt::white));
        const auto fill = capture().pixelColor(135, 100);
        QVERIFY(fill.red() > 17 && fill.red() < 100);
        QCOMPARE(fill.red(), fill.green());
        QCOMPARE(fill.green(), fill.blue());
        QCOMPARE(label->property("color").value<QColor>(), QColor("#ff9900"));
        keyboard->setProperty("selection", QVariantList { });
        QVERIFY(!selected->isVisible());
        keyboard->setProperty("pressedKeys", QVariantList { 1000 });
        QVERIFY(selected->isVisible());
        QCOMPARE(selection(), QVariantList { });
        QTRY_COMPARE(capture().pixelColor(122, 123), QColor(Qt::white));
        QCOMPARE(capture().pixelColor(135, 100), QColor("#111111")); // Outline only, no selection fill.
        keyboard->setProperty("pressedKeys", QVariantList { });
        QVERIFY(!selected->isVisible());
        for (int mode : { 2, 1 }) {
            controller.keyMode = mode;
            controller.keyLighting = mode == 2 ? "#ff9900" : "#000000";
            for (auto& preview : controller.stripPreview) {
                auto map = preview.toMap();
                map["mode"] = mode;
                map["color"] = controller.keyLighting;
                preview = map;
            }
            Q_EMIT controller.contentChanged();
            QTRY_COMPARE(capture().pixelColor(red), QColor("#444444"));
            QCOMPARE(capture().pixelColor(124, 123), QColor("#444444"));
            QCOMPARE(label->property("color").value<QColor>(), QColor("#444444"));
            QCOMPARE(icon->property("color").value<QColor>(), QColor("#444444"));
        }
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        keyboard->setParentItem(nullptr);
    }
    void capabilitiesAndStableIds_data()
    {
        QTest::addColumn<bool>("editable");
        QTest::newRow("brightness-only") << false;
        QTest::newRow("uniform-color") << false;
        QTest::newRow("per-key") << true;
    }
    void capabilitiesAndStableIds()
    {
        QFETCH(bool, editable);
        KeyboardFixture controller;
        controller.editable = editable;
        QQmlEngine engine;
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(UI_SOURCE_DIR) + "/KeyboardView.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(
            component.createWithInitialProperties({ { "controller", QVariant::fromValue(&controller) } }));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* item = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(item);
        QQuickWindow window;
        window.resize(700, 400);
        item->setParentItem(window.contentItem());
        item->setWidth(700);
        item->setHeight(400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        int count = 0;
        for (auto* child : item->childItems())
            if (child->property("modelData").isValid()) {
                ++count;
                QCOMPARE(child->isEnabled(), editable);
                const auto data = child->property("modelData").toMap();
                const int index = child->property("index").toInt();
                QCOMPARE(data.value("id").toInt(), 1000 + index * 17);
            }
        QCOMPARE(count, 104);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCOMPARE(item->property("selection").value<QJSValue>().toVariant().toList(),
            editable ? QVariantList { 1000 } : QVariantList { });
        if (editable) {
            QTest::mouseClick(&window, Qt::LeftButton, Qt::ControlModifier, QPoint(70, 20));
            QCOMPARE(item->property("selection").value<QJSValue>().toVariant().toList(),
                (QVariantList { 1000, 1017 }));
        }
        item->setParentItem(nullptr);
    }
};
int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QApplication app(argc, argv);
    KeyboardEditorTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_keyboard_editor.moc"
