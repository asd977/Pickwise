#include "BacktestWidget.h"

#include <QLineEdit>
#include <QDateEdit>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QCandlestickSeries>
#include <QtCharts/QCandlestickSet>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
static const char* kEM_UT = "fa5fd1943c7b386f172d6893dbfba10b";

static void FillCommonHeaders(QNetworkRequest& req) {
    req.setRawHeader("User-Agent", "Mozilla/5.0");
    req.setRawHeader("Accept", "application/json,text/plain,*/*");
    req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
    req.setRawHeader("Referer", "https://quote.eastmoney.com/");
}

static QList<int> marketsForCode(const QString& code) {
    QList<int> markets;
    if (code.startsWith("6")) {
        markets << 1;
    } else if (code.startsWith("8") || code.startsWith("4") || code.startsWith("43")
               || code.startsWith("83") || code.startsWith("87") || code.startsWith("88")) {
        markets << 2;
    } else {
        markets << 0;
    }
    if (!markets.contains(0)) markets << 0;
    if (!markets.contains(1)) markets << 1;
    if (!markets.contains(2)) markets << 2;
    return markets;
}
}

BacktestWidget::BacktestWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* topLayout = new QVBoxLayout(this);
    auto* formLayout = new QGridLayout();

    auto* codeLabel = new QLabel("股票代码:", this);
    m_codeEdit = new QLineEdit(this);
    m_codeEdit->setPlaceholderText("例如 600519");

    auto* startLabel = new QLabel("开始日期:", this);
    m_startDateEdit = new QDateEdit(this);
    m_startDateEdit->setCalendarPopup(true);
    m_startDateEdit->setDate(QDate::currentDate().addMonths(-6));
    m_startDateEdit->setDisplayFormat("yyyy-MM-dd");

    auto* endLabel = new QLabel("结束日期:", this);
    m_endDateEdit = new QDateEdit(this);
    m_endDateEdit->setCalendarPopup(true);
    m_endDateEdit->setDate(QDate::currentDate());
    m_endDateEdit->setDisplayFormat("yyyy-MM-dd");

    m_runButton = new QPushButton("开始推演", this);

    formLayout->addWidget(codeLabel, 0, 0);
    formLayout->addWidget(m_codeEdit, 0, 1);
    formLayout->addWidget(startLabel, 0, 2);
    formLayout->addWidget(m_startDateEdit, 0, 3);
    formLayout->addWidget(endLabel, 0, 4);
    formLayout->addWidget(m_endDateEdit, 0, 5);
    formLayout->addWidget(m_runButton, 0, 6);

    m_summaryLabel = new QLabel("请输入代码与日期范围后开始推演。", this);

    m_chartView = new QtCharts::QChartView(this);
    m_chartView->setRenderHint(QPainter::Antialiasing);

    topLayout->addLayout(formLayout);
    topLayout->addWidget(m_summaryLabel);
    topLayout->addWidget(m_chartView);

    m_nam = new QNetworkAccessManager(this);

    connect(m_runButton, &QPushButton::clicked, this, &BacktestWidget::startRequest);
}

void BacktestWidget::setBusy(bool busy)
{
    m_runButton->setEnabled(!busy);
}

void BacktestWidget::startRequest()
{
    const QString code = m_codeEdit->text().trimmed();
    if (code.isEmpty()) {
        QMessageBox::warning(this, "推演", "请输入股票代码");
        return;
    }
    const QDate startDate = m_startDateEdit->date();
    const QDate endDate = m_endDateEdit->date();
    if (startDate > endDate) {
        QMessageBox::warning(this, "推演", "开始日期不能晚于结束日期");
        return;
    }

    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    setBusy(true);
    m_summaryLabel->setText(QString("正在拉取 %1 的K线数据...").arg(code));
    const QList<int> markets = marketsForCode(code);
    requestKline(code, startDate, endDate, markets, 0);
}

void BacktestWidget::requestKline(const QString& code, const QDate& startDate, const QDate& endDate,
                                  const QList<int>& markets, int marketIndex)
{
    if (marketIndex >= markets.size()) {
        setBusy(false);
        m_summaryLabel->setText("无法获取K线数据，请检查代码是否正确。");
        return;
    }

    const QString secid = QString("%1.%2").arg(markets[marketIndex]).arg(code);
    const QDate fetchStart = startDate.addDays(-20);

    QUrl url("https://push2his.eastmoney.com/api/qt/stock/kline/get");
    QUrlQuery q;
    q.addQueryItem("secid", secid);
    q.addQueryItem("klt", "101");
    q.addQueryItem("fqt", "0");
    q.addQueryItem("beg", fetchStart.toString("yyyyMMdd"));
    q.addQueryItem("end", endDate.toString("yyyyMMdd"));
    q.addQueryItem("lmt", "500");
    q.addQueryItem("rtntype", "6");
    q.addQueryItem("ut", kEM_UT);
    q.addQueryItem("fields1", "f1,f2,f3,f4");
    q.addQueryItem("fields2", "f51,f52,f53,f54,f55");
    url.setQuery(q);

    QNetworkRequest req(url);
    FillCommonHeaders(req);

    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this, [this, code, startDate, endDate, markets, marketIndex]() {
        const QByteArray raw = m_reply->readAll();
        const auto err = m_reply->error();
        m_reply->deleteLater();
        m_reply = nullptr;

        QVector<QString> dates;
        QVector<double> opens;
        QVector<double> closes;
        QVector<double> highs;
        QVector<double> lows;

        auto parseOk = [&]() -> bool {
            if (err != QNetworkReply::NoError) return false;
            auto doc = QJsonDocument::fromJson(raw);
            if (!doc.isObject()) return false;
            const auto root = doc.object();
            const auto dataVal = root.value("data");
            if (!dataVal.isObject()) return false;
            const auto data = dataVal.toObject();
            const auto kl = data.value("klines");
            if (!kl.isArray()) return false;
            const auto arr = kl.toArray();
            if (arr.isEmpty()) return false;
            for (auto v : arr) {
                const auto s = v.toString();
                const auto parts = s.split(',');
                if (parts.size() < 5) continue;
                dates.push_back(parts[0]);
                opens.push_back(parts[1].toDouble());
                closes.push_back(parts[2].toDouble());
                highs.push_back(parts[3].toDouble());
                lows.push_back(parts[4].toDouble());
            }
            return !dates.isEmpty();
        }();

        if (!parseOk) {
            requestKline(code, startDate, endDate, markets, marketIndex + 1);
            return;
        }

        renderBacktest(code, startDate, endDate, dates, opens, closes, highs, lows);
    });
}

void BacktestWidget::renderBacktest(const QString& code, const QDate& startDate, const QDate& endDate,
                                    const QVector<QString>& dates, const QVector<double>& opens,
                                    const QVector<double>& closes, const QVector<double>& highs,
                                    const QVector<double>& lows)
{
    setBusy(false);
    if (dates.size() != closes.size() || closes.size() != opens.size() || closes.size() != highs.size()
        || closes.size() != lows.size()) {
        m_summaryLabel->setText("K线数据格式异常。");
        return;
    }

    QVector<QDate> barDates;
    barDates.reserve(dates.size());
    for (const auto& s : dates) {
        barDates.push_back(QDate::fromString(s, "yyyy-MM-dd"));
    }

    const int n = closes.size();
    QVector<double> ma5(n, std::numeric_limits<double>::quiet_NaN());
    QVector<double> ma10(n, std::numeric_limits<double>::quiet_NaN());
    for (int i = 0; i < n; ++i) {
        if (i >= 4) {
            double sum = 0;
            for (int j = i - 4; j <= i; ++j) sum += closes[j];
            ma5[i] = sum / 5.0;
        }
        if (i >= 9) {
            double sum = 0;
            for (int j = i - 9; j <= i; ++j) sum += closes[j];
            ma10[i] = sum / 10.0;
        }
    }

    int startIndex = -1;
    int endIndex = -1;
    for (int i = 0; i < n; ++i) {
        if (barDates[i].isValid() && barDates[i] >= startDate && startIndex < 0) startIndex = i;
        if (barDates[i].isValid() && barDates[i] <= endDate) endIndex = i;
    }
    if (startIndex < 0 || endIndex < startIndex) {
        m_summaryLabel->setText("指定日期范围内没有数据。");
        return;
    }

    bool inPosition = false;
    double entry = 0.0;
    double equity = 1.0;
    int tradeCount = 0;
    QVector<QPointF> buyPoints;
    QVector<QPointF> sellPoints;

    for (int i = startIndex; i <= endIndex; ++i) {
        if (i < 4) continue;
        bool prev3Below = true;
        if (i < 3) {
            prev3Below = false;
        } else {
            for (int j = i - 3; j <= i - 1; ++j) {
                if (j < 4 || std::isnan(ma5[j]) || closes[j] >= ma5[j]) {
                    prev3Below = false;
                    break;
                }
            }
        }

        const bool ma5Rising = (i > 0 && !std::isnan(ma5[i]) && !std::isnan(ma5[i - 1]) && ma5[i] > ma5[i - 1]);
        if (!inPosition && prev3Below && closes[i] > ma5[i] && ma5Rising) {
            inPosition = true;
            entry = closes[i];
            const qint64 ts = QDateTime(barDates[i]).toMSecsSinceEpoch();
            buyPoints.push_back(QPointF(ts, closes[i]));
        } else if (inPosition && closes[i] < ma5[i]) {
            inPosition = false;
            equity *= closes[i] / entry;
            ++tradeCount;
            const qint64 ts = QDateTime(barDates[i]).toMSecsSinceEpoch();
            sellPoints.push_back(QPointF(ts, closes[i]));
        }
    }

    if (inPosition) {
        equity *= closes[endIndex] / entry;
        ++tradeCount;
        const qint64 ts = QDateTime(barDates[endIndex]).toMSecsSinceEpoch();
        sellPoints.push_back(QPointF(ts, closes[endIndex]));
    }

    const double pct = (equity - 1.0) * 100.0;
    m_summaryLabel->setText(QString("%1 推演完成：交易 %2 次，收益率 %3%")
                            .arg(code)
                            .arg(tradeCount)
                            .arg(QString::number(pct, 'f', 2)));

    auto* chart = new QtCharts::QChart();
    chart->setTitle(QString("%1 K线推演").arg(code));

    auto* candleSeries = new QtCharts::QCandlestickSeries();
    candleSeries->setIncreasingColor(QColor("#d32f2f"));
    candleSeries->setDecreasingColor(QColor("#2e7d32"));

    auto* ma5Series = new QtCharts::QLineSeries();
    ma5Series->setName("MA5");
    ma5Series->setColor(QColor("#ff9800"));

    auto* ma10Series = new QtCharts::QLineSeries();
    ma10Series->setName("MA10");
    ma10Series->setColor(QColor("#1976d2"));

    auto* buySeries = new QtCharts::QScatterSeries();
    buySeries->setName("买点");
    buySeries->setColor(QColor("#2e7d32"));
    buySeries->setMarkerSize(8.0);

    auto* sellSeries = new QtCharts::QScatterSeries();
    sellSeries->setName("卖点");
    sellSeries->setColor(QColor("#d32f2f"));
    sellSeries->setMarkerSize(8.0);

    double minPrice = std::numeric_limits<double>::max();
    double maxPrice = std::numeric_limits<double>::lowest();

    for (int i = startIndex; i <= endIndex; ++i) {
        if (!barDates[i].isValid()) continue;
        const qint64 ts = QDateTime(barDates[i]).toMSecsSinceEpoch();
        auto* set = new QtCharts::QCandlestickSet(opens[i], highs[i], lows[i], closes[i], ts);
        candleSeries->append(set);
        minPrice = std::min(minPrice, lows[i]);
        maxPrice = std::max(maxPrice, highs[i]);
        if (!std::isnan(ma5[i])) ma5Series->append(ts, ma5[i]);
        if (!std::isnan(ma10[i])) ma10Series->append(ts, ma10[i]);
    }

    for (const auto& p : buyPoints) buySeries->append(p);
    for (const auto& p : sellPoints) sellSeries->append(p);

    chart->addSeries(candleSeries);
    chart->addSeries(ma5Series);
    chart->addSeries(ma10Series);
    chart->addSeries(buySeries);
    chart->addSeries(sellSeries);

    auto* axisX = new QtCharts::QDateTimeAxis;
    axisX->setFormat("yyyy-MM-dd");
    axisX->setTickCount(8);
    axisX->setRange(QDateTime(barDates[startIndex]), QDateTime(barDates[endIndex]));
    chart->addAxis(axisX, Qt::AlignBottom);
    candleSeries->attachAxis(axisX);
    ma5Series->attachAxis(axisX);
    ma10Series->attachAxis(axisX);
    buySeries->attachAxis(axisX);
    sellSeries->attachAxis(axisX);

    auto* axisY = new QtCharts::QValueAxis;
    axisY->setLabelFormat("%.2f");
    if (minPrice < maxPrice) {
        const double padding = (maxPrice - minPrice) * 0.05;
        axisY->setRange(minPrice - padding, maxPrice + padding);
    }
    chart->addAxis(axisY, Qt::AlignLeft);
    candleSeries->attachAxis(axisY);
    ma5Series->attachAxis(axisY);
    ma10Series->attachAxis(axisY);
    buySeries->attachAxis(axisY);
    sellSeries->attachAxis(axisY);

    chart->legend()->setAlignment(Qt::AlignTop);

    m_chartView->setChart(chart);
}
