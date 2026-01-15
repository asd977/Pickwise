#include "KlineButtonDelegate.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionButton>

KlineButtonDelegate::KlineButtonDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{}

void KlineButtonDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    QStyleOptionButton button;
    button.rect = option.rect.adjusted(6, 4, -6, -4);
    button.state = QStyle::State_Enabled;
    button.text = QStringLiteral("K线");
    if (option.state & QStyle::State_MouseOver) {
        button.state |= QStyle::State_MouseOver;
    }
    QApplication::style()->drawControl(QStyle::CE_PushButton, &button, painter);
    Q_UNUSED(index);
}

bool KlineButtonDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index)
{
    Q_UNUSED(model);
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (option.rect.contains(mouseEvent->pos())) {
            emit klineClicked(index);
        }
    }
    return true;
}
