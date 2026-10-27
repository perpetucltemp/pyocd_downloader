#include "PyocdLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>

namespace {

/// 从任意文本里提取 "主.次.修订" 版本号，没有返回空
QString firstVersionIn(const QString &text)
{
    static const QRegularExpression re(R"((\d+)\.(\d+)\.(\d+))");
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    if (!it.hasNext()) {
        return {};
    }
    const QRegularExpressionMatch m = it.next();
    return QStringLiteral("%1.%2.%3")
            .arg(m.captured(1).toInt()).arg(m.captured(2).toInt()).arg(m.captured(3).toInt());
}

/// 版本号 -> 可比较的整数(主.次.修订各占 8 位)
qint64 versionKey(const QString &version)
{
    const QStringList parts = version.split('.');
    qint64 key = 0;
    for (int i = 0; i < 3 && i < parts.size(); ++i) {
        key = (key << 8) + qBound(0, parts.at(i).toInt(), 255);
    }
    return key;
}

/// 同步执行外部进程，返回 {退出码, 合并后的输出}
QPair<int, QString> runSync(const QString &program, const QStringList &args,
                            int timeoutMs = 20000)
{
    if (program.isEmpty()) {
        return {-1, QStringLiteral("程序路径为空")};
    }
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QDir::toNativeSeparators(program), args);
    if (!proc.waitForStarted(8000)) {
        return {-1, QStringLiteral("无法启动: %1").arg(program)};
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        return {-2, QStringLiteral("执行超时: %1").arg(program)};
    }
    return {proc.exitCode(), QString::fromUtf8(proc.readAll())};
}

} // namespace

QStringList PyocdLocator::bundledCandidates()
{
    // 候选根目录：exe 同级/上一级，再逐级向上。
    // 每个根目录下找 pyocd-windows-<版本>\pyocd.exe 或 pyocd.exe。
    QStringList bases;
    const QString appDir = QCoreApplication::applicationDirPath();

    bases << appDir;
    bases << QDir(appDir).absoluteFilePath("..");
    bases << QDir(appDir).absoluteFilePath("../own_pyocd");

    QDir dir(appDir);
    for (int i = 0; i < 6; ++i) {
        if (!dir.cdUp()) {
            break;
        }
        bases << dir.absolutePath();
        bases << dir.absoluteFilePath("own_pyocd");
    }

    QStringList exes;
    for (const QString &base : bases) {
        QDir baseDir(base);
        if (!baseDir.exists()) {
            continue;
        }
        // 根目录直接放 pyocd.exe 的情况
        const QString direct = baseDir.absoluteFilePath("pyocd.exe");
        if (QFileInfo::exists(direct)) {
            exes << QDir::cleanPath(direct);
        }
        // 常规布局: pyocd-windows-0.46.0\pyocd.exe
        const QStringList subs = baseDir.entryList(QStringList{"pyocd-windows-*"},
                                                   QDir::Dirs | QDir::NoDotAndDotDot,
                                                   QDir::Name);
        for (const QString &sub : subs) {
            const QString exe = baseDir.absoluteFilePath(sub + "/pyocd.exe");
            if (QFileInfo::exists(exe)) {
                exes << QDir::cleanPath(exe);
            }
        }
    }
    exes.removeDuplicates();

    // 版本新的排在前面(从路径里的 pyocd-windows-X.Y.Z 提取版本号)
    std::stable_sort(exes.begin(), exes.end(), [](const QString &a, const QString &b) {
        return versionKey(firstVersionIn(a)) > versionKey(firstVersionIn(b));
    });
    return exes;
}

QString PyocdLocator::probeVersion(const QString &exe)
{
    const QPair<int, QString> rc = runSync(exe, {"--version"});
    if (rc.first != 0) {
        return {};
    }
    return firstVersionIn(rc.second.trimmed());
}

QString PyocdLocator::findBundledPyocd(QString &versionOut)
{
    versionOut.clear();
    const QStringList exes = bundledCandidates();
    if (exes.isEmpty()) {
        return {};
    }
    // 从最新版本开始逐个校验，第一个能跑通 `--version` 的才算可用
    for (const QString &exe : exes) {
        const QString version = probeVersion(exe);
        if (!version.isEmpty()) {
            versionOut = version;
            return exe;
        }
    }
    return {};
}
