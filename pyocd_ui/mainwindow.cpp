#include "mainwindow.h"

#include "PyOcdRunner.h"
#include "PyocdLocator.h"
#include "PackParser.h"
#include "FirmwareParser.h"
#include "SectorSelectorDialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr char kSettingsOrg[] = "pyocd_ui";
constexpr char kSettingsApp[] = "pyocd_ui";

/// 从 `pyocd list --targets --no-header` 的输出里取第一列(target 名)
QStringList parseTargetList(const QString &text)
{
    QStringList names;
    for (const QString &raw : text.split(QRegularExpression("[\\r\\n]"),
                                         Qt::SkipEmptyParts)) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        const QString first = line.split(QRegularExpression("\\s+"),
                                         Qt::SkipEmptyParts).value(0);
        // target 名都是字母数字下划线，长度 >= 3
        static const QRegularExpression kName(R"(^[A-Za-z][A-Za-z0-9_\-]{2,}$)");
        if (kName.match(first).hasMatch()) {
            names << first;
        }
    }
    names.removeDuplicates();
    return names;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("pyocd_ui — 自带 pyocd 固件下载 (STM32 / GD32)"));
    resize(900, 680);

    m_runner = new PyOcdRunner(this);

    connect(m_runner, &PyOcdRunner::logLine, this, &MainWindow::onLogLine);
    connect(m_runner, &PyOcdRunner::progress, this, &MainWindow::onProgress);
    connect(m_runner, &PyOcdRunner::finished, this, &MainWindow::onCommandFinished);
    connect(m_runner, &PyOcdRunner::outputReady, this, &MainWindow::onOutputReady);
    connect(m_runner, &PyOcdRunner::probeChoiceNeeded, this, &MainWindow::onProbeChoiceNeeded);

    buildUi();
    loadSettings();
    setAcceptDrops(true);
    updateUiState();

    initBundledPyocd();
}

MainWindow::~MainWindow()
{
    if (m_runner->isRunning()) {
        m_runner->stop();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        return;
    }
    const QString path = urls.first().toLocalFile();
    const QString lower = path.toLower();
    if (lower.endsWith(".bin") || lower.endsWith(".hex") || lower.endsWith(".elf")
        || lower.endsWith(".axf") || lower.endsWith(".out")) {
        m_fileEdit->setText(QDir::toNativeSeparators(path));
        if (lower.endsWith(".hex")) {
            m_formatCombo->setCurrentIndex(m_formatCombo->findData("hex"));
        } else {
            m_formatCombo->setCurrentIndex(m_formatCombo->findData("bin"));
        }
        onLogLine("info", tr("已载入固件: %1").arg(path));
    }
    event->acceptProposedAction();
}

// --------------------------------------------------------------------------- //
// UI
// --------------------------------------------------------------------------- //
void MainWindow::buildUi()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    // ---- pyocd(自带) ----
    auto *pyocdGroup = new QGroupBox(tr("pyocd(自带, 不依赖 mamba/conda)"), page);
    auto *pyocdRow = new QHBoxLayout(pyocdGroup);
    m_pyocdLabel = new QLabel(tr("正在检测..."), pyocdGroup);
    m_redetectBtn = new QPushButton(tr("重新检测"), pyocdGroup);
    pyocdRow->addWidget(new QLabel(tr("pyocd:"), pyocdGroup));
    pyocdRow->addWidget(m_pyocdLabel, 1);
    pyocdRow->addWidget(m_redetectBtn);
    connect(m_redetectBtn, &QPushButton::clicked, this, &MainWindow::onRedetectPyocd);
    layout->addWidget(pyocdGroup);

    // ---- MCU / 固件 ----
    auto *fwGroup = new QGroupBox(tr("MCU 与固件"), page);
    auto *fwForm = new QFormLayout(fwGroup);

    auto *mcuRow = new QHBoxLayout();
    m_targetCombo = new QComboBox(fwGroup);
    m_targetCombo->setEditable(true);
    m_targetCombo->setToolTip(tr(
        "pyocd 的 target 名，例如 STM32F407VGTx / gd32f307vgt6。<br>"
        "名字可能和丝印不一致(常带 x 通配符)，请点「刷新」取真实列表；<br>"
        "手输 stm32f407vgt6 也会被 pyocd 自动匹配。"));
    m_refreshTargetsBtn = new QPushButton(tr("刷新"), fwGroup);
    m_freqSpin = new QSpinBox(fwGroup);
    m_freqSpin->setRange(100, 24000);
    m_freqSpin->setValue(4000);
    m_freqSpin->setSuffix(tr(" kHz"));
    mcuRow->addWidget(m_targetCombo, 1);
    mcuRow->addWidget(m_refreshTargetsBtn);
    mcuRow->addWidget(new QLabel(tr("SWD:"), fwGroup));
    mcuRow->addWidget(m_freqSpin);
    fwForm->addRow(tr("MCU 型号:"), mcuRow);
    connect(m_refreshTargetsBtn, &QPushButton::clicked, this, &MainWindow::onRefreshTargets);

    auto *probeRow = new QHBoxLayout();
    m_probeCombo = new QComboBox(fwGroup);
    m_probeCombo->addItem(tr("(自动)"), QString());
    m_probeCombo->setToolTip(tr("留空=自动；只有一个探针时 pyocd 自己选，"
                                "多个探针时会弹窗让你选"));
    m_refreshProbesBtn = new QPushButton(tr("刷新"), fwGroup);
    probeRow->addWidget(m_probeCombo, 1);
    probeRow->addWidget(m_refreshProbesBtn);
    fwForm->addRow(tr("调试探针:"), probeRow);
    connect(m_refreshProbesBtn, &QPushButton::clicked, this, &MainWindow::onRefreshProbes);

    auto *fileRow = new QHBoxLayout();
    m_fileEdit = new QLineEdit(fwGroup);
    m_fileEdit->setPlaceholderText(tr("把 .bin / .hex / .elf 拖到窗口上也可以"));
    auto *browseFile = new QPushButton(tr("浏览..."), fwGroup);
    fileRow->addWidget(m_fileEdit, 1);
    fileRow->addWidget(browseFile);
    fwForm->addRow(tr("固件路径:"), fileRow);
    connect(browseFile, &QPushButton::clicked, this, &MainWindow::onBrowseFirmware);

    auto *fmtRow = new QHBoxLayout();
    m_formatCombo = new QComboBox(fwGroup);
    m_formatCombo->addItem(tr("自动(按扩展名)"), "auto");
    m_formatCombo->addItem(tr("Intel HEX (.hex)"), "hex");
    m_formatCombo->addItem(tr("BIN (.bin)"), "bin");
    m_formatCombo->addItem(tr("ELF (.elf)"), "elf");
    m_addrEdit = new QLineEdit(fwGroup);
    m_addrEdit->setMaximumWidth(200);
    fmtRow->addWidget(m_formatCombo);
    fmtRow->addWidget(new QLabel(tr("下载地址:"), fwGroup));
    fmtRow->addWidget(m_addrEdit);
    fmtRow->addStretch();
    fwForm->addRow(tr("固件格式:"), fmtRow);
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onFormatChanged);

    auto *eraseRow = new QHBoxLayout();
    m_eraseCombo = new QComboBox(fwGroup);
    m_eraseCombo->addItem(tr("整片擦除"), "chip");
    m_eraseCombo->addItem(tr("按扇区擦除"), "sector");
    m_eraseCombo->setToolTip(tr("整片擦除=全擦; 按扇区擦除=只擦指定区域"));
    m_eraseAddrEdit = new QLineEdit(fwGroup);
    m_eraseAddrEdit->setPlaceholderText(tr("0x08000000"));
    m_eraseAddrEdit->setMaximumWidth(140);
    m_eraseAddrEdit->setEnabled(false);
    m_eraseSizeEdit = new QLineEdit(fwGroup);
    m_eraseSizeEdit->setPlaceholderText(tr("0x4000"));
    m_eraseSizeEdit->setMaximumWidth(100);
    m_eraseSizeEdit->setEnabled(false);
    m_sectorSelectBtn = new QPushButton(tr("选择扇区..."), fwGroup);
    m_sectorSelectBtn->setEnabled(false);
    m_sectorSelectBtn->setToolTip(tr("根据 MCU Flash 布局图形化选择要擦除的扇区"));
    m_resetAfterCheck = new QCheckBox(tr("下载后复位运行"), fwGroup);
    m_resetAfterCheck->setChecked(true);
    eraseRow->addWidget(m_eraseCombo);
    eraseRow->addWidget(new QLabel(tr("地址:"), fwGroup));
    eraseRow->addWidget(m_eraseAddrEdit);
    eraseRow->addWidget(new QLabel(tr("大小:"), fwGroup));
    eraseRow->addWidget(m_eraseSizeEdit);
    eraseRow->addWidget(m_sectorSelectBtn);
    eraseRow->addWidget(m_resetAfterCheck);
    eraseRow->addStretch();
    fwForm->addRow(tr("擦除设置:"), eraseRow);
    layout->addWidget(fwGroup);
    connect(m_eraseCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                const bool isSector = (m_eraseCombo->itemData(index).toString() == "sector");
                m_eraseAddrEdit->setEnabled(isSector);
                m_eraseSizeEdit->setEnabled(isSector);
                m_sectorSelectBtn->setEnabled(isSector);
            });
    connect(m_sectorSelectBtn, &QPushButton::clicked,
            this, &MainWindow::onSelectSectors);

    // ---- 下载时的擦除设置 ----
    auto *dlEraseRow = new QHBoxLayout();
    m_dlEraseCombo = new QComboBox(fwGroup);
    m_dlEraseCombo->addItem(tr("不擦除(直接写入)"), "none");
    m_dlEraseCombo->addItem(tr("按固件地址自动擦除"), "firmware");
    m_dlEraseCombo->addItem(tr("擦除选中扇区"), "sectors");
    m_dlEraseCombo->addItem(tr("整片擦除后下载"), "chip");
    m_dlEraseCombo->setToolTip(tr(
        "不擦除: 直接下载(目标区域必须已擦除)<br>"
        "按固件地址自动擦除: 解析 hex/elf 地址(或 bin+起始地址)，擦除覆盖到的扇区<br>"
        "擦除选中扇区: 用下方「选择扇区」选好的区域<br>"
        "整片擦除后下载: pyocd load -e chip"));
    m_dlSectorBtn = new QPushButton(tr("选择扇区..."), fwGroup);
    m_dlSectorBtn->setEnabled(false);
    m_dlSectorBtn->setToolTip(tr("选择下载前要擦除的扇区(仅「擦除选中扇区」模式有效)"));
    dlEraseRow->addWidget(m_dlEraseCombo);
    dlEraseRow->addWidget(m_dlSectorBtn);
    dlEraseRow->addStretch();
    fwForm->addRow(tr("下载擦除:"), dlEraseRow);
    connect(m_dlEraseCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                m_dlSectorBtn->setEnabled(
                    m_dlEraseCombo->itemData(index).toString() == "sectors");
            });
    connect(m_dlSectorBtn, &QPushButton::clicked,
            this, &MainWindow::onSelectDownloadSectors);

    // ---- 操作 ----
    auto *opGroup = new QGroupBox(tr("操作(直接调用 pyocd 命令行)"), page);
    auto *opGrid = new QGridLayout(opGroup);
    m_connectBtn = new QPushButton(tr("连接"), opGroup);
    m_connectBtn->setMinimumHeight(34);
    m_disconnectBtn = new QPushButton(tr("断开"), opGroup);
    m_eraseBtn = new QPushButton(tr("擦除芯片"), opGroup);
    m_eraseBtn->setMinimumHeight(34);
    m_downloadBtn = new QPushButton(tr("下载固件"), opGroup);
    m_resetBtn = new QPushButton(tr("复位运行"), opGroup);
    m_stopBtn = new QPushButton(tr("停止"), opGroup);
    m_connLabel = new QLabel(tr("未连接"), opGroup);
    m_progressBar = new QProgressBar(opGroup);
    m_progressBar->setRange(0, 100);
    m_opStatusLabel = new QLabel(tr("就绪"), opGroup);
    opGrid->addWidget(m_connectBtn, 0, 0);
    opGrid->addWidget(m_disconnectBtn, 0, 1);
    opGrid->addWidget(m_connLabel, 0, 2, 1, 2);
    opGrid->addWidget(m_eraseBtn, 1, 0);
    opGrid->addWidget(m_downloadBtn, 1, 1);
    opGrid->addWidget(m_resetBtn, 1, 2);
    opGrid->addWidget(m_stopBtn, 1, 3);
    opGrid->addWidget(m_progressBar, 2, 0, 1, 4);
    opGrid->addWidget(m_opStatusLabel, 3, 0, 1, 4);
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    connect(m_eraseBtn, &QPushButton::clicked, this, &MainWindow::onEraseChip);
    connect(m_downloadBtn, &QPushButton::clicked, this, &MainWindow::onDownload);
    connect(m_resetBtn, &QPushButton::clicked, this, &MainWindow::onResetRun);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStopOperation);
    layout->addWidget(opGroup);

    // ---- 日志 ----
    auto *logGroup = new QGroupBox(tr("日志"), page);
    auto *logLayout = new QVBoxLayout(logGroup);
    m_logEdit = new QPlainTextEdit(logGroup);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumBlockCount(5000);
    m_logEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    logLayout->addWidget(m_logEdit, 1);
    auto *logBtnRow = new QHBoxLayout();
    auto *clearBtn = new QPushButton(tr("清空"), logGroup);
    auto *saveBtn = new QPushButton(tr("保存日志..."), logGroup);
    logBtnRow->addStretch();
    logBtnRow->addWidget(clearBtn);
    logBtnRow->addWidget(saveBtn);
    logLayout->addLayout(logBtnRow);
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearLog);
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onSaveLog);
    layout->addWidget(logGroup, 1);

    setCentralWidget(page);
    statusBar()->showMessage(tr("就绪"));

    onFormatChanged(m_formatCombo->currentIndex());
}

// --------------------------------------------------------------------------- //
// 设置
// --------------------------------------------------------------------------- //
void MainWindow::loadSettings()
{
    QSettings s(kSettingsOrg, kSettingsApp);
    m_targetCombo->setCurrentText(s.value("target").toString());
    m_probeCombo->setCurrentText(s.value("probe").toString());
    m_freqSpin->setValue(s.value("freqKHz", 4000).toInt());
    m_fileEdit->setText(s.value("file").toString());
    const QString fmt = s.value("format", "bin").toString();
    m_formatCombo->setCurrentIndex(qMax(0, m_formatCombo->findData(fmt)));
    m_addrEdit->setText(s.value("address").toString());
    const QString eraseMode = s.value("eraseMode", "chip").toString();
    m_eraseCombo->setCurrentIndex(qMax(0, m_eraseCombo->findData(eraseMode)));
    m_eraseAddrEdit->setText(s.value("eraseAddr", "0x08000000").toString());
    m_eraseSizeEdit->setText(s.value("eraseSize", "0x4000").toString());
    const QString dlErase = s.value("dlErase", "none").toString();
    m_dlEraseCombo->setCurrentIndex(qMax(0, m_dlEraseCombo->findData(dlErase)));
    m_resetAfterCheck->setChecked(s.value("resetAfter", true).toBool());
}

void MainWindow::saveSettings()
{
    QSettings s(kSettingsOrg, kSettingsApp);
    s.setValue("target", m_targetCombo->currentText());
    s.setValue("probe", m_probeCombo->currentText());
    s.setValue("freqKHz", m_freqSpin->value());
    s.setValue("file", m_fileEdit->text());
    s.setValue("format", m_formatCombo->currentData());
    s.setValue("address", m_addrEdit->text());
    s.setValue("eraseMode", m_eraseCombo->currentData());
    s.setValue("eraseAddr", m_eraseAddrEdit->text());
    s.setValue("eraseSize", m_eraseSizeEdit->text());
    s.setValue("dlErase", m_dlEraseCombo->currentData());
    s.setValue("resetAfter", m_resetAfterCheck->isChecked());
}

// --------------------------------------------------------------------------- //
// pyocd(自带)：定位 <根>\tools\own_pyocd\pyocd-windows-<版本>\pyocd.exe
// --------------------------------------------------------------------------- //
void MainWindow::onRedetectPyocd()
{
    initBundledPyocd();
}

void MainWindow::initBundledPyocd()
{
    QString version;
    const QString exe = PyocdLocator::findBundledPyocd(version);
    if (exe.isEmpty()) {
        m_pyocdLabel->setText(tr("未找到 pyocd.exe"));
        onLogLine("error", tr("未找到自带的 pyocd.exe，应位于 "
                              "tools\\own_pyocd\\pyocd-windows-<版本>\\pyocd.exe"));
        updateUiState();
        return;
    }
    m_pyocdLabel->setText(tr("%1  (pyocd %2)")
                              .arg(QDir::toNativeSeparators(exe), version));
    onLogLine("info", tr("使用自带 pyocd: %1 (pyocd %2)")
                .arg(QDir::toNativeSeparators(exe), version));
    applyPyocd(exe);
    updateUiState();
}

void MainWindow::applyPyocd(const QString &exe)
{
    const QStringList cmd{QDir::toNativeSeparators(exe)};
    const QStringList previous = m_runner->commandPrefix();
    const bool firstTime = previous.isEmpty();
    const bool changed = (previous != cmd);
    m_runner->setCommandPrefix(cmd);

    // 只有"第一次"或"换了 pyocd"才自动拉 target 列表
    if (!firstTime && !changed) {
        return;
    }
    if (m_runner->isRunning()) {
        // runner 正忙：挂起，等当前命令跑完再拉(否则这次刷新就丢了)
        m_autoRefreshPending = true;
        onLogLine("debug", tr("runner 正忙，target 列表刷新已排队"));
    } else {
        onRefreshTargets();
    }
}

QStringList MainWindow::bundledPacks() const
{
    // <exe_dir>\pyocd-windows-<ver>\pyocd.exe -> <exe_dir>\packs\<file>.pack
    const QString exePath = m_runner->commandPrefix().value(0);
    if (exePath.isEmpty()) {
        return {};
    }
    const QString exeDir = QFileInfo(exePath).absolutePath();
    const QString packsDir = QDir(exeDir).absoluteFilePath("../packs");
    QDir dir(packsDir);
    if (!dir.exists()) {
        return {};
    }
    QStringList out;
    for (const QString &f : dir.entryList(QStringList{"*.pack"}, QDir::Files, QDir::Name)) {
        out << dir.absoluteFilePath(f);
    }
    return out;
}

// --------------------------------------------------------------------------- //
// MCU / 固件
// --------------------------------------------------------------------------- //
void MainWindow::onRefreshTargets()
{
    if (m_runner->commandPrefix().isEmpty()) {
        onLogLine("warn", tr("尚未找到可用的 pyocd.exe"));
        return;
    }
    // list 子命令也支持 --pack：在其他电脑(全局 pack 缓存为空)上，
    // 不加 --pack 就看不到 pack 目标(如 stm32f405rgtx)
    QStringList args{"list", "--targets", "--no-header", "--color", "never"};
    for (const QString &pack : bundledPacks()) {
        args << "--pack" << QDir::toNativeSeparators(pack);
    }
    runPyocd(args, tr("刷新 target 列表"), false);      // 列 target 不需要探针
}

void MainWindow::onRefreshProbes()
{
    if (m_runner->commandPrefix().isEmpty()) {
        onLogLine("warn", tr("尚未找到可用的 pyocd.exe"));
        return;
    }
    runPyocd(QStringList{"list", "--probes", "--no-header", "--color", "never"},
             tr("刷新探针列表"), false);           // 列探针不需要探针
}

void MainWindow::onBrowseFirmware()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("选择固件"), QString(),
        tr("固件镜像 (*.bin *.hex *.elf *.axf *.out);;所有文件 (*.*)"));
    if (file.isEmpty()) {
        return;
    }
    m_fileEdit->setText(QDir::toNativeSeparators(file));
    const QString lower = file.toLower();
    if (lower.endsWith(".hex")) {
        m_formatCombo->setCurrentIndex(m_formatCombo->findData("hex"));
    } else if (lower.endsWith(".bin")) {
        m_formatCombo->setCurrentIndex(m_formatCombo->findData("bin"));
    } else {
        m_formatCombo->setCurrentIndex(m_formatCombo->findData("elf"));
    }
}

void MainWindow::onFormatChanged(int index)
{
    const QString fmt = m_formatCombo->itemData(index).toString();
    const bool isBin = (fmt == "bin");
    m_addrEdit->setEnabled(isBin);
    if (isBin) {
        m_addrEdit->setPlaceholderText(tr("0x08000000，留空 = flash 起始地址"));
        m_addrEdit->setToolTip(tr("BIN 没有地址信息，必须指定烧写地址；留空则用 flash 起始地址"));
    } else {
        m_addrEdit->setPlaceholderText(tr("由文件内地址决定，无需填写"));
        m_addrEdit->setToolTip(tr("Intel HEX / ELF 自带地址信息，pyocd 按文件内地址烧写"));
    }
}

// --------------------------------------------------------------------------- //
// 操作：连接 / 擦除 / 下载 / 复位
// --------------------------------------------------------------------------- //
QString MainWindow::selectedTarget() const
{
    return m_targetCombo->currentText().trimmed();
}

void MainWindow::onConnect()
{
    if (selectedTarget().isEmpty()) {
        QMessageBox::warning(this, tr("连接"), tr("请先填写 MCU 型号(target 名)。"));
        return;
    }
    if (m_runner->commandPrefix().isEmpty()) {
        QMessageBox::warning(this, tr("连接"), tr("尚未找到可用的 pyocd.exe。"));
        return;
    }

    // pyocd commander -c "status" -c "read32 0x08000000 8"
    //   status  -> 连接目标并打印核心状态(能连通就说明探针/target/频率都没问题)
    //   read32  -> 读向量表，顺带看出 flash 是空白还是已有固件
    // 注意：commander 的 -c 是 nargs='+'，一个 -c 后面的多个词会被合并成
    // 一条命令(status 不接受参数)，所以每条命令必须单独一个 -c。
    QStringList full;
    full << "commander" << "-t" << selectedTarget()
         << "-f" << QStringLiteral("%1k").arg(m_freqSpin->value())
         << "-c" << "status"
         << "-c" << "read32 0x08000000 8";
    const QString probe = m_probeCombo->currentData().toString();
    if (!probe.isEmpty()) {
        full << "-u" << probe;
    }
    m_runner->setProbeId(probe);

    m_connected = false;
    runPyocd(full, tr("连接"));
}

void MainWindow::onDisconnect()
{
    m_connected = false;
    m_connLabel->setText(tr("未连接"));
    onLogLine("info", tr("已断开(下次操作会重新连接)"));
    updateUiState();
}

void MainWindow::runPyocd(const QStringList &argv, const QString &what,
                          bool needProbeCheck)
{
    // 先确认能启动，再计入"待处理命令"，
    // 否则命令被忽略时计数不归零，按钮会永远灰着
    if (m_runner->isRunning()) {
        // 不弹模态框(会打断正在跑的流程)：只在日志和状态栏提示
        onLogLine("warn", tr("上一条命令还在执行，已忽略: %1").arg(what));
        statusBar()->showMessage(tr("上一条命令还在执行，请稍候..."), 3000);
        return;
    }

    // 自动带上自带 pack(如果存在且是 pack 目标)
    // list 子命令不需要 --pack；connect/erase/load/reset 都需要
    QStringList full = argv;
    if (needProbeCheck) {
        for (const QString &pack : bundledPacks()) {
            full << "--pack" << QDir::toNativeSeparators(pack);
        }
    }

    if (!m_runner->runCommand(full, what, needProbeCheck)) {
        onLogLine("error", tr("命令未能启动: %1").arg(what));
        return;
    }
    m_pendingArgv = full;
    m_pendingWhat = what;
    beginCommand(what);
}

bool MainWindow::loadFlashLayout()
{
    if (m_flashLayoutLoaded) {
        return !m_flashRegions.isEmpty() && !m_flashRegions.first().sectors.isEmpty();
    }
    const QStringList packs = bundledPacks();
    for (const QString &pack : packs) {
        m_flashRegions = PackParser::parseFlashLayout(pack, selectedTarget());
        if (!m_flashRegions.isEmpty()) {
            break;
        }
    }
    m_flashLayoutLoaded = true;
    return !m_flashRegions.isEmpty() && !m_flashRegions.first().sectors.isEmpty();
}

QVector<QPair<quint64, quint64>> MainWindow::pickSectors(QWidget *parent)
{
    if (selectedTarget().isEmpty()) {
        QMessageBox::warning(parent, tr("选择扇区"), tr("请先选择 MCU 型号。"));
        return {};
    }
    if (!loadFlashLayout()) {
        QMessageBox::warning(parent, tr("选择扇区"),
                             tr("无法获取 MCU Flash 布局。\n"
                                "请确认 pack 文件存在且包含该型号。"));
        return {};
    }

    const FlashRegion &region = m_flashRegions.first();
    SectorSelectorDialog dlg(parent);
    dlg.setFlashRegion(region);

    // 如果已选择固件，自动选中固件覆盖的扇区
    const QString file = m_fileEdit->text().trimmed();
    if (!file.isEmpty() && QFileInfo::exists(file)) {
        const QString fmt = m_formatCombo->currentData().toString();
        quint64 baseAddr = region.start;
        if (fmt == "bin") {
            bool ok = false;
            const quint64 a = m_addrEdit->text().trimmed().toULongLong(&ok, 16);
            if (ok) baseAddr = a;
        }
        const auto range = FirmwareParser::parseAddressRange(file, fmt, baseAddr);
        if (range.second > 0) {
            dlg.selectByFirmwareRange(range.first, range.second);
        }
    }

    if (dlg.exec() != QDialog::Accepted) {
        return {};
    }
    return dlg.selectedRanges();
}

void MainWindow::onSelectSectors()
{
    const auto ranges = pickSectors(this);
    if (ranges.isEmpty()) {
        return;
    }
    m_eraseRanges = ranges;

    quint64 minAddr = ~0ULL, maxEnd = 0, totalSize = 0;
    for (const auto &r : ranges) {
        minAddr = qMin(minAddr, r.first);
        maxEnd = qMax(maxEnd, r.first + r.second);
        totalSize += r.second;
    }

    m_eraseAddrEdit->setText(QStringLiteral("0x%1").arg(minAddr, 0, 16));
    m_eraseSizeEdit->setText(QStringLiteral("0x%1").arg(totalSize, 0, 16));
    onLogLine("info", tr("擦除扇区已选择 %1 个: 0x%2 - 0x%3, 共 %4 字节")
                 .arg(ranges.size()).arg(minAddr, 0, 16)
                 .arg(maxEnd - 1, 0, 16).arg(totalSize));
}

void MainWindow::onSelectDownloadSectors()
{
    const auto ranges = pickSectors(this);
    if (ranges.isEmpty()) {
        return;
    }
    m_dlEraseRanges = ranges;

    quint64 totalSize = 0;
    for (const auto &r : ranges) {
        totalSize += r.second;
    }
    onLogLine("info", tr("下载前擦除扇区已选择 %1 个, 共 %2 字节")
                 .arg(ranges.size()).arg(totalSize));
}

void MainWindow::onEraseChip()
{
    if (selectedTarget().isEmpty()) {
        QMessageBox::warning(this, tr("擦除"), tr("请先填写 MCU 型号(target 名)。"));
        return;
    }
    if (m_runner->commandPrefix().isEmpty()) {
        QMessageBox::warning(this, tr("擦除"), tr("尚未找到可用的 pyocd.exe。"));
        return;
    }
    if (!m_connected) {
        QMessageBox::information(this, tr("擦除"), tr("请先点「连接」确认设备可达。"));
        return;
    }

    const QString eraseMode = m_eraseCombo->currentData().toString();

    if (eraseMode == "chip") {
        const auto ret = QMessageBox::question(this, tr("擦除芯片"),
                                               tr("确认整片擦除目标芯片？该操作不可恢复。"),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            return;
        }
    } else {
        // sector 模式：优先用扇区选择的结果，否则用地址/大小框
        if (m_eraseRanges.isEmpty()) {
            const QString addrStr = m_eraseAddrEdit->text().trimmed();
            const QString sizeStr = m_eraseSizeEdit->text().trimmed();
            if (addrStr.isEmpty() || sizeStr.isEmpty()) {
                QMessageBox::warning(this, tr("擦除"),
                                     tr("请先点「选择扇区...」选择要擦除的区域，\n"
                                        "或手动填写地址和大小。"));
                return;
            }
            bool okAddr = false, okSize = false;
            const quint64 addr = addrStr.toULongLong(&okAddr, 16);
            const quint64 size = sizeStr.toULongLong(&okSize, 16);
            if (!okAddr || !okSize || size == 0) {
                QMessageBox::warning(this, tr("擦除"),
                                     tr("地址或大小格式不正确，请输入十六进制数。"));
                return;
            }
            m_eraseRanges = {{addr, size}};
        }

        quint64 totalSize = 0;
        for (const auto &r : m_eraseRanges) totalSize += r.second;
        const auto ret = QMessageBox::question(this, tr("按扇区擦除"),
                                               tr("确认擦除选中的 %1 个扇区(共 %2 字节)？")
                                                   .arg(m_eraseRanges.size()).arg(totalSize),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            return;
        }
    }

    // 构造命令队列（多扇区时每扇区一条命令）
    const QString probe = m_probeCombo->currentData().toString();
    m_runner->setProbeId(probe);

    m_cmdQueue.clear();
    m_queueHadError = false;

    if (eraseMode == "chip") {
        QStringList args;
        args << "erase" << "--chip"
             << "-t" << selectedTarget()
             << "-f" << QStringLiteral("%1k").arg(m_freqSpin->value());
        m_cmdQueue.enqueue({args, tr("整片擦除")});
    } else {
        for (const auto &r : m_eraseRanges) {
            const quint64 end = r.first + r.second - 1;
            QStringList args;
            args << "erase" << "--sector"
                 << QStringLiteral("0x%1-0x%2").arg(r.first, 0, 16).arg(end, 0, 16)
                 << "-t" << selectedTarget()
                 << "-f" << QStringLiteral("%1k").arg(m_freqSpin->value());
            m_cmdQueue.enqueue({args, tr("擦除扇区 0x%1").arg(r.first, 0, 16)});
        }
    }

    m_queueTotal = m_cmdQueue.size();
    QueuedCmd first = m_cmdQueue.dequeue();
    QStringList full = first.argv;
    if (!probe.isEmpty()) {
        full << "-u" << probe;
    }
    // pack 由 runPyocd 统一添加，这里不重复加
    runPyocd(full, first.what);
}

void MainWindow::onDownload()
{
    const QString file = m_fileEdit->text().trimmed();
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        QMessageBox::warning(this, tr("下载"), tr("固件文件不存在。"));
        return;
    }
    if (selectedTarget().isEmpty()) {
        QMessageBox::warning(this, tr("下载"), tr("请先填写 MCU 型号(target 名)。"));
        return;
    }
    if (m_runner->commandPrefix().isEmpty()) {
        QMessageBox::warning(this, tr("下载"), tr("尚未找到可用的 pyocd.exe。"));
        return;
    }
    if (!m_connected) {
        QMessageBox::information(this, tr("下载"), tr("请先点「连接」确认设备可达。"));
        return;
    }
    const QString fmt = m_formatCombo->currentData().toString();
    if (fmt == "bin" && !m_addrEdit->text().trimmed().isEmpty()) {
        bool ok = false;
        m_addrEdit->text().trimmed().toULongLong(&ok, 16);
        if (!ok) {
            QMessageBox::warning(this, tr("下载"), tr("下载地址不是合法的十六进制数。"));
            return;
        }
    }

    const QString dlErase = m_dlEraseCombo->currentData().toString();

    // ---- 确定要擦除的扇区范围 ----
    QVector<QPair<quint64, quint64>> eraseRanges;
    if (dlErase == "firmware") {
        // 按固件地址自动计算要擦除的扇区
        if (!loadFlashLayout()) {
            QMessageBox::warning(this, tr("下载"),
                                 tr("无法获取 MCU Flash 布局，无法自动计算擦除范围。"));
            return;
        }
        const FlashRegion &region = m_flashRegions.first();
        quint64 baseAddr = region.start;
        if (fmt == "bin") {
            bool ok = false;
            const quint64 a = m_addrEdit->text().trimmed().toULongLong(&ok, 16);
            if (ok) baseAddr = a;
        }
        const auto fwRange = FirmwareParser::parseAddressRange(file, fmt, baseAddr);
        if (fwRange.second == 0) {
            QMessageBox::warning(this, tr("下载"),
                                 tr("无法解析固件地址范围，请改用「擦除选中扇区」或「整片擦除」。"));
            return;
        }
        const quint64 fwEnd = fwRange.first + fwRange.second - 1;
        for (const auto &s : region.sectors) {
            const quint64 sEnd = s.address + s.size - 1;
            if (s.address <= fwEnd && sEnd >= fwRange.first) {
                eraseRanges.append({s.address, s.size});
            }
        }
        onLogLine("info", tr("固件覆盖 0x%1 - 0x%2，需擦除 %3 个扇区")
                     .arg(fwRange.first, 0, 16).arg(fwEnd, 0, 16).arg(eraseRanges.size()));
    } else if (dlErase == "sectors") {
        if (m_dlEraseRanges.isEmpty()) {
            QMessageBox::warning(this, tr("下载"),
                                 tr("请先点「选择扇区...」选择下载前要擦除的扇区。"));
            return;
        }
        eraseRanges = m_dlEraseRanges;
    }

    // ---- 构造命令队列 ----
    m_cmdQueue.clear();
    m_queueHadError = false;

    // 1. 擦除命令(如有需要)
    if (dlErase == "chip") {
        QStringList args;
        args << "erase" << "--chip"
             << "-t" << selectedTarget()
             << "-f" << QStringLiteral("%1k").arg(m_freqSpin->value());
        m_cmdQueue.enqueue({args, tr("整片擦除")});
    } else if (!eraseRanges.isEmpty()) {
        for (const auto &r : eraseRanges) {
            const quint64 end = r.first + r.second - 1;
            QStringList args;
            args << "erase" << "--sector"
                 << QStringLiteral("0x%1-0x%2").arg(r.first, 0, 16).arg(end, 0, 16)
                 << "-t" << selectedTarget()
                 << "-f" << QStringLiteral("%1k").arg(m_freqSpin->value());
            m_cmdQueue.enqueue({args, tr("擦除扇区 0x%1").arg(r.first, 0, 16)});
        }
    }

    // 2. 下载命令
    {
        QStringList args;
        args << "load" << QDir::toNativeSeparators(file)
             << "-t" << selectedTarget()
             << "-f" << QStringLiteral("%1k").arg(m_freqSpin->value());
        if (dlErase == "chip") {
            // pyocd load -e chip 让它自己整片擦，就不单独擦了
            args << "-e" << "chip";
            // 把刚才 enqueue 的 chip erase 去掉，避免重复
            m_cmdQueue.clear();
            m_queueHadError = false;
        } else if (dlErase == "sectors" || dlErase == "firmware") {
            // 扇区已在前面单独擦除，load 时不再重复擦除
            // pyocd load 的 -e 只有 auto/chip/sector，没有 none；
            // 传 sector 让 pyocd 按需擦(已擦过的扇区不会重复擦)
            args << "-e" << "sector";
        }
        // dlErase == "none": 不传 -e，用 pyocd 默认 auto
        if (fmt != "auto") {
            args << "--format" << fmt;
        }
        if (fmt == "bin" && !m_addrEdit->text().trimmed().isEmpty()) {
            args << "-a" << m_addrEdit->text().trimmed();
        }
        if (!m_resetAfterCheck->isChecked()) {
            args << "--no-reset";
        }
        m_cmdQueue.enqueue({args, tr("下载固件")});
    }

    // 3. 探针和 pack 附加到每条命令
    const QString probe = m_probeCombo->currentData().toString();
    m_runner->setProbeId(probe);
    m_queueTotal = m_cmdQueue.size();

    // 启动第一条
    QueuedCmd first = m_cmdQueue.dequeue();
    QStringList full = first.argv;
    if (!probe.isEmpty()) {
        full << "-u" << probe;
    }
    // pack 由 runPyocd 统一添加，这里不重复加
    runPyocd(full, first.what);
}

void MainWindow::onResetRun()
{
    if (selectedTarget().isEmpty()) {
        QMessageBox::warning(this, tr("复位"), tr("请先填写 MCU 型号(target 名)。"));
        return;
    }
    if (m_runner->commandPrefix().isEmpty()) {
        QMessageBox::warning(this, tr("复位"), tr("尚未找到可用的 pyocd.exe。"));
        return;
    }
    if (!m_connected) {
        QMessageBox::information(this, tr("复位"), tr("请先点「连接」确认设备可达。"));
        return;
    }
    // pyocd reset --target <t> -f <频率> [-u <探针>]
    QStringList full;
    full << "reset" << "-t" << selectedTarget()
         << "-f" << QStringLiteral("%1k").arg(m_freqSpin->value());
    const QString probe = m_probeCombo->currentData().toString();
    if (!probe.isEmpty()) {
        full << "-u" << probe;
    }
    m_runner->setProbeId(probe);
    runPyocd(full, tr("复位运行"));
}

void MainWindow::onStopOperation()
{
    if (m_runner->isRunning()) {
        onLogLine("warn", tr("用户中止当前 pyocd 命令"));
        m_runner->stop();
    }
    // 清空队列，无论如何都把忙碌状态清掉，避免界面卡在"全灰"
    m_cmdQueue.clear();
    m_queueTotal = 0;
    m_pendingCommands = 0;
    m_connected = false;
    setBusy(false, tr("已中止"));
    updateUiState();
}

// --------------------------------------------------------------------------- //
// 日志
// --------------------------------------------------------------------------- //
void MainWindow::onClearLog()
{
    m_logEdit->clear();
}

void MainWindow::onSaveLog()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("保存日志"),
                                                      "pyocd_ui.log",
                                                      tr("日志文件 (*.log *.txt)"));
    if (file.isEmpty()) {
        return;
    }
    QFile out(file);
    if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        out.write(m_logEdit->toPlainText().toUtf8());
        onLogLine("info", tr("日志已保存: %1").arg(file));
    } else {
        onLogLine("error", tr("保存日志失败: %1").arg(file));
    }
}

// --------------------------------------------------------------------------- //
// runner 回调
// --------------------------------------------------------------------------- //
void MainWindow::onLogLine(const QString &level, const QString &msg)
{
    QTextCharFormat fmt;
    if (level == "error") {
        fmt.setForeground(Qt::red);
    } else if (level == "warn") {
        fmt.setForeground(QColor(0xC0, 0x60, 0x00));
    } else if (level == "debug" || level == "stderr") {
        fmt.setForeground(Qt::gray);
    } else {
        fmt.setForeground(Qt::black);
    }
    QTextCursor cursor(m_logEdit->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("[%1] %2\n").arg(level, msg), fmt);
    m_logEdit->setTextCursor(cursor);
    m_logEdit->ensureCursorVisible();
}

void MainWindow::onProgress(int percent)
{
    m_progressBar->setValue(qBound(0, percent, 100));
    m_opStatusLabel->setText(tr("%1 ... %2%").arg(m_pendingWhat.isEmpty()
                                                  ? tr("执行中") : m_pendingWhat).arg(percent));
}

void MainWindow::onCommandFinished(bool ok, int exitCode, const QString &what)
{
    Q_UNUSED(exitCode)
    if (m_pendingCommands > 0) {
        --m_pendingCommands;
    }
    if (!ok) {
        m_queueHadError = true;
        onLogLine("warn", tr("%1 未成功，详细过程见上面的日志").arg(what));
    }

    // 「连接」命令的结果决定后续操作是否可用
    if (what == tr("连接")) {
        m_connected = ok;
        m_connLabel->setText(ok ? tr("已连接: %1").arg(selectedTarget())
                                : tr("未连接"));
        if (ok) {
            onLogLine("info", tr("设备已连接，可以擦除/下载/复位了"));
        }
    }

    // 队列里还有下一条命令：继续执行
    if (!ok || m_cmdQueue.isEmpty()) {
        // 出错或队列空：结束本批次
        if (m_pendingCommands == 0) {
            if (m_queueTotal > 0) {
                const QString summary = m_queueHadError
                    ? tr("失败(共 %1 步)").arg(m_queueTotal)
                    : tr("完成(共 %1 步)").arg(m_queueTotal);
                setBusy(false, summary);
                m_progressBar->setValue(m_queueHadError ? m_progressBar->value() : 100);
                m_queueTotal = 0;
            } else {
                setBusy(false, ok ? tr("完成: %1").arg(what) : tr("失败: %1").arg(what));
                m_progressBar->setValue(ok ? 100 : m_progressBar->value());
            }
        }
    } else {
        // 启动下一条
        QueuedCmd next = m_cmdQueue.dequeue();
        QStringList full = next.argv;
        const QString probe = m_probeCombo->currentData().toString();
        if (!probe.isEmpty()) {
            full << "-u" << probe;
        }
        // pack 由 runPyocd 统一添加，这里不重复加
        m_pendingWhat = next.what;
        onLogLine("info", tr("[%1/%2] %3")
                     .arg(m_queueTotal - m_cmdQueue.size())
                     .arg(m_queueTotal).arg(next.what));
        runPyocd(full, next.what);
    }

    updateUiState();

    // 环境切换时被挂起的 target 列表刷新，等命令跑完了再补上
    if (m_autoRefreshPending && !m_runner->isRunning()) {
        m_autoRefreshPending = false;
        onRefreshTargets();
    }
}

void MainWindow::onOutputReady(const QString &what, const QString &text)
{
    if (what == tr("刷新 target 列表")) {
        const QStringList names = parseTargetList(text);
        if (names.isEmpty()) {
            onLogLine("warn", tr("没解析到 target 列表，请检查 pyocd / pack"));
            return;
        }
        const QString current = m_targetCombo->currentText();
        m_targetCombo->clear();
        m_targetCombo->addItems(names);
        if (!current.isEmpty()) {
            m_targetCombo->setCurrentText(current);
        }
        onLogLine("info", tr("target 列表已更新(%1 个)").arg(names.size()));
    } else if (what == tr("刷新探针列表")) {
        const QVariantList probes = PyOcdRunner::parseProbeTable(text);
        m_probeCombo->clear();
        m_probeCombo->addItem(tr("(自动)"), QString());
        for (const QVariant &v : probes) {
            const QVariantMap map = v.toMap();
            m_probeCombo->addItem(QStringLiteral("%1  [%2]").arg(map.value("desc").toString(),
                                                                 map.value("id").toString()),
                                  map.value("id").toString());
        }
        onLogLine("info", probes.isEmpty() ? tr("未检测到探针")
                                          : tr("探针 %1 个").arg(probes.size()));
    } else if (what == tr("连接")) {
        // 解析 read32 的输出，判断 flash 是空白还是已有固件
        // 形如: 08000000:  20001000 08000101 deadbeef ...
        static const QRegularExpression kReadLine(
            R"(^([0-9A-Fa-f]{8}):\s+((?:[0-9A-Fa-f]{8}\s*)+)$)");
        for (const QString &raw : text.split(QRegularExpression("[\\r\n]"),
                                             Qt::SkipEmptyParts)) {
            const QRegularExpressionMatch m = kReadLine.match(raw.trimmed());
            if (!m.hasMatch()) {
                continue;
            }
            const QStringList words = m.captured(2).split(QRegularExpression("\\s+"),
                                                          Qt::SkipEmptyParts);
            const bool blank = !words.isEmpty()
                    && std::all_of(words.cbegin(), words.cend(),
                                   [](const QString &w) {
                                       return w.toLower() == "ffffffff";
                                   });
            onLogLine("info", tr("flash 0x%1: %2")
                        .arg(m.captured(1),
                             blank ? tr("空白(未烧录固件)")
                                   : tr("已有内容(已烧录固件)")));
            break;
        }
    }
}

void MainWindow::onProbeChoiceNeeded(const QVariantList &probes)
{
    QStringList labels;
    QStringList ids;
    for (const QVariant &v : probes) {
        const QVariantMap map = v.toMap();
        labels << QStringLiteral("%1  [%2]").arg(map.value("desc").toString(),
                                                 map.value("id").toString());
        ids << map.value("id").toString();
    }
    bool ok = false;
    const QString picked = QInputDialog::getItem(this, tr("选择探针"),
                                                 tr("检测到多个调试探针，请选择:"),
                                                 labels, 0, false, &ok);
    if (!ok || picked.isEmpty()) {
        onLogLine("warn", tr("已取消选择探针"));
        if (m_pendingCommands > 0) {
            --m_pendingCommands;
        }
        setBusy(false, tr("已取消"));
        updateUiState();
        return;
    }
    const int index = labels.indexOf(picked);
    if (index < 0) {
        return;
    }
    m_probeCombo->setCurrentText(labels.at(index));
    m_runner->setProbeId(ids.at(index));
    onLogLine("info", tr("已选择探针: %1").arg(labels.at(index)));

    // 用选定的探针重新执行刚才的操作
    const QStringList argv = m_pendingArgv;
    const QString what = m_pendingWhat;
    QStringList full = argv;
    full << "-u" << ids.at(index);
    if (!m_runner->runDirect(full, what)) {
        onLogLine("error", tr("命令启动失败: %1").arg(what));
        if (m_pendingCommands > 0) {
            --m_pendingCommands;
        }
        setBusy(false, tr("失败: %1").arg(what));
        updateUiState();
    }
}

// --------------------------------------------------------------------------- //
// 辅助
// --------------------------------------------------------------------------- //
void MainWindow::beginCommand(const QString &what)
{
    ++m_pendingCommands;
    m_pendingWhat = what;
    m_progressBar->setValue(0);
    setBusy(true, what);
}

void MainWindow::setBusy(bool busy, const QString &what)
{
    m_busy = busy;
    if (!what.isEmpty()) {
        m_opStatusLabel->setText(what);
    }
    statusBar()->showMessage(what);
    updateUiState();
}

void MainWindow::updateUiState()
{
    const bool running = m_runner->isRunning();
    const bool busy = m_busy || running;
    const bool ready = !m_runner->commandPrefix().isEmpty();

    m_stopBtn->setEnabled(running);
    m_redetectBtn->setEnabled(!running);
    m_refreshTargetsBtn->setEnabled(ready && !busy);
    m_refreshProbesBtn->setEnabled(!busy);
    m_connectBtn->setEnabled(ready && !busy);
    m_disconnectBtn->setEnabled(!busy);

    const bool canOperate = ready && m_connected && !busy;
    m_eraseBtn->setEnabled(canOperate);
    m_downloadBtn->setEnabled(canOperate);
    m_resetBtn->setEnabled(canOperate);

    if (m_pendingCommands == 0 && !m_busy) {
        if (!ready) {
            m_opStatusLabel->setText(tr("未找到可用的 pyocd.exe"));
        } else if (m_connected) {
            m_opStatusLabel->setText(tr("已连接，可以操作"));
        } else {
            m_opStatusLabel->setText(tr("请点「连接」"));
        }
    }
    if (!m_connected) {
        m_connLabel->setText(tr("未连接"));
    }
}
