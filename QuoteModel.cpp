#include "QuoteModel.h"
#include <algorithm>
#include <QtMath>

QuoteModel::QuoteModel(QObject* parent) : QAbstractTableModel(parent) {}

int QuoteModel::rowCount(const QModelIndex&) const { return m_rows.size(); }
int QuoteModel::columnCount(const QModelIndex&) const { return 7; }

QVariant QuoteModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (o == Qt::Horizontal) {
        switch (section) {
        case 0: return "代码";
        case 1: return "名称";
        case 2: return "市场";
        case 3: return "现价";
        case 4: return "MA5(近收)";
        case 5: return "偏离(%)";
        case 6: return "N(收<MA5)";
        default: return {};
        }
    }
    return section + 1;
}

QVariant QuoteModel::data(const QModelIndex& idx, int role) const
{
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_rows.size()) return {};
    const auto& r = m_rows[idx.row()];

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case 0: return r.code;
        case 1: return r.name;
        case 2: return r.market;
        case 3: return QString::number(r.last, 'f', 3);
        case 4: return QString::number(r.ma5, 'f', 3);
        case 5: return QString::number(r.biasPct, 'f', 2);
        case 6: return r.belowDays;
        default: return {};
        }
    }
    return {};
}

void QuoteModel::setRows(const QVector<PickRow>& rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

void QuoteModel::sort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;

    std::sort(m_rows.begin(), m_rows.end(), [&](const PickRow& a, const PickRow& b){
        auto less = [&](auto x, auto y){ return (order == Qt::AscendingOrder) ? (x < y) : (x > y); };
        switch (column) {
        case 0: return less(a.code, b.code);
        case 1: return less(a.name, b.name);
        case 2: return less(a.market, b.market);
        case 3: return less(a.last, b.last);
        case 4: return less(a.ma5, b.ma5);
        case 5: return less(a.biasPct, b.biasPct);
        case 6: return less(a.belowDays, b.belowDays);
        default: return false;
        }
    });

    emit dataChanged(index(0,0), index(rowCount()-1, columnCount()-1));
}
