#pragma once
#include "engine.h"
#include <QMutex>
#include <QLockFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QObject>
#include <QQuickImageProvider>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>

class ImageStore : public QQuickImageProvider {
  public:
    ImageStore() : QQuickImageProvider(Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
    void put(const QString &id, const QImage &image);

  private:
    QMutex mutex;
    QHash<QString, QImage> images;
};
class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recoveryPending READ recoveryPending NOTIFY recoveryChanged)
    Q_PROPERTY(bool recoveryRestorable READ recoveryRestorable NOTIFY recoveryChanged)
    Q_PROPERTY(QString recoveryMessage READ recoveryMessage NOTIFY recoveryChanged)
    Q_PROPERTY(QString autosaveError READ autosaveError NOTIFY recoveryChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY changed)
    Q_PROPERTY(QString processingDevice READ processingDevice WRITE setProcessingDevice NOTIFY changed)
    Q_PROPERTY(bool cpuOverride READ cpuOverride CONSTANT)
    Q_PROPERTY(QString inferenceDetails READ inferenceDetails NOTIFY changed)
    Q_PROPERTY(QString inferenceStatus READ inferenceStatus NOTIFY changed)
    Q_PROPERTY(bool highQualityLoaded READ highQualityLoaded NOTIFY changed)
    Q_PROPERTY(bool qualityConfirmationPending READ qualityConfirmationPending NOTIFY changed)
    Q_PROPERTY(QString processingProgress READ processingProgress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int uiScale READ uiScale WRITE setUiScale NOTIFY appearanceChanged)
    Q_PROPERTY(
        QString buttonAccent READ buttonAccent WRITE setButtonAccent NOTIFY appearanceChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY changed)
  public:
    explicit Controller(ImageStore *store, QObject *parent = nullptr, const QString &recoveryDirectory = {}, bool enableRecovery = true);
    ~Controller() override;
    bool recoveryPending() const { return recoveryOffered; }
    bool recoveryRestorable() const { return !recoverySnapshot.isEmpty(); }
    QString recoveryMessage() const { return recoveryNotice; }
    QString autosaveError() const { return recoveryError; }
    Q_INVOKABLE void resolveRecovery(bool restore);
    QVariantList items() const;
    QVariantMap settings() const;
    QVariantMap result() const {
        return details;
    }
    int current() const {
        return index;
    }
    QString processingDevice() const { return devicePreference; }
    bool cpuOverride() const { return qEnvironmentVariable("GIBBON_INFERENCE_DEVICE") == "cpu"; }
    QString inferenceDetails() const { return engine.inferenceDetails(); }
    void setProcessingDevice(const QString &value);
    Q_INVOKABLE void releaseModels();
    QString inferenceStatus() const { return engine.inferenceStatus(); }
    bool highQualityLoaded() const {
        return engine.highQualityLoaded();
    }
    bool qualityConfirmationPending() const {
        return bool(pendingQualityAction);
    }
    Q_INVOKABLE void confirmHighQuality(bool accept);
    QString processingProgress() const;
    bool busy() const {
        return working;
    }
    QString message() const {
        return status;
    }
    int revision() const {
        return generation;
    }
    bool dark() const {
        return darkTheme;
    }
    int uiScale() const {
        return interfaceScale;
    }
    QString buttonAccent() const {
        return accentName;
    }
    void setUiScale(int value);
    void setButtonAccent(const QString &value);
    void setDark(bool dark);
    void setCurrent(int value);
    Q_INVOKABLE void add(const QList<QUrl> &urls, bool recursive = false);
    Q_INVOKABLE void loadSample();
    Q_INVOKABLE void set(const QString &key, const QVariant &value);
    Q_INVOKABLE void setSize(int width, int height);
    Q_INVOKABLE void setReference(const QUrl &url) {
        set("reference", url.toLocalFile());
        preview();
    }
    Q_INVOKABLE void preview();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void select(int row, bool selected);
    Q_INVOKABLE void applyAll();
    Q_INVOKABLE void approve();
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void batch(const QUrl &directory, bool exportFiles);
    Q_INVOKABLE void exportZip(const QUrl &destination);
    Q_INVOKABLE void exportCurrent(const QUrl &directory);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void saveSession(const QUrl &url);
    Q_INVOKABLE void loadSession(const QUrl &url);
    Q_INVOKABLE void savePreset(const QUrl &url);
    Q_INVOKABLE void loadPreset(const QUrl &url);
    Q_INVOKABLE QString modelDescription() const;
    Q_INVOKABLE void installModel(const QString &id);
    Q_INVOKABLE void setCrop(double x, double y, double width, double height);
    Q_INVOKABLE void setCropZoom(double percent);
    Q_INVOKABLE void nudgeCrop(double dx, double dy);
    Q_INVOKABLE void stroke(const QVariantList &points, bool keep, double radius);
  signals:
    void changed();
    void progressChanged();
    void recoveryChanged();
    void appearanceChanged();
    void highQualityConfirmationRequested();

  private:
    struct Item {
        QString path, state = "Imported", error;
        QString fingerprint;
        bool selected = true;
        gibbon::Settings settings;
        QVector<gibbon::Settings> history;
        QStringList warnings;
    };
    QString recoveryPath, recoveryNotice, recoveryError;
    QJsonObject recoverySnapshot;
    QByteArray lastRecovery;
    QTimer recoveryTimer;
    std::unique_ptr<QLockFile> recoveryLock;
    bool recoveryOffered = false, repairRecovery = false;
    QList<QUrl> deferredImports;
    bool deferredRecursive = false;
    static QString sourceFingerprint(const QString &path);
    static QJsonObject validateRecovery(const QByteArray &bytes);
    QJsonObject recoveryJson() const;
    void initializeRecovery(const QString &directory);
    void autosaveRecovery();
    quint64 operation = 0;
    QTimer progressTimer;
    QElapsedTimer progressClock;
    QString stageText;
    int progressPhoto = 0, progressTotal = 0;
    void beginProgress();
    void endProgress();
    gibbon::ProgressCallback progressCallback(quint64 token, int photo, int total);
    void acceptProgress(quint64 token, int photo, int total, const gibbon::Progress &progress);
    friend class ControllerTests;
    QVector<Item> queue;
    gibbon::Settings importDefaults;
    int index = -1, generation = 0;
    int interfaceScale = 100;
    QString accentName = "graphite";
    QString devicePreference = "automatic";
    bool working = false, darkTheme = false;
    QString status = "Add portraits to get started";
    QVariantMap details;
    ImageStore *images;
    QThreadPool pool;
    std::atomic_bool cancelled{false};
    gibbon::Engine engine;
    std::function<void()> pendingQualityAction;
    bool qualityConsent = false;
    bool allowHighQuality(bool needed, std::function<void()> resume);
    void remember();
    void fail(const QString &error);
    void showResult(const gibbon::Result &r, int row);
    void processMany(QVector<int> rows, QString directory, bool exportFiles, bool zip = false);
};
