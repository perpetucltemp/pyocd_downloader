#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QQueue>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QVector>

#include "PackParser.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

class PyOcdRunner;

/**
 * @brief pyocd_ui 主窗口：调用 tools\own_pyocd 下自带的 pyocd.exe，
 *        完成 连接 MCU / 擦除芯片 / 下载固件 / 复位运行。
 *        不依赖 mamba / conda 环境。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    // pyocd(自带)
    void onRedetectPyocd();
    // 设备 / 固件
    void onRefreshTargets();
    void onRefreshProbes();
    void onBrowseFirmware();
    void onFormatChanged(int index);
    // 操作
    void onConnect();
    void onDisconnect();
    void onEraseChip();
    void onDownload();
    void onResetRun();
    void onStopOperation();
    // 扇区选择(擦除用 / 下载用)
    void onSelectSectors();          // 擦除区域的扇区选择
    void onSelectDownloadSectors();  // 下载前擦除的扇区选择
    // 日志
    void onClearLog();
    void onSaveLog();

    // runner 回调
    void onLogLine(const QString &level, const QString &msg);
    void onProgress(int percent);
    void onCommandFinished(bool ok, int exitCode, const QString &what);
    void onOutputReady(const QString &what, const QString &text);
    void onProbeChoiceNeeded(const QVariantList &probes);

private:
    void buildUi();
    void loadSettings();
    void saveSettings();

    void initBundledPyocd();
    void applyPyocd(const QString &exe);
    /// 自带 pack 目录(与 pyocd.exe 同级): <exe_dir>\..\packs\<file>.pack
    /// 返回所有可用的 .pack 文件(绝对路径)
    QStringList bundledPacks() const;
    QString selectedTarget() const;
    void runPyocd(const QStringList &argv, const QString &what,
                  bool needProbeCheck = true);
    /// 加载当前 target 的 Flash 布局(带缓存)，失败返回 false
    bool loadFlashLayout();
    /// 打开扇区选择对话框，返回选中的 (地址,大小) 列表；取消返回空列表
    QVector<QPair<quint64, quint64>> pickSectors(QWidget *parent);
    void beginCommand(const QString &what);
    void setBusy(bool busy, const QString &what);
    void updateUiState();

    PyOcdRunner *m_runner = nullptr;

    // pyocd(自带)
    QLabel *m_pyocdLabel = nullptr;
    QPushButton *m_redetectBtn = nullptr;

    // 设备 / 固件
    QComboBox *m_targetCombo = nullptr;
    QPushButton *m_refreshTargetsBtn = nullptr;
    QComboBox *m_probeCombo = nullptr;
    QPushButton *m_refreshProbesBtn = nullptr;
    QSpinBox *m_freqSpin = nullptr;
    QLineEdit *m_fileEdit = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QLineEdit *m_addrEdit = nullptr;
    QComboBox *m_eraseCombo = nullptr;
    QLineEdit *m_eraseAddrEdit = nullptr;
    QLineEdit *m_eraseSizeEdit = nullptr;
    QPushButton *m_sectorSelectBtn = nullptr;
    QComboBox *m_dlEraseCombo = nullptr;     ///< 下载时的擦除方式
    QPushButton *m_dlSectorBtn = nullptr;    ///< 下载前选择要擦除的扇区
    QCheckBox *m_resetAfterCheck = nullptr;

    // 操作
    QPushButton *m_connectBtn = nullptr;
    QPushButton *m_disconnectBtn = nullptr;
    QPushButton *m_eraseBtn = nullptr;
    QPushButton *m_downloadBtn = nullptr;
    QPushButton *m_resetBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_opStatusLabel = nullptr;
    QLabel *m_connLabel = nullptr;

    // 日志
    QPlainTextEdit *m_logEdit = nullptr;

    QStringList m_pendingArgv;      ///< 等多探针选择时要执行的命令
    QString m_pendingWhat;
    bool m_autoRefreshPending = false;   ///< runner 忙时挂起的 target 列表刷新
    bool m_connected = false;       ///< 是否已连接(点了「连接」且成功)
    int m_pendingCommands = 0;
    bool m_busy = false;

    // Flash 布局缓存
    QVector<FlashRegion> m_flashRegions;
    bool m_flashLayoutLoaded = false;

    // 扇区选择结果
    QVector<QPair<quint64, quint64>> m_eraseRanges;    ///< 「擦除」按钮用的选中扇区
    QVector<QPair<quint64, quint64>> m_dlEraseRanges;  ///< 「下载」前擦除用的选中扇区

    // 命令队列(下载前擦除: 先跑 N 条 erase，再跑 1 条 load)
    struct QueuedCmd { QStringList argv; QString what; };
    QQueue<QueuedCmd> m_cmdQueue;
    int m_queueTotal = 0;       ///< 本批次命令总数(用于状态显示)
    bool m_queueHadError = false;
};

#endif // MAINWINDOW_H
