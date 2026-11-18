// Подключаем главное окно и инфраструктуру Qt.
#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
// Создаём совместимый OpenGL-контекст версии 2.1 и буфер глубины.
    QSurfaceFormat format;
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setDepthBufferSize(24);
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);

// Объект QApplication управляет GUI и циклом событий.
    QApplication app(argc, argv);

// При заданном пути к исходникам делаем его рабочим каталогом приложения.
#ifdef FOTON_TASK6_SOURCE_DIR
    QDir::setCurrent(QString::fromUtf8(FOTON_TASK6_SOURCE_DIR));
#endif

// Необязательный аргумент может задавать путь к базовой карте.
    const QStringList args = QCoreApplication::arguments();
    const QString mapDirectory = args.size() > 1 ? args.at(1) : QString();

// Создаём окно, показываем его и передаём управление Qt.
    MainWindow window(mapDirectory);
    window.show();

// Приложение обрабатывает события до закрытия окна.
    return app.exec();
}
