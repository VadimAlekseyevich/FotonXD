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

// Предварительные объявления событий Qt, изображения и класса геопривязки.
class QEvent;
class QMouseEvent;
class QWheelEvent;
class QImage;
class GeoReference;

// Графический компонент: владеет OpenGL-ресурсами, камерой и данными наложения.
class EarthWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
// Нужно для сигналов FPS и координат.
    Q_OBJECT

public:
    explicit EarthWidget(const QString &mapDirectory = QString(), QWidget *parent = nullptr);
    ~EarthWidget() override;

// Создаём сетку маршрута по исходным размерам и объекту GeoReference.
    bool loadRouteOverlay(const QImage &image,
                          const GeoReference &geoReference,
                          int originalWidth,
                          int originalHeight,
                          QString &error);

// Удаляем предыдущую сетку и текстуру; базовый глобус остаётся.
    void clearRouteOverlay();
    int routeTextureLimit() const;
    void centerOnCoordinates(double latitude, double longitude);

// События передаются главному окну без прямого доступа к QStatusBar.
signals:
    void fpsChanged(int fps);
    void textureStatusChanged(const QString &status);
    void coordinatesChanged(bool onGlobe, double latitude, double longitude);

// Методы OpenGL и реакции на мышь/колёсико, вызываемые Qt.
protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;

// Реализация графики скрыта от MainWindow.
private:
// XYZ — положение в 3D; UV — положение пикселя на текстуре.
    struct Vertex { float x, y, z, u, v; };
    bool initializeShaders();
    void initializeSphere();
    void initializeTexture();
    QMatrix4x4 modelViewMatrix() const;
    void updateCursorCoordinates(const QPoint &position);
    QString findBestTexture(int maxTextureSize, int &width, int &height) const;
    bool readImageSize(const QString &filePath, int &width, int &height) const;
    bool loadTexture(const QString &filePath, int expectedWidth, int expectedHeight);

// Путь поиска карты, шейдер, буферы геометрии и матрица перспективы.
    QString m_mapDirectory;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLBuffer> m_vertexBuffer;
    std::unique_ptr<QOpenGLBuffer> m_indexBuffer;
    QMatrix4x4 m_projection;
// Отдельные идентификаторы текстур Земли и маршрута, размеры индексных массивов.
    GLuint m_textureId;
    GLuint m_routeTextureId;
    int m_indexCount;
    int m_routeIndexCount;
    int m_maxTextureSize;
    bool m_hasTexture;
    std::unique_ptr<QOpenGLBuffer> m_routeVertexBuffer;
    std::unique_ptr<QOpenGLBuffer> m_routeIndexBuffer;
// Положение камеры и ориентация глобуса управляются мышью.
    float m_rotationX;
    float m_rotationY;
    float m_cameraDistance;
    QPoint m_lastMousePosition;
    QPoint m_lastCursorPosition;
    bool m_hasCursorPosition;
// Таймер измерения FPS, счётчик кадров и таймер перерисовки.
    QElapsedTimer m_fpsTimer;
    int m_frameCount;
    QTimer m_updateTimer;
};
#endif // EARTHWIDGET_H
