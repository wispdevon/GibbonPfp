#pragma once
#include <QFontDatabase>
#include <QGuiApplication>
#include <stdexcept>

inline void loadWorkspaceFonts(const QString &directory = ":/assets/fonts") {
    for (const auto &entry :
         {qMakePair("Inter", "Inter"), qMakePair("SpaceGrotesk", "Space Grotesk"),
          qMakePair("GeistMono", "Geist Mono")}) {
        const int id = QFontDatabase::addApplicationFont(directory + "/" + entry.first + ".ttf");
        if (id < 0 || !QFontDatabase::applicationFontFamilies(id).contains(entry.second))
            throw std::runtime_error("Could not load a required bundled workspace font");
    }
    QFont font("Inter");
    font.setPixelSize(13);
    font.setWeight(QFont::Medium);
    QGuiApplication::setFont(font);
}
