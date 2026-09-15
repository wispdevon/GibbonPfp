#pragma once
#include "models.h"
#include "settings.h"
#include <QImage>
#include <atomic>

namespace gibbon {
struct Result {
    QImage source, preview, mask;
    QByteArray encoded;
    qint64 encodedBytes = 0;
    QRectF crop, cropBasis;
    QStringList warnings;
    bool review = false;
    QSize sourceSize, outputSize;
    double brightness = 0;
    double totalMs = 0;
    std::vector<StageTiming> timings;
    QString deviceStatus;
    size_t cacheHits = 0, cacheMisses = 0;
    QJsonObject diagnostics() const;
};
class Engine {
  public:
    void releaseModels() { models.release(); }
    void setDevice(const QString &value) { models.setPreference(value); }
    QString inferenceDetails() const { return models.inferenceDetails(); }
    void clearProcessingCache() { models.clearCache(); }
    ProcessingCache::Stats cacheStats() const { return models.cacheStats(); }
    QString inferenceStatus() const { return models.inferenceStatus(); }
    bool highQualityLoaded() const {
        return models.highQualityLoaded();
    }
    Result process(const QString &path, const Settings &settings,
                   std::atomic_bool *cancel = nullptr, ProgressCallback progress = {});
    static QRectF zoomCrop(QRectF basis, QPointF headAnchor, double percent, double headroom);
    static QRect maskContext(QRect crop, QSize sourceSize, double zoom, double headroom);
    static cv::Mat cropMask(const cv::Mat &mask, QRect context, QRect crop, QSize outputSize);
    static cv::Mat sharpenForScreen(const cv::Mat &rgb, const cv::Mat &alpha, const QString &level,
                                    std::atomic_bool *cancel = nullptr);
    static QByteArray samplePortrait();
    static QImage decode(const QString &path, const Settings &settings);
    static QSize outputSize(QSize crop, const Settings &settings);
    static double tone(double lightness, double amount);
    static cv::Vec3f adjust(const cv::Vec3f &rgb, double amount);
    static QString save(const Result &result, const QString &source, const QString &directory,
                        const Settings &settings);

  private:
    Models models;
};
} // namespace gibbon
