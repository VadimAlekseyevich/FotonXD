#ifndef RASTERCONTRAST_H
#define RASTERCONTRAST_H

#include <QString>
#include <QVector>
#include <QtGlobal>

class QTextStream;

struct ContrastBandStats
{
    ContrastBandStats();

    qint64 low;
    qint64 high;
    quint64 validSamples;
};

struct ContrastResult
{
    ContrastResult();

    int width;
    int height;
    int bitsPerSample;
    int samplesPerPixel;

    bool hasNoData;
    double noDataValue;

    QVector<ContrastBandStats> bands;
};

class RasterContrast
{
public:
    static bool process(const QString &inputPath,
                        const QString &outputPath,
                        bool hasNoData,
                        const QString &noDataText,
                        ContrastResult &result,
                        QTextStream &out,
                        QString &error);
};

#endif // RASTERCONTRAST_H
