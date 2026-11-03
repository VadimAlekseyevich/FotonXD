#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

class EarthWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &mapDirectory = QString(),
                        QWidget *parent = nullptr);

private slots:
    void openImage();

private:
    void refreshStatusBar();

    EarthWidget *m_earthWidget;
    int m_fps;
    bool m_onGlobe;
    double m_latitude;
    double m_longitude;
    QString m_textureStatus;

    bool m_hasRoute;
    double m_routeCenterLatitude;
    double m_routeCenterLongitude;
};

#endif // MAINWINDOW_H
