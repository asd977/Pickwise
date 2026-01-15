#pragma once

#include <QStyledItemDelegate>

class KlineButtonDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit KlineButtonDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

signals:
    void klineClicked(const QModelIndex& index);
};
