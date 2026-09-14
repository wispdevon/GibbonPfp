#pragma once
#include <QImage>
#include <QString>
namespace gibbon {
QImage decodeTiff(const QString &path);
QImage decodeWebp(const QString &path);
} // namespace gibbon
