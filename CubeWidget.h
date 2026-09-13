#ifndef CUBEWIDGET_H
#define CUBEWIDGET_H

#include <QOpenGLFunctions_2_1>
#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QPoint>
#include <QTimer>

class CubeWidget : public QOpenGLWidget,
                   protected QOpenGLFunctions_2_1
{
    Q_OBJECT

public:
    explicit CubeWidget(QWidget *parent = nullptr);

signals:
    void fpsChanged(int fps);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void drawCube();

    float m_rotationX;
    float m_rotationY;

    QPoint m_lastMousePosition;

    QElapsedTimer m_fpsTimer;
    int m_frameCount;

    QTimer m_updateTimer;
};

#endif // CUBEWIDGET_H