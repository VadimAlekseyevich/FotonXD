#include "PointFile.h"

#include <QFile>
#include <QRegExp>
#include <QTextStream>

bool PointFile::read(const QString &filePath,
                     QVector<TestPoint> &points,
                     QString &error)
{
    points.clear();
    error.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        error = QString("Cannot open points file: %1")
                    .arg(file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");

    QRegExp pattern(
        "\\s*([0-9]+)\\.\\s*"
        "([0-9]+\\.?[0-9]*)x([0-9]+\\.?[0-9]*):\\s*"
        "([0-9]+\\.?[0-9]*)\\s*([NS])\\s*"
        "([0-9]+\\.?[0-9]*)\\s*([EW])"
    );

    int lineNumber = 0;

    while (!stream.atEnd())
    {
        const QString line = stream.readLine();
        ++lineNumber;

        const QString trimmed = line.trimmed();

        if (trimmed.isEmpty() || trimmed.startsWith('#'))
            continue;

        if (pattern.indexIn(trimmed) < 0)
        {
            error = QString("Invalid points file format at line %1: %2")
                        .arg(lineNumber)
                        .arg(line);
            return false;
        }

        TestPoint point;
        point.index = pattern.cap(1).toInt();
        point.x = pattern.cap(2).toDouble();
        point.y = pattern.cap(3).toDouble();

        point.latitude = pattern.cap(4).toDouble();
        if (pattern.cap(5) == "S")
            point.latitude = -point.latitude;

        point.longitude = pattern.cap(6).toDouble();
        if (pattern.cap(7) == "W")
            point.longitude = -point.longitude;

        points.push_back(point);
    }

    if (points.isEmpty())
    {
        error = "Points file contains no test points";
        return false;
    }

    return true;
}
