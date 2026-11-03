#include "RasterContrast.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
const quint16 TagImageWidth = 256;
const quint16 TagImageLength = 257;
const quint16 TagBitsPerSample = 258;
const quint16 TagCompression = 259;
const quint16 TagPhotometric = 262;
const quint16 TagStripOffsets = 273;
const quint16 TagOrientation = 274;
const quint16 TagSamplesPerPixel = 277;
const quint16 TagRowsPerStrip = 278;
const quint16 TagStripByteCounts = 279;
const quint16 TagPlanarConfig = 284;
const quint16 TagPredictor = 317;
const quint16 TagTileWidth = 322;
const quint16 TagTileLength = 323;
const quint16 TagTileOffsets = 324;
const quint16 TagTileByteCounts = 325;
const quint16 TagSampleFormat = 339;

const quint16 TypeByte = 1;
const quint16 TypeAscii = 2;
const quint16 TypeShort = 3;
const quint16 TypeLong = 4;

const quint16 CompressionNone = 1;
const quint16 CompressionLzw = 5;
const quint16 CompressionPackBits = 32773;

const quint16 PhotometricMinIsWhite = 0;
const quint16 PhotometricMinIsBlack = 1;
const quint16 PhotometricRgb = 2;

const quint16 PlanarContig = 1;
const quint16 PlanarSeparate = 2;

const quint16 SampleFormatUint = 1;
const quint16 SampleFormatInt = 2;

struct TiffEntry
{
    quint16 tag;
    quint16 type;
    quint32 count;
    quint32 valueOffset;
    QByteArray inlineValue;
};

struct ImageInfo
{
    ImageInfo()
        : littleEndian(true),
          width(0),
          height(0),
          bitsPerSample(0),
          samplesPerPixel(1),
          sampleFormat(SampleFormatUint),
          compression(CompressionNone),
          photometric(PhotometricMinIsBlack),
          planarConfig(PlanarContig),
          predictor(1),
          orientation(1),
          rowsPerStrip(0)
    {
    }

    bool littleEndian;
    quint32 width;
    quint32 height;
    quint16 bitsPerSample;
    quint16 samplesPerPixel;
    quint16 sampleFormat;
    quint16 compression;
    quint16 photometric;
    quint16 planarConfig;
    quint16 predictor;
    quint16 orientation;
    quint32 rowsPerStrip;
    QVector<quint32> stripOffsets;
    QVector<quint32> stripByteCounts;
};

quint16 readU16(const QByteArray &data, int offset, bool littleEndian)
{
    const quint16 a = static_cast<quint8>(data.at(offset));
    const quint16 b = static_cast<quint8>(data.at(offset + 1));

    return littleEndian
        ? static_cast<quint16>(a | (b << 8))
        : static_cast<quint16>((a << 8) | b);
}

quint32 readU32(const QByteArray &data, int offset, bool littleEndian)
{
    quint32 result = 0;

    if (littleEndian)
    {
        result =
            static_cast<quint32>(static_cast<quint8>(data.at(offset))) |
            (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 8) |
            (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 16) |
            (static_cast<quint32>(static_cast<quint8>(data.at(offset + 3))) << 24);
    }
    else
    {
        result =
            (static_cast<quint32>(static_cast<quint8>(data.at(offset))) << 24) |
            (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 16) |
            (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 8) |
            static_cast<quint32>(static_cast<quint8>(data.at(offset + 3)));
    }

    return result;
}

void appendU16LE(QByteArray &data, quint16 value)
{
    data.append(static_cast<char>(value & 0xff));
    data.append(static_cast<char>((value >> 8) & 0xff));
}

void appendU32LE(QByteArray &data, quint32 value)
{
    data.append(static_cast<char>(value & 0xff));
    data.append(static_cast<char>((value >> 8) & 0xff));
    data.append(static_cast<char>((value >> 16) & 0xff));
    data.append(static_cast<char>((value >> 24) & 0xff));
}

bool readAt(QFile &file, quint32 offset, quint32 size, QByteArray &data)
{
    if (!file.seek(offset))
        return false;

    data = file.read(size);
    return data.size() == static_cast<int>(size);
}

int typeSize(quint16 type)
{
    switch (type)
    {
    case TypeByte:
    case TypeAscii:
        return 1;
    case TypeShort:
        return 2;
    case TypeLong:
        return 4;
    default:
        return 0;
    }
}

bool entryData(QFile &file,
               const TiffEntry &entry,
               QByteArray &data)
{
    const int itemSize = typeSize(entry.type);

    if (itemSize == 0)
        return false;

    const quint64 total =
        static_cast<quint64>(itemSize) * entry.count;

    if (total == 0 || total > 64u * 1024u * 1024u)
        return false;

    if (total <= 4)
    {
        data = entry.inlineValue.left(static_cast<int>(total));
        return true;
    }

    return readAt(file,
                  entry.valueOffset,
                  static_cast<quint32>(total),
                  data);
}

const TiffEntry *findEntry(const QVector<TiffEntry> &entries,
                           quint16 tag)
{
    for (int i = 0; i < entries.size(); ++i)
    {
        if (entries.at(i).tag == tag)
            return &entries.at(i);
    }

    return 0;
}

bool unsignedValues(QFile &file,
                    const TiffEntry &entry,
                    bool littleEndian,
                    QVector<quint32> &values)
{
    QByteArray data;

    if (!entryData(file, entry, data))
        return false;

    values.clear();
    values.reserve(static_cast<int>(entry.count));

    if (entry.type == TypeShort)
    {
        for (quint32 i = 0; i < entry.count; ++i)
            values.push_back(readU16(data, static_cast<int>(i * 2u), littleEndian));

        return true;
    }

    if (entry.type == TypeLong)
    {
        for (quint32 i = 0; i < entry.count; ++i)
            values.push_back(readU32(data, static_cast<int>(i * 4u), littleEndian));

        return true;
    }

    return false;
}

bool firstUnsigned(QFile &file,
                   const QVector<TiffEntry> &entries,
                   quint16 tag,
                   bool littleEndian,
                   quint32 defaultValue,
                   quint32 &value)
{
    const TiffEntry *entry = findEntry(entries, tag);

    if (!entry)
    {
        value = defaultValue;
        return true;
    }

    QVector<quint32> values;

    if (!unsignedValues(file, *entry, littleEndian, values) ||
        values.isEmpty())
    {
        return false;
    }

    value = values.first();
    return true;
}

bool parseTiff(QFile &file,
               ImageInfo &info,
               QString &error)
{
    if (!file.seek(0))
    {
        error = "Cannot seek to TIFF header";
        return false;
    }

    const QByteArray header = file.read(8);

    if (header.size() != 8)
    {
        error = "Invalid TIFF header";
        return false;
    }

    if (header.at(0) == 'I' && header.at(1) == 'I')
        info.littleEndian = true;
    else if (header.at(0) == 'M' && header.at(1) == 'M')
        info.littleEndian = false;
    else
    {
        error = "Invalid TIFF byte order";
        return false;
    }

    if (readU16(header, 2, info.littleEndian) != 42)
    {
        error = "Only classic TIFF is supported";
        return false;
    }

    const quint32 ifdOffset =
        readU32(header, 4, info.littleEndian);

    QByteArray countBytes;

    if (!readAt(file, ifdOffset, 2, countBytes))
    {
        error = "Cannot read TIFF IFD";
        return false;
    }

    const quint16 count =
        readU16(countBytes, 0, info.littleEndian);

    QVector<TiffEntry> entries;
    entries.reserve(count);

    for (quint16 i = 0; i < count; ++i)
    {
        QByteArray raw;

        if (!readAt(file,
                    ifdOffset + 2u + static_cast<quint32>(i) * 12u,
                    12,
                    raw))
        {
            error = "Cannot read TIFF directory entry";
            return false;
        }

        TiffEntry entry;
        entry.tag = readU16(raw, 0, info.littleEndian);
        entry.type = readU16(raw, 2, info.littleEndian);
        entry.count = readU32(raw, 4, info.littleEndian);
        entry.valueOffset = readU32(raw, 8, info.littleEndian);
        entry.inlineValue = raw.mid(8, 4);

        entries.push_back(entry);
    }

    quint32 value = 0;

    if (!firstUnsigned(file, entries, TagImageWidth,
                       info.littleEndian, 0, value) ||
        value == 0)
    {
        error = "Invalid or missing TIFF ImageWidth";
        return false;
    }
    info.width = value;

    if (!firstUnsigned(file, entries, TagImageLength,
                       info.littleEndian, 0, value) ||
        value == 0)
    {
        error = "Invalid or missing TIFF ImageLength";
        return false;
    }
    info.height = value;

    if (!firstUnsigned(file, entries, TagSamplesPerPixel,
                       info.littleEndian, 1, value))
    {
        error = "Invalid SamplesPerPixel";
        return false;
    }
    info.samplesPerPixel = static_cast<quint16>(value);

    const TiffEntry *bitsEntry = findEntry(entries, TagBitsPerSample);

    if (!bitsEntry)
    {
        info.bitsPerSample = 1;
    }
    else
    {
        QVector<quint32> bits;

        if (!unsignedValues(file, *bitsEntry,
                            info.littleEndian, bits) ||
            bits.isEmpty())
        {
            error = "Invalid BitsPerSample";
            return false;
        }

        info.bitsPerSample =
            static_cast<quint16>(bits.first());

        for (int i = 1; i < bits.size(); ++i)
        {
            if (bits.at(i) != info.bitsPerSample)
            {
                error = "Different BitsPerSample values per band are not supported";
                return false;
            }
        }
    }

    if (!firstUnsigned(file, entries, TagCompression,
                       info.littleEndian, CompressionNone, value))
    {
        error = "Invalid Compression";
        return false;
    }
    info.compression = static_cast<quint16>(value);

    if (!firstUnsigned(file, entries, TagPhotometric,
                       info.littleEndian,
                       info.samplesPerPixel == 1
                           ? PhotometricMinIsBlack
                           : PhotometricRgb,
                       value))
    {
        error = "Invalid PhotometricInterpretation";
        return false;
    }
    info.photometric = static_cast<quint16>(value);

    if (!firstUnsigned(file, entries, TagPlanarConfig,
                       info.littleEndian, PlanarContig, value))
    {
        error = "Invalid PlanarConfiguration";
        return false;
    }
    info.planarConfig = static_cast<quint16>(value);

    if (!firstUnsigned(file, entries, TagPredictor,
                       info.littleEndian, 1, value))
    {
        error = "Invalid Predictor";
        return false;
    }
    info.predictor = static_cast<quint16>(value);

    if (!firstUnsigned(file, entries, TagOrientation,
                       info.littleEndian, 1, value))
    {
        error = "Invalid Orientation";
        return false;
    }
    info.orientation = static_cast<quint16>(value);

    if (!firstUnsigned(file, entries, TagRowsPerStrip,
                       info.littleEndian, info.height, value) ||
        value == 0)
    {
        error = "Invalid RowsPerStrip";
        return false;
    }
    info.rowsPerStrip = value;

    if (!firstUnsigned(file, entries, TagSampleFormat,
                       info.littleEndian, SampleFormatUint, value))
    {
        error = "Invalid SampleFormat";
        return false;
    }
    info.sampleFormat = static_cast<quint16>(value);

    if (findEntry(entries, TagTileWidth) ||
        findEntry(entries, TagTileLength) ||
        findEntry(entries, TagTileOffsets) ||
        findEntry(entries, TagTileByteCounts))
    {
        error = "Tiled TIFF is not supported by the built-in task reader yet";
        return false;
    }

    const TiffEntry *offsetEntry =
        findEntry(entries, TagStripOffsets);
    const TiffEntry *countEntry =
        findEntry(entries, TagStripByteCounts);

    if (!offsetEntry || !countEntry ||
        !unsignedValues(file, *offsetEntry,
                        info.littleEndian, info.stripOffsets) ||
        !unsignedValues(file, *countEntry,
                        info.littleEndian, info.stripByteCounts) ||
        info.stripOffsets.size() != info.stripByteCounts.size())
    {
        error = "Invalid TIFF strip offsets/byte counts";
        return false;
    }

    if (info.bitsPerSample != 8 &&
        info.bitsPerSample != 16)
    {
        error = QString("Unsupported BitsPerSample=%1; supported: 8 or 16")
                    .arg(info.bitsPerSample);
        return false;
    }

    if (info.samplesPerPixel != 1 &&
        info.samplesPerPixel != 3)
    {
        error = QString("Unsupported SamplesPerPixel=%1; supported: grayscale or RGB")
                    .arg(info.samplesPerPixel);
        return false;
    }

    if (info.sampleFormat != SampleFormatUint &&
        info.sampleFormat != SampleFormatInt)
    {
        error = QString("Unsupported SampleFormat=%1; integer raster required")
                    .arg(info.sampleFormat);
        return false;
    }

    if (info.planarConfig != PlanarContig &&
        info.planarConfig != PlanarSeparate)
    {
        error = QString("Unsupported PlanarConfiguration=%1")
                    .arg(info.planarConfig);
        return false;
    }

    if (info.compression != CompressionNone &&
        info.compression != CompressionLzw &&
        info.compression != CompressionPackBits)
    {
        error = QString("Unsupported TIFF compression=%1. Supported: none, LZW, PackBits.")
                    .arg(info.compression);
        return false;
    }

    if (info.predictor != 1 && info.predictor != 2)
    {
        error = QString("Unsupported TIFF Predictor=%1")
                    .arg(info.predictor);
        return false;
    }

    if (info.orientation != 1)
    {
        error = QString("Unsupported TIFF Orientation=%1; expected top-left")
                    .arg(info.orientation);
        return false;
    }

    if (info.samplesPerPixel == 1)
    {
        if (info.photometric != PhotometricMinIsBlack &&
            info.photometric != PhotometricMinIsWhite)
        {
            error = QString("Unsupported grayscale PhotometricInterpretation=%1")
                        .arg(info.photometric);
            return false;
        }
    }
    else if (info.photometric != PhotometricRgb)
    {
        error = QString("RGB raster expected PhotometricInterpretation=2, got %1")
                    .arg(info.photometric);
        return false;
    }

    return true;
}

bool decodePackBits(const QByteArray &input,
                    int expectedSize,
                    QByteArray &output,
                    QString &error)
{
    output.clear();
    output.reserve(expectedSize);

    int i = 0;

    while (i < input.size() &&
           output.size() < expectedSize)
    {
        const qint8 n =
            static_cast<qint8>(input.at(i++));

        if (n >= 0)
        {
            const int count = static_cast<int>(n) + 1;

            if (i + count > input.size())
            {
                error = "Invalid PackBits literal run";
                return false;
            }

            output.append(input.constData() + i, count);
            i += count;
        }
        else if (n >= -127)
        {
            if (i >= input.size())
            {
                error = "Invalid PackBits repeat run";
                return false;
            }

            const char value = input.at(i++);
            const int count = 1 - static_cast<int>(n);

            for (int k = 0; k < count; ++k)
                output.append(value);
        }
    }

    if (output.size() < expectedSize)
    {
        error = QString("PackBits decoded only %1 of %2 bytes")
                    .arg(output.size())
                    .arg(expectedSize);
        return false;
    }

    if (output.size() > expectedSize)
        output.truncate(expectedSize);

    return true;
}

class MsbBitReader
{
public:
    explicit MsbBitReader(const QByteArray &data)
        : m_data(data),
          m_bitPosition(0)
    {
    }

    bool read(int bits, int &value)
    {
        if (bits <= 0 ||
            m_bitPosition + bits > m_data.size() * 8)
        {
            return false;
        }

        value = 0;

        for (int i = 0; i < bits; ++i)
        {
            const int byteIndex = m_bitPosition / 8;
            const int bitIndex = 7 - (m_bitPosition % 8);
            const int bit =
                (static_cast<quint8>(m_data.at(byteIndex)) >> bitIndex) & 1;

            value = (value << 1) | bit;
            ++m_bitPosition;
        }

        return true;
    }

private:
    const QByteArray &m_data;
    int m_bitPosition;
};

bool decodeLzw(const QByteArray &input,
               int expectedSize,
               QByteArray &output,
               QString &error)
{
    QVector<QByteArray> dictionary(4096);

    for (int i = 0; i < 256; ++i)
        dictionary[i] = QByteArray(1, static_cast<char>(i));

    MsbBitReader reader(input);
    output.clear();
    output.reserve(expectedSize);

    int codeSize = 9;
    int nextCode = 258;
    int previousCode = -1;

    while (output.size() < expectedSize)
    {
        int code = 0;

        if (!reader.read(codeSize, code))
            break;

        if (code == 256)
        {
            codeSize = 9;
            nextCode = 258;
            previousCode = -1;
            continue;
        }

        if (code == 257)
            break;

        QByteArray current;

        if (code < nextCode && !dictionary.at(code).isEmpty())
        {
            current = dictionary.at(code);
        }
        else if (code == nextCode && previousCode >= 0)
        {
            current = dictionary.at(previousCode);
            current.append(current.at(0));
        }
        else
        {
            error = QString("Invalid TIFF LZW code %1").arg(code);
            return false;
        }

        output.append(current);

        if (previousCode >= 0 && nextCode < 4096)
        {
            QByteArray added = dictionary.at(previousCode);
            added.append(current.at(0));
            dictionary[nextCode++] = added;

            // TIFF LZW uses "early change".
            if (codeSize < 12 &&
                nextCode == ((1 << codeSize) - 1))
            {
                ++codeSize;
            }
        }

        previousCode = code;
    }

    if (output.size() < expectedSize)
    {
        error = QString("LZW decoded only %1 of %2 bytes")
                    .arg(output.size())
                    .arg(expectedSize);
        return false;
    }

    if (output.size() > expectedSize)
        output.truncate(expectedSize);

    return true;
}

bool decodeSegment(const QByteArray &encoded,
                   quint16 compression,
                   int expectedSize,
                   QByteArray &decoded,
                   QString &error)
{
    if (compression == CompressionNone)
    {
        if (encoded.size() < expectedSize)
        {
            error = QString("Uncompressed TIFF strip has %1 bytes, expected %2")
                        .arg(encoded.size())
                        .arg(expectedSize);
            return false;
        }

        decoded = encoded.left(expectedSize);
        return true;
    }

    if (compression == CompressionPackBits)
        return decodePackBits(encoded, expectedSize, decoded, error);

    if (compression == CompressionLzw)
        return decodeLzw(encoded, expectedSize, decoded, error);

    error = QString("Unsupported TIFF compression=%1")
                .arg(compression);
    return false;
}

quint32 rawSample(const QByteArray &data,
                  int sampleIndex,
                  int bitsPerSample,
                  bool littleEndian)
{
    if (bitsPerSample == 8)
        return static_cast<quint8>(data.at(sampleIndex));

    const int offset = sampleIndex * 2;
    return readU16(data, offset, littleEndian);
}

void setRawSample(QByteArray &data,
                  int sampleIndex,
                  int bitsPerSample,
                  bool littleEndian,
                  quint32 value)
{
    if (bitsPerSample == 8)
    {
        data[sampleIndex] =
            static_cast<char>(value & 0xff);
        return;
    }

    const int offset = sampleIndex * 2;
    const quint16 v = static_cast<quint16>(value & 0xffff);

    if (littleEndian)
    {
        data[offset] = static_cast<char>(v & 0xff);
        data[offset + 1] =
            static_cast<char>((v >> 8) & 0xff);
    }
    else
    {
        data[offset] =
            static_cast<char>((v >> 8) & 0xff);
        data[offset + 1] = static_cast<char>(v & 0xff);
    }
}

void reverseHorizontalPredictor(QByteArray &data,
                                quint32 width,
                                quint32 rows,
                                quint16 components,
                                quint16 bitsPerSample,
                                bool littleEndian)
{
    if (components == 0)
        return;

    const quint32 mask =
        bitsPerSample == 8 ? 0xffu : 0xffffu;

    for (quint32 row = 0; row < rows; ++row)
    {
        const quint32 rowBase =
            row * width * components;

        for (quint32 x = 1; x < width; ++x)
        {
            for (quint16 c = 0; c < components; ++c)
            {
                const int currentIndex =
                    static_cast<int>(
                        rowBase + x * components + c
                    );

                const int previousIndex =
                    currentIndex - components;

                const quint32 current =
                    rawSample(data,
                              currentIndex,
                              bitsPerSample,
                              littleEndian);

                const quint32 previous =
                    rawSample(data,
                              previousIndex,
                              bitsPerSample,
                              littleEndian);

                setRawSample(data,
                             currentIndex,
                             bitsPerSample,
                             littleEndian,
                             (current + previous) & mask);
            }
        }
    }
}

qint64 numericSample(const QByteArray &data,
                     int sampleIndex,
                     const ImageInfo &info)
{
    const quint32 raw =
        rawSample(data,
                  sampleIndex,
                  info.bitsPerSample,
                  info.littleEndian);

    if (info.sampleFormat == SampleFormatUint)
        return static_cast<qint64>(raw);

    if (info.bitsPerSample == 8)
        return static_cast<qint8>(raw & 0xff);

    return static_cast<qint16>(raw & 0xffff);
}

qint64 minimumSample(const ImageInfo &info)
{
    if (info.sampleFormat == SampleFormatInt)
        return info.bitsPerSample == 8 ? -128 : -32768;

    return 0;
}

int histogramSize(const ImageInfo &info)
{
    return info.bitsPerSample == 8 ? 256 : 65536;
}

bool readDecodedStrip(QFile &file,
                      const ImageInfo &info,
                      int stripIndex,
                      quint32 rows,
                      quint16 components,
                      QByteArray &decoded,
                      QString &error)
{
    if (stripIndex < 0 ||
        stripIndex >= info.stripOffsets.size())
    {
        error = QString("TIFF strip index %1 is out of range")
                    .arg(stripIndex);
        return false;
    }

    QByteArray encoded;

    if (!readAt(file,
                info.stripOffsets.at(stripIndex),
                info.stripByteCounts.at(stripIndex),
                encoded))
    {
        error = QString("Cannot read TIFF strip %1")
                    .arg(stripIndex);
        return false;
    }

    const quint64 expected64 =
        static_cast<quint64>(rows) *
        info.width *
        components *
        (info.bitsPerSample / 8);

    if (expected64 > 0x7fffffffULL)
    {
        error = "TIFF strip is too large";
        return false;
    }

    if (!decodeSegment(encoded,
                       info.compression,
                       static_cast<int>(expected64),
                       decoded,
                       error))
    {
        return false;
    }

    if (info.predictor == 2)
    {
        reverseHorizontalPredictor(decoded,
                                   info.width,
                                   rows,
                                   components,
                                   info.bitsPerSample,
                                   info.littleEndian);
    }

    return true;
}

bool noDataMatches(qint64 value,
                   bool hasNoData,
                   qint64 noData)
{
    return hasNoData && value == noData;
}

void progress(QTextStream &out,
              const QString &phase,
              int percent,
              int &last)
{
    const int bucket = (percent / 10) * 10;

    if (bucket <= last)
        return;

    last = bucket;
    out << phase << ": " << bucket << "%\n";
    out.flush();
}

ContrastBandStats limitsFromHistogram(
    const QVector<quint64> &histogram,
    quint64 validCount,
    qint64 minimum)
{
    ContrastBandStats result;
    result.validSamples = validCount;

    if (validCount == 0)
        return result;

    const quint64 clip =
        static_cast<quint64>(
            std::floor(validCount * 0.02)
        );

    quint64 sum = 0;
    int lowIndex = 0;

    for (int i = 0; i < histogram.size(); ++i)
    {
        sum += histogram.at(i);

        if (sum > clip)
        {
            lowIndex = i;
            break;
        }
    }

    sum = 0;
    int highIndex = histogram.size() - 1;

    for (int i = histogram.size() - 1; i >= 0; --i)
    {
        sum += histogram.at(i);

        if (sum > clip)
        {
            highIndex = i;
            break;
        }
    }

    result.low = minimum + lowIndex;
    result.high = minimum + highIndex;

    return result;
}

quint8 contrast(qint64 value,
                const ContrastBandStats &stats,
                bool hasNoData,
                qint64 noData)
{
    if (noDataMatches(value, hasNoData, noData))
        return 0;

    if (stats.high <= stats.low)
        return 0;

    if (value <= stats.low)
        return 0;

    if (value >= stats.high)
        return 255;

    const double t =
        static_cast<double>(value - stats.low) /
        static_cast<double>(stats.high - stats.low);

    int v = static_cast<int>(std::floor(t * 255.0 + 0.5));

    if (v < 0)
        v = 0;
    if (v > 255)
        v = 255;

    return static_cast<quint8>(v);
}

bool buildHistograms(QFile &file,
                     const ImageInfo &info,
                     bool hasNoData,
                     qint64 noData,
                     QVector<QVector<quint64> > &histograms,
                     QVector<quint64> &valid,
                     QTextStream &out,
                     QString &error)
{
    const quint32 stripsPerPlane =
        (info.height + info.rowsPerStrip - 1) /
        info.rowsPerStrip;

    const int expectedStripCount =
        info.planarConfig == PlanarContig
            ? static_cast<int>(stripsPerPlane)
            : static_cast<int>(
                  stripsPerPlane * info.samplesPerPixel
              );

    if (info.stripOffsets.size() < expectedStripCount)
    {
        error = QString("TIFF has %1 strips, expected at least %2")
                    .arg(info.stripOffsets.size())
                    .arg(expectedStripCount);
        return false;
    }

    int lastProgress = -10;

    for (quint32 s = 0; s < stripsPerPlane; ++s)
    {
        const quint32 y = s * info.rowsPerStrip;
        const quint32 rows =
            std::min(info.rowsPerStrip,
                     info.height - y);

        if (info.planarConfig == PlanarContig)
        {
            QByteArray decoded;

            if (!readDecodedStrip(file,
                                  info,
                                  static_cast<int>(s),
                                  rows,
                                  info.samplesPerPixel,
                                  decoded,
                                  error))
            {
                return false;
            }

            const quint64 pixels =
                static_cast<quint64>(rows) * info.width;

            for (quint64 p = 0; p < pixels; ++p)
            {
                for (quint16 band = 0;
                     band < info.samplesPerPixel;
                     ++band)
                {
                    const qint64 value =
                        numericSample(
                            decoded,
                            static_cast<int>(
                                p * info.samplesPerPixel +
                                band
                            ),
                            info
                        );

                    if (noDataMatches(value,
                                      hasNoData,
                                      noData))
                    {
                        continue;
                    }

                    const int index =
                        static_cast<int>(
                            value - minimumSample(info)
                        );

                    ++histograms[band][index];
                    ++valid[band];
                }
            }
        }
        else
        {
            for (quint16 band = 0;
                 band < info.samplesPerPixel;
                 ++band)
            {
                QByteArray decoded;

                const int stripIndex =
                    static_cast<int>(
                        band * stripsPerPlane + s
                    );

                if (!readDecodedStrip(file,
                                      info,
                                      stripIndex,
                                      rows,
                                      1,
                                      decoded,
                                      error))
                {
                    return false;
                }

                const quint64 pixels =
                    static_cast<quint64>(rows) *
                    info.width;

                for (quint64 p = 0; p < pixels; ++p)
                {
                    const qint64 value =
                        numericSample(
                            decoded,
                            static_cast<int>(p),
                            info
                        );

                    if (noDataMatches(value,
                                      hasNoData,
                                      noData))
                    {
                        continue;
                    }

                    const int index =
                        static_cast<int>(
                            value - minimumSample(info)
                        );

                    ++histograms[band][index];
                    ++valid[band];
                }
            }
        }

        progress(out,
                 "Histogram",
                 static_cast<int>(
                     ((s + 1) * 100u) /
                     stripsPerPlane
                 ),
                 lastProgress);
    }

    return true;
}

struct IfdEntryOut
{
    quint16 tag;
    quint16 type;
    quint32 count;
    quint32 value;
};

void appendIfdEntry(QByteArray &data,
                    const IfdEntryOut &entry)
{
    appendU16LE(data, entry.tag);
    appendU16LE(data, entry.type);
    appendU32LE(data, entry.count);

    if (entry.type == TypeShort &&
        entry.count == 1)
    {
        appendU16LE(data,
                    static_cast<quint16>(entry.value));
        appendU16LE(data, 0);
    }
    else
    {
        appendU32LE(data, entry.value);
    }
}

bool finishOutputTiff(QFile &output,
                      const ImageInfo &info,
                      quint32 rowsPerStrip,
                      const QVector<quint32> &stripOffsets,
                      const QVector<quint32> &stripByteCounts,
                      QString &error)
{
    if (stripOffsets.isEmpty() ||
        stripOffsets.size() != stripByteCounts.size())
    {
        error = "Internal output TIFF strip table error";
        return false;
    }

    const quint32 ifdOffset =
        static_cast<quint32>(output.pos());

    const int entryCount = 10;

    quint32 extraOffset =
        ifdOffset +
        2u +
        static_cast<quint32>(entryCount) * 12u +
        4u;

    quint32 bitsOffset = 8;
    QByteArray extra;

    if (info.samplesPerPixel == 3)
    {
        bitsOffset = extraOffset;

        for (int i = 0; i < 3; ++i)
            appendU16LE(extra, 8);

        extraOffset += 6;
    }

    quint32 stripOffsetsValue = stripOffsets.first();

    if (stripOffsets.size() > 1)
    {
        stripOffsetsValue = extraOffset;

        for (int i = 0; i < stripOffsets.size(); ++i)
            appendU32LE(extra, stripOffsets.at(i));

        extraOffset +=
            static_cast<quint32>(
                stripOffsets.size() * 4
            );
    }

    quint32 stripCountsValue =
        stripByteCounts.first();

    if (stripByteCounts.size() > 1)
    {
        stripCountsValue = extraOffset;

        for (int i = 0; i < stripByteCounts.size(); ++i)
            appendU32LE(extra, stripByteCounts.at(i));

        extraOffset +=
            static_cast<quint32>(
                stripByteCounts.size() * 4
            );
    }

    QVector<IfdEntryOut> entries;

    IfdEntryOut e;

    e.tag = TagImageWidth;
    e.type = TypeLong;
    e.count = 1;
    e.value = info.width;
    entries.push_back(e);

    e.tag = TagImageLength;
    e.type = TypeLong;
    e.count = 1;
    e.value = info.height;
    entries.push_back(e);

    e.tag = TagBitsPerSample;
    e.type = TypeShort;
    e.count = info.samplesPerPixel;
    e.value = bitsOffset;
    entries.push_back(e);

    e.tag = TagCompression;
    e.type = TypeShort;
    e.count = 1;
    e.value = CompressionNone;
    entries.push_back(e);

    e.tag = TagPhotometric;
    e.type = TypeShort;
    e.count = 1;
    e.value =
        info.samplesPerPixel == 1
            ? PhotometricMinIsBlack
            : PhotometricRgb;
    entries.push_back(e);

    e.tag = TagStripOffsets;
    e.type = TypeLong;
    e.count = stripOffsets.size();
    e.value = stripOffsetsValue;
    entries.push_back(e);

    e.tag = TagSamplesPerPixel;
    e.type = TypeShort;
    e.count = 1;
    e.value = info.samplesPerPixel;
    entries.push_back(e);

    e.tag = TagRowsPerStrip;
    e.type = TypeLong;
    e.count = 1;
    e.value = rowsPerStrip;
    entries.push_back(e);

    e.tag = TagStripByteCounts;
    e.type = TypeLong;
    e.count = stripByteCounts.size();
    e.value = stripCountsValue;
    entries.push_back(e);

    e.tag = TagPlanarConfig;
    e.type = TypeShort;
    e.count = 1;
    e.value = PlanarContig;
    entries.push_back(e);

    std::sort(entries.begin(),
              entries.end(),
              [](const IfdEntryOut &a,
                 const IfdEntryOut &b)
              {
                  return a.tag < b.tag;
              });

    QByteArray ifd;
    appendU16LE(ifd,
                static_cast<quint16>(entries.size()));

    for (int i = 0; i < entries.size(); ++i)
        appendIfdEntry(ifd, entries.at(i));

    appendU32LE(ifd, 0);
    ifd.append(extra);

    if (output.write(ifd) != ifd.size())
    {
        error = "Failed to write TIFF IFD";
        return false;
    }

    if (!output.seek(4))
    {
        error = "Failed to patch TIFF header";
        return false;
    }

    QByteArray offsetBytes;
    appendU32LE(offsetBytes, ifdOffset);

    if (output.write(offsetBytes) != 4)
    {
        error = "Failed to write TIFF IFD offset";
        return false;
    }

    return true;
}

bool writeContrasted(QFile &input,
                     const ImageInfo &info,
                     const QVector<ContrastBandStats> &stats,
                     bool hasNoData,
                     qint64 noData,
                     const QString &outputPath,
                     QTextStream &out,
                     QString &error)
{
    QFile output(outputPath);

    if (!output.open(QIODevice::WriteOnly |
                     QIODevice::Truncate))
    {
        error = QString("Cannot create output TIFF: %1")
                    .arg(output.errorString());
        return false;
    }

    QByteArray header;
    header.append('I');
    header.append('I');
    appendU16LE(header, 42);
    appendU32LE(header, 0);

    if (output.write(header) != header.size())
    {
        error = "Cannot write output TIFF header";
        return false;
    }

    const quint32 stripsPerPlane =
        (info.height + info.rowsPerStrip - 1) /
        info.rowsPerStrip;

    QVector<quint32> outputOffsets;
    QVector<quint32> outputCounts;

    int lastProgress = -10;

    for (quint32 s = 0; s < stripsPerPlane; ++s)
    {
        const quint32 y = s * info.rowsPerStrip;
        const quint32 rows =
            std::min(info.rowsPerStrip,
                     info.height - y);

        const quint64 pixels =
            static_cast<quint64>(rows) *
            info.width;

        QByteArray outputStrip(
            static_cast<int>(
                pixels * info.samplesPerPixel
            ),
            0
        );

        if (info.planarConfig == PlanarContig)
        {
            QByteArray decoded;

            if (!readDecodedStrip(input,
                                  info,
                                  static_cast<int>(s),
                                  rows,
                                  info.samplesPerPixel,
                                  decoded,
                                  error))
            {
                return false;
            }

            for (quint64 p = 0; p < pixels; ++p)
            {
                for (quint16 band = 0;
                     band < info.samplesPerPixel;
                     ++band)
                {
                    const qint64 value =
                        numericSample(
                            decoded,
                            static_cast<int>(
                                p * info.samplesPerPixel +
                                band
                            ),
                            info
                        );

                    outputStrip[
                        static_cast<int>(
                            p * info.samplesPerPixel +
                            band
                        )
                    ] = static_cast<char>(
                        contrast(value,
                                 stats.at(band),
                                 hasNoData,
                                 noData)
                    );
                }
            }
        }
        else
        {
            for (quint16 band = 0;
                 band < info.samplesPerPixel;
                 ++band)
            {
                QByteArray decoded;

                const int stripIndex =
                    static_cast<int>(
                        band * stripsPerPlane + s
                    );

                if (!readDecodedStrip(input,
                                      info,
                                      stripIndex,
                                      rows,
                                      1,
                                      decoded,
                                      error))
                {
                    return false;
                }

                for (quint64 p = 0; p < pixels; ++p)
                {
                    const qint64 value =
                        numericSample(
                            decoded,
                            static_cast<int>(p),
                            info
                        );

                    outputStrip[
                        static_cast<int>(
                            p * info.samplesPerPixel +
                            band
                        )
                    ] = static_cast<char>(
                        contrast(value,
                                 stats.at(band),
                                 hasNoData,
                                 noData)
                    );
                }
            }
        }

        outputOffsets.push_back(
            static_cast<quint32>(output.pos())
        );

        if (output.write(outputStrip) !=
            outputStrip.size())
        {
            error = "Failed to write output TIFF raster data";
            return false;
        }

        outputCounts.push_back(
            static_cast<quint32>(outputStrip.size())
        );

        progress(out,
                 "Writing 8-bit TIFF",
                 static_cast<int>(
                     ((s + 1) * 100u) /
                     stripsPerPlane
                 ),
                 lastProgress);
    }

    if (!finishOutputTiff(output,
                          info,
                          info.rowsPerStrip,
                          outputOffsets,
                          outputCounts,
                          error))
    {
        return false;
    }

    output.close();
    return true;
}
}

ContrastBandStats::ContrastBandStats()
    : low(0),
      high(0),
      validSamples(0)
{
}

ContrastResult::ContrastResult()
    : width(0),
      height(0),
      bitsPerSample(0),
      samplesPerPixel(0),
      hasNoData(false),
      noDataValue(0.0)
{
}

bool RasterContrast::process(const QString &inputPath,
                             const QString &outputPath,
                             bool hasNoData,
                             const QString &noDataText,
                             ContrastResult &result,
                             QTextStream &out,
                             QString &error)
{
    result = ContrastResult();
    error.clear();

    if (QFileInfo(inputPath).absoluteFilePath() ==
        QFileInfo(outputPath).absoluteFilePath())
    {
        error =
            "Output TIFF path must be different from input TIFF path.";
        return false;
    }

    double noDataValue = 0.0;
    qint64 noDataInteger = 0;

    if (hasNoData)
    {
        bool ok = false;
        noDataValue = noDataText.toDouble(&ok);

        if (!ok)
        {
            error = QString("GDAL_NODATA is not numeric: %1")
                        .arg(noDataText);
            return false;
        }

        noDataInteger =
            static_cast<qint64>(
                noDataValue >= 0.0
                    ? std::floor(noDataValue + 0.5)
                    : std::ceil(noDataValue - 0.5)
            );

        if (std::fabs(
                noDataValue -
                static_cast<double>(noDataInteger)
            ) > 1e-9)
        {
            error =
                "This task implementation expects integer GDAL_NODATA.";
            return false;
        }
    }

    QFile input(inputPath);

    if (!input.open(QIODevice::ReadOnly))
    {
        error = QString("Cannot open TIFF raster: %1")
                    .arg(input.errorString());
        return false;
    }

    ImageInfo info;

    if (!parseTiff(input, info, error))
        return false;

    const qint64 minimum = minimumSample(info);
    const qint64 maximum =
        minimum + histogramSize(info) - 1;

    if (hasNoData &&
        (noDataInteger < minimum ||
         noDataInteger > maximum))
    {
        error = QString(
            "GDAL_NODATA=%1 is outside raster sample range [%2,%3]")
            .arg(noDataText)
            .arg(minimum)
            .arg(maximum);
        return false;
    }

    result.width = static_cast<int>(info.width);
    result.height = static_cast<int>(info.height);
    result.bitsPerSample = info.bitsPerSample;
    result.samplesPerPixel = info.samplesPerPixel;
    result.hasNoData = hasNoData;
    result.noDataValue = noDataValue;

    out << "Raster: "
        << info.width << " x " << info.height
        << ", " << info.bitsPerSample << " bit"
        << ", samples=" << info.samplesPerPixel
        << ", compression=" << info.compression
        << "\n";

    if (hasNoData)
        out << "GDAL_NODATA: " << noDataText << "\n";
    else
        out << "GDAL_NODATA: not present\n";

    out.flush();

    QVector<QVector<quint64> > histograms(
        info.samplesPerPixel
    );

    QVector<quint64> valid(
        info.samplesPerPixel,
        0
    );

    const int bins = histogramSize(info);

    for (int band = 0;
         band < info.samplesPerPixel;
         ++band)
    {
        histograms[band] =
            QVector<quint64>(bins, 0);
    }

    if (!buildHistograms(input,
                         info,
                         hasNoData,
                         noDataInteger,
                         histograms,
                         valid,
                         out,
                         error))
    {
        return false;
    }

    result.bands.clear();

    for (int band = 0;
         band < info.samplesPerPixel;
         ++band)
    {
        if (valid.at(band) == 0)
        {
            error = QString(
                "Band %1 has no valid pixels after excluding GDAL_NODATA")
                .arg(band + 1);
            return false;
        }

        const ContrastBandStats stats =
            limitsFromHistogram(
                histograms.at(band),
                valid.at(band),
                minimum
            );

        result.bands.push_back(stats);

        out << "Band " << (band + 1)
            << ": valid=" << stats.validSamples
            << ", 2% low=" << stats.low
            << ", 2% high=" << stats.high
            << "\n";
    }

    out.flush();

    if (!writeContrasted(input,
                         info,
                         result.bands,
                         hasNoData,
                         noDataInteger,
                         outputPath,
                         out,
                         error))
    {
        return false;
    }

    out << "Saved 8-bit TIFF: "
        << outputPath << "\n";
    out.flush();

    return true;
}
