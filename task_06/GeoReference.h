#ifndef GEOREFERENCE_H
#define GEOREFERENCE_H

#include "GeoTiff.h"
#include "Projection.h"

#include <QString>

// Связывает аффинные координаты пикселей с математикой UTM/UPS.
class GeoReference
{
public:
    GeoReference();

// Проверяем метаданные, определяем проекцию и обратимость аффинной матрицы.
    bool initialize(const GeoTiffMetadata &metadata, QString &error);

    // Координаты (0,0) соответствуют верхнему левому углу самого первого пикселя.
// Из пикселя (x,y) в широту/долготу через E,N в метрах.
    bool planar2geo(double x,
                    double y,
                    double &latitudeDeg,
                    double &longitudeDeg) const;

// Из широты/долготы обратно в координаты пикселя.
    bool geo2planar(double &x,
                    double &y,
                    double latitudeDeg,
                    double longitudeDeg) const;

// Доступ к объекту проекции для чтения его параметров.
    const Projection &projection() const;

private:
// Храним метаданные и вычислитель проекции для дальнейших запросов.
    GeoTiffMetadata m_metadata;
    Projection m_projection;
    bool m_initialized;
};

#endif // GEOREFERENCE_H
