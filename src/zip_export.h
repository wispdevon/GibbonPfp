#pragma once
#include <QByteArray>
#include <QSaveFile>
#include <QSet>
#include <QString>
#include <atomic>
struct archive;
namespace gibbon {
// Streaming, atomic archive: destruction without commit leaves the destination intact.
class ZipExport {
  public:
    explicit ZipExport(const QString &path, std::atomic_bool *cancel = nullptr);
    ~ZipExport();
    ZipExport(const ZipExport &) = delete;
    ZipExport &operator=(const ZipExport &) = delete;
    QString addPhoto(const QString &source, const QString &prefix, const QString &format,
                     const QByteArray &bytes);
    void commit(const QByteArray &report);

  private:
    QSaveFile file;
    archive *writer = nullptr;
    std::atomic_bool *cancel;
    QSet<QString> names;
    void add(const QString &name, const QByteArray &bytes);
    void check(int result);
    void checkCancelled() const;
};
} // namespace gibbon
