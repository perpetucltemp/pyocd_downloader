#include "FirmwareParser.h"

#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#include <QRegularExpression>

QPair<quint64, quint64> FirmwareParser::parseAddressRange(const QString &filePath,
                                                            const QString &format,
                                                            quint64 binBaseAddr)
{
    QString fmt = format.toLower();
    if (fmt == "auto") {
        const QString suffix = QFileInfo(filePath).suffix().toLower();
        if (suffix == "hex" || suffix == "ihx") fmt = "hex";
        else if (suffix == "bin") fmt = "bin";
        else if (suffix == "elf" || suffix == "axf" || suffix == "out") fmt = "elf";
        else return {0, 0};
    }

    if (fmt == "hex") {
        return parseHex(filePath);
    } else if (fmt == "bin") {
        return parseBin(filePath, binBaseAddr);
    } else if (fmt == "elf") {
        return parseElf(filePath);
    }
    return {0, 0};
}

QPair<quint64, quint64> FirmwareParser::parseHex(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {0, 0};
    }

    quint64 minAddr = ~0ULL;
    quint64 maxAddr = 0;
    quint64 baseAddr = 0;  // 扩展线性地址

    while (!file.atEnd()) {
        const QString line = QString::fromLatin1(file.readLine()).trimmed();
        if (line.isEmpty() || !line.startsWith(':')) {
            continue;
        }

        // :LLAAAATT[DD...]CC
        bool ok = false;
        const int len = line.mid(1, 2).toInt(&ok, 16);
        if (!ok) continue;
        const quint32 addr = line.mid(3, 4).toUInt(&ok, 16);
        if (!ok) continue;
        const int type = line.mid(7, 2).toInt(&ok, 16);
        if (!ok) continue;

        if (type == 0x00) {  // 数据记录
            const quint64 absAddr = baseAddr + addr;
            if (absAddr < minAddr) minAddr = absAddr;
            if (absAddr + len > maxAddr) maxAddr = absAddr + len;
        } else if (type == 0x04) {  // 扩展线性地址
            baseAddr = line.mid(9, 4).toUInt(&ok, 16) << 16;
            if (!ok) continue;
        } else if (type == 0x01) {  // EOF
            break;
        }
    }

    if (minAddr > maxAddr) {
        return {0, 0};
    }
    return {minAddr, maxAddr - minAddr};
}

QPair<quint64, quint64> FirmwareParser::parseBin(const QString &filePath, quint64 baseAddr)
{
    const QFileInfo info(filePath);
    if (!info.exists()) {
        return {0, 0};
    }
    return {baseAddr, static_cast<quint64>(info.size())};
}

QPair<quint64, quint64> FirmwareParser::parseElf(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {0, 0};
    }

    // 读 ELF header
    unsigned char ident[16];
    if (file.read(reinterpret_cast<char*>(ident), 16) != 16) {
        return {0, 0};
    }
    if (ident[0] != 0x7F || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') {
        return {0, 0};
    }

    const bool is64 = (ident[4] == 2);
    const bool isBE = (ident[5] == 2);

    // 跳到 program header  offset
    quint64 phoff = 0;
    quint16 phentsize = 0;
    quint16 phnum = 0;

    if (is64) {
        file.seek(32);
        quint64 v;
        file.read(reinterpret_cast<char*>(&v), 8);
        phoff = isBE ? qFromBigEndian(v) : qFromLittleEndian(v);
        file.seek(54);
        quint16 s;
        file.read(reinterpret_cast<char*>(&s), 2);
        phentsize = isBE ? qFromBigEndian(s) : qFromLittleEndian(s);
        file.read(reinterpret_cast<char*>(&s), 2);
        phnum = isBE ? qFromBigEndian(s) : qFromLittleEndian(s);
    } else {
        file.seek(28);
        quint32 v;
        file.read(reinterpret_cast<char*>(&v), 4);
        phoff = isBE ? qFromBigEndian(v) : qFromLittleEndian(v);
        file.seek(42);
        quint16 s;
        file.read(reinterpret_cast<char*>(&s), 2);
        phentsize = isBE ? qFromBigEndian(s) : qFromLittleEndian(s);
        file.read(reinterpret_cast<char*>(&s), 2);
        phnum = isBE ? qFromBigEndian(s) : qFromLittleEndian(s);
    }

    quint64 minAddr = ~0ULL;
    quint64 maxAddr = 0;

    for (int i = 0; i < phnum; ++i) {
        file.seek(phoff + i * phentsize);

        quint32 type = 0;
        quint64 vaddr = 0;
        quint64 memsz = 0;

        if (is64) {
            quint32 t;
            file.read(reinterpret_cast<char*>(&t), 4);
            type = isBE ? qFromBigEndian(t) : qFromLittleEndian(t);
            file.read(reinterpret_cast<char*>(&vaddr), 8);  // 跳过 flags+offset
            file.read(reinterpret_cast<char*>(&vaddr), 8);  // vaddr
            quint64 paddr;
            file.read(reinterpret_cast<char*>(&paddr), 8);
            quint64 filesz;
            file.read(reinterpret_cast<char*>(&filesz), 8);
            file.read(reinterpret_cast<char*>(&memsz), 8);
            vaddr = isBE ? qFromBigEndian(vaddr) : qFromLittleEndian(vaddr);
            memsz = isBE ? qFromBigEndian(memsz) : qFromLittleEndian(memsz);
        } else {
            quint32 t;
            file.read(reinterpret_cast<char*>(&t), 4);
            type = isBE ? qFromBigEndian(t) : qFromLittleEndian(t);
            quint32 v32;
            file.read(reinterpret_cast<char*>(&v32), 4);  // offset
            file.read(reinterpret_cast<char*>(&v32), 4);  // vaddr
            vaddr = isBE ? qFromBigEndian(v32) : qFromLittleEndian(v32);
            quint32 p32;
            file.read(reinterpret_cast<char*>(&p32), 4);
            quint32 f32;
            file.read(reinterpret_cast<char*>(&f32), 4);  // filesz
            file.read(reinterpret_cast<char*>(&f32), 4);  // memsz
            memsz = isBE ? qFromBigEndian(f32) : qFromLittleEndian(f32);
        }

        // PT_LOAD = 1
        if (type == 1 && memsz > 0) {
            if (vaddr < minAddr) minAddr = vaddr;
            if (vaddr + memsz > maxAddr) maxAddr = vaddr + memsz;
        }
    }

    if (minAddr > maxAddr) {
        return {0, 0};
    }
    return {minAddr, maxAddr - minAddr};
}
