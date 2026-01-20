#pragma once

#include <QMainWindow>
#include "Ma5Scanner.h"
#include "MaPullbackScanner.h"
#include "QuoteModel.h"
#include "PullbackModel.h"

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
    void setPullbackUiBusy(bool busy);

private:
    Ui::MainWindow *ui;
    Ma5Scanner* m_scanner = nullptr;
    QuoteModel* m_model = nullptr;
    MaPullbackScanner* m_pullbackScanner = nullptr;
    PullbackModel* m_pullbackModel = nullptr;
    BacktestWidget* m_backtestWidget = nullptr;
};
