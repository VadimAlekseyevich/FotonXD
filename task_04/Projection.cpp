#include "Projection.h"

#include <cmath>

namespace
{
const double Pi = 3.1415926535897932384626433832795;
const double A = 6378137.0;
const double InvF = 298.257223563;
const double F = 1.0 / InvF;
const double E = std::sqrt(F * (2.0 - F));

double degToRad(double value)
{
    return value * Pi / 180.0;
}

double radToDeg(double value)
{
    return value * 180.0 / Pi;
}

double asinhCompat(double x)
{
    return std::log(x + std::sqrt(x * x + 1.0));
}

double atanhCompat(double x)
{
    return 0.5 * std::log((1.0 + x) / (1.0 - x));
}

double clampUnit(double value)
{
    if (value < -1.0)
        return -1.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

double wrapRadians(double value)
{
    while (value > Pi)
        value -= 2.0 * Pi;
    while (value < -Pi)
        value += 2.0 * Pi;
    return value;
}
}

Projection::Projection()
    : m_type(Unknown),
      m_epsgCode(0),
      m_utmZone(0),
      m_northHemisphere(true),
      m_latitudeOfOriginDeg(0.0),
      m_longitudeOfOriginDeg(0.0),
      m_scaleFactor(1.0),
      m_falseEasting(0.0),
      m_falseNorthing(0.0)
{
}

bool Projection::configureFromEpsg(int epsgCode, QString &error)
{
    error.clear();

    m_type = Unknown;
    m_epsgCode = epsgCode;
    m_utmZone = 0;
    m_northHemisphere = true;

    if (epsgCode >= 32601 && epsgCode <= 32660)
    {
        m_type = UTM;
        m_utmZone = epsgCode - 32600;
        m_northHemisphere = true;
        m_latitudeOfOriginDeg = 0.0;
        m_longitudeOfOriginDeg = m_utmZone * 6.0 - 183.0;
        m_scaleFactor = 0.9996;
        m_falseEasting = 500000.0;
        m_falseNorthing = 0.0;
        return true;
    }

    if (epsgCode >= 32701 && epsgCode <= 32760)
    {
        m_type = UTM;
        m_utmZone = epsgCode - 32700;
        m_northHemisphere = false;
        m_latitudeOfOriginDeg = 0.0;
        m_longitudeOfOriginDeg = m_utmZone * 6.0 - 183.0;
        m_scaleFactor = 0.9996;
        m_falseEasting = 500000.0;
        m_falseNorthing = 10000000.0;
        return true;
    }

    if (epsgCode == 32661 || epsgCode == 32761)
    {
        m_type = UPS;
        m_northHemisphere = (epsgCode == 32661);
        m_latitudeOfOriginDeg = m_northHemisphere ? 90.0 : -90.0;
        m_longitudeOfOriginDeg = 0.0;
        m_scaleFactor = 0.994;
        m_falseEasting = 2000000.0;
        m_falseNorthing = 2000000.0;
        return true;
    }

    error = QString("Unsupported projected CRS EPSG:%1. "
                    "Supported: WGS84 UTM 32601..32660 / 32701..32760 "
                    "and WGS84 UPS 32661 / 32761.")
                .arg(epsgCode);
    return false;
}

bool Projection::projectedToGeo(double easting,
                                double northing,
                                double &latitudeDeg,
                                double &longitudeDeg) const
{
    if (m_type == UTM)
        return utmReverse(easting, northing, latitudeDeg, longitudeDeg);

    if (m_type == UPS)
        return upsReverse(easting, northing, latitudeDeg, longitudeDeg);

    return false;
}

bool Projection::geoToProjected(double latitudeDeg,
                                double longitudeDeg,
                                double &easting,
                                double &northing) const
{
    if (m_type == UTM)
        return utmForward(latitudeDeg, longitudeDeg, easting, northing);

    if (m_type == UPS)
        return upsForward(latitudeDeg, longitudeDeg, easting, northing);

    return false;
}

bool Projection::utmForward(double latitudeDeg,
                            double longitudeDeg,
                            double &easting,
                            double &northing) const
{
    const double phi = degToRad(latitudeDeg);
    const double lambda = degToRad(longitudeDeg);
    const double lambda0 = degToRad(m_longitudeOfOriginDeg);

    const double n = F / (2.0 - F);
    const double n2 = n * n;
    const double n3 = n2 * n;
    const double n4 = n2 * n2;

    const double B =
        A / (1.0 + n) * (1.0 + n2 / 4.0 + n4 / 64.0);

    const double h1 =
        n / 2.0 - (2.0 / 3.0) * n2 +
        (5.0 / 16.0) * n3 + (41.0 / 180.0) * n4;

    const double h2 =
        (13.0 / 48.0) * n2 -
        (3.0 / 5.0) * n3 +
        (557.0 / 1440.0) * n4;

    const double h3 =
        (61.0 / 240.0) * n3 -
        (103.0 / 140.0) * n4;

    const double h4 =
        (49561.0 / 161280.0) * n4;

    const double q =
        asinhCompat(std::tan(phi)) -
        E * atanhCompat(E * std::sin(phi));

    const double beta = std::atan(std::sinh(q));
    const double deltaLambda = wrapRadians(lambda - lambda0);

    const double eta0 =
        atanhCompat(std::cos(beta) * std::sin(deltaLambda));

    const double xi0 =
        std::asin(clampUnit(std::sin(beta) * std::cosh(eta0)));

    const double xi =
        xi0 +
        h1 * std::sin(2.0 * xi0) * std::cosh(2.0 * eta0) +
        h2 * std::sin(4.0 * xi0) * std::cosh(4.0 * eta0) +
        h3 * std::sin(6.0 * xi0) * std::cosh(6.0 * eta0) +
        h4 * std::sin(8.0 * xi0) * std::cosh(8.0 * eta0);

    const double eta =
        eta0 +
        h1 * std::cos(2.0 * xi0) * std::sinh(2.0 * eta0) +
        h2 * std::cos(4.0 * xi0) * std::sinh(4.0 * eta0) +
        h3 * std::cos(6.0 * xi0) * std::sinh(6.0 * eta0) +
        h4 * std::cos(8.0 * xi0) * std::sinh(8.0 * eta0);

    easting = m_falseEasting + m_scaleFactor * B * eta;
    northing = m_falseNorthing + m_scaleFactor * B * xi;

    return std::isfinite(easting) && std::isfinite(northing);
}

bool Projection::utmReverse(double easting,
                            double northing,
                            double &latitudeDeg,
                            double &longitudeDeg) const
{
    const double lambda0 = degToRad(m_longitudeOfOriginDeg);

    const double n = F / (2.0 - F);
    const double n2 = n * n;
    const double n3 = n2 * n;
    const double n4 = n2 * n2;

    const double B =
        A / (1.0 + n) * (1.0 + n2 / 4.0 + n4 / 64.0);

    const double h1 =
        n / 2.0 - (2.0 / 3.0) * n2 +
        (37.0 / 96.0) * n3 - (1.0 / 360.0) * n4;

    const double h2 =
        (1.0 / 48.0) * n2 +
        (1.0 / 15.0) * n3 -
        (437.0 / 1440.0) * n4;

    const double h3 =
        (17.0 / 480.0) * n3 -
        (37.0 / 840.0) * n4;

    const double h4 =
        (4397.0 / 161280.0) * n4;

    const double etaPrime =
        (easting - m_falseEasting) /
        (B * m_scaleFactor);

    const double xiPrime =
        (northing - m_falseNorthing) /
        (B * m_scaleFactor);

    const double xi0 =
        xiPrime -
        h1 * std::sin(2.0 * xiPrime) * std::cosh(2.0 * etaPrime) -
        h2 * std::sin(4.0 * xiPrime) * std::cosh(4.0 * etaPrime) -
        h3 * std::sin(6.0 * xiPrime) * std::cosh(6.0 * etaPrime) -
        h4 * std::sin(8.0 * xiPrime) * std::cosh(8.0 * etaPrime);

    const double eta0 =
        etaPrime -
        h1 * std::cos(2.0 * xiPrime) * std::sinh(2.0 * etaPrime) -
        h2 * std::cos(4.0 * xiPrime) * std::sinh(4.0 * etaPrime) -
        h3 * std::cos(6.0 * xiPrime) * std::sinh(6.0 * etaPrime) -
        h4 * std::cos(8.0 * xiPrime) * std::sinh(8.0 * etaPrime);

    const double beta =
        std::asin(clampUnit(std::sin(xi0) / std::cosh(eta0)));

    const double qPrime = asinhCompat(std::tan(beta));

    double q = qPrime;

    for (int i = 0; i < 20; ++i)
    {
        const double next =
            qPrime + E * atanhCompat(E * std::tanh(q));

        if (std::fabs(next - q) < 1e-14)
        {
            q = next;
            break;
        }

        q = next;
    }

    const double phi = std::atan(std::sinh(q));

    const double lambda =
        wrapRadians(
            lambda0 +
            std::asin(
                clampUnit(std::tanh(eta0) / std::cos(beta))
            )
        );

    latitudeDeg = radToDeg(phi);
    longitudeDeg = radToDeg(lambda);

    return std::isfinite(latitudeDeg) && std::isfinite(longitudeDeg);
}

bool Projection::upsForward(double latitudeDeg,
                            double longitudeDeg,
                            double &easting,
                            double &northing) const
{
    const double phi = degToRad(latitudeDeg);
    const double lambda = degToRad(longitudeDeg);
    const double lambda0 = degToRad(m_longitudeOfOriginDeg);

    const double sinPhi = std::sin(phi);
    const double ratio =
        (1.0 + E * sinPhi) /
        (1.0 - E * sinPhi);

    double t = 0.0;

    if (m_northHemisphere)
    {
        t =
            std::tan(Pi / 4.0 - phi / 2.0) *
            std::pow(ratio, E / 2.0);
    }
    else
    {
        t =
            std::tan(Pi / 4.0 + phi / 2.0) /
            std::pow(ratio, E / 2.0);
    }

    const double c =
        std::sqrt(
            std::pow(1.0 + E, 1.0 + E) *
            std::pow(1.0 - E, 1.0 - E)
        );

    const double rho =
        2.0 * A * m_scaleFactor * t / c;

    const double theta = wrapRadians(lambda - lambda0);

    easting =
        m_falseEasting + rho * std::sin(theta);

    if (m_northHemisphere)
        northing = m_falseNorthing - rho * std::cos(theta);
    else
        northing = m_falseNorthing + rho * std::cos(theta);

    return std::isfinite(easting) && std::isfinite(northing);
}

bool Projection::upsReverse(double easting,
                            double northing,
                            double &latitudeDeg,
                            double &longitudeDeg) const
{
    const double dx = easting - m_falseEasting;
    const double dy = northing - m_falseNorthing;
    const double rho = std::sqrt(dx * dx + dy * dy);

    const double c =
        std::sqrt(
            std::pow(1.0 + E, 1.0 + E) *
            std::pow(1.0 - E, 1.0 - E)
        );

    const double t =
        rho * c /
        (2.0 * A * m_scaleFactor);

    double chi = 0.0;

    if (m_northHemisphere)
        chi = Pi / 2.0 - 2.0 * std::atan(t);
    else
        chi = 2.0 * std::atan(t) - Pi / 2.0;

    const double e2 = E * E;
    const double e4 = e2 * e2;
    const double e6 = e4 * e2;
    const double e8 = e4 * e4;

    const double phi =
        chi +
        (e2 / 2.0 + 5.0 * e4 / 24.0 +
         e6 / 12.0 + 13.0 * e8 / 360.0) *
            std::sin(2.0 * chi) +
        (7.0 * e4 / 48.0 + 29.0 * e6 / 240.0 +
         811.0 * e8 / 11520.0) *
            std::sin(4.0 * chi) +
        (7.0 * e6 / 120.0 + 81.0 * e8 / 1120.0) *
            std::sin(6.0 * chi) +
        (4279.0 * e8 / 161280.0) *
            std::sin(8.0 * chi);

    double lambda = degToRad(m_longitudeOfOriginDeg);

    if (std::fabs(dx) > 1e-15)
    {
        if (m_northHemisphere)
            lambda += std::atan2(dx, m_falseNorthing - northing);
        else
            lambda += std::atan2(dx, northing - m_falseNorthing);
    }

    latitudeDeg = radToDeg(phi);
    longitudeDeg = radToDeg(wrapRadians(lambda));

    return std::isfinite(latitudeDeg) && std::isfinite(longitudeDeg);
}

Projection::Type Projection::type() const
{
    return m_type;
}

int Projection::epsgCode() const
{
    return m_epsgCode;
}

int Projection::utmZone() const
{
    return m_utmZone;
}

bool Projection::northHemisphere() const
{
    return m_northHemisphere;
}

double Projection::latitudeOfOriginDeg() const
{
    return m_latitudeOfOriginDeg;
}

double Projection::longitudeOfOriginDeg() const
{
    return m_longitudeOfOriginDeg;
}

double Projection::scaleFactor() const
{
    return m_scaleFactor;
}

double Projection::falseEasting() const
{
    return m_falseEasting;
}

double Projection::falseNorthing() const
{
    return m_falseNorthing;
}

QString Projection::description() const
{
    if (m_type == UTM)
    {
        return QString("WGS84 / UTM zone %1%2")
            .arg(m_utmZone)
            .arg(m_northHemisphere ? "N" : "S");
    }

    if (m_type == UPS)
    {
        return QString("WGS84 / UPS %1")
            .arg(m_northHemisphere ? "North" : "South");
    }

    return "Unknown projection";
}
