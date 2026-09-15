#pragma once
#include "engine.h"
#include <QMutex>
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
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int uiScale READ uiScale WRITE setUiScale NOTIFY appearanceChanged)
    Q_PROPERTY(
        QString buttonAccent READ buttonAccent WRITE setButtonAccent NOTIFY appearanceChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY changed)
  public:
    explicit Controller(ImageStore *store, QObject *parent = nullptr);
    ~Controller() override;
    QVariantList items() const;
    QVariantMap settings() const;
    QVariantMap result() const {
        return details;
    }
    int current() const {
        return index;
    }
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
    Q_INVOKABLE void applySelected();
    Q_INVOKABLE void approve();
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void batch(const QUrl &directory, bool exportFiles);
    Q_INVOKABLE void exportCurrent(const QUrl &directory);
    Q_INVOKABLE void cancel() {
        cancelled = true;
    }
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
    void appearanceChanged();

  private:
    struct Item {
        QString path, state = "Imported", error;
        bool selected = true;
        gibbon::Settings settings;
        QVector<gibbon::Settings> history;
        QStringList warnings;
    };
    QVector<Item> queue;
    int index = -1, generation = 0;
    int interfaceScale = 100;
    QString accentName = "graphite";
    bool working = false, darkTheme = false;
    QString status = "Add portraits to get started";
    QVariantMap details;
    ImageStore *images;
    QThreadPool pool;
    std::atomic_bool cancelled{false};
    gibbon::Engine engine;
    void remember();
    void fail(const QString &error);
    void showResult(const gibbon::Result &r, int row);
    void processMany(QVector<int> rows, QString directory, bool exportFiles);
};
