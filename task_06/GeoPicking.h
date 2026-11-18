#ifndef GEOPICKING_H
#define GEOPICKING_H

#include <QMatrix4x4>
#include <QPoint>
#include <QSize>
#include <QVector3D>
#include <QVector4D>
#include <QtMath>
#include <cmath>

// Геометрия здесь сферическая, а не эллипсоид WGS84.
// Обратная model-view учитывает масштабирование, перенос камеры и вращение.
// Алгоритм для курсора: экранная точка → луч камеры → пересечение с единичной сферой.
namespace GeoPicking
{
// inline-функция расположена в заголовке и возвращает признак попадания луча на глобус.
inline bool screenToGeodetic(const QPoint &pixel,
                            const QSize &viewport,
                            const QMatrix4x4 &projection,
                            const QMatrix4x4 &modelView,
                            double &latitude,
                            double &longitude)
{
// Сбрасываем выход и проверяем, что курсор внутри виджета.
    latitude = 0.0;
    longitude = 0.0;

    if (viewport.width() <= 0 || viewport.height() <= 0 ||
        pixel.x() < 0 || pixel.x() >= viewport.width() ||
        pixel.y() < 0 || pixel.y() >= viewport.height())
        return false;

    // QMouseEvent uses logical widget coordinates. The projected aspect ratio
    // uses the same units, so devicePixelRatio does not enter these ratios.
// Преобразуем координаты пикселя в диапазон NDC от -1 до +1.
    const float ndcX = 2.0f * (pixel.x() + 0.5f) / viewport.width() - 1.0f;
    const float ndcY = 1.0f - 2.0f * (pixel.y() + 0.5f) / viewport.height();

// Обратная матрица перспективы восстанавливает направление луча камеры.
    bool invertible = false;
    const QMatrix4x4 inverseProjection = projection.inverted(&invertible);
    if (!invertible)
        return false;

    // In camera space the ray starts at (0,0,0) and goes through the pixel.
    const QVector4D nearPoint =
        inverseProjection * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    const QVector3D cameraDirection =
        nearPoint.toVector3DAffine().normalized();

// Обратная матрица снимает повороты и смещение камеры.
    const QMatrix4x4 inverseModelView = modelView.inverted(&invertible);
    if (!invertible)
        return false;

    const QVector3D origin = inverseModelView.map(QVector3D(0, 0, 0));
    const QVector3D direction =
        inverseModelView.mapVector(cameraDirection).normalized();

    // Solve |origin + t*direction|^2 = 1. Select the nearest positive root.
// Решаем квадратное уравнение пересечения луча с единичной сферой.
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

// Нормализуем найденную точку пересечения до радиуса 1.
    const QVector3D hit = (origin + static_cast<float>(t) * direction).normalized();

    // Exactly the mesh basis: X = cos(lat)*sin(lon),
    // Y = sin(lat), Z = cos(lat)*cos(lon). Positive longitude = East.
// Получаем градусные широту и долготу по координатам XYZ.
    latitude = qRadiansToDegrees(
        std::asin(qBound(-1.0, static_cast<double>(hit.y()), 1.0)));
    longitude = qRadiansToDegrees(
        std::atan2(static_cast<double>(hit.x()), static_cast<double>(hit.z())));
    return true;
}
} // namespace GeoPicking

#endif // GEOPICKING_H
