#include "models.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace gibbon {
static QStringList roots() {
    QStringList r;
    if (!qEnvironmentVariable("GIBBON_MODEL_DIR").isEmpty())
        r << qEnvironmentVariable("GIBBON_MODEL_DIR");
    r << QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/models"
      << QCoreApplication::applicationDirPath() + "/models"
      << QCoreApplication::applicationDirPath() + "/../share/gibbonpfp/models"
      << QCoreApplication::applicationDirPath() + "/../Resources/models";
    if (!qEnvironmentVariableIsSet("GIBBON_DISABLE_SOURCE_MODELS"))
        r << QString::fromUtf8(GIBBON_SOURCE_MODELS);
    return r;
}
QJsonArray Models::manifest() {
    for (const auto &r : roots()) {
        QFile f(r + "/manifest.json");
        if (f.open(QIODevice::ReadOnly))
            return QJsonDocument::fromJson(f.readAll()).object()["models"].toArray();
    }
    throw std::runtime_error("Model manifest missing. Reinstall GibbonPfp.");
}
static QJsonObject entry(const QString &id) {
    for (const auto &e : Models::manifest())
        if (e.toObject()["id"] == id)
            return e.toObject();
    throw std::runtime_error("Unknown model identifier");
}
QString Models::locate(const QString &id) {
    auto e = entry(id);
    for (const auto &r : roots()) {
        QFile f(r + "/" + e["file"].toString());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&f);
        if (hash.result().toHex() == e["sha256"].toString().toLatin1())
            return f.fileName();
    }
    throw std::runtime_error(
        ("Model '" + id +
         "' is missing or damaged. Install it from Models or run: gibbonpfp models install " + id)
            .toStdString());
}
QString Models::install(const QString &id, std::atomic_bool *cancel,
                        std::function<void(qint64, qint64)> progress) {
    auto e = entry(id);
    QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/models";
    if (!QDir().mkpath(dir))
        throw std::runtime_error("Cannot create model directory");
    QString path = dir + "/" + e["file"].toString();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(file.errorString().toStdString());
    QNetworkAccessManager network;
    QNetworkRequest request(QUrl(e["url"].toString()));
    request.setTransferTimeout(120000);
    auto *reply = network.get(request);
    QEventLoop loop;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    bool writeFailed = false;
    auto drain = [&] {
        auto data = reply->readAll();
        hash.addData(data);
        if (file.write(data) != data.size()) {
            writeFailed = true;
            reply->abort();
        }
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, drain);
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [&](qint64 a, qint64 b) {
        if (progress)
            progress(a, b);
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (cancel && *cancel)
            reply->abort();
    });
    timer.start(100);
    loop.exec();
    drain();
    if (writeFailed || reply->error() != QNetworkReply::NoError)
        throw std::runtime_error(("Model download failed: " + reply->errorString()).toStdString());
    if (hash.result().toHex() != e["sha256"].toString().toLatin1())
        throw std::runtime_error("Model checksum mismatch; download discarded");
    if (!file.commit())
        throw std::runtime_error("Cannot save model");
    return path;
}
static void checkModelCancel(std::atomic_bool *cancel) {
    if (cancel && cancel->load()) throw std::runtime_error("Cancelled");
}
QByteArray Models::cacheKey(const cv::Mat &rgb, const QString &id, const QString &purpose) {
    session(id); // Resolve the actual device before looking up compatible results.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto identity = id + ":" + purpose + ":preprocessing-v1:" +
        checksums.at(id) + ":" + (cudaSessions.contains(id) ? "cuda" : "cpu") +
        ":" + QString::number(rgb.cols) + ":" + QString::number(rgb.rows) + ":" + QString::number(rgb.type());
    hash.addData(identity.toUtf8());
    for (int y = 0; y < rgb.rows; ++y)
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(rgb.ptr(y)), rgb.cols * rgb.elemSize()));
    return hash.result();
}
void Models::release() {
    sessions.clear();
    cache.clear();
    cudaSessions.clear();
    cpuFallback.clear();
    checksums.clear();
    qualityLoaded.store(false);
    std::lock_guard lock(statusMutex);
    devices.clear();
    diagnostics.clear();
}
void Models::setPreference(const QString &value) {
    if (value != "automatic" && value != "cpu") throw std::runtime_error("Invalid processing device");
    if (preference == value) return;
    release();
    preference = value;
}
QString Models::inferenceDetails() const {
    std::lock_guard lock(statusMutex);
    QStringList lines;
    for (const auto &[id, message] : diagnostics) lines << id + ": " + message;
    return lines.isEmpty() ? "No runtime errors reported." : lines.join("\n");
}
QString Models::inferenceStatus() const {
    std::lock_guard lock(statusMutex);
    QStringList lines;
    for (const auto &[id, device] : devices)
        lines << (id == "face" ? "Face" : id == "fast" ? "Fast" : "High Quality") + QString(": ") + device;
    return lines.isEmpty() ? (cpuRequested()
                                 ? "CPU requested · no models loaded"
                                 : "Automatic GPU selection · no models loaded") : lines.join("\n");
}
void Models::setDevice(const QString &id, const QString &device) {
    std::lock_guard lock(statusMutex);
    devices[id] = device;
}
Ort::Session &Models::session(const QString &id) {
    if (!sessions.contains(id)) {
        const auto path = locate(id);
        checksums[id] = entry(id)["sha256"].toString();
        auto create = [&](bool cuda) {
            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(2);
            options.SetInterOpNumThreads(1);
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            if (cuda) {
#ifdef __linux__
                // Some distribution providers dynamically resolve cuDNN without
                // declaring it in DT_NEEDED. Preload its exported symbols first.
                static QLibrary cudnn(QStringLiteral("libcudnn.so.9"));
                static std::once_flag preload;
                std::call_once(preload, [] {
                    cudnn.setLoadHints(QLibrary::ExportExternalSymbolsHint | QLibrary::PreventUnloadHint);
                    cudnn.load();
                });
#endif
                OrtCUDAProviderOptions provider;
                // Avoid expensive exhaustive convolution searches during first use.
                provider.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchHeuristic;
                options.AppendExecutionProvider_CUDA(provider);
            }
#ifdef _WIN32
            return std::make_unique<Ort::Session>(env, path.toStdWString().c_str(), options);
#else
            return std::make_unique<Ort::Session>(env, path.toUtf8().constData(), options);
#endif
        };
        const auto providers = Ort::GetAvailableProviders();
        const bool tryCuda = !cpuRequested() &&
                             !cpuFallback.contains(id) &&
                             std::find(providers.begin(), providers.end(), "CUDAExecutionProvider") != providers.end();
        if (tryCuda) {
            try {
                sessions[id] = create(true);
                cudaSessions.insert(id);
                setDevice(id, "NVIDIA GPU (CUDA; unsupported operations use CPU)");
            } catch (const Ort::Exception &e) {
                qWarning() << "CUDA initialization failed for" << id << e.what();
                cpuFallback.insert(id);
                std::lock_guard lock(statusMutex);
                diagnostics[id] = QString::fromUtf8(e.what());
            }
        }
        if (!sessions.contains(id)) {
            sessions[id] = create(false);
            setDevice(id, cpuFallback.contains(id) ? "CPU (GPU initialization or execution failed)" :
                          cpuRequested() ? "CPU" : "CPU (CUDA provider unavailable in this runtime)");
        }
        if (id == "quality")
            qualityLoaded.store(true);
    }
    return *sessions.at(id);
}
std::vector<Ort::Value> Models::run(const QString &id, const cv::Mat &rgb, int size,
                                    const cv::Vec3f &mean, const cv::Vec3f &scale) {
    auto &s = session(id);
    cv::Mat resized;
    cv::resize(rgb, resized, {size, size}, 0, 0, cv::INTER_AREA);
    std::vector<float> input(3 * size * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            for (int c = 0; c < 3; ++c)
                input[c * size * size + y * size + x] =
                    (resized.at<cv::Vec3f>(y, x)[c] - mean[c]) * scale[c];
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<int64_t, 4> shape{1, 3, size, size};
    auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(),
                                                  shape.size());
    Ort::AllocatorWithDefaultOptions allocator;
    auto inName = s.GetInputNameAllocated(0, allocator);
    const char *in = inName.get();
    std::vector<Ort::AllocatedStringPtr> names;
    std::vector<const char *> outs;
    for (size_t i = 0; i < s.GetOutputCount(); ++i) {
        names.push_back(s.GetOutputNameAllocated(i, allocator));
        outs.push_back(names.back().get());
    }
    try {
        return s.Run(Ort::RunOptions{nullptr}, &in, &tensor, 1, outs.data(), outs.size());
    } catch (const Ort::Exception &e) {
        if (!cudaSessions.contains(id) || e.GetOrtErrorCode() == ORT_INVALID_ARGUMENT)
            throw;
        qWarning() << "CUDA inference failed; retrying on CPU for" << id << e.what();
        { std::lock_guard lock(statusMutex); diagnostics[id] = QString::fromUtf8(e.what()); }
        cudaSessions.erase(id);
        cpuFallback.insert(id);
        sessions.erase(id);
        if (id == "quality")
            qualityLoaded.store(false);
        setDevice(id, "Retrying on CPU after GPU failure");
        return run(id, rgb, size, mean, scale);
    }
}
std::vector<cv::Rect> Models::faces(const cv::Mat &rgb, std::atomic_bool *cancel, ProcessingTrace *trace) {
    const auto previous = trace ? trace->state() : Progress{};
    const auto restore = qScopeGuard([&] { if (trace) trace->stage(previous.stage, previous.detail); });
    if (trace) trace->stage(sessions.contains("face") ? "Cache lookup" : "Model loading", "face");
    checkModelCancel(cancel);
    const auto key = cacheKey(rgb, "face", "detection");
    if (auto cached = cache.get(key)) {
        checkModelCancel(cancel);
        if (trace) trace->stage("Cached detection", "face", true);
        return std::get<std::vector<cv::Rect>>(*cached);
    }
    if (trace) trace->stage("Inference", "face");
    // YuNet 2023: fixed 640x640, BGR 0..255, outputs cls/obj/bbox/kps at strides 8,16,32.
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    // Preserve face proportions for landscape and portrait camera originals.
    const float scale = 640.f / std::max(rgb.cols, rgb.rows);
    cv::Mat resized;
    cv::resize(bgr, resized,
               {std::max(1, int(std::round(rgb.cols * scale))),
                std::max(1, int(std::round(rgb.rows * scale)))},
               0, 0, cv::INTER_AREA);
    cv::Mat padded(640, 640, CV_32FC3, cv::Scalar(0, 0, 0));
    resized.copyTo(padded(cv::Rect(0, 0, resized.cols, resized.rows)));
    auto outputs = run("face", padded, 640, {0, 0, 0}, {255, 255, 255});
    auto &s = session("face");
    Ort::AllocatorWithDefaultOptions allocator;
    std::map<std::string, const float *> out;
    for (size_t i = 0; i < outputs.size(); ++i)
        out[s.GetOutputNameAllocated(i, allocator).get()] = outputs[i].GetTensorData<float>();
    struct Candidate {
        cv::Rect2f rect;
        float score;
    };
    std::vector<Candidate> candidates;
    for (int stride : {8, 16, 32}) {
        std::string suffix = std::to_string(stride);
        auto cls = out.at("cls_" + suffix);
        auto obj = out.at("obj_" + suffix);
        auto box = out.at("bbox_" + suffix);
        int cols = 640 / stride;
        for (int i = 0; i < cols * cols; ++i) {
            float score = std::sqrt(std::clamp(cls[i], 0.f, 1.f) * std::clamp(obj[i], 0.f, 1.f));
            if (score < .8f)
                continue;
            float cx = (i % cols + box[4 * i]) * stride, cy = (i / cols + box[4 * i + 1]) * stride;
            float w = std::exp(box[4 * i + 2]) * stride, h = std::exp(box[4 * i + 3]) * stride;
            candidates.push_back(
                {{(cx - w / 2) / scale, (cy - h / 2) / scale, w / scale, h / scale}, score});
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](auto &a, auto &b) { return a.score > b.score; });
    std::vector<cv::Rect> result;
    for (auto &c : candidates) {
        bool duplicate = false;
        for (auto &r : result) {
            float intersection = (c.rect & cv::Rect2f(r)).area();
            if (intersection / (c.rect.area() + r.area() - intersection) > .3f)
                duplicate = true;
        }
        if (!duplicate) {
            cv::Rect r = c.rect;
            r &= cv::Rect(0, 0, rgb.cols, rgb.rows);
            if (r.area() > 0)
                result.push_back(r);
        }
    }
    std::sort(result.begin(), result.end(), [](auto &a, auto &b) { return a.area() > b.area(); });
    checkModelCancel(cancel);
    cache.put(cacheKey(rgb, "face", "detection"), result);
    return result;
}
cv::Mat Models::refineFastMask(const cv::Mat &prediction, const cv::Mat &rgb) {
    // Guided reconstruction at crop resolution. Replicated borders avoid inventing
    // transparent pixels where a foreground region meets the image boundary.
    cv::Mat guide, probability;
    cv::cvtColor(rgb, guide, cv::COLOR_RGB2GRAY);
    cv::resize(prediction, probability, rgb.size(), 0, 0, cv::INTER_LINEAR);
    const int radius = std::max(2, int(std::ceil(double(std::max(rgb.cols, rgb.rows)) / std::max(prediction.cols, prediction.rows))) * 2);
    auto mean = [radius](const cv::Mat &input) {
        cv::Mat output;
        cv::boxFilter(input, output, CV_32F, {radius * 2 + 1, radius * 2 + 1},
                      {-1, -1}, true, cv::BORDER_REPLICATE);
        return output;
    };
    auto meanGuide = mean(guide), meanProbability = mean(probability);
    cv::Mat variance = mean(guide.mul(guide)) - meanGuide.mul(meanGuide);
    cv::max(variance, 0, variance);
    cv::Mat covariance = mean(guide.mul(probability)) - meanGuide.mul(meanProbability);
    cv::Mat a;
    cv::divide(covariance, variance + 1e-4f, a);
    cv::Mat b = meanProbability - a.mul(meanGuide);
    cv::Mat refined = mean(a).mul(guide) + mean(b);
    // Remove low-confidence residue in clear background / solid foreground.
    // Keep a continuous transition for uncertain boundary pixels.
    refined = (refined - .1f) / .8f;
    cv::max(refined, 0, refined);
    cv::min(refined, 1, refined);
    return refined;
}
cv::Mat Models::mask(const cv::Mat &rgb, const QString &method, const QString &purpose,
                     std::atomic_bool *cancel, ProcessingTrace *trace) {
    const auto previous = trace ? trace->state() : Progress{};
    const auto restore = qScopeGuard([&] { if (trace) trace->stage(previous.stage, previous.detail); });
    const auto detail = (purpose + " · " + method).toStdString();
    if (trace) trace->stage(sessions.contains(method) ? "Cache lookup" : "Model loading", detail);
    checkModelCancel(cancel);
    const auto key = cacheKey(rgb, method, purpose);
    if (auto cached = cache.get(key)) {
        checkModelCancel(cancel);
        if (trace) trace->stage("Cached mask", detail, true);
        return std::get<cv::Mat>(*cached);
    }
    if (trace) trace->stage("Inference", detail);
    bool fast = method == "fast";
    auto outputs =
        fast ? run("fast", rgb, 512, {.5f, .5f, .5f}, {2, 2, 2})
             : run("quality", rgb, 1024, {.485f, .456f, .406f}, {1 / .229f, 1 / .224f, 1 / .225f});
    if (trace) trace->stage("Mask refinement", detail);
    auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 4)
        throw std::runtime_error("Unexpected segmentation output shape");
    int h = int(shape[2]), w = int(shape[3]);
    const float *p = outputs[0].GetTensorData<float>();
    cv::Mat result(h, w, CV_32F);
    for (int i = 0; i < h * w; ++i) {
        float value = fast ? p[i] : 1.f / (1.f + std::exp(-std::clamp(p[i], -80.f, 80.f)));
        result.ptr<float>()[i] = std::clamp(value, 0.f, 1.f);
    }
    if (!fast) {
        double lo, hi;
        cv::minMaxLoc(result, &lo, &hi);
        if (hi - lo > 1e-6)
            result = (result - lo) / (hi - lo);
    }
    if (fast)
        result = refineFastMask(result, rgb);
    else
        cv::resize(result, result, rgb.size(), 0, 0, cv::INTER_LINEAR);
    checkModelCancel(cancel);
    cache.put(cacheKey(rgb, method, purpose), result);
    return result;
}
} // namespace gibbon
