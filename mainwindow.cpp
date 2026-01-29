#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "BacktestWidget.h"
#include "KlineDialog.h"
#include "KlineButtonDelegate.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QVBoxLayout>
#include <QVariant>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_scanner = new Ma5Scanner(this);
    m_model = new QuoteModel(this);
    m_pullbackModel = new QuoteModel(this);
    m_model->setDaysHeaderLabel("N(收<MA5)");
    m_pullbackModel->setDaysHeaderLabel("N(收>MA5)");

    struct ApiProvider {
        QString name;
        ScanConfig::Provider provider;
        QString spotUrl;
        QString klineUrl;
    };
    const QVector<ApiProvider> providers = {
        {"东财主站(82)", ScanConfig::Provider::Eastmoney, "https://82.push2.eastmoney.com/api/qt/clist/get", "https://push2his.eastmoney.com/api/qt/stock/kline/get"},
        {"东财镜像(83)", ScanConfig::Provider::Eastmoney, "https://83.push2.eastmoney.com/api/qt/clist/get", "https://push2his.eastmoney.com/api/qt/stock/kline/get"},
        {"东财镜像(84)", ScanConfig::Provider::Eastmoney, "https://84.push2.eastmoney.com/api/qt/clist/get", "https://push2his.eastmoney.com/api/qt/stock/kline/get"},
        {"东财备用(push2)", ScanConfig::Provider::Eastmoney, "https://push2.eastmoney.com/api/qt/clist/get", "https://push2.eastmoney.com/api/qt/stock/kline/get"},
        {"AkShare(免费)", ScanConfig::Provider::AkShare,
         "https://api.akshare.xyz/stock_zh_a_spot_em|https://api.akshare.xyz/api/public/stock_zh_a_spot_em|https://akshare.akfamily.xyz/api/public/stock_zh_a_spot_em",
         "https://api.akshare.xyz/stock_zh_a_hist|https://api.akshare.xyz/api/public/stock_zh_a_hist|https://akshare.akfamily.xyz/api/public/stock_zh_a_hist"},
        {"新浪财经(HTTPS)", ScanConfig::Provider::Sina, "https://money.finance.sina.com.cn/quotes_service/api/jsonp_v2.php/IO.XSRV2.CallbackList/Market_Center.getHQNodeData", "https://money.finance.sina.com.cn/quotes_service/api/json_v2.php/CN_MarketData.getKLineData"},
        {"新浪财经(HTTP)", ScanConfig::Provider::Sina, "http://money.finance.sina.com.cn/quotes_service/api/jsonp_v2.php/IO.XSRV2.CallbackList/Market_Center.getHQNodeData", "http://money.finance.sina.com.cn/quotes_service/api/json_v2.php/CN_MarketData.getKLineData"},
        {"新浪财经(HTTPS+移动K线)", ScanConfig::Provider::Sina, "https://money.finance.sina.com.cn/quotes_service/api/jsonp_v2.php/IO.XSRV2.CallbackList/Market_Center.getHQNodeData", "https://quotes.sina.cn/cn/api/json_v2.php/CN_MarketData.getKLineData"},
        {"新浪财经(HTTP+移动K线)", ScanConfig::Provider::Sina, "http://money.finance.sina.com.cn/quotes_service/api/jsonp_v2.php/IO.XSRV2.CallbackList/Market_Center.getHQNodeData", "http://quotes.sina.cn/cn/api/json_v2.php/CN_MarketData.getKLineData"}
    };
    for (const auto& p : providers) {
        QVariantMap payload;
        payload.insert("provider", static_cast<int>(p.provider));
        payload.insert("spot", p.spotUrl);
        payload.insert("kline", p.klineUrl);
        ui->comboApiProvider->addItem(p.name, payload);
        ui->comboPullbackApiProvider->addItem(p.name, payload);
    }

    auto* chartLayout = new QVBoxLayout(ui->backtestHost);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    m_backtestWidget = new BacktestWidget(ui->backtestHost);
    chartLayout->addWidget(m_backtestWidget);

    ui->tableView->setModel(m_model);
    ui->tableView->setSortingEnabled(true);
    auto* klineDelegate = new KlineButtonDelegate(this);
    ui->tableView->setItemDelegateForColumn(9, klineDelegate);
    ui->tableViewPullback->setModel(m_pullbackModel);
    ui->tableViewPullback->setSortingEnabled(true);
    auto* pullbackDelegate = new KlineButtonDelegate(this);
    ui->tableViewPullback->setItemDelegateForColumn(9, pullbackDelegate);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    ui->progressBarPullback->setRange(0, 100);
    ui->progressBarPullback->setValue(0);
    ui->listMenu->setCurrentRow(0);
    ui->stackedWidget->setCurrentIndex(0);

    setUiBusy(false);

    connect(ui->listMenu, &QListWidget::currentRowChanged, this, [this](int row){
        ui->stackedWidget->setCurrentIndex(row);
    });

    connect(klineDelegate, &KlineButtonDelegate::klineClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        const auto& rows = m_model->rows();
        if (index.row() < 0 || index.row() >= rows.size()) return;
        const auto& row = rows[index.row()];
        auto* dialog = new KlineDialog(row.code, row.market, row.name, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        dialog->show();
    });
    connect(pullbackDelegate, &KlineButtonDelegate::klineClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        const auto& rows = m_pullbackModel->rows();
        if (index.row() < 0 || index.row() >= rows.size()) return;
        const auto& row = rows[index.row()];
        auto* dialog = new KlineDialog(row.code, row.market, row.name, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        dialog->show();
    });

    connect(ui->btnScan, &QPushButton::clicked, this, [this](){
        ScanConfig cfg;
        cfg.belowDays = ui->spinBelowDays->value();
        cfg.includeBJ = ui->cbIncludeBJ->isChecked();
        cfg.requireMa5SlopeUp = ui->cbMa5SlopeUp->isChecked();
        const auto apiData = ui->comboApiProvider->currentData().toMap();
        if (!apiData.isEmpty()) {
            cfg.provider = static_cast<ScanConfig::Provider>(apiData.value("provider").toInt());
            cfg.spotBaseUrl = apiData.value("spot").toString();
            cfg.klineBaseUrl = apiData.value("kline").toString();
        }
//        cfg.excludeST = ui->cbExcludeST->isChecked();
        cfg.pageSize = ui->spinPageSize->value();
        cfg.maxInFlight = ui->spinInFlight->value();
        cfg.timeoutMs = ui->spinTimeout->value();
        cfg.maxRetries = ui->spinRetry->value();
        cfg.sortField = ui->comboSortField->currentIndex();
        cfg.sortDesc = ui->cbSortDesc->isChecked();
        cfg.mode = ScanConfig::Mode::BreakAboveMa5;

        m_activeMode = ScanConfig::Mode::BreakAboveMa5;
        m_model->setRows({});
        updateStage("启动扫描...", m_activeMode);
        setUiBusy(true);
        m_scanner->runOnce(cfg);
    });

    connect(ui->btnCancel, &QPushButton::clicked, this, [this](){
        m_scanner->cancel();
    });

    connect(ui->btnPullbackScan, &QPushButton::clicked, this, [this](){
        ScanConfig cfg;
        cfg.belowDays = ui->spinBelowDays->value();
        cfg.pullbackAboveDays = ui->spinPullbackAboveDays->value();
        cfg.pullbackTolerancePct = ui->spinPullbackTolerance->value();
        cfg.includeBJ = ui->cbPullbackIncludeBJ->isChecked();
        cfg.requireMa5SlopeUp = ui->cbPullbackSlopeUp->isChecked();
        const auto apiData = ui->comboPullbackApiProvider->currentData().toMap();
        if (!apiData.isEmpty()) {
            cfg.provider = static_cast<ScanConfig::Provider>(apiData.value("provider").toInt());
            cfg.spotBaseUrl = apiData.value("spot").toString();
            cfg.klineBaseUrl = apiData.value("kline").toString();
        }
        cfg.pageSize = ui->spinPullbackPageSize->value();
        cfg.maxInFlight = ui->spinPullbackInFlight->value();
        cfg.timeoutMs = ui->spinPullbackTimeout->value();
        cfg.maxRetries = ui->spinPullbackRetry->value();
        cfg.sortField = ui->comboPullbackSortField->currentIndex();
        cfg.sortDesc = ui->cbPullbackSortDesc->isChecked();
        cfg.mode = ScanConfig::Mode::PullbackToMa5;

        m_activeMode = ScanConfig::Mode::PullbackToMa5;
        m_pullbackModel->setRows({});
        updateStage("启动扫描...", m_activeMode);
        setUiBusy(true);
        m_scanner->runOnce(cfg);
    });

    connect(ui->btnPullbackCancel, &QPushButton::clicked, this, [this](){
        m_scanner->cancel();
    });

    connect(ui->btnTest, &QPushButton::clicked, this, [this](){
        updateStage("测试接口中...", ScanConfig::Mode::BreakAboveMa5);
//        m_scanner->testApis(ui->editTestCode->text());
    });

    connect(ui->btnExport, &QPushButton::clicked, this, [this](){
        exportCsv(m_model);
    });

    connect(ui->btnPullbackExport, &QPushButton::clicked, this, [this](){
        exportCsv(m_pullbackModel);
    });

    // scanner signals
    connect(m_scanner, &Ma5Scanner::stageChanged, this, [this](const QString& s){
        updateStage(s, m_activeMode);
    });

    connect(m_scanner, &Ma5Scanner::progress, this, [this](int done, int total){
        updateProgress(done, total, m_activeMode);
    });

    connect(m_scanner, &Ma5Scanner::finished, this, [this](QVector<PickRow> rows){
        if (m_activeMode == ScanConfig::Mode::BreakAboveMa5) {
            m_model->setRows(rows);
        } else {
            m_pullbackModel->setRows(rows);
        }
        setUiBusy(false);
    });

    connect(m_scanner, &Ma5Scanner::failed, this, [this](const QString& e){
        setUiBusy(false);
        QMessageBox::warning(this, "扫描失败", e);
    });

    connect(m_scanner, &Ma5Scanner::cancelled, this, [this](){
        setUiBusy(false);
        updateStage("已取消", m_activeMode);
    });

//    connect(m_scanner, &Ma5Scanner::testFinished, this, [this](const QString& rep){
//        ui->labelStage->setText("测试完成");
//        QMessageBox::information(this, "接口测试结果", rep);
//    });
//    connect(m_scanner, &Ma5Scanner::testFailed, this, [this](const QString& e){
//        ui->labelStage->setText("测试失败");
//        QMessageBox::warning(this, "接口测试失败", e);
//    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::exportCsv(const QuoteModel* model)
{
    if (!model || model->rowCount() == 0) {
        QMessageBox::information(this, "导出", "暂无数据可导出");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "导出CSV", "", "CSV (*.csv)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, "导出", "无法写入文件");
        return;
    }
    QTextStream ts(&f);
    ts << "code,name,sector,pe,market,last,ma5,biasPct,belowDays\n";
    for (const auto& r : model->rows()) {
        ts << r.code << "," << r.name << "," << r.sector << "," << r.pe << ","
           << r.market << "," << r.last << "," << r.ma5 << "," << r.biasPct << "," << r.belowDays << "\n";
    }
    f.close();
    QMessageBox::information(this, "导出", "导出完成");
}

void MainWindow::updateProgress(int done, int total, ScanConfig::Mode mode)
{
    QProgressBar* bar = (mode == ScanConfig::Mode::BreakAboveMa5) ? ui->progressBar : ui->progressBarPullback;
    QLabel* label = (mode == ScanConfig::Mode::BreakAboveMa5) ? ui->labelProgress : ui->labelPullbackProgress;
    if (total <= 0) {
        bar->setRange(0, 0);
        return;
    }
    bar->setRange(0, total);
    bar->setValue(done);
    label->setText(QString("%1 / %2").arg(done).arg(total));
}

void MainWindow::updateStage(const QString& text, ScanConfig::Mode mode)
{
    QLabel* label = (mode == ScanConfig::Mode::BreakAboveMa5) ? ui->labelStage : ui->labelPullbackStage;
    label->setText(text);
}

void MainWindow::setUiBusy(bool busy)
{
    ui->btnScan->setEnabled(!busy);
    ui->btnCancel->setEnabled(busy);
    ui->btnExport->setEnabled(!busy && m_model->rowCount() > 0);
    ui->btnPullbackScan->setEnabled(!busy);
    ui->btnPullbackCancel->setEnabled(busy);
    ui->btnPullbackExport->setEnabled(!busy && m_pullbackModel->rowCount() > 0);
}
