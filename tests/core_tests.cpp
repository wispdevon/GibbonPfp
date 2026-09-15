#include "engine.h"
#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QThread>
#include <QtTest>
#include <random>
#include <tiffio.h>
#include <webp/encode.h>

using namespace gibbon;
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void inferenceDevices() {
        cv::Mat rgb(96, 72, CV_32FC3, cv::Scalar(.4, .4, .4));
        Models automatic;
        const auto mask = automatic.mask(rgb, "fast");
        QCOMPARE(mask.size(), rgb.size());
        QVERIFY(cv::checkRange(mask, true, nullptr, 0., 1.00001));
        if (qEnvironmentVariableIsSet("GIBBON_EXPECT_CUDA"))
            QVERIFY2(automatic.inferenceStatus().contains("NVIDIA GPU"),
                     qPrintable(automatic.inferenceStatus()));
        const auto prior = qgetenv("GIBBON_INFERENCE_DEVICE");
        const bool existed = qEnvironmentVariableIsSet("GIBBON_INFERENCE_DEVICE");
        const auto restore = qScopeGuard([&] {
            if (existed) qputenv("GIBBON_INFERENCE_DEVICE", prior);
            else qunsetenv("GIBBON_INFERENCE_DEVICE");
        });
        qputenv("GIBBON_INFERENCE_DEVICE", "cpu");
        Models cpu;
        const auto cpuMask = cpu.mask(rgb, "fast");
        QCOMPARE(cpu.inferenceStatus(), QString("Fast: CPU"));
        QVERIFY(cv::norm(mask, cpuMask, cv::NORM_INF) < .02);
    }
    void relativeZoom() {
        const QRectF initial(.3, .25, .3, .3);
        const double headroom = .08;
        const QPointF anchor(initial.center().x(), initial.y() + headroom * initial.height());
        QCOMPARE(Engine::zoomCrop(initial, anchor, 100, headroom), initial);
        auto wider = Engine::zoomCrop(initial, anchor, 50, headroom);
        QCOMPARE(wider.width(), .6);
        QVERIFY(qAbs((anchor.y() - wider.y()) / wider.height() - headroom) < 1e-8);
        auto moreHeadroom = Engine::zoomCrop(initial, anchor, 50, .2);
        QCOMPARE(moreHeadroom.size(), wider.size());
        QVERIFY(moreHeadroom.y() < wider.y());
        QVERIFY(qAbs((anchor.y() - moreHeadroom.y()) / moreHeadroom.height() - .2) < 1e-8);
        auto bounded = Engine::zoomCrop(initial, QPointF(.99, .01), 40, .25);
        QVERIFY(bounded.left() >= 0 && bounded.top() >= 0 && bounded.right() <= 1 &&
                bounded.bottom() <= 1);
        for (double invalid : {0., 39., 101., 400.})
            QVERIFY_EXCEPTION_THROWN(Engine::zoomCrop(initial, anchor, invalid, headroom),
                                     std::runtime_error);
    }
    void screenSharpening() {
        cv::Mat flat(40, 30, CV_32FC3, cv::Scalar(.4, .4, .4));
        cv::Mat alpha(40, 30, CV_32F, cv::Scalar(1));
        QCOMPARE(cv::norm(flat, Engine::sharpenForScreen(flat, alpha, "high"), cv::NORM_INF), 0.);
        cv::Mat edge = flat.clone();
        edge.colRange(15, 30).setTo(cv::Scalar(.6, .6, .6));
        double previous = 0;
        for (const auto *level : {"low", "standard", "high"}) {
            auto sharp = Engine::sharpenForScreen(edge, alpha, level);
            double change = cv::norm(edge, sharp, cv::NORM_L1);
            QVERIFY(change > previous);
            previous = change;
            QVERIFY(sharp.at<cv::Vec3f>(20, 14)[0] < .4f);
            QVERIFY(sharp.at<cv::Vec3f>(20, 15)[0] > .6f);
        }
        // Hidden RGB must not create a fringe in the visible, constant-color area.
        alpha.colRange(15, 30).setTo(0);
        auto sharp = Engine::sharpenForScreen(edge, alpha, "high");
        QVERIFY(cv::norm(edge, sharp, cv::NORM_INF) < 1e-6);
        std::atomic_bool cancelled{true};
        QVERIFY_EXCEPTION_THROWN(Engine::sharpenForScreen(edge, alpha, "standard", &cancelled),
                                 std::runtime_error);
        QTemporaryDir dir;
        QImage image(60, 80, QImage::Format_RGB32);
        for (int y = 0; y < 80; ++y)
            for (int x = 0; x < 60; ++x)
                image.setPixelColor(
                    x, y, QColor(x < 30 ? 100 : 160, x < 30 ? 100 : 160, x < 30 ? 100 : 160));
        auto path = dir.filePath("edge.png");
        QVERIFY(image.save(path));
        Settings settings;
        settings.autoCrop = false;
        settings.format = "png";
        QVERIFY(settings.sharpenScreen);
        QCOMPARE(Settings::fromJson(settings.json()).sharpening, "standard");
        Engine engine;
        auto on = engine.process(path, settings);
        settings.sharpenScreen = false;
        auto off = engine.process(path, settings);
        QCOMPARE(on.outputSize, off.outputSize);
        QCOMPARE(on.preview, QImage::fromData(on.encoded));
        QVERIFY(on.preview != off.preview);
        QCOMPARE(on.mask, off.mask);
        auto invalid = settings.json();
        invalid["sharpening"] = "ultra";
        QVERIFY_EXCEPTION_THROWN(Settings::fromJson(invalid), std::runtime_error);
    }

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
        QCOMPARE(s.feather, 0.);
        QCOMPARE(Settings::fromJson(s.json()).feather, 0.);
        auto legacy = s.json();
        legacy["feather"] = 1.;
        QCOMPARE(Settings::fromJson(legacy).feather, 1.);
        legacy.remove("feather");
        QCOMPARE(Settings::fromJson(legacy).feather, 0.);
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
    void backgroundContext() {
        const QRect crop(300, 200, 330, 440);
        const auto context = Engine::maskContext(crop, {1200, 1600}, 65, .1);
        QVERIFY(context.contains(crop));
        QVERIFY(qAbs(context.width() - 390) <= 1);
        QVERIFY(qAbs(context.height() - 520) <= 1);
        QVERIFY(qAbs((context.y() + .1 * context.height()) - (crop.y() + .1 * crop.height())) <= 1);
        const QRect source(0, 0, 1200, 1600);
        for (double zoom : {40., 65., 100.})
            for (double headroom : {0., .08, .25})
                for (const QRect frame : {QRect(0, 0, 330, 440), QRect(870, 1160, 330, 440), source}) {
                    const auto expanded = Engine::maskContext(frame, source.size(), zoom, headroom);
                    QVERIFY(source.contains(expanded));
                    QVERIFY(expanded.contains(frame));
                }
        // Asymmetric crop offsets and a scaled model mask must map to the same
        // source pixel centers, with no newly transparent strip at the border.
        cv::Mat mask(400, 300, CV_32F);
        for (int y = 0; y < mask.rows; ++y)
            for (int x = 0; x < mask.cols; ++x)
                mask.at<float>(y, x) = float(.01 * x + .001 * y);
        const auto mapped = Engine::cropMask(mask, {100, 100, 600, 800},
                                             {250, 300, 300, 400}, {150, 200});
        QVERIFY(qAbs(mapped.at<float>(0, 0) - .85f) < 1e-5);
        QVERIFY(qAbs(mapped.at<float>(199, 149) - 2.539f) < 1e-5);
        mask.setTo(1);
        const auto solid = Engine::cropMask(mask, source, crop, {360, 480});
        QCOMPARE(cv::norm(solid, cv::Mat(480, 360, CV_32F, cv::Scalar(1)), cv::NORM_INF), 0.);
    }
    void contextPreservesExport() {
        QTemporaryDir dir;
        QFile sample(dir.filePath("sample.png"));
        QVERIFY(sample.open(QIODevice::WriteOnly));
        QVERIFY(sample.write(Engine::samplePortrait()) > 0);
        sample.close();
        Settings s;
        s.cropZoom = 65;
        s.headroom = .12;
        s.format = "png";
        s.sharpenScreen = false;
        Engine engine;
        const auto original = engine.process(sample.fileName(), s);
        s.background = "fast";
        const auto removed = engine.process(sample.fileName(), s);
        QCOMPARE(removed.crop, original.crop);
        QCOMPARE(removed.outputSize, original.outputSize);
        QCOMPARE(removed.preview.convertToFormat(QImage::Format_RGB888),
                 original.preview.convertToFormat(QImage::Format_RGB888));
    }
    void lightweightMaskRefinement() {
        cv::Mat guide(480, 360, CV_32FC3, cv::Scalar(.8, .8, .8));
        // Solid regions remain solid, including all four image edges.
        for (const auto &[probability, expected] :
             {std::pair{.04f, 0.f}, std::pair{.96f, 1.f}, std::pair{.5f, .5f}}) {
            cv::Mat prediction(192, 192, CV_32F, cv::Scalar(probability));
            const auto refined = Models::refineFastMask(prediction, guide);
            QCOMPARE(refined.size(), guide.size());
            QVERIFY(cv::norm(refined, cv::Mat(guide.size(), CV_32F, cv::Scalar(expected)),
                             cv::NORM_INF) < 1e-5);
        }
        // Use the higher resolution source edge to sharpen a coarse prediction.
        guide.colRange(0, 180).setTo(cv::Scalar(.1, .1, .1));
        cv::Mat prediction(192, 192, CV_32F, cv::Scalar(.04));
        prediction.colRange(96, 192).setTo(.96);
        const auto refined = Models::refineFastMask(prediction, guide);
        QVERIFY(refined.at<float>(240, 179) < .1f);
        QVERIFY(refined.at<float>(240, 180) > .9f);
        double lo, hi;
        cv::minMaxLoc(refined, &lo, &hi);
        QCOMPARE(lo, 0.);
        QCOMPARE(hi, 1.);
    }
    void firmMaskBrushes() {
        QTemporaryDir dir;
        QImage image(360, 480, QImage::Format_RGB32);
        image.fill(QColor("#bb9977"));
        const auto path = dir.filePath("brush.png");
        QVERIFY(image.save(path));
        Settings s;
        s.autoCrop = false;
        s.background = "fast";
        s.format = "png";
        s.strokes = QJsonArray{
            QJsonObject{{"points", QJsonArray{QJsonArray{.5, .5}}}, {"radius", .2}, {"keep", true}},
            QJsonObject{{"points", QJsonArray{QJsonArray{.5, .5}}}, {"radius", .025}, {"keep", false}}};
        Engine engine;
        for (double feather : {0., 10.}) {
            s.feather = feather;
            const auto result = engine.process(path, s);
            QCOMPARE(result.mask.size(), result.outputSize);
            const int cx = (result.outputSize.width() - 1) / 2;
            const int cy = (result.outputSize.height() - 1) / 2;
            const int radius = int(.025 * result.outputSize.width());
            // Fully removed right up to the brush edge, fully kept immediately outside it.
            QCOMPARE(result.preview.pixelColor(cx + radius, cy).alpha(), 0);
            QCOMPARE(result.preview.pixelColor(cx + radius + 1, cy).alpha(), 255);
            QCOMPARE(result.mask.pixelColor(cx + radius, cy).red(), 0);
            QCOMPARE(result.mask.pixelColor(cx + radius + 1, cy).red(), 255);
        }
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
