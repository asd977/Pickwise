#pragma once

#include <QWidget>
#include <QList>
#include <QVector>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QLineEdit;
class QDateEdit;
class QPushButton;
class QLabel;
class QSpinBox;
class QDate;
namespace QtCharts { class QChartView; }

class BacktestWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BacktestWidget(QWidget* parent = nullptr);

private:
    void setBusy(bool busy);
    void startRequest();
    void requestKline(const QString& code, const QDate& startDate, const QDate& endDate,
                      const QList<int>& markets, int marketIndex);
    void renderBacktest(const QString& code, const QDate& startDate, const QDate& endDate,
                        const QVector<QString>& dates, const QVector<double>& opens,
                        const QVector<double>& closes, const QVector<double>& highs,
                        const QVector<double>& lows);

    QLineEdit* m_codeEdit = nullptr;
    QDateEdit* m_startDateEdit = nullptr;
    QDateEdit* m_endDateEdit = nullptr;
    QSpinBox* m_buyBelowDaysSpin = nullptr;
    QSpinBox* m_sellBelowDaysSpin = nullptr;
    QPushButton* m_runButton = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QtCharts::QChartView* m_chartView = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
};
