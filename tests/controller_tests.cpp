#include "controller.h"
#include "fonts.h"
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class ControllerTests : public QObject {
    Q_OBJECT
    QTemporaryDir config;
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
        QQuickStyle::setStyle("Fusion");
        loadWorkspaceFonts(GIBBON_SOURCE_DIR "/assets/fonts");
    }
    void appearance() {
        QSettings().clear();
        ImageStore images;
        Controller c(&images);
        QCOMPARE(c.uiScale(), 100);
        QCOMPARE(c.buttonAccent(), "graphite");
        const auto settings = c.settings();
        const int revision = c.revision();
        c.setUiScale(150);
        c.setButtonAccent("blue");
        c.setUiScale(999);
        c.setButtonAccent("red");
        Controller restored(&images);
        QCOMPARE(restored.uiScale(), 150);
        QCOMPARE(restored.buttonAccent(), "blue");
        QCOMPARE(c.settings(), settings);
        QCOMPARE(c.revision(), revision);
        QSettings().clear();
    }
    void workspace() {
        QTemporaryDir dir;
        QImage photo(600, 800, QImage::Format_RGB32);
        for (int y = 0; y < photo.height(); ++y)
            for (int x = 0; x < photo.width(); ++x)
                photo.setPixelColor(x, y, QColor(x * 255 / 600, y * 255 / 800, 100));
        const auto path = dir.filePath("portrait.png");
        QVERIFY(photo.save(path));
        QQmlApplicationEngine qml;
        auto *images = new ImageStore;
        qml.addImageProvider("photos", images);
        Controller c(images);
        qml.rootContext()->setContextProperty("backend", &c);
        qml.load(QUrl::fromLocalFile(GIBBON_SOURCE_DIR "/qml/Main.qml"));
        QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
        QVERIFY(window);
        c.add({QUrl::fromLocalFile(path)});
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        auto *crop = window->findChild<QQuickItem *>("cropOutput");
        auto *result = window->findChild<QQuickItem *>("resultImage");
        auto *input = window->findChild<QQuickItem *>("cropInput");
        QVERIFY(crop && result && input);
        auto verifyOutput = [&] {
            QCOMPARE(crop->property("source").toUrl(), result->property("source").toUrl());
            QVERIFY(crop->property("source").toString().endsWith(QString::number(c.revision())));
            QSize size;
            auto decoded = images->requestImage("output", &size, {});
            QVERIFY(!decoded.isNull());
            QCOMPARE(size, QSize(c.result()["width"].toInt(), c.result()["height"].toInt()));
        };
        verifyOutput();
        for (const auto &edit : QList<QPair<QString, QVariant>>{{"sharpenScreen", false},
                                                                {"sharpening", "high"},
                                                                {"sharpenScreen", true},
                                                                {"brightness", .3},
                                                                {"rotation", 90},
                                                                {"format", "png"},
                                                                {"background", "fast"},
                                                                {"format", "jpeg"}}) {
            int revision = c.revision();
            c.set(edit.first, edit.second);
            c.preview();
            QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 30000);
            QVERIFY2(c.revision() > revision, qPrintable(c.message()));
            verifyOutput();
        }
        c.setReference(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        verifyOutput();
        c.setCrop(.1, .1, .5, .5);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        verifyOutput();
        c.stroke({QVariantList{.3, .4}, QVariantList{.4, .5}}, false, .025);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        verifyOutput();
        const auto outputSize = QSize(c.result()["width"].toInt(), c.result()["height"].toInt());
        const QString captures = qEnvironmentVariable("GIBBON_TEST_SCREENSHOTS");
        if (!captures.isEmpty())
            QDir().mkpath(captures);
        for (int scale : {80, 100, 150})
            for (bool dark : {false, true})
                for (const auto *accent : {"graphite", "blue"}) {
                    c.setUiScale(scale);
                    c.setDark(dark);
                    c.setButtonAccent(accent);
                    for (const auto size : {QSize(1440, 940), QSize(1024, 720)}) {
                        window->resize(size);
                        QTest::qWait(100);
                        QVERIFY(crop->width() > 0 && result->width() > 0);
                        verifyOutput();
                        QCOMPARE(QSize(c.result()["width"].toInt(), c.result()["height"].toInt()),
                                 outputSize);
                        if (!captures.isEmpty())
                            QVERIFY(window->grabWindow().save(captures +
                                                              QString("/%1-%2-%3-%4.png")
                                                                  .arg(scale)
                                                                  .arg(dark ? "dark" : "light")
                                                                  .arg(accent)
                                                                  .arg(size.width())));
                    }
                }
        // At each scale, map native pointer positions into the crop and mask.
        for (int scale : {80, 100, 150}) {
            c.setUiScale(scale);
            window->resize(1024, 720);
            QTest::qWait(100);
            auto *left = window->findChild<QQuickItem *>("leftPane");
            QVERIFY(left);
            const auto oldWidth = left->width();
            auto divider =
                left->mapToScene(QPointF(left->width() + 5, left->height() / 2)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, divider);
            QTest::mouseMove(window, divider + QPoint(-20, 0));
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, divider + QPoint(-20, 0));
            QTest::qWait(50);
            QVERIFY(left->width() < oldWidth);
            auto start =
                input->mapToScene(QPointF(input->width() / 2, input->height() / 2)).toPoint();
            double before = c.result()["cropX"].toDouble();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(window, start + QPoint(8, 0));
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + QPoint(8, 0));
            QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            QVERIFY(c.result()["cropX"].toDouble() > before);
            verifyOutput();
            window->setProperty("brushMode", 2);
            auto *brush = window->findChild<QQuickItem *>("brushInput");
            QVERIFY(brush);
            auto center =
                brush->mapToScene(QPointF(brush->width() / 2, brush->height() / 2)).toPoint();
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center);
            QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            const auto strokes = c.settings()["strokes"].toList();
            QVERIFY(!strokes.isEmpty());
            const auto point = strokes.last().toMap()["points"].toList().first().toList();
            QVERIFY(qAbs(point[0].toDouble() - .5) < .02);
            QVERIFY(qAbs(point[1].toDouble() - .5) < .02);
            verifyOutput();
            window->setProperty("brushMode", 0);
        }
        // Reopen the scrolling sidebar and appearance popup at the smallest workspace.
        window->setProperty("queueOpen", false);
        window->setProperty("adjustmentsOpen", true);
        QTest::qWait(100);
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/150-adjustments-1024.png"));
        auto *scroll = window->findChild<QQuickItem *>("adjustmentsScroll");
        QVERIFY(scroll);
        auto *flickable = scroll->property("contentItem").value<QObject *>();
        QVERIFY(flickable);
        const double bottom = flickable->property("contentHeight").toDouble() -
                              flickable->property("height").toDouble();
        QVERIFY(bottom > 0);
        QVERIFY(flickable->setProperty("contentY", bottom));
        QTest::qWait(100);
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/150-export-controls-1024.png"));
        auto *sharpen = window->findChild<QQuickItem *>("sharpenScreen");
        QVERIFY(sharpen);
        QVERIFY(sharpen->property("checked").toBool());
        auto *flickableItem = qobject_cast<QQuickItem *>(flickable);
        const double sharpeningY = sharpen->mapToItem(flickableItem, QPointF{}).y() +
                                   flickable->property("contentY").toDouble() - 16;
        flickable->setProperty("contentY", std::clamp(sharpeningY, 0., bottom));
        QTest::qWait(100);
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/150-screen-sharpening-1024.png"));
        auto *appearance = window->findChild<QObject *>("appearanceDialog");
        QVERIFY(appearance);
        QVERIFY(QMetaObject::invokeMethod(appearance, "open"));
        QTest::qWait(100);
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/150-appearance-1024.png"));
        auto *scaleChoice = window->findChild<QQuickItem *>("appearanceScale");
        QVERIFY(scaleChoice);
        const auto controlFont = scaleChoice->property("font").value<QFont>();
        QCOMPARE(controlFont.family(), "Inter");
        QVERIFY(controlFont.weight() >= QFont::Medium);
        auto *popup = scaleChoice->property("popup").value<QObject *>();
        QVERIFY(popup);
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTest::qWait(150);
        auto *popupItem = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(popupItem);
        int checked = 0;
        std::function<void(QQuickItem *)> checkFonts = [&](QQuickItem *item) {
            if (item->objectName() == "choiceDelegate") {
                auto font = item->property("font").value<QFont>();
                QCOMPARE(font.family(), "Inter");
                QVERIFY(font.weight() >= QFont::Medium);
                ++checked;
            }
            for (auto *child : item->childItems())
                checkFonts(child);
        };
        checkFonts(popupItem);
        QVERIFY(checked > 0);
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/150-dropdown-fonts-1024.png"));
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QVERIFY(QMetaObject::invokeMethod(appearance, "close"));
        QTest::qWait(100);
        // Crop arrows belong to the focused left pane, including after scaling.
        input->forceActiveFocus();
        double x = c.result()["cropX"].toDouble();
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.result()["cropX"].toDouble() > x);
        verifyOutput();
        c.setUiScale(100);
        window->resize(1440, 940);
        auto *sampleButton = window->findChild<QQuickItem *>("loadSample");
        QVERIFY(sampleButton);
        auto samplePoint =
            sampleButton->mapToScene(QPointF(sampleButton->width() / 2, sampleButton->height() / 2))
                .toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, samplePoint);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.items().size(), 2);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 100.);
        window->setProperty("adjustmentsOpen", true);
        flickable->setProperty("contentY", 0);
        c.setCropZoom(60);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.set("headroom", .12);
        c.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 60.);
        QTest::qWait(100);
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/sample-headroom-zoom.png"));
        qDeleteAll(qml.rootObjects());
        QSettings().clear();
    }

    void cropZoomState() {
        QTemporaryDir dir;
        ImageStore images;
        Controller c(&images);
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.items().size(), 1);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 100.);
        const auto initial = c.result();
        c.setCropZoom(60);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 60.);
        QVERIFY(c.result()["cropW"].toDouble() > initial["cropW"].toDouble());
        // The known sample head starts at y=180, even as zoom/headroom change.
        auto checkHeadroom = [&](double expected) {
            const auto r = c.result();
            QVERIFY(qAbs((180. / 1600 - r["cropY"].toDouble()) / r["cropH"].toDouble() - expected) <
                    .005);
        };
        checkHeadroom(.08);
        const auto zoomed = c.result();
        c.set("headroom", .12);
        c.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 60.);
        checkHeadroom(.12);
        QCOMPARE(c.result()["cropW"], zoomed["cropW"]);
        c.nudgeCrop(.01, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 60.);
        c.setCropZoom(80);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        checkHeadroom(.12);
        c.undo();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 60.);
        auto session = QUrl::fromLocalFile(dir.filePath("session.json"));
        c.saveSession(session);
        c.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.loadSession(session);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 60.);
        checkHeadroom(.12);
        c.set("headroom", .1);
        c.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 60.);
        checkHeadroom(.1);
        c.setCropZoom(40);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 40.);
        QVERIFY(c.result()["cropX"].toDouble() >= 0);
        QVERIFY(c.result()["cropY"].toDouble() >= 0);
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.items().size(), 1);
    }
    void editAndSession() {
        QTemporaryDir dir;
        QImage image(600, 800, QImage::Format_RGB888);
        image.fill(Qt::gray);
        auto a = dir.filePath("first.png"), b = dir.filePath("second.png");
        QVERIFY(image.save(a));
        QVERIFY(image.save(b));
        ImageStore images;
        Controller c(&images);
        c.add({QUrl::fromLocalFile(a), QUrl::fromLocalFile(b)});
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        QCOMPARE(c.items().size(), 2);
        QVERIFY(c.result()["review"].toBool());
        c.approve();
        QVERIFY(!c.result()["review"].toBool());
        QVERIFY(c.settings()["approved"].toBool());
        c.set("brightness", .2);
        QVERIFY(!c.settings()["approved"].toBool());
        c.undo();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        QCOMPARE(c.settings()["brightness"].toDouble(), 0.);
        c.set("capped", false);
        c.setSize(0, 0);
        QCOMPARE(c.settings()["width"].toInt(), 0);
        c.setSize(720, 960);
        QCOMPARE(c.settings()["height"].toInt(), 960);
        c.set("brightness", .4);
        c.applySelected();
        c.setCurrent(1);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        QCOMPARE(c.settings()["brightness"].toDouble(), .4);
        auto session = QUrl::fromLocalFile(dir.filePath("session.json"));
        c.saveSession(session);
        ImageStore otherImages;
        Controller other(&otherImages);
        other.loadSession(session);
        QTRY_VERIFY_WITH_TIMEOUT(!other.busy(), 10000);
        QCOMPARE(other.items().size(), 2);
        QCOMPARE(other.settings()["brightness"].toDouble(), .4);
        c.set("autoCrop", false);
        c.exportCurrent(QUrl::fromLocalFile(dir.filePath("exports")));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        QVERIFY(QFile::exists(dir.filePath("exports/second-pfp.jpg")));
        c.setCrop(.1, .1, .5, .5);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        QCOMPARE(c.result()["width"].toInt(), 300);
        QCOMPARE(c.result()["height"].toInt(), 400);
    }
};
QTEST_MAIN(ControllerTests)
#include "controller_tests.moc"
