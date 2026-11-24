#pragma once
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QStringList>

namespace headshotflow {
struct Settings {
    static constexpr int defaultHeight = 1024;
    static constexpr int defaultWidth = defaultHeight / 4 * 3;
    bool autoCrop = true, capped = true, automatic = false, approved = false;
    bool sharpenScreen = true;
    QString sharpening = "standard";
    QRectF cropBasis; // Initial framing used as the 100% zoom baseline.
    int width = defaultWidth, height = defaultHeight, quality = 80, rotation = 0;
    double cropZoom = 70; // Requested percentage, preserved when headroom changes.
    double headroom = .08, brightness = 0, feather = 0;
    QString background = "fast", format = "jpeg", reference, prefix;
    QColor backgroundColor = Qt::white;
    QRectF crop; // Normalized, after EXIF orientation and user rotation.
    QString whiteBalance = "camera";
    int temperature = 6500, highlight = 0;
    double tint = 1;
    QJsonArray strokes;
    QJsonObject json() const;
    static Settings fromJson(const QJsonObject &json);
    void validate() const;
};
QStringList expandInputs(const QStringList &paths, bool recursive = false);
bool supportedPath(const QString &path);
bool sameExistingFile(const QString &first, const QString &second);
QString errorText(const std::exception &e);
} // namespace headshotflow
