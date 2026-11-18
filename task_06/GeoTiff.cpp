#include "GeoTiff.h"

#include <QByteArray>
#include <QFile>
#include <QMap>
#include <QVector>

#include <cstring>

// Вспомогательный разбор TIFF/GeoTIFF, не доступный из других файлов.
namespace
{
const quint16 TagImageWidth = 256;
const quint16 TagImageLength = 257;
const quint16 TagModelPixelScale = 33550;
const quint16 TagModelTiepoint = 33922;
const quint16 TagModelTransformation = 34264;
const quint16 TagGeoKeyDirectory = 34735;
const quint16 TagGeoDoubleParams = 34736;
const quint16 TagGdalNoData = 42113;

const quint16 GeoKeyModelType = 1024;
const quint16 GeoKeyRasterType = 1025;

const quint16 GeoKeyGeographicType = 2048;
const quint16 GeoKeyEllipsoid = 2056;
const quint16 GeoKeySemiMajorAxis = 2057;
const quint16 GeoKeyInvFlattening = 2059;

const quint16 GeoKeyProjectedCrs = 3072;
const quint16 GeoKeyCoordTransform = 3075;
const quint16 GeoKeyLinearUnits = 3076;
const quint16 GeoKeyNatOriginLong = 3080;
const quint16 GeoKeyNatOriginLat = 3081;
const quint16 GeoKeyFalseEasting = 3082;
const quint16 GeoKeyFalseNorthing = 3083;
const quint16 GeoKeyScaleAtNatOrigin = 3092;
const quint16 GeoKeyStraightVertPoleLong = 3095;

const quint16 RasterPixelIsPoint = 2;

// Элемент каталога IFD: тег, тип, число значений и их адрес.
struct TiffEntry
{
    quint16 tag;
    quint16 type;
    quint32 count;
    quint32 valueOffset;
    QByteArray inlineValue;
};

// Читаем число из двух байтов с учётом порядка байтов TIFF.
quint16 readU16(const QByteArray &data, int offset, bool littleEndian)
{
    const uchar b0 = static_cast<uchar>(data.at(offset));
    const uchar b1 = static_cast<uchar>(data.at(offset + 1));

    if (littleEndian)
        return static_cast<quint16>(b0 | (static_cast<quint16>(b1) << 8));

    return static_cast<quint16>((static_cast<quint16>(b0) << 8) | b1);
}

// Читаем 32-битное число в порядке little-endian или big-endian.
quint32 readU32(const QByteArray &data, int offset, bool littleEndian)
{
    quint32 value = 0;

    if (littleEndian)
    {
        for (int i = 3; i >= 0; --i)
        {
            value <<= 8;
            value |= static_cast<uchar>(data.at(offset + i));
        }
    }
    else
    {
        for (int i = 0; i < 4; ++i)
        {
            value <<= 8;
            value |= static_cast<uchar>(data.at(offset + i));
        }
    }

    return value;
}

// Читаем восемь байтов, сохраняя правильный порядок.
quint64 readU64(const QByteArray &data, int offset, bool littleEndian)
{
    quint64 value = 0;

    if (littleEndian)
    {
        for (int i = 7; i >= 0; --i)
        {
            value <<= 8;
            value |= static_cast<uchar>(data.at(offset + i));
        }
    }
    else
    {
        for (int i = 0; i < 8; ++i)
        {
            value <<= 8;
            value |= static_cast<uchar>(data.at(offset + i));
        }
    }

    return value;
}

// Преобразуем 64-битное представление в double для параметров проекции.
double readF64(const QByteArray &data, int offset, bool littleEndian)
{
    const quint64 bits = readU64(data, offset, littleEndian);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Определяем размер одного элемента TIFF-тега по его типу.
int typeSize(quint16 type)
{
    switch (type)
    {
    case 1:
    case 2:
    case 6:
    case 7:
        return 1;

    case 3:
    case 8:
        return 2;

    case 4:
    case 9:
    case 11:
        return 4;

    case 5:
    case 10:
    case 12:
        return 8;

    default:
        return 0;
    }
}

// Считываем указанное число байтов по смещению файла и проверяем длину.
bool readAt(QFile &file, quint32 offset, qint64 size, QByteArray &data)
{
    if (!file.seek(static_cast<qint64>(offset)))
        return false;

    data = file.read(size);
    return data.size() == size;
}

// Значение тега может помещаться прямо в IFD или лежать по смещению.
bool entryData(QFile &file,
               const TiffEntry &entry,
               QByteArray &data)
{
    const int itemSize = typeSize(entry.type);
    if (itemSize == 0)
        return false;

    const quint64 totalSize =
        static_cast<quint64>(itemSize) * static_cast<quint64>(entry.count);

    if (totalSize == 0 || totalSize > 16u * 1024u * 1024u)
        return false;

    if (totalSize <= 4)
    {
        data = entry.inlineValue.left(static_cast<int>(totalSize));
        return data.size() == static_cast<int>(totalSize);
    }

    return readAt(file,
                  entry.valueOffset,
                  static_cast<qint64>(totalSize),
                  data);
}

const TiffEntry *findEntry(const QVector<TiffEntry> &entries, quint16 tag)
{
    for (int i = 0; i < entries.size(); ++i)
    {
        if (entries.at(i).tag == tag)
            return &entries.at(i);
    }

    return 0;
}

// Извлекаем первое целочисленное значение из записи TIFF.
bool firstUnsigned(QFile &file,
                   const TiffEntry &entry,
                   bool littleEndian,
                   quint32 &value)
{
    QByteArray data;
    if (!entryData(file, entry, data))
        return false;

    if (entry.type == 3 && data.size() >= 2)
    {
        value = readU16(data, 0, littleEndian);
        return true;
    }

    if (entry.type == 4 && data.size() >= 4)
    {
        value = readU32(data, 0, littleEndian);
        return true;
    }

    return false;
}

// Получаем массив 16-битных значений, в частности GeoKeyDirectory.
bool shortValues(QFile &file,
                 const TiffEntry &entry,
                 bool littleEndian,
                 QVector<quint16> &values)
{
    if (entry.type != 3)
        return false;

    QByteArray data;
    if (!entryData(file, entry, data))
        return false;

    if (data.size() != static_cast<int>(entry.count * 2u))
        return false;

    values.clear();
    values.reserve(static_cast<int>(entry.count));

    for (quint32 i = 0; i < entry.count; ++i)
        values.push_back(readU16(data, static_cast<int>(i * 2u), littleEndian));

    return true;
}

// Получаем массив дробных параметров, записанных в GeoTIFF.
bool doubleValues(QFile &file,
                  const TiffEntry &entry,
                  bool littleEndian,
                  QVector<double> &values)
{
    if (entry.type != 12)
        return false;

    QByteArray data;
    if (!entryData(file, entry, data))
        return false;

    if (data.size() != static_cast<int>(entry.count * 8u))
        return false;

    values.clear();
    values.reserve(static_cast<int>(entry.count));

    for (quint32 i = 0; i < entry.count; ++i)
        values.push_back(readF64(data, static_cast<int>(i * 8u), littleEndian));

    return true;
}

// Извлекаем текстовый TIFF-тег, например GDAL_NODATA.
bool asciiValue(QFile &file,
                const TiffEntry &entry,
                QString &value)
{
    if (entry.type != 2)
        return false;

    QByteArray data;
    if (!entryData(file, entry, data))
        return false;

    const int zero = data.indexOf('\0');
    if (zero >= 0)
        data.truncate(zero);

    value = QString::fromLatin1(data).trimmed();
    return !value.isEmpty();
}

// По ссылке из географического ключа находим соответствующее значение double.
bool geoDoubleValue(const QVector<double> &doubleParams,
                    quint16 location,
                    quint16 count,
                    quint16 valueOffset,
                    double &value)
{
    if (location != TagGeoDoubleParams || count < 1)
        return false;

    const int index = static_cast<int>(valueOffset);

    if (index < 0 || index >= doubleParams.size())
        return false;

    value = doubleParams.at(index);
    return true;
}
}

// Обнуляем поля структуры, чтобы отличать отсутствующие данные от существующих.
GeoTiffMetadata::GeoTiffMetadata()
    : width(0),
      height(0),
      modelType(0),
      rasterType(0),
      geographicType(0),
      projectedCrs(0),
      ellipsoid(0),
      coordinateTransform(0),
      linearUnits(0),
      semiMajorAxis(0.0),
      inverseFlattening(0.0),
      latitudeOfOrigin(0.0),
      longitudeOfOrigin(0.0),
      straightVertPoleLongitude(0.0),
      scaleAtNaturalOrigin(0.0),
      falseEasting(0.0),
      falseNorthing(0.0),
      hasSemiMajorAxis(false),
      hasInverseFlattening(false),
      hasLatitudeOfOrigin(false),
      hasLongitudeOfOrigin(false),
      hasStraightVertPoleLongitude(false),
      hasScaleAtNaturalOrigin(false),
      hasFalseEasting(false),
      hasFalseNorthing(false),
      hasNoData(false),
      hasGeoTransform(false)
{
    for (int i = 0; i < 6; ++i)
        gt[i] = 0.0;
}

// Основной разбор файла: заголовок TIFF, каталог тегов, GeoKeys и геопривязка.
bool GeoTiffReader::read(const QString &filePath,
                         GeoTiffMetadata &metadata,
                         QString &error)
{
    metadata = GeoTiffMetadata();
    error.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = QString("Cannot open GeoTIFF: %1").arg(file.errorString());
        return false;
    }

// Читаем заголовок TIFF: порядок байтов, магическое число, смещение IFD.
    const QByteArray header = file.read(8);
    if (header.size() != 8)
    {
        error = "Invalid TIFF header";
        return false;
    }

    bool littleEndian = false;

    if (header.at(0) == 'I' && header.at(1) == 'I')
        littleEndian = true;
    else if (header.at(0) == 'M' && header.at(1) == 'M')
        littleEndian = false;
    else
    {
        error = "Invalid TIFF byte order";
        return false;
    }

    const quint16 magic = readU16(header, 2, littleEndian);

// BigTIFF здесь не поддерживается: структура такого файла отличается.
    if (magic == 43)
    {
        error = "BigTIFF is not supported by this task reader";
        return false;
    }

    if (magic != 42)
    {
        error = "File is not a classic TIFF";
        return false;
    }

    const quint32 ifdOffset = readU32(header, 4, littleEndian);

    QByteArray countBytes;
    if (!readAt(file, ifdOffset, 2, countBytes))
    {
        error = "Cannot read TIFF IFD";
        return false;
    }

    const quint16 entryCount = readU16(countBytes, 0, littleEndian);
    QVector<TiffEntry> entries;
    entries.reserve(entryCount);

// Извлекаем 12-байтные записи каталога TIFF.
    for (quint16 i = 0; i < entryCount; ++i)
    {
        QByteArray entryBytes;
        const quint32 entryOffset =
            ifdOffset + 2u + static_cast<quint32>(i) * 12u;

        if (!readAt(file, entryOffset, 12, entryBytes))
        {
            error = "Cannot read TIFF directory entry";
            return false;
        }

        TiffEntry entry;
        entry.tag = readU16(entryBytes, 0, littleEndian);
        entry.type = readU16(entryBytes, 2, littleEndian);
        entry.count = readU32(entryBytes, 4, littleEndian);
        entry.valueOffset = readU32(entryBytes, 8, littleEndian);
        entry.inlineValue = entryBytes.mid(8, 4);

        entries.push_back(entry);
    }

// Определяем размеры растра по тегам ImageWidth и ImageLength.
    const TiffEntry *widthEntry = findEntry(entries, TagImageWidth);
    const TiffEntry *heightEntry = findEntry(entries, TagImageLength);

    if (!widthEntry || !heightEntry)
    {
        error = "TIFF image size tags are missing";
        return false;
    }

    quint32 width = 0;
    quint32 height = 0;

    if (!firstUnsigned(file, *widthEntry, littleEndian, width) ||
        !firstUnsigned(file, *heightEntry, littleEndian, height))
    {
        error = "Cannot read TIFF image size";
        return false;
    }

    metadata.width = static_cast<int>(width);
    metadata.height = static_cast<int>(height);

// Необязательный ASCII-тег GDAL_NODATA задаёт значение фона.
    const TiffEntry *noDataEntry = findEntry(entries, TagGdalNoData);
    if (noDataEntry)
    {
        QString noDataText;
        if (asciiValue(file, *noDataEntry, noDataText))
        {
            metadata.noDataText = noDataText;
            metadata.hasNoData = true;
        }
    }

// GeoKeyDirectory содержит сведения о системе координат и проекции.
    const TiffEntry *geoKeyEntry = findEntry(entries, TagGeoKeyDirectory);
    if (!geoKeyEntry)
    {
        error = "GeoKeyDirectoryTag is missing; file is not a usable GeoTIFF";
        return false;
    }

    QVector<quint16> keys;
    if (!shortValues(file, *geoKeyEntry, littleEndian, keys) || keys.size() < 4)
    {
        error = "Invalid GeoKeyDirectoryTag";
        return false;
    }

// Параметры пользовательской проекции могут храниться в отдельном массиве double.
    QVector<double> doubleParams;
    const TiffEntry *doubleEntry = findEntry(entries, TagGeoDoubleParams);

    if (doubleEntry)
    {
        if (!doubleValues(file, *doubleEntry, littleEndian, doubleParams))
        {
            error = "Invalid GeoDoubleParamsTag";
            return false;
        }
    }

    const int numberOfKeys = keys.at(3);
    if (keys.size() < 4 + numberOfKeys * 4)
    {
        error = "Truncated GeoKeyDirectoryTag";
        return false;
    }

// Сортируем и читаем географические ключи по их идентификаторам.
    QMap<quint16, quint16> shortGeoKeys;

    for (int i = 0; i < numberOfKeys; ++i)
    {
        const int base = 4 + i * 4;
        const quint16 keyId = keys.at(base);
        const quint16 location = keys.at(base + 1);
        const quint16 count = keys.at(base + 2);
        const quint16 valueOffset = keys.at(base + 3);

        if (location == 0 && count == 1)
        {
            shortGeoKeys.insert(keyId, valueOffset);
            continue;
        }

        double value = 0.0;
        if (!geoDoubleValue(doubleParams,
                            location,
                            count,
                            valueOffset,
                            value))
        {
            continue;
        }

        switch (keyId)
        {
        case GeoKeySemiMajorAxis:
            metadata.semiMajorAxis = value;
            metadata.hasSemiMajorAxis = true;
            break;

        case GeoKeyInvFlattening:
            metadata.inverseFlattening = value;
            metadata.hasInverseFlattening = true;
            break;

        case GeoKeyNatOriginLong:
            metadata.longitudeOfOrigin = value;
            metadata.hasLongitudeOfOrigin = true;
            break;

        case GeoKeyNatOriginLat:
            metadata.latitudeOfOrigin = value;
            metadata.hasLatitudeOfOrigin = true;
            break;

        case GeoKeyFalseEasting:
            metadata.falseEasting = value;
            metadata.hasFalseEasting = true;
            break;

        case GeoKeyFalseNorthing:
            metadata.falseNorthing = value;
            metadata.hasFalseNorthing = true;
            break;

        case GeoKeyScaleAtNatOrigin:
            metadata.scaleAtNaturalOrigin = value;
            metadata.hasScaleAtNaturalOrigin = true;
            break;

        case GeoKeyStraightVertPoleLong:
            metadata.straightVertPoleLongitude = value;
            metadata.hasStraightVertPoleLongitude = true;
            break;

        default:
            break;
        }
    }

    metadata.modelType = shortGeoKeys.value(GeoKeyModelType, 0);
    metadata.rasterType = shortGeoKeys.value(GeoKeyRasterType, 0);
    metadata.geographicType = shortGeoKeys.value(GeoKeyGeographicType, 0);
    metadata.projectedCrs = shortGeoKeys.value(GeoKeyProjectedCrs, 0);
    metadata.ellipsoid = shortGeoKeys.value(GeoKeyEllipsoid, 0);
    metadata.coordinateTransform = shortGeoKeys.value(GeoKeyCoordTransform, 0);
    metadata.linearUnits = shortGeoKeys.value(GeoKeyLinearUnits, 0);

// Первый вариант геопривязки — готовая матрица координатного преобразования.
    const TiffEntry *matrixEntry = findEntry(entries, TagModelTransformation);

    if (matrixEntry)
    {
        QVector<double> matrix;

        if (!doubleValues(file, *matrixEntry, littleEndian, matrix) ||
            matrix.size() < 16)
        {
            error = "Invalid ModelTransformationTag";
            return false;
        }

        metadata.gt[0] = matrix.at(3);
        metadata.gt[1] = matrix.at(0);
        metadata.gt[2] = matrix.at(1);
        metadata.gt[3] = matrix.at(7);
        metadata.gt[4] = matrix.at(4);
        metadata.gt[5] = matrix.at(5);
        metadata.hasGeoTransform = true;
    }
    else
    {
// Второй вариант — размеры пикселя и опорная точка Tiepoint.
        const TiffEntry *scaleEntry = findEntry(entries, TagModelPixelScale);
        const TiffEntry *tieEntry = findEntry(entries, TagModelTiepoint);

        if (!scaleEntry || !tieEntry)
        {
            error =
                "GeoTIFF has neither ModelTransformationTag nor "
                "ModelPixelScaleTag + ModelTiepointTag";
            return false;
        }

        QVector<double> scale;
        QVector<double> tie;

        if (!doubleValues(file, *scaleEntry, littleEndian, scale) ||
            !doubleValues(file, *tieEntry, littleEndian, tie) ||
            scale.size() < 2 ||
            tie.size() < 6)
        {
            error = "Invalid GeoTIFF raster-to-model tags";
            return false;
        }

        const double rasterI = tie.at(0);
        const double rasterJ = tie.at(1);
        const double modelX = tie.at(3);
        const double modelY = tie.at(4);

        metadata.gt[0] = modelX - rasterI * scale.at(0);
        metadata.gt[1] = scale.at(0);
        metadata.gt[2] = 0.0;
        metadata.gt[3] = modelY + rasterJ * scale.at(1);
        metadata.gt[4] = 0.0;
        metadata.gt[5] = -scale.at(1);
        metadata.hasGeoTransform = true;
    }

    // PixelIsPoint хранит опорную точку в центре пикселя.
    // Для задания переводим (0,0) к верхнему левому углу первого пикселя.
// Приводим координаты центра пикселя к соглашению о его верхнем левом угле.
    if (metadata.rasterType == RasterPixelIsPoint)
    {
        metadata.gt[0] -= 0.5 * metadata.gt[1] + 0.5 * metadata.gt[2];
        metadata.gt[3] -= 0.5 * metadata.gt[4] + 0.5 * metadata.gt[5];
    }

    return true;
}
