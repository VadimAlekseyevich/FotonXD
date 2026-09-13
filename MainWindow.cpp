#include "MainWindow.h"

#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_cubeWidget(new CubeWidget(this))
{
    setWindowTitle("QOpenGLWidget - 3D Cube");
    resize(800, 600);

    setCentralWidget(m_cubeWidget);

    statusBar()->showMessage("FPS: 0");

    connect(m_cubeWidget,
            &CubeWidget::fpsChanged,
            this,
            &MainWindow::updateFps);
}

void MainWindow::updateFps(int fps)
{
    statusBar()->showMessage(
        QString("FPS: %1").arg(fps)
    );
}
