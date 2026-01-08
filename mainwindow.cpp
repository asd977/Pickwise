#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_scanner = new Ma5Scanner(this);
    m_model = new QuoteModel(this);

    ui->tableView->setModel(m_model);
    ui->tableView->setSortingEnabled(true);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    ui->listMenu->setCurrentRow(0);
    ui->stackedWidget->setCurrentIndex(0);

    setUiBusy(false);

    connect(ui->listMenu, &QListWidget::currentRowChanged, this, [this](int row){
        ui->stackedWidget->setCurrentIndex(row);
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
        ts.setCodec("UTF-8");
        ts << "code,name,sector,pe,market,last,ma5,biasPct,belowDays\n";
        for (const auto& r : m_model->rows()) {
            ts << r.code << "," << r.name << "," << r.sector << "," << r.pe << ","
               << r.market << "," << r.last << "," << r.ma5 << "," << r.biasPct << "," << r.belowDays << "\n";
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
