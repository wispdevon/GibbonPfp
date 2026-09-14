#include "controller.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class ControllerTests : public QObject {
    Q_OBJECT
  private slots:
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
