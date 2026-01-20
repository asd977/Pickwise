#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include <QString>

struct PullbackRow
{
    QString code;
    QString name;
    QString sector;
    double pe = 0;
    int market = 0;      // f13
    double last = 0;     // 现价（快照）
    int maPeriod = 0;    // 均线周期
    double maValue = 0;  // 最近已收盘日 MA（rolling）
    double biasPct = 0;  // (last/ma-1)*100
    int belowDays = 0;   // 你输入的 N
};

class PullbackModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit PullbackModel(QObject* parent=nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void sort(int column, Qt::SortOrder order) override;

    void setRows(const QVector<PullbackRow>& rows);
    const QVector<PullbackRow>& rows() const { return m_rows; }

private:
    QVector<PullbackRow> m_rows;
    int m_sortColumn = -1;
    Qt::SortOrder m_sortOrder = Qt::DescendingOrder;
};
