#ifndef RASTERCONTRAST_H
#define RASTERCONTRAST_H

#include <QString>
#include <QVector>
#include <QtGlobal>

class QTextStream;
class QImage;

// Границы 2%-контрастирования и количество действительных пикселей одного канала.
struct ContrastBandStats
{
    ContrastBandStats();

    qint64 low;
    qint64 high;
    quint64 validSamples;
};

// Информация о растре и статистика всех цветовых каналов.
struct ContrastResult
{
    ContrastResult();

    int width;
    int height;
    int bitsPerSample;
    int samplesPerPixel;

    bool hasNoData;
    double noDataValue;

// Для RGB у красного, зелёного и синего канала независимые пороги.
    QVector<ContrastBandStats> bands;
};

// Фасад чтения TIFF, расчёта контраста и подготовки текстуры маршрута.
class RasterContrast
{
public:
// Статическая функция передаёт данные через ссылки; создание объекта не требуется.
    static bool process(const QString &inputPath,
                        const QString &outputPath,
                        bool hasNoData,
                        const QString &noDataText,
                        ContrastResult &result,
                        QTextStream &out,
                        QString &error,
                        QImage *routePreview = 0,
                        int maxPreviewDimension = 2048);
};

#endif // RASTERCONTRAST_H
