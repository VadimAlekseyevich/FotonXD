#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setDepthBufferSize(24);
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    const QStringList args = QCoreApplication::arguments();
    const QString mapDirectory = args.size() > 1 ? args.at(1) : QString();

    MainWindow window(mapDirectory);
    window.show();

    return app.exec();
}
