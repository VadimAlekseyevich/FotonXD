#include "CubeWidget.h"

#include <QMouseEvent>
#include <QtMath>

CubeWidget::CubeWidget(QWidget *parent)
    : QOpenGLWidget(parent),
      m_rotationX(20.0f),
      m_rotationY(-30.0f),
      m_frameCount(0)
{
    setFocusPolicy(Qt::StrongFocus);

    connect(&m_updateTimer,
            &QTimer::timeout,
            [this]()
            {
                update();
            });

    m_updateTimer.start(16);

    m_fpsTimer.start();
}

void CubeWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
}

void CubeWidget::resizeGL(int width, int height)
{
    if (height == 0)
        height = 1;

    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    const float aspect =
        static_cast<float>(width) /
        static_cast<float>(height);

    const float nearPlane = 0.1f;
    const float farPlane = 100.0f;
    const float fov = 45.0f;

    const float top =
        nearPlane * qTan(qDegreesToRadians(fov / 2.0f));

    const float bottom = -top;
    const float right = top * aspect;
    const float left = -right;

    glFrustum(
        left,
        right,
        bottom,
        top,
        nearPlane,
        farPlane
    );

    glMatrixMode(GL_MODELVIEW);
}

void CubeWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glTranslatef(0.0f, 0.0f, -5.0f);

    glRotatef(m_rotationX, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rotationY, 0.0f, 1.0f, 0.0f);

    drawCube();

    ++m_frameCount;

    const qint64 elapsed = m_fpsTimer.elapsed();

    if (elapsed >= 1000)
    {
        const int fps =
            static_cast<int>(
                (m_frameCount * 1000.0) /
                static_cast<double>(elapsed)
            );

        emit fpsChanged(fps);

        m_frameCount = 0;
        m_fpsTimer.restart();
    }
}

void CubeWidget::drawCube()
{
    glBegin(GL_QUADS);

    // Передняя грань
    glColor3f(1.0f, 0.0f, 0.0f);

    glVertex3f(-1.0f, -1.0f,  1.0f);
    glVertex3f( 1.0f, -1.0f,  1.0f);
    glVertex3f( 1.0f,  1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f,  1.0f);

    // Задняя грань
    glColor3f(0.0f, 1.0f, 0.0f);

    glVertex3f( 1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f,  1.0f, -1.0f);
    glVertex3f( 1.0f,  1.0f, -1.0f);

    // Левая грань
    glColor3f(0.0f, 0.0f, 1.0f);

    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f, -1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f, -1.0f);

    // Правая грань
    glColor3f(1.0f, 1.0f, 0.0f);

    glVertex3f(1.0f, -1.0f,  1.0f);
    glVertex3f(1.0f, -1.0f, -1.0f);
    glVertex3f(1.0f,  1.0f, -1.0f);
    glVertex3f(1.0f,  1.0f,  1.0f);

    // Верхняя грань
    glColor3f(1.0f, 0.0f, 1.0f);

    glVertex3f(-1.0f, 1.0f,  1.0f);
    glVertex3f( 1.0f, 1.0f,  1.0f);
    glVertex3f( 1.0f, 1.0f, -1.0f);
    glVertex3f(-1.0f, 1.0f, -1.0f);

    // Нижняя грань
    glColor3f(0.0f, 1.0f, 1.0f);

    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f,  1.0f);
    glVertex3f(-1.0f, -1.0f,  1.0f);

    glEnd();
}

void CubeWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_lastMousePosition = event->pos();
        event->accept();
        return;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void CubeWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        const QPoint currentPosition = event->pos();

        const int dx =
            currentPosition.x() - m_lastMousePosition.x();

        const int dy =
            currentPosition.y() - m_lastMousePosition.y();

        m_rotationY += dx * 0.5f;
        m_rotationX += dy * 0.5f;

        m_lastMousePosition = currentPosition;

        update();

        event->accept();
        return;
    }

    QOpenGLWidget::mouseMoveEvent(event);
}
