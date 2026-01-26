#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include <QString>

struct PickRow
{
    QString code;
    QString name;
    QString sector;
    double pe = 0;
    int market = 0;      // f13
    double last = 0;     // 现价（快照）
    double ma5 = 0;      // 最近已收盘日 MA5（rolling）
    double biasPct = 0;  // (last/ma5-1)*100
    int belowDays = 0;   // 你输入的 N
};

class QuoteModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit QuoteModel(QObject* parent=nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void sort(int column, Qt::SortOrder order) override;

    void setRows(const QVector<PickRow>& rows);
    void setDaysHeaderLabel(const QString& label);
    const QVector<PickRow>& rows() const { return m_rows; }

private:
    QVector<PickRow> m_rows;
    QString m_daysHeaderLabel = "N(收<MA5)";
    int m_sortColumn = -1;
    Qt::SortOrder m_sortOrder = Qt::DescendingOrder;
};
