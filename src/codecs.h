#pragma once
#include <QImage>
#include <QString>
namespace headshotflow {
QImage decodeTiff(const QString &path);
QImage decodeWebp(const QString &path);
} // namespace headshotflow
