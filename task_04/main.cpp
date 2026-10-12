#include "GeoReference.h"
#include "GeoTiff.h"
#include "PointFile.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>

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
    out << "Usage:" << Qt::endl;
    out << "  " << programName
        << " <image.tif> <points.txt>" << Qt::endl;
    out << Qt::endl;
    out << "Example:" << Qt::endl;
    out << "  " << programName
        << " data/map.tiff data/map_points.txt" << Qt::endl;
}
}

int main(int argc, char *argv[])
{
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
        err << "GeoTIFF error: " << error << Qt::endl;
        return 2;
    }

    GeoReference geoReference;

    if (!geoReference.initialize(metadata, error))
    {
        err << "Georeference error: " << error << Qt::endl;
        return 3;
    }

    QVector<TestPoint> points;

    if (!PointFile::read(pointsPath, points, error))
    {
        err << "Points error: " << error << Qt::endl;
        return 4;
    }

    const Projection &projection = geoReference.projection();

    out << "Image: " << QFileInfo(imagePath).fileName() << Qt::endl;
    out << "Size: " << metadata.width << " x " << metadata.height << Qt::endl;
    out << "Projected CRS: EPSG:" << projection.epsgCode()
        << " (" << projection.description() << ")" << Qt::endl;

    if (metadata.geographicType != 0)
        out << "Geographic CRS GeoKey: EPSG:"
            << metadata.geographicType << Qt::endl;

    out << Qt::endl;
    out << "Raster -> projected affine transform:" << Qt::endl;
    out << "  E = " << number(metadata.gt[0])
        << " + x * " << number(metadata.gt[1])
        << " + y * " << number(metadata.gt[2])
        << Qt::endl;
    out << "  N = " << number(metadata.gt[3])
        << " + x * " << number(metadata.gt[4])
        << " + y * " << number(metadata.gt[5])
        << Qt::endl;

    out << Qt::endl;
    out << "Projection parameters:" << Qt::endl;
    out << "  latitude of origin  = "
        << number(projection.latitudeOfOriginDeg()) << " deg" << Qt::endl;
    out << "  longitude of origin = "
        << number(projection.longitudeOfOriginDeg()) << " deg" << Qt::endl;
    out << "  scale factor        = "
        << number(projection.scaleFactor()) << Qt::endl;
    out << "  false easting       = "
        << number(projection.falseEasting()) << " m" << Qt::endl;
    out << "  false northing      = "
        << number(projection.falseNorthing()) << " m" << Qt::endl;

    if (projection.type() == Projection::UTM)
        out << "  UTM zone            = "
            << projection.utmZone()
            << (projection.northHemisphere() ? "N" : "S")
            << Qt::endl;

    out << Qt::endl;
    out << "Pixel coordinates use (0,0) at the upper-left corner "
           "of the upper-left pixel."
        << Qt::endl;
    out << "Test points: " << points.size() << Qt::endl;

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
                << point.index << Qt::endl;
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
                << point.index << Qt::endl;
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
                << point.index << Qt::endl;
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

        out << Qt::endl;
        out << "Point " << point.index << Qt::endl;

        out << "  pixel reference : ("
            << number(point.x) << ", "
            << number(point.y) << ")" << Qt::endl;

        out << "  pixel round-trip: ("
            << number(x1) << ", "
            << number(y1) << ")" << Qt::endl;

        out << "  pixel delta     : ("
            << number(dx) << ", "
            << number(dy) << ")" << Qt::endl;

        out << "  geo reference   : (lat "
            << number(point.latitude) << ", lon "
            << number(point.longitude) << ")" << Qt::endl;

        out << "  geo calculated  : (lat "
            << number(latitude1) << ", lon "
            << number(longitude1) << ")" << Qt::endl;

        out << "  geo delta       : (lat "
            << number(dLatitude) << ", lon "
            << number(dLongitude) << ")" << Qt::endl;

        out << "  reference geo -> pixel: ("
            << number(xFromReferenceGeo) << ", "
            << number(yFromReferenceGeo) << ")" << Qt::endl;

        out << "  reference pixel delta : ("
            << number(dxReference) << ", "
            << number(dyReference) << ")" << Qt::endl;
    }

    out << Qt::endl;
    out << "Maximum absolute errors:" << Qt::endl;
    out << "  latitude  : " << number(maxLatitudeError)
        << " deg" << Qt::endl;
    out << "  longitude : " << number(maxLongitudeError)
        << " deg" << Qt::endl;
    out << "  pixel X   : " << number(maxPixelErrorX)
        << " px" << Qt::endl;
    out << "  pixel Y   : " << number(maxPixelErrorY)
        << " px" << Qt::endl;

    return 0;
}
