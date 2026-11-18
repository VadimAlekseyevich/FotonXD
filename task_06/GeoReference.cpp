#include "GeoReference.h"

#include <cmath>

// Пока объект не настроен по GeoTIFF, преобразования запрещены.
GeoReference::GeoReference()
    : m_initialized(false)
{
}

// Проверяем наличие геопривязки и определитель матрицы пиксели → метры.
bool GeoReference::initialize(const GeoTiffMetadata &metadata, QString &error)
{
    error.clear();

    if (!metadata.hasGeoTransform)
    {
        error = "GeoTIFF geotransform is missing";
        return false;
    }

    if (!m_projection.configureFromGeoTiff(metadata, error))
        return false;

// Нулевой определитель означает, что обратное аффинное преобразование невозможно.
    const double determinant =
        metadata.gt[1] * metadata.gt[5] -
        metadata.gt[2] * metadata.gt[4];

    if (std::fabs(determinant) < 1e-20)
    {
        error = "GeoTIFF geotransform is not invertible";
        return false;
    }

    m_metadata = metadata;
    m_initialized = true;
    return true;
}

// Пиксельные координаты преобразуются в E,N, затем в геодезические градусы.
bool GeoReference::planar2geo(double x,
                              double y,
                              double &latitudeDeg,
                              double &longitudeDeg) const
{
    if (!m_initialized)
        return false;

// Формула аффинной геопривязки: смещение, размер пикселя и возможный поворот.
    const double easting =
        m_metadata.gt[0] +
        x * m_metadata.gt[1] +
        y * m_metadata.gt[2];

    const double northing =
        m_metadata.gt[3] +
        x * m_metadata.gt[4] +
        y * m_metadata.gt[5];

// Обратная формула UTM/UPS превращает плоские метры в широту/долготу.
    return m_projection.projectedToGeo(
        easting,
        northing,
        latitudeDeg,
        longitudeDeg
    );
}

// Выполняем обратную цепочку: градусы → метры → пиксельные координаты.
bool GeoReference::geo2planar(double &x,
                              double &y,
                              double latitudeDeg,
                              double longitudeDeg) const
{
    if (!m_initialized)
        return false;

    double easting = 0.0;
    double northing = 0.0;

// Прямая картографическая проекция на плоскость.
    if (!m_projection.geoToProjected(
            latitudeDeg,
            longitudeDeg,
            easting,
            northing))
    {
        return false;
    }

// Вычитаем координаты начала растра перед обращением линейного преобразования.
    const double dx = easting - m_metadata.gt[0];
    const double dy = northing - m_metadata.gt[3];

    const double determinant =
        m_metadata.gt[1] * m_metadata.gt[5] -
        m_metadata.gt[2] * m_metadata.gt[4];

    if (std::fabs(determinant) < 1e-20)
        return false;

    x =
        (m_metadata.gt[5] * dx -
         m_metadata.gt[2] * dy) /
        determinant;

    y =
        (-m_metadata.gt[4] * dx +
          m_metadata.gt[1] * dy) /
        determinant;

    return true;
}

// Возвращаем настроенный объект проекции по постоянной ссылке.
const Projection &GeoReference::projection() const
{
    return m_projection;
}
