#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

// Предварительное объявление: храним указатель, не подключая тяжёлый заголовок.
class EarthWidget;

// Главный координатор интерфейса и сценария открытия GeoTIFF.
class MainWindow : public QMainWindow
{
// Поддержка метаобъектной системы Qt и слотов.
    Q_OBJECT

public:
    explicit MainWindow(const QString &mapDirectory = QString(),
                        QWidget *parent = nullptr);

// Слот пункта меню «Открыть изображение».
private slots:
    void openImage();

private:
// Единое место формирования строки состояния.
    void refreshStatusBar();

// Графический виджет принадлежит главному окну как дочерний QObject.
    EarthWidget *m_earthWidget;
// Храним FPS, координаты курсора и статус базовой текстуры.
    int m_fps;
    bool m_onGlobe;
    double m_latitude;
    double m_longitude;
    QString m_textureStatus;

// Флаг наличия маршрута и координаты географического центра.
    bool m_hasRoute;
    double m_routeCenterLatitude;
    double m_routeCenterLongitude;
};

#endif // MAINWINDOW_H
