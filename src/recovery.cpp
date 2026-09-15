#include "controller.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

using namespace gibbon;
QString Controller::sourceFingerprint(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}
QJsonObject Controller::validateRecovery(const QByteArray &bytes) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    const auto object = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        object["version"].toInt() != 1 || object["kind"] != "gibbon-recovery" ||
        !object["photos"].isArray() || !object["active"].isDouble())
        throw std::runtime_error("Invalid recovery snapshot");
    const auto photos = object["photos"].toArray();
    int active = object["active"].toInt(-2);
    if (active < -1 || active >= photos.size() || (!photos.isEmpty() && active < 0))
        throw std::runtime_error("Invalid active photo in recovery snapshot");
    for (auto value : photos) {
        const auto p = value.toObject();
        if (!value.isObject() || p["source"].toString().isEmpty() || !p["settings"].isObject() ||
            !p["selected"].isBool() || !p["fingerprint"].isString())
            throw std::runtime_error("Invalid photo in recovery snapshot");
        Settings::fromJson(p["settings"].toObject());
    }
    return object;
}
QJsonObject Controller::recoveryJson() const {
    QJsonArray photos;
    for (const auto &q : queue)
        photos.append(QJsonObject{{"source", q.path}, {"fingerprint", q.fingerprint},
                                  {"settings", q.settings.json()}, {"selected", q.selected}});
    return {{"version", 1}, {"kind", "gibbon-recovery"}, {"active", index}, {"photos", photos}};
}
void Controller::initializeRecovery(const QString &directory) {
    // Ordinary unit tests must not read/write the user's recovery workspace.
    if (directory.isEmpty() && QStandardPaths::isTestModeEnabled()) return;
    recoveryPath = directory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery"
        : directory;
    if (!QDir().mkpath(recoveryPath)) {
        recoveryError = "Autosave unavailable: cannot create recovery directory. Save a session manually.";
        return;
    }
    recoveryLock = std::make_unique<QLockFile>(recoveryPath + "/workspace.lock");
    // Never expire a live process's lock based on elapsed time; dead-process locks
    // can still be removed by QLockFile's PID/hostname checks after a crash.
    recoveryLock->setStaleLockTime(0);
    if (!recoveryLock->tryLock(0))
        recoveryError = "Autosave unavailable: another instance owns recovery, or its lock cannot be acquired. Save a session manually.";
    bool found = false;
    for (const auto *name : {"current.json", "previous.json"}) {
        QFile file(recoveryPath + "/" + name);
        if (!file.exists()) continue;
        found = true;
        if (!file.open(QIODevice::ReadOnly)) continue;
        auto bytes = file.readAll();
        try {
            recoverySnapshot = validateRecovery(bytes);
            lastRecovery = QJsonDocument(recoverySnapshot).toJson(QJsonDocument::Compact);
            repairRecovery = QString(name) != "current.json";
            recoveryNotice = QString("Restore %1 %2 from %3? Photos are referenced at their original paths. High Quality will ask before loading.")
                .arg(recoverySnapshot["photos"].toArray().size())
                .arg(recoverySnapshot["photos"].toArray().size() == 1 ? "photo" : "photos")
                .arg(QString(name) == "current.json" ? "the last workspace" : "the previous valid snapshot (latest snapshot unavailable)");
            break;
        } catch (const std::exception &) { }
    }
    recoveryOffered = found;
    if (found && recoverySnapshot.isEmpty())
        recoveryNotice = "Recovery snapshots are unreadable or corrupt. Start fresh, or open a manually saved session. Existing files are retained until you choose Start fresh.";
    recoveryTimer.setInterval(1000);
    recoveryTimer.setSingleShot(true);
    connect(&recoveryTimer, &QTimer::timeout, this, &Controller::autosaveRecovery);
    connect(this, &Controller::changed, this, [this] {
        if (!recoveryOffered && recoveryLock && recoveryLock->isLocked()) recoveryTimer.start();
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &Controller::autosaveRecovery);
}
static void atomicWrite(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Cannot atomically save recovery snapshot");
}
void Controller::autosaveRecovery() {
    if (recoveryOffered || !recoveryLock || !recoveryLock->isLocked()) return;
    try {
        const auto bytes = QJsonDocument(recoveryJson()).toJson(QJsonDocument::Compact);
        if (bytes == lastRecovery && !repairRecovery) return;
        validateRecovery(bytes);
        // lastRecovery was fully validated, so a corrupt current file never
        // replaces the one valid previous snapshot.
        if (!lastRecovery.isEmpty()) atomicWrite(recoveryPath + "/previous.json", lastRecovery);
        atomicWrite(recoveryPath + "/current.json", bytes);
        lastRecovery = bytes;
        repairRecovery = false;
        recoveryError.clear();
        emit recoveryChanged();
    } catch (const std::exception &e) {
        recoveryError = "Autosave failed: " + errorText(e) + ". Edits remain in memory; save a session manually.";
        emit recoveryChanged();
    }
}
void Controller::resolveRecovery(bool restore) {
    if (!recoveryOffered || working || (restore && recoverySnapshot.isEmpty())) return;
    if (restore) {
        QVector<Item> restored;
        for (auto value : recoverySnapshot["photos"].toArray()) {
            const auto p = value.toObject();
            Item item;
            item.path = p["source"].toString();
            item.settings = Settings::fromJson(p["settings"].toObject());
            item.selected = p["selected"].toBool();
            item.fingerprint = sourceFingerprint(item.path);
            item.state = "Restored";
            if (item.fingerprint.isEmpty()) {
                item.settings.approved = false;
                item.state = "Missing";
                item.error = "Source photo is missing or unreadable: " + item.path;
            } else if (p["fingerprint"].toString().isEmpty() || item.fingerprint != p["fingerprint"].toString()) {
                item.settings.approved = false;
                item.state = "Needs review";
                item.error = "Source changed since the snapshot; framing approval was invalidated.";
            }
            restored.append(item);
        }
        queue = restored;
        index = recoverySnapshot["active"].toInt();
        details.clear();
        status = "Workspace restored · refresh a preview when ready";
    } else {
        queue.clear();
        index = -1;
        details.clear();
        status = "Started fresh · add portraits to get started";
    }
    qualityConsent = false;
    recoveryOffered = false;
    recoverySnapshot = {};
    emit recoveryChanged();
    emit changed();
    autosaveRecovery();
    if (!deferredImports.isEmpty()) {
        const auto urls = std::exchange(deferredImports, {});
        add(urls, std::exchange(deferredRecursive, false));
    }
}
