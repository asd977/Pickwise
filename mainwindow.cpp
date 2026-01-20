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


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_scanner = new Ma5Scanner(this);
    m_model = new QuoteModel(this);
    m_pullbackScanner = new MaPullbackScanner(this);
    m_pullbackModel = new PullbackModel(this);

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
    auto* pullbackKlineDelegate = new KlineButtonDelegate(this);
    ui->tableViewPullback->setItemDelegateForColumn(10, pullbackKlineDelegate);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    ui->progressBarPullback->setRange(0, 100);
    ui->progressBarPullback->setValue(0);
    ui->listMenu->setCurrentRow(0);
    ui->stackedWidget->setCurrentIndex(0);

    setUiBusy(false);
    setPullbackUiBusy(false);

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
    connect(pullbackKlineDelegate, &KlineButtonDelegate::klineClicked, this, [this](const QModelIndex& index) {
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
//        cfg.excludeST = ui->cbExcludeST->isChecked();
        cfg.pageSize = ui->spinPageSize->value();
        cfg.maxInFlight = ui->spinInFlight->value();
        cfg.timeoutMs = ui->spinTimeout->value();
        cfg.maxRetries = ui->spinRetry->value();
        cfg.sortField = ui->comboSortField->currentIndex();
        cfg.sortDesc = ui->cbSortDesc->isChecked();

        m_model->setRows({});
        ui->labelStage->setText("启动扫描...");
        setUiBusy(true);
        m_scanner->runOnce(cfg);
    });

    connect(ui->btnCancel, &QPushButton::clicked, this, [this](){
        m_scanner->cancel();
    });

    connect(ui->btnTest, &QPushButton::clicked, this, [this](){
        ui->labelStage->setText("测试接口中...");
//        m_scanner->testApis(ui->editTestCode->text());
    });

    connect(ui->btnExport, &QPushButton::clicked, this, [this](){
        const QString path = QFileDialog::getSaveFileName(this, "导出CSV", "", "CSV (*.csv)");
        if (path.isEmpty()) return;

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QMessageBox::warning(this, "导出", "无法写入文件");
            return;
        }
        QTextStream ts(&f);
        ts << "code,name,sector,pe,market,last,ma5,biasPct,belowDays\n";
        for (const auto& r : m_model->rows()) {
            ts << r.code << "," << r.name << "," << r.sector << "," << r.pe << ","
               << r.market << "," << r.last << "," << r.ma5 << "," << r.biasPct << "," << r.belowDays << "\n";
        }
        f.close();
        QMessageBox::information(this, "导出", "导出完成");
    });

    connect(ui->btnScanPullback, &QPushButton::clicked, this, [this](){
        PullbackScanConfig cfg;
        cfg.belowDays = ui->spinBelowDaysPullback->value();
        cfg.maPeriod = ui->comboMaPeriodPullback->currentText().toInt();
        cfg.conditionMode = ui->comboConditionPullback->currentIndex();
        cfg.includeBJ = ui->cbIncludeBJPullback->isChecked();
//        cfg.excludeST = ui->cbExcludeSTPullback->isChecked();
        cfg.pageSize = ui->spinPageSizePullback->value();
        cfg.maxInFlight = ui->spinInFlightPullback->value();
        cfg.timeoutMs = ui->spinTimeoutPullback->value();
        cfg.maxRetries = ui->spinRetryPullback->value();
        cfg.sortField = ui->comboSortFieldPullback->currentIndex();
        cfg.sortDesc = ui->cbSortDescPullback->isChecked();

        m_pullbackModel->setRows({});
        ui->labelStagePullback->setText("启动扫描...");
        setPullbackUiBusy(true);
        m_pullbackScanner->runOnce(cfg);
    });

    connect(ui->btnCancelPullback, &QPushButton::clicked, this, [this](){
        m_pullbackScanner->cancel();
    });

    connect(ui->btnExportPullback, &QPushButton::clicked, this, [this](){
        const QString path = QFileDialog::getSaveFileName(this, "导出CSV", "", "CSV (*.csv)");
        if (path.isEmpty()) return;

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QMessageBox::warning(this, "导出", "无法写入文件");
            return;
        }
        QTextStream ts(&f);
        ts << "code,name,sector,pe,market,last,maPeriod,maValue,biasPct,belowDays\n";
        for (const auto& r : m_pullbackModel->rows()) {
            ts << r.code << "," << r.name << "," << r.sector << "," << r.pe << ","
               << r.market << "," << r.last << "," << r.maPeriod << "," << r.maValue << ","
               << r.biasPct << "," << r.belowDays << "\n";
        }
        f.close();
        QMessageBox::information(this, "导出", "导出完成");
    });

    // scanner signals
    connect(m_scanner, &Ma5Scanner::stageChanged, this, [this](const QString& s){
        ui->labelStage->setText(s);
    });

    connect(m_scanner, &Ma5Scanner::progress, this, [this](int done, int total){
        if (total <= 0) {
            ui->progressBar->setRange(0, 0); // busy
            return;
        }
        ui->progressBar->setRange(0, total);
        ui->progressBar->setValue(done);
        ui->labelProgress->setText(QString("%1 / %2").arg(done).arg(total));
    });

    connect(m_scanner, &Ma5Scanner::finished, this, [this](QVector<PickRow> rows){
        m_model->setRows(rows);
        setUiBusy(false);
    });

    connect(m_scanner, &Ma5Scanner::failed, this, [this](const QString& e){
        setUiBusy(false);
        QMessageBox::warning(this, "扫描失败", e);
    });

    connect(m_scanner, &Ma5Scanner::cancelled, this, [this](){
        setUiBusy(false);
        ui->labelStage->setText("已取消");
    });

    connect(m_pullbackScanner, &MaPullbackScanner::stageChanged, this, [this](const QString& s){
        ui->labelStagePullback->setText(s);
    });

    connect(m_pullbackScanner, &MaPullbackScanner::progress, this, [this](int done, int total){
        if (total <= 0) {
            ui->progressBarPullback->setRange(0, 0);
            return;
        }
        ui->progressBarPullback->setRange(0, total);
        ui->progressBarPullback->setValue(done);
        ui->labelProgressPullback->setText(QString("%1 / %2").arg(done).arg(total));
    });

    connect(m_pullbackScanner, &MaPullbackScanner::finished, this, [this](QVector<PullbackRow> rows){
        m_pullbackModel->setRows(rows);
        setPullbackUiBusy(false);
    });

    connect(m_pullbackScanner, &MaPullbackScanner::failed, this, [this](const QString& e){
        setPullbackUiBusy(false);
        QMessageBox::warning(this, "扫描失败", e);
    });

    connect(m_pullbackScanner, &MaPullbackScanner::cancelled, this, [this](){
        setPullbackUiBusy(false);
        ui->labelStagePullback->setText("已取消");
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

void MainWindow::setUiBusy(bool busy)
{
    ui->btnScan->setEnabled(!busy);
    ui->btnCancel->setEnabled(busy);
    ui->btnExport->setEnabled(!busy && m_model->rowCount() > 0);
}

void MainWindow::setPullbackUiBusy(bool busy)
{
    ui->btnScanPullback->setEnabled(!busy);
    ui->btnCancelPullback->setEnabled(busy);
    ui->btnExportPullback->setEnabled(!busy && m_pullbackModel->rowCount() > 0);
}
