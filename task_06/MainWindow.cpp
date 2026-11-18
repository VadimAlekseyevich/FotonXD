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

// Конструктор настраивает меню, графический компонент и сигналы состояния.
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
// Всю центральную область окна занимает Земля, рисуемая в EarthWidget.
    setCentralWidget(m_earthWidget);

// Добавляем меню и действие выбора GeoTIFF.
    QMenu *fileMenu =
        menuBar()->addMenu(QString::fromUtf8("Файл"));
    QAction *openAction =
        fileMenu->addAction(QString::fromUtf8("Открыть изображение"));

// Связываем нажатие пункта меню со слотом openImage().
    connect(openAction, &QAction::triggered,
            this, &MainWindow::openImage);

// При новом измерении FPS сохраняем число и обновляем QStatusBar.
    connect(m_earthWidget, &EarthWidget::fpsChanged,
            this, [this](int fps)
            {
                m_fps = fps;
                refreshStatusBar();
            });

// Получаем от графического компонента информацию о загрузке базовой карты.
    connect(m_earthWidget, &EarthWidget::textureStatusChanged,
            this, [this](const QString &status)
            {
                m_textureStatus = status;
                refreshStatusBar();
            });

// Координаты курсора приходят из EarthWidget через сигнал Qt.
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

// Полный сценарий загрузки: выбрать файл, прочитать геопривязку, контрастировать, наложить на Землю.
void MainWindow::openImage()
{
// Выбор исходного TIFF через системный диалог.
    const QString imagePath =
        QFileDialog::getOpenFileName(
            this,
            QString::fromUtf8("Открыть маршрут GeoTIFF"),
            QDir::currentPath(),
            QStringLiteral("GeoTIFF (*.tif *.tiff);;All files (*)")
        );

// При отмене выбора выходим, ничего не меняя.
    if (imagePath.isEmpty())
        return;

    // По условию задания предыдущий маршрут удаляется перед открытием нового.
// До чтения нового файла удаляем прежние OpenGL-ресурсы маршрута.
    m_earthWidget->clearRouteOverlay();
    m_hasRoute = false;
    setWindowTitle(QStringLiteral("Task 6 - Earth routes"));
    refreshStatusBar();

// Метаданные GeoTIFF и GeoReference отвечают за координаты, не за цвет пикселей.
    GeoTiffMetadata metadata;
    GeoReference geoReference;
    QString error;

// Проверяем чтение географических тегов и инициализацию проекции.
    if (!GeoTiffReader::read(imagePath, metadata, error) ||
        !geoReference.initialize(metadata, error))
    {
        QMessageBox::warning(
            this, QString::fromUtf8("Ошибка геопривязки"), error
        );
        return;
    }

// Выходной TIFF имеет суффикс _8bit и не заменяет исходник.
    const QFileInfo inputInfo(imagePath);
    const QString outputPath =
        inputInfo.dir().absoluteFilePath(
            inputInfo.completeBaseName() + QStringLiteral("_8bit.tif")
        );

// Статистика гистограмм и прогресс выводятся в консоль.
    QTextStream output(stdout);
#ifdef Q_OS_WIN
    output.setCodec(QTextCodec::codecForLocale());
#else
    output.setCodec("UTF-8");
#endif

    output << "Opening route: " << imagePath << "\n";
    output.flush();

// Контрастирование возвращает статистику и уменьшенную RGBA-текстуру.
    ContrastResult contrastResult;
    QImage routePreview;

// Читаем пиксели, исключаем NoData, растягиваем контраст и готовим маршрут к отрисовке.
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

// Определяем широту и долготу центрального пикселя исходного растра.
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

// Создаём географическую сетку и загружаем RGBA-текстуру в OpenGL.
    if (!m_earthWidget->loadRouteOverlay(
            routePreview, geoReference,
            metadata.width, metadata.height, error))
    {
        QMessageBox::warning(
            this, QString::fromUtf8("Ошибка OpenGL"), error
        );
        return;
    }

// После успеха помечаем маршрут открытым и поворачиваем камеру к его центру.
    m_routeCenterLatitude = centerLatitude;
    m_routeCenterLongitude = centerLongitude;
    m_hasRoute = true;

    m_earthWidget->centerOnCoordinates(
        centerLatitude, centerLongitude
    );

// Показываем исходное имя файла в заголовке окна.
    setWindowTitle(
        QStringLiteral("Task 6 - Earth routes - %1")
            .arg(inputInfo.fileName())
    );

    refreshStatusBar();
}

// Формируем текст статуса из четырёх частей: FPS, курсор, центр маршрута, базовая карта.
void MainWindow::refreshStatusBar()
{
// Если луч курсора не попал в Землю, вместо координат выводятся прочерки.
    const QString cursorCoordinates = m_onGlobe
        ? QStringLiteral("Lat: %1, Lon: %2")
            .arg(m_latitude, 0, 'f', 6)
            .arg(m_longitude, 0, 'f', 6)
        : QStringLiteral("Lat: --, Lon: --");

// Географический центр маршрута отображается с шестью знаками после запятой.
    const QString routeCoordinates = m_hasRoute
        ? QStringLiteral("Route center: %1, %2")
            .arg(m_routeCenterLatitude, 0, 'f', 6)
            .arg(m_routeCenterLongitude, 0, 'f', 6)
        : QStringLiteral("Route: none");

// Выводим объединённую строку в QStatusBar.
    statusBar()->showMessage(
        QStringLiteral("FPS: %1 | %2 | %3 | %4")
            .arg(m_fps)
            .arg(cursorCoordinates)
            .arg(routeCoordinates)
            .arg(m_textureStatus)
    );
}
