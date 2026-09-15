#include "engine.h"
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QPainter>
#include <QSaveFile>
#include <QSysInfo>
#include <QTextStream>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
using namespace gibbon;
static void writeJson(const QString &path, const QJsonObject &value) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(value).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Cannot save benchmark report");
}
static QJsonObject memory() {
    QJsonObject result{{"rssBytes", QJsonValue::Null}, {"rssMeasurement", "unavailable"},
                       {"peakBytes", QJsonValue::Null}, {"peakMeasurement", "unavailable"},
                       {"gpuBytes", QJsonValue::Null}, {"gpuMeasurement", "unavailable"}};
#ifdef Q_OS_LINUX
    QFile status("/proc/self/status");
    if (status.open(QIODevice::ReadOnly)) {
        for (auto line : status.readAll().split('\n')) if (line.startsWith("VmRSS:")) {
            result["rssBytes"] = line.simplified().split(' ').value(1).toDouble() * 1024;
            result["rssMeasurement"] = "sampled at run boundary; not a peak";
        }
    }
#endif
#ifdef Q_OS_UNIX
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef Q_OS_MACOS
        result["peakBytes"] = double(usage.ru_maxrss);
#else
        result["peakBytes"] = double(usage.ru_maxrss) * 1024;
#endif
        result["peakMeasurement"] = "process lifetime high-water mark; not per-image";
    }
#endif
    return result;
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("GibbonPfp");
    app.setOrganizationName("Devon Labs");
    try {
        const auto args = app.arguments();
        if (args.size() < 4 || args.size() > 5 || (args.size() == 5 && args[4] != "--quality"))
            throw std::runtime_error("Usage: gibbon_benchmark manifest.json photos-directory output-directory [--quality]");
        QFile manifest(args[1]);
        if (!manifest.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open manifest");
        auto specification = QJsonDocument::fromJson(manifest.readAll()).object();
        auto photos = specification["portraits"].toArray();
        if (photos.isEmpty()) throw std::runtime_error("No benchmark portraits");
        const QString mode = args.size() == 5 ? "quality" : "fast";
        QDir output(args[3]);
        if (!QDir().mkpath(output.path())) throw std::runtime_error("Cannot create output directory");
        QImage sheet(4 * 360, photos.size() * 530 + 40, QImage::Format_RGB32);
        sheet.fill(Qt::white);
        QPainter painter(&sheet);
        painter.setFont(QFont("sans-serif", 12));
        for (int col = 0; col < 4; ++col)
            painter.drawText(col * 360 + 8, 25, QStringList{"Source", "Mask", "Black", "White"}[col]);
        Engine engine;
        Settings settings;
        settings.background = mode;
        settings.format = "png";
        settings.cropZoom = 65;
        settings.feather = 0;
        QJsonArray reports;
        int row = 0;
        for (auto value : photos) {
            auto photo = value.toObject();
            const auto path = QDir(args[2]).filePath(photo["file"].toString());
            QFile source(path);
            if (!source.open(QIODevice::ReadOnly)) throw std::runtime_error("Missing benchmark photo; run download script");
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(&source);
            if (hash.result().toHex() != photo["sha256"].toString().toLatin1())
                throw std::runtime_error("Benchmark photo checksum mismatch");
            source.close();
            engine.releaseModels();
            QJsonArray runs;
            Result result;
            for (int run = 0; run < 4; ++run) {
                auto before = memory();
                result = engine.process(path, settings);
                auto diagnostic = result.diagnostics();
                diagnostic["run"] = run;
                diagnostic["state"] = run == 0 ? "cold sessions/cache; OS disk cache uncontrolled" : "warm";
                diagnostic["memoryBefore"] = before;
                diagnostic["memoryAfter"] = memory();
                runs.append(diagnostic);
                QTextStream(stdout) << photo["id"].toString() << " " << run << ": " << result.totalMs << " ms" << Qt::endl;
            }
            const QString id = photo["id"].toString();
            if (!result.mask.save(output.filePath(id + "-mask.png")) ||
                !result.preview.save(output.filePath(id + "-transparent.png")))
                throw std::runtime_error("Cannot save benchmark images");
            int y = 40 + row * 530;
            auto fitted = result.source.scaled(360, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter.drawImage(QPoint((360 - fitted.width()) / 2, y), fitted);
            painter.drawImage(QRect(360, y, 360, 480), result.mask);
            for (int col : {2, 3}) {
                QImage composite(360, 480, QImage::Format_RGB32);
                composite.fill(col == 2 ? Qt::black : Qt::white);
                { QPainter p(&composite); p.drawImage(QRect(0, 0, 360, 480), result.preview); }
                if (!composite.save(output.filePath(id + (col == 2 ? "-black.png" : "-white.png"))))
                    throw std::runtime_error("Cannot save composite");
                painter.drawImage(QPoint(col * 360, y), composite);
            }
            painter.drawText(QRect(8, y + 482, 1424, 46), Qt::TextWordWrap, photo["attribution"].toString());
            photo["runs"] = runs;
            photo["warnings"] = QJsonArray::fromStringList(result.warnings);
            reports.append(photo);
            ++row;
        }
        painter.end();
        if (!sheet.save(output.filePath("contact-sheet.png"))) throw std::runtime_error("Cannot save contact sheet");
        writeJson(output.filePath("report.json"), {{"version", 1}, {"build", GIBBON_BENCH_VERSION},
            {"platform", QSysInfo::prettyProductName()}, {"cpuArchitecture", QSysInfo::currentCpuArchitecture()},
            {"mode", mode}, {"settings", settings.json()}, {"removalContextZoom", 55},
            {"modelManifest", Models::manifest()}, {"portraits", reports},
            {"qualityAssessment", "Visual inspection required; no ground-truth masks or accuracy scores"}});
        return 0;
    } catch (const std::exception &e) {
        QTextStream(stderr) << e.what() << Qt::endl;
        return 1;
    }
}
