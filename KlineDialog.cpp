#include "KlineDialog.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWebEngineCore/QWebEngineSettings>

KlineDialog::KlineDialog(const QString& code, int market, const QString& name, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString("%1 %2").arg(code, name));
    resize(980, 640);
    setMinimumSize(860, 540);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setText(QString("K线 & 指标：%1 %2").arg(code, name));
    m_titleLabel->setStyleSheet("padding: 6px 12px; color: #e0e0e0; background: #121218;");

    auto* infoLabel = new QLabel(this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("padding: 0 12px 8px 12px; color: #b0b0b0; background: #121218;");
    infoLabel->setText(QStringLiteral(
        "关键指标：MA(5/10/20)、MACD、RSI；"
        "<span style=\"color:#2e7d32;\">● 买入</span> "
        "<span style=\"color:#d32f2f;\">● 卖出</span>"));

    layout->addWidget(m_titleLabel);
    layout->addWidget(infoLabel);

    m_view = new QWebEngineView(this);
    m_view->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    layout->addWidget(m_view, 1);

    const QString symbol = toTradingViewSymbol(code.trimmed(), market);
    loadChart(symbol);
}

QString KlineDialog::toTradingViewSymbol(const QString& code, int market) const
{
    if (market == 1 || code.startsWith("6")) {
        return QString("SSE:%1").arg(code);
    }
    if (market == 2 || code.startsWith("8") || code.startsWith("4")
        || code.startsWith("43") || code.startsWith("83")
        || code.startsWith("87") || code.startsWith("88")) {
        return QString("BSE:%1").arg(code);
    }
    return QString("SZSE:%1").arg(code);
}

void KlineDialog::loadChart(const QString& symbol)
{
    const QString html = QString(R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <style>
    html, body { margin: 0; width: 100%%; height: 100%%; background: #101014; overflow: hidden; }
    body { display: flex; }
    #tv_container { flex: 1; min-height: 100%%; }
  </style>
</head>
<body>
  <div id="tv_container"></div>
  <script src="https://s3.tradingview.com/tv.js"></script>
  <script>
    var widget = new TradingView.widget({
      "autosize": true,
      "symbol": "%1",
      "interval": "D",
      "timezone": "Asia/Shanghai",
      "theme": "dark",
      "style": "1",
      "locale": "zh_CN",
      "toolbar_bg": "#101014",
      "enable_publishing": false,
      "allow_symbol_change": true,
      "studies": [
        "MACD@tv-basicstudies",
        "RSI@tv-basicstudies"
      ],
      "container_id": "tv_container",
      "withdateranges": true,
      "hide_side_toolbar": false,
      "details": true,
      "show_popup_button": true,
      "popup_width": "1000",
      "popup_height": "640"
    });
    widget.onChartReady(function() {
      var chart = widget.chart();
      chart.createStudy('Moving Average', false, false, [5]);
      chart.createStudy('Moving Average', false, false, [10]);
      chart.createStudy('Moving Average', false, false, [20]);
    });
  </script>
</body>
</html>
    )").arg(symbol);

    m_view->setHtml(html, QUrl("https://s3.tradingview.com/"));
}
