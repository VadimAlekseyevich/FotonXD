#include "GeoPicking.h"

#include <QMatrix4x4>
#include <QPoint>
#include <QSize>
#include <cmath>
#include <iostream>

namespace
{
const QSize viewport(801, 601);
const QPoint center(400, 300);

QMatrix4x4 projection()
{
    QMatrix4x4 matrix;
    matrix.perspective(45.0f, float(viewport.width()) / viewport.height(),
                       0.1f, 100.0f);
    return matrix;
}

QMatrix4x4 modelView(float distance, float xAngle = 0.0f, float yAngle = 0.0f)
{
    QMatrix4x4 matrix;
    matrix.translate(0, 0, -distance);
    matrix.rotate(xAngle, 1, 0, 0);
    matrix.rotate(yAngle, 0, 1, 0);
    return matrix;
}

bool near(double value, double expected, double epsilon = 0.0001)
{
    return std::abs(value - expected) < epsilon;
}

int check(const char *name, bool result)
{
    std::cout << (result ? "PASS: " : "FAIL: ") << name << '\n';
    return result ? 0 : 1;
}
}

int main()
{
    int failures = 0;
    double lat = 0.0, lon = 0.0;
    const QMatrix4x4 proj = projection();

    bool hit = GeoPicking::screenToGeodetic(center, viewport, proj,
                                           modelView(3.2f), lat, lon);
    failures += check("front center maps to 0 N, 0 E",
                      hit && near(lat, 0) && near(lon, 0));

    hit = GeoPicking::screenToGeodetic(center, viewport, proj,
                                      modelView(3.2f, 0, 90), lat, lon);
    failures += check("Y rotation is undone: 90 W at center",
                      hit && near(lat, 0) && near(lon, -90));

    hit = GeoPicking::screenToGeodetic(center, viewport, proj,
                                      modelView(3.2f, 90, 0), lat, lon);
    failures += check("X rotation is undone: north pole at center",
                      hit && near(lat, 90));

    hit = GeoPicking::screenToGeodetic(center, viewport, proj,
                                      modelView(1.15f), lat, lon);
    failures += check("zoomed-in center remains geographic origin",
                      hit && near(lat, 0) && near(lon, 0));

    hit = GeoPicking::screenToGeodetic(center, viewport, proj,
                                      modelView(20.0f), lat, lon);
    failures += check("zoomed-out center remains geographic origin",
                      hit && near(lat, 0) && near(lon, 0));

    hit = GeoPicking::screenToGeodetic(QPoint(0, 0), viewport, proj,
                                      modelView(3.2f), lat, lon);
    failures += check("background has no geographic coordinates", !hit);

    hit = GeoPicking::screenToGeodetic(QPoint(-1, 300), viewport, proj,
                                      modelView(3.2f), lat, lon);
    failures += check("cursor outside widget is rejected", !hit);

    double leftLat, leftLon, rightLat, rightLon;
    const bool leftHit = GeoPicking::screenToGeodetic(
        QPoint(350, 300), viewport, proj, modelView(3.2f), leftLat, leftLon);
    const bool rightHit = GeoPicking::screenToGeodetic(
        QPoint(450, 300), viewport, proj, modelView(3.2f), rightLat, rightLon);
    failures += check("opposite cursor directions yield west/east longitude",
                      leftHit && rightHit && leftLon < 0 && rightLon > 0 &&
                      near(leftLat, 0) && near(rightLat, 0) &&
                      near(leftLon, -rightLon));

    std::cout << (failures ? "FAILED" : "ALL TESTS PASSED") << '\n';
    return failures ? 1 : 0;
}
