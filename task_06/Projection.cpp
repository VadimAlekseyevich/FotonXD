#include "Projection.h"
#include "GeoTiff.h"

#include <cmath>

// Числовые константы WGS84 и внутренние математические функции.
namespace
{
const double Pi = 3.1415926535897932384626433832795;
// Большая полуось WGS84; сжатие и эксцентриситет учитывают отличие Земли от сферы.
const double A = 6378137.0;
const double InvF = 298.257223563;
const double F = 1.0 / InvF;
const double E = std::sqrt(F * (2.0 - F));

// Перевод градусов в радианы, необходимые для sin/cos/tan в C++.
double degToRad(double value)
{
    return value * Pi / 180.0;
}

// Обратное преобразование радиан в градусы.
double radToDeg(double value)
{
    return value * 180.0 / Pi;
}

// Совместимая со старым компилятором формула обратного гиперболического синуса.
double asinhCompat(double x)
{
    return std::log(x + std::sqrt(x * x + 1.0));
}

// Вычисление обратного гиперболического тангенса через логарифм.
double atanhCompat(double x)
{
    return 0.5 * std::log((1.0 + x) / (1.0 - x));
}

// Ограничиваем вычисленные значения интервалом [-1,1].
double clampUnit(double value)
{
    if (value < -1.0)
        return -1.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

// Нормализуем разность долгот в диапазон [-π,+π].
double wrapRadians(double value)
{
    while (value > Pi)
        value -= 2.0 * Pi;
    while (value < -Pi)
        value += 2.0 * Pi;
    return value;
}

// Сравниваем параметры с допустимой погрешностью.
bool approximately(double value, double expected, double tolerance)
{
    return std::fabs(value - expected) <= tolerance;
}

// Пересчитываем масштаб полярной проекции с заданной параллели на полюс.
double polarScaleAtPole(double standardParallelDeg)
{
    const double phiF = degToRad(standardParallelDeg);
    const double sinPhiF = std::sin(phiF);
    const double e2 = E * E;

    const double mF =
        std::cos(phiF) /
        std::sqrt(1.0 - e2 * sinPhiF * sinPhiF);

    const double ratio =
        (1.0 + E * sinPhiF) /
        (1.0 - E * sinPhiF);

    double tF = 0.0;

    if (standardParallelDeg >= 0.0)
    {
        tF =
            std::tan(Pi / 4.0 - phiF / 2.0) *
            std::pow(ratio, E / 2.0);
    }
    else
    {
        tF =
            std::tan(Pi / 4.0 + phiF / 2.0) /
            std::pow(ratio, E / 2.0);
    }

    const double factor =
        std::sqrt(
            std::pow(1.0 + E, 1.0 + E) *
            std::pow(1.0 - E, 1.0 - E)
        );

    return mF * factor / (2.0 * tF);
}
}

// Начальные значения параметров до выбора проекции.
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

// Настраиваем UTM/UPS по стандартному EPSG или пользовательским GeoKeys.
bool Projection::configureFromGeoTiff(const GeoTiffMetadata &metadata,
                                      QString &error)
{
    error.clear();

// При наличии EPSG выбираем стандартную конфигурацию координатной системы.
    if (metadata.projectedCrs != 0 &&
        metadata.projectedCrs != 32767)
    {
        return configureFromEpsg(metadata.projectedCrs, error);
    }

    // Тесты используют WGS84: допустимы как стандартные GeoKeys,
    // так и явно указанные параметры эллипсоида.
// Убеждаемся, что географическая система и эллипсоид соответствуют WGS84.
    if (metadata.geographicType != 0 &&
        metadata.geographicType != 32767 &&
        metadata.geographicType != 4326)
    {
        error = QString("Unsupported geographic CRS GeoKey: %1; WGS84 is required.")
                    .arg(metadata.geographicType);
        return false;
    }

    if (metadata.ellipsoid != 0 &&
        metadata.ellipsoid != 32767 &&
        metadata.ellipsoid != 7030)
    {
        error = QString("Unsupported ellipsoid GeoKey: %1; WGS84 is required.")
                    .arg(metadata.ellipsoid);
        return false;
    }

    if (metadata.hasSemiMajorAxis &&
        !approximately(metadata.semiMajorAxis, A, 0.001))
    {
        error = "GeoTIFF semi-major axis is not WGS84.";
        return false;
    }

    if (metadata.hasInverseFlattening &&
        !approximately(metadata.inverseFlattening, InvF, 0.000001))
    {
        error = "GeoTIFF inverse flattening is not WGS84.";
        return false;
    }

// Эта реализация ожидает метры как единицы плоских координат.
    if (metadata.linearUnits != 0 &&
        metadata.linearUnits != 32767 &&
        metadata.linearUnits != 9001)
    {
        error = QString("Unsupported projected linear units GeoKey: %1; metres are required.")
                    .arg(metadata.linearUnits);
        return false;
    }

    // Тип GeoTIFF CT_TransverseMercator: поперечная проекция Меркатора.
// GeoTIFF-код 1: поперечная проекция Меркатора, используемая UTM.
    if (metadata.coordinateTransform == 1)
    {
        if (!metadata.hasLatitudeOfOrigin ||
            !metadata.hasLongitudeOfOrigin ||
            !metadata.hasScaleAtNaturalOrigin ||
            !metadata.hasFalseEasting ||
            !metadata.hasFalseNorthing)
        {
            error =
                "User-defined Transverse Mercator is missing one or more "
                "projection parameter GeoKeys.";
            return false;
        }

        if (std::fabs(metadata.latitudeOfOrigin) > 1e-10)
        {
            error =
                "This task supports UTM, whose latitude of natural origin "
                "must be the equator.";
            return false;
        }

        m_type = UTM;
        m_epsgCode = 0;
        m_latitudeOfOriginDeg = metadata.latitudeOfOrigin;
        m_longitudeOfOriginDeg = metadata.longitudeOfOrigin;
        m_scaleFactor = metadata.scaleAtNaturalOrigin;
        m_falseEasting = metadata.falseEasting;
        m_falseNorthing = metadata.falseNorthing;

        m_utmZone = static_cast<int>(
            std::floor((m_longitudeOfOriginDeg + 183.0) / 6.0 + 0.5)
        );

        if (m_utmZone < 1 || m_utmZone > 60)
            m_utmZone = 0;

        m_northHemisphere = m_falseNorthing < 5000000.0;
        return true;
    }

    // Тип GeoTIFF CT_PolarStereographic: полярная стереографическая проекция.
// GeoTIFF-код 15: полярная стереографическая проекция.
    if (metadata.coordinateTransform == 15)
    {
        if (!metadata.hasLatitudeOfOrigin ||
            !metadata.hasFalseEasting ||
            !metadata.hasFalseNorthing)
        {
            error =
                "User-defined Polar Stereographic is missing latitude of "
                "origin or false grid coordinates.";
            return false;
        }

        double longitudeOfOrigin = 0.0;

        if (metadata.hasStraightVertPoleLongitude)
            longitudeOfOrigin = metadata.straightVertPoleLongitude;
        else if (metadata.hasLongitudeOfOrigin)
            longitudeOfOrigin = metadata.longitudeOfOrigin;
        else
        {
            error =
                "User-defined Polar Stereographic is missing "
                "ProjStraightVertPoleLongGeoKey/longitude of origin.";
            return false;
        }

        const double latitudeParameter = metadata.latitudeOfOrigin;

        m_type = UPS;
        m_epsgCode = 0;
        m_utmZone = 0;
        m_northHemisphere = latitudeParameter >= 0.0;

        // Формулы используют вариант A полярной стереографии с началом на полюсе.
        // Некоторые GeoTIFF задают широту истинного масштаба вне полюса;
        // здесь переводим её в эквивалентный масштабный коэффициент у полюса.
        m_latitudeOfOriginDeg = m_northHemisphere ? 90.0 : -90.0;
        m_longitudeOfOriginDeg = longitudeOfOrigin;
        m_falseEasting = metadata.falseEasting;
        m_falseNorthing = metadata.falseNorthing;

// Для начала у полюса используем заданный масштаб или стандартный коэффициент UPS.
        if (std::fabs(latitudeParameter) >= 89.0)
        {
            if (metadata.hasScaleAtNaturalOrigin)
            {
                m_scaleFactor = metadata.scaleAtNaturalOrigin;
            }
            else if (approximately(m_falseEasting, 2000000.0, 0.01) &&
                     approximately(m_falseNorthing, 2000000.0, 0.01))
            {
                // Если файл использует стандартную UPS, но не содержит масштабный
                // GeoKey, подставляем общеизвестный коэффициент 0.994.
                m_scaleFactor = 0.994;
            }
            else
            {
                error =
                    "Polar Stereographic scale factor is missing.";
                return false;
            }
        }
        else
        {
            m_scaleFactor = polarScaleAtPole(latitudeParameter);
        }

        return true;
    }

    error = QString(
        "Projected CRS is user-defined, but ProjCoordTransGeoKey=%1 "
        "is not UTM/Transverse Mercator (1) or Polar Stereographic (15).")
        .arg(metadata.coordinateTransform);
    return false;
}

// Определяем масштаб, центральный меридиан и ложные смещения по коду EPSG.
bool Projection::configureFromEpsg(int epsgCode, QString &error)
{
    error.clear();

    m_type = Unknown;
    m_epsgCode = epsgCode;
    m_utmZone = 0;
    m_northHemisphere = true;

// UTM для северного полушария: зоны 1–60.
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

// UTM для южного полушария: false northing 10 миллионов метров.
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

// Стандартная UPS: масштаб 0.994, ложные восток и север по 2 миллиона метров.
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

// Выбираем обратную математическую функцию для UTM или UPS.
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

// Выбираем прямую математическую функцию для UTM или UPS.
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

// Прямая формула поперечного Меркатора на эллипсоиде WGS84.
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

    return true;
}

// Обратная формула UTM: плоские координаты преобразуются в геодезические.
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

    return true;
}

// Прямая полярная стереография: вычисляем положение на плоскости относительно полюса.
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

// Обратная полярная стереография: восстанавливаем широту и долготу.
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

// Возвращаем EPSG-код или 0 для пользовательской проекции.
int Projection::epsgCode() const
{
    return m_epsgCode;
}

// Номер UTM-зоны не применяется к UPS.
int Projection::utmZone() const
{
    return m_utmZone;
}

// Настроенное полушарие.
bool Projection::northHemisphere() const
{
    return m_northHemisphere;
}

// Параметр широты начала проекции.
double Projection::latitudeOfOriginDeg() const
{
    return m_latitudeOfOriginDeg;
}

// Центральный меридиан.
double Projection::longitudeOfOriginDeg() const
{
    return m_longitudeOfOriginDeg;
}

// Масштабный коэффициент проекции.
double Projection::scaleFactor() const
{
    return m_scaleFactor;
}

// Ложное восточное смещение.
double Projection::falseEasting() const
{
    return m_falseEasting;
}

// Ложное северное смещение.
double Projection::falseNorthing() const
{
    return m_falseNorthing;
}

// Большая полуось WGS84.
double Projection::semiMajorAxis() const
{
    return A;
}

// Обратное сжатие WGS84.
double Projection::inverseFlattening() const
{
    return InvF;
}

// Краткая строка с описанием проекции и её параметров.
QString Projection::description() const
{
    if (m_type == UTM)
    {
        if (m_epsgCode == 0)
            return QString("WGS84 / UTM (user-defined GeoTIFF)");

        return QString("WGS84 / UTM zone %1%2")
            .arg(m_utmZone)
            .arg(m_northHemisphere ? "N" : "S");
    }

    if (m_type == UPS)
    {
        if (m_epsgCode == 0)
            return QString("WGS84 / Polar Stereographic (user-defined GeoTIFF)");

        return QString("WGS84 / UPS %1")
            .arg(m_northHemisphere ? "North" : "South");
    }

    return "Unknown projection";
}
