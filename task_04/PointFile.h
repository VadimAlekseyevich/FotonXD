#ifndef POINTFILE_H
#define POINTFILE_H

#include <QString>
#include <QVector>

struct TestPoint
{
    int index;
    double x;
    double y;
    double latitude;
    double longitude;
};

class PointFile
{
public:
    static bool read(const QString &filePath,
                     QVector<TestPoint> &points,
                     QString &error);
};

#endif // POINTFILE_H
