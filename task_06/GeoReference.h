#ifndef GEOREFERENCE_H
#define GEOREFERENCE_H

#include "GeoTiff.h"
#include "Projection.h"

#include <QString>

// Преобразования: пиксель GeoTIFF ↔ координаты карты ↔ широта/долгота.
class GeoReference
{
public:
    GeoReference();

    bool initialize(const GeoTiffMetadata &metadata, QString &error);

    // (0,0) соответствует верхнему левому углу первого пикселя.
    bool planar2geo(double x,
                    double y,
                    double &latitudeDeg,
                    double &longitudeDeg) const;

    bool geo2planar(double &x,
                    double &y,
                    double latitudeDeg,
                    double longitudeDeg) const;

    const Projection &projection() const;

private:
    GeoTiffMetadata m_metadata;
    Projection m_projection;
    bool m_initialized;
};

#endif // GEOREFERENCE_H
