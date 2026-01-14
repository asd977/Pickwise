#pragma once

#include <QDialog>

class QWebEngineView;
class QLabel;

class KlineDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KlineDialog(const QString& code, int market, const QString& name, QWidget* parent = nullptr);

private:
    QString toTradingViewSymbol(const QString& code, int market) const;
    void loadChart(const QString& symbol);

    QWebEngineView* m_view = nullptr;
    QLabel* m_titleLabel = nullptr;
};
