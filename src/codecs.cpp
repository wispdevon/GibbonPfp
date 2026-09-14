#include "codecs.h"
#include <QColorSpace>
#include <QFile>
#include <QTransform>
#include <QtEndian>
#include <memory>
#include <stdexcept>
#include <tiffio.h>
#include <webp/decode.h>
#include <webp/demux.h>

namespace gibbon {
static QImage orient(QImage i, int o) {
    QTransform t;
    switch (o) {
    case 2:
        return i.mirrored(true, false);
    case 3:
        t.rotate(180);
        break;
    case 4:
        return i.mirrored(false, true);
    case 5:
        return i.mirrored(true, false).transformed(t.rotate(270));
    case 6:
        t.rotate(90);
        break;
    case 7:
        return i.mirrored(true, false).transformed(t.rotate(90));
    case 8:
        t.rotate(270);
        break;
    default:
        return i;
    }
    return i.transformed(t);
}
QImage decodeTiff(const QString &path) {
#ifdef _WIN32
    TIFF *pointer = TIFFOpenW(path.toStdWString().c_str(), "r");
#else
    TIFF *pointer = TIFFOpen(path.toUtf8().constData(), "r");
#endif
    if (!pointer)
        throw std::runtime_error("Cannot decode TIFF");
    auto t = std::unique_ptr<TIFF, decltype(&TIFFClose)>(pointer, TIFFClose);
    uint32_t w = 0, h = 0;
    uint16_t bits = 8, samples = 3, photo = PHOTOMETRIC_RGB, planar = PLANARCONFIG_CONTIG,
             orientation = ORIENTATION_TOPLEFT, extras = 0;
    uint16_t *extraTypes = nullptr;
    TIFFGetField(t.get(), TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(t.get(), TIFFTAG_IMAGELENGTH, &h);
    TIFFGetFieldDefaulted(t.get(), TIFFTAG_BITSPERSAMPLE, &bits);
    TIFFGetFieldDefaulted(t.get(), TIFFTAG_SAMPLESPERPIXEL, &samples);
    TIFFGetFieldDefaulted(t.get(), TIFFTAG_PHOTOMETRIC, &photo);
    TIFFGetFieldDefaulted(t.get(), TIFFTAG_PLANARCONFIG, &planar);
    TIFFGetFieldDefaulted(t.get(), TIFFTAG_ORIENTATION, &orientation);
    TIFFGetFieldDefaulted(t.get(), TIFFTAG_EXTRASAMPLES, &extras, &extraTypes);
    if (!w || !h || uint64_t(w) * h > 200000000)
        throw std::runtime_error("TIFF dimensions exceed the 200MP decode limit");
    QImage image;
    bool direct = (bits == 8 || bits == 16) && planar == PLANARCONFIG_CONTIG &&
                  !TIFFIsTiled(t.get()) &&
                  ((photo == PHOTOMETRIC_RGB && (samples == 3 || samples == 4)) ||
                   ((photo == PHOTOMETRIC_MINISBLACK || photo == PHOTOMETRIC_MINISWHITE) &&
                    (samples == 1 || samples == 2)));
    if (direct) {
        bool alpha = extras > 0 && (samples == 4 || samples == 2);
        image = QImage(int(w), int(h),
                       alpha && extraTypes[0] == EXTRASAMPLE_ASSOCALPHA
                           ? QImage::Format_RGBA64_Premultiplied
                           : QImage::Format_RGBA64);
        auto size = TIFFScanlineSize(t.get());
        if (size < tsize_t(w) * samples * (bits / 8))
            throw std::runtime_error("Invalid TIFF row size");
        QByteArray row(size, '\0');
        for (uint32_t y = 0; y < h; ++y) {
            if (TIFFReadScanline(t.get(), row.data(), y) < 0)
                throw std::runtime_error("Corrupt TIFF scanline");
            auto *out = reinterpret_cast<QRgba64 *>(image.scanLine(int(y)));
            auto sample = [&](uint32_t i) -> quint16 {
                return bits == 16 ? reinterpret_cast<const quint16 *>(row.constData())[i]
                                  : quint16(uchar(row[i])) * 257;
            };
            for (uint32_t x = 0; x < w; ++x) {
                quint16 r = sample(x * samples), g = r, b = r, a = 65535;
                if (photo == PHOTOMETRIC_RGB) {
                    g = sample(x * samples + 1);
                    b = sample(x * samples + 2);
                } else if (photo == PHOTOMETRIC_MINISWHITE) {
                    r = g = b = 65535 - r;
                }
                if (alpha)
                    a = sample(x * samples + samples - 1);
                out[x] = QRgba64::fromRgba64(r, g, b, a);
            }
        }
        image = orient(image, orientation);
    } else {
        std::vector<uint32_t> raster(size_t(w) * h);
        if (!TIFFReadRGBAImageOriented(t.get(), w, h, raster.data(), ORIENTATION_TOPLEFT, 0))
            throw std::runtime_error("Unsupported TIFF layout");
        image = QImage(int(w), int(h), QImage::Format_RGBA8888_Premultiplied);
        for (uint32_t y = 0; y < h; ++y) {
            auto *row = image.scanLine(int(y));
            for (uint32_t x = 0; x < w; ++x) {
                auto p = raster[y * w + x];
                row[4 * x] = TIFFGetR(p);
                row[4 * x + 1] = TIFFGetG(p);
                row[4 * x + 2] = TIFFGetB(p);
                row[4 * x + 3] = TIFFGetA(p);
            }
        }
    }
    uint32_t length = 0;
    void *profile = nullptr;
    if (TIFFGetField(t.get(), TIFFTAG_ICCPROFILE, &length, &profile))
        image.setColorSpace(
            QColorSpace::fromIccProfile(QByteArray(static_cast<const char *>(profile), length)));
    return image;
}
static int exifOrientation(const uint8_t *data, size_t size) {
    if (size >= 6 && !memcmp(data, "Exif\0\0", 6)) {
        data += 6;
        size -= 6;
    }
    if (size < 8)
        return 1;
    bool le = data[0] == 'I' && data[1] == 'I';
    if (!le && !(data[0] == 'M' && data[1] == 'M'))
        return 1;
    auto u16 = [&](size_t offset) {
        return le ? qFromLittleEndian<quint16>(data + offset)
                  : qFromBigEndian<quint16>(data + offset);
    };
    auto u32 = [&](size_t offset) {
        return le ? qFromLittleEndian<quint32>(data + offset)
                  : qFromBigEndian<quint32>(data + offset);
    };
    size_t offset = u32(4);
    if (offset > size - 2)
        return 1;
    int count = u16(offset);
    offset += 2;
    for (int i = 0; i < count && offset + 12 <= size; ++i, offset += 12)
        if (u16(offset) == 274 && u16(offset + 2) == 3 && u32(offset + 4) == 1)
            return u16(offset + 8);
    return 1;
}
QImage decodeWebp(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot open WebP");
    auto bytes = f.readAll();
    auto *data = reinterpret_cast<const uint8_t *>(bytes.constData());
    int w, h;
    if (!WebPGetInfo(data, size_t(bytes.size()), &w, &h) || int64_t(w) * h > 200000000)
        throw std::runtime_error("Invalid or oversized WebP");
    QImage image(w, h, QImage::Format_RGBA8888);
    if (!WebPDecodeRGBAInto(data, size_t(bytes.size()), image.bits(), size_t(image.sizeInBytes()),
                            int(image.bytesPerLine())))
        throw std::runtime_error("WebP decode failed");
    WebPData input{data, size_t(bytes.size())};
    auto demux = std::unique_ptr<WebPDemuxer, decltype(&WebPDemuxDelete)>(WebPDemux(&input),
                                                                          WebPDemuxDelete);
    if (demux) {
        WebPChunkIterator it;
        if (WebPDemuxGetChunk(demux.get(), "ICCP", 1, &it)) {
            image.setColorSpace(QColorSpace::fromIccProfile(QByteArray(
                reinterpret_cast<const char *>(it.chunk.bytes), qsizetype(it.chunk.size))));
            WebPDemuxReleaseChunkIterator(&it);
        }
        if (WebPDemuxGetChunk(demux.get(), "EXIF", 1, &it)) {
            image = orient(image, exifOrientation(it.chunk.bytes, it.chunk.size));
            WebPDemuxReleaseChunkIterator(&it);
        }
    }
    return image;
}
} // namespace gibbon
