#include "zip_export.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <memory>
#include <stdexcept>

namespace gibbon {
void ZipExport::checkCancelled() const {
    if (cancel && cancel->load())
        throw std::runtime_error("ZIP export cancelled; destination unchanged");
}
void ZipExport::check(int result) {
    if (result < ARCHIVE_OK) {
        const char *message = archive_error_string(writer);
        throw std::runtime_error(message ? message : "ZIP write failed");
    }
}
ZipExport::ZipExport(const QString &path, std::atomic_bool *cancel) : file(path), cancel(cancel) {
    checkCancelled();
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(file.errorString().toStdString());
    writer = archive_write_new();
    if (!writer)
        throw std::runtime_error("Cannot allocate ZIP writer");
    try {
        check(archive_write_set_format_zip(writer));
        check(archive_write_set_format_option(writer, "zip", "compression", "store"));
        check(archive_write_set_bytes_per_block(writer, 0));
        check(archive_write_open(
            writer, this, nullptr,
            [](archive *, void *context, const void *data, size_t size) -> la_ssize_t {
                auto *self = static_cast<ZipExport *>(context);
                if (self->cancel && self->cancel->load())
                    return -1;
                const auto written =
                    self->file.write(static_cast<const char *>(data), qint64(size));
                return written == qint64(size) ? la_ssize_t(written) : -1;
            },
            nullptr));
    } catch (...) {
        archive_write_free(writer);
        writer = nullptr;
        throw;
    }
}
ZipExport::~ZipExport() {
    if (writer)
        archive_write_free(writer);
}
void ZipExport::add(const QString &name, const QByteArray &bytes) {
    checkCancelled();
    std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(),
                                                                        archive_entry_free);
    if (!entry)
        throw std::runtime_error("Cannot allocate ZIP entry");
    const auto utf8 = name.toUtf8();
    archive_entry_set_pathname_utf8(entry.get(), utf8.constData());
    archive_entry_set_size(entry.get(), bytes.size());
    archive_entry_set_filetype(entry.get(), AE_IFREG);
    archive_entry_set_perm(entry.get(), 0644);
    check(archive_write_header(writer, entry.get()));
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        checkCancelled();
        const auto n =
            archive_write_data(writer, bytes.constData() + offset,
                               size_t(std::min<qsizetype>(bytes.size() - offset, 1024 * 1024)));
        if (n <= 0) {
            check(ARCHIVE_FATAL);
        }
        offset += n;
    }
    check(archive_write_finish_entry(writer));
}
QString ZipExport::addPhoto(const QString &source, const QString &prefix, const QString &format,
                            const QByteArray &bytes) {
    QString stem = prefix + QFileInfo(source).completeBaseName() + "-pfp";
    stem.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "_");
    while (stem.startsWith('.'))
        stem.remove(0, 1);
    stem = stem.left(120);
    if (stem.isEmpty())
        stem = "photo";
    const QString extension = format == "jpeg" ? ".jpg" : ".png";
    QString name = stem + extension;
    for (int suffix = 1; names.contains(name.toCaseFolded()); ++suffix)
        name = stem + "-" + QString::number(suffix) + extension;
    add(name, bytes);
    names.insert(name.toCaseFolded());
    return name;
}
void ZipExport::commit(const QByteArray &report) {
    add("export-report.json", report);
    checkCancelled();
    check(archive_write_close(writer));
    archive_write_free(writer);
    writer = nullptr;
    checkCancelled();
    if (!file.commit())
        throw std::runtime_error(file.errorString().toStdString());
}
} // namespace gibbon
