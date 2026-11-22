#include "controller.h"
#include "fonts.h"
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QUrl local(const QString &path) {
    return QUrl::fromLocalFile(path);
}
QVariantMap qmlMap(QObject *object, const char *name) {
    const auto value = object->property(name);
    return value.metaType() == QMetaType::fromType<QJSValue>() ?
        value.value<QJSValue>().toVariant().toMap() : value.toMap();
}
void addPhoto(Controller &controller, const QString &path) {
    QImage image(600, 800, QImage::Format_RGB32);
    image.fill(Qt::gray);
    QVERIFY(image.save(path));
    controller.add({local(path)});
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15000);
    controller.setCurrent(controller.items().size() - 1);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15000);
    controller.set("background", "off");
    controller.set("automatic", true);
    controller.preview();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15000);
}
}

class ExportTests : public QObject {
    Q_OBJECT
    QTemporaryDir config;

  private slots:
    void initTestCase() {
        QVERIFY(config.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
        QQuickStyle::setStyle("Fusion");
        loadWorkspaceFonts(GIBBON_SOURCE_DIR "/assets/fonts");
    }
    void init() { QSettings().clear(); }

    void samplesDefaultToChecked() {
        ImageStore store;
        Controller c(&store, nullptr, {}, false);
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.selectedCount(), 3);
        QCOMPARE(c.exportOptions(true).value("scope").toString(), "checked");
        QVERIFY(c.exportOptions(true).value("mixed").toBool());
        QVERIFY(!c.quickExport());
        c.select(1, false);
        c.select(2, false);
        QCOMPARE(c.exportOptions(true).value("scope").toString(), "current");
    }
    void customExportCanPrepareAnUnpreviewedReview() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage im(600, 800, QImage::Format_RGB32);
        im.fill(Qt::gray);
        const auto source = dir.filePath("portrait.png");
        QVERIFY(im.save(source));
        ImageStore store;
        Controller c(&store, nullptr, {}, false);
        c.add({local(source)});
        c.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.result().isEmpty());
        c.set("background", "off");

        auto options = c.exportOptions(false);
        auto style = options.value("style").toMap();
        style["width"] = 120;
        style["height"] = 160;
        options["style"] = style;
        QVERIFY(c.startExport(options, local(dir.path()), false));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.result().value("review").toBool());
        QCOMPARE(c.result().value("width").toInt(), 600);
        QVERIFY(!QFileInfo::exists(dir.filePath("portrait-pfp.jpg")));
        c.approve();
        QVERIFY(c.settings().value("approved").toBool());
    }

    void repeatWithPresetAndMissingDestination() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir root(dir.path());
        QVERIFY(root.mkdir("out"));
        const auto out = local(dir.filePath("out"));
        ImageStore store;
        Controller c(&store, nullptr, {}, false);
        addPhoto(c, dir.filePath("first.png"));
        const auto jpeg = c.settings();
        const auto overlay = c.result();
        auto draft = c.exportOptions(true);
        QCOMPARE(draft.value("scope").toString(), "current");
        QCOMPARE(draft.value("style").toMap().value("format").toString(), "jpeg");
        QVERIFY(!c.quickExport());
        auto style = draft.value("style").toMap();
        style["quality"] = 47;
        style["width"] = 180;
        style["height"] = 240;
        style["prefix"] = "repeat-";
        draft["style"] = style;
        QVERIFY(c.startExport(draft, out, false));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(QImageReader(dir.filePath("out/repeat-first-pfp.jpg")).size(), QSize(180, 240));
        QCOMPARE(c.settings(), jpeg);
        QCOMPARE(c.result(), overlay);

        QVERIFY(c.quickExport());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(QImageReader(dir.filePath("out/repeat-first-pfp-1.jpg")).size(), QSize(180, 240));
        QVERIFY2(c.message().contains(QDir::toNativeSeparators(out.toLocalFile())),
                 qPrintable(c.message()));
        ImageStore secondStore;
        Controller resumed(&secondStore, nullptr, {}, false);
        addPhoto(resumed, dir.filePath("second.png"));
        QCOMPARE(resumed.exportOptions(true).value("style").toMap(), style);

        auto preset = gibbon::Settings{};
        preset.background = "off";
        preset.automatic = true;
        preset.format = "png";
        preset.width = 210;
        preset.height = 280;
        preset.prefix = "preset-";
        QFile presetFile(dir.filePath("output.json"));
        auto presetFields = preset.json();
        presetFields["approved"] = true;
        QVERIFY(presetFile.open(QIODevice::WriteOnly));
        QVERIFY(presetFile.write(QJsonDocument(presetFields).toJson()) > 0);
        presetFile.close();
        resumed.loadPreset(local(presetFile.fileName()));
        QTRY_VERIFY_WITH_TIMEOUT(!resumed.busy(), 15000);
        QVERIFY(!resumed.settings().value("approved").toBool());
        QCOMPARE(resumed.exportOptions(true).value("folder").toUrl(), out);
        addPhoto(resumed, dir.filePath("third.png"));
        QCOMPARE(resumed.exportOptions(true).value("style").toMap().value("format").toString(), "jpeg");
        resumed.setCurrent(0);
        QTRY_VERIFY_WITH_TIMEOUT(!resumed.busy(), 15000);
        resumed.loadPreset(local(presetFile.fileName()));
        QTRY_VERIFY_WITH_TIMEOUT(!resumed.busy(), 15000);
        QVERIFY(resumed.quickExport());
        QTRY_VERIFY_WITH_TIMEOUT(!resumed.busy(), 15000);
        QCOMPARE(QImageReader(dir.filePath("out/preset-second-pfp.png")).size(), QSize(210, 280));
        QVERIFY(root.rename("out", "moved"));
        QVERIFY(!resumed.quickExport());
        QVERIFY(!root.exists("out"));
        QCOMPARE(resumed.exportOptions(true).value("style").toMap().value("format").toString(), "png");
    }
    void checkedAndZipFailuresKeepLastCompletedExport() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir root(dir.path());
        for (const QString folder : {"prior", "partial", "mixed"})
            QVERIFY(root.mkdir(folder));
        ImageStore store;
        Controller c(&store, nullptr, {}, false);
        addPhoto(c, dir.filePath("first.png"));
        addPhoto(c, dir.filePath("second.png"));
        c.set("format", "png");
        c.setSize(120, 160);
        c.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        auto initial = c.exportOptions(true);
        QCOMPARE(initial.value("scope").toString(), "checked");
        QVERIFY(initial.value("mixed").toBool());
        initial["scope"] = "current";
        QVERIFY(c.startExport(initial, local(dir.filePath("prior")), false));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        const auto saved = QSettings().value("exportHistory").toMap();
        QVERIFY(!saved.isEmpty());

        auto batch = c.exportOptions(false);
        batch["scope"] = "checked";
        batch["mixed"] = true;
        c.set("automatic", false);
        c.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.result().value("review").toBool());
        QVERIFY(c.startExport(batch, local(dir.filePath("partial")), false));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY2(c.message().contains("1 need review"), qPrintable(c.message()));
        QVERIFY(root.exists("partial/first-pfp.jpg"));
        QVERIFY(!root.exists("partial/second-pfp.png"));
        QCOMPARE(QSettings().value("exportHistory").toMap(), saved);

        batch["scope"] = "zip";
        batch["mixed"] = false;
        auto style = batch.value("style").toMap();
        style["approved"] = true;
        style["automatic"] = true;
        style["width"] = 180;
        style["height"] = 240;
        batch["style"] = style;
        const auto zip = local(dir.filePath("queue.zip"));
        QVERIFY(c.startExport(batch, zip, false));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY2(c.message().contains("1 need review"), qPrintable(c.message()));
        QVERIFY(!root.exists("queue.zip"));
        QCOMPARE(QSettings().value("exportHistory").toMap(), saved);

        // A held photo can still be approved from its editor preview.
        c.approve();
        QVERIFY(c.settings().value("approved").toBool());
        batch["scope"] = "checked";
        batch["mixed"] = true;
        QVERIFY(c.startExport(batch, local(dir.filePath("mixed")), false));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(root.exists("mixed/first-pfp.jpg"));
        QCOMPARE(QImageReader(dir.filePath("mixed/second-pfp.png")).size(), QSize(120, 160));
        QCOMPARE(c.quickExportScope(), "2 checked");
        c.select(0, false);
        QCOMPARE(c.quickExportScope(), "1 checked");
        QVERIFY(c.quickExport());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(root.exists("mixed/second-pfp-1.png"));
        const auto originalSettings = c.settings();
        QVERIFY(c.quickExport(480));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(QImageReader(dir.filePath("mixed/second-pfp-2.png")).size(), QSize(360, 480));
        QCOMPARE(c.settings(), originalSettings);
        QVERIFY(!root.exists("mixed/first-pfp-1.jpg"));
        auto loadedStyle = c.exportOptions(false).value("style").toMap();
        loadedStyle["prefix"] = "changed-";
        const auto mixedPreset = local(dir.filePath("mixed-preset.json"));
        c.saveExportPreset(mixedPreset, loadedStyle);
        c.loadPreset(mixedPreset);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.exportOptions(true).value("mixed").toBool());
        c.approve();
        c.select(0, true);
        QVERIFY(c.quickExport());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(root.exists("mixed/first-pfp-1.jpg"));
        QVERIFY(root.exists("mixed/changed-second-pfp.png"));
        QVERIFY(!root.exists("mixed/changed-first-pfp.png"));

        batch["scope"] = "zip";
        QVERIFY(!c.startExport(batch, local(dir.filePath("first.png")), true));
        QVERIFY(c.message().contains("source photo"));
        QVERIFY(c.startExport(batch, zip, false));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.quickExportScope(), "Queue ZIP");
        QFile archive(zip.toLocalFile());
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const auto complete = archive.readAll();
        archive.close();
        QVERIFY(complete.startsWith("PK"));
        QVERIFY(complete.contains("first-pfp.jpg"));
        QVERIFY(complete.contains("changed-second-pfp.png"));
        QVERIFY(!c.quickExport());
        QVERIFY(!c.startExport(batch, zip, false));
        QVERIFY(archive.open(QIODevice::ReadOnly));
        QCOMPARE(archive.readAll(), complete);
        archive.close();
        const auto zipHistory = QSettings().value("exportHistory").toMap();
        const auto cancelledPath = local(dir.filePath("cancelled.zip"));
        QVERIFY(c.startExport(batch, cancelledPath, false));
        c.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(!root.exists("cancelled.zip"));
        QCOMPARE(QSettings().value("exportHistory").toMap(), zipHistory);
        QVERIFY(archive.remove());
        QVERIFY(c.quickExport());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(root.exists("queue.zip"));
    }
    void dialogDraftAndPresetAreIndependent() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ImageStore *store = new ImageStore;
        Controller c(store, nullptr, {}, false);
        addPhoto(c, dir.filePath("portrait.png"));
        const auto original = c.settings();
        QQmlApplicationEngine qml;
        qml.rootContext()->setContextProperty("backend", &c);
        qml.addImageProvider("photos", store);
        qml.load(QUrl::fromLocalFile(GIBBON_SOURCE_DIR "/qml/Main.qml"));
        QCOMPARE(qml.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
        QVERIFY(window);
        auto *quick = window->findChild<QObject *>("quickExport");
        auto *modal = window->findChild<QObject *>("exportDialog");
        QVERIFY(quick);
        QVERIFY(modal);
        auto *quickSize = window->findChild<QObject *>("quickExportSize");
        QVERIFY(quickSize && quickSize->setProperty("currentIndex", 2));
        QVERIFY(QMetaObject::invokeMethod(quick, "clicked"));
        QTRY_VERIFY(modal->property("visible").toBool());
        QCOMPARE(modal->property("quickHeight").toInt(), 720);
        QVERIFY(QMetaObject::invokeMethod(modal, "close"));
        QTRY_VERIFY(!modal->property("visible").toBool());
        QVERIFY(quickSize->setProperty("currentIndex", 0));
        QVERIFY(QMetaObject::invokeMethod(quick, "clicked"));
        QTRY_VERIFY(modal->property("visible").toBool());
        QCOMPARE(modal->property("scope").toString(), "current");
        c.setDark(false);
        c.setUiScale(150);
        window->resize(1024, 720);
        QCOMPARE(modal->property("sizeMode").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(modal, "changeSize", Q_ARG(int, 4)));
        auto *customWidth = modal->findChild<QObject *>("exportWidth");
        QVERIFY(customWidth);
        for (const auto width : {"", "720", "7200"}) {
            QVERIFY(customWidth->setProperty("text", QString(width)));
            QVERIFY(QMetaObject::invokeMethod(customWidth, "textEdited"));
            QCOMPARE(modal->property("sizeMode").toInt(), 4);
            QVERIFY(customWidth->property("visible").toBool());
        }
        QCOMPARE(qmlMap(modal, "style").value("width").toInt(), 7200);
        QVERIFY(QMetaObject::invokeMethod(modal, "changeSize", Q_ARG(int, 0)));
        auto style = qmlMap(modal, "style");
        QCOMPARE(style.value("format").toString(), "jpeg");
        style["format"] = "png";
        style["width"] = 120;
        style["height"] = 160;
        style["sharpenScreen"] = false;
        const auto preset = local(dir.filePath("preset.json"));
        c.saveExportPreset(preset, style);
        QFile presetFile(preset.toLocalFile());
        QVERIFY(presetFile.open(QIODevice::ReadOnly));
        const auto fields = QJsonDocument::fromJson(presetFile.readAll()).object();
        QCOMPARE(fields.value("format").toString(), "png");
        QCOMPARE(fields.value("width").toInt(), 120);
        QVERIFY(!fields.value("sharpenScreen").toBool());
        QVERIFY(!fields.value("approved").toBool());
        auto *choose = modal->findChild<QObject *>("chooseExportDestination");
        QVERIFY(choose);
        auto *chooseLabel = choose->property("contentItem").value<QObject *>();
        QVERIFY(chooseLabel);
        QCOMPARE(chooseLabel->property("color").value<QColor>(), QColor(Qt::white));
        const auto capture = qEnvironmentVariable("GIBBON_TEST_SCREENSHOTS");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(capture + "/150-export-as.png"));
            c.setDark(true);
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(capture + "/150-export-as-dark.png"));
        }
        auto *cancel = modal->findChild<QObject *>("cancelExport");
        QVERIFY(cancel);
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QTRY_VERIFY(!modal->property("visible").toBool());
        QCOMPARE(c.settings(), original);
        QVERIFY(!QSettings().contains("exportHistory"));

        addPhoto(c, dir.filePath("another.png"));
        c.setDark(false);
        QVERIFY(QMetaObject::invokeMethod(quick, "clicked"));
        QTRY_VERIFY(modal->property("visible").toBool());
        QCOMPARE(modal->property("scope").toString(), "checked");
        QVERIFY(modal->property("perPhoto").toBool());
        for (const auto name : {"exportSize", "exportFormat", "exportPrefix", "exportSharpen",
                                "saveExportPreset"}) {
            auto *control = modal->findChild<QObject *>(name);
            QVERIFY(control);
            QVERIFY(!control->property("visible").toBool());
        }
        if (!capture.isEmpty()) {
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(capture + "/150-export-as-checked.png"));
        }
        auto *perPhoto = modal->findChild<QObject *>("exportPerPhoto");
        QVERIFY(perPhoto);
        QVERIFY(perPhoto->setProperty("checked", false));
        QVERIFY(QMetaObject::invokeMethod(perPhoto, "clicked"));
        QVERIFY(!modal->property("perPhoto").toBool());
        QVERIFY(modal->findChild<QObject *>("exportFormat")->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QVERIFY(!QSettings().contains("exportHistory"));
    }
};

QTEST_MAIN(ExportTests)
#include "export_tests.moc"
