#ifndef GEOTIFF_H
#define GEOTIFF_H

#include <QString>
#include <QtGlobal>

struct GeoTiffMetadata
{
    GeoTiffMetadata();

    int width;
    int height;

    // GDAL-style affine transform:
    // X = gt[0] + x * gt[1] + y * gt[2]
    // Y = gt[3] + x * gt[4] + y * gt[5]
    double gt[6];

    quint16 modelType;
    quint16 rasterType;
    quint16 geographicType;
    quint16 projectedCrs;

    bool hasGeoTransform;
};

class GeoTiffReader
{
public:
    static bool read(const QString &filePath,
                     GeoTiffMetadata &metadata,
                     QString &error);
};

#endif // GEOTIFF_H
