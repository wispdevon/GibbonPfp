#pragma once
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QStringList>

namespace gibbon {
struct Settings {
    bool autoCrop = true, capped = true, automatic = false, approved = false;
    int width = 360, height = 480, quality = 92, rotation = 0;
    double headroom = .08, brightness = 0, feather = 1;
    QString background = "off", format = "jpeg", reference, prefix;
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
QString errorText(const std::exception &e);
} // namespace gibbon
