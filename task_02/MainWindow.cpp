#include "MainWindow.h"
#include "EarthWidget.h"

#include <QStatusBar>

MainWindow::MainWindow(const QString &mapDirectory, QWidget *parent)
    : QMainWindow(parent),
      m_earthWidget(new EarthWidget(mapDirectory, this)),
      m_fps(0),
      m_textureStatus(QStringLiteral("Map: initialization..."))
{
    setWindowTitle(QStringLiteral("Task 2 - Textured Earth (QOpenGLWidget)"));
    resize(1000, 700);
    setCentralWidget(m_earthWidget);

    connect(m_earthWidget, &EarthWidget::fpsChanged,
            this, [this](int fps)
            {
                m_fps = fps;
                refreshStatusBar();
            });

    connect(m_earthWidget, &EarthWidget::textureStatusChanged,
            this, [this](const QString &status)
            {
                m_textureStatus = status;
                refreshStatusBar();
            });

    refreshStatusBar();
}

void MainWindow::refreshStatusBar()
{
    statusBar()->showMessage(
        QStringLiteral("FPS: %1 | %2")
            .arg(m_fps)
            .arg(m_textureStatus)
    );
}
