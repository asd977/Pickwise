#include <QApplication>
#include "mainwindow.h"
#include <QApplication>
#include <QLoggingCategory>

int main(int argc, char *argv[])
{
    // 关掉 qt.network.monitor 的 warning 噪音（不影响功能）
    QLoggingCategory::setFilterRules(
        QStringLiteral("qt.network.monitor.warning=false\n"
                       "qt.network.monitor.info=false\n"
                       "qt.network.monitor.debug=false\n")
    );

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
