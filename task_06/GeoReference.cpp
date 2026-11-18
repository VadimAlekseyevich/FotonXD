#include "GeoReference.h"

#include <cmath>

GeoReference::GeoReference()
    : m_initialized(false)
{
}

// Без геопривязки и обратимой матрицы нельзя переводить координаты.
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

// Пиксель сначала переводится в метры карты, затем через UTM/UPS — в градусы.
bool GeoReference::planar2geo(double x,
                              double y,
                              double &latitudeDeg,
                              double &longitudeDeg) const
{
    if (!m_initialized)
        return false;

    const double easting =
        m_metadata.gt[0] +
        x * m_metadata.gt[1] +
        y * m_metadata.gt[2];

    const double northing =
        m_metadata.gt[3] +
        x * m_metadata.gt[4] +
        y * m_metadata.gt[5];

    return m_projection.projectedToGeo(
        easting,
        northing,
        latitudeDeg,
        longitudeDeg
    );
}

// В обратную сторону сначала применяем проекцию, затем обращаем аффинную матрицу.
bool GeoReference::geo2planar(double &x,
                              double &y,
                              double latitudeDeg,
                              double longitudeDeg) const
{
    if (!m_initialized)
        return false;

    double easting = 0.0;
    double northing = 0.0;

    if (!m_projection.geoToProjected(
            latitudeDeg,
            longitudeDeg,
            easting,
            northing))
    {
        return false;
    }

    // Убираем смещение карты и решаем систему из двух линейных уравнений.
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

const Projection &GeoReference::projection() const
{
    return m_projection;
}
