#pragma once
#include "PullbackModel.h"

#include <QObject>
#include <QVector>
#include <QQueue>
#include <QHash>
#include <QDate>

class QNetworkAccessManager;
class QNetworkReply;

struct PullbackSpot {
    QString code;
    QString name;
    QString sector;
    double pe = 0;
    int market = 0;     // f13
    double last = 0;    // f2
};

struct PullbackScanConfig {
    int belowDays = 3;
    int maPeriod = 5;
    bool includeBJ = true;
    int pageSize = 200;
    int maxInFlight = 12;
    int timeoutMs = 12000;
    int maxRetries = 2;
    int sortField = 0;
    bool sortDesc = true;
};

struct PullbackKlineStats {
    bool ok = false;
    double maLast = 0;
    bool lastNDaysCloseBelowMA = false;
};

class MaPullbackScanner : public QObject
{
    Q_OBJECT
public:
    explicit MaPullbackScanner(QObject* parent=nullptr);

    void runOnce(const PullbackScanConfig& cfg);
    void cancel();

signals:
    void stageChanged(const QString& text);
    void progress(int done, int total);
    void finished(QVector<PullbackRow> rows);
    void failed(const QString& reason);
    void cancelled();

private:
    void fetchSpotPage(int pn);
    bool parseSpotPage(const QByteArray& body, QVector<PullbackSpot>& outPage, int* totalOut);

    void startKlineQueue();
    void pumpKline();

    struct Task {
        PullbackSpot s;
        int retry = 0;
        QList<int> marketTryList;
        int marketTryIndex = 0;
        QString secidUsed;
    };

    void requestKlineInitial(const PullbackSpot& s);
    void sendKlineTask(Task t);

    static QByteArray normalizeJsonMaybeJsonp(const QByteArray& body);
    static bool parseKlineBars(const QByteArray& body, QVector<QString>& dates, QVector<double>& closes);
    static bool computeStatsFromBars(const QVector<QString>& dates, const QVector<double>& closes, int maPeriod, int belowDays, PullbackKlineStats& out);

    void loadCache();
    void saveCache();
    QString cachePath() const;
    bool cacheGet(const QString& secid, QVector<QString>& dates, QVector<double>& closes) const;
    void cachePut(const QString& secid, const QVector<QString>& dates, const QVector<double>& closes);

    QString secidFor(const PullbackSpot& s, int marketOverride = -1) const;
    QList<int> fallbackMarketsFor(const PullbackSpot& s) const;

private:
    QNetworkAccessManager* m_nam = nullptr;

    PullbackScanConfig m_cfg;
    bool m_cancelled = false;
    bool m_cancelSignalSent = false;

    QVector<PullbackSpot> m_spots;
    int m_spotTotal = 0;

    QQueue<PullbackSpot> m_queue;
    int m_totalToDo = 0;
    int m_done = 0;
    int m_inFlight = 0;

    QHash<QNetworkReply*, Task> m_tasks;
    QVector<PullbackRow> m_results;

    QDate m_cacheDate;
    struct CacheItem { QVector<QString> dates; QVector<double> closes; };
    QHash<QString, CacheItem> m_cache;
};
