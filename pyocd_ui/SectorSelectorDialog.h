#ifndef SECTORSELECTORDIALOG_H
#define SECTORSELECTORDIALOG_H

#include "PackParser.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QTableWidget;
class QVBoxLayout;

/// 扇区选择对话框：显示 Flash 扇区列表，让用户勾选要擦除的区域
class SectorSelectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SectorSelectorDialog(QWidget *parent = nullptr);

    /// 设置 Flash 区域和扇区列表
    void setFlashRegion(const FlashRegion &region);

    /// 根据固件地址范围自动选择扇区
    /// @param start 固件起始地址
    /// @param size 固件大小
    void selectByFirmwareRange(quint64 start, quint64 size);

    /// 获取选中的扇区地址范围列表
    /// @return 选中的扇区范围(地址-大小对)
    QVector<QPair<quint64, quint64>> selectedRanges() const;

    /// 获取选中的扇区数量
    int selectedCount() const;

private slots:
    void onSelectAll();
    void onSelectNone();
    void onSelectInvert();
    void onItemChanged();

private:
    void buildUi();
    void updateSummary();

    QTableWidget *m_table = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;

    FlashRegion m_region;
    QVector<FlashSector> m_sectors;
};

#endif // SECTORSELECTORDIALOG_H
