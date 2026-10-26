#include "RasterContrast.h"

#include <QByteArray>
#include <QFile>
#include <QTextStream>
#include <QVector>

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
struct ImageInfo
{
    uint32_t width;
    uint32_t height;
    uint16_t bitsPerSample;
    uint16_t samplesPerPixel;
    uint16_t sampleFormat;
    uint16_t planarConfig;
    uint16_t photometric;
    uint16_t orientation;

    bool tiled;
    uint32_t tileWidth;
    uint32_t tileLength;
    uint32_t rowsPerStrip;
};

TIFF *openTiff(const QString &path, const char *mode)
{
    const QByteArray localPath = QFile::encodeName(path);
    return TIFFOpen(localPath.constData(), mode);
}

bool readInfo(TIFF *tif, ImageInfo &info, QString &error)
{
    std::memset(&info, 0, sizeof(info));

    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &info.width) ||
        !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &info.height))
    {
        error = "TIFF image size tags are missing";
        return false;
    }

    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &info.bitsPerSample);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &info.samplesPerPixel);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &info.sampleFormat);
    TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &info.planarConfig);
    TIFFGetFieldDefaulted(tif, TIFFTAG_ORIENTATION, &info.orientation);

    if (!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &info.photometric))
        info.photometric =
            (info.samplesPerPixel == 1) ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB;

    if (info.bitsPerSample != 8 && info.bitsPerSample != 16)
    {
        error = QString("Unsupported BitsPerSample=%1. Task 5 supports 8-bit and 16-bit integer TIFF.")
                    .arg(info.bitsPerSample);
        return false;
    }

    if (info.sampleFormat != SAMPLEFORMAT_UINT &&
        info.sampleFormat != SAMPLEFORMAT_INT)
    {
        error = QString("Unsupported SampleFormat=%1. Task 5 supports integer rasters.")
                    .arg(info.sampleFormat);
        return false;
    }

    if (info.samplesPerPixel != 1 && info.samplesPerPixel != 3)
    {
        error = QString("Unsupported SamplesPerPixel=%1. Supported: grayscale (1) and RGB (3).")
                    .arg(info.samplesPerPixel);
        return false;
    }

    if (info.samplesPerPixel == 1)
    {
        if (info.photometric != PHOTOMETRIC_MINISBLACK &&
            info.photometric != PHOTOMETRIC_MINISWHITE)
        {
            error = QString("Unsupported grayscale PhotometricInterpretation=%1.")
                        .arg(info.photometric);
            return false;
        }
    }
    else if (info.photometric != PHOTOMETRIC_RGB)
    {
        error = QString("Three-band TIFF must use RGB photometric interpretation; got %1.")
                    .arg(info.photometric);
        return false;
    }

    if (info.orientation != ORIENTATION_TOPLEFT)
    {
        error = QString("Unsupported TIFF orientation=%1. Expected top-left orientation.")
                    .arg(info.orientation);
        return false;
    }

    info.tiled = TIFFIsTiled(tif) != 0;

    if (info.tiled)
    {
        if (!TIFFGetField(tif, TIFFTAG_TILEWIDTH, &info.tileWidth) ||
            !TIFFGetField(tif, TIFFTAG_TILELENGTH, &info.tileLength))
        {
            error = "Tiled TIFF is missing TileWidth/TileLength";
            return false;
        }
    }
    else
    {
        TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &info.rowsPerStrip);

        if (info.rowsPerStrip == 0)
            info.rowsPerStrip = info.height;
    }

    return true;
}

qint64 minSampleValue(const ImageInfo &info)
{
    if (info.sampleFormat == SAMPLEFORMAT_INT)
        return -(static_cast<qint64>(1) << (info.bitsPerSample - 1));

    return 0;
}

qint64 maxSampleValue(const ImageInfo &info)
{
    if (info.sampleFormat == SAMPLEFORMAT_INT)
        return (static_cast<qint64>(1) << (info.bitsPerSample - 1)) - 1;

    return (static_cast<qint64>(1) << info.bitsPerSample) - 1;
}

int histogramSize(const ImageInfo &info)
{
    return 1 << info.bitsPerSample;
}

int bytesPerSample(const ImageInfo &info)
{
    return info.bitsPerSample / 8;
}

qint64 decodeSample(const char *data,
                    int elementIndex,
                    const ImageInfo &info)
{
    if (info.bitsPerSample == 8)
    {
        if (info.sampleFormat == SAMPLEFORMAT_INT)
        {
            qint8 value = 0;
            std::memcpy(&value, data + elementIndex, 1);
            return value;
        }

        quint8 value = 0;
        std::memcpy(&value, data + elementIndex, 1);
        return value;
    }

    if (info.sampleFormat == SAMPLEFORMAT_INT)
    {
        qint16 value = 0;
        std::memcpy(&value,
                    data + elementIndex * static_cast<int>(sizeof(qint16)),
                    sizeof(value));
        return value;
    }

    quint16 value = 0;
    std::memcpy(&value,
                data + elementIndex * static_cast<int>(sizeof(quint16)),
                sizeof(value));
    return value;
}

int histogramIndex(qint64 value, const ImageInfo &info)
{
    return static_cast<int>(value - minSampleValue(info));
}

bool isNoData(qint64 value,
              bool hasNoData,
              qint64 noDataInteger)
{
    return hasNoData && value == noDataInteger;
}

void reportProgress(QTextStream &out,
                    const QString &phase,
                    int percent,
                    int &lastReported)
{
    const int bucket = (percent / 10) * 10;

    if (bucket <= lastReported)
        return;

    lastReported = bucket;
    out << phase << ": " << bucket << "%\n";
    out.flush();
}

bool readTile(TIFF *tif,
              QByteArray &buffer,
              uint32_t x,
              uint32_t y,
              uint16_t sample,
              QString &error)
{
    const tsize_t size = TIFFTileSize(tif);

    if (size <= 0)
    {
        error = "Invalid TIFF tile size";
        return false;
    }

    buffer.resize(static_cast<int>(size));

    if (TIFFReadTile(tif,
                     buffer.data(),
                     x,
                     y,
                     0,
                     sample) < 0)
    {
        error = QString("Failed to decode TIFF tile at (%1,%2), sample %3")
                    .arg(x)
                    .arg(y)
                    .arg(sample);
        return false;
    }

    return true;
}

bool readStrip(TIFF *tif,
               QByteArray &buffer,
               tstrip_t strip,
               QString &error)
{
    const tsize_t size = TIFFStripSize(tif);

    if (size <= 0)
    {
        error = "Invalid TIFF strip size";
        return false;
    }

    buffer.resize(static_cast<int>(size));

    const tsize_t decoded =
        TIFFReadEncodedStrip(tif, strip, buffer.data(), size);

    if (decoded < 0)
    {
        error = QString("Failed to decode TIFF strip %1")
                    .arg(static_cast<qulonglong>(strip));
        return false;
    }

    buffer.resize(static_cast<int>(decoded));
    return true;
}

bool addHistogramValue(qint64 value,
                       int sample,
                       const ImageInfo &info,
                       bool hasNoData,
                       qint64 noDataInteger,
                       QVector<QVector<quint64> > &histograms,
                       QVector<quint64> &validCounts)
{
    if (isNoData(value, hasNoData, noDataInteger))
        return true;

    const int index = histogramIndex(value, info);

    if (index < 0 || index >= histograms.at(sample).size())
        return false;

    ++histograms[sample][index];
    ++validCounts[sample];
    return true;
}

bool buildHistogramsTiled(TIFF *tif,
                          const ImageInfo &info,
                          bool hasNoData,
                          qint64 noDataInteger,
                          QVector<QVector<quint64> > &histograms,
                          QVector<quint64> &validCounts,
                          QTextStream &out,
                          QString &error)
{
    QByteArray tile;
    QVector<QByteArray> separateTiles(info.samplesPerPixel);

    const uint32_t tilesX =
        (info.width + info.tileWidth - 1) / info.tileWidth;
    const uint32_t tilesY =
        (info.height + info.tileLength - 1) / info.tileLength;
    const quint64 totalTiles =
        static_cast<quint64>(tilesX) * static_cast<quint64>(tilesY);

    quint64 done = 0;
    int lastProgress = -10;

    for (uint32_t y = 0; y < info.height; y += info.tileLength)
    {
        const uint32_t actualHeight =
            std::min(info.tileLength, info.height - y);

        for (uint32_t x = 0; x < info.width; x += info.tileWidth)
        {
            const uint32_t actualWidth =
                std::min(info.tileWidth, info.width - x);

            if (info.planarConfig == PLANARCONFIG_CONTIG)
            {
                if (!readTile(tif, tile, x, y, 0, error))
                    return false;
            }
            else
            {
                for (uint16_t sample = 0;
                     sample < info.samplesPerPixel;
                     ++sample)
                {
                    if (!readTile(tif,
                                  separateTiles[sample],
                                  x,
                                  y,
                                  sample,
                                  error))
                    {
                        return false;
                    }
                }
            }

            for (uint32_t row = 0; row < actualHeight; ++row)
            {
                for (uint32_t col = 0; col < actualWidth; ++col)
                {
                    const int pixelIndex =
                        static_cast<int>(row * info.tileWidth + col);

                    for (uint16_t sample = 0;
                         sample < info.samplesPerPixel;
                         ++sample)
                    {
                        qint64 value = 0;

                        if (info.planarConfig == PLANARCONFIG_CONTIG)
                        {
                            const int elementIndex =
                                pixelIndex * info.samplesPerPixel + sample;

                            value = decodeSample(tile.constData(),
                                                 elementIndex,
                                                 info);
                        }
                        else
                        {
                            value = decodeSample(
                                separateTiles[sample].constData(),
                                pixelIndex,
                                info
                            );
                        }

                        if (!addHistogramValue(value,
                                               sample,
                                               info,
                                               hasNoData,
                                               noDataInteger,
                                               histograms,
                                               validCounts))
                        {
                            error = "Decoded sample is outside expected integer range";
                            return false;
                        }
                    }
                }
            }

            ++done;
            const int percent =
                static_cast<int>((done * 100u) / totalTiles);
            reportProgress(out,
                           "Histogram",
                           percent,
                           lastProgress);
        }
    }

    return true;
}

bool buildHistogramsStripped(TIFF *tif,
                             const ImageInfo &info,
                             bool hasNoData,
                             qint64 noDataInteger,
                             QVector<QVector<quint64> > &histograms,
                             QVector<quint64> &validCounts,
                             QTextStream &out,
                             QString &error)
{
    QByteArray strip;
    QVector<QByteArray> separateStrips(info.samplesPerPixel);

    const uint32_t stripsY =
        (info.height + info.rowsPerStrip - 1) / info.rowsPerStrip;

    int lastProgress = -10;

    for (uint32_t stripY = 0; stripY < stripsY; ++stripY)
    {
        const uint32_t y = stripY * info.rowsPerStrip;
        const uint32_t actualRows =
            std::min(info.rowsPerStrip, info.height - y);

        if (info.planarConfig == PLANARCONFIG_CONTIG)
        {
            const tstrip_t stripIndex =
                TIFFComputeStrip(tif, y, 0);

            if (!readStrip(tif, strip, stripIndex, error))
                return false;

            const quint64 requiredBytes =
                static_cast<quint64>(actualRows) *
                info.width *
                info.samplesPerPixel *
                bytesPerSample(info);

            if (static_cast<quint64>(strip.size()) < requiredBytes)
            {
                error = "Decoded TIFF strip is shorter than expected";
                return false;
            }
        }
        else
        {
            for (uint16_t sample = 0;
                 sample < info.samplesPerPixel;
                 ++sample)
            {
                const tstrip_t stripIndex =
                    TIFFComputeStrip(tif, y, sample);

                if (!readStrip(tif,
                               separateStrips[sample],
                               stripIndex,
                               error))
                {
                    return false;
                }

                const quint64 requiredBytes =
                    static_cast<quint64>(actualRows) *
                    info.width *
                    bytesPerSample(info);

                if (static_cast<quint64>(
                        separateStrips[sample].size()) < requiredBytes)
                {
                    error = "Decoded planar TIFF strip is shorter than expected";
                    return false;
                }
            }
        }

        for (uint32_t row = 0; row < actualRows; ++row)
        {
            for (uint32_t col = 0; col < info.width; ++col)
            {
                const int pixelIndex =
                    static_cast<int>(row * info.width + col);

                for (uint16_t sample = 0;
                     sample < info.samplesPerPixel;
                     ++sample)
                {
                    qint64 value = 0;

                    if (info.planarConfig == PLANARCONFIG_CONTIG)
                    {
                        const int elementIndex =
                            pixelIndex * info.samplesPerPixel + sample;

                        value = decodeSample(strip.constData(),
                                             elementIndex,
                                             info);
                    }
                    else
                    {
                        value = decodeSample(
                            separateStrips[sample].constData(),
                            pixelIndex,
                            info
                        );
                    }

                    if (!addHistogramValue(value,
                                           sample,
                                           info,
                                           hasNoData,
                                           noDataInteger,
                                           histograms,
                                           validCounts))
                    {
                        error = "Decoded sample is outside expected integer range";
                        return false;
                    }
                }
            }
        }

        const int percent =
            static_cast<int>(
                (static_cast<quint64>(stripY + 1) * 100u) /
                stripsY
            );

        reportProgress(out,
                       "Histogram",
                       percent,
                       lastProgress);
    }

    return true;
}

ContrastBandStats calculateLimits(const QVector<quint64> &histogram,
                                  quint64 validCount,
                                  qint64 minimumValue)
{
    ContrastBandStats stats;
    stats.validSamples = validCount;

    if (validCount == 0)
        return stats;

    const quint64 clip =
        static_cast<quint64>(
            std::floor(static_cast<double>(validCount) * 0.02)
        );

    quint64 cumulative = 0;
    int lowIndex = 0;

    for (int i = 0; i < histogram.size(); ++i)
    {
        cumulative += histogram.at(i);

        if (cumulative > clip)
        {
            lowIndex = i;
            break;
        }
    }

    cumulative = 0;
    int highIndex = histogram.size() - 1;

    for (int i = histogram.size() - 1; i >= 0; --i)
    {
        cumulative += histogram.at(i);

        if (cumulative > clip)
        {
            highIndex = i;
            break;
        }
    }

    stats.low = minimumValue + lowIndex;
    stats.high = minimumValue + highIndex;

    return stats;
}

quint8 contrastValue(qint64 value,
                     const ContrastBandStats &stats,
                     bool hasNoData,
                     qint64 noDataInteger)
{
    if (isNoData(value, hasNoData, noDataInteger))
        return 0;

    if (stats.high <= stats.low)
        return 0;

    if (value <= stats.low)
        return 0;

    if (value >= stats.high)
        return 255;

    const double normalized =
        static_cast<double>(value - stats.low) /
        static_cast<double>(stats.high - stats.low);

    int result =
        static_cast<int>(std::floor(normalized * 255.0 + 0.5));

    if (result < 0)
        result = 0;
    if (result > 255)
        result = 255;

    return static_cast<quint8>(result);
}

bool configureOutput(TIFF *outTiff,
                     const ImageInfo &info,
                     QString &error)
{
    if (!TIFFSetField(outTiff, TIFFTAG_IMAGEWIDTH, info.width) ||
        !TIFFSetField(outTiff, TIFFTAG_IMAGELENGTH, info.height) ||
        !TIFFSetField(outTiff, TIFFTAG_BITSPERSAMPLE, 8) ||
        !TIFFSetField(outTiff, TIFFTAG_SAMPLESPERPIXEL, info.samplesPerPixel) ||
        !TIFFSetField(outTiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT) ||
        !TIFFSetField(outTiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
        !TIFFSetField(outTiff, TIFFTAG_PHOTOMETRIC, info.photometric) ||
        !TIFFSetField(outTiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) ||
        !TIFFSetField(outTiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE))
    {
        error = "Failed to configure output TIFF";
        return false;
    }

    TIFFSetField(outTiff,
                 TIFFTAG_SOFTWARE,
                 "Foton task 5 - 2 percent histogram contrast");

    if (info.tiled)
    {
        if (!TIFFSetField(outTiff, TIFFTAG_TILEWIDTH, info.tileWidth) ||
            !TIFFSetField(outTiff, TIFFTAG_TILELENGTH, info.tileLength))
        {
            error = "Failed to configure output TIFF tiles";
            return false;
        }
    }
    else
    {
        if (!TIFFSetField(outTiff,
                          TIFFTAG_ROWSPERSTRIP,
                          info.rowsPerStrip))
        {
            error = "Failed to configure output TIFF strips";
            return false;
        }
    }

    return true;
}

bool writeContrastedTiled(TIFF *input,
                          TIFF *output,
                          const ImageInfo &info,
                          const QVector<ContrastBandStats> &stats,
                          bool hasNoData,
                          qint64 noDataInteger,
                          QTextStream &out,
                          QString &error)
{
    QByteArray tile;
    QVector<QByteArray> separateTiles(info.samplesPerPixel);

    const int outputTileBytes =
        static_cast<int>(
            static_cast<quint64>(info.tileWidth) *
            info.tileLength *
            info.samplesPerPixel
        );

    QByteArray outputTile(outputTileBytes, 0);

    const uint32_t tilesX =
        (info.width + info.tileWidth - 1) / info.tileWidth;
    const uint32_t tilesY =
        (info.height + info.tileLength - 1) / info.tileLength;
    const quint64 totalTiles =
        static_cast<quint64>(tilesX) * static_cast<quint64>(tilesY);

    quint64 done = 0;
    int lastProgress = -10;

    for (uint32_t y = 0; y < info.height; y += info.tileLength)
    {
        const uint32_t actualHeight =
            std::min(info.tileLength, info.height - y);

        for (uint32_t x = 0; x < info.width; x += info.tileWidth)
        {
            const uint32_t actualWidth =
                std::min(info.tileWidth, info.width - x);

            outputTile.fill(0);

            if (info.planarConfig == PLANARCONFIG_CONTIG)
            {
                if (!readTile(input, tile, x, y, 0, error))
                    return false;
            }
            else
            {
                for (uint16_t sample = 0;
                     sample < info.samplesPerPixel;
                     ++sample)
                {
                    if (!readTile(input,
                                  separateTiles[sample],
                                  x,
                                  y,
                                  sample,
                                  error))
                    {
                        return false;
                    }
                }
            }

            for (uint32_t row = 0; row < actualHeight; ++row)
            {
                for (uint32_t col = 0; col < actualWidth; ++col)
                {
                    const int pixelIndex =
                        static_cast<int>(row * info.tileWidth + col);

                    for (uint16_t sample = 0;
                         sample < info.samplesPerPixel;
                         ++sample)
                    {
                        qint64 value = 0;

                        if (info.planarConfig == PLANARCONFIG_CONTIG)
                        {
                            value = decodeSample(
                                tile.constData(),
                                pixelIndex * info.samplesPerPixel + sample,
                                info
                            );
                        }
                        else
                        {
                            value = decodeSample(
                                separateTiles[sample].constData(),
                                pixelIndex,
                                info
                            );
                        }

                        outputTile[
                            pixelIndex * info.samplesPerPixel + sample
                        ] = static_cast<char>(
                            contrastValue(value,
                                          stats.at(sample),
                                          hasNoData,
                                          noDataInteger)
                        );
                    }
                }
            }

            if (TIFFWriteTile(output,
                              outputTile.data(),
                              x,
                              y,
                              0,
                              0) < 0)
            {
                error = QString("Failed to write output TIFF tile at (%1,%2)")
                            .arg(x)
                            .arg(y);
                return false;
            }

            ++done;
            const int percent =
                static_cast<int>((done * 100u) / totalTiles);

            reportProgress(out,
                           "Writing 8-bit TIFF",
                           percent,
                           lastProgress);
        }
    }

    return true;
}

bool writeContrastedStripped(TIFF *input,
                             TIFF *output,
                             const ImageInfo &info,
                             const QVector<ContrastBandStats> &stats,
                             bool hasNoData,
                             qint64 noDataInteger,
                             QTextStream &out,
                             QString &error)
{
    QByteArray strip;
    QVector<QByteArray> separateStrips(info.samplesPerPixel);

    const uint32_t stripsY =
        (info.height + info.rowsPerStrip - 1) / info.rowsPerStrip;

    int lastProgress = -10;

    for (uint32_t stripY = 0; stripY < stripsY; ++stripY)
    {
        const uint32_t y = stripY * info.rowsPerStrip;
        const uint32_t actualRows =
            std::min(info.rowsPerStrip, info.height - y);

        if (info.planarConfig == PLANARCONFIG_CONTIG)
        {
            const tstrip_t stripIndex =
                TIFFComputeStrip(input, y, 0);

            if (!readStrip(input, strip, stripIndex, error))
                return false;
        }
        else
        {
            for (uint16_t sample = 0;
                 sample < info.samplesPerPixel;
                 ++sample)
            {
                const tstrip_t stripIndex =
                    TIFFComputeStrip(input, y, sample);

                if (!readStrip(input,
                               separateStrips[sample],
                               stripIndex,
                               error))
                {
                    return false;
                }
            }
        }

        const quint64 pixelCount =
            static_cast<quint64>(actualRows) * info.width;

        QByteArray outputStrip(
            static_cast<int>(pixelCount * info.samplesPerPixel),
            0
        );

        for (quint64 pixel = 0; pixel < pixelCount; ++pixel)
        {
            for (uint16_t sample = 0;
                 sample < info.samplesPerPixel;
                 ++sample)
            {
                qint64 value = 0;

                if (info.planarConfig == PLANARCONFIG_CONTIG)
                {
                    value = decodeSample(
                        strip.constData(),
                        static_cast<int>(
                            pixel * info.samplesPerPixel + sample
                        ),
                        info
                    );
                }
                else
                {
                    value = decodeSample(
                        separateStrips[sample].constData(),
                        static_cast<int>(pixel),
                        info
                    );
                }

                outputStrip[
                    static_cast<int>(
                        pixel * info.samplesPerPixel + sample
                    )
                ] = static_cast<char>(
                    contrastValue(value,
                                  stats.at(sample),
                                  hasNoData,
                                  noDataInteger)
                );
            }
        }

        const tstrip_t outputStripIndex =
            TIFFComputeStrip(output, y, 0);

        const tsize_t written =
            TIFFWriteEncodedStrip(
                output,
                outputStripIndex,
                outputStrip.data(),
                static_cast<tsize_t>(outputStrip.size())
            );

        if (written < 0)
        {
            error = QString("Failed to write output TIFF strip %1")
                        .arg(static_cast<qulonglong>(outputStripIndex));
            return false;
        }

        const int percent =
            static_cast<int>(
                (static_cast<quint64>(stripY + 1) * 100u) /
                stripsY
            );

        reportProgress(out,
                       "Writing 8-bit TIFF",
                       percent,
                       lastProgress);
    }

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

    double noDataValue = 0.0;
    bool noDataNumeric = false;

    if (hasNoData)
    {
        noDataValue = noDataText.toDouble(&noDataNumeric);

        if (!noDataNumeric)
        {
            error = QString("GDAL_NODATA is not a numeric integer value: %1")
                        .arg(noDataText);
            return false;
        }
    }

    TIFF *input = openTiff(inputPath, "r");

    if (!input)
    {
        error = "libtiff cannot open input TIFF";
        return false;
    }

    ImageInfo info;

    if (!readInfo(input, info, error))
    {
        TIFFClose(input);
        return false;
    }

    const qint64 minimum = minSampleValue(info);
    const qint64 maximum = maxSampleValue(info);

    qint64 noDataInteger = 0;

    if (hasNoData)
    {
        noDataInteger =
            static_cast<qint64>(
                noDataValue >= 0.0
                    ? std::floor(noDataValue + 0.5)
                    : std::ceil(noDataValue - 0.5)
            );

        if (std::fabs(noDataValue - static_cast<double>(noDataInteger)) > 1e-9 ||
            noDataInteger < minimum ||
            noDataInteger > maximum)
        {
            error =
                QString("GDAL_NODATA=%1 cannot be represented by this integer raster.")
                    .arg(noDataText);
            TIFFClose(input);
            return false;
        }
    }

    result.width = static_cast<int>(info.width);
    result.height = static_cast<int>(info.height);
    result.bitsPerSample = info.bitsPerSample;
    result.samplesPerPixel = info.samplesPerPixel;
    result.hasNoData = hasNoData;
    result.noDataValue = noDataValue;

    const int bins = histogramSize(info);

    QVector<QVector<quint64> > histograms(info.samplesPerPixel);
    QVector<quint64> validCounts(info.samplesPerPixel, 0);

    for (int sample = 0; sample < info.samplesPerPixel; ++sample)
        histograms[sample] = QVector<quint64>(bins, 0);

    out << "Raster: "
        << info.width << " x " << info.height
        << ", " << info.bitsPerSample << " bit"
        << ", samples=" << info.samplesPerPixel
        << (info.tiled ? ", tiled" : ", stripped")
        << "\n";

    if (hasNoData)
        out << "GDAL_NODATA: " << noDataText << "\n";
    else
        out << "GDAL_NODATA: not present\n";

    out.flush();

    bool histogramOk = false;

    if (info.tiled)
    {
        histogramOk =
            buildHistogramsTiled(input,
                                 info,
                                 hasNoData,
                                 noDataInteger,
                                 histograms,
                                 validCounts,
                                 out,
                                 error);
    }
    else
    {
        histogramOk =
            buildHistogramsStripped(input,
                                    info,
                                    hasNoData,
                                    noDataInteger,
                                    histograms,
                                    validCounts,
                                    out,
                                    error);
    }

    TIFFClose(input);

    if (!histogramOk)
        return false;

    result.bands.clear();

    for (int sample = 0; sample < info.samplesPerPixel; ++sample)
    {
        if (validCounts.at(sample) == 0)
        {
            error = QString("Band %1 contains no valid pixels after excluding GDAL_NODATA.")
                        .arg(sample + 1);
            return false;
        }

        const ContrastBandStats stats =
            calculateLimits(histograms.at(sample),
                            validCounts.at(sample),
                            minimum);

        result.bands.push_back(stats);

        out << "Band " << (sample + 1)
            << ": valid=" << stats.validSamples
            << ", 2% low=" << stats.low
            << ", 2% high=" << stats.high
            << "\n";
    }

    out.flush();

    input = openTiff(inputPath, "r");

    if (!input)
    {
        error = "libtiff cannot reopen input TIFF for conversion";
        return false;
    }

    TIFF *output = openTiff(outputPath, "w");

    if (!output)
    {
        TIFFClose(input);
        error = "libtiff cannot create output TIFF";
        return false;
    }

    if (!configureOutput(output, info, error))
    {
        TIFFClose(output);
        TIFFClose(input);
        return false;
    }

    bool writeOk = false;

    if (info.tiled)
    {
        writeOk =
            writeContrastedTiled(input,
                                 output,
                                 info,
                                 result.bands,
                                 hasNoData,
                                 noDataInteger,
                                 out,
                                 error);
    }
    else
    {
        writeOk =
            writeContrastedStripped(input,
                                    output,
                                    info,
                                    result.bands,
                                    hasNoData,
                                    noDataInteger,
                                    out,
                                    error);
    }

    TIFFClose(output);
    TIFFClose(input);

    if (!writeOk)
        return false;

    out << "Saved 8-bit TIFF: " << outputPath << "\n";
    out.flush();

    return true;
}
