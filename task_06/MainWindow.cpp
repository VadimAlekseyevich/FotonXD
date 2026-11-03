#include "MainWindow.h"
#include "EarthWidget.h"
#include <QStatusBar>

MainWindow::MainWindow(const QString &mapDirectory, QWidget *parent)
    : QMainWindow(parent),
      m_earthWidget(new EarthWidget(mapDirectory, this)),
      m_fps(0),
      m_onGlobe(false),
      m_latitude(0.0),
      m_longitude(0.0),
      m_textureStatus(QStringLiteral("Map: initialization..."))
{
    setWindowTitle(QStringLiteral("Task 3 - Earth zoom and coordinates (QOpenGLWidget)"));
    resize(1000, 700);
    setCentralWidget(m_earthWidget);

    connect(m_earthWidget, &EarthWidget::fpsChanged,
            this, [this](int fps) { m_fps = fps; refreshStatusBar(); });
    connect(m_earthWidget, &EarthWidget::textureStatusChanged,
            this, [this](const QString &status) {
                m_textureStatus = status;
                refreshStatusBar();
            });
    connect(m_earthWidget, &EarthWidget::coordinatesChanged,
            this, [this](bool onGlobe, double latitude, double longitude) {
                m_onGlobe = onGlobe;
                m_latitude = latitude;
                m_longitude = longitude;
                refreshStatusBar();
            });
    refreshStatusBar();
}

void MainWindow::refreshStatusBar()
{
    const QString coordinates = m_onGlobe
        ? QStringLiteral("Lat: %1, Lon: %2")
            .arg(m_latitude, 0, 'f', 6).arg(m_longitude, 0, 'f', 6)
        : QStringLiteral("Lat: --, Lon: --");

    statusBar()->showMessage(
        QStringLiteral("FPS: %1 | %2 | %3")
            .arg(m_fps).arg(coordinates).arg(m_textureStatus));
}
