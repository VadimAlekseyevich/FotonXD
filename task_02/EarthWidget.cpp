#include "EarthWidget.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QVector>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>

namespace
{
const float Pi = 3.14159265358979323846f;
const float MinCameraDistance = 1.15f;
const float MaxCameraDistance = 12.0f;
const float ZoomFactorPerStep = 0.82f;
}

EarthWidget::EarthWidget(const QString &mapDirectory, QWidget *parent)
    : QOpenGLWidget(parent),
      m_mapDirectory(mapDirectory),
      m_textureId(0),
      m_indexCount(0),
      m_hasTexture(false),
      m_rotationX(15.0f),
      m_rotationY(0.0f),
      m_cameraDistance(3.2f),
      m_frameCount(0)
{
    setFocusPolicy(Qt::StrongFocus);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    m_updateTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_updateTimer, &QTimer::timeout,
            this, [this]()
            {
                update();
            });
    m_updateTimer.start(0);

    m_fpsTimer.start();
}

EarthWidget::~EarthWidget()
{
    m_updateTimer.stop();

    if (context())
    {
        makeCurrent();

        if (m_textureId != 0)
        {
            glDeleteTextures(1, &m_textureId);
            m_textureId = 0;
        }

        if (m_indexBuffer)
            m_indexBuffer->destroy();

        if (m_vertexBuffer)
            m_vertexBuffer->destroy();

        m_program.reset();
        m_indexBuffer.reset();
        m_vertexBuffer.reset();

        doneCurrent();
    }
}

void EarthWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.04f, 0.05f, 0.08f, 1.0f);

    if (!initializeShaders())
    {
        emit textureStatusChanged(QStringLiteral("OpenGL shader initialization failed"));
        return;
    }

    initializeSphere();
    initializeTexture();
}

void EarthWidget::resizeGL(int width, int height)
{
    if (height <= 0)
        height = 1;

    glViewport(0, 0, width, height);

    m_projection.setToIdentity();
    m_projection.perspective(
        45.0f,
        static_cast<float>(width) / static_cast<float>(height),
        0.1f,
        100.0f
    );
}

void EarthWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_program || !m_program->isLinked() ||
        !m_vertexBuffer || !m_indexBuffer || m_indexCount == 0)
    {
        return;
    }

    QMatrix4x4 modelView;
    modelView.translate(0.0f, 0.0f, -m_cameraDistance);
    modelView.rotate(m_rotationX, 1.0f, 0.0f, 0.0f);
    modelView.rotate(m_rotationY, 0.0f, 1.0f, 0.0f);

    const QMatrix4x4 mvp = m_projection * modelView;

    m_program->bind();
    m_program->setUniformValue("u_mvp", mvp);
    m_program->setUniformValue("u_texture", 0);
    m_program->setUniformValue("u_hasTexture", m_hasTexture ? 1 : 0);

    if (m_hasTexture)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textureId);
    }

    m_vertexBuffer->bind();
    m_indexBuffer->bind();

    const int positionAttribute = m_program->attributeLocation("a_position");
    const int texCoordAttribute = m_program->attributeLocation("a_texCoord");

    m_program->enableAttributeArray(positionAttribute);
    m_program->setAttributeBuffer(
        positionAttribute,
        GL_FLOAT,
        0,
        3,
        static_cast<int>(sizeof(Vertex))
    );

    m_program->enableAttributeArray(texCoordAttribute);
    m_program->setAttributeBuffer(
        texCoordAttribute,
        GL_FLOAT,
        3 * static_cast<int>(sizeof(float)),
        2,
        static_cast<int>(sizeof(Vertex))
    );

    glDrawElements(
        GL_TRIANGLES,
        m_indexCount,
        GL_UNSIGNED_INT,
        0
    );

    m_program->disableAttributeArray(positionAttribute);
    m_program->disableAttributeArray(texCoordAttribute);

    m_indexBuffer->release();
    m_vertexBuffer->release();

    if (m_hasTexture)
        glBindTexture(GL_TEXTURE_2D, 0);

    m_program->release();

    ++m_frameCount;
    const qint64 elapsedMs = m_fpsTimer.elapsed();

    if (elapsedMs >= 1000)
    {
        const int fps = qRound(
            static_cast<double>(m_frameCount) * 1000.0 /
            static_cast<double>(elapsedMs)
        );

        emit fpsChanged(fps);
        m_frameCount = 0;
        m_fpsTimer.restart();
    }
}

void EarthWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        float zoomSteps = 0.0f;

        if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal)
            zoomSteps = 1.0f;
        else if (event->key() == Qt::Key_Minus)
            zoomSteps = -1.0f;

        if (zoomSteps != 0.0f)
        {
            const float zoomMultiplier = static_cast<float>(
                std::pow(static_cast<double>(ZoomFactorPerStep),
                         static_cast<double>(zoomSteps))
            );

            m_cameraDistance *= zoomMultiplier;

            if (m_cameraDistance < MinCameraDistance)
                m_cameraDistance = MinCameraDistance;
            else if (m_cameraDistance > MaxCameraDistance)
                m_cameraDistance = MaxCameraDistance;

            update();
            event->accept();
            return;
        }
    }

    QOpenGLWidget::keyPressEvent(event);
}

void EarthWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_lastMousePosition = event->pos();
        event->accept();
        return;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void EarthWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        const QPoint currentPosition = event->pos();
        const QPoint delta = currentPosition - m_lastMousePosition;

        m_rotationY += static_cast<float>(delta.x()) * 0.45f;
        m_rotationX += static_cast<float>(delta.y()) * 0.45f;
        m_lastMousePosition = currentPosition;

        update();
        event->accept();
        return;
    }

    QOpenGLWidget::mouseMoveEvent(event);
}

void EarthWidget::wheelEvent(QWheelEvent *event)
{
    const int wheelDelta = event->angleDelta().y();
    if (wheelDelta == 0)
    {
        QOpenGLWidget::wheelEvent(event);
        return;
    }

    const float wheelSteps = static_cast<float>(wheelDelta) / 120.0f;
    const float zoomMultiplier = static_cast<float>(
        std::pow(static_cast<double>(ZoomFactorPerStep),
                 static_cast<double>(wheelSteps))
    );

    m_cameraDistance *= zoomMultiplier;

    if (m_cameraDistance < MinCameraDistance)
        m_cameraDistance = MinCameraDistance;
    else if (m_cameraDistance > MaxCameraDistance)
        m_cameraDistance = MaxCameraDistance;

    update();
    event->accept();
}

bool EarthWidget::initializeShaders()
{
    static const char *vertexShaderSource = R"(
        #version 120

        attribute vec3 a_position;
        attribute vec2 a_texCoord;

        uniform mat4 u_mvp;

        varying vec2 v_texCoord;

        void main()
        {
            gl_Position = u_mvp * vec4(a_position, 1.0);
            v_texCoord = a_texCoord;
        }
    )";

    static const char *fragmentShaderSource = R"(
        #version 120

        uniform sampler2D u_texture;
        uniform int u_hasTexture;

        varying vec2 v_texCoord;

        void main()
        {
            if (u_hasTexture != 0)
                gl_FragColor = texture2D(u_texture, v_texCoord);
            else
                gl_FragColor = vec4(0.15, 0.38, 0.65, 1.0);
        }
    )";

    m_program.reset(new QOpenGLShaderProgram());

    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource))
    {
        qWarning() << "Vertex shader error:" << m_program->log();
        return false;
    }

    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource))
    {
        qWarning() << "Fragment shader error:" << m_program->log();
        return false;
    }

    if (!m_program->link())
    {
        qWarning() << "Shader link error:" << m_program->log();
        return false;
    }

    return true;
}

void EarthWidget::initializeSphere()
{
    const int longitudeSegments = 256;
    const int latitudeSegments = 128;

    QVector<Vertex> vertices;
    QVector<quint32> indices;

    vertices.reserve((latitudeSegments + 1) * (longitudeSegments + 1));
    indices.reserve(latitudeSegments * longitudeSegments * 6);

    for (int latitudeIndex = 0; latitudeIndex <= latitudeSegments; ++latitudeIndex)
    {
        const float v = static_cast<float>(latitudeIndex) /
                        static_cast<float>(latitudeSegments);
        const float latitude = Pi * 0.5f - v * Pi;

        const float cosLatitude = std::cos(latitude);
        const float sinLatitude = std::sin(latitude);

        for (int longitudeIndex = 0; longitudeIndex <= longitudeSegments; ++longitudeIndex)
        {
            const float u = static_cast<float>(longitudeIndex) /
                            static_cast<float>(longitudeSegments);

            const float longitude = -Pi + u * 2.0f * Pi;
            const float sinLongitude = std::sin(longitude);
            const float cosLongitude = std::cos(longitude);

            Vertex vertex;
            vertex.x = cosLatitude * sinLongitude;
            vertex.y = sinLatitude;
            vertex.z = cosLatitude * cosLongitude;
            vertex.u = u;
            vertex.v = v;

            vertices.push_back(vertex);
        }
    }

    const int rowSize = longitudeSegments + 1;

    for (int latitudeIndex = 0; latitudeIndex < latitudeSegments; ++latitudeIndex)
    {
        for (int longitudeIndex = 0; longitudeIndex < longitudeSegments; ++longitudeIndex)
        {
            const quint32 topLeft = static_cast<quint32>(
                latitudeIndex * rowSize + longitudeIndex
            );
            const quint32 bottomLeft = static_cast<quint32>(
                (latitudeIndex + 1) * rowSize + longitudeIndex
            );
            const quint32 topRight = topLeft + 1;
            const quint32 bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    m_indexCount = indices.size();

    m_vertexBuffer.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    m_vertexBuffer->create();
    m_vertexBuffer->bind();
    m_vertexBuffer->setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_vertexBuffer->allocate(
        vertices.constData(),
        static_cast<int>(vertices.size() * sizeof(Vertex))
    );
    m_vertexBuffer->release();

    m_indexBuffer.reset(new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer));
    m_indexBuffer->create();
    m_indexBuffer->bind();
    m_indexBuffer->setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_indexBuffer->allocate(
        indices.constData(),
        static_cast<int>(indices.size() * sizeof(quint32))
    );
    m_indexBuffer->release();
}

void EarthWidget::initializeTexture()
{
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);

    int width = 0;
    int height = 0;
    const QString texturePath = findBestTexture(maxTextureSize, width, height);

    if (texturePath.isEmpty())
    {
        emit textureStatusChanged(
            QStringLiteral("Map: no readable Map_*.tif(f) found; GL_MAX_TEXTURE_SIZE=%1")
                .arg(maxTextureSize)
        );
        return;
    }

    if (!loadTexture(texturePath, width, height))
    {
        emit textureStatusChanged(
            QStringLiteral("Map: failed to load %1")
                .arg(QFileInfo(texturePath).fileName())
        );
        return;
    }

    m_hasTexture = true;

    emit textureStatusChanged(
        QStringLiteral("Map: %1 [%2x%3], GL max=%4")
            .arg(QFileInfo(texturePath).fileName())
            .arg(width)
            .arg(height)
            .arg(maxTextureSize)
    );
}

QString EarthWidget::findBestTexture(int maxTextureSize, int &width, int &height) const
{
    width = 0;
    height = 0;

    QString bestPath;
    quint64 bestPixelCount = 0;

    const auto considerFile = [maxTextureSize, &bestPath, &bestPixelCount, &width, &height, this]
                              (const QString &filePath)
    {
        const QFileInfo fileInfo(filePath);
        const QString lowerName = fileInfo.fileName().toLower();

        if (!lowerName.startsWith(QStringLiteral("map_")) ||
            !(lowerName.endsWith(QStringLiteral(".tif")) ||
              lowerName.endsWith(QStringLiteral(".tiff"))))
        {
            return;
        }

        int imageWidth = 0;
        int imageHeight = 0;

        if (!readImageSize(fileInfo.absoluteFilePath(), imageWidth, imageHeight))
            return;

        if (imageWidth <= 0 || imageHeight <= 0 ||
            imageWidth > maxTextureSize || imageHeight > maxTextureSize)
        {
            return;
        }

        const quint64 pixelCount =
            static_cast<quint64>(imageWidth) * static_cast<quint64>(imageHeight);

        if (pixelCount > bestPixelCount)
        {
            bestPixelCount = pixelCount;
            bestPath = fileInfo.absoluteFilePath();
            width = imageWidth;
            height = imageHeight;
        }
    };

    if (!m_mapDirectory.isEmpty())
    {
        const QFileInfo requestedPath(m_mapDirectory);

        if (requestedPath.isFile())
            considerFile(requestedPath.absoluteFilePath());
    }

    QStringList directories;

    const auto addDirectory = [&directories](const QString &path)
    {
        if (path.isEmpty())
            return;

        const QString absolutePath = QDir(path).absolutePath();
        if (!directories.contains(absolutePath))
            directories.push_back(absolutePath);
    };

    if (!m_mapDirectory.isEmpty() && QFileInfo(m_mapDirectory).isDir())
        addDirectory(m_mapDirectory);

    QStringList searchRoots;
    searchRoots << QDir::currentPath()
                << QCoreApplication::applicationDirPath();

    for (int rootIndex = 0; rootIndex < searchRoots.size(); ++rootIndex)
    {
        QDir root(searchRoots.at(rootIndex));

        for (int level = 0; level < 6; ++level)
        {
            addDirectory(root.absolutePath());
            addDirectory(root.filePath(QStringLiteral("maps")));
            addDirectory(root.filePath(QStringLiteral("task_02/maps")));

            if (!root.cdUp())
                break;
        }
    }

    for (int directoryIndex = 0; directoryIndex < directories.size(); ++directoryIndex)
    {
        const QDir directory(directories.at(directoryIndex));
        if (!directory.exists())
            continue;

        const QFileInfoList files = directory.entryInfoList(
            QDir::Files | QDir::Readable,
            QDir::Name
        );

        for (int fileIndex = 0; fileIndex < files.size(); ++fileIndex)
            considerFile(files.at(fileIndex).absoluteFilePath());
    }

    return bestPath;
}

bool EarthWidget::readImageSize(const QString &filePath, int &width, int &height) const
{
    width = 0;
    height = 0;

    QImageReader reader(filePath);
    const QSize imageSize = reader.size();

    if (imageSize.isValid())
    {
        width = imageSize.width();
        height = imageSize.height();
        return true;
    }

    const QImage image = reader.read();
    if (image.isNull())
    {
        qWarning() << "Cannot read image" << filePath << reader.errorString();
        return false;
    }

    width = image.width();
    height = image.height();
    return true;
}

bool EarthWidget::loadTexture(const QString &filePath,
                              int expectedWidth,
                              int expectedHeight)
{
    QImageReader reader(filePath);
    QImage image = reader.read();

    if (image.isNull())
    {
        qWarning() << "Cannot load texture" << filePath << reader.errorString();
        return false;
    }

    if (image.width() != expectedWidth || image.height() != expectedHeight)
        return false;

    image = image.convertToFormat(QImage::Format_RGBA8888);

    if (m_textureId != 0)
        glDeleteTextures(1, &m_textureId);

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        image.width(),
        image.height(),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        image.constBits()
    );

    glBindTexture(GL_TEXTURE_2D, 0);

    return glGetError() == GL_NO_ERROR;
}