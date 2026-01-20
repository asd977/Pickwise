#include "MaPullbackScanner.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTimer>
#include <QtMath>
#include <algorithm>

namespace {
static const char* kEM_UT = "fa5fd1943c7b386f172d6893dbfba10b";

static void FillCommonHeaders(QNetworkRequest& req) {
    req.setRawHeader("User-Agent", "Mozilla/5.0");
    req.setRawHeader("Accept", "application/json,text/plain,*/*");
    req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
    req.setRawHeader("Referer", "https://quote.eastmoney.com/");
}
}

MaPullbackScanner::MaPullbackScanner(QObject* parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    loadCache();
}

void MaPullbackScanner::runOnce(const PullbackScanConfig& cfg)
{
    cancel();

    m_cfg = cfg;
    m_cancelled = false;
    m_cancelSignalSent = false;

    m_spots.clear();
    m_queue.clear();
    m_results.clear();
    m_tasks.clear();

    m_done = 0;
    m_inFlight = 0;
    m_totalToDo = 0;
    m_spotTotal = 0;

    loadCache();

    emit stageChanged("拉取沪深京A股列表...");
    fetchSpotPage(1);
}

void MaPullbackScanner::cancel()
{
    m_cancelled = true;
    m_queue.clear();

    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it.key()) it.key()->abort();
    }

    if (!m_cancelSignalSent) {
        m_cancelSignalSent = true;
        emit stageChanged("已取消");
        emit cancelled();
    }
}

QByteArray MaPullbackScanner::normalizeJsonMaybeJsonp(const QByteArray& body)
{
    int l = body.indexOf('{');
    int r = body.lastIndexOf('}');
    if (l >= 0 && r > l) return body.mid(l, r - l + 1);
    return body;
}

void MaPullbackScanner::fetchSpotPage(int pn)
{
    if (m_cancelled) return;

    QUrl url("https://82.push2.eastmoney.com/api/qt/clist/get");
    QUrlQuery q;
    q.addQueryItem("pn", QString::number(pn));
    q.addQueryItem("pz", QString::number(m_cfg.pageSize));
    q.addQueryItem("po", "1");
    q.addQueryItem("np", "2");
    q.addQueryItem("fltt", "2");
    q.addQueryItem("invt", "2");
    q.addQueryItem("fid", "f3");
    q.addQueryItem("ut", kEM_UT);
    q.addQueryItem("fs", "m:0+t:6,m:0+t:80,m:1+t:2,m:1+t:23,m:0+t:81+s:2048");
    q.addQueryItem("fields", "f12,f14,f2,f13,f9,f100");
    url.setQuery(q);

    QNetworkRequest req(url);
    FillCommonHeaders(req);

    auto* reply = m_nam->get(req);
    QTimer::singleShot(m_cfg.timeoutMs, reply, [reply](){
        if (reply && reply->isRunning()) reply->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, pn]() {
        const QByteArray raw = reply->readAll();
        const auto err = reply->error();
        const QString errStr = reply->errorString();
        reply->deleteLater();

        if (m_cancelled) return;

        if (err != QNetworkReply::NoError) {
            emit failed(QString("拉取列表失败：%1").arg(errStr));
            return;
        }

        QVector<PullbackSpot> page;
        int total = 0;
        if (!parseSpotPage(normalizeJsonMaybeJsonp(raw), page, &total)) {
            emit failed(QString("解析列表失败。响应前200字：%1").arg(QString::fromUtf8(raw.left(200))));
            return;
        }
        if (total > 0) m_spotTotal = total;

        if (page.isEmpty()) {
            emit stageChanged(QString("列表完成：%1 只，开始计算回踩 MA%2 / 条件筛选...")
                                  .arg(m_spots.size()).arg(m_cfg.maPeriod));
            startKlineQueue();
            return;
        }

        m_spots += page;
        emit progress(m_spots.size(), m_spotTotal > 0 ? m_spotTotal : -1);

        fetchSpotPage(pn + 1);
    });
}

bool MaPullbackScanner::parseSpotPage(const QByteArray& body, QVector<PullbackSpot>& outPage, int* totalOut)
{
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return false;

    const auto root = doc.object();
    const auto data = root.value("data").toObject();
    if (totalOut) *totalOut = data.value("total").toInt();

    const auto diffVal = data.value("diff");
    if (diffVal.isUndefined() || diffVal.isNull()) return true;

    auto handle = [&](const QJsonObject& o){
        PullbackSpot s;
        s.code   = o.value("f12").toString();
        s.name   = o.value("f14").toString();
        s.last   = o.value("f2").toDouble();
        s.market = o.value("f13").toInt();
        s.pe     = o.value("f9").toDouble();
        s.sector = o.value("f100").toString();
        if (s.code.size() == 6 && s.last > 0) outPage.push_back(s);
    };

    if (diffVal.isArray()) {
        const auto arr = diffVal.toArray();
        for (auto v : arr) if (v.isObject()) handle(v.toObject());
    } else if (diffVal.isObject()) {
        const auto obj = diffVal.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            if (it.value().isObject()) handle(it.value().toObject());
    }
    return true;
}

void MaPullbackScanner::startKlineQueue()
{
    if (m_cancelled) return;

    for (const auto& s : m_spots) {
        if (!m_cfg.includeBJ) {
            if (s.code.startsWith("43") || s.code.startsWith("83") ||
                s.code.startsWith("87") || s.code.startsWith("88"))
                continue;
        }
        m_queue.enqueue(s);
    }

    m_done = 0;
    m_totalToDo = m_queue.size();
    emit progress(0, m_totalToDo);

    pumpKline();
}

void MaPullbackScanner::pumpKline()
{
    if (m_cancelled) return;

    while (m_inFlight < m_cfg.maxInFlight && !m_queue.isEmpty()) {
        const PullbackSpot s = m_queue.dequeue();

        QVector<QString> dates;
        QVector<double> closes;
        const QString secid = secidFor(s);
        const int need = m_cfg.belowDays + m_cfg.maPeriod + 1;

        if (cacheGet(secid, dates, closes) && closes.size() >= need) {
            PullbackKlineStats st;
            if (computeStatsFromBars(dates, closes, m_cfg.maPeriod, m_cfg.belowDays, st) && st.ok) {
                if (st.lastNDaysCloseBelowMA) {
                    PullbackRow r;
                    r.code = s.code; r.name = s.name; r.market = s.market;
                    r.sector = s.sector; r.pe = s.pe;
                    r.last = s.last; r.maPeriod = m_cfg.maPeriod; r.maValue = st.maLast;
                    r.biasPct = (r.last / r.maValue - 1.0) * 100.0;
                    r.belowDays = m_cfg.belowDays;
                    m_results.push_back(r);
                }
            }
            ++m_done;
            emit progress(m_done, m_totalToDo);
            continue;
        }

        requestKlineInitial(s);
    }

    if (!m_cancelled && m_inFlight == 0 && m_queue.isEmpty()) {
        auto key = [&](const PullbackRow& r){
            switch (m_cfg.sortField) {
            case 1: return std::abs(r.biasPct);
            case 2: return r.pe;
            default: return r.biasPct;
            }
        };
        std::sort(m_results.begin(), m_results.end(), [&](const PullbackRow& a, const PullbackRow& b){
            return m_cfg.sortDesc ? (key(a) > key(b)) : (key(a) < key(b));
        });

        saveCache();
        emit stageChanged(QString("完成：%1 只满足条件").arg(m_results.size()));
        emit finished(m_results);
    }
}

QString MaPullbackScanner::secidFor(const PullbackSpot& s, int marketOverride) const
{
    const int m = (marketOverride >= 0) ? marketOverride : s.market;
    return QString("%1.%2").arg(m).arg(s.code);
}

QList<int> MaPullbackScanner::fallbackMarketsFor(const PullbackSpot& s) const
{
    QList<int> ms;
    ms << s.market;
    if (!ms.contains(0)) ms << 0;
    if (!ms.contains(1)) ms << 1;
    if (!ms.contains(2)) ms << 2;
    return ms;
}

void MaPullbackScanner::requestKlineInitial(const PullbackSpot& s)
{
    Task t;
    t.s = s;
    t.retry = 0;
    t.marketTryList = fallbackMarketsFor(s);
    t.marketTryIndex = 0;
    sendKlineTask(t);
}

void MaPullbackScanner::sendKlineTask(Task t)
{
    if (m_cancelled) return;

    const int marketUsed = t.marketTryList.value(t.marketTryIndex, t.s.market);
    t.secidUsed = secidFor(t.s, marketUsed);

    const int needLmt = qMax(80, m_cfg.belowDays + m_cfg.maPeriod + 15);

    QUrl url("https://push2his.eastmoney.com/api/qt/stock/kline/get");
    QUrlQuery q;
    q.addQueryItem("secid", t.secidUsed);
    q.addQueryItem("klt", "101");
    q.addQueryItem("fqt", "0");
    q.addQueryItem("beg", "0");
    q.addQueryItem("end", "20500101");
    q.addQueryItem("lmt", QString::number(needLmt));
    q.addQueryItem("rtntype", "6");
    q.addQueryItem("ut", kEM_UT);
    q.addQueryItem("fields1", "f1,f2,f3,f4");
    q.addQueryItem("fields2", "f51,f52,f53");
    url.setQuery(q);

    QNetworkRequest req(url);
    FillCommonHeaders(req);

    auto* reply = m_nam->get(req);
    ++m_inFlight;
    m_tasks.insert(reply, t);

    QTimer::singleShot(m_cfg.timeoutMs, reply, [reply](){
        if (reply && reply->isRunning()) reply->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        Task t = m_tasks.take(reply);
        const QByteArray raw = reply->readAll();
        const auto err = reply->error();
        reply->deleteLater();

        if (m_inFlight > 0) --m_inFlight;

        if (m_cancelled) {
            return;
        }

        QVector<QString> dates;
        QVector<double> closes;
        bool okBars = (err == QNetworkReply::NoError) && parseKlineBars(normalizeJsonMaybeJsonp(raw), dates, closes);

        if (!okBars) {
            if (t.marketTryIndex + 1 < t.marketTryList.size()) {
                ++t.marketTryIndex;
                sendKlineTask(t);
                pumpKline();
                return;
            }
            if (t.retry < m_cfg.maxRetries) {
                ++t.retry;
                t.marketTryIndex = 0;
                sendKlineTask(t);
                pumpKline();
                return;
            }

            ++m_done;
            emit progress(m_done, m_totalToDo);
            pumpKline();
            return;
        }

        if (dates.size() > 80) {
            const int drop = dates.size() - 80;
            dates = dates.mid(drop);
            closes = closes.mid(drop);
        }
        cachePut(t.secidUsed, dates, closes);

        PullbackKlineStats st;
        if (computeStatsFromBars(dates, closes, m_cfg.maPeriod, m_cfg.belowDays, st) && st.ok) {
            if (st.lastNDaysCloseBelowMA) {
                PullbackRow r;
                r.code = t.s.code; r.name = t.s.name; r.market = t.s.market;
                r.sector = t.s.sector; r.pe = t.s.pe;
                r.last = t.s.last; r.maPeriod = m_cfg.maPeriod; r.maValue = st.maLast;
                r.biasPct = (r.last / r.maValue - 1.0) * 100.0;
                r.belowDays = m_cfg.belowDays;
                m_results.push_back(r);
            }
        }

        ++m_done;
        emit progress(m_done, m_totalToDo);
        pumpKline();
    });
}

bool MaPullbackScanner::parseKlineBars(const QByteArray& body, QVector<QString>& dates, QVector<double>& closes)
{
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return false;

    const auto root = doc.object();
    const auto dataVal = root.value("data");
    if (!dataVal.isObject()) return false;

    const auto data = dataVal.toObject();
    const auto kl = data.value("klines");
    if (!kl.isArray()) return false;

    const auto arr = kl.toArray();
    if (arr.size() < 6) return false;

    dates.clear();
    closes.clear();
    dates.reserve(arr.size());
    closes.reserve(arr.size());

    for (auto v : arr) {
        const auto s = v.toString();
        const auto parts = s.split(',');
        if (parts.size() < 3) continue;
        dates.push_back(parts[0]);
        closes.push_back(parts[2].toDouble());
    }
    return closes.size() >= 6;
}

bool MaPullbackScanner::computeStatsFromBars(const QVector<QString>& dates, const QVector<double>& closes, int maPeriod, int belowDays, PullbackKlineStats& out)
{
    out = PullbackKlineStats{};
    if (dates.size() != closes.size()) return false;
    if (closes.size() < belowDays + maPeriod) return false;

    QVector<QString> d = dates;
    QVector<double>  c = closes;

    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    if (!d.isEmpty() && d.back() == today) {
        d.pop_back();
        c.pop_back();
    }

    const int n = c.size();
    if (n < belowDays + maPeriod) return false;

    auto maAt = [&](int idx)->double {
        double sum = 0;
        const int start = idx - (maPeriod - 1);
        for (int j = start; j <= idx; ++j) sum += c[j];
        return sum / static_cast<double>(maPeriod);
    };

    out.maLast = maAt(n - 1);

    bool allBelow = true;
    for (int idx = n - belowDays; idx <= n - 1; ++idx) {
        if (idx < (maPeriod - 1)) { allBelow = false; break; }
        const double ma = maAt(idx);
        if (!(c[idx] < ma)) { allBelow = false; break; }
    }

    out.ok = true;
    out.lastNDaysCloseBelowMA = allBelow;
    return true;
}

QString MaPullbackScanner::cachePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/kline_cache_pullback.json";
}

void MaPullbackScanner::loadCache()
{
    const QString path = cachePath();
    QFile f(path);
    if (!f.exists()) { m_cache.clear(); m_cacheDate = QDate(); return; }
    if (!f.open(QIODevice::ReadOnly)) { m_cache.clear(); m_cacheDate = QDate(); return; }

    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) { m_cache.clear(); m_cacheDate = QDate(); return; }

    const auto root = doc.object();
    m_cacheDate = QDate::fromString(root.value("date").toString(), Qt::ISODate);

    m_cache.clear();
    const auto items = root.value("items").toObject();
    for (auto it = items.begin(); it != items.end(); ++it) {
        const QString secid = it.key();
        const auto obj = it.value().toObject();

        CacheItem ci;
        const auto jd = obj.value("d").toArray();
        const auto jc = obj.value("c").toArray();
        ci.dates.reserve(jd.size());
        ci.closes.reserve(jc.size());
        for (auto v : jd) ci.dates.push_back(v.toString());
        for (auto v : jc) ci.closes.push_back(v.toDouble());

        if (!ci.closes.isEmpty() && ci.dates.size() == ci.closes.size())
            m_cache.insert(secid, ci);
    }
}

void MaPullbackScanner::saveCache()
{
    m_cacheDate = QDate::currentDate();

    QJsonObject items;
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        QJsonArray jd, jc;
        for (const auto& s : it.value().dates) jd.append(s);
        for (double x : it.value().closes) jc.append(x);

        QJsonObject obj;
        obj.insert("d", jd);
        obj.insert("c", jc);
        items.insert(it.key(), obj);
    }

    QJsonObject root;
    root.insert("date", m_cacheDate.toString(Qt::ISODate));
    root.insert("items", items);

    QSaveFile f(cachePath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

bool MaPullbackScanner::cacheGet(const QString& secid, QVector<QString>& dates, QVector<double>& closes) const
{
    if (!m_cacheDate.isValid() || m_cacheDate != QDate::currentDate()) return false;

    auto it = m_cache.find(secid);
    if (it == m_cache.end()) return false;

    dates = it.value().dates;
    closes = it.value().closes;
    return !closes.isEmpty() && dates.size() == closes.size();
}

void MaPullbackScanner::cachePut(const QString& secid, const QVector<QString>& dates, const QVector<double>& closes)
{
    CacheItem ci;
    ci.dates = dates;
    ci.closes = closes;
    m_cache.insert(secid, ci);
}
