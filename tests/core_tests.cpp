#include "engine.h"
#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <random>
#include <tiffio.h>
#include <webp/encode.h>

using namespace gibbon;
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void smallWorkerStack() {
        QTemporaryDir dir;
        QImage image(60, 80, QImage::Format_RGB888);
        image.fill(Qt::gray);
        auto path = dir.filePath("thread.png");
        QVERIFY(image.save(path));
        QString error;
        auto thread = std::unique_ptr<QThread>(QThread::create([&] {
            try {
                Settings s;
                s.autoCrop = false;
                Engine engine;
                engine.process(path, s);
            } catch (const std::exception &e) {
                error = errorText(e);
            }
        }));
        thread->setStackSize(512 * 1024);
        thread->start();
        QVERIFY(thread->wait(10000));
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
    void independentCodecs() {
        QTemporaryDir dir;
        auto path = dir.filePath("gradient16.tiff");
        auto *t = TIFFOpen(path.toUtf8().constData(), "w");
        QVERIFY(t);
        TIFFSetField(t, TIFFTAG_IMAGEWIDTH, 60);
        TIFFSetField(t, TIFFTAG_IMAGELENGTH, 80);
        TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, 3);
        TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 16);
        TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
        TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(t, TIFFTAG_ORIENTATION, ORIENTATION_RIGHTTOP);
        std::vector<quint16> row(180, 12345);
        for (int y = 0; y < 80; ++y)
            QVERIFY(TIFFWriteScanline(t, row.data(), y) >= 0);
        TIFFClose(t);
        auto decoded = Engine::decode(path, Settings{});
        QCOMPARE(decoded.size(), QSize(80, 60));
        QVERIFY(std::abs(decoded.pixelColor(0, 0).redF() - 12345. / 65535) < .0001);
        QImage image(60, 80, QImage::Format_RGBA8888);
        image.fill(QColor(80, 120, 160, 90));
        uint8_t *data = nullptr;
        auto size = WebPEncodeLosslessRGBA(image.constBits(), image.width(), image.height(),
                                           image.bytesPerLine(), &data);
        QVERIFY(size > 0);
        QFile file(dir.filePath("alpha.webp"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(reinterpret_cast<const char *>(data), qint64(size)), qint64(size));
        file.close();
        WebPFree(data);
        auto webp = Engine::decode(file.fileName(), Settings{});
        QCOMPARE(webp.size(), image.size());
        QCOMPARE(webp.pixelColor(0, 0).alpha(), 90);
    }
    void settingsRoundtrip() {
        Settings s;
        s.background = "fast";
        s.brightness = .4;
        s.crop = {.1, .1, .3, .4};
        QCOMPARE(Settings::fromJson(s.json()).json(), s.json());
        auto j = s.json();
        j["version"] = 2;
        QVERIFY_EXCEPTION_THROWN(Settings::fromJson(j), std::runtime_error);
        j = s.json();
        j["width"] = 480;
        QVERIFY_EXCEPTION_THROWN(Settings::fromJson(j), std::runtime_error);
    }
    void dimensions() {
        Settings s;
        QCOMPARE(Engine::outputSize({4000, 6000}, s), QSize(360, 480));
        QCOMPARE(Engine::outputSize({100, 150}, s), QSize(99, 132));
        s.capped = false;
        s.width = s.height = 0;
        QCOMPARE(Engine::outputSize({4000, 6000}, s), QSize(3999, 5332));
        s.width = 720;
        s.height = 960;
        QCOMPARE(Engine::outputSize({4000, 6000}, s), QSize(720, 960));
    }
    void monotonicTone() {
        for (double a : {-1., -.5, 0., .5, 1.}) {
            QCOMPARE(Engine::tone(0, a), 0.);
            QCOMPARE(Engine::tone(1, a), 1.);
            double previous = -1;
            for (int i = 0; i <= 65535; ++i) {
                auto t = Engine::tone(i / 65535., a);
                QVERIFY(t > previous);
                QVERIFY(t >= 0 && t <= 1);
                previous = t;
            }
        }
    }
    void gamut() {
        std::mt19937 random(42);
        std::uniform_real_distribution<float> d(0, 1);
        for (int i = 0; i < 10000; ++i) {
            cv::Vec3f rgb{d(random), d(random), d(random)};
            for (double a : {-1., 1.}) {
                auto r = Engine::adjust(rgb, a);
                for (int c = 0; c < 3; ++c) {
                    QVERIFY(std::isfinite(r[c]));
                    QVERIFY(r[c] >= 0 && r[c] <= 1);
                }
            }
        }
        for (double a : {-1., 1.}) {
            auto black = Engine::adjust({0, 0, 0}, a), white = Engine::adjust({1, 1, 1}, a);
            for (int c = 0; c < 3; ++c) {
                QVERIFY(black[c] < 1e-5);
                QVERIFY(white[c] > .9999);
            }
        }
    }
    void exportAndCollision() {
        QTemporaryDir dir;
        QImage i(1200, 800, QImage::Format_RGB888);
        i.fill(QColor("#bb9977"));
        i.setColorSpace(QColorSpace::DisplayP3);
        QString input = dir.filePath("portrait.png");
        QVERIFY(i.save(input));
        Settings s;
        s.autoCrop = false;
        Engine engine;
        auto r = engine.process(input, s);
        QCOMPARE(r.outputSize, QSize(360, 480));
        QVERIFY(!r.review);
        QCOMPARE(r.preview, QImage::fromData(r.encoded));
        QVERIFY(r.preview.colorSpace().isValid());
        auto first = Engine::save(r, input, dir.path(), s),
             second = Engine::save(r, input, dir.path(), s);
        QVERIFY(first != second);
        QVERIFY(QFile::exists(input));
        QCOMPARE(QImage(input).size(), i.size());
    }
    void alphaAndManualCrop() {
        QTemporaryDir dir;
        QImage image(600, 800, QImage::Format_RGBA8888);
        image.fill(QColor(220, 100, 50, 80));
        auto path = dir.filePath("alpha.png");
        QVERIFY(image.save(path));
        Settings s;
        s.autoCrop = false;
        s.format = "png";
        s.crop = {.1, .1, .5, .5};
        Engine e;
        auto r = e.process(path, s);
        QCOMPARE(r.outputSize, QSize(300, 400));
        QVERIFY(std::abs(r.preview.pixelColor(10, 10).alpha() - 80) <= 1);
        s.format = "jpeg";
        r = e.process(path, s);
        QCOMPARE(r.preview.pixelColor(10, 10).alpha(), 255);
    }
    void cancelAndCorrupt() {
        QTemporaryDir dir;
        QString path = dir.filePath("broken.jpg");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not an image");
        f.close();
        Engine e;
        Settings s;
        QVERIFY_EXCEPTION_THROWN(e.process(path, s), std::runtime_error);
        std::atomic_bool cancel{true};
        QVERIFY_EXCEPTION_THROWN(e.process(path, s, &cancel), std::runtime_error);
    }
    void modelAndReview() {
        QTemporaryDir dir;
        QImage image(1200, 800, QImage::Format_RGB888);
        image.fill(Qt::gray);
        auto path = dir.filePath("blank.png");
        QVERIFY(image.save(path));
        Engine e;
        Settings s;
        auto r = e.process(path, s);
        QVERIFY(r.review);
        QVERIFY(!r.warnings.empty());
        QVERIFY_EXCEPTION_THROWN(Engine::save(r, path, dir.path(), s), std::runtime_error);
        s.automatic = true;
        r = e.process(path, s);
        QVERIFY(!r.review);
        s.autoCrop = false;
        s.background = "fast";
        s.format = "png";
        r = e.process(path, s);
        QCOMPARE(r.outputSize, QSize(360, 480));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
