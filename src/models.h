#pragma once
#include "processing_cache.h"
#include <QJsonArray>
#include <QString>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>

namespace gibbon {
class Models {
  public:
    bool highQualityLoaded() const {
        return qualityLoaded.load();
    }
    QString inferenceStatus() const;
    QString inferenceDetails() const;
    void release(); // Caller must serialize with processing.
    void setPreference(const QString &value);
    bool cpuRequested() const { return preference == "cpu" || qEnvironmentVariable("GIBBON_INFERENCE_DEVICE") == "cpu"; }
    static QJsonArray manifest();
    static QString locate(const QString &id);
    static QString install(const QString &id, std::atomic_bool *cancel = nullptr,
                           std::function<void(qint64, qint64)> progress = {});
    std::vector<cv::Rect> faces(const cv::Mat &rgb, std::atomic_bool *cancel = nullptr);
    cv::Mat mask(const cv::Mat &rgb, const QString &method,
                 const QString &purpose = "export", std::atomic_bool *cancel = nullptr);
    void clearCache() { cache.clear(); }
    ProcessingCache::Stats cacheStats() const { return cache.statistics(); }
    static cv::Mat refineFastMask(const cv::Mat &prediction, const cv::Mat &rgb);

  private:
    QString preference = "automatic";
    std::map<QString, QString> diagnostics, checksums;
    ProcessingCache cache;
    QByteArray cacheKey(const cv::Mat &rgb, const QString &id, const QString &purpose);
    mutable std::mutex statusMutex;
    std::map<QString, QString> devices;
    std::set<QString> cpuFallback;
    std::set<QString> cudaSessions;
    void setDevice(const QString &id, const QString &device);
    std::atomic_bool qualityLoaded{false};
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "gibbonpfp"};
    std::map<QString, std::unique_ptr<Ort::Session>> sessions;
    Ort::Session &session(const QString &id);
    std::vector<Ort::Value> run(const QString &id, const cv::Mat &rgb, int size,
                                const cv::Vec3f &mean, const cv::Vec3f &scale);
};
} // namespace gibbon
