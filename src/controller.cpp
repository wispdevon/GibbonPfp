#include "controller.h"
#include "zip_export.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryFile>
#include <QtConcurrent>
#include <algorithm>

using namespace gibbon;
static QVariantMap outputStyle(const Settings &settings) {
    return {{"format", settings.format}, {"quality", settings.quality},
            {"backgroundColor", settings.backgroundColor.name()}, {"capped", settings.capped},
            {"width", settings.width}, {"height", settings.height}, {"prefix", settings.prefix},
            {"sharpenScreen", settings.sharpenScreen}, {"sharpening", settings.sharpening}};
}
static Settings readOutputStyle(const QVariantMap &style) {
    auto fields = outputStyle(Settings{});
    for (auto field = fields.begin(); field != fields.end(); ++field) {
        auto value = style.constFind(field.key());
        if (value != style.constEnd())
            field.value() = value.value();
    }
    return Settings::fromJson(QJsonObject::fromVariantMap(fields));
}
static void applyOutputStyle(Settings &settings, const Settings &style) {
    settings.format = style.format;
    settings.quality = style.quality;
    settings.backgroundColor = style.backgroundColor;
    settings.capped = style.capped;
    settings.width = style.width;
    settings.height = style.height;
    settings.prefix = style.prefix;
    settings.sharpenScreen = style.sharpenScreen;
    settings.sharpening = style.sharpening;
}
static QString thumbnailKey(const QString &path, const QString &fingerprint) {
    return "thumb" +
           QString::fromLatin1(
               QCryptographicHash::hash(path.toUtf8() + '\0' + fingerprint.toLatin1(),
                                        QCryptographicHash::Sha256).toHex());
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
bool ImageStore::contains(const QString &id) {
    QMutexLocker lock(&mutex);
    return images.contains(id);
}
Controller::Controller(ImageStore *store, QObject *parent, const QString &recoveryDirectory, bool enableRecovery) : QObject(parent), images(store) {
    progressTimer.setInterval(100);
    connect(&progressTimer, &QTimer::timeout, this, &Controller::progressChanged);
    pool.setMaxThreadCount(1);
    exportHistory = QSettings().value("exportHistory").toMap();
    if (QSettings().value("processingDevice").toString() == "cpu") devicePreference = "cpu";
    engine.setDevice(devicePreference);
    const int savedScale = QSettings().value("uiScale", 100).toInt();
    if (QList<int>{80, 90, 100, 110, 125, 150}.contains(savedScale))
        interfaceScale = savedScale;
    if (QSettings().value("buttonAccent", "graphite").toString() == "blue")
        accentName = "blue";
    darkTheme =
        QSettings()
            .value("dark", QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
            .toBool();
    if (enableRecovery) initializeRecovery(recoveryDirectory);
}
Controller::~Controller() {
    cancelled = true;
    pool.waitForDone();
    autosaveRecovery();
}
void Controller::beginProgress() {
    ++operation;
    stageText = "Preparing";
    progressPhoto = 0;
    progressTotal = 0;
    progressClock.start();
    progressTimer.start();
    emit progressChanged();
}
void Controller::endProgress() {
    ++operation; // Invalidate any queued worker notification.
    progressTimer.stop();
    stageText.clear();
    emit progressChanged();
}
QString Controller::processingProgress() const {
    if (!progressTimer.isActive()) return {};
    const QString prefix = progressTotal > 1 ? QString("Photo %1 of %2 · ").arg(progressPhoto).arg(progressTotal) : QString();
    return prefix + stageText + QString(" · %1 s").arg(progressClock.elapsed() / 1000., 0, 'f', 1) +
        (cancelled ? " · Cancellation pending; waiting for the current stage" : "");
}
void Controller::acceptProgress(quint64 token, int photo, int total, const Progress &progress) {
    if (!working || token != operation || !progressTimer.isActive()) return;
    progressPhoto = photo;
    progressTotal = total;
    stageText = QString::fromStdString(progress.stage);
    if (!progress.detail.empty()) stageText += " · " + QString::fromStdString(progress.detail);
    emit progressChanged();
}
ProgressCallback Controller::progressCallback(quint64 token, int photo, int total) {
    return [this, token, photo, total](const Progress &progress) {
        QMetaObject::invokeMethod(this, [this, token, photo, total, progress] {
            acceptProgress(token, photo, total, progress);
        }, Qt::QueuedConnection);
    };
}
void Controller::cancel() {
    if (qualityConfirmationPending()) confirmHighQuality(false);
    else { cancelled = true; emit progressChanged(); }
}
void Controller::releaseModels() {
    if (working) return;
    engine.releaseModels();
    qualityConsent = false;
    status = "Loaded models and processing caches released · preview retained";
    emit changed();
}
void Controller::setProcessingDevice(const QString &value) {
    if (working || value == devicePreference || (value != "automatic" && value != "cpu")) return;
    engine.setDevice(value);
    devicePreference = value;
    qualityConsent = false;
    QSettings().setValue("processingDevice", value);
    status = "Processing device changed · models and caches released; preview retained";
    emit changed();
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
            {"row", i},
            {"name", QFileInfo(q.path).fileName()},
            {"path", q.path},
            {"state", q.state},
            {"selected", q.selected},
            {"error", q.error},
            {"warnings", q.warnings},
            {"thumb", QString("image://photos/%1?%2")
                          .arg(thumbnailKey(q.path, q.fingerprint)).arg(generation)}};
    }
    return list;
}
int Controller::selectedCount() const {
    return int(std::count_if(queue.cbegin(), queue.cend(), [](const Item &item) {
        return item.selected;
    }));
}
QVariantMap Controller::settings() const {
    return index >= 0 ? queue[index].settings.json().toVariantMap()
                      : Settings{}.json().toVariantMap();
}
void Controller::setCurrent(int i) {
    if (working || i < 0 || i >= queue.size() || i == index)
        return;
    presetOutputPending = false;
    index = i;
    details.clear();
    emit changed();
    preview();
}
void Controller::add(const QList<QUrl> &urls, bool recursive) {
    if (recoveryOffered) { deferredImports.append(urls); deferredRecursive |= recursive; return; }
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
        if (!exists) {
            Item item; item.settings = importDefaults; item.path = p; item.fingerprint = sourceFingerprint(p);
            queue.push_back(item);
        }
    }
    if (index < 0 && !queue.empty())
        index = 0;
    status = QString("%1 photos · originals stay untouched").arg(queue.size());
    emit changed();
    if (index >= 0)
        preview();
}
void Controller::loadSample() {
    if (working || recoveryOffered)
        return;
    const auto cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cachePath.isEmpty() || !QDir().mkpath(cachePath)) {
        fail("Could not create the sample image directory");
        return;
    }
    const QDir directory(cachePath);
    struct Sample {
        Item photo;
        QString resource;
        int row = -1;
        bool cached = false;
    };
    QList<Sample> samples;
    const auto cacheError = [this](const QFileInfo &file) {
        fail(QString("Could not create or verify the cached sample image %1").arg(file.fileName()));
    };
    // Check all sources before changing the cache or queue. Keep the existing
    // filenames so old sessions resolve to the same originals.
    for (const auto *resource : Engine::samplePortraitResources) {
        const QFileInfo file(directory.filePath(QFileInfo(resource).fileName()));
        Sample sample;
        sample.resource = resource;
        sample.photo.path = file.filePath();
        sample.photo.fingerprint = sourceFingerprint(sample.resource);
        sample.photo.settings = importDefaults;
        sample.cached = file.exists() || file.isSymLink();
        const auto existing = std::find_if(queue.cbegin(), queue.cend(), [&](const Item &item) {
            return item.path == sample.photo.path;
        });
        if (sample.photo.fingerprint.isEmpty() ||
            (sample.cached &&
             (!file.isFile() || sourceFingerprint(sample.photo.path) != sample.photo.fingerprint)) ||
            (existing != queue.cend() && !existing->fingerprint.isEmpty() &&
             existing->fingerprint != sample.photo.fingerprint)) {
            cacheError(file);
            return;
        }
        if (existing != queue.cend())
            sample.row = int(existing - queue.cbegin());
        samples.append(std::move(sample));
    }
    for (const auto &sample : samples) {
        if (sample.cached)
            continue;
        // QFile::copy protects a destination created by another instance.
        QFile::copy(sample.resource, sample.photo.path);
        const QFileInfo file(sample.photo.path);
        if (!file.isFile() || sourceFingerprint(sample.photo.path) != sample.photo.fingerprint) {
            cacheError(file);
            return;
        }
    }
    bool queueChanged = false;
    for (auto &sample : samples) {
        if (sample.row >= 0) {
            auto &item = queue[sample.row];
            if (item.fingerprint.isEmpty()) {
                item.fingerprint = sample.photo.fingerprint;
                if (item.state == "Missing") {
                    item.state = "Restored";
                    item.error.clear();
                }
                queueChanged = true;
            }
            continue;
        }
        sample.row = int(queue.size());
        queue.append(std::move(sample.photo));
        queueChanged = true;
    }
    const bool seeded = seedSampleThumbnails();
    const int firstRow = samples.front().row;
    if (index != firstRow)
        setCurrent(firstRow);
    else if (details.isEmpty())
        preview();
    else if (queueChanged || seeded)
        emit changed();
}
bool Controller::seedSampleThumbnails() {
    const auto cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cachePath.isEmpty())
        return false;
    const QDir directory(cachePath);
    bool seeded = false;
    for (const auto *resource : Engine::samplePortraitResources) {
        const auto path = directory.filePath(QFileInfo(resource).fileName());
        const auto item = std::find_if(queue.cbegin(), queue.cend(), [&](const Item &candidate) {
            return candidate.path == path;
        });
        if (item == queue.cend() || item->fingerprint.isEmpty())
            continue;
        const auto key = thumbnailKey(item->path, item->fingerprint);
        if (images->contains(key) || item->fingerprint != sourceFingerprint(resource))
            continue;
        QImageReader reader(resource);
        reader.setScaledSize(QSize(96, 128));
        const auto thumbnail = reader.read();
        if (!thumbnail.isNull()) {
            images->put(key, thumbnail);
            seeded = true;
        }
    }
    if (seeded)
        ++generation;
    return seeded;
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
    presetOutputPending = false;
    queue[index].settings = queue[index].history.takeLast();
    emit changed();
    preview();
}
void Controller::reset() {
    if (working || index < 0)
        return;
    presetOutputPending = false;
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
void Controller::applyAll() {
    if (working || index < 0) return;
    importDefaults = queue[index].settings;
    importDefaults.crop = {};
    importDefaults.cropBasis = {};
    importDefaults.strokes = {};
    importDefaults.approved = false;
    for (auto &q : queue) {
        q.history.push_back(q.settings);
        if (q.history.size() > 50) q.history.removeFirst();
        q.settings = importDefaults;
        q.state = "Edited";
        q.error.clear();
        q.warnings.clear();
    }
    details.clear();
    emit changed();
    preview();
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
    if (working || selectedCount() == 0)
        return;
    for (int i = queue.size() - 1; i >= 0; --i)
        if (queue[i].selected)
            queue.removeAt(i);
    presetOutputPending = false;
    index = queue.empty() ? -1 : 0;
    if (queue.empty()) importDefaults = Settings{};
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
    images->put(thumbnailKey(q.path, q.fingerprint),
                r.preview.scaled(96, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (row == index) {
        images->put("source", r.source);
        images->put("output", r.preview);
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
    if (working || recoveryOffered || index < 0)
        return;
    if (!allowHighQuality(queue[index].settings.background == "quality", [this] { preview(); }))
        return;
    working = true;
    cancelled = false;
    beginProgress();
    const auto token = operation;
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
        endProgress();
        qualityConsent = false;
        if (work.error.isEmpty()) {
            showResult(work.result, row);
            status = work.result.review ? "Review suggested · adjust the crop or approve it"
                                        : "Photo preview ready";
        } else {
            queue[row].state = "Failed";
            queue[row].error = work.error;
            status = work.error;
        }
        w->deleteLater();
        emit changed();
    });
    w->setFuture(QtConcurrent::run(&pool, [this, item, token] {
        Work work;
        try {
            work.result = engine.process(item.path, item.settings, &cancelled, progressCallback(token, 1, 1));
        } catch (const std::exception &e) {
            work.error = errorText(e);
        }
        return work;
    }));
}
void Controller::batch(const QUrl &dir, bool exportFiles) {
    if (working)
        return;
    if (exportFiles) {
        auto options = exportOptions(false);
        options["scope"] = "checked";
        options["mixed"] = true;
        startExportRequest(options, dir, false, true);
        return;
    }
    QVector<int> rows;
    for (int i = 0; i < queue.size(); ++i)
        if (queue[i].selected)
            rows << i;
    if (rows.isEmpty()) {
        fail("Select photos in the queue first");
        return;
    }
    processMany(rows);
}
void Controller::exportCurrent(const QUrl &dir) {
    if (index < 0) return;
    auto options = exportOptions(false);
    options["scope"] = "current";
    startExportRequest(options, dir, false, true);
}
void Controller::exportZip(const QUrl &destination) {
    auto options = exportOptions(false);
    options["scope"] = "zip";
    options["mixed"] = true;
    startExport(options, destination, true);
}
QString Controller::quickExportScope() const {
    const auto scope = exportHistory.value("scope").toString();
    if (scope == "current")
        return "Current photo";
    if (scope == "checked")
        return QString("%1 checked").arg(selectedCount());
    if (scope == "zip")
        return "Queue ZIP";
    return {};
}
QString Controller::quickExportDescription() const {
    if (exportHistory.isEmpty())
        return "Choose output settings and a destination for your first export.";
    const auto options = exportOptions(true);
    const auto notice = options.value("notice").toString();
    const auto scope = options.value("scope").toString();
    const auto target = scope == "zip" ? "the whole queue to a ZIP" :
                        scope == "checked" ? "checked photos, including any hidden by the filter" :
                        "the current photo";
    const auto path = QDir::toNativeSeparators(exportHistory.value("destination").toString());
    return QString("Export %1. %2%3").arg(target, path, notice.isEmpty() ? "" : "\n" + notice);
}
QVariantMap Controller::exportOptions(bool quick) const {
    const bool firstBatch = exportHistory.isEmpty() && selectedCount() > 1;
    auto scope = firstBatch ? QString("checked") :
        quick && !exportHistory.isEmpty() ? exportHistory.value("scope").toString() : QString("current");
    const auto rememberedStyle = exportHistory.value("style").toMap();
    const auto path = exportHistory.value("destination").toString();
    const QFileInfo destination(path);
    const QFileInfo zipParent(destination.absolutePath());
    const auto folder = exportHistory.value("folder").toString();
    QVariantMap style = outputStyle(index >= 0 ? queue[index].settings : importDefaults);
    bool mixed = firstBatch;
    QString reason;
    if (quick && !exportHistory.isEmpty()) {
        mixed = exportHistory.value("mixed").toBool();
        if (!mixed && !presetOutputPending && !rememberedStyle.isEmpty() &&
            exportStyleError(rememberedStyle).isEmpty())
            style = rememberedStyle;
        if (scope == "checked" && selectedCount() == 0)
            reason = "Check photos or choose another scope.";
        else if (scope == "zip" && (destination.exists() || destination.isSymLink()))
            reason = "Choose a ZIP destination. The previous archive already exists.";
        else if (scope == "zip" && (!zipParent.isDir() || !zipParent.isWritable()))
            reason = "The previous ZIP folder is no longer available.";
        else if ((scope == "checked" || scope == "current") && (!destination.isDir() || !destination.isWritable()))
            reason = "The previous export folder is no longer available.";
    }
    if (scope != "checked" && scope != "zip") scope = "current";
    return {{"scope", scope}, {"style", style}, {"mixed", mixed}, {"notice", reason},
            {"folder", QFileInfo(folder).isDir() ? QUrl::fromLocalFile(folder) : QUrl{}},
            {"zipFile", exportHistory.value("scope").toString() == "zip" && zipParent.isDir() ?
                            QUrl::fromLocalFile(path) : QUrl{}}};
}
QString Controller::exportStyleError(const QVariantMap &style) const {
    try {
        readOutputStyle(style);
        return {};
    } catch (const std::exception &e) {
        return errorText(e);
    }
}
bool Controller::archiveExists(const QUrl &destination) const {
    const QFileInfo info(destination.toLocalFile());
    return info.exists() || info.isSymLink();
}
bool Controller::quickExport() {
    if (working || index < 0 || exportHistory.isEmpty())
        return false;
    const auto options = exportOptions(true);
    if (!options.value("notice").toString().isEmpty())
        return false;
    return startExport(options, QUrl::fromLocalFile(exportHistory.value("destination").toString()));
}
bool Controller::startExport(const QVariantMap &options, const QUrl &destination, bool replaceArchive) {
    return startExportRequest(options, destination, replaceArchive, false);
}
bool Controller::startExportRequest(const QVariantMap &options, const QUrl &destination,
                                    bool replaceArchive, bool createFolder) {
    if (working || recoveryOffered || index < 0)
        return false;
    const auto scope = options.value("scope").toString();
    const bool zip = scope == "zip";
    QVector<int> rows;
    if (scope == "current")
        rows << index;
    else if (scope == "checked" || zip) {
        for (int i = 0; i < queue.size(); ++i)
            if (zip || queue[i].selected) rows << i;
    }
    if (rows.isEmpty()) {
        fail("Select photos in the queue first");
        return false;
    }
    const auto path = destination.toLocalFile();
    const QFileInfo info(path);
    const bool available = zip ? QFileInfo(info.absolutePath()).isDir() :
        info.isDir() || (createFolder && !info.exists() && !info.isSymLink());
    if (path.isEmpty() || !available) {
        fail(zip ? "Choose a ZIP destination in an existing folder" : "Choose an existing output folder");
        return false;
    }
    if (zip) {
        if (!checkSaveDestination(path))
            return false;
        if (info.isDir() || (!replaceArchive && (info.exists() || info.isSymLink()))) {
            fail("Choose another ZIP destination");
            return false;
        }
    }
    const bool mixed = scope != "current" && options.value("mixed").toBool();
    QVariantMap style;
    try {
        style = mixed ? outputStyle(queue[index].settings) :
                        outputStyle(readOutputStyle(options.value("style").toMap()));
    } catch (const std::exception &e) {
        fail(errorText(e));
        return false;
    }
    QVariantMap request = {{"scope", scope}, {"mixed", mixed}, {"style", style},
                           {"destination", info.absoluteFilePath()},
                           {"folder", zip ? exportHistory.value("folder") : info.absoluteFilePath()}};
    processMany(rows, request, replaceArchive, createFolder);
    return true;
}
void Controller::processMany(QVector<int> rows, const QVariantMap &exportRequest, bool replaceArchive,
                             bool createFolder) {
    if (working || rows.empty())
        return;
    const bool needsQuality = std::any_of(rows.cbegin(), rows.cend(), [this](int row) {
        return queue[row].settings.background == "quality";
    });
    if (!allowHighQuality(needsQuality, [=, this] {
            processMany(rows, exportRequest, replaceArchive, createFolder);
        }))
        return;
    const bool exportFiles = !exportRequest.isEmpty();
    const auto scope = exportRequest.value("scope").toString();
    const bool zip = scope == "zip";
    const auto directory = exportRequest.value("destination").toString();
    working = true;
    cancelled = false;
    auto snapshot = queue;
    const int unpreviewedRow = details.isEmpty() ? index : -1;
    beginProgress();
    const auto token = operation;
    status = zip ? "Exporting every queued photo to ZIP…" :
             scope == "current" ? "Exporting current photo…" :
             exportFiles ? "Exporting checked photos…" : "Preparing checked photos…";
    emit changed();
    struct Work {
        QString message;
        bool exported = false;
    };
    auto *w = new QFutureWatcher<Work>(this);
    connect(w, &QFutureWatcher<Work>::finished, this, [this, w, exportRequest] {
        working = false;
        endProgress();
        const auto result = w->result();
        status = result.message;
        if (result.exported && !exportRequest.isEmpty()) {
            exportHistory = exportRequest;
            QSettings().setValue("exportHistory", exportHistory);
            presetOutputPending = false;
        }
        qualityConsent = false;
        w->deleteLater();
        emit changed();
    });
    w->setFuture(QtConcurrent::run(&pool, [=, this]() -> Work {
      try {
        const bool sharedStyle = !exportRequest.isEmpty() && !exportRequest.value("mixed").toBool();
        const auto style = exportRequest.value("style").toMap();
        const auto sharedSettings = readOutputStyle(style);
        if (exportFiles && !zip && !createFolder && !QFileInfo(directory).isDir())
            throw std::runtime_error("The chosen output folder is no longer available");
        QTemporaryFile stagedArchive;
        std::unique_ptr<ZipExport> archive;
        if (zip) {
            if (!replaceArchive) {
                // Publish a new archive without replacing a path created during processing.
                stagedArchive.setFileTemplate(QFileInfo(directory).absoluteDir().filePath(".gibbon-XXXXXX"));
                if (!stagedArchive.open())
                    throw std::runtime_error(stagedArchive.errorString().toStdString());
                stagedArchive.close();
            }
            archive = std::make_unique<ZipExport>(replaceArchive ? directory : stagedArchive.fileName(), &cancelled);
        }
        int done = 0, held = 0, failed = 0;
        QJsonArray report;
        int photo = 0;
        for (int row : rows) {
            ++photo;
            if (cancelled)
                break;
            const auto &q = snapshot[row];
            try {
                auto settings = q.settings;
                const bool matchesPreview = !sharedStyle || style == outputStyle(settings);
                if (sharedStyle) applyOutputStyle(settings, sharedSettings);
                auto r = engine.process(q.path, settings, &cancelled, progressCallback(token, photo, rows.size()));
                QString output;
                if (r.review)
                    ++held;
                else {
                    if (exportFiles)
                        output = zip ? archive->addPhoto(q.path, settings.prefix, settings.format, r.encoded)
                                     : Engine::save(r, q.path, directory, settings, createFolder);
                    ++done;
                }
                report.append(QJsonObject{{"source", q.path},
                                          {"output", output},
                                          {"status", r.review      ? "review"
                                                     : exportFiles ? "exported"
                                                                   : "ready"},
                                          {"warnings", QJsonArray::fromStringList(r.warnings)},
                                          {"diagnostics", r.diagnostics()}});
                bool showPreview = matchesPreview;
                if (!matchesPreview && row == unpreviewedRow && !cancelled) {
                    // An export can finish preparation for a photo whose initial preview
                    // was cancelled. Render its editor settings so framing can be reviewed.
                    try {
                        r = engine.process(q.path, q.settings, &cancelled,
                                           progressCallback(token, photo, rows.size()));
                        showPreview = true;
                    } catch (const std::exception &) {
                        // The file or review result above is still valid.
                    }
                }
                r.encoded.clear();
                r.preview =
                    r.preview.scaled(1400, 1400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QMetaObject::invokeMethod(
                    this,
                    [this, r, row, output, token, zip, showPreview] {
                        if (token != operation) return;
                        if (showPreview)
                            showResult(r, row);
                        else {
                            auto &item = queue[row];
                            item.error.clear();
                            item.warnings = r.warnings;
                            item.state = r.review ? "Needs review" : "Ready";
                            if (row == index && !details.isEmpty()) {
                                details["review"] = r.review;
                                details["warnings"] = r.warnings.join(" · ");
                            }
                        }
                        if (!zip && !output.isEmpty())
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
                    [this, row, error, token] {
                        if (token != operation) return;
                        queue[row].state = "Failed";
                        queue[row].error = error;
                        emit changed();
                    },
                    Qt::QueuedConnection);
            }
        }
        if (zip) {
            if (cancelled || held || failed)
                return {QString("ZIP not saved · %1 need review · %2 failed%3 · destination unchanged")
                    .arg(held).arg(failed).arg(cancelled ? " · cancelled" : "")};
            progressCallback(token, rows.size(), rows.size())({"Writing ZIP", "Finalizing archive"});
            archive->commit(QJsonDocument(report).toJson());
            if (!replaceArchive && (cancelled || !QFile::rename(stagedArchive.fileName(), directory)))
                throw std::runtime_error(cancelled ? "Cancelled" : "Could not save ZIP to the chosen destination");
            QMetaObject::invokeMethod(this, [this, rows, token] {
                if (token != operation) return;
                for (int row : rows) queue[row].state = "Exported";
                emit changed();
            }, Qt::QueuedConnection);
            return {QString("ZIP saved · %1 photos · %2").arg(done).arg(directory), true};
        }
        if (exportFiles) {
            QString reportError;
            if (createFolder) QDir().mkpath(directory);
            QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch());
            QFile f(QDir(directory).filePath("gibbon-report-" + stamp + ".json"));
            const auto reportBytes = QJsonDocument(report).toJson();
            if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                reportError = " · report could not be written";
            } else if (f.write(reportBytes) != reportBytes.size() || !f.flush()) {
                f.close();
                f.remove();
                reportError = " · report could not be written";
            }
            return {QString("%1 complete · %2 need review · %3 failed%4%5")
                .arg(done)
                .arg(held)
                .arg(failed)
                .arg(cancelled ? " · cancelled" : "")
                .arg(reportError), done == rows.size() && !cancelled && reportError.isEmpty()};
        }
        return {QString("%1 ready · %2 need review · %3 failed%4")
            .arg(done)
            .arg(held)
            .arg(failed)
            .arg(cancelled ? " · cancelled" : "")};
      } catch (const std::exception &e) {
        return {QString("Export failed · %1 · destination unchanged").arg(errorText(e))};
      }
    }));
}
bool Controller::checkSaveDestination(const QString &destination) {
    const bool conflicts = std::any_of(queue.cbegin(), queue.cend(), [&](const Item &item) {
        return sameExistingFile(destination, item.path) ||
               sameExistingFile(destination, item.settings.reference);
    });
    if (conflicts || sameExistingFile(destination, importDefaults.reference)) {
        fail("Choose a destination other than a source photo or reference image");
        return false;
    }
    return true;
}
static void writeJson(const QString &path, const QJsonObject &j) {
    QSaveFile f(path);
    const auto bytes = QJsonDocument(j).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
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
    const auto path = u.toLocalFile();
    if (!checkSaveDestination(path))
        return;
    try {
        QJsonArray a;
        for (auto &q : queue)
            a.append(QJsonObject{
                {"source", q.path}, {"settings", q.settings.json()}, {"selected", q.selected}});
        writeJson(path, {{"version", 1}, {"photos", a}, {"importDefaults", importDefaults.json()}});
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
        if (j.contains("importDefaults") && !j["importDefaults"].isObject())
            throw std::runtime_error("Invalid queue import defaults");
        auto defaults = Settings::fromJson(j["importDefaults"].toObject());
        defaults.crop = {}; defaults.cropBasis = {}; defaults.strokes = {}; defaults.approved = false;
        QVector<Item> loaded;
        for (auto v : j["photos"].toArray()) {
            auto o = v.toObject();
            if (!v.isObject() || o["source"].toString().isEmpty() || !o["settings"].isObject() ||
                (o.contains("selected") && !o["selected"].isBool()))
                throw std::runtime_error("Invalid photo in session");
            Item q;
            q.path = o["source"].toString();
            q.fingerprint = sourceFingerprint(q.path);
            q.settings = Settings::fromJson(o["settings"].toObject());
            q.selected = o["selected"].toBool(true);
            loaded << q;
        }
        queue = loaded;
        seedSampleThumbnails();
        importDefaults = defaults;
        presetOutputPending = false;
        index = queue.empty() ? -1 : 0;
        details.clear();
        emit changed();
        preview();
    } catch (const std::exception &e) {
        fail(errorText(e));
    }
}
void Controller::savePreset(const QUrl &u) {
    saveExportPreset(u, {});
}
void Controller::saveExportPreset(const QUrl &u, const QVariantMap &style) {
    const auto path = u.toLocalFile();
    if (working || index < 0 || !checkSaveDestination(path))
        return;
    try {
        auto s = queue[index].settings;
        if (!style.isEmpty())
            applyOutputStyle(s, readOutputStyle(style));
        s.crop = {};
        s.cropBasis = {};
        s.strokes = {};
        s.approved = false;
        s.reference.clear();
        writeJson(path, s.json());
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
        presetOutputPending = true;
        remember();
        s.approved = false;
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
        endProgress();
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
