#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

class EarthWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &mapDirectory = QString(), QWidget *parent = nullptr);

private:
    void refreshStatusBar();

    EarthWidget *m_earthWidget;
    int m_fps;
    QString m_textureStatus;
};

#endif // MAINWINDOW_H
