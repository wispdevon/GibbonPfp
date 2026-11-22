#include "controller.h"
#include "fonts.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QPainter>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSettings>
#include <QProcess>
#include <QProcessEnvironment>
#include <csignal>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class ControllerTests : public QObject {
    Q_OBJECT
    QTemporaryDir config;
    static QImage reviewFixture() {
        QImage photo(240, 320, QImage::Format_RGB32);
        photo.fill(QColor("#d7dfe0"));
        QPainter painter(&photo);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#2c4760"));
        painter.drawRoundedRect(QRect(-25, 249, 290, 170), 60, 60);
        painter.setBrush(QColor("#7f563f"));
        painter.drawRoundedRect(QRect(102, 205, 58, 75), 24, 24);
        painter.setBrush(QColor("#4e372c"));
        painter.drawEllipse(QRect(64, 74, 130, 157));
        painter.end();
        return photo;
    }
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
        QQuickStyle::setStyle("Fusion");
        loadWorkspaceFonts(GIBBON_SOURCE_DIR "/assets/fonts");
    }
    void queueDefaultsAndCompleteZip() {
        QTemporaryDir dir;
        QImage image(120, 160, QImage::Format_RGB32); image.fill(Qt::gray);
        auto first = dir.filePath("first.png"), second = dir.filePath("second.png"), third = dir.filePath("third.png");
        QVERIFY(image.save(first)); QVERIFY(image.save(second)); QVERIFY(image.save(third));
        ImageStore images; Controller c(&images);
        c.add({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)});
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.set("brightness", .3);
        QCOMPARE(c.queue[1].settings.brightness, 0.);
        c.setCurrent(1); QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.settings()["brightness"].toDouble(), 0.);
        c.setCurrent(0); QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.queue[1].selected = false;
        c.queue[1].settings.crop = QRectF(.1, .1, .5, .5);
        c.queue[1].settings.strokes.append(QJsonObject{{"keep", true}});
        c.queue[1].settings.approved = true;
        c.set("background", "off");
        c.applyAll(); QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.queue[1].settings.brightness, .3);
        QCOMPARE(c.queue[1].settings.cropZoom, 70.);
        QVERIFY(c.queue[1].settings.crop.isEmpty());
        QVERIFY(c.queue[1].settings.strokes.isEmpty());
        QVERIFY(!c.queue[1].settings.approved);
        c.set("brightness", .6);
        c.add({QUrl::fromLocalFile(third)}); QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.queue[2].settings.brightness, .3);
        const auto session = QUrl::fromLocalFile(dir.filePath("session.json"));
        c.saveSession(session);
        ImageStore otherImages; Controller other(&otherImages);
        other.loadSession(session); QTRY_VERIFY_WITH_TIMEOUT(!other.busy(), 15000);
        QCOMPARE(other.importDefaults.brightness, .3);
        QCOMPARE(Controller::validateRecovery(QJsonDocument(c.recoveryJson()).toJson())["importDefaults"].toObject()["brightness"].toDouble(), .3);
        auto old = c.recoveryJson(); old.remove("importDefaults");
        QVERIFY(!Controller::validateRecovery(QJsonDocument(old).toJson()).isEmpty());
        c.exportZip(QUrl::fromLocalFile(first));
        QVERIFY(c.message().contains("other than a source photo"));
        QCOMPARE(QImage(first), image);
        auto zip = QUrl::fromLocalFile(dir.filePath("queue.zip"));
        QFile file(zip.toLocalFile()); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("existing"); file.close();
        c.exportZip(zip); QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.message().contains("ZIP not saved"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("existing")); file.close();
        for (auto &photo : c.queue) photo.settings.approved = true;
        c.exportZip(zip); QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY2(c.message().contains("ZIP saved · 3"), qPrintable(c.message()));
        QProcess reader;
        reader.start("python", {"-c", "import sys,zipfile,json; z=zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None; r=json.loads(z.read('export-report.json')); assert len(r)==3 and all(p['status']=='exported' for p in r); assert len(z.namelist())==4", zip.toLocalFile()});
        QVERIFY(reader.waitForFinished()); QVERIFY2(reader.exitCode()==0, reader.readAllStandardError().constData());
        QVERIFY(file.open(QIODevice::ReadOnly)); auto complete = file.readAll(); file.close();
        c.queue[1].path = dir.filePath("missing.png");
        c.exportZip(zip); QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.message().contains("ZIP not saved"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), complete); file.close();
        c.exportZip(zip); c.cancel(); QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), complete);
    }
    void emptySelectionLeavesCurrentPhotoInPlace() {
        QTemporaryDir dir;
        QImage photo(120, 160, QImage::Format_RGB32);
        photo.fill(Qt::gray);
        const auto path = dir.filePath("portrait.png");
        QVERIFY(photo.save(path));
        ImageStore images;
        Controller c(&images, nullptr, {}, false);
        c.add({QUrl::fromLocalFile(path)});
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.current(), 1);
        for (int row = 0; row < c.items().size(); ++row)
            c.select(row, false);
        const auto revision = c.revision();
        c.removeSelected();
        QCOMPARE(c.current(), 1);
        QCOMPARE(c.items().size(), 4);
        QCOMPARE(c.revision(), revision);
        QVERIFY(!c.busy());
        c.batch(QUrl::fromLocalFile(dir.path()), true);
        QVERIFY(!c.busy());
        QVERIFY(c.message().contains("Select photos"));
    }
    void workspaceSavesProtectSourcePhotos() {
        QTemporaryDir dir;
        const auto path = dir.filePath("portrait.png");
        QImage photo(120, 160, QImage::Format_RGB32);
        photo.fill(Qt::gray);
        QVERIFY(photo.save(path));
        ImageStore images;
        Controller c(&images);
        const auto url = QUrl::fromLocalFile(path);
        c.add({url});
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.saveSession(url);
        QCOMPARE(QImage(path), photo);
        QVERIFY(c.message().contains("other than a source photo"));
        c.savePreset(url);
        QCOMPARE(QImage(path), photo);
        QVERIFY(c.message().contains("other than a source photo"));
    }
    void malformedSessionPreservesWorkspace() {
        QTemporaryDir dir;
        QFile session(dir.filePath("malformed.json"));
        QVERIFY(session.open(QIODevice::WriteOnly));
        session.write(R"({"version":1,"photos":[null]})");
        session.close();
        ImageStore images;
        Controller c(&images);
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        const auto workspace = c.recoveryJson();
        c.loadSession(QUrl::fromLocalFile(session.fileName()));
        QCOMPARE(c.recoveryJson(), workspace);
        QVERIFY(c.message().contains("Invalid photo in session"));
    }
    void highQualityConsentAndCache() {
        QTemporaryDir dir;
        const auto priorModelDir = qgetenv("GIBBON_MODEL_DIR");
        const bool hadModelDir = qEnvironmentVariableIsSet("GIBBON_MODEL_DIR");
        const auto restore = qScopeGuard([&] {
            if (hadModelDir)
                qputenv("GIBBON_MODEL_DIR", priorModelDir);
            else
                qunsetenv("GIBBON_MODEL_DIR");
        });
        // Use the small face graph as a deliberately incompatible quality graph.
        // It loads a real ONNX session without allocating the full portrait model.
        QFile source(GIBBON_SOURCE_DIR "/assets/models/face_detection_yunet_2023mar.onnx");
        QVERIFY(source.open(QIODevice::ReadOnly));
        auto bytes = source.readAll();
        const auto modelPath = dir.filePath("test-session.onnx");
        QFile model(modelPath);
        QVERIFY(model.open(QIODevice::WriteOnly));
        QCOMPARE(model.write(bytes), bytes.size());
        model.close();
        QJsonObject entry{
            {"id", "quality"},
            {"file", "test-session.onnx"},
            {"sha256", QString::fromLatin1(
                           QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
        auto entries = gibbon::Models::manifest();
        for (auto i = 0; i < entries.size(); ++i)
            if (entries[i].toObject()["id"] == "quality")
                entries.replace(i, entry);
        QFile manifest(dir.filePath("manifest.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(QJsonDocument(QJsonObject{{"models", entries}}).toJson());
        manifest.close();
        qputenv("GIBBON_MODEL_DIR", dir.path().toUtf8());
        ImageStore images;
        Controller c(&images);
        QSignalSpy confirmation(&c, &Controller::highQualityConfirmationRequested);
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.set("background", "quality");
        const int revision = c.revision();
        c.preview();
        QVERIFY(c.qualityConfirmationPending());
        QVERIFY(c.busy());
        QVERIFY(!c.highQualityLoaded());
        QCOMPARE(confirmation.count(), 1);
        QCOMPARE(c.revision(), revision);
        c.set("headroom", .2);
        QCOMPARE(c.settings()["headroom"].toDouble(), .08);
        c.confirmHighQuality(false);
        QVERIFY(!c.busy());
        QCOMPARE(c.revision(), revision);
        c.batch({}, false);
        QVERIFY(c.qualityConfirmationPending());
        c.cancel();
        QVERIFY(!c.busy());
        auto output = QUrl::fromLocalFile(dir.filePath("exports"));
        c.exportCurrent(output);
        QVERIFY(c.qualityConfirmationPending());
        c.confirmHighQuality(false);
        QVERIFY(!QFileInfo::exists(output.toLocalFile()));
        const auto session = QUrl::fromLocalFile(dir.filePath("session.json"));
        c.saveSession(session);
        c.loadSession(session);
        QVERIFY(c.qualityConfirmationPending());
        c.confirmHighQuality(false);
        const auto preset = QUrl::fromLocalFile(dir.filePath("preset.json"));
        c.savePreset(preset);
        c.loadPreset(preset);
        QVERIFY(c.qualityConfirmationPending());
        c.confirmHighQuality(false);
        c.preview();
        QVERIFY(c.qualityConfirmationPending());
        c.confirmHighQuality(true);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.highQualityLoaded());
        QVERIFY(c.message().contains("dimension", Qt::CaseInsensitive));
        c.releaseModels();
        QVERIFY(!c.highQualityLoaded());
        c.preview();
        QVERIFY(c.qualityConfirmationPending());
        c.confirmHighQuality(true);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.highQualityLoaded());
        const int prompts = confirmation.count();
        QVERIFY(QFile::remove(modelPath));
        c.set("background", "off");
        c.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.highQualityLoaded());
        c.set("background", "quality");
        c.preview();
        QVERIFY(!c.qualityConfirmationPending());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(confirmation.count(), prompts);
        // With the file removed, a reload would fail lookup. The dimension error
        // proves the existing session was reused instead.
        QVERIFY(c.message().contains("dimension", Qt::CaseInsensitive));
        ImageStore freshImages;
        Controller fresh(&freshImages);
        fresh.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!fresh.busy(), 15000);
        fresh.set("background", "quality");
        fresh.preview();
        QVERIFY(fresh.qualityConfirmationPending());
        fresh.confirmHighQuality(true);
        QTRY_VERIFY_WITH_TIMEOUT(!fresh.busy(), 15000);
        QVERIFY(!fresh.highQualityLoaded());
        fresh.preview();
        QVERIFY(fresh.qualityConfirmationPending());
        fresh.cancel();
    }
    void recoveryRoundtrip() {
        QTemporaryDir recovery;
        ImageStore images;
        QJsonObject saved;
        {
            Controller c(&images, nullptr, recovery.path());
            QVERIFY(!c.recoveryPending());
            c.loadSample();
            QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            c.autosaveRecovery(); // Exercise an existing snapshot even on fast machines.
            c.set("brightness", .23);
            c.stroke(QVariantList{QVariantMap{{"x", .5}, {"y", .5}}}, true, .05);
            QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            c.approve();
            c.select(0, false);
            QVERIFY(c.queue[0].settings.approved);
            QVERIFY(!c.queue[0].selected);
            c.set("background", "quality");
            saved = c.recoveryJson();
            // Earlier processing may already have triggered autosave on a slow runner.
            // The latest edit must debounce, then the file must contain that edit.
            auto persisted = [&] {
                QFile file(recovery.filePath("current.json"));
                if (!file.open(QIODevice::ReadOnly)) return QJsonObject{};
                return QJsonDocument::fromJson(file.readAll()).object();
            };
            QVERIFY(persisted() != saved);
            QTRY_COMPARE_WITH_TIMEOUT(persisted(), saved, 5000);
        }
        Controller c(&images, nullptr, recovery.path());
        QVERIFY(c.recoveryPending());
        QVERIFY(c.recoveryRestorable());
        QVERIFY(!c.busy());
        QCOMPARE(c.items().size(), 0);
        c.resolveRecovery(true);
        QCOMPARE(c.recoveryJson(), saved);
        QCOMPARE(c.items().size(), 3);
        QVERIFY(c.result().isEmpty());
        QVERIFY(!c.highQualityLoaded());
        c.preview();
        QVERIFY(c.qualityConfirmationPending());
        c.cancel();
    }
    void recoveryChangedMissingAndLock() {
        QTemporaryDir root;
        const auto directory = root.filePath("recovery");
        QImage photo(60, 80, QImage::Format_RGB32); photo.fill(Qt::gray);
        const auto first = root.filePath("first.png"), second = root.filePath("second.png");
        QVERIFY(photo.save(first)); QVERIFY(photo.save(second));
        ImageStore images;
        {
            Controller c(&images, nullptr, directory);
            c.add({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)});
            QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            c.queue[0].settings.approved = true;
            c.queue[1].settings.approved = true;
            c.index = 1;
            c.autosaveRecovery();
            QFile before(directory + "/current.json"); QVERIFY(before.open(QIODevice::ReadOnly));
            auto data = before.readAll(); before.close();
            {
                Controller other(&images, nullptr, directory);
                QVERIFY(other.autosaveError().contains("another instance"));
                other.resolveRecovery(false);
                other.autosaveRecovery();
            }
            QVERIFY(before.open(QIODevice::ReadOnly)); QCOMPARE(before.readAll(), data);
        }
        photo.fill(Qt::white); QVERIFY(photo.save(first)); QVERIFY(QFile::remove(second));
        Controller c(&images, nullptr, directory);
        c.resolveRecovery(true);
        QCOMPARE(c.items().size(), 2); QCOMPARE(c.current(), 1);
        QVERIFY(!c.queue[0].settings.approved); QVERIFY(!c.queue[1].settings.approved);
        QVERIFY(c.queue[0].error.contains("Source changed"));
        QCOMPARE(c.queue[1].state, "Missing");
        QVERIFY(c.queue[1].error.contains(second));
    }
    void recoveryBackupCorruptionAndWriteFailure() {
        QTemporaryDir root;
        ImageStore images;
        {
            Controller c(&images, nullptr, root.path());
            c.loadSample(); QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            c.autosaveRecovery();
            c.set("brightness", .5); c.autosaveRecovery();
        }
        auto corrupt = [&] {
            QFile file(root.filePath("current.json")); QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("{broken");
        };
        corrupt();
        {
            Controller c(&images, nullptr, root.path());
            QVERIFY(c.recoveryMessage().contains("previous valid"));
            c.resolveRecovery(true);
            QCOMPARE(c.settings()["brightness"].toDouble(), 0.);
            c.set("brightness", .2);
            QVERIFY(QFile::remove(root.filePath("current.json")));
            QVERIFY(QDir().mkdir(root.filePath("current.json")));
            c.autosaveRecovery();
            QVERIFY(c.autosaveError().contains("Autosave failed"));
            QCOMPARE(c.settings()["brightness"].toDouble(), .2);
            QVERIFY(QFile::exists(root.filePath("previous.json")));
        }
        QVERIFY(QDir().rmdir(root.filePath("current.json")));
        corrupt();
        QVERIFY(QFile::remove(root.filePath("previous.json")));
        Controller c(&images, nullptr, root.path());
        QVERIFY(c.recoveryPending()); QVERIFY(!c.recoveryRestorable());
        c.resolveRecovery(false);
        QVERIFY(!c.recoveryPending());
        c.autosaveRecovery();
        QFile file(root.filePath("current.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(Controller::validateRecovery(file.readAll())["photos"].toArray().isEmpty());
    }
    void recoveryCrashWriter() {
        const auto directory = qEnvironmentVariable("GIBBON_CRASH_RECOVERY_DIR");
        if (directory.isEmpty()) QSKIP("Subprocess helper");
        ImageStore images;
        Controller c(&images, nullptr, directory);
        c.loadSample(); QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.autosaveRecovery(); // Crash must wait for the edit, not this earlier snapshot.
        c.set("brightness", .55);
        const auto expected = c.recoveryJson();
        auto latestSaved = [&] {
            QFile file(directory + "/current.json");
            return file.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(file.readAll()).object() == expected;
        };
        QTRY_VERIFY_WITH_TIMEOUT(latestSaved(), 5000);
#ifdef Q_OS_UNIX
        std::raise(SIGKILL);
#else
        std::_Exit(0);
#endif
    }
    void recoveryAfterForcedTermination() {
        QTemporaryDir root;
        QProcess child;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("GIBBON_CRASH_RECOVERY_DIR", root.path());
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(), {"recoveryCrashWriter"});
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished(30000));
#ifdef Q_OS_UNIX
        QCOMPARE(child.exitStatus(), QProcess::CrashExit);
#endif
        ImageStore images;
        Controller c(&images, nullptr, root.path());
        QVERIFY2(c.autosaveError().isEmpty(), qPrintable(c.autosaveError()));
        QVERIFY(c.recoveryRestorable());
        c.resolveRecovery(true);
        QCOMPARE(c.settings()["brightness"].toDouble(), .55);
    }
    void progressAndStaleUpdates() {
        ImageStore images;
        Controller c(&images);
        c.working = true;
        c.beginProgress();
        auto token = c.operation;
        c.acceptProgress(token, 2, 5, {"Inference", "fast"});
        QVERIFY(c.processingProgress().contains("Photo 2 of 5"));
        c.acceptProgress(token - 1, 3, 5, {"Stale"});
        QVERIFY(!c.processingProgress().contains("Stale"));
        c.cancel();
        QVERIFY(c.processingProgress().contains("Cancellation pending"));
        c.endProgress();
        c.acceptProgress(token, 3, 5, {"Late"});
        QVERIFY(c.processingProgress().isEmpty());
        c.working = false;
    }
    void releaseAndDevicePreference() {
        QSettings().remove("processingDevice");
        const auto restore = qScopeGuard([] { QSettings().remove("processingDevice"); });
        ImageStore images;
        Controller c(&images);
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        auto result = c.result();
        auto settings = c.settings();
        c.setProcessingDevice("cpu");
        QCOMPARE(c.processingDevice(), "cpu");
        QCOMPARE(c.result(), result);
        QCOMPARE(c.settings(), settings);
        c.releaseModels();
        QCOMPARE(c.result(), result);
        QVERIFY(!c.highQualityLoaded());
        ImageStore other;
        Controller restored(&other);
        QCOMPARE(restored.processingDevice(), "cpu");
        c.set("background", "quality");
        c.preview();
        QVERIFY(c.qualityConfirmationPending());
        c.setProcessingDevice("automatic");
        QCOMPARE(c.processingDevice(), "cpu");
        c.releaseModels();
        QVERIFY(c.qualityConfirmationPending());
        c.cancel();
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
    void recoveryDialogScreenshot() {
        QTemporaryDir root;
        { ImageStore images; Controller c(&images, nullptr, root.path());
          c.loadSample(); QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000); }
        QQmlApplicationEngine qml;
        auto *images = new ImageStore;
        qml.addImageProvider("photos", images);
        Controller c(images, nullptr, root.path());
        c.setUiScale(150);
        qml.rootContext()->setContextProperty("backend", &c);
        qml.load(QUrl::fromLocalFile(GIBBON_SOURCE_DIR "/qml/Main.qml"));
        QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().front());
        QVERIFY(window); window->resize(1024, 720); window->show();
        QTest::qWait(200);
        auto *dialog = window->findChild<QObject *>("recoveryDialog");
        QVERIFY(dialog); QVERIFY(dialog->property("visible").toBool());
        QVERIFY(dialog->property("height").toDouble() * 1.5 <= 720);
        const auto captures = qEnvironmentVariable("GIBBON_TEST_SCREENSHOTS");
        if (!captures.isEmpty()) { QDir().mkpath(captures); QVERIFY(window->grabWindow().save(captures + "/recovery-150.png")); }
        c.resolveRecovery(true);
        QVERIFY(!dialog->property("visible").toBool());
        c.recoveryError = "Autosave unavailable: another instance owns recovery. Save a session manually.";
        emit c.recoveryChanged();
        QTest::qWait(150);
        auto *warning = window->findChild<QQuickItem *>("autosaveError");
        QVERIFY(warning); QVERIFY(warning->isVisible());
        if (!captures.isEmpty()) QVERIFY(window->grabWindow().save(captures + "/autosave-unavailable-150.png"));
        qDeleteAll(qml.rootObjects());
        c.setUiScale(100);
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
        QScopeGuard deleteWindow([&] { qDeleteAll(qml.rootObjects()); });
        c.add({QUrl::fromLocalFile(path)});
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        auto *source = window->findChild<QQuickItem *>("cropSource");
        auto *crop = window->findChild<QQuickItem *>("cropOutline");
        auto *stroke = window->findChild<QQuickItem *>("cropOutlineStroke");
        auto *focusStroke = window->findChild<QQuickItem *>("cropOutlineFocus");
        auto *result = window->findChild<QQuickItem *>("resultImage");
        auto *input = window->findChild<QQuickItem *>("cropInput");
        QVERIFY(source && crop && stroke && focusStroke && result && input);
        QCOMPARE(source->childItems(), QList<QQuickItem *>{crop});
        QCOMPARE(crop->childItems(), (QList<QQuickItem *>{stroke, input}));
        QCOMPARE(crop->property("color").value<QColor>(), QColor(Qt::transparent));
        QCOMPARE(stroke->property("color").value<QColor>(), QColor(Qt::transparent));
        QCOMPARE(focusStroke->property("color").value<QColor>(), QColor(Qt::transparent));
        const auto sourcePixels = gibbon::Engine::decode(path, {});
        auto verifyOutput = [&] {
            QCOMPARE(source->property("source").toUrl(),
                     QUrl(QString("image://photos/source?%1").arg(c.revision())));
            QCOMPARE(result->property("source").toUrl(),
                     QUrl(QString("image://photos/output?%1").arg(c.revision())));
            QVERIFY(crop->isVisible());
            QCOMPARE(crop->width(), source->property("paintedWidth").toDouble() * c.result()["cropW"].toDouble());
            QCOMPARE(crop->height(), source->property("paintedHeight").toDouble() * c.result()["cropH"].toDouble());
            const auto oriented = sourcePixels.transformed(QTransform().rotate(c.settings()["rotation"].toInt()))
                                      .scaled(1200, 1200, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                      .convertToFormat(QImage::Format_RGBA8888);
            QCOMPARE(images->requestImage("source", nullptr, {}), oriented);
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
        QVERIFY(!window->findChild<QQuickItem *>("maskHold"));
        window->requestActivate();
        auto *tabs = window->findChild<QQuickItem *>("inspectorTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->property("currentIndex").toInt(), 0);
        auto *brush = window->findChild<QQuickItem *>("brushInput");
        QVERIFY(brush);
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
            QVERIFY(!brush->property("enabled").toBool());
            window->setProperty("queueOpen", false);
            window->setProperty("adjustmentsOpen", true);
            tabs->setProperty("currentIndex", 1);
            QTRY_VERIFY(brush->property("enabled").toBool());
            QTest::qWait(100);
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
            tabs->setProperty("currentIndex", 0);
        }
        // Reopen the scrolling sidebar and appearance popup at the smallest workspace.
        window->setProperty("queueOpen", false);
        window->setProperty("adjustmentsOpen", true);
        QTest::qWait(100);
        auto *frameTab = window->findChild<QQuickItem *>("frameTab");
        auto *refineTab = window->findChild<QQuickItem *>("refineTab");
        auto *frameScroll = window->findChild<QQuickItem *>("frameScroll");
        auto *refineScroll = window->findChild<QQuickItem *>("refineScroll");
        QVERIFY(tabs && frameTab && refineTab && frameScroll && refineScroll);
        QCOMPARE(tabs->property("currentIndex").toInt(), 0);
        QVERIFY(frameTab->property("checked").toBool());
        QVERIFY(frameScroll->isVisible());
        QVERIFY(!refineScroll->isVisible());
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/150-frame-1024.png"));
        const auto tabPoint = refineTab->mapToScene(QPointF(refineTab->width()/2, refineTab->height()/2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, tabPoint);
        QTRY_COMPARE(tabs->property("currentIndex").toInt(), 1);
        QVERIFY(!frameScroll->isVisible());
        QVERIFY(refineScroll->isVisible());
        auto *clearStrokes = window->findChild<QQuickItem *>("clearStrokes");
        QVERIFY(clearStrokes && clearStrokes->isVisible());
        auto *featherValue = window->findChild<QQuickItem *>("featherValue");
        auto *featherSlider = window->findChild<QQuickItem *>("featherSlider");
        QVERIFY(featherValue && featherSlider);
        QCOMPARE(featherValue->property("text").toString(), QString("0.0 px"));
        c.set("feather", 2.5);
        QTRY_COMPARE(featherValue->property("text").toString(), QString("2.5 px"));
        QCOMPARE(featherSlider->property("value").toDouble(), 2.5);
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/150-refine-1024.png"));
        c.set("feather", 0.);
        c.set("strokes", QVariantList{});
        QTRY_VERIFY(!clearStrokes->isVisible());
        auto *exportAs = window->findChild<QQuickItem *>("exportAs");
        auto *quickExport = window->findChild<QQuickItem *>("quickExport");
        QVERIFY(exportAs && quickExport);
        QVERIFY(exportAs->isVisible() && quickExport->isVisible());
        QVERIFY(exportAs->mapToScene(QPointF{}).x() >= 0);
        QVERIFY(quickExport->mapToScene(QPointF(quickExport->width(), 0)).x() <= window->width());
        const auto *headerSample = window->findChild<QQuickItem *>("loadSample");
        const auto *headerQueue = window->findChild<QQuickItem *>("toggleQueue");
        const auto *headerOverflow = window->findChild<QQuickItem *>("workspaceActions");
        QVERIFY(headerSample && headerQueue && headerOverflow);
        QVERIFY(!headerSample->property("iconOnly").toBool());
        QVERIFY(headerSample->mapToScene(QPointF(headerSample->width(), 0)).x() <= headerQueue->mapToScene(QPointF{}).x());
        QVERIFY(headerOverflow->mapToScene(QPointF(headerOverflow->width(), 0)).x() <= window->width());
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
        const auto *choiceLabel = scaleChoice->property("contentItem").value<QQuickItem *>();
        const auto *indicator = scaleChoice->property("indicator").value<QQuickItem *>();
        QVERIFY(choiceLabel && indicator);
        QCOMPARE(choiceLabel->x(), 12.);
        QVERIFY(choiceLabel->x() + choiceLabel->width() + 4 <= indicator->x());
        QCOMPARE(scaleChoice->width() - indicator->x() - indicator->width(), 12.);
        auto *popup = scaleChoice->property("popup").value<QObject *>();
        QVERIFY(popup);
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTest::qWait(150);
        auto *popupItem = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(popupItem);
        int checked = 0;
        std::function<void(QQuickItem *)> checkRows = [&](QQuickItem *item) {
            if (item->objectName() == "choiceDelegate") {
                auto font = item->property("font").value<QFont>();
                QCOMPARE(font.family(), "Inter");
                QVERIFY(font.weight() >= QFont::Medium);
                const auto *label = item->property("contentItem").value<QQuickItem *>();
                QVERIFY(label);
                QCOMPARE(item->width(), popupItem->width());
                QCOMPARE(label->x(), 12.);
                QCOMPARE(item->width() - label->x() - label->width(), 12.);
                QVERIFY(item->height() >= 32.);
                ++checked;
            }
            for (auto *child : item->childItems())
                checkRows(child);
        };
        checkRows(popupItem);
        QVERIFY(checked > 0);
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/150-dropdown-fonts-1024.png"));
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QVERIFY(QMetaObject::invokeMethod(appearance, "close"));
        QTest::qWait(100);
        auto *modelsDialog = window->findChild<QObject *>("modelDialog");
        QVERIFY(modelsDialog);
        QVERIFY(QMetaObject::invokeMethod(modelsDialog, "open"));
        QTest::qWait(100);
        QVERIFY(modelsDialog->property("height").toDouble() * c.uiScale() / 100 <= window->height());
        auto *deviceLabel = window->findChild<QQuickItem *>("inferenceStatus");
        QVERIFY(deviceLabel);
        QCOMPARE(deviceLabel->property("text").toString(), c.inferenceStatus());
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/gpu-models-150.png"));
        QVERIFY(QMetaObject::invokeMethod(modelsDialog, "close"));
        c.working = true;
        c.beginProgress();
        c.acceptProgress(c.operation, 2, 5, {"Cached mask", "export · fast", 1200, true});
        QTest::qWait(120);
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/processing-stage-150.png"));
        c.endProgress();
        c.working = false;
        // Crop arrows belong to the focused left pane, including after scaling.
        input->forceActiveFocus();
        QVERIFY(focusStroke->isVisible());
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
        QCOMPARE(c.items().size(), 4);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 70.);
        window->setProperty("adjustmentsOpen", true);
        tabs->setProperty("currentIndex", 0);
        c.setCropZoom(65);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.set("headroom", .06);
        c.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 65.);
        QVERIFY2(!c.result()["review"].toBool(), qPrintable(c.result()["warnings"].toString()));
        if (!captures.isEmpty()) {
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(captures + "/sample-headroom-zoom.png"));
        }
        QTemporaryDir sampleExport;
        QVERIFY(sampleExport.isValid());
        c.exportCurrent(QUrl::fromLocalFile(sampleExport.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(QImage(sampleExport.filePath("gibbon-sample-pfp.jpg")).size(), QSize(768, 1024));
        c.set("background", "quality");
        c.preview();
        QVERIFY(c.qualityConfirmationPending());
        auto *confirmationDialog = window->findChild<QObject *>("qualityConfirmation");
        QVERIFY(confirmationDialog);
        QTRY_VERIFY(confirmationDialog->property("visible").toBool());
        c.setUiScale(150);
        window->resize(1024, 720);
        QTest::qWait(100);
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/quality-confirmation-150.png"));
        auto *cancelQuality = window->findChild<QQuickItem *>("cancelQuality");
        QVERIFY(cancelQuality);
        auto cancelPoint =
            cancelQuality
                ->mapToScene(QPointF(cancelQuality->width() / 2, cancelQuality->height() / 2))
                .toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, cancelPoint);
        QTRY_VERIFY(!c.qualityConfirmationPending());
        QVERIFY(!c.busy());
        QVERIFY(!c.highQualityLoaded());
        c.set("background", "fast");
        const auto unreadablePath = dir.filePath("unreadable.png");
        QFile unreadable(unreadablePath);
        QVERIFY(unreadable.open(QIODevice::WriteOnly));
        QVERIFY(unreadable.write("unreadable image") > 0);
        unreadable.close();
        c.add({QUrl::fromLocalFile(unreadablePath)});
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.setCurrent(c.items().size() - 1);
        QVERIFY(source->property("source").toUrl().isEmpty());
        QVERIFY(!crop->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(source->property("source").toUrl().isEmpty());
        QVERIFY(!crop->isVisible());
        QSettings().clear();
    }

    void actionMenusStayInsideWorkspace() {
        QQmlApplicationEngine qml;
        auto *images = new ImageStore;
        qml.addImageProvider("photos", images);
        Controller c(images, nullptr, {}, false);
        qml.rootContext()->setContextProperty("backend", &c);
        qml.load(QUrl::fromLocalFile(GIBBON_SOURCE_DIR "/qml/Main.qml"));
        QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().front());
        QVERIFY(window);
        QScopeGuard deleteWindow([&] { qDeleteAll(qml.rootObjects()); });
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);

        const auto captures = qEnvironmentVariable("GIBBON_TEST_SCREENSHOTS");
        if (!captures.isEmpty())
            QVERIFY(QDir().mkpath(captures));
        const QList<QPair<QSize, int>> cases = {
            {QSize(1024, 720), 150}, {QSize(1024, 720), 100}, {QSize(1440, 900), 100}
        };
        struct MenuCase {
            const char *button;
            const char *menu;
            bool above;
        };
        const MenuCase menus[] = {
            {"workspaceActions", "workspaceMenu", false},
            {"queueActions", "queueMenu", false},
            {"photoActions", "photoMenu", true}
        };
        for (const auto &[size, scale] : cases) {
            window->resize(size);
            c.setUiScale(scale);
            QTest::qWait(100);
            window->setProperty("adjustmentsOpen", size.width() * 100 / scale >= 1100);
            window->setProperty("queueOpen", true);
            QTest::qWait(100);
            for (const auto &[button, menuName, above] : menus) {
                auto *trigger = window->findChild<QQuickItem *>(button);
                auto *menu = window->findChild<QObject *>(menuName);
                QVERIFY(trigger && trigger->isVisible() && menu);
                trigger->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window, Qt::Key_Space);
                QTRY_VERIFY(menu->property("opened").toBool());

                const auto *background = menu->property("background").value<QQuickItem *>();
                QVERIFY(background);
                const QRectF bounds = background->mapRectToScene(background->boundingRect());
                const auto geometry = QString("%1: bounds %2,%3 %4×%5 at %6×%7/%8%")
                    .arg(menuName).arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())
                    .arg(size.width()).arg(size.height()).arg(scale);
                QVERIFY2(QRectF(QPointF{}, QSizeF(size)).contains(bounds), qPrintable(geometry));
                const QRectF anchor = trigger->mapRectToScene(trigger->boundingRect());
                QVERIFY2(qAbs(bounds.right() - anchor.right()) <= 2., qPrintable(geometry));
                QVERIFY2(above ? bounds.bottom() <= anchor.top() : bounds.top() >= anchor.bottom(),
                         qPrintable(geometry));

                const auto count = menu->property("count").toInt();
                QVERIFY(count > 0);
                for (int row = 0; row < count; ++row) {
                    QQuickItem *item = nullptr;
                    QVERIFY(QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, row)));
                    if (!item || item->property("text").toString().isEmpty())
                        continue;
                    QCOMPARE(item->property("leftPadding").toReal(), 12.);
                    QCOMPARE(item->property("rightPadding").toReal(), 12.);
                    QVERIFY(item->height() >= 32.);
                    const auto *label = item->property("contentItem").value<QQuickItem *>();
                    QVERIFY(label && label->implicitWidth() <= label->width() + 1.);
                    if (item->property("checkable").toBool()) {
                        const auto *indicator = item->property("indicator").value<QQuickItem *>();
                        QVERIFY(indicator && indicator->x() >= 12.);
                    }
                }
                if (!captures.isEmpty()) {
                    const auto path = QString("%1/%2-%3x%4-%5.png").arg(captures, menuName)
                                          .arg(size.width()).arg(size.height()).arg(scale);
                    QVERIFY(window->grabWindow().save(path));
                }
                QTest::keyClick(window, Qt::Key_Down);
                QTRY_VERIFY(menu->property("currentIndex").toInt() >= 0);
                QTest::keyClick(window, Qt::Key_Escape);
                QTRY_VERIFY(!menu->property("visible").toBool());
                QTRY_VERIFY(trigger->hasActiveFocus());
            }
        }
    }

    void workspaceMenuDistinguishesUnavailableActions() {
        QQmlApplicationEngine qml;
        auto *images = new ImageStore;
        qml.addImageProvider("photos", images);
        Controller c(images, nullptr, {}, false);
        c.setDark(true);
        c.setUiScale(150);
        qml.rootContext()->setContextProperty("backend", &c);
        qml.load(QUrl::fromLocalFile(GIBBON_SOURCE_DIR "/qml/Main.qml"));
        QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().front());
        QVERIFY(window);
        QScopeGuard deleteWindow([&] { qDeleteAll(qml.rootObjects()); });
        window->resize(1024, 720);
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        auto *trigger = window->findChild<QQuickItem *>("workspaceActions");
        auto *menu = window->findChild<QObject *>("workspaceMenu");
        QVERIFY(trigger && menu);
        const QSet<QString> needsPhoto = {"Save session…", "Load preset…", "Save preset…"};
        const auto captures = qEnvironmentVariable("GIBBON_TEST_SCREENSHOTS");
        for (const bool loaded : {false, true}) {
            if (loaded) {
                c.loadSample();
                QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            }
            trigger->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_Space);
            QTRY_VERIFY(menu->property("opened").toBool());
            int matched = 0;
            for (int row = 0; row < menu->property("count").toInt(); ++row) {
                QQuickItem *item = nullptr;
                QVERIFY(QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, row)));
                const auto label = item ? item->property("text").toString() : QString{};
                if (label.isEmpty())
                    continue;
                const bool needsCurrentPhoto = needsPhoto.contains(label);
                matched += needsCurrentPhoto;
                const bool enabled = !needsCurrentPhoto || loaded;
                QCOMPARE(item->isEnabled(), enabled);
                QVERIFY2(enabled ? item->opacity() == 1. : item->opacity() < .7, qPrintable(label));
            }
            QCOMPARE(matched, needsPhoto.size());
            if (!captures.isEmpty()) {
                QVERIFY(QDir().mkpath(captures));
                const auto name = loaded ? "/workspace-menu-loaded-dark-150.png" : "/workspace-menu-empty-dark-150.png";
                QVERIFY(window->grabWindow().save(captures + name));
            }
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!menu->property("visible").toBool());
        }
    }

    void panelDividersHaveBoundedWidths() {
        QQmlApplicationEngine qml;
        auto *images = new ImageStore;
        qml.addImageProvider("photos", images);
        Controller c(images, nullptr, {}, false);
        c.setUiScale(100);
        c.setDark(false);
        qml.rootContext()->setContextProperty("backend", &c);
        qml.load(QUrl::fromLocalFile(GIBBON_SOURCE_DIR "/qml/Main.qml"));
        QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().front());
        QVERIFY(window);
        QScopeGuard deleteWindow([&] { qDeleteAll(qml.rootObjects()); });
        window->resize(1440, 900);
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        window->setProperty("queueOpen", true);
        window->setProperty("adjustmentsOpen", true);
        auto *queue = window->findChild<QQuickItem *>("queuePane");
        auto *inspector = window->findChild<QQuickItem *>("inspectorPane");
        auto *crop = window->findChild<QQuickItem *>("leftPane");
        auto *result = window->findChild<QQuickItem *>("rightPane");
        const auto *panes = window->findChild<QQuickItem *>("panes");
        QVERIFY(queue && inspector && crop && result && panes);

        auto divider = [window](const QString &name) -> QQuickItem * {
            for (auto *item : window->findChildren<QQuickItem *>(name))
                if (item->isVisible())
                    return item;
            return nullptr;
        };
        auto dragTo = [&](const QString &name, int x) {
            QTest::qWait(80);
            const auto *handle = divider(name);
            QVERIFY2(handle, qPrintable(name));
            const auto start = handle->mapToScene(QPointF(handle->width()/2, handle->height()/2)).toPoint();
            const QPoint finish(x, start.y());
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(window, finish);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, finish);
        };
        auto click = [window](const QQuickItem *item) {
            QTest::qWait(100);
            const auto point = item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint();
            QVERIFY(QRect(QPoint{}, window->size()).contains(point));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        };
        const auto captures = qEnvironmentVariable("GIBBON_TEST_SCREENSHOTS");
        auto capture = [&](const QString &name) {
            if (!captures.isEmpty()) {
                QVERIFY(QDir().mkpath(captures));
                QTest::qWait(100);
                QVERIFY(window->grabWindow().save(captures + "/" + name + ".png"));
            }
        };
        QTRY_VERIFY(qAbs(queue->width() - 224) < 1.);
        QTRY_VERIFY(qAbs(inspector->width() - 270) < 1.);
        QTRY_VERIFY(qAbs(crop->width() - result->width()) < 1.);
        auto *firstTabStop = window->findChild<QQuickItem *>("addPhotos");
        QVERIFY(firstTabStop);
        firstTabStop->forceActiveFocus(Qt::TabFocusReason);
        QSet<QString> reachedDividers;
        for (int step = 0; step < 100 && reachedDividers.size() < 3; ++step) {
            QTest::keyClick(window, Qt::Key_Tab);
            const auto *focused = window->activeFocusItem();
            if (focused == firstTabStop)
                break;
            if (focused && focused->objectName().endsWith("Divider"))
                reachedDividers.insert(focused->objectName());
        }
        QCOMPARE(reachedDividers, QSet<QString>({"queueDivider", "photoDivider", "inspectorDivider"}));
        dragTo("queueDivider", window->width()-2);
        QTRY_VERIFY(qAbs(queue->width() - 380) < 1.);
        dragTo("queueDivider", 2);
        QTRY_VERIFY(qAbs(queue->width() - 170) < 1.);
        dragTo("inspectorDivider", 2);
        QTRY_VERIFY(qAbs(inspector->width() - 420) < 1.);
        dragTo("inspectorDivider", window->width()-2);
        QTRY_VERIFY(qAbs(inspector->width() - 240) < 1.);
        const auto *queueCaption = window->findChild<QQuickItem *>("queueCaption");
        const auto *checkedCount = window->findChild<QQuickItem *>("queueSelectionCount");
        QVERIFY(queueCaption && checkedCount);
        QVERIFY(queueCaption->mapRectToScene(queueCaption->boundingRect()).right() <=
                checkedCount->mapRectToScene(checkedCount->boundingRect()).left());
        const auto *refine = window->findChild<QQuickItem *>("refineTab");
        QVERIFY(refine);
        click(refine);
        c.setReference(QUrl::fromLocalFile(c.items().front().toMap()["path"].toString()));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.stroke({QVariantList{.2, .3}}, true, .025);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        const auto inspectorBounds = inspector->mapRectToScene(inspector->boundingRect());
        for (const auto *name : {"referenceCommand", "removeReference", "brushChoice", "clearStrokes"}) {
            const auto *control = window->findChild<QQuickItem *>(name);
            QVERIFY(control && control->isVisible());
            const auto bounds = control->mapRectToScene(control->boundingRect());
            QVERIFY2(bounds.left() >= inspectorBounds.left() && bounds.right() <= inspectorBounds.right(), name);
        }
        capture("panel-divider-sidebar-minima-100");
        dragTo("photoDivider", 2);
        QTRY_VERIFY(qAbs(crop->width() - 120) < 1.);
        dragTo("photoDivider", window->width()-2);
        QTRY_VERIFY(qAbs(result->width() - 120) < 1.);
        auto *photoDivider = divider("photoDivider");
        QVERIFY(photoDivider);
        photoDivider->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_VERIFY(result->width() > 125.);
        capture("panel-divider-wide-100");

        const auto *queueToggle = window->findChild<QQuickItem *>("toggleQueue");
        const auto *inspectorToggle = window->findChild<QQuickItem *>("toggleInspector");
        QVERIFY(queueToggle && inspectorToggle);
        const auto chosenCropWidth = crop->width();
        click(queueToggle);
        QTRY_VERIFY(!queue->isVisible());
        QTRY_VERIFY(qAbs(crop->width() - chosenCropWidth) < 1.);
        click(queueToggle);
        QTRY_VERIFY(queue->isVisible());
        QTRY_VERIFY(qAbs(crop->width() - chosenCropWidth) < 1.);

        c.setDark(true);
        c.setUiScale(150);
        window->resize(1024, 720);
        QTest::qWait(100);
        if (!inspector->isVisible())
            click(inspectorToggle);
        QTRY_VERIFY(!queue->isVisible() && inspector->isVisible());
        dragTo("inspectorDivider", 2);
        QTRY_VERIFY(inspector->width() > 350. && inspector->width() < 420.);
        QTRY_VERIFY(crop->width() >= 119. && result->width() >= 119.);
        QVERIFY(inspector->mapRectToScene(inspector->boundingRect()).right() <= window->width());
        const auto compactInspectorWidth = inspector->width();
        click(queueToggle);
        QTRY_VERIFY(queue->isVisible() && !inspector->isVisible());
        QTRY_VERIFY(qAbs(queue->width() - 170) < 1.);
        dragTo("queueDivider", window->width()-2);
        QTRY_VERIFY(qAbs(queue->width() - 380) < 1.);
        auto *queueDivider = divider("queueDivider");
        QVERIFY(queueDivider);
        queueDivider->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_VERIFY(queue->width() < 375.);
        capture("panel-divider-queue-150");
        click(inspectorToggle);
        QTRY_VERIFY(!queue->isVisible() && inspector->isVisible());
        QTRY_VERIFY(qAbs(inspector->width() - compactInspectorWidth) < 1.);
        auto *inspectorDivider = divider("inspectorDivider");
        QVERIFY(inspectorDivider);
        inspectorDivider->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY(inspector->width() < compactInspectorWidth - 5.);
        capture("panel-divider-inspector-150");
    }

    void queueControlsAndApplyConfirmation() {
        QQmlApplicationEngine qml;
        auto *images = new ImageStore;
        qml.addImageProvider("photos", images);
        Controller c(images, nullptr, {}, false);
        c.setUiScale(100);
        qml.rootContext()->setContextProperty("backend", &c);
        qml.load(QUrl::fromLocalFile(GIBBON_SOURCE_DIR "/qml/Main.qml"));
        QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().front());
        QVERIFY(window);
        QScopeGuard deleteWindow([&] { qDeleteAll(qml.rootObjects()); });
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.set("background", "off");
        c.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        const auto *count = window->findChild<QQuickItem *>("queueSelectionCount");
        QVERIFY(count);
        QVERIFY(count->property("text").toString().contains("3 of 3"));
        for (int row = 0; row < c.items().size(); ++row)
            c.select(row, false);
        QVERIFY(count->property("text").toString().contains("0 of 3"));
        for (const auto *name : {"prepareSelected", "removeSelected"}) {
            const auto *action = window->findChild<QQuickItem *>(name);
            QVERIFY(action);
            QVERIFY(!action->isEnabled());
        }
        const auto captures = qEnvironmentVariable("GIBBON_TEST_SCREENSHOTS");
        if (!captures.isEmpty()) {
            QDir().mkpath(captures);
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(captures + "/queue-no-selection.png"));
        }

        c.setCrop(.2, .2, .5, .5);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        auto *apply = window->findChild<QQuickItem *>("applyAll");
        auto *dialog = window->findChild<QObject *>("applyAllConfirmation");
        QVERIFY(apply && dialog);
        QVERIFY(apply->isEnabled());
        const auto crop = c.settings()["crop"];
        const auto *photoActions = window->findChild<QQuickItem *>("photoActions");
        auto *photoMenu = window->findChild<QObject *>("photoMenu");
        QVERIFY(photoActions && photoMenu);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          photoActions->mapToScene(QPointF(photoActions->width()/2, photoActions->height()/2)).toPoint());
        QTRY_VERIFY(photoMenu->property("visible").toBool());
        QTRY_VERIFY(apply->isVisible());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          apply->mapToScene(QPointF(apply->width()/2, apply->height()/2)).toPoint());
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(c.settings()["crop"], crop);
        if (!captures.isEmpty()) {
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(captures + "/apply-to-all-confirmation.png"));
        }
        const auto *cancel = window->findChild<QQuickItem *>("cancelApplyAll");
        QVERIFY(cancel);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          cancel->mapToScene(QPointF(cancel->width() / 2, cancel->height() / 2)).toPoint());
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(c.settings()["crop"], crop);
        QVERIFY(!c.busy());

        QVERIFY(QMetaObject::invokeMethod(apply, "triggered"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        const auto *confirm = window->findChild<QQuickItem *>("confirmApplyAll");
        QVERIFY(confirm);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          confirm->mapToScene(QPointF(confirm->width() / 2, confirm->height() / 2)).toPoint());
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.settings()["crop"] != crop);
    }

    void emptyWorkspaceAndFilteredReview() {
        QTemporaryDir dir;
        const auto photo = reviewFixture();
        const auto path = dir.filePath("review-portrait.png");
        QVERIFY(photo.save(path));

        QQmlApplicationEngine qml;
        auto *images = new ImageStore;
        qml.addImageProvider("photos", images);
        Controller c(images, nullptr, {}, false);
        c.setUiScale(100);
        qml.rootContext()->setContextProperty("backend", &c);
        qml.load(QUrl::fromLocalFile(GIBBON_SOURCE_DIR "/qml/Main.qml"));
        QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().front());
        QVERIFY(window);
        QScopeGuard deleteWindow([&] { qDeleteAll(qml.rootObjects()); });

        const auto *header = window->findChild<QQuickItem *>("commandHeader");
        const auto *open = window->findChild<QQuickItem *>("addPhotos");
        const auto *folder = window->findChild<QQuickItem *>("addFolder");
        const auto *sample = window->findChild<QQuickItem *>("loadSample");
        QVERIFY(header && open && sample && folder);
        QVERIFY(!window->findChild<QQuickItem *>("emptyOpenPhotos"));
        const auto captures = qEnvironmentVariable("GIBBON_TEST_SCREENSHOTS");
        const QList<QSize> sizes = {{1024, 720}, {1440, 900}};
        auto capture = [&](const QString &name, const QSize &size) {
            if (captures.isEmpty())
                return;
            QDir().mkpath(captures);
            QTest::qWait(100);
            const auto file = QString("%1/%2-%3x%4.png").arg(captures, name).arg(size.width()).arg(size.height());
            QVERIFY(window->grabWindow().save(file));
        };
        for (const auto &size : sizes) {
            window->resize(size);
            window->setProperty("adjustmentsOpen", true);
            window->setProperty("queueOpen", size.width() >= 1100);
            QVERIFY(open->isVisible() && open->isEnabled());
            QVERIFY(sample->isVisible() && sample->isEnabled());
            QVERIFY(folder->isVisible() && folder->isEnabled());
            QCOMPARE(header->height(), 54.);
            QVERIFY(open->mapToScene(QPointF{}).x() < folder->mapToScene(QPointF{}).x());
            QVERIFY(folder->mapToScene(QPointF{}).x() < sample->mapToScene(QPointF{}).x());
            QVERIFY(!folder->property("iconOnly").toBool());
            capture("empty-workspace", size);
        }
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          sample->mapToScene(QPointF(sample->width() / 2, sample->height() / 2)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.items().size(), 3);
        QVERIFY(open->isVisible() && sample->isVisible() && folder->isVisible());
        auto *photoQueue = window->findChild<QQuickItem *>("photoQueue");
        QVERIFY(photoQueue && photoQueue->isVisible());
        photoQueue->polish();
        QTest::qWait(100);
        for (int row : {1, 2}) {
            QCOMPARE(c.items()[row].toMap()["state"].toString(), "Imported");
            QQuickItem *delegate = nullptr;
            QVERIFY(QMetaObject::invokeMethod(photoQueue, "itemAtIndex",
                                              Q_RETURN_ARG(QQuickItem *, delegate), Q_ARG(int, row)));
            QVERIFY(delegate);
            const auto *thumbnail = delegate->findChild<QQuickItem *>("queueThumbnail");
            QVERIFY(thumbnail);
            QCOMPARE(thumbnail->property("source").toUrl(), c.items()[row].toMap()["thumb"].toUrl());
            QTRY_COMPARE(thumbnail->property("status").toInt(), 1);
        }
        capture("sample-batch", sizes.back());
        c.add({QUrl::fromLocalFile(path)});
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.setCurrent(3);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.set("background", "off");
        c.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.result()["review"].toBool());

        window->setProperty("adjustmentsOpen", false);
        window->setProperty("queueOpen", true);
        auto *filter = window->findChild<QQuickItem *>("queueFilter");
        QVERIFY(filter);
        filter->setProperty("currentIndex", 1);
        QVERIFY(QMetaObject::invokeMethod(filter, "activated", Q_ARG(int, 1)));
        auto *hidden = window->findChild<QQuickItem *>("queueHiddenSelectionCount");
        const auto *reason = window->findChild<QQuickItem *>("reviewReason");
        const auto *approve = window->findChild<QQuickItem *>("approveFraming");
        QVERIFY(hidden && hidden->isVisible());
        QVERIFY(hidden->property("text").toString().contains("3 selected"));
        QVERIFY(reason && reason->property("text").toString().contains(c.result()["warnings"].toString()));
        QVERIFY(approve && approve->isVisible());
        QCOMPARE(c.selectedCount(), 4);
        for (const auto &size : sizes) {
            window->resize(size);
            QTest::qWait(100);
            window->setProperty("adjustmentsOpen", size.width() >= 1100);
            window->setProperty("queueOpen", true);
            QTRY_VERIFY(hidden->isVisible());
            capture("filtered-review", size);
        }
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          approve->mapToScene(QPointF(approve->width() / 2, approve->height() / 2)).toPoint());
        QTRY_VERIFY(!c.result()["review"].toBool());
        QVERIFY(c.queue[3].settings.approved);
        QVERIFY(!c.queue[0].settings.approved);
        QVERIFY(hidden->property("text").toString().contains("4 selected"));
    }

    void queueNavigationRespectsFiltersAndSelection() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto photo = reviewFixture();
        QList<QUrl> urls;
        for (const auto *name : {"first.png", "second.png", "third.png", "fourth.png"}) {
            const auto path = dir.filePath(name);
            QVERIFY(photo.save(path));
            urls.append(QUrl::fromLocalFile(path));
        }

        QQmlApplicationEngine qml;
        auto *images = new ImageStore;
        qml.addImageProvider("photos", images);
        Controller c(images, nullptr, {}, false);
        c.importDefaults.background = "off";
        qml.rootContext()->setContextProperty("backend", &c);
        qml.load(QUrl::fromLocalFile(GIBBON_SOURCE_DIR "/qml/Main.qml"));
        QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().front());
        QVERIFY(window);
        QScopeGuard deleteWindow([&] { qDeleteAll(qml.rootObjects()); });
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        c.add(urls);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        for (const int row : {0, 2}) {
            c.queue[row].settings.autoCrop = false;
            c.queue[row].settings.cropZoom = 100;
        }
        c.batch({}, false);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.queue[0].state, "Ready");
        QCOMPARE(c.queue[1].state, "Needs review");
        QCOMPARE(c.queue[2].state, "Ready");
        QCOMPARE(c.queue[3].state, "Needs review");
        c.select(1, false);

        auto *filter = window->findChild<QQuickItem *>("queueFilter");
        auto *position = window->findChild<QQuickItem *>("photoPosition");
        auto *previous = window->findChild<QQuickItem *>("previousPhoto");
        auto *next = window->findChild<QQuickItem *>("nextPhoto");
        auto *review = window->findChild<QQuickItem *>("nextReviewPhoto");
        QVERIFY(filter && position && previous && next && review);
        filter->setProperty("currentIndex", 2);
        QVERIFY(QMetaObject::invokeMethod(filter, "activated", Q_ARG(int, 2)));
        QVERIFY(!previous->isEnabled());
        QVERIFY(next->isEnabled());
        QVERIFY(position->property("text").toString().contains("1 of 2 visible"));
        auto *apply = window->findChild<QQuickItem *>("applyAll");
        auto *confirmation = window->findChild<QObject *>("applyAllConfirmation");
        QVERIFY(apply && confirmation);
        QVERIFY(QMetaObject::invokeMethod(apply, "triggered"));
        QTRY_VERIFY(confirmation->property("visible").toBool());
        QVERIFY(!next->isEnabled());
        QTest::keyClick(window, Qt::Key_Right, Qt::AltModifier);
        QCOMPARE(c.current(), 0);
        QVERIFY(QMetaObject::invokeMethod(confirmation, "reject"));
        QTRY_VERIFY(next->isEnabled());
        const auto captures = qEnvironmentVariable("GIBBON_TEST_SCREENSHOTS");
        if (!captures.isEmpty()) {
            window->resize(1440, 900);
            QTest::qWait(100);
            QDir().mkpath(captures);
            QVERIFY(window->grabWindow().save(captures + "/batch-navigation-filtered.png"));
        }

        auto click = [&](const QQuickItem *item) {
            const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        };
        click(next);
        QTRY_COMPARE(c.current(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(!next->isEnabled());
        QVERIFY(position->property("text").toString().contains("2 of 2 visible"));
        auto *crop = window->findChild<QQuickItem *>("cropInput");
        QVERIFY(crop);
        crop->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Left, Qt::AltModifier);
        QTRY_COMPARE(c.current(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QTest::keyClick(window, Qt::Key_Right, Qt::AltModifier);
        QTRY_COMPARE(c.current(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);

        click(review);
        QTRY_COMPARE(c.current(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(filter->property("currentIndex").toInt(), 2);
        QVERIFY(position->property("text").toString().contains("Outside filter"));
        QTest::keyClick(window, Qt::Key_R, Qt::AltModifier | Qt::ShiftModifier);
        QTRY_COMPARE(c.current(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QVERIFY(c.result()["review"].toBool());
        QCOMPARE(c.selectedCount(), 3);
        QVERIFY(!c.queue[1].selected);
        if (!captures.isEmpty()) {
            filter->setProperty("currentIndex", 0);
            QVERIFY(QMetaObject::invokeMethod(filter, "activated", Q_ARG(int, 0)));
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(captures + "/batch-navigation.png"));
        }
    }

    void cropZoomState() {
        QTemporaryDir dir;
        ImageStore images;
        Controller c(&images);
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.items().size(), 3);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 70.);
        const auto initial = c.result();
        c.setCropZoom(65);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 65.);
        QVERIFY(c.result()["cropW"].toDouble() > initial["cropW"].toDouble());
        // The detected head anchor stays fixed as zoom and headroom change.
        const auto headY = initial["cropY"].toDouble() + .08 * initial["cropH"].toDouble();
        auto checkHeadroom = [&](double expected) {
            const auto r = c.result();
            QVERIFY(qAbs((headY - r["cropY"].toDouble()) / r["cropH"].toDouble() - expected) <
                    .005);
        };
        checkHeadroom(.08);
        const auto zoomed = c.result();
        c.set("headroom", .06);
        c.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 65.);
        checkHeadroom(.06);
        QCOMPARE(c.result()["cropW"], zoomed["cropW"]);
        c.nudgeCrop(.01, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 65.);
        c.setCropZoom(80);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        checkHeadroom(.06);
        c.undo();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 65.);
        auto session = QUrl::fromLocalFile(dir.filePath("session.json"));
        c.saveSession(session);
        c.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.loadSession(session);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 65.);
        checkHeadroom(.06);
        c.set("headroom", .07);
        c.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 65.);
        checkHeadroom(.07);
        c.setCropZoom(40);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 40.);
        QVERIFY(c.result()["cropX"].toDouble() >= 0);
        QVERIFY(c.result()["cropY"].toDouble() >= 0);
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.items().size(), 3);
        QCOMPARE(c.result()["cropZoom"].toDouble(), 40.);
    }
    void sampleLoadPreservesCachedSources() {
        const auto appName = QCoreApplication::applicationName();
        QTemporaryDir isolation;
        QVERIFY(isolation.isValid());
        QCoreApplication::setApplicationName(QFileInfo(isolation.path()).fileName());
        const auto restoreAppName = qScopeGuard([&] {
            QCoreApplication::setApplicationName(appName);
        });
        const QDir cache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
        QVERIFY(!cache.exists());
        QVERIFY(QDir().mkpath(cache.path()));
        const auto legacy = cache.filePath("gibbon-sample.png");
        const auto first = cache.filePath("gibbon-sample.jpg");
        const auto second = cache.filePath("gibbon-sample-2.jpg");
        const auto third = cache.filePath("gibbon-sample-3.jpg");
        const auto cleanup = qScopeGuard([&] {
            QFile::remove(legacy);
            QFile::remove(first);
            QFile::remove(second);
            QFile::remove(third);
            QDir().rmdir(cache.path());
        });
        QImage photo(360, 480, QImage::Format_RGB32);
        photo.fill(Qt::gray);
        QVERIFY(photo.save(legacy));
        QVERIFY(photo.save(third));
        const auto previous = Controller::sourceFingerprint(legacy);
        const auto conflict = Controller::sourceFingerprint(third);
        ImageStore images;
        Controller c(&images, nullptr, {}, false);
        c.add({QUrl::fromLocalFile(third)});
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.set("brightness", .19);
        c.select(0, false);
        c.loadSample();
        QVERIFY(c.message().contains("cached sample"));
        QCOMPARE(c.items().size(), 1);
        QCOMPARE(c.settings()["brightness"].toDouble(), .19);
        QCOMPARE(c.selectedCount(), 0);
        QCOMPARE(Controller::sourceFingerprint(third), conflict);
        QVERIFY(!QFile::exists(first));
        QVERIFY(!QFile::exists(second));
        QVERIFY(QFile::remove(third));
        c.loadSample();
        QCOMPARE(c.items().size(), 1);
        QVERIFY(!QFile::exists(third));
        c.select(0, true);
        c.removeSelected();
        c.add({QUrl::fromLocalFile(legacy)});
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.set("brightness", .19);
        c.select(0, false);
        c.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.items().size(), 4);
        QCOMPARE(c.current(), 1);
        QCOMPARE(c.queue[0].path, legacy);
        QCOMPARE(c.queue[1].path, first);
        QCOMPARE(c.queue[2].path, second);
        QCOMPARE(c.queue[3].path, third);
        QCOMPARE(c.queue[0].settings.brightness, .19);
        QVERIFY(!c.queue[0].selected);
        QCOMPARE(Controller::sourceFingerprint(legacy), previous);
        QVERIFY(Controller::sourceFingerprint(third) != conflict);
        QCOMPARE(c.result()["width"].toInt(), 768);
        QCOMPARE(c.result()["height"].toInt(), 1024);

        auto thumbnail = [&](int row) {
            const QUrl url(c.items()[row].toMap()["thumb"].toString());
            return images.requestImage(url.path().mid(1), nullptr, {});
        };
        for (int row : {2, 3}) {
            QCOMPARE(c.queue[row].state, "Imported");
            QCOMPARE(thumbnail(row).size(), QSize(96, 128));
            QCOMPARE(thumbnail(row).pixelColor(48, 64).alpha(), 255);
        }
        QVERIFY(thumbnail(2) != thumbnail(3));
        c.set("brightness", .27);
        c.select(1, false);
        c.select(3, false);
        c.removeSelected();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        QCOMPARE(c.items().size(), 3);
        c.setCurrent(1);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
        c.loadSample();
        QCOMPARE(c.items().size(), 4);
        QCOMPARE(c.current(), 1);
        QCOMPARE(c.selectedCount(), 1);
        QCOMPARE(c.queue[3].path, second);
        QCOMPARE(c.settings()["brightness"].toDouble(), .27);
        const auto revision = c.revision();
        c.loadSample();
        QCOMPARE(c.items().size(), 4);
        QCOMPARE(c.revision(), revision);
        QVERIFY(!c.busy());
    }
    void sampleThumbnailsSurviveSessionAndRecovery() {
        QTemporaryDir test;
        QVERIFY(test.isValid());
        const auto appName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(QFileInfo(test.path()).fileName());
        const auto restoreAppName = qScopeGuard([&] { QCoreApplication::setApplicationName(appName); });
        const QDir cache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
        QVERIFY(!cache.exists());
        const auto cleanup = qScopeGuard([&] {
            for (const auto *resource : gibbon::Engine::samplePortraitResources)
                QFile::remove(cache.filePath(QFileInfo(resource).fileName()));
            QDir().rmdir(cache.path());
        });
        const auto session = QUrl::fromLocalFile(test.filePath("session.json"));
        const auto recovery = test.filePath("recovery");
        auto thumbnail = [](Controller &c, ImageStore &store, int row) {
            const QUrl url(c.items()[row].toMap()["thumb"].toString());
            return store.requestImage(url.path().mid(1), nullptr, {});
        };
        QString secondPath, thirdPath;
        QImage secondThumb;
        ImageStore originalStore;
        {
            Controller c(&originalStore, nullptr, recovery);
            c.loadSample();
            QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            QCOMPARE(c.items().size(), 3);
            secondPath = c.queue[1].path;
            thirdPath = c.queue[2].path;
            secondThumb = thumbnail(c, originalStore, 1);
            QCOMPARE(secondThumb.pixelColor(48, 64).alpha(), 255);
            c.saveSession(session);
        }
        {
            ImageStore store;
            Controller c(&store, nullptr, {}, false);
            c.loadSession(session);
            QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            QCOMPARE(thumbnail(c, store, 1), secondThumb);
            c.setCurrent(1);
            QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            const auto processed = thumbnail(c, store, 1);
            QVERIFY(processed != secondThumb);
            c.loadSample();
            QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            QCOMPARE(thumbnail(c, store, 1), processed);
        }
        QVERIFY(QFile::remove(secondPath));
        QVERIFY(QFile::remove(thirdPath));
        QImage replacement(120, 160, QImage::Format_RGB32);
        replacement.fill(Qt::gray);
        QVERIFY(replacement.save(thirdPath));
        {
            Controller c(&originalStore, nullptr, {}, false);
            c.loadSession(session);
            QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 15000);
            QCOMPARE(thumbnail(c, originalStore, 1).pixelColor(48, 64).alpha(), 0);
            QCOMPARE(thumbnail(c, originalStore, 2).pixelColor(48, 64).alpha(), 0);
        }
        {
            ImageStore store;
            Controller c(&store, nullptr, recovery);
            QVERIFY(c.recoveryPending());
            c.resolveRecovery(true);
            QCOMPARE(c.queue[1].state, "Missing");
            QCOMPARE(c.queue[2].state, "Needs review");
            QCOMPARE(thumbnail(c, store, 0).pixelColor(48, 64).alpha(), 255);
            QCOMPARE(thumbnail(c, store, 1).pixelColor(48, 64).alpha(), 0);
            QCOMPARE(thumbnail(c, store, 2).pixelColor(48, 64).alpha(), 0);
        }
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
        c.applyAll();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
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
        c.set("background", "off");
        c.set("cropZoom", 100);
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
