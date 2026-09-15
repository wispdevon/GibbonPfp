#include "settings.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <stdexcept>

namespace gibbon {
QJsonObject Settings::json() const {
    return {{"version", 1},
            {"autoCrop", autoCrop},
            {"sharpenScreen", sharpenScreen},
            {"sharpening", sharpening},
            {"cropBasis",
             QJsonArray{cropBasis.x(), cropBasis.y(), cropBasis.width(), cropBasis.height()}},
            {"capped", capped},
            {"automatic", automatic},
            {"approved", approved},
            {"width", width},
            {"height", height},
            {"quality", quality},
            {"rotation", rotation},
            {"headroom", headroom},
            {"cropZoom", cropZoom},
            {"brightness", brightness},
            {"feather", feather},
            {"background", background},
            {"format", format},
            {"backgroundColor", backgroundColor.name()},
            {"reference", reference},
            {"prefix", prefix},
            {"crop", QJsonArray{crop.x(), crop.y(), crop.width(), crop.height()}},
            {"whiteBalance", whiteBalance},
            {"temperature", temperature},
            {"tint", tint},
            {"highlight", highlight},
            {"strokes", strokes}};
}
Settings Settings::fromJson(const QJsonObject &j) {
    if (j.value("version").toInt(1) != 1)
        throw std::runtime_error("Unsupported settings version");
    Settings s;
    auto b = [&](const char *k, bool &v) {
        if (j.contains(k))
            v = j[k].toBool();
    };
    auto n = [&](const char *k, int &v) {
        if (j.contains(k))
            v = j[k].toInt();
    };
    auto d = [&](const char *k, double &v) {
        if (j.contains(k))
            v = j[k].toDouble();
    };
    auto t = [&](const char *k, QString &v) {
        if (j.contains(k))
            v = j[k].toString();
    };
    b("autoCrop", s.autoCrop);
    b("sharpenScreen", s.sharpenScreen);
    t("sharpening", s.sharpening);
    b("capped", s.capped);
    b("automatic", s.automatic);
    b("approved", s.approved);
    n("width", s.width);
    n("height", s.height);
    n("quality", s.quality);
    n("rotation", s.rotation);
    d("headroom", s.headroom);
    d("cropZoom", s.cropZoom);
    d("brightness", s.brightness);
    d("feather", s.feather);
    t("background", s.background);
    t("format", s.format);
    t("reference", s.reference);
    t("prefix", s.prefix);
    t("whiteBalance", s.whiteBalance);
    n("temperature", s.temperature);
    d("tint", s.tint);
    n("highlight", s.highlight);
    if (j.contains("backgroundColor"))
        s.backgroundColor = QColor(j["backgroundColor"].toString());
    auto a = j["crop"].toArray();
    if (a.size() == 4)
        s.crop = QRectF(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble());
    auto basis = j["cropBasis"].toArray();
    if (basis.size() == 4)
        s.cropBasis = QRectF(basis[0].toDouble(), basis[1].toDouble(), basis[2].toDouble(),
                             basis[3].toDouble());
    // Older sessions stored magnification only through crop geometry. Keep
    // their saved manual frame as the 100% baseline in the new range.
    if (!j.contains("cropZoom") && !s.crop.isNull())
        s.cropBasis = s.crop;
    s.strokes = j["strokes"].toArray();
    s.validate();
    return s;
}
void Settings::validate() const {
    auto check = [](bool ok, const char *m) {
        if (!ok)
            throw std::runtime_error(m);
    };
    check(width >= 0 && height >= 0 && width <= 60000 && height <= 80000,
          "Invalid output dimensions");
    check((width == 0 && height == 0) || (width >= 3 && height >= 4 && width % 3 == 0 &&
                                          height % 4 == 0 && width / 3 == height / 4),
          "Size must be exact 3:4 (for example 360x480); use 0x0 for source size");
    check(QStringList{"low", "standard", "high"}.contains(sharpening),
          "Unknown screen sharpening level");
    if (!cropBasis.isNull())
        check(std::isfinite(cropBasis.x()) && std::isfinite(cropBasis.y()) &&
                  std::isfinite(cropBasis.width()) && std::isfinite(cropBasis.height()) &&
                  cropBasis.x() >= 0 && cropBasis.y() >= 0 && cropBasis.width() > 0 &&
                  cropBasis.height() > 0 && cropBasis.right() <= 1.000001 &&
                  cropBasis.bottom() <= 1.000001,
              "Crop zoom baseline must be inside the source image");
    check(std::isfinite(cropZoom) && cropZoom >= 40 && cropZoom <= 100,
          "Crop zoom must be 40–100%");
    check(quality >= 1 && quality <= 100, "JPEG quality must be 1–100");
    check(std::isfinite(headroom) && headroom >= 0 && headroom <= .25, "Headroom must be 0–25%");
    check(std::isfinite(brightness) && std::abs(brightness) <= 1, "Brightness must be -1 to 1");
    check(std::isfinite(feather) && feather >= 0 && feather <= 10, "Feather must be 0–10 pixels");
    check(QStringList{"off", "fast", "quality"}.contains(background), "Unknown background method");
    check(QStringList{"jpeg", "png"}.contains(format), "Format must be jpeg or png");
    check(QStringList{"camera", "daylight", "cloudy", "tungsten", "custom"}.contains(whiteBalance),
          "Unknown white balance");
    check(temperature >= 2000 && temperature <= 12000 && std::isfinite(tint) && tint >= .5 &&
              tint <= 2 && highlight >= 0 && highlight <= 9,
          "Invalid RAW development settings");
    check(rotation % 90 == 0, "Rotation must be a multiple of 90 degrees");
    check(backgroundColor.isValid(), "Invalid background color");
    check(!prefix.contains('/') && !prefix.contains('\\') && !prefix.contains(':') &&
              !prefix.contains(".."),
          "Prefix must be a filename, not a path");
    if (!crop.isNull())
        check(std::isfinite(crop.x()) && std::isfinite(crop.y()) && std::isfinite(crop.width()) &&
                  std::isfinite(crop.height()) && crop.x() >= 0 && crop.y() >= 0 &&
                  crop.width() > 0 && crop.height() > 0 && crop.right() <= 1.000001 &&
                  crop.bottom() <= 1.000001,
              "Crop must be inside the source image");
    check(strokes.size() <= 10000, "Too many mask strokes");
}
bool supportedPath(const QString &p) {
    return QStringList{"jpg", "jpeg", "png", "webp", "heic", "heif", "tif", "tiff", "dng",
                       "cr2", "cr3",  "nef", "nrw",  "arw",  "srf",  "sr2", "raf",  "orf",
                       "rw2", "pef",  "raw", "rwl",  "3fr",  "iiq",  "erf", "kdc",  "mos"}
        .contains(QFileInfo(p).suffix().toLower());
}
QStringList expandInputs(const QStringList &paths, bool recursive) {
    QStringList out;
    QSet<QString> seen;
    auto add = [&](QString p) {
        p = QFileInfo(p).absoluteFilePath();
        if (!seen.contains(p)) {
            out << p;
            seen.insert(p);
        }
    };
    for (const auto &p : paths) {
        if (QFileInfo(p).isDir()) {
            QDirIterator it(p, QDir::Files,
                            recursive ? QDirIterator::Subdirectories
                                      : QDirIterator::NoIteratorFlags);
            QStringList files;
            while (it.hasNext()) {
                auto f = it.next();
                if (supportedPath(f))
                    files << f;
            }
            files.sort();
            for (auto f : files)
                add(f);
        } else
            add(p);
    }
    return out;
}
QString errorText(const std::exception &e) {
    return QString::fromUtf8(e.what());
}
} // namespace gibbon
