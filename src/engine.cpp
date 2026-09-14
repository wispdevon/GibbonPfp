#include "engine.h"
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
    s.validate();
    checkCancel(cancel);
    Result r;
    auto source = decode(path, s);
    r.sourceSize = source.size();
    r.source = source.scaled(1200, 1200, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                   .convertToFormat(QImage::Format_RGBA8888);
    auto small = source.scaled(960, 960, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    cv::Mat analysis = rgbMat(small);
    checkCancel(cancel);
    std::vector<cv::Rect> faces;
    if ((s.autoCrop && s.crop.isNull()) || !s.reference.isEmpty())
        faces = models.faces(analysis);
    if (s.autoCrop && s.crop.isNull()) {
        if (faces.empty())
            r.warnings << "No face detected; center crop needs review";
        else if (faces.size() > 1)
            r.warnings << "Multiple faces; largest face selected";
    }
    QRectF crop = s.crop;
    if (crop.isNull()) {
        double height = std::min(double(source.height()), source.width() / .75),
               width = height * .75;
        double x = (source.width() - width) / 2, y = (source.height() - height) / 2;
        if (s.autoCrop && !faces.empty()) {
            auto f = faces.front();
            double top = f.y - .3 * f.height;
            auto mask = models.mask(analysis, "fast");
            int from = std::max(0, int(f.y - .7 * f.height)), to = std::max(0, f.y);
            bool found = false;
            for (int yy = from; yy < to; ++yy) {
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
    // Normalize manual crops to exact 3:4 by shrinking about their center.
    double w = crop.width() * source.width(), h = crop.height() * source.height();
    int units = std::min({int(std::min(w / 3, h / 4)), source.width() / 3, source.height() / 4});
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
        auto ref =
            decode(s.reference, s).scaled(960, 960, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        auto refRgb = rgbMat(ref);
        auto refFaces = models.faces(refRgb);
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
    auto cut =
        source.copy(region).scaled(r.outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    source = QImage();
    cv::Mat rgb = rgbMat(cut), alpha(cut.height(), cut.width(), CV_32F);
    for (int y = 0; y < cut.height(); ++y) {
        auto *p = reinterpret_cast<const float *>(cut.constScanLine(y));
        for (int x = 0; x < cut.width(); ++x)
            alpha.at<float>(y, x) = p[x * 4 + 3];
    }
    if (s.background != "off") {
        checkCancel(cancel);
        auto segmentation = models.mask(rgb, s.background);
        cv::multiply(alpha, segmentation, alpha);
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
        if (s.feather > 0)
            cv::GaussianBlur(alpha, alpha, {0, 0}, s.feather);
    }
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
            auto color = adjust(rgb.at<cv::Vec3f>(y, x), r.brightness);
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
    r.mask = maskImage.scaled(1200, 1200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
