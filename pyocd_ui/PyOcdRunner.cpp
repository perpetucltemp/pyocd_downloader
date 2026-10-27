#include "PyOcdRunner.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

PyOcdRunner::PyOcdRunner(QObject *parent)
    : QObject(parent)
{
    m_proc.setProcessChannelMode(QProcess::SeparateChannels);
    m_proc.setProcessEnvironment(QProcessEnvironment::systemEnvironment());

    connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        pump(QProcess::StandardOutput, QStringLiteral("debug"));
    });
    connect(&m_proc, &QProcess::readyReadStandardError, this, [this]() {
        pump(QProcess::StandardError, QStringLiteral("stderr"));
    });
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit logLine("error", QStringLiteral("无法启动 pyocd: %1")
                                    .arg(m_prefix.join(' ')));
        }
    });
    connect(&m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PyOcdRunner::onProcessFinished);
}

PyOcdRunner::~PyOcdRunner()
{
    stop();
}

void PyOcdRunner::stop()
{
    if (!isRunning()) {
        return;
    }
    m_stopping = true;
    m_proc.terminate();
    if (!m_proc.waitForFinished(1500)) {
        m_proc.kill();
        m_proc.waitForFinished(1000);
    }
}

bool PyOcdRunner::runCommand(const QStringList &argv, const QString &what,
                             bool needProbeCheck)
{
    if (isRunning()) {
        emit logLine("warn", QStringLiteral("上一条命令还在跑，忽略: %1").arg(what));
        return false;
    }
    if (m_prefix.isEmpty()) {
        emit logLine("error", QStringLiteral("尚未设置 pyocd 命令(未找到自带 pyocd.exe)"));
        return false;
    }

    // list 之类的命令不需要探针，跳过枚举
    if (!needProbeCheck || m_probeChecked || !m_probeId.isEmpty()) {
        return runDirect(argv, what);
    }

    // 先枚举探针：0 个直接报错；1 个让 pyocd 自己选择；多个才需要用户选
    m_checkingProbes = true;
    m_pendingArgv = argv;
    m_what = what;
    emit logLine("debug", QStringLiteral("枚举调试探针..."));
    startProcess(QStringList{"list", "--probes", "--no-header", "--color", "never"},
                 tr("枚举探针"));
    return true;
}

bool PyOcdRunner::runDirect(const QStringList &argv, const QString &what)
{
    if (isRunning()) {
        emit logLine("warn", QStringLiteral("上一条命令还在跑，忽略: %1").arg(what));
        return false;
    }
    QStringList args = argv;
    // 自动带上选定的探针。注意：list 子命令没有 -u 选项，不能加
    // (pyocd 会报 "unrecognized arguments: -u xxx")。
    const bool supportsProbe = !args.isEmpty() && args.first() != "list";
    if (supportsProbe && !m_probeId.isEmpty() && !args.contains("-u")
        && !args.contains("--uid") && !args.contains("--probe")
        && !args.contains("--unique-id")) {
        args << "-u" << m_probeId;
    }
    m_checkingProbes = false;
    m_pendingArgv.clear();
    startProcess(args, what);
    return true;
}

void PyOcdRunner::startProcess(const QStringList &fullArgs, const QString &what)
{
    m_what = what;
    m_stopping = false;
    m_stdout.clear();
    emit logLine("info", QStringLiteral("执行: %1 %2")
                            .arg(m_prefix.join(' '), fullArgs.join(' ')));
    m_proc.setWorkingDirectory(QCoreApplication::applicationDirPath());
    m_proc.start(m_prefix.constFirst(), m_prefix.mid(1) + fullArgs);
}

void PyOcdRunner::pump(QProcess::ProcessChannel channel, const QString &level)
{
    QProcess::ProcessChannel ch = channel;
    const QByteArray data = (ch == QProcess::StandardOutput)
            ? m_proc.readAllStandardOutput()
            : m_proc.readAllStandardError();
    const QString text = QString::fromUtf8(data);
    if (ch == QProcess::StandardOutput) {
        m_stdout += text;
    }
    for (const QString &line : text.split(QRegularExpression("[\\r\\n]"),
                                          Qt::SkipEmptyParts)) {
        const int percent = parsePercent(line);
        if (percent >= 0) {
            emit progress(percent);
        }
        emit logLine(level, line);
    }
}

int PyOcdRunner::parsePercent(const QString &line)
{
    // 匹配 "50%" / " 50 %" / "50.0%" 之类的进度
    static const QRegularExpression re(R"((\d{1,3})(?:\.\d+)?\s*%)");
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch()) {
        return -1;
    }
    bool ok = false;
    const int value = m.captured(1).toInt(&ok);
    if (!ok || value < 0 || value > 100) {
        return -1;
    }
    return value;
}

QVariantList PyOcdRunner::parseProbeTable(const QString &text)
{
    QVariantList out;
    static const QRegularExpression kHexId(R"(^[0-9A-Fa-f]{8,}$)");
    for (const QString &raw : text.split(QRegularExpression("[\\r\\n]"),
                                         Qt::SkipEmptyParts)) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith("No available")) {
            continue;
        }
        const QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (tokens.size() < 2) {
            continue;
        }
        // 形如:  0   STM32 STLink   000000000000000000000000   n/a
        int idIndex = -1;
        for (int i = 1; i < tokens.size(); ++i) {
            if (kHexId.match(tokens.at(i)).hasMatch()) {
                idIndex = i;
                break;
            }
        }
        if (idIndex < 0) {
            continue;
        }
        const QString id = tokens.at(idIndex);
        const QString desc = tokens.mid(1, idIndex - 1).join(' ');
        QVariantMap map;
        map["id"] = id;
        map["desc"] = desc.isEmpty() ? id : desc;
        out.append(map);
    }
    return out;
}

void PyOcdRunner::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    const bool intentional = m_stopping;
    m_stopping = false;

    // 把完整 stdout 回传(探针枚举阶段内部自己解析，这里也发一份给界面)
    if (!intentional) {
        m_stdout += QString::fromUtf8(m_proc.readAllStandardOutput());
        emit outputReady(m_what, m_stdout);
    }

    if (intentional) {
        emit logLine("info", tr("已停止"));
        m_checkingProbes = false;
        m_pendingArgv.clear();
        emit finished(false, -1, tr("已停止"));
        return;
    }

    // ---- 探针枚举阶段结束 ----
    if (m_checkingProbes) {
        m_checkingProbes = false;
        const QStringList argv = m_pendingArgv;
        const QString what = m_what;
        m_pendingArgv.clear();

        // 注意：stdout 已经在上面读进 m_stdout 了，这里不能再读一次(会是空)
        const QVariantList probes = parseProbeTable(m_stdout);
        if (probes.isEmpty()) {
            emit logLine("error", tr("没有检测到调试探针，请检查 USB / ST-Link / J-Link 连接"));
            emit finished(false, exitCode, what);
            return;
        }
        if (probes.size() == 1) {
            emit logLine("info", tr("探针: %1").arg(probes.first().toMap().value("desc").toString()));
            if (!runDirect(argv, what)) {
                emit finished(false, -1, what);
            }
            return;
        }
        emit logLine("warn", tr("检测到 %1 个探针，请选择").arg(probes.size()));
        m_pendingArgv = argv;
        m_what = what;
        emit probeChoiceNeeded(probes);
        return;
    }

    const bool ok = (exitCode == 0 && status == QProcess::NormalExit);
    if (ok) {
        emit progress(100);
    }
    emit finished(ok, exitCode, m_what);
}
