#ifndef PACKPARSER_H
#define PACKPARSER_H

#include <QString>
#include <QStringList>
#include <QVector>

/// Flash 扇区信息
struct FlashSector {
    quint64 address;    ///< 起始地址
    quint64 size;       ///< 扇区大小(字节)
    int index;          ///< 扇区编号
};

/// Flash 区域信息
struct FlashRegion {
    QString name;       ///< 区域名(如 "Flash")
    quint64 start;      ///< 起始地址
    quint64 size;       ///< 总大小
    QVector<FlashSector> sectors;  ///< 扇区列表
};

/// 从 pack 的 PDSC 文件解析 MCU 的 Flash 布局
class PackParser
{
public:
    /// 解析指定 target 的 Flash 布局
    /// @param packPath pack 文件路径
    /// @param targetName target 名(如 "STM32F407ZGTx")
    /// @return Flash 区域列表，失败返回空
    static QVector<FlashRegion> parseFlashLayout(const QString &packPath,
                                                  const QString &targetName);

    /// 根据 MCU 型号和 Flash 大小生成扇区表
    /// 内置常见系列的扇区布局
    static QVector<FlashSector> generateSectors(const QString &mcuName,
                                                  quint64 flashStart,
                                                  quint64 flashSize);

private:
    /// 解压 pack 文件到临时目录
    static QString extractPack(const QString &packPath);
    /// 从 PDSC 文件查找 device 并解析 memory
    static QVector<FlashRegion> parsePdsc(const QString &pdscPath,
                                           const QString &targetName);
    /// 根据 MCU 名称匹配内置扇区表
    static QVector<FlashSector> builtinSectors(const QString &mcuName,
                                                 quint64 flashStart,
                                                 quint64 flashSize);
};

#endif // PACKPARSER_H
