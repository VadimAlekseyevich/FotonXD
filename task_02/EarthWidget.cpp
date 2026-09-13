#include "EarthWidget.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QMouseEvent>
#include <QVector>
#include <QtMath>
#include <QtGlobal>

#include <tiffio.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace
{
constexpr float Pi = 3.14159265358979323846f;

TIFF *openTiffForRead(const QString &filePath)
{
#ifdef Q_OS_WIN
    const std::wstring widePath = QDir::toNativeSeparators(filePath).toStdWString();
    return TIFFOpenW(widePath.c_str(), "r");
#else
    const QByteArray encodedPath = QFile::encodeName(filePath);
    return TIFFOpen(encodedPath.constData(), "r");
#endif
}
}

EarthWidget::EarthWidget(const QString &mapDirectory, QWidget *parent)
    : QOpenGLWidget(parent),
      m_mapDirectory(mapDirectory),
      m_textureId(0),
      m_indexCount(0),
      m_hasTexture(false),
      m_rotationX(15.0f),
      m_rotationY(0.0f),
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
    modelView.translate(0.0f, 0.0f, -3.2f);
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
        nullptr
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
    constexpr int longitudeSegments = 256;
    constexpr int latitudeSegments = 128;

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
        static_cast<int>(vertices.size() * static_cast<int>(sizeof(Vertex)))
    );
    m_vertexBuffer->release();

    m_indexBuffer.reset(new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer));
    m_indexBuffer->create();
    m_indexBuffer->bind();
    m_indexBuffer->setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_indexBuffer->allocate(
        indices.constData(),
        static_cast<int>(indices.size() * static_cast<int>(sizeof(quint32)))
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
            QStringLiteral("Map: no suitable Map_*.tif(f) found; GL_MAX_TEXTURE_SIZE=%1")
                .arg(maxTextureSize)
        );
        return;
    }

    if (!loadTiffTexture(texturePath, width, height))
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

    const auto considerFile = [this, maxTextureSize, &bestPath, &bestPixelCount, &width, &height]
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

        if (!readTiffSize(fileInfo.absoluteFilePath(), imageWidth, imageHeight))
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

#ifdef FOTON_TASK2_SOURCE_DIR
    addDirectory(QStringLiteral(FOTON_TASK2_SOURCE_DIR) + QStringLiteral("/maps"));
#endif

    addDirectory(QDir::current().filePath(QStringLiteral("maps")));
    addDirectory(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("maps")));

    for (const QString &directoryPath : directories)
    {
        const QDir directory(directoryPath);
        if (!directory.exists())
            continue;

        const QFileInfoList files = directory.entryInfoList(
            QDir::Files | QDir::Readable,
            QDir::Name
        );

        for (const QFileInfo &fileInfo : files)
            considerFile(fileInfo.absoluteFilePath());
    }

    return bestPath;
}

bool EarthWidget::readTiffSize(const QString &filePath, int &width, int &height) const
{
    width = 0;
    height = 0;

    TIFF *tiff = openTiffForRead(filePath);
    if (!tiff)
        return false;

    uint32_t imageWidth = 0;
    uint32_t imageHeight = 0;

    const bool ok =
        TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &imageWidth) == 1 &&
        TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &imageHeight) == 1;

    TIFFClose(tiff);

    if (!ok ||
        imageWidth > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        imageHeight > static_cast<uint32_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    width = static_cast<int>(imageWidth);
    height = static_cast<int>(imageHeight);
    return true;
}

bool EarthWidget::loadTiffTexture(const QString &filePath,
                                  int expectedWidth,
                                  int expectedHeight)
{
    TIFF *tiff = openTiffForRead(filePath);
    if (!tiff)
        return false;

    uint32_t imageWidth = 0;
    uint32_t imageHeight = 0;

    if (TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &imageWidth) != 1 ||
        TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &imageHeight) != 1)
    {
        TIFFClose(tiff);
        return false;
    }

    if (static_cast<int>(imageWidth) != expectedWidth ||
        static_cast<int>(imageHeight) != expectedHeight)
    {
        TIFFClose(tiff);
        return false;
    }

    const quint64 pixelCount =
        static_cast<quint64>(imageWidth) * static_cast<quint64>(imageHeight);

    if (pixelCount == 0 ||
        pixelCount > static_cast<quint64>(std::numeric_limits<int>::max()))
    {
        TIFFClose(tiff);
        return false;
    }

    QVector<uint32_t> raster(static_cast<int>(pixelCount));

    const int readResult = TIFFReadRGBAImageOriented(
        tiff,
        imageWidth,
        imageHeight,
        raster.data(),
        ORIENTATION_TOPLEFT,
        0
    );

    TIFFClose(tiff);

    if (readResult == 0)
        return false;

    if (m_textureId != 0)
        glDeleteTextures(1, &m_textureId);

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    const void *pixelData = raster.constData();
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        static_cast<GLsizei>(imageWidth),
        static_cast<GLsizei>(imageHeight),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixelData
    );
#else
    const quint64 byteCount = pixelCount * 4u;
    if (byteCount > static_cast<quint64>(std::numeric_limits<int>::max()))
    {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
        return false;
    }

    QByteArray rgbaBytes;
    rgbaBytes.resize(static_cast<int>(byteCount));
    unsigned char *destination = reinterpret_cast<unsigned char *>(rgbaBytes.data());

    for (int i = 0; i < raster.size(); ++i)
    {
        const uint32_t pixel = raster.at(i);
        destination[i * 4 + 0] = TIFFGetR(pixel);
        destination[i * 4 + 1] = TIFFGetG(pixel);
        destination[i * 4 + 2] = TIFFGetB(pixel);
        destination[i * 4 + 3] = TIFFGetA(pixel);
    }

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        static_cast<GLsizei>(imageWidth),
        static_cast<GLsizei>(imageHeight),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgbaBytes.constData()
    );
#endif

    glBindTexture(GL_TEXTURE_2D, 0);
    return glGetError() == GL_NO_ERROR;
}
