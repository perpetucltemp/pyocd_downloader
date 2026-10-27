#include "PackParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QXmlStreamReader>

QVector<FlashRegion> PackParser::parseFlashLayout(const QString &packPath,
                                                    const QString &targetName)
{
    // 1. 解压 pack
    const QString tempDir = extractPack(packPath);
    if (tempDir.isEmpty()) {
        return {};
    }

    // 2. 找 PDSC 文件
    QDir dir(tempDir);
    const QStringList pdscFiles = dir.entryList(QStringList{"*.pdsc"}, QDir::Files);
    if (pdscFiles.isEmpty()) {
        return {};
    }

    // 3. 解析 PDSC
    QVector<FlashRegion> regions = parsePdsc(dir.absoluteFilePath(pdscFiles.first()),
                                              targetName);

    // 4. 为每个 Flash 区域生成扇区表
    for (FlashRegion &region : regions) {
        region.sectors = generateSectors(targetName, region.start, region.size);
    }

    // 清理临时目录
    QDir(tempDir).removeRecursively();

    return regions;
}

QString PackParser::extractPack(const QString &packPath)
{
    // pack 就是 zip，用 PowerShell 解压
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return {};
    }
    tempDir.setAutoRemove(false);  // 我们手动清理

    // 复制为 .zip 再解压
    const QString zipPath = tempDir.path() + "/pack.zip";
    if (!QFile::copy(packPath, zipPath)) {
        return {};
    }

    QProcess ps;
    ps.start("powershell", QStringList{
        "-NoProfile", "-Command",
        QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
            .arg(zipPath, tempDir.path())
    });
    if (!ps.waitForFinished(30000) || ps.exitCode() != 0) {
        return {};
    }

    return tempDir.path();
}

QVector<FlashRegion> PackParser::parsePdsc(const QString &pdscPath,
                                             const QString &targetName)
{
    QFile file(pdscPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QVector<FlashRegion> regions;
    QXmlStreamReader xml(&file);

    // 从 target 名提取 device 名: "stm32f407zgtx" -> "stm32f407zg"
    QString deviceName = targetName;
    deviceName.remove(QRegularExpression("[Tt][Xx]?$"));  // 去掉 Tx/X 后缀

    bool inDevice = false;

    while (!xml.atEnd() && !xml.hasError()) {
        const QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = xml.name();

            if (name == QLatin1String("device")) {
                const QString dname = xml.attributes().value("Dname").toString();
                // 大小写不敏感 + 前缀匹配:
                // PDSC: Dname="STM32F407ZG"  pyocd target: "stm32f407zgtx"
                // variant 名以 device 名开头
                if (dname.compare(deviceName, Qt::CaseInsensitive) == 0
                    || dname.compare(targetName, Qt::CaseInsensitive) == 0
                    || targetName.startsWith(dname, Qt::CaseInsensitive)) {
                    inDevice = true;
                }
            } else if (inDevice && name == QLatin1String("memory")) {
                const auto attrs = xml.attributes();
                // 兼容两种格式: STM32 用 name="Flash", HK32 用 id="IROM1"
                QString memName = attrs.value("name").toString();
                if (memName.isEmpty()) {
                    memName = attrs.value("id").toString();
                }
                const QString access = attrs.value("access").toString();
                const QString startStr = attrs.value("start").toString();
                const QString sizeStr = attrs.value("size").toString();
                const bool isDefault = attrs.value("default").toString() == "1";
                const bool isStartup = attrs.value("startup").toString() == "1";

                // 判断是否主 Flash:
                //   有 access 属性(STM32): access 含 "x"(可执行) 且 default=1
                //   无 access 属性(HK32): id=IROMx 或 startup=1
                bool isFlash;
                if (!access.isEmpty()) {
                    isFlash = access.contains("x") && isDefault;
                } else {
                    isFlash = isDefault && (isStartup
                            || memName.startsWith("IROM", Qt::CaseInsensitive)
                            || memName.contains("flash", Qt::CaseInsensitive));
                }

                if (isFlash) {
                    bool okStart = false, okSize = false;
                    const quint64 start = startStr.toULongLong(&okStart, 0);
                    const quint64 size = sizeStr.toULongLong(&okSize, 0);
                    if (okStart && okSize && size > 0) {
                        FlashRegion region;
                        region.name = memName;
                        region.start = start;
                        region.size = size;
                        regions.append(region);
                    }
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (xml.name() == QLatin1String("device") && inDevice) {
                inDevice = false;
                break;  // 找到目标 device 就退出
            }
        }
    }

    return regions;
}

QVector<FlashSector> PackParser::generateSectors(const QString &mcuName,
                                                   quint64 flashStart,
                                                   quint64 flashSize)
{
    // 先尝试内置扇区表
    QVector<FlashSector> sectors = builtinSectors(mcuName, flashStart, flashSize);
    if (!sectors.isEmpty()) {
        return sectors;
    }

    // 默认：按 2KB 扇区均分(适用于大多数 Cortex-M0/M3)
    const quint64 defaultSectorSize = 2048;
    const int count = static_cast<int>(flashSize / defaultSectorSize);
    for (int i = 0; i < count; ++i) {
        FlashSector s;
        s.address = flashStart + i * defaultSectorSize;
        s.size = defaultSectorSize;
        s.index = i;
        sectors.append(s);
    }
    return sectors;
}

QVector<FlashSector> PackParser::builtinSectors(const QString &mcuName,
                                                  quint64 flashStart,
                                                  quint64 flashSize)
{
    QVector<FlashSector> sectors;
    const QString name = mcuName.toUpper();

    // STM32F4 系列: 16KB x4 + 64KB x1 + 128KB xN
    if (name.startsWith("STM32F4")) {
        quint64 addr = flashStart;
        int idx = 0;

        // 4 x 16KB
        for (int i = 0; i < 4; ++i) {
            sectors.append({addr, 16 * 1024, idx++});
            addr += 16 * 1024;
        }
        // 1 x 64KB
        sectors.append({addr, 64 * 1024, idx++});
        addr += 64 * 1024;
        // 剩余按 128KB
        while (addr < flashStart + flashSize) {
            const quint64 remain = flashStart + flashSize - addr;
            const quint64 sz = qMin(remain, 128ULL * 1024);
            sectors.append({addr, sz, idx++});
            addr += sz;
        }
        return sectors;
    }

    // STM32F1 系列: 1KB 或 2KB 页
    if (name.startsWith("STM32F1")) {
        const quint64 pageSize = (flashSize <= 128 * 1024) ? 1024 : 2048;
        quint64 addr = flashStart;
        int idx = 0;
        while (addr < flashStart + flashSize) {
            sectors.append({addr, pageSize, idx++});
            addr += pageSize;
        }
        return sectors;
    }

    // STM32F0 系列: 1KB/2KB/4KB 页
    if (name.startsWith("STM32F0")) {
        quint64 pageSize = 1024;
        if (flashSize > 64 * 1024) pageSize = 2048;
        if (flashSize > 256 * 1024) pageSize = 4096;
        quint64 addr = flashStart;
        int idx = 0;
        while (addr < flashStart + flashSize) {
            sectors.append({addr, pageSize, idx++});
            addr += pageSize;
        }
        return sectors;
    }

    // STM32L4 系列: 2KB 页
    if (name.startsWith("STM32L4")) {
        quint64 addr = flashStart;
        int idx = 0;
        while (addr < flashStart + flashSize) {
            sectors.append({addr, 2048ULL, idx++});
            addr += 2048;
        }
        return sectors;
    }

    // GD32F4 系列: 同 STM32F4
    if (name.startsWith("GD32F4")) {
        quint64 addr = flashStart;
        int idx = 0;
        for (int i = 0; i < 4; ++i) {
            sectors.append({addr, 16 * 1024, idx++});
            addr += 16 * 1024;
        }
        sectors.append({addr, 64 * 1024, idx++});
        addr += 64 * 1024;
        while (addr < flashStart + flashSize) {
            const quint64 remain = flashStart + flashSize - addr;
            const quint64 sz = qMin(remain, 128ULL * 1024);
            sectors.append({addr, sz, idx++});
            addr += sz;
        }
        return sectors;
    }

    // GD32F1/F3 系列: 1KB/2KB 页
    if (name.startsWith("GD32F1") || name.startsWith("GD32F3")) {
        const quint64 pageSize = (flashSize <= 128 * 1024) ? 1024 : 2048;
        quint64 addr = flashStart;
        int idx = 0;
        while (addr < flashStart + flashSize) {
            sectors.append({addr, pageSize, idx++});
            addr += pageSize;
        }
        return sectors;
    }

    // HK32 系列: 通常 1KB/2KB 页
    if (name.startsWith("HK32")) {
        const quint64 pageSize = (flashSize <= 64 * 1024) ? 1024 : 2048;
        quint64 addr = flashStart;
        int idx = 0;
        while (addr < flashStart + flashSize) {
            sectors.append({addr, pageSize, idx++});
            addr += pageSize;
        }
        return sectors;
    }

    return sectors;  // 空，用默认
}
