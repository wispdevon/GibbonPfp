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
};
class Engine {
  public:
    Result process(const QString &path, const Settings &settings,
                   std::atomic_bool *cancel = nullptr);
    static QRectF zoomCrop(QRectF basis, QPointF headAnchor, double percent, double headroom);
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
