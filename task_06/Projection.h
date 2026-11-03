#ifndef PROJECTION_H
#define PROJECTION_H

#include <QString>

struct GeoTiffMetadata;

class Projection
{
public:
    enum Type
    {
        Unknown,
        UTM,
        UPS
    };

    Projection();

    bool configureFromGeoTiff(const GeoTiffMetadata &metadata,
                              QString &error);

    bool configureFromEpsg(int epsgCode, QString &error);

    bool projectedToGeo(double easting,
                        double northing,
                        double &latitudeDeg,
                        double &longitudeDeg) const;

    bool geoToProjected(double latitudeDeg,
                        double longitudeDeg,
                        double &easting,
                        double &northing) const;

    Type type() const;
    int epsgCode() const;
    int utmZone() const;
    bool northHemisphere() const;

    double latitudeOfOriginDeg() const;
    double longitudeOfOriginDeg() const;
    double scaleFactor() const;
    double falseEasting() const;
    double falseNorthing() const;
    double semiMajorAxis() const;
    double inverseFlattening() const;

    QString description() const;

private:
    bool utmForward(double latitudeDeg,
                    double longitudeDeg,
                    double &easting,
                    double &northing) const;

    bool utmReverse(double easting,
                    double northing,
                    double &latitudeDeg,
                    double &longitudeDeg) const;

    bool upsForward(double latitudeDeg,
                    double longitudeDeg,
                    double &easting,
                    double &northing) const;

    bool upsReverse(double easting,
                    double northing,
                    double &latitudeDeg,
                    double &longitudeDeg) const;

    Type m_type;
    int m_epsgCode;
    int m_utmZone;
    bool m_northHemisphere;

    double m_latitudeOfOriginDeg;
    double m_longitudeOfOriginDeg;
    double m_scaleFactor;
    double m_falseEasting;
    double m_falseNorthing;
};

#endif // PROJECTION_H
