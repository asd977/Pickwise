#pragma once

#include <QMainWindow>
#include "Ma5Scanner.h"
#include "QuoteModel.h"

class BacktestWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    void setUiBusy(bool busy);
    void exportCsv(const QuoteModel* model);
    void updateProgress(int done, int total, ScanConfig::Mode mode);
    void updateStage(const QString& text, ScanConfig::Mode mode);

private:
    Ui::MainWindow *ui;
    Ma5Scanner* m_scanner = nullptr;
    QuoteModel* m_model = nullptr;
    QuoteModel* m_pullbackModel = nullptr;
    BacktestWidget* m_backtestWidget = nullptr;
    ScanConfig::Mode m_activeMode = ScanConfig::Mode::BreakAboveMa5;
};
