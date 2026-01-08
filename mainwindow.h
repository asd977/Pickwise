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

private:
    Ui::MainWindow *ui;
    Ma5Scanner* m_scanner = nullptr;
    QuoteModel* m_model = nullptr;
    BacktestWidget* m_backtestWidget = nullptr;
};
