#include "GeoReference.h"
#include "GeoTiff.h"
#include "PointFile.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>

namespace
{
QString number(double value)
{
    return QString::number(value, 'f', 9);
}

double longitudeDelta(double value, double reference)
{
    double delta = value - reference;

    while (delta > 180.0)
        delta -= 360.0;

    while (delta < -180.0)
        delta += 360.0;

    return delta;
}

void printUsage(QTextStream &out, const QString &programName)
{
    out << "Usage:" << '\n';
    out << "  " << programName
        << " <image.tif> <points.txt>" << '\n';
    out << '\n';
    out << "Example:" << '\n';
    out << "  " << programName
        << " data/map.tiff data/map_points.txt" << '\n';
}
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    QCoreApplication app(argc, argv);

    QTextStream out(stdout);
    QTextStream err(stderr);
    out.setCodec("UTF-8");
    err.setCodec("UTF-8");

    const QStringList args = QCoreApplication::arguments();

    if (args.size() != 3)
    {
        printUsage(out, QFileInfo(args.at(0)).fileName());
        return 1;
    }

    const QString imagePath = args.at(1);
    const QString pointsPath = args.at(2);

    GeoTiffMetadata metadata;
    QString error;

    if (!GeoTiffReader::read(imagePath, metadata, error))
    {
        err << "GeoTIFF error: " << error << '\n';
        return 2;
    }

    GeoReference geoReference;

    if (!geoReference.initialize(metadata, error))
    {
        err << "Georeference error: " << error << '\n';
        return 3;
    }

    QVector<TestPoint> points;

    if (!PointFile::read(pointsPath, points, error))
    {
        err << "Points error: " << error << '\n';
        return 4;
    }

    const Projection &projection = geoReference.projection();

    out << "Image: " << QFileInfo(imagePath).fileName() << '\n';
    out << "Size: " << metadata.width << " x " << metadata.height << '\n';
    if (projection.epsgCode() != 0)
    {
        out << "Projected CRS: EPSG:" << projection.epsgCode()
            << " (" << projection.description() << ")" << '\n';
    }
    else
    {
        out << "Projected CRS: user-defined GeoTIFF"
            << " (" << projection.description() << ")" << '\n';
    }

    if (metadata.geographicType != 0 &&
        metadata.geographicType != 32767)
    {
        out << "Geographic CRS GeoKey: EPSG:"
            << metadata.geographicType << '\n';
    }

    if (metadata.projectedCrs == 32767 || metadata.projectedCrs == 0)
    {
        out << "GeoTIFF projection GeoKeys:" << '\n';
        out << "  ProjCoordTrans        = "
            << metadata.coordinateTransform << '\n';

        if (metadata.hasLatitudeOfOrigin)
            out << "  latitude parameter    = "
                << number(metadata.latitudeOfOrigin) << " deg" << '\n';

        if (metadata.hasLongitudeOfOrigin)
            out << "  longitude of origin   = "
                << number(metadata.longitudeOfOrigin) << " deg" << '\n';

        if (metadata.hasStraightVertPoleLongitude)
            out << "  straight pole long.   = "
                << number(metadata.straightVertPoleLongitude)
                << " deg" << '\n';

        if (metadata.hasScaleAtNaturalOrigin)
            out << "  scale at nat. origin  = "
                << number(metadata.scaleAtNaturalOrigin) << '\n';

        if (metadata.hasFalseEasting)
            out << "  false easting         = "
                << number(metadata.falseEasting) << " m" << '\n';

        if (metadata.hasFalseNorthing)
            out << "  false northing        = "
                << number(metadata.falseNorthing) << " m" << '\n';
    }

    out << '\n';
    out << "Raster -> projected affine transform:" << '\n';
    out << "  E = " << number(metadata.gt[0])
        << " + x * " << number(metadata.gt[1])
        << " + y * " << number(metadata.gt[2])
        << '\n';
    out << "  N = " << number(metadata.gt[3])
        << " + x * " << number(metadata.gt[4])
        << " + y * " << number(metadata.gt[5])
        << '\n';

    out << '\n';
    out << "Projection parameters:" << '\n';
    out << "  semi-major axis a   = "
        << number(projection.semiMajorAxis()) << " m" << '\n';
    out << "  inverse flattening  = "
        << number(projection.inverseFlattening()) << '\n';
    out << "  latitude of origin  = "
        << number(projection.latitudeOfOriginDeg()) << " deg" << '\n';
    out << "  longitude of origin = "
        << number(projection.longitudeOfOriginDeg()) << " deg" << '\n';
    out << "  scale factor        = "
        << number(projection.scaleFactor()) << '\n';
    out << "  false easting       = "
        << number(projection.falseEasting()) << " m" << '\n';
    out << "  false northing      = "
        << number(projection.falseNorthing()) << " m" << '\n';

    if (projection.type() == Projection::UTM)
        out << "  UTM zone            = "
            << projection.utmZone()
            << (projection.northHemisphere() ? "N" : "S")
            << '\n';

    out << '\n';
    out << "Pixel coordinates use (0,0) at the upper-left corner "
           "of the upper-left pixel."
        << '\n';
    out << "Test points: " << points.size() << '\n';

    double maxLatitudeError = 0.0;
    double maxLongitudeError = 0.0;
    double maxPixelErrorX = 0.0;
    double maxPixelErrorY = 0.0;

    for (int i = 0; i < points.size(); ++i)
    {
        const TestPoint &point = points.at(i);

        double latitude1 = 0.0;
        double longitude1 = 0.0;

        if (!geoReference.planar2geo(
                point.x,
                point.y,
                latitude1,
                longitude1))
        {
            err << "planar2geo failed for point "
                << point.index << '\n';
            return 5;
        }

        // Required round trip: pixel -> geo -> pixel.
        double x1 = 0.0;
        double y1 = 0.0;

        if (!geoReference.geo2planar(
                x1,
                y1,
                latitude1,
                longitude1))
        {
            err << "geo2planar failed for point "
                << point.index << '\n';
            return 6;
        }

        // Additional independent reverse check:
        // reference geo from the points file -> pixel.
        double xFromReferenceGeo = 0.0;
        double yFromReferenceGeo = 0.0;

        if (!geoReference.geo2planar(
                xFromReferenceGeo,
                yFromReferenceGeo,
                point.latitude,
                point.longitude))
        {
            err << "geo2planar(reference) failed for point "
                << point.index << '\n';
            return 7;
        }

        const double dLatitude =
            latitude1 - point.latitude;

        const double dLongitude =
            longitudeDelta(longitude1, point.longitude);

        const double dx = x1 - point.x;
        const double dy = y1 - point.y;

        const double dxReference =
            xFromReferenceGeo - point.x;

        const double dyReference =
            yFromReferenceGeo - point.y;

        maxLatitudeError =
            std::max(maxLatitudeError, std::fabs(dLatitude));

        maxLongitudeError =
            std::max(maxLongitudeError, std::fabs(dLongitude));

        maxPixelErrorX =
            std::max(maxPixelErrorX, std::fabs(dxReference));

        maxPixelErrorY =
            std::max(maxPixelErrorY, std::fabs(dyReference));

        out << '\n';
        out << "Point " << point.index << '\n';

        out << "  pixel reference : ("
            << number(point.x) << ", "
            << number(point.y) << ")" << '\n';

        out << "  pixel round-trip: ("
            << number(x1) << ", "
            << number(y1) << ")" << '\n';

        out << "  pixel delta     : ("
            << number(dx) << ", "
            << number(dy) << ")" << '\n';

        out << "  geo reference   : (lat "
            << number(point.latitude) << ", lon "
            << number(point.longitude) << ")" << '\n';

        out << "  geo calculated  : (lat "
            << number(latitude1) << ", lon "
            << number(longitude1) << ")" << '\n';

        out << "  geo delta       : (lat "
            << number(dLatitude) << ", lon "
            << number(dLongitude) << ")" << '\n';

        out << "  reference geo -> pixel: ("
            << number(xFromReferenceGeo) << ", "
            << number(yFromReferenceGeo) << ")" << '\n';

        out << "  reference pixel delta : ("
            << number(dxReference) << ", "
            << number(dyReference) << ")" << '\n';
    }

    out << '\n';
    out << "Maximum absolute errors:" << '\n';
    out << "  latitude  : " << number(maxLatitudeError)
        << " deg" << '\n';
    out << "  longitude : " << number(maxLongitudeError)
        << " deg" << '\n';
    out << "  pixel X   : " << number(maxPixelErrorX)
        << " px" << '\n';
    out << "  pixel Y   : " << number(maxPixelErrorY)
        << " px" << '\n';

    return 0;
}
