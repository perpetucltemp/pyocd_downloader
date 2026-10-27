#ifndef PYOCDRUNNER_H
#define PYOCDRUNNER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>

/**
 * @brief 直接调用 pyocd 命令行(本文件夹自带的 pyocd.exe)。
 *
 * 每个操作(擦除/下载/复位)就是一条独立的 pyocd 命令，跑完就退出，
 * 不需要长驻进程，也不需要 mamba/conda 环境。
 */
class PyOcdRunner : public QObject
{
    Q_OBJECT
public:
    explicit PyOcdRunner(QObject *parent = nullptr);
    ~PyOcdRunner() override;

    bool isRunning() const { return m_proc.state() != QProcess::NotRunning; }

    /// pyocd 命令前缀：{"<path>\pyocd.exe"}
    void setCommandPrefix(const QStringList &prefix) { m_prefix = prefix; }
    QStringList commandPrefix() const { return m_prefix; }

    /// 指定探针 unique_id(留空 = 自动：每次操作前先枚举探针)
    void setProbeId(const QString &id) { m_probeId = id.trimmed(); m_probeChecked = false; }
    QString probeId() const { return m_probeId; }

    /// 先枚举探针(处理多探针选择)，再执行 argv
    /// @return true=已启动; false=被忽略(上一条还在跑 / 未设置命令)
    bool runCommand(const QStringList &argv, const QString &what,
                    bool needProbeCheck = true);

    /// 直接执行(不带探针检查)
    /// @return true=已启动; false=被忽略
    bool runDirect(const QStringList &argv, const QString &what);

    void stop();

    /// 解析 `pyocd list --probes` 的表格输出 -> [{"id","desc"}]
    static QVariantList parseProbeTable(const QString &text);

signals:
    void logLine(const QString &level, const QString &msg);
    void progress(int percent);
    void finished(bool ok, int exitCode, const QString &what);
    /// 命令结束时把完整 stdout 回传(用于解析 list --targets / --probes 的输出)
    void outputReady(const QString &what, const QString &text);
    /// 检测到多个探针，需要上位机选择；items = [{"id","desc"}]
    void probeChoiceNeeded(const QVariantList &probes);

private:
    void startProcess(const QStringList &fullArgs, const QString &what);
    void pump(QProcess::ProcessChannel channel, const QString &level);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    static int parsePercent(const QString &line);

    QProcess m_proc;
    QStringList m_prefix;
    QStringList m_pendingArgv;     ///< 探针检查通过后要执行的命令
    QString m_what;
    QString m_probeId;             ///< 用户选定的探针
    QString m_stdout;              ///< 本次命令的完整 stdout
    bool m_stopping = false;
    bool m_checkingProbes = false;
    bool m_probeChecked = false;
};

#endif // PYOCDRUNNER_H
