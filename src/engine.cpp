#include "engine.h"
#include <QScopeGuard>
#include "codecs.h"
#include <QBuffer>
#include <QColorSpace>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <libheif/heif.h>
#include <libraw/libraw.h>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace gibbon {
static void checkCancel(std::atomic_bool *c) {
    if (c && *c)
        throw std::runtime_error("Cancelled");
}
static bool rawPath(QString p) {
    return !QStringList{"jpg", "jpeg", "png", "webp", "heic", "heif", "tif", "tiff"}.contains(
        QFileInfo(p).suffix().toLower());
}
static cv::Vec3f kelvin(double temperature) {
    double t = temperature / 100.;
    return {float(std::clamp(t <= 66 ? 255. : 329.698727446 * std::pow(t - 60, -.1332047592), 1.,
                             255.) /
                  255),
            float(std::clamp(t <= 66 ? 99.4708025861 * std::log(t) - 161.1195681661
                                     : 288.1221695283 * std::pow(t - 60, -.0755148492),
                             1., 255.) /
                  255),
            float(std::clamp(
                      t >= 66 ? 255.
                              : (t <= 19 ? 0. : 138.5177312231 * std::log(t - 10) - 305.0447927307),
                      1., 255.) /
                  255)};
}
QImage Engine::decode(const QString &path, const Settings &s) {
    QImage image;
    QString ext = QFileInfo(path).suffix().toLower();
    if (!supportedPath(path))
        throw std::runtime_error("Unsupported image format");
    if (rawPath(path)) {
        auto raw = std::make_unique<LibRaw>();
        raw->imgdata.params.use_camera_wb = 1;
        raw->imgdata.params.no_auto_bright = 1;
        raw->imgdata.params.output_bps = 16;
        raw->imgdata.params.output_color = 1;
        raw->imgdata.params.highlight = s.highlight;
        raw->imgdata.params.gamm[0] = 1 / 2.4;
        raw->imgdata.params.gamm[1] = 12.92;
#ifdef _WIN32
        int err = raw->open_file(path.toStdWString().c_str());
#else
        int err = raw->open_file(path.toUtf8().constData());
#endif
        if (err == LIBRAW_SUCCESS)
            err = raw->unpack();
        if (err == LIBRAW_SUCCESS && s.whiteBalance != "camera") {
            int t = s.whiteBalance == "daylight"   ? 5500
                    : s.whiteBalance == "cloudy"   ? 6500
                    : s.whiteBalance == "tungsten" ? 3200
                                                   : s.temperature;
            auto white = kelvin(t), base = kelvin(6500);
            for (int i = 0; i < 4; ++i) {
                int c = i == 3 ? 1 : i;
                double neutral = raw->imgdata.color.pre_mul[i] > 0 ? raw->imgdata.color.pre_mul[i]
                                                                   : raw->imgdata.color.pre_mul[c];
                raw->imgdata.params.user_mul[i] =
                    float(neutral * base[c] / white[c] * (c == 1 ? s.tint : 1));
            }
            raw->imgdata.params.use_camera_wb = 0;
        }
        if (err == LIBRAW_SUCCESS)
            err = raw->dcraw_process();
        if (err != LIBRAW_SUCCESS)
            throw std::runtime_error(
                (QString("RAW decode failed: ") + libraw_strerror(err)).toStdString());
        auto *data = raw->dcraw_make_mem_image(&err);
        if (!data || err != LIBRAW_SUCCESS || data->type != LIBRAW_IMAGE_BITMAP ||
            data->colors != 3 || data->bits != 16) {
            if (data)
                LibRaw::dcraw_clear_mem(data);
            throw std::runtime_error("Unsupported RAW development output");
        }
        image = QImage(data->width, data->height, QImage::Format_RGBX64);
        auto *source = reinterpret_cast<const quint16 *>(data->data);
        for (int y = 0; y < image.height(); ++y) {
            auto *row = reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                auto i = 3 * (y * image.width() + x);
                row[x] = QRgba64::fromRgba64(source[i], source[i + 1], source[i + 2], 65535);
            }
        }
        LibRaw::dcraw_clear_mem(data);
        image.setColorSpace(QColorSpace::SRgb);
    } else if (ext == "tiff" || ext == "tif") {
        image = decodeTiff(path);
    } else if (ext == "webp") {
        image = decodeWebp(path);
    } else if (ext == "heic" || ext == "heif") {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            throw std::runtime_error(f.errorString().toStdString());
        auto bytes = f.readAll();
        auto ctx = std::unique_ptr<heif_context, decltype(&heif_context_free)>(heif_context_alloc(),
                                                                               heif_context_free);
        auto error = heif_context_read_from_memory_without_copy(ctx.get(), bytes.constData(),
                                                                size_t(bytes.size()), nullptr);
        if (error.code != heif_error_Ok)
            throw std::runtime_error(error.message);
        heif_image_handle *handle = nullptr;
        error = heif_context_get_primary_image_handle(ctx.get(), &handle);
        if (error.code != heif_error_Ok)
            throw std::runtime_error(error.message);
        auto held = std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)>(
            handle, heif_image_handle_release);
        heif_image *decoded = nullptr;
        error = heif_decode_image(handle, &decoded, heif_colorspace_RGB,
                                  heif_chroma_interleaved_RGBA, nullptr);
        if (error.code != heif_error_Ok)
            throw std::runtime_error(error.message);
        auto holder =
            std::unique_ptr<heif_image, decltype(&heif_image_release)>(decoded, heif_image_release);
        int stride;
        auto plane = heif_image_get_plane_readonly(decoded, heif_channel_interleaved, &stride);
        image = QImage(plane, heif_image_get_width(decoded, heif_channel_interleaved),
                       heif_image_get_height(decoded, heif_channel_interleaved), stride,
                       QImage::Format_RGBA8888)
                    .copy();
        auto length = heif_image_handle_get_raw_color_profile_size(handle);
        if (length) {
            QByteArray icc(qsizetype(length), '\0');
            if (heif_image_handle_get_raw_color_profile(handle, icc.data()).code == heif_error_Ok)
                image.setColorSpace(QColorSpace::fromIccProfile(icc));
        } else {
            heif_color_profile_nclx *nclx = nullptr;
            if (heif_image_handle_get_nclx_color_profile(handle, &nclx).code == heif_error_Ok) {
                if (nclx->transfer_characteristics == 16 || nclx->transfer_characteristics == 18) {
                    heif_nclx_color_profile_free(nclx);
                    throw std::runtime_error("HDR HEIC requires SDR conversion before import");
                }
                if (nclx->color_primaries == 12)
                    image.setColorSpace(QColorSpace::DisplayP3);
                heif_nclx_color_profile_free(nclx);
            }
        }
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        image = reader.read();
        if (image.isNull())
            throw std::runtime_error(
                ("Image decode failed: " + reader.errorString()).toStdString());
    }
    if (image.isNull())
        throw std::runtime_error("Could not decode image");
    if (!image.colorSpace().isValid())
        image.setColorSpace(QColorSpace::SRgb);
    image = image.convertedToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA32FPx4);
    if (s.rotation % 360)
        image = image.transformed(QTransform().rotate(s.rotation));
    return image;
}
static cv::Mat rgbMat(const QImage &i) {
    cv::Mat rgba(i.height(), i.width(), CV_32FC4, const_cast<uchar *>(i.constBits()),
                 size_t(i.bytesPerLine()));
    cv::Mat rgb;
    cv::cvtColor(rgba, rgb, cv::COLOR_RGBA2RGB);
    return rgb;
}
static float linear(float x) {
    return x <= .04045f ? x / 12.92f : std::pow((x + .055f) / 1.055f, 2.4f);
}
static float encoded(float x) {
    return x <= .0031308f ? 12.92f * x : 1.055f * std::pow(x, 1 / 2.4f) - .055f;
}
static cv::Vec3f lab(const cv::Vec3f &r) {
    float R = linear(r[0]), G = linear(r[1]), B = linear(r[2]);
    float l = std::cbrt(.4122214708f * R + .5363325363f * G + .0514459929f * B),
          m = std::cbrt(.2119034982f * R + .6806995451f * G + .1073969566f * B),
          s = std::cbrt(.0883024619f * R + .2817188376f * G + .6299787005f * B);
    return {.2104542553f * l + .793617785f * m - .0040720468f * s,
            1.9779984951f * l - 2.428592205f * m + .4505937099f * s,
            .0259040371f * l + .7827717662f * m - .808675766f * s};
}
static cv::Vec3f unlab(cv::Vec3f v) {
    float l = v[0] + .3963377774f * v[1] + .2158037573f * v[2],
          m = v[0] - .1055613458f * v[1] - .0638541728f * v[2],
          s = v[0] - .0894841775f * v[1] - 1.291485548f * v[2];
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    return {4.0767416621f * l - 3.3077115913f * m + .2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - .3413193965f * s,
            -.0041960863f * l - .7034186147f * m + 1.707614701f * s};
}
double Engine::tone(double L, double amount) {
    L = std::clamp(L, 0., 1.);
    return L + amount * .45 * L * (1 - L);
}
cv::Vec3f Engine::adjust(const cv::Vec3f &rgb, double amount) {
    if (std::abs(amount) < 1e-8)
        return rgb;
    auto v = lab(rgb);
    v[0] = float(tone(v[0], amount));
    auto result = unlab(v);
    auto gamut = [](auto r) {
        return r[0] >= -1e-6 && r[1] >= -1e-6 && r[2] >= -1e-6 && r[0] <= 1.000001 &&
               r[1] <= 1.000001 && r[2] <= 1.000001;
    };
    if (!gamut(result)) {
        float lo = 0, hi = 1;
        for (int i = 0; i < 16; ++i) {
            float k = (lo + hi) / 2;
            if (gamut(unlab({v[0], v[1] * k, v[2] * k})))
                lo = k;
            else
                hi = k;
        }
        result = unlab({v[0], v[1] * lo, v[2] * lo});
    }
    for (int c = 0; c < 3; ++c)
        result[c] = encoded(std::clamp(result[c], 0.f, 1.f));
    return result;
}
QByteArray Engine::samplePortrait() {
    static const QByteArray png = [] {
        QImage image(1200, 1600, QImage::Format_RGB32);
        image.fill(QColor("#e9e5dc"));
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#315f86"), 64, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(600, 580, 600, 1110);
        painter.drawLine(600, 710, 320, 940);
        painter.drawLine(600, 710, 880, 940);
        painter.drawLine(600, 1110, 405, 1430);
        painter.drawLine(600, 1110, 795, 1430);
        painter.setPen(QPen(QColor("#25282c"), 16));
        painter.setBrush(QColor("#edc5a3"));
        painter.drawEllipse(QRectF(420, 180, 360, 420));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#25282c"));
        painter.drawEllipse(QRectF(500, 340, 24, 30));
        painter.drawEllipse(QRectF(676, 340, 24, 30));
        painter.setPen(QPen(QColor("#25282c"), 14, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(QRectF(520, 390, 160, 110), 190 * 16, 160 * 16);
        painter.end();
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return data;
    }();
    return png;
}
QRectF Engine::zoomCrop(QRectF basis, QPointF headAnchor, double percent, double headroom) {
    if (!std::isfinite(percent) || percent < 40 || percent > 100 || basis.isEmpty() ||
        !std::isfinite(headroom) || headroom < 0 || headroom > .25)
        throw std::runtime_error("Crop zoom must be 40–100% and headroom 0–25%");
    double scale = std::min({100. / percent, 1. / basis.width(), 1. / basis.height()});
    double width = basis.width() * scale, height = basis.height() * scale;
    return {std::clamp(headAnchor.x() - width / 2, 0., 1. - width),
            std::clamp(headAnchor.y() - headroom * height, 0., 1. - height), width, height};
}
QRect Engine::maskContext(QRect crop, QSize sourceSize, double zoom, double headroom) {
    // The removal-only zoom can reach 30%; the user's export slider stays 40–100%.
    const double scale = std::min({zoom / (zoom - 10.),
                                   double(sourceSize.width()) / crop.width(),
                                   double(sourceSize.height()) / crop.height()});
    const int width = std::max(crop.width(), int(std::floor(crop.width() * scale)));
    const int height = std::max(crop.height(), int(std::floor(crop.height() * scale)));
    const int x = std::clamp(crop.x() - (width - crop.width()) / 2, 0, sourceSize.width() - width);
    const int y = std::clamp(crop.y() - int(std::round(headroom * (height - crop.height()))),
                             0, sourceSize.height() - height);
    return {x, y, width, height};
}
cv::Mat Engine::cropMask(const cv::Mat &mask, QRect context, QRect crop, QSize outputSize) {
    // Map pixel centers directly; avoid rounding a low-resolution mask ROI, which
    // would shift brush/image alignment when crop or pane dimensions change.
    const double sx = double(mask.cols) / context.width() * crop.width() / outputSize.width();
    const double sy = double(mask.rows) / context.height() * crop.height() / outputSize.height();
    const double tx = double(crop.x() - context.x()) * mask.cols / context.width() + (sx - 1) / 2;
    const double ty = double(crop.y() - context.y()) * mask.rows / context.height() + (sy - 1) / 2;
    cv::Mat output;
    cv::warpAffine(mask, output, cv::Matx23d(sx, 0, tx, 0, sy, ty),
                   {outputSize.width(), outputSize.height()},
                   cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
    return output;
}
cv::Mat Engine::sharpenForScreen(const cv::Mat &rgb, const cv::Mat &alpha, const QString &level,
                                 std::atomic_bool *cancel) {
    // Output-scale, alpha-normalized luminance unsharp mask. These are our own
    // screen presets, not a reproduction of Adobe or Capture One processing.
    const float amount = level == "low" ? .35f : level == "high" ? 1.f : .65f;
    cv::Mat luma(rgb.rows, rgb.cols, CV_32F), weighted, weight, blurred;
    for (int y = 0; y < rgb.rows; ++y) {
        if (y % 32 == 0)
            checkCancel(cancel);
        for (int x = 0; x < rgb.cols; ++x) {
            const auto c = rgb.at<cv::Vec3f>(y, x);
            luma.at<float>(y, x) = .2126f * c[0] + .7152f * c[1] + .0722f * c[2];
        }
    }
    cv::multiply(luma, alpha, weighted);
    cv::GaussianBlur(weighted, blurred, {0, 0}, .6);
    cv::GaussianBlur(alpha, weight, {0, 0}, .6);
    cv::Mat result = rgb.clone();
    for (int y = 0; y < rgb.rows; ++y) {
        if (y % 32 == 0)
            checkCancel(cancel);
        for (int x = 0; x < rgb.cols; ++x) {
            if (alpha.at<float>(y, x) <= 0 || weight.at<float>(y, x) < 1e-6f)
                continue;
            float detail = luma.at<float>(y, x) - blurred.at<float>(y, x) / weight.at<float>(y, x);
            float delta =
                amount * std::copysign(std::max(0.f, std::abs(detail) - 1.f / 255), detail);
            delta = std::clamp(delta, -.1f, .1f);
            auto &c = result.at<cv::Vec3f>(y, x);
            for (int k = 0; k < 3; ++k)
                c[k] = std::clamp(c[k] + delta, 0.f, 1.f);
        }
    }
    return result;
}
QSize Engine::outputSize(QSize crop, const Settings &s) {
    int units = std::min(crop.width() / 3, crop.height() / 4);
    if (s.capped)
        units = std::min(units, 120);
    if (s.width > 0)
        units = std::min(units, s.width / 3);
    if (units < 1)
        throw std::runtime_error("Image is too small for a 3:4 crop");
    return {units * 3, units * 4};
}
static double medianLightness(const cv::Mat &rgb, cv::Rect face) {
    face &= cv::Rect(0, 0, rgb.cols, rgb.rows);
    std::vector<float> values;
    for (int y = face.y; y < face.y + face.height; y += 2)
        for (int x = face.x; x < face.x + face.width; x += 2)
            values.push_back(lab(rgb.at<cv::Vec3f>(y, x))[0]);
    if (values.empty())
        throw std::runtime_error("Face region is empty");
    auto mid = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), mid, values.end());
    return *mid;
}
Result Engine::process(const QString &path, const Settings &s, std::atomic_bool *cancel) {
    bool completed = false;
    const auto cleanup = qScopeGuard([&] { if (!completed) models.clearCache(); });
    s.validate();
    checkCancel(cancel);
    Result r;
    auto source = decode(path, s);
    r.sourceSize = source.size();
    r.source = source.scaled(1200, 1200, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                   .convertToFormat(QImage::Format_RGBA8888);
    auto analysisImage = source.scaled(960, 960, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    cv::Mat analysis = rgbMat(analysisImage);
    checkCancel(cancel);
    // Only our exact generated PNG gets synthetic landmarks. Ordinary photos
    // always use detection, even if renamed to the sample's filename.
    QFile sampleFile(path);
    const bool sample = s.rotation % 360 == 0 && sampleFile.size() == samplePortrait().size() &&
                        sampleFile.open(QIODevice::ReadOnly) &&
                        sampleFile.readAll() == samplePortrait();
    std::vector<cv::Rect> faces;
    if (sample && s.autoCrop)
        faces.emplace_back(450 * analysis.cols / 1200, 270 * analysis.rows / 1600,
                           300 * analysis.cols / 1200, 300 * analysis.rows / 1600);
    else if ((s.autoCrop && s.crop.isNull()) || !s.reference.isEmpty())
        faces = models.faces(analysis, cancel);
    if (s.autoCrop && s.crop.isNull()) {
        if (faces.empty())
            r.warnings << "No face detected; center crop needs review";
        else if (faces.size() > 1)
            r.warnings << "Multiple faces; largest face selected";
    }
    QRectF crop = s.crop;
    QPointF headAnchor;
    bool faceAnchor = false;
    if (crop.isNull()) {
        double height = std::min(double(source.height()), source.width() / .75),
               width = height * .75;
        double x = (source.width() - width) / 2, y = (source.height() - height) / 2;
        if (s.autoCrop && !faces.empty()) {
            auto f = faces.front();
            double top = f.y - .3 * f.height;
            auto mask = sample ? cv::Mat() : models.mask(analysis, "fast", "head", cancel);
            int from = std::max(0, int(f.y - .7 * f.height)), to = std::max(0, f.y);
            bool found = sample;
            if (sample)
                top = 180. * analysis.rows / 1600;
            for (int yy = from; yy < to && !sample; ++yy) {
                int count = 0;
                for (int xx = std::max(0, f.x); xx < std::min(mask.cols, f.x + f.width); ++xx)
                    if (mask.at<float>(yy, xx) > .65)
                        ++count;
                if (count > f.width * .25) {
                    top = yy;
                    found = true;
                    break;
                }
            }
            if (!found || top <= 1)
                r.warnings << "Head boundary is uncertain";
            double scale = double(source.height()) / analysis.rows,
                   head = (f.y + f.height - top) * scale;
            headAnchor = QPointF((f.x + f.width / 2.) / analysis.cols, top / analysis.rows);
            faceAnchor = true;
            height = head / .6;
            width = height * .75;
            x = (f.x + f.width / 2.) * scale - width / 2;
            y = top * scale - s.headroom * height;
            if (x < 0 || y < 0 || x + width > source.width() || y + height > source.height())
                r.warnings << "Limited source space; framing adjusted";
            double shrink = std::min({1., source.width() / width, source.height() / height});
            width *= shrink;
            height *= shrink;
            x = std::clamp(x, 0., source.width() - width);
            y = std::clamp(y, 0., source.height() - height);
        }
        crop = {x / source.width(), y / source.height(), width / source.width(),
                height / source.height()};
    }
    r.cropBasis = s.crop.isNull() || s.cropBasis.isNull() ? crop : s.cropBasis;
    if (s.crop.isNull()) {
        if (!faceAnchor)
            headAnchor = QPointF(crop.center().x(), crop.y() + s.headroom * crop.height());
        crop = zoomCrop(r.cropBasis, headAnchor, s.cropZoom, s.headroom);
        if (faceAnchor && std::abs((headAnchor.y() - crop.y()) / crop.height() - s.headroom) > .005)
            r.warnings << "Limited source space; requested headroom could not be maintained";
    }
    if (s.crop.isNull() && r.cropBasis.width() * 100. / s.cropZoom > crop.width() + 1e-6)
        r.warnings << "Crop zoom limited by source boundaries";
    // Normalize manual crops to exact 3:4 by shrinking about their center.
    double w = crop.width() * source.width(), h = crop.height() * source.height();
    int units =
        std::min({int(std::min(w / 3, h / 4) + 1e-7), source.width() / 3, source.height() / 4});
    if (units < 1)
        throw std::runtime_error("Crop is too small");
    QRect region(int(crop.center().x() * source.width() - units * 1.5),
                 int(crop.center().y() * source.height() - units * 2), units * 3, units * 4);
    region.moveLeft(std::clamp(region.x(), 0, source.width() - region.width()));
    region.moveTop(std::clamp(region.y(), 0, source.height() - region.height()));
    r.crop = {double(region.x()) / source.width(), double(region.y()) / source.height(),
              double(region.width()) / source.width(), double(region.height()) / source.height()};
    r.brightness = s.brightness;
    if (!s.reference.isEmpty()) {
        auto ref = decode(s.reference, Settings{})
                       .scaled(960, 960, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        auto refRgb = rgbMat(ref);
        auto refFaces = models.faces(refRgb, cancel);
        if (faces.size() != 1 || refFaces.size() != 1)
            r.warnings << "Brightness matching requires one face in source and reference";
        else {
            double current = medianLightness(analysis, faces.front()),
                   target = medianLightness(refRgb, refFaces.front());
            double amount = (target - current) / std::max(.001, .45 * current * (1 - current));
            r.brightness = std::clamp(amount + s.brightness, -1., 1.);
            if (std::abs(amount) > 1)
                r.warnings << "Reference brightness exceeds the gentle adjustment range";
        }
    }
    r.review = !r.warnings.isEmpty() && !s.automatic && !s.approved;
    checkCancel(cancel);
    r.outputSize = outputSize(region.size(), s);
    auto sourceCrop = source.copy(region);
    auto cut = sourceCrop.scaled(r.outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage maskGuide;
    QRect context = region;
    if (s.background != "off") {
        context = maskContext(region, source.size(), s.cropZoom, s.headroom);
        auto expanded = source.copy(context);
        const int limit = s.background == "fast" ? 1600 : 1024;
        maskGuide = expanded.width() > limit || expanded.height() > limit
                        ? expanded.scaled(limit, limit, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                        : expanded;
    }
    sourceCrop = QImage();
    source = QImage();
    cv::Mat rgb = rgbMat(cut), alpha(cut.height(), cut.width(), CV_32F);
    for (int y = 0; y < cut.height(); ++y) {
        auto *p = reinterpret_cast<const float *>(cut.constScanLine(y));
        for (int x = 0; x < cut.width(); ++x)
            alpha.at<float>(y, x) = p[x * 4 + 3];
    }
    if (s.background != "off") {
        checkCancel(cancel);
        auto contextMask = models.mask(rgbMat(maskGuide), s.background, "export", cancel);
        maskGuide = QImage();
        auto segmentation = cropMask(contextMask, context, region, r.outputSize);
        cv::multiply(alpha, segmentation, alpha);
        // Feather the automatic mask; manual corrections remain explicit keep/remove pixels.
        if (s.feather > 0)
            cv::GaussianBlur(alpha, alpha, {0, 0}, s.feather);
        for (const auto &value : s.strokes) {
            auto stroke = value.toObject();
            auto points = stroke["points"].toArray();
            float radius = float(std::clamp(stroke["radius"].toDouble(.03), .001, .25) *
                                 std::min(alpha.cols, alpha.rows));
            cv::Point previous;
            bool first = true;
            for (auto point : points) {
                auto a = point.toArray();
                if (a.size() != 2)
                    continue;
                double nx = a[0].toDouble(), ny = a[1].toDouble();
                if (!std::isfinite(nx) || !std::isfinite(ny) || nx < 0 || nx > 1 || ny < 0 ||
                    ny > 1)
                    continue;
                cv::Point p(int(nx * (alpha.cols - 1)), int(ny * (alpha.rows - 1)));
                float value = stroke["keep"].toBool() ? 1 : 0;
                cv::circle(alpha, p, std::max(1, int(radius)), cv::Scalar(value), -1);
                if (!first)
                    cv::line(alpha, previous, p, cv::Scalar(value), std::max(1, int(radius * 2)));
                previous = p;
                first = false;
            }
        }
    }
    for (int y = 0; y < rgb.rows; ++y) {
        if (y % 32 == 0)
            checkCancel(cancel);
        for (int x = 0; x < rgb.cols; ++x)
            rgb.at<cv::Vec3f>(y, x) = adjust(rgb.at<cv::Vec3f>(y, x), r.brightness);
    }
    if (s.sharpenScreen)
        rgb = sharpenForScreen(rgb, alpha, s.sharpening, cancel);
    QImage output(cut.size(), QImage::Format_RGBA8888),
        maskImage(cut.size(), QImage::Format_Grayscale8);
    cv::Vec3f bg{float(s.backgroundColor.redF()), float(s.backgroundColor.greenF()),
                 float(s.backgroundColor.blueF())};
    for (int y = 0; y < rgb.rows; ++y) {
        if (y % 32 == 0)
            checkCancel(cancel);
        auto *row = output.scanLine(y);
        auto *mr = maskImage.scanLine(y);
        for (int x = 0; x < rgb.cols; ++x) {
            auto color = rgb.at<cv::Vec3f>(y, x);
            float a = alpha.at<float>(y, x);
            mr[x] = uchar(std::lround(std::clamp(a, 0.f, 1.f) * 255));
            if (s.format == "jpeg") {
                for (int c = 0; c < 3; ++c)
                    color[c] = encoded(linear(color[c]) * a + linear(bg[c]) * (1 - a));
                a = 1;
            }
            for (int c = 0; c < 3; ++c)
                row[4 * x + c] = uchar(std::lround(std::clamp(color[c], 0.f, 1.f) * 255));
            row[4 * x + 3] = uchar(std::lround(std::clamp(a, 0.f, 1.f) * 255));
        }
    }
    output.setColorSpace(QColorSpace::SRgb);
    QBuffer buffer(&r.encoded);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, s.format.toLatin1());
    writer.setQuality(s.quality);
    if (!writer.write(output))
        throw std::runtime_error(writer.errorString().toStdString());
    r.preview = QImage::fromData(r.encoded);
    r.encodedBytes = r.encoded.size();
    r.mask = (maskImage.width() > 1200 || maskImage.height() > 1200)
                 ? maskImage.scaled(1200, 1200, Qt::KeepAspectRatio, Qt::FastTransformation)
                 : maskImage;
    checkCancel(cancel);
    completed = true;
    return r;
}
QString Engine::save(const Result &r, const QString &source, const QString &directory,
                     const Settings &s) {
    if (r.review)
        throw std::runtime_error("Photo needs review before export");
    if (!QDir().mkpath(directory))
        throw std::runtime_error("Cannot create output directory");
    QString stem = s.prefix + QFileInfo(source).completeBaseName() + "-pfp",
            extension = s.format == "jpeg" ? "jpg" : "png";
    for (int suffix = 0; suffix < 100000; ++suffix) {
        QString path = QDir(directory).filePath(
            stem + (suffix ? "-" + QString::number(suffix) : QString()) + "." + extension);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (QFileInfo::exists(path))
                continue;
            throw std::runtime_error(file.errorString().toStdString());
        }
        if (file.write(r.encoded) != r.encoded.size() || !file.flush()) {
            file.close();
            file.remove();
            throw std::runtime_error("Output write failed; incomplete output removed");
        }
        file.close();
        return path;
    }
    throw std::runtime_error("Too many output name collisions");
}
} // namespace gibbon
