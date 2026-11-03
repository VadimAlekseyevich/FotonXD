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

class QEvent;
class QMouseEvent;
class QWheelEvent;
class QImage;
class GeoReference;

class EarthWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit EarthWidget(const QString &mapDirectory = QString(), QWidget *parent = nullptr);
    ~EarthWidget() override;

    bool loadRouteOverlay(const QImage &image,
                          const GeoReference &geoReference,
                          int originalWidth,
                          int originalHeight,
                          QString &error);

    void clearRouteOverlay();
    int routeTextureLimit() const;
    void centerOnCoordinates(double latitude, double longitude);

signals:
    void fpsChanged(int fps);
    void textureStatusChanged(const QString &status);
    void coordinatesChanged(bool onGlobe, double latitude, double longitude);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Vertex { float x, y, z, u, v; };
    bool initializeShaders();
    void initializeSphere();
    void initializeTexture();
    QMatrix4x4 modelViewMatrix() const;
    void updateCursorCoordinates(const QPoint &position);
    QString findBestTexture(int maxTextureSize, int &width, int &height) const;
    bool readImageSize(const QString &filePath, int &width, int &height) const;
    bool loadTexture(const QString &filePath, int expectedWidth, int expectedHeight);

    QString m_mapDirectory;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLBuffer> m_vertexBuffer;
    std::unique_ptr<QOpenGLBuffer> m_indexBuffer;
    QMatrix4x4 m_projection;
    GLuint m_textureId;
    GLuint m_routeTextureId;
    int m_indexCount;
    int m_routeIndexCount;
    int m_maxTextureSize;
    bool m_hasTexture;
    std::unique_ptr<QOpenGLBuffer> m_routeVertexBuffer;
    std::unique_ptr<QOpenGLBuffer> m_routeIndexBuffer;
    float m_rotationX;
    float m_rotationY;
    float m_cameraDistance;
    QPoint m_lastMousePosition;
    QPoint m_lastCursorPosition;
    bool m_hasCursorPosition;
    QElapsedTimer m_fpsTimer;
    int m_frameCount;
    QTimer m_updateTimer;
};
#endif // EARTHWIDGET_H
