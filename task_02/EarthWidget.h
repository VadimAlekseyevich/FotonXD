#ifndef EARTHWIDGET_H
#define EARTHWIDGET_H

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>
#include <QTimer>

#include <memory>

class QMouseEvent;

class EarthWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit EarthWidget(const QString &mapDirectory = QString(), QWidget *parent = nullptr);
    ~EarthWidget() override;

signals:
    void fpsChanged(int fps);
    void textureStatusChanged(const QString &status);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    struct Vertex
    {
        float x;
        float y;
        float z;
        float u;
        float v;
    };

    bool initializeShaders();
    void initializeSphere();
    void initializeTexture();

    QString findBestTexture(int maxTextureSize, int &width, int &height) const;
    bool readImageSize(const QString &filePath, int &width, int &height) const;
    bool loadTexture(const QString &filePath, int expectedWidth, int expectedHeight);

    QString m_mapDirectory;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLBuffer> m_vertexBuffer;
    std::unique_ptr<QOpenGLBuffer> m_indexBuffer;

    QMatrix4x4 m_projection;
    GLuint m_textureId;
    int m_indexCount;
    bool m_hasTexture;

    float m_rotationX;
    float m_rotationY;
    QPoint m_lastMousePosition;

    QElapsedTimer m_fpsTimer;
    int m_frameCount;
    QTimer m_updateTimer;
};

#endif // EARTHWIDGET_H
