#include "controller.h"
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTextStream>
#include <QTimer>
#include <csignal>

using namespace gibbon;
static std::atomic_bool interrupted{false};
static void interrupt(int) {
    interrupted = true;
}
static QJsonObject readSettings(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot open preset");
    QJsonParseError e;
    auto j = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !j.isObject())
        throw std::runtime_error("Invalid preset JSON");
    return j.object();
}
static void writeReport(const QString &path, const QJsonArray &report) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot open report destination");
    QByteArray data;
    if (path.endsWith(".csv", Qt::CaseInsensitive)) {
        data = "source,output,status,warnings,error\r\n";
        auto quote = [](QString s) {
            s.replace('"', "\"\"");
            return '"' + s + '"';
        };
        for (auto v : report) {
            auto o = v.toObject();
            QStringList row;
            for (auto key : {"source", "output", "status", "warnings", "error"}) {
                auto value = o[key];
                QString text = value.isArray() ? ([&] {
                    QStringList list;
                    for (auto v : value.toArray())
                        list << v.toString();
                    return list.join("; ");
                })()
                                               : value.toString();
                row << quote(text);
            }
            data += (row.join(',') + "\r\n").toUtf8();
        }
    } else
        data = QJsonDocument(report).toJson();
    if (f.write(data) != data.size() || !f.commit())
        throw std::runtime_error("Cannot write report");
}
int main(int argc, char **argv) {
    bool cli = argc > 1 && (QString::fromLocal8Bit(argv[1]) == "process" ||
                            QString::fromLocal8Bit(argv[1]) == "models" ||
                            QString::fromLocal8Bit(argv[1]) == "-h" ||
                            QString::fromLocal8Bit(argv[1]) == "-v" ||
                            QString::fromLocal8Bit(argv[1]).startsWith("--help") ||
                            QString::fromLocal8Bit(argv[1]) == "--version");
#ifdef Q_OS_WIN
    // Qt otherwise opens a help/version dialog when launched without an
    // attached console, including automation with inherited output handles.
    if (cli)
        qputenv("QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES", "1");
#endif
    // The workspace owns its theme. Avoid loading third-party Linux Qt theme
    // plugins built against a different Qt, especially in portable packages.
#ifdef Q_OS_LINUX
    if (!cli)
        qputenv("QT_QPA_PLATFORMTHEME", "generic");
#endif
    std::unique_ptr<QCoreApplication> application;
    if (cli)
        application = std::make_unique<QCoreApplication>(argc, argv);
    else
        application = std::make_unique<QGuiApplication>(argc, argv);
    auto &app = *application;
    app.setApplicationName("GibbonPfp");
    app.setOrganizationName("Devon Labs");
    app.setApplicationVersion("0.1.0");
    QCommandLineParser p;
    p.setApplicationDescription("Offline portrait preparation · Qt desktop and batch CLI");
    p.addHelpOption();
    p.addVersionOption();
    p.addPositionalArgument("command", "process, models, or image paths to open in the desktop app",
                            "[command]");
    p.addPositionalArgument("inputs", "Input photos or folders", "[inputs...]");
    p.addOptions({{{"o", "output"}, "Output folder", "directory"},
                  {"preset", "JSON settings preset", "file"},
                  {"size", "Exact 3:4 dimensions, e.g. 720x960", "WxH"},
                  {"uncapped", "Remove 360x480 limit; use source crop size unless --size is set"},
                  {"no-auto-crop", "Use centered 3:4 framing"},
                  {"fully-automatic", "Export uncertain crops using documented fallbacks"},
                  {"headroom", "Headroom percentage (0–25)", "percent"},
                  {"brightness", "Perceptual brightness (-1 to 1)", "amount"},
                  {"reference", "Reference portrait for brightness matching", "file"},
                  {"background", "off, fast, quality", "method"},
                  {"background-color", "JPEG backdrop, e.g. #ffffff", "color"},
                  {"format", "jpeg or png", "format"},
                  {"quality", "JPEG quality 1–100", "number"},
                  {"prefix", "Output filename prefix", "text"},
                  {"recursive", "Import nested folders"},
                  {"report", "JSON or CSV report path", "file"},
                  {"white-balance", "camera, daylight, cloudy, tungsten, custom", "preset"},
                  {"temperature", "RAW temperature 2000–12000", "kelvin"},
                  {"tint", "RAW green multiplier 0.5–2", "amount"},
                  {"highlight", "RAW highlight recovery 0–9", "level"},
                  {"rotate", "Clockwise rotation, multiple of 90", "degrees"},
                  {"smoke-test", "Open desktop and exit after startup"},
                  {"screenshot", "Save desktop capture after preview is ready", "file"}});
    p.process(app);
    auto args = p.positionalArguments();
    try {
        if (!args.empty() && args.front() == "models") {
            if (args.size() == 1 || (args.size() == 2 && args[1] == "list")) {
                QTextStream(stdout) << QJsonDocument(Models::manifest()).toJson();
                return 0;
            }
            if (args.size() == 3 && args[1] == "install") {
                std::signal(SIGINT, interrupt);
                QTextStream(stdout) << Models::install(args[2], &interrupted) << Qt::endl;
                return 0;
            }
            throw std::runtime_error("Usage: gibbonpfp models [list | install face|fast|quality]");
        }
        if (!args.empty() && args.front() == "process") {
            args.removeFirst();
            if (args.empty() || !p.isSet("output"))
                throw std::runtime_error("Usage: gibbonpfp process INPUT... --output DIRECTORY");
            auto s = p.isSet("preset") ? Settings::fromJson(readSettings(p.value("preset")))
                                       : Settings{};
            if (p.isSet("uncapped")) {
                s.capped = false;
                s.width = s.height = 0;
            }
            auto j = s.json();
            for (auto pair : {std::pair{"brightness", "brightness"},
                              {"headroom", "headroom"},
                              {"quality", "quality"},
                              {"temperature", "temperature"},
                              {"tint", "tint"},
                              {"highlight", "highlight"},
                              {"rotate", "rotation"}}) {
                if (p.isSet(pair.first)) {
                    bool ok;
                    double n = p.value(pair.first).toDouble(&ok);
                    if (!ok)
                        throw std::runtime_error("Invalid numeric option");
                    if (QString(pair.first) == "headroom")
                        n /= 100;
                    j[pair.second] = n;
                }
            }
            for (auto pair : {std::pair{"reference", "reference"},
                              {"background", "background"},
                              {"background-color", "backgroundColor"},
                              {"format", "format"},
                              {"prefix", "prefix"},
                              {"white-balance", "whiteBalance"}})
                if (p.isSet(pair.first))
                    j[pair.second] = p.value(pair.first);
            if (p.isSet("no-auto-crop"))
                j["autoCrop"] = false;
            if (p.isSet("fully-automatic"))
                j["automatic"] = true;
            if (p.isSet("size")) {
                auto parts = p.value("size").split('x');
                if (parts.size() != 2)
                    throw std::runtime_error("Size must be WIDTHxHEIGHT");
                bool a, b;
                int w = parts[0].toInt(&a), h = parts[1].toInt(&b);
                if (!a || !b)
                    throw std::runtime_error("Invalid size");
                j["width"] = w;
                j["height"] = h;
            }
            s = Settings::fromJson(j);
            auto files = expandInputs(args, p.isSet("recursive"));
            if (files.empty())
                throw std::runtime_error("No supported images found");
            Engine engine;
            QJsonArray report;
            int held = 0, failed = 0;
            std::signal(SIGINT, interrupt);
            std::signal(SIGTERM, interrupt);
            for (auto &file : files) {
                if (interrupted)
                    break;
                QJsonObject entry{{"source", file}};
                try {
                    auto r = engine.process(file, s, &interrupted);
                    entry["warnings"] = QJsonArray::fromStringList(r.warnings);
                    entry["width"] = r.outputSize.width();
                    entry["height"] = r.outputSize.height();
                    if (r.review) {
                        entry["status"] = "review";
                        ++held;
                    } else {
                        entry["output"] = Engine::save(r, file, p.value("output"), s);
                        entry["status"] = "exported";
                    }
                } catch (const std::exception &e) {
                    entry["status"] = "failed";
                    entry["error"] = errorText(e);
                    ++failed;
                }
                report.append(entry);
                QTextStream(stdout)
                    << QJsonDocument(entry).toJson(QJsonDocument::Compact) << Qt::endl;
            }
            if (p.isSet("report"))
                writeReport(p.value("report"), report);
            return interrupted ? 130 : (held || failed ? 2 : 0);
        }
        QFontDatabase::addApplicationFont(":/assets/fonts/Inter.ttf");
        QFontDatabase::addApplicationFont(":/assets/fonts/SpaceGrotesk.ttf");
        QFontDatabase::addApplicationFont(":/assets/fonts/GeistMono.ttf");
        QGuiApplication::setFont(QFont("Inter", 10));
        QGuiApplication::setWindowIcon(QIcon(":/assets/gibbonpfp.png"));
        QQuickStyle::setStyle("Fusion");
        QQmlApplicationEngine qml;
        auto *store = new ImageStore;
        qml.addImageProvider("photos", store);
        Controller controller(store);
        qml.rootContext()->setContextProperty("backend", &controller);
        qml.loadFromModule("Gibbon", "Main");
        if (qml.rootObjects().isEmpty())
            return 1;
        if (!args.empty()) {
            QList<QUrl> urls;
            for (auto &a : args)
                urls << QUrl::fromLocalFile(QFileInfo(a).absoluteFilePath());
            controller.add(urls);
        }
        QTimer smoke;
        int ticks = 0;
        if (p.isSet("smoke-test") || p.isSet("screenshot")) {
            QObject::connect(&smoke, &QTimer::timeout, &app, [&] {
                if ((controller.busy() && ++ticks < 240) || ticks++ < 2)
                    return;
                if (p.isSet("screenshot")) {
                    auto *w = qobject_cast<QQuickWindow *>(qml.rootObjects().front());
                    if (!w || !w->grabWindow().save(p.value("screenshot"))) {
                        app.exit(1);
                        return;
                    }
                }
                app.quit();
            });
            smoke.start(500);
        }
        const int exitCode = app.exec();
        // Destroy bindings while their controller still exists. The image
        // provider stays alive until the controller has joined its worker.
        qDeleteAll(qml.rootObjects());
        return exitCode;
    } catch (const std::exception &e) {
        QTextStream(stderr) << "GibbonPfp: " << errorText(e) << Qt::endl;
        return 1;
    }
}
