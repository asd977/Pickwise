#pragma once
#include "QuoteModel.h"

#include <QObject>
#include <QVector>
#include <QQueue>
#include <QHash>
#include <QDate>

class QNetworkAccessManager;
class QNetworkReply;

struct Spot {
    QString code;
    QString name;
    int market = 0;     // f13
    double last = 0;    // f2
};

struct ScanConfig {
    int belowDays = 3;
    bool includeBJ = true;
    int pageSize = 200;
    int maxInFlight = 12;
    int timeoutMs = 12000;
    int maxRetries = 2;
    bool sortByAbs = false;
    bool sortDesc = true;
};

struct KlineStats {
    bool ok = false;
    double ma5Last = 0;
    bool lastNDaysCloseBelowMA5 = false;
};

class Ma5Scanner : public QObject
{
    Q_OBJECT
public:
    explicit Ma5Scanner(QObject* parent=nullptr);

    void runOnce(const ScanConfig& cfg);
    void cancel();

signals:
    void stageChanged(const QString& text);
    void progress(int done, int total);
    void finished(QVector<PickRow> rows);
    void failed(const QString& reason);
    void cancelled();

private:
    // step1: fetch all spots
    void fetchSpotPage(int pn);
    bool parseSpotPage(const QByteArray& body, QVector<Spot>& outPage, int* totalOut);

    // step2: kline queue
    void startKlineQueue();
    void pumpKline();

    // kline task
    struct Task {
        Spot s;
        int retry = 0;
        QList<int> marketTryList;
        int marketTryIndex = 0;
        QString secidUsed; // 本次请求实际用的 secid
    };

    void requestKlineInitial(const Spot& s);   // 入队用：创建 Task
    void sendKlineTask(Task t);                // 真正发请求：保持 Task 状态续跑

    static QByteArray normalizeJsonMaybeJsonp(const QByteArray& body);
    static bool parseKlineBars(const QByteArray& body, QVector<QString>& dates, QVector<double>& closes);
    static bool computeStatsFromBars(const QVector<QString>& dates, const QVector<double>& closes, int belowDays, KlineStats& out);

    // cache
    void loadCache();
    void saveCache();
    QString cachePath() const;
    bool cacheGet(const QString& secid, QVector<QString>& dates, QVector<double>& closes) const;
    void cachePut(const QString& secid, const QVector<QString>& dates, const QVector<double>& closes);

    // secid helpers
    QString secidFor(const Spot& s, int marketOverride = -1) const;
    QList<int> fallbackMarketsFor(const Spot& s) const;

private:
    QNetworkAccessManager* m_nam = nullptr;

    ScanConfig m_cfg;
    bool m_cancelled = false;
    bool m_cancelSignalSent = false;

    QVector<Spot> m_spots;
    int m_spotTotal = 0;

    QQueue<Spot> m_queue;
    int m_totalToDo = 0;   // 固定总数
    int m_done = 0;
    int m_inFlight = 0;

    QHash<QNetworkReply*, Task> m_tasks;
    QVector<PickRow> m_results;

    // file cache: secid -> bars
    QDate m_cacheDate;
    struct CacheItem { QVector<QString> dates; QVector<double> closes; };
    QHash<QString, CacheItem> m_cache;
};
