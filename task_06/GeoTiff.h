#ifndef GEOTIFF_H
#define GEOTIFF_H

#include <QString>
#include <QtGlobal>

// Сводные метаданные: размеры, аффинная привязка, GeoKeys, проекция и NoData.
struct GeoTiffMetadata
{
    GeoTiffMetadata();

    int width;
    int height;

    // Формат аффинного преобразования GDAL: из пикселей в координаты карты.
    // X = gt[0] + x * gt[1] + y * gt[2]
    // Y = gt[3] + x * gt[4] + y * gt[5]
// Шесть коэффициентов связывают координаты пикселя с плоскими координатами карты.
    double gt[6];

// Географические типы и EPSG-коды читаются из GeoKeyDirectory.
    quint16 modelType;
    quint16 rasterType;
    quint16 geographicType;
    quint16 projectedCrs;

    quint16 ellipsoid;
    quint16 coordinateTransform;
    quint16 linearUnits;

    double semiMajorAxis;
    double inverseFlattening;

// Параметры пользовательской UTM/полярной проекции в градусах и метрах.
    double latitudeOfOrigin;
    double longitudeOfOrigin;
    double straightVertPoleLongitude;
    double scaleAtNaturalOrigin;
    double falseEasting;
    double falseNorthing;

// Отдельные флаги говорят, какие параметры действительно записаны в файле.
    bool hasSemiMajorAxis;
    bool hasInverseFlattening;
    bool hasLatitudeOfOrigin;
    bool hasLongitudeOfOrigin;
    bool hasStraightVertPoleLongitude;
    bool hasScaleAtNaturalOrigin;
    bool hasFalseEasting;
    bool hasFalseNorthing;

// Значение отсутствующих данных применяется при контрастировании и прозрачности.
    bool hasNoData;
    QString noDataText;

    bool hasGeoTransform;
};

// Чтение географических метаданных TIFF, но не всех пикселей изображения.
class GeoTiffReader
{
public:
    static bool read(const QString &filePath,
                     GeoTiffMetadata &metadata,
                     QString &error);
};

#endif // GEOTIFF_H
