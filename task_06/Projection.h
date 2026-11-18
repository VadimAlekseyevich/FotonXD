#ifndef PROJECTION_H
#define PROJECTION_H

#include <QString>

struct GeoTiffMetadata;

// Вычисляет преобразования между WGS84 и координатами карты в метрах.
class Projection
{
public:
// Возможные проекции: неизвестная, UTM или полярная UPS.
    enum Type
    {
        Unknown,
        UTM,
        UPS
    };

    Projection();

// Настройка по EPSG-коду или параметрам, записанным в GeoTIFF.
    bool configureFromGeoTiff(const GeoTiffMetadata &metadata,
                              QString &error);

// Настройка стандартных координатных систем по коду EPSG.
    bool configureFromEpsg(int epsgCode, QString &error);

// Из плоских координат E,N в широту и долготу.
    bool projectedToGeo(double easting,
                        double northing,
                        double &latitudeDeg,
                        double &longitudeDeg) const;

// Из широты и долготы в плоские E,N.
    bool geoToProjected(double latitudeDeg,
                        double longitudeDeg,
                        double &easting,
                        double &northing) const;

// Набор геттеров для просмотра настроенной проекции.
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

// Внешние классы не вызывают внутренние математические формулы напрямую.
private:
// Прямое и обратное преобразования UTM.
    bool utmForward(double latitudeDeg,
                    double longitudeDeg,
                    double &easting,
                    double &northing) const;

    bool utmReverse(double easting,
                    double northing,
                    double &latitudeDeg,
                    double &longitudeDeg) const;

// Прямое и обратное преобразования полярной стереографии.
    bool upsForward(double latitudeDeg,
                    double longitudeDeg,
                    double &easting,
                    double &northing) const;

    bool upsReverse(double easting,
                    double northing,
                    double &latitudeDeg,
                    double &longitudeDeg) const;

// Поля хранят тип, номер зоны, полушарие и параметры проекции.
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
