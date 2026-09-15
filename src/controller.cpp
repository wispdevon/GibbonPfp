#include "controller.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QtConcurrent>

using namespace gibbon;
static QString thumbnailKey(const QString &path) {
    return "thumb" +
           QString::fromLatin1(
               QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QImage ImageStore::requestImage(const QString &id, QSize *size, const QSize &requested) {
    QMutexLocker lock(&mutex);
    auto image = images.value(id.section('?', 0, 0));
    if (image.isNull() && id.startsWith("thumb")) {
        image = QImage(96, 128, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
    }
    if (size)
        *size = image.size();
    return requested.isValid()
               ? image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation)
               : image;
}
void ImageStore::put(const QString &id, const QImage &i) {
    QMutexLocker lock(&mutex);
    images[id] = i;
}
Controller::Controller(ImageStore *store, QObject *parent) : QObject(parent), images(store) {
    pool.setMaxThreadCount(1);
    const int savedScale = QSettings().value("uiScale", 100).toInt();
    if (QList<int>{80, 90, 100, 110, 125, 150}.contains(savedScale))
        interfaceScale = savedScale;
    if (QSettings().value("buttonAccent", "graphite").toString() == "blue")
        accentName = "blue";
    darkTheme =
        QSettings()
            .value("dark", QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
            .toBool();
}
Controller::~Controller() {
    cancelled = true;
    pool.waitForDone();
}
void Controller::setDark(bool d) {
    darkTheme = d;
    QSettings().setValue("dark", d);
    emit changed();
}
void Controller::setUiScale(int value) {
    if (!QList<int>{80, 90, 100, 110, 125, 150}.contains(value) || value == interfaceScale)
        return;
    interfaceScale = value;
    QSettings().setValue("uiScale", value);
    emit appearanceChanged();
}
void Controller::setButtonAccent(const QString &value) {
    if ((value != "graphite" && value != "blue") || value == accentName)
        return;
    accentName = value;
    QSettings().setValue("buttonAccent", value);
    emit appearanceChanged();
}
QVariantList Controller::items() const {
    QVariantList list;
    for (int i = 0; i < queue.size(); ++i) {
        auto &q = queue[i];
        list << QVariantMap{
            {"name", QFileInfo(q.path).fileName()},
            {"path", q.path},
            {"state", q.state},
            {"selected", q.selected},
            {"error", q.error},
            {"warnings", q.warnings},
            {"thumb", QString("image://photos/%1?%2").arg(thumbnailKey(q.path)).arg(generation)}};
    }
    return list;
}
QVariantMap Controller::settings() const {
    return index >= 0 ? queue[index].settings.json().toVariantMap()
                      : Settings{}.json().toVariantMap();
}
void Controller::setCurrent(int i) {
    if (working || i < 0 || i >= queue.size() || i == index)
        return;
    index = i;
    details.clear();
    emit changed();
    preview();
}
void Controller::add(const QList<QUrl> &urls, bool recursive) {
    if (working)
        return;
    QStringList paths;
    for (auto &u : urls)
        paths << u.toLocalFile();
    for (auto &p : expandInputs(paths, recursive)) {
        bool exists = false;
        for (auto &q : queue)
            if (q.path == p)
                exists = true;
        if (!exists)
            queue.push_back({p});
    }
    if (index < 0 && !queue.empty())
        index = 0;
    status = QString("%1 photos · originals stay untouched").arg(queue.size());
    emit changed();
    if (index >= 0)
        preview();
}
void Controller::loadSample() {
    if (working)
        return;
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!QDir().mkpath(directory)) {
        fail("Could not create the sample image directory");
        return;
    }
    const auto path = directory + "/gibbon-sample.png";
    QSaveFile file(path);
    const auto data = Engine::samplePortrait();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        fail("Could not save the sample image");
        return;
    }
    for (int i = 0; i < queue.size(); ++i)
        if (queue[i].path == path) {
            setCurrent(i);
            return;
        }
    const int row = queue.size();
    queue.push_back({path});
    setCurrent(row);
}
void Controller::remember() {
    if (index >= 0) {
        auto &q = queue[index];
        q.history.push_back(q.settings);
        if (q.history.size() > 50)
            q.history.removeFirst();
        q.settings.approved = false;
        q.state = "Edited";
    }
}
void Controller::set(const QString &key, const QVariant &value) {
    if (working || index < 0)
        return;
    try {
        auto j = queue[index].settings.json();
        j[key] = QJsonValue::fromVariant(value);
        auto s = Settings::fromJson(j);
        if (QStringList{"crop", "cropZoom", "rotation", "autoCrop", "headroom"}.contains(key))
            s.strokes = {};
        if (QStringList{"rotation", "autoCrop", "headroom"}.contains(key) ||
            (key == "crop" && s.crop.isNull())) {
            s.cropBasis = {};
            s.crop = {};
        }
        if (key == "cropZoom") {
            // Recalculate an automatic frame in the engine, with the real head anchor.
            // For a manually moved frame, use its current headroom anchor instead.
            if (!s.crop.isNull() && !s.cropBasis.isEmpty() && !details.isEmpty())
                s.crop = Engine::zoomCrop(
                    s.cropBasis,
                    QPointF(details["cropX"].toDouble() + details["cropW"].toDouble() / 2,
                            details["cropY"].toDouble() + s.headroom * details["cropH"].toDouble()),
                    s.cropZoom, s.headroom);
        }
        remember();
        s.approved = false;
        queue[index].settings = s;
        emit changed();
    } catch (const std::exception &e) {
        fail(errorText(e));
    }
}
void Controller::setCrop(double x, double y, double w, double h) {
    set("crop", QVariantList{x, y, w, h});
    preview();
}
void Controller::setCropZoom(double percent) {
    if (working || index < 0 || details.isEmpty())
        return;
    set("cropZoom", percent);
    preview();
}
void Controller::nudgeCrop(double dx, double dy) {
    if (working || index < 0 || details.isEmpty())
        return;
    auto w = details["cropW"].toDouble(), h = details["cropH"].toDouble();
    setCrop(std::clamp(details["cropX"].toDouble() + dx, 0., 1. - w),
            std::clamp(details["cropY"].toDouble() + dy, 0., 1. - h), w, h);
}
void Controller::setSize(int width, int height) {
    if (working || index < 0)
        return;
    try {
        auto j = queue[index].settings.json();
        j["width"] = width;
        j["height"] = height;
        auto s = Settings::fromJson(j);
        remember();
        s.approved = false;
        queue[index].settings = s;
        emit changed();
    } catch (const std::exception &e) {
        fail(errorText(e));
    }
}
void Controller::stroke(const QVariantList &points, bool keep, double radius) {
    if (index < 0 || working)
        return;
    auto a = queue[index].settings.strokes;
    a.append(QJsonObject{
        {"points", QJsonArray::fromVariantList(points)}, {"keep", keep}, {"radius", radius}});
    set("strokes", a.toVariantList());
    preview();
}
void Controller::undo() {
    if (working || index < 0 || queue[index].history.empty())
        return;
    queue[index].settings = queue[index].history.takeLast();
    emit changed();
    preview();
}
void Controller::reset() {
    if (working || index < 0)
        return;
    remember();
    queue[index].settings = Settings{};
    emit changed();
    preview();
}
void Controller::select(int row, bool selected) {
    if (working || row < 0 || row >= queue.size())
        return;
    queue[row].selected = selected;
    emit changed();
}
void Controller::applySelected() {
    if (working || index < 0)
        return;
    auto s = queue[index].settings;
    for (int i = 0; i < queue.size(); ++i)
        if (i != index && queue[i].selected) {
            auto &q = queue[i];
            q.history.push_back(q.settings);
            auto crop = q.settings.crop;
            auto basis = q.settings.cropBasis;
            auto zoom = q.settings.cropZoom;
            q.settings = s;
            q.settings.crop = crop;
            q.settings.cropBasis = basis;
            q.settings.cropZoom = zoom;
            q.settings.strokes = {};
            q.settings.approved = false;
            q.state = "Edited";
        }
    status = "Settings applied to selected photos; individual crops retained";
    emit changed();
}
void Controller::approve() {
    if (working || index < 0 || details.isEmpty() || queue[index].state == "Failed")
        return;
    queue[index].settings.approved = true;
    queue[index].state = "Ready";
    details["review"] = false;
    status = "Current framing approved";
    emit changed();
}
void Controller::removeSelected() {
    if (working)
        return;
    for (int i = queue.size() - 1; i >= 0; --i)
        if (queue[i].selected)
            queue.removeAt(i);
    index = queue.empty() ? -1 : 0;
    details.clear();
    emit changed();
    if (index >= 0)
        preview();
}
void Controller::fail(const QString &e) {
    status = e;
    emit changed();
}
void Controller::showResult(const Result &r, int row) {
    auto &q = queue[row];
    q.settings.cropBasis = r.cropBasis;
    q.warnings = r.warnings;
    q.error.clear();
    q.state = r.review ? "Needs review" : "Ready";
    images->put(thumbnailKey(q.path),
                r.preview.scaled(96, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (row == index) {
        images->put("source", r.source);
        images->put("output", r.preview);
        images->put("mask", r.mask);
        details = {{"width", r.outputSize.width()},
                   {"height", r.outputSize.height()},
                   {"sourceWidth", r.sourceSize.width()},
                   {"sourceHeight", r.sourceSize.height()},
                   {"cropX", r.crop.x()},
                   {"cropY", r.crop.y()},
                   {"cropW", r.crop.width()},
                   {"cropH", r.crop.height()},
                   {"cropZoom", q.settings.cropZoom},
                   {"review", r.review},
                   {"warnings", r.warnings.join(" · ")},
                   {"bytes", r.encodedBytes},
                   {"brightness", r.brightness}};
    }
    ++generation;
}
bool Controller::allowHighQuality(bool needed, std::function<void()> resume) {
    if (!needed || engine.highQualityLoaded() || qualityConsent)
        return true;
    pendingQualityAction = std::move(resume);
    working = true; // Freeze the pending operation's photo/settings until resolved.
    status = "Confirm loading High Quality into memory";
    emit changed();
    emit highQualityConfirmationRequested();
    return false;
}
void Controller::confirmHighQuality(bool accept) {
    if (!pendingQualityAction)
        return;
    auto resume = std::move(pendingQualityAction);
    pendingQualityAction = {};
    working = false;
    if (accept) {
        qualityConsent = true;
        resume();
    } else {
        status = "High Quality load cancelled · preview unchanged; choose Fast or Off to continue "
                 "without it";
        emit changed();
    }
}
void Controller::preview() {
    if (working || index < 0)
        return;
    if (!allowHighQuality(queue[index].settings.background == "quality", [this] { preview(); }))
        return;
    working = true;
    cancelled = false;
    status = "Preparing portrait…";
    emit changed();
    int row = index;
    auto item = queue[row];
    struct Work {
        Result result;
        QString error;
    };
    auto *w = new QFutureWatcher<Work>(this);
    connect(w, &QFutureWatcher<Work>::finished, this, [this, w, row] {
        auto work = w->result();
        working = false;
        qualityConsent = false;
        if (work.error.isEmpty()) {
            showResult(work.result, row);
            status = work.result.review ? "Review suggested · adjust the crop or approve it"
                                        : "Preview ready · matches encoded export";
        } else {
            queue[row].state = "Failed";
            queue[row].error = work.error;
            status = work.error;
        }
        w->deleteLater();
        emit changed();
    });
    w->setFuture(QtConcurrent::run(&pool, [this, item] {
        Work work;
        try {
            work.result = engine.process(item.path, item.settings, &cancelled);
        } catch (const std::exception &e) {
            work.error = errorText(e);
        }
        return work;
    }));
}
void Controller::batch(const QUrl &dir, bool exportFiles) {
    QVector<int> rows;
    for (int i = 0; i < queue.size(); ++i)
        if (queue[i].selected)
            rows << i;
    processMany(rows, dir.toLocalFile(), exportFiles);
}
void Controller::exportCurrent(const QUrl &dir) {
    if (index >= 0)
        processMany({index}, dir.toLocalFile(), true);
}
void Controller::processMany(QVector<int> rows, QString directory, bool exportFiles) {
    if (working || rows.empty())
        return;
    if (exportFiles && directory.isEmpty()) {
        fail("Choose an output folder");
        return;
    }
    const bool needsQuality = std::any_of(rows.cbegin(), rows.cend(), [this](int row) {
        return queue[row].settings.background == "quality";
    });
    if (!allowHighQuality(needsQuality, [this, rows, directory, exportFiles] {
            processMany(rows, directory, exportFiles);
        }))
        return;
    working = true;
    cancelled = false;
    auto snapshot = queue;
    status = "Processing selected photos…";
    emit changed();
    auto *w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcher<QString>::finished, this, [this, w] {
        working = false;
        status = w->result();
        qualityConsent = false;
        w->deleteLater();
        emit changed();
    });
    w->setFuture(QtConcurrent::run(&pool, [this, rows, snapshot, directory, exportFiles] {
        int done = 0, held = 0, failed = 0;
        QJsonArray report;
        for (int row : rows) {
            if (cancelled)
                break;
            const auto &q = snapshot[row];
            try {
                auto r = engine.process(q.path, q.settings, &cancelled);
                QString output;
                if (r.review)
                    ++held;
                else {
                    if (exportFiles)
                        output = Engine::save(r, q.path, directory, q.settings);
                    ++done;
                }
                report.append(QJsonObject{{"source", q.path},
                                          {"output", output},
                                          {"status", r.review      ? "review"
                                                     : exportFiles ? "exported"
                                                                   : "ready"},
                                          {"warnings", QJsonArray::fromStringList(r.warnings)}});
                r.encoded.clear();
                r.preview =
                    r.preview.scaled(1400, 1400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QMetaObject::invokeMethod(
                    this,
                    [this, r, row, output] {
                        showResult(r, row);
                        if (!output.isEmpty())
                            queue[row].state = "Exported";
                        status = QString("Processed %1").arg(QFileInfo(queue[row].path).fileName());
                        emit changed();
                    },
                    Qt::QueuedConnection);
            } catch (const std::exception &e) {
                if (cancelled)
                    break;
                ++failed;
                auto error = errorText(e);
                report.append(
                    QJsonObject{{"source", q.path}, {"status", "failed"}, {"error", error}});
                QMetaObject::invokeMethod(
                    this,
                    [this, row, error] {
                        queue[row].state = "Failed";
                        queue[row].error = error;
                        emit changed();
                    },
                    Qt::QueuedConnection);
            }
        }
        if (exportFiles) {
            QString reportError;
            QDir().mkpath(directory);
            QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch());
            QFile f(QDir(directory).filePath("gibbon-report-" + stamp + ".json"));
            if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
                f.write(QJsonDocument(report).toJson()) < 0)
                reportError = " · report could not be written";
            return QString("%1 complete · %2 need review · %3 failed%4%5")
                .arg(done)
                .arg(held)
                .arg(failed)
                .arg(cancelled ? " · cancelled" : "")
                .arg(reportError);
        }
        return QString("%1 ready · %2 need review · %3 failed%4")
            .arg(done)
            .arg(held)
            .arg(failed)
            .arg(cancelled ? " · cancelled" : "");
    }));
}
static void writeJson(const QUrl &url, const QJsonObject &j) {
    QSaveFile f(url.toLocalFile());
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(j).toJson()) < 0 || !f.commit())
        throw std::runtime_error("Cannot save file");
}
static QJsonObject readJson(const QUrl &url) {
    QFile f(url.toLocalFile());
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot open file");
    QJsonParseError error;
    auto d = QJsonDocument::fromJson(f.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !d.isObject())
        throw std::runtime_error("Invalid JSON document");
    return d.object();
}
void Controller::saveSession(const QUrl &u) {
    try {
        QJsonArray a;
        for (auto &q : queue)
            a.append(QJsonObject{
                {"source", q.path}, {"settings", q.settings.json()}, {"selected", q.selected}});
        writeJson(u, {{"version", 1}, {"photos", a}});
        status = "Session saved";
        emit changed();
    } catch (const std::exception &e) {
        fail(errorText(e));
    }
}
void Controller::loadSession(const QUrl &u) {
    if (working)
        return;
    try {
        auto j = readJson(u);
        if (j["version"].toInt() != 1 || !j["photos"].isArray())
            throw std::runtime_error("Unsupported session format");
        QVector<Item> loaded;
        for (auto v : j["photos"].toArray()) {
            auto o = v.toObject();
            Item q;
            q.path = o["source"].toString();
            q.settings = Settings::fromJson(o["settings"].toObject());
            q.selected = o["selected"].toBool(true);
            loaded << q;
        }
        queue = loaded;
        index = queue.empty() ? -1 : 0;
        details.clear();
        emit changed();
        preview();
    } catch (const std::exception &e) {
        fail(errorText(e));
    }
}
void Controller::savePreset(const QUrl &u) {
    if (index < 0)
        return;
    try {
        auto s = queue[index].settings;
        s.crop = {};
        s.cropBasis = {};
        s.strokes = {};
        s.approved = false;
        s.reference.clear();
        writeJson(u, s.json());
        status = "Preset saved";
        emit changed();
    } catch (const std::exception &e) {
        fail(errorText(e));
    }
}
void Controller::loadPreset(const QUrl &u) {
    if (working || index < 0)
        return;
    try {
        auto s = Settings::fromJson(readJson(u));
        remember();
        queue[index].settings = s;
        emit changed();
        preview();
    } catch (const std::exception &e) {
        fail(errorText(e));
    }
}
QString Controller::modelDescription() const {
    QStringList lines;
    try {
        for (auto v : Models::manifest()) {
            auto e = v.toObject();
            lines << QString("%1 — %2 MB")
                         .arg(e["id"].toString())
                         .arg(e["bytes"].toDouble() / 1000000, 0, 'f', 1);
        }
    } catch (const std::exception &e) {
        return errorText(e);
    }
    return lines.join('\n');
}
void Controller::installModel(const QString &id) {
    if (working)
        return;
    working = true;
    cancelled = false;
    status = "Downloading " + id + " model…";
    emit changed();
    auto *w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcher<QString>::finished, this, [this, w] {
        working = false;
        status = w->result();
        qualityConsent = false;
        w->deleteLater();
        emit changed();
    });
    w->setFuture(QtConcurrent::run(&pool, [this, id] {
        try {
            Models::install(id, &cancelled, [this](qint64 done, qint64 total) {
                QMetaObject::invokeMethod(
                    this,
                    [this, done, total] {
                        status = QString("Model download · %1 / %2 MB")
                                     .arg(done / 1000000)
                                     .arg(total / 1000000);
                        emit changed();
                    },
                    Qt::QueuedConnection);
            });
            return QString("Model installed and verified");
        } catch (const std::exception &e) {
            return errorText(e);
        }
    }));
}
