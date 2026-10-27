#ifndef PYOCDLOCATOR_H
#define PYOCDLOCATOR_H

#include <QString>
#include <QStringList>

/**
 * @brief 定位并校验自带( bundled )的 pyocd.exe。
 *
 * 从 exe 所在目录逐级向上查找:
 *   <某个目录>\pyocd-windows-<版本>\pyocd.exe
 *   <某个目录>\own_pyocd\pyocd-windows-<版本>\pyocd.exe
 * 找到后用 `pyocd.exe --version` 校验真的能跑。
 * 不依赖 mamba / conda / Python 环境。
 */
class PyocdLocator
{
public:
    /**
     * @brief 找自带 pyocd.exe(多个版本时取最新的可用的那个)
     * @param versionOut  成功时输出版本号(如 "0.46.0")
     * @return pyocd.exe 绝对路径；找不到或都不可用时返回空串
     */
    static QString findBundledPyocd(QString &versionOut);

    /// 所有候选的 pyocd.exe 路径(不校验，版本新的在前)
    static QStringList bundledCandidates();

private:
    /// 跑 `pyocd.exe --version` 取版本号，失败返回空
    static QString probeVersion(const QString &exe);
};

#endif // PYOCDLOCATOR_H
