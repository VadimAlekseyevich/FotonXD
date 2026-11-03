#include "MainWindow.h"
#include "EarthWidget.h"
#include "GeoTiff.h"
#include "GeoReference.h"
#include "RasterContrast.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTextCodec>
#include <QTextStream>

MainWindow::MainWindow(const QString &mapDirectory, QWidget *parent)
    : QMainWindow(parent),
      m_earthWidget(new EarthWidget(mapDirectory, this)),
      m_fps(0),
      m_onGlobe(false),
      m_latitude(0.0),
      m_longitude(0.0),
      m_textureStatus(QStringLiteral("Map: initialization...")),
      m_hasRoute(false),
      m_routeCenterLatitude(0.0),
      m_routeCenterLongitude(0.0)
{
    setWindowTitle(QStringLiteral("Task 6 - Earth routes"));
    resize(1100, 760);
    setCentralWidget(m_earthWidget);

    QMenu *fileMenu =
        menuBar()->addMenu(QString::fromUtf8("Файл"));
    QAction *openAction =
        fileMenu->addAction(QString::fromUtf8("Открыть изображение"));

    connect(openAction, &QAction::triggered,
            this, &MainWindow::openImage);

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

    connect(m_earthWidget, &EarthWidget::coordinatesChanged,
            this, [this](bool onGlobe, double latitude, double longitude)
            {
                m_onGlobe = onGlobe;
                m_latitude = latitude;
                m_longitude = longitude;
                refreshStatusBar();
            });

    refreshStatusBar();
}

void MainWindow::openImage()
{
    const QString imagePath =
        QFileDialog::getOpenFileName(
            this,
            QString::fromUtf8("Открыть маршрут GeoTIFF"),
            QDir::currentPath(),
            QStringLiteral("GeoTIFF (*.tif *.tiff);;All files (*)")
        );

    if (imagePath.isEmpty())
        return;

    // Task requirement: discard the previous route before opening a new one.
    m_earthWidget->clearRouteOverlay();
    m_hasRoute = false;
    setWindowTitle(QStringLiteral("Task 6 - Earth routes"));
    refreshStatusBar();

    GeoTiffMetadata metadata;
    GeoReference geoReference;
    QString error;

    if (!GeoTiffReader::read(imagePath, metadata, error) ||
        !geoReference.initialize(metadata, error))
    {
        QMessageBox::warning(
            this, QString::fromUtf8("Ошибка геопривязки"), error
        );
        return;
    }

    const QFileInfo inputInfo(imagePath);
    const QString outputPath =
        inputInfo.dir().absoluteFilePath(
            inputInfo.completeBaseName() + QStringLiteral("_8bit.tif")
        );

    QTextStream output(stdout);
#ifdef Q_OS_WIN
    output.setCodec(QTextCodec::codecForLocale());
#else
    output.setCodec("UTF-8");
#endif

    output << "Opening route: " << imagePath << "\n";
    output.flush();

    ContrastResult contrastResult;
    QImage routePreview;

    if (!RasterContrast::process(
            imagePath,
            outputPath,
            metadata.hasNoData,
            metadata.noDataText,
            contrastResult,
            output,
            error,
            &routePreview,
            m_earthWidget->routeTextureLimit()))
    {
        QMessageBox::warning(
            this, QString::fromUtf8("Ошибка контрастирования"), error
        );
        return;
    }

    double centerLatitude = 0.0;
    double centerLongitude = 0.0;

    if (!geoReference.planar2geo(
            metadata.width * 0.5,
            metadata.height * 0.5,
            centerLatitude,
            centerLongitude))
    {
        QMessageBox::warning(
            this,
            QString::fromUtf8("Ошибка маршрута"),
            QString::fromUtf8("Не удалось определить координаты центра.")
        );
        return;
    }

    if (!m_earthWidget->loadRouteOverlay(
            routePreview, geoReference,
            metadata.width, metadata.height, error))
    {
        QMessageBox::warning(
            this, QString::fromUtf8("Ошибка OpenGL"), error
        );
        return;
    }

    m_routeCenterLatitude = centerLatitude;
    m_routeCenterLongitude = centerLongitude;
    m_hasRoute = true;

    m_earthWidget->centerOnCoordinates(
        centerLatitude, centerLongitude
    );

    setWindowTitle(
        QStringLiteral("Task 6 - Earth routes - %1")
            .arg(inputInfo.fileName())
    );

    refreshStatusBar();
}

void MainWindow::refreshStatusBar()
{
    const QString cursorCoordinates = m_onGlobe
        ? QStringLiteral("Lat: %1, Lon: %2")
            .arg(m_latitude, 0, 'f', 6)
            .arg(m_longitude, 0, 'f', 6)
        : QStringLiteral("Lat: --, Lon: --");

    const QString routeCoordinates = m_hasRoute
        ? QStringLiteral("Route center: %1, %2")
            .arg(m_routeCenterLatitude, 0, 'f', 6)
            .arg(m_routeCenterLongitude, 0, 'f', 6)
        : QStringLiteral("Route: none");

    statusBar()->showMessage(
        QStringLiteral("FPS: %1 | %2 | %3 | %4")
            .arg(m_fps)
            .arg(cursorCoordinates)
            .arg(routeCoordinates)
            .arg(m_textureStatus)
    );
}
