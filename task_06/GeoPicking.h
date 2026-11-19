#ifndef GEOPICKING_H
#define GEOPICKING_H

#include <QMatrix4x4>
#include <QPoint>
#include <QSize>
#include <QVector3D>
#include <QVector4D>
#include <QtMath>
#include <cmath>

namespace GeoPicking
{
inline bool screenToGeodetic(const QPoint &pixel,
                            const QSize &viewport,
                            const QMatrix4x4 &projection,
                            const QMatrix4x4 &modelView,
                            double &latitude,
                            double &longitude)
{
    latitude = 0.0;
    longitude = 0.0;

    if (viewport.width() <= 0 || viewport.height() <= 0 ||
        pixel.x() < 0 || pixel.x() >= viewport.width() ||
        pixel.y() < 0 || pixel.y() >= viewport.height())
        return false;

    const float ndcX = 2.0f * (pixel.x() + 0.5f) / viewport.width() - 1.0f;
    const float ndcY = 1.0f - 2.0f * (pixel.y() + 0.5f) / viewport.height();

    bool invertible = false;
    const QMatrix4x4 inverseProjection = projection.inverted(&invertible);
    if (!invertible)
        return false;

    const QVector4D nearPoint =
        inverseProjection * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    const QVector3D cameraDirection =
        nearPoint.toVector3DAffine().normalized();

    const QMatrix4x4 inverseModelView = modelView.inverted(&invertible);
    if (!invertible)
        return false;

    const QVector3D origin = inverseModelView.map(QVector3D(0, 0, 0));
    const QVector3D direction =
        inverseModelView.mapVector(cameraDirection).normalized();

    const double halfB = QVector3D::dotProduct(origin, direction);
    const double c = QVector3D::dotProduct(origin, origin) - 1.0;
    const double discriminant = halfB * halfB - c;
    if (discriminant < 0.0)
        return false;

    const double root = std::sqrt(discriminant);
    double t = -halfB - root;
    if (t < 0.0)
        t = -halfB + root;
    if (t < 0.0)
        return false;

    const QVector3D hit = (origin + static_cast<float>(t) * direction).normalized();

    latitude = qRadiansToDegrees(
        std::asin(qBound(-1.0, static_cast<double>(hit.y()), 1.0)));
    longitude = qRadiansToDegrees(
        std::atan2(static_cast<double>(hit.x()), static_cast<double>(hit.z())));
    return true;
}
}

#endif
