#ifndef FIRMWAREPARSER_H
#define FIRMWAREPARSER_H

#include <QString>
#include <QPair>

/// 固件文件解析器：获取固件的地址范围
class FirmwareParser
{
public:
    /// 解析固件文件，返回地址范围
    /// @param filePath 固件文件路径
    /// @param format 格式: "hex", "bin", "elf", "auto"
    /// @param binBaseAddr bin 文件的起始地址(仅 bin 需要)
    /// @return (起始地址, 大小)，失败返回 (0, 0)
    static QPair<quint64, quint64> parseAddressRange(const QString &filePath,
                                                       const QString &format,
                                                       quint64 binBaseAddr = 0);

private:
    /// 解析 Intel HEX 文件
    static QPair<quint64, quint64> parseHex(const QString &filePath);
    /// 解析 BIN 文件(需要外部提供基地址)
    static QPair<quint64, quint64> parseBin(const QString &filePath, quint64 baseAddr);
    /// 解析 ELF 文件(简化版，只读 program header)
    static QPair<quint64, quint64> parseElf(const QString &filePath);
};

#endif // FIRMWAREPARSER_H
