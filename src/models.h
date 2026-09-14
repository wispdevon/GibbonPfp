#pragma once
#include <QJsonArray>
#include <QString>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>

namespace gibbon {
class Models {
  public:
    static QJsonArray manifest();
    static QString locate(const QString &id);
    static QString install(const QString &id, std::atomic_bool *cancel = nullptr,
                           std::function<void(qint64, qint64)> progress = {});
    std::vector<cv::Rect> faces(const cv::Mat &rgb);
    cv::Mat mask(const cv::Mat &rgb, const QString &method);

  private:
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "gibbonpfp"};
    std::map<QString, std::unique_ptr<Ort::Session>> sessions;
    Ort::Session &session(const QString &id);
    std::vector<Ort::Value> run(const QString &id, const cv::Mat &rgb, int size,
                                const cv::Vec3f &mean, const cv::Vec3f &scale);
};
} // namespace gibbon
