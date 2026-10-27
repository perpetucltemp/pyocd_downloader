#include "SectorSelectorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

SectorSelectorDialog::SectorSelectorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("选择要擦除的扇区"));
    setMinimumSize(500, 400);
    buildUi();
}

void SectorSelectorDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    // 工具按钮行
    auto *btnRow = new QHBoxLayout();
    auto *selectAllBtn = new QPushButton(tr("全选"), this);
    auto *selectNoneBtn = new QPushButton(tr("全不选"), this);
    auto *invertBtn = new QPushButton(tr("反选"), this);
    btnRow->addWidget(selectAllBtn);
    btnRow->addWidget(selectNoneBtn);
    btnRow->addWidget(invertBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    connect(selectAllBtn, &QPushButton::clicked, this, &SectorSelectorDialog::onSelectAll);
    connect(selectNoneBtn, &QPushButton::clicked, this, &SectorSelectorDialog::onSelectNone);
    connect(invertBtn, &QPushButton::clicked, this, &SectorSelectorDialog::onSelectInvert);

    // 扇区表格
    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({tr(""), tr("扇区"), tr("地址"), tr("大小")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QTableWidget::SelectRows);
    layout->addWidget(m_table, 1);

    connect(m_table, &QTableWidget::itemChanged,
            this, &SectorSelectorDialog::onItemChanged);

    // 摘要
    m_summaryLabel = new QLabel(tr("已选择 0 个扇区"), this);
    layout->addWidget(m_summaryLabel);

    // 按钮
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                       this);
    layout->addWidget(m_buttonBox);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SectorSelectorDialog::setFlashRegion(const FlashRegion &region)
{
    m_region = region;
    m_sectors = region.sectors;

    m_table->setRowCount(m_sectors.size());
    m_table->blockSignals(true);

    for (int i = 0; i < m_sectors.size(); ++i) {
        const FlashSector &s = m_sectors[i];

        // 勾选框
        auto *checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 0, checkItem);

        // 扇区号
        auto *idxItem = new QTableWidgetItem(QStringLiteral("[%1]").arg(s.index, 3, 10, QChar('0')));
        idxItem->setFlags(idxItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 1, idxItem);

        // 地址
        auto *addrItem = new QTableWidgetItem(
            QStringLiteral("0x%1 - 0x%2")
                .arg(s.address, 8, 16, QChar('0'))
                .arg(s.address + s.size - 1, 8, 16, QChar('0')));
        addrItem->setFlags(addrItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 2, addrItem);

        // 大小
        QString sizeStr;
        if (s.size >= 1024 * 1024) {
            sizeStr = QStringLiteral("%1 MB").arg(s.size / (1024.0 * 1024.0), 0, 'f', 1);
        } else if (s.size >= 1024) {
            sizeStr = QStringLiteral("%1 KB").arg(s.size / 1024.0, 0, 'f', 1);
        } else {
            sizeStr = QStringLiteral("%1 B").arg(s.size);
        }
        auto *sizeItem = new QTableWidgetItem(sizeStr);
        sizeItem->setFlags(sizeItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 3, sizeItem);
    }

    m_table->blockSignals(false);
    m_table->resizeColumnsToContents();
    updateSummary();
}

void SectorSelectorDialog::selectByFirmwareRange(quint64 start, quint64 size)
{
    if (size == 0) {
        return;
    }
    const quint64 end = start + size - 1;

    m_table->blockSignals(true);
    for (int i = 0; i < m_sectors.size(); ++i) {
        const FlashSector &s = m_sectors[i];
        const quint64 sEnd = s.address + s.size - 1;

        // 扇区与固件范围有交集就选中
        const bool overlap = (s.address <= end) && (sEnd >= start);
        QTableWidgetItem *item = m_table->item(i, 0);
        if (item) {
            item->setCheckState(overlap ? Qt::Checked : Qt::Unchecked);
        }
    }
    m_table->blockSignals(false);
    updateSummary();
}

QVector<QPair<quint64, quint64>> SectorSelectorDialog::selectedRanges() const
{
    QVector<QPair<quint64, quint64>> ranges;
    for (int i = 0; i < m_sectors.size(); ++i) {
        QTableWidgetItem *item = m_table->item(i, 0);
        if (item && item->checkState() == Qt::Checked) {
            ranges.append({m_sectors[i].address, m_sectors[i].size});
        }
    }
    return ranges;
}

int SectorSelectorDialog::selectedCount() const
{
    int count = 0;
    for (int i = 0; i < m_sectors.size(); ++i) {
        QTableWidgetItem *item = m_table->item(i, 0);
        if (item && item->checkState() == Qt::Checked) {
            ++count;
        }
    }
    return count;
}

void SectorSelectorDialog::onSelectAll()
{
    m_table->blockSignals(true);
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QTableWidgetItem *item = m_table->item(i, 0);
        if (item) item->setCheckState(Qt::Checked);
    }
    m_table->blockSignals(false);
    updateSummary();
}

void SectorSelectorDialog::onSelectNone()
{
    m_table->blockSignals(true);
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QTableWidgetItem *item = m_table->item(i, 0);
        if (item) item->setCheckState(Qt::Unchecked);
    }
    m_table->blockSignals(false);
    updateSummary();
}

void SectorSelectorDialog::onSelectInvert()
{
    m_table->blockSignals(true);
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QTableWidgetItem *item = m_table->item(i, 0);
        if (item) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
    }
    m_table->blockSignals(false);
    updateSummary();
}

void SectorSelectorDialog::onItemChanged()
{
    updateSummary();
}

void SectorSelectorDialog::updateSummary()
{
    const int count = selectedCount();
    quint64 totalSize = 0;
    for (int i = 0; i < m_sectors.size(); ++i) {
        QTableWidgetItem *item = m_table->item(i, 0);
        if (item && item->checkState() == Qt::Checked) {
            totalSize += m_sectors[i].size;
        }
    }

    QString sizeStr;
    if (totalSize >= 1024 * 1024) {
        sizeStr = QStringLiteral("%1 MB").arg(totalSize / (1024.0 * 1024.0), 0, 'f', 2);
    } else if (totalSize >= 1024) {
        sizeStr = QStringLiteral("%1 KB").arg(totalSize / 1024.0, 0, 'f', 1);
    } else {
        sizeStr = QStringLiteral("%1 B").arg(totalSize);
    }

    m_summaryLabel->setText(tr("已选择 %1 / %2 个扇区，共 %3")
                                .arg(count).arg(m_sectors.size()).arg(sizeStr));
}
