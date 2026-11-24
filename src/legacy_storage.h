#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace headshotflow {
inline constexpr auto legacyAppName = "GibbonPfp";

inline QString legacyLocation(QStandardPaths::StandardLocation type) {
    if (QCoreApplication::applicationName() != "HeadshotFlow")
        return {};
    const auto current = QStandardPaths::writableLocation(type);
    if (current.isEmpty())
        return {};
    return QDir::cleanPath(QDir(current).filePath(QString("../%1").arg(legacyAppName)));
}
} // namespace headshotflow
