#include "Ma5Scanner.h"
#include "QuoteModel.h"

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

Ma5Scanner::Ma5Scanner(QObject* parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    loadCache();
}

void Ma5Scanner::runOnce(const ScanConfig& cfg)
{
    // 先安全取消上次（不清reply回调，靠 m_cancelled 兜住）
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

void Ma5Scanner::cancel()
{
    m_cancelled = true;

    // 清队列：不再发新请求
    m_queue.clear();

    // abort 正在进行的请求，但不要清 m_tasks / 不要手动改 m_inFlight
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it.key()) it.key()->abort();
    }

    if (!m_cancelSignalSent) {
        m_cancelSignalSent = true;
        emit stageChanged("已取消");
        emit cancelled();
    }
}

QByteArray Ma5Scanner::normalizeJsonMaybeJsonp(const QByteArray& body)
{
    int lb = body.indexOf('[');
    int rb = body.lastIndexOf(']');
    int lo = body.indexOf('{');
    int ro = body.lastIndexOf('}');
    if (lb >= 0 && rb > lb && (lo < 0 || lb < lo)) return body.mid(lb, rb - lb + 1);
    if (lo >= 0 && ro > lo) return body.mid(lo, ro - lo + 1);
    const QByteArray trimmed = body.trimmed();
    if (trimmed == "null" || trimmed == "OK") return QByteArrayLiteral("[]");
    if (body.contains("CallbackList(null)") || body.contains("(null)")) {
        return QByteArrayLiteral("[]");
    }
    return body;
}

// ------------------- spot list -------------------
void Ma5Scanner::fetchSpotPage(int pn)
{
    if (m_cancelled) return;

    QUrl url(m_cfg.spotBaseUrl);
    QUrlQuery q;
    if (m_cfg.provider == ScanConfig::Provider::Sina) {
        q.addQueryItem("page", QString::number(pn));
        q.addQueryItem("num", QString::number(m_cfg.pageSize));
        q.addQueryItem("sort", "code");
        q.addQueryItem("asc", "1");
        q.addQueryItem("node", "hs_a");
        q.addQueryItem("symbol", "");
        q.addQueryItem("_s_r_a", "init");
    } else {
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
    }
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

        QVector<Spot> page;
        int total = 0;
        if (m_cfg.provider == ScanConfig::Provider::Sina) {
            if (!parseSpotPageSina(normalizeJsonMaybeJsonp(raw), page)) {
                emit failed(QString("解析列表失败。响应前200字：%1").arg(QString::fromUtf8(raw.left(200))));
                return;
            }
        } else if (!parseSpotPageEastmoney(normalizeJsonMaybeJsonp(raw), page, &total)) {
            emit failed(QString("解析列表失败。响应前200字：%1").arg(QString::fromUtf8(raw.left(200))));
            return;
        }
        if (total > 0) m_spotTotal = total;

        if (page.isEmpty()) {
            emit stageChanged(QString("列表完成：%1 只，开始计算 MA5 / 条件筛选...").arg(m_spots.size()));
            startKlineQueue();
            return;
        }

        m_spots += page;
        emit progress(m_spots.size(), m_spotTotal > 0 ? m_spotTotal : -1);

        fetchSpotPage(pn + 1);
    });
}

bool Ma5Scanner::parseSpotPageEastmoney(const QByteArray& body, QVector<Spot>& outPage, int* totalOut)
{
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return false;

    const auto root = doc.object();
    const auto data = root.value("data").toObject();
    if (totalOut) *totalOut = data.value("total").toInt();

    const auto diffVal = data.value("diff");
    if (diffVal.isUndefined() || diffVal.isNull()) return true;

    auto handle = [&](const QJsonObject& o){
        Spot s;
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

bool Ma5Scanner::parseSpotPageSina(const QByteArray& body, QVector<Spot>& outPage)
{
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) return false;

    const auto arr = doc.array();
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        const auto o = v.toObject();
        Spot s;
        s.code = o.value("code").toString();
        s.name = o.value("name").toString();
        s.last = o.value("trade").toString().toDouble();
        s.pe = o.value("per").toDouble();
        const QString symbol = o.value("symbol").toString();
        s.market = symbol.startsWith("sh") ? 1 : 0;
        if (s.code.size() == 6 && s.last > 0) outPage.push_back(s);
    }
    return true;
}

// ------------------- scan queue -------------------
void Ma5Scanner::startKlineQueue()
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
    m_totalToDo = m_queue.size();              // ✅ 固定总数
    emit progress(0, m_totalToDo);

    pumpKline();
}

void Ma5Scanner::pumpKline()
{
    if (m_cancelled) return;

    while (m_inFlight < m_cfg.maxInFlight && !m_queue.isEmpty()) {
        const Spot s = m_queue.dequeue();

        // cache 命中：直接算
        QVector<QString> dates;
        QVector<double> closes;
        const QString secid = secidFor(s);
        const int aboveDays = (m_cfg.mode == ScanConfig::Mode::PullbackToMa5) ? m_cfg.pullbackAboveDays : 0;
        const int need = qMax(m_cfg.belowDays, aboveDays) + 6;

        if (cacheGet(secid, dates, closes) && closes.size() >= need) {
            KlineStats st;
            const int aboveDays = (m_cfg.mode == ScanConfig::Mode::PullbackToMa5) ? m_cfg.pullbackAboveDays : 0;
            if (computeStatsFromBars(dates, closes, m_cfg.belowDays, aboveDays, st) && st.ok) {
                const bool slopeOk = !m_cfg.requireMa5SlopeUp || (st.ma5Last > st.ma5Prev);
                const bool isBreakAbove = (s.last > st.ma5Last && st.prevNDaysCloseBelowMA5 && slopeOk);
                const double tol = m_cfg.pullbackTolerancePct / 100.0;
                const bool nearMa5 = (s.last >= st.ma5Last * (1.0 - tol) && s.last <= st.ma5Last * (1.0 + tol));
                const bool isPullback = (st.prevNDaysCloseAboveMA5 && slopeOk && nearMa5);
                const bool match = (m_cfg.mode == ScanConfig::Mode::BreakAboveMa5) ? isBreakAbove : isPullback;
                if (match) {
                    const int daysValue = (m_cfg.mode == ScanConfig::Mode::PullbackToMa5)
                                              ? m_cfg.pullbackAboveDays
                                              : m_cfg.belowDays;
                    PickRow r;
                    r.code = s.code; r.name = s.name; r.market = s.market;
                    r.sector = s.sector; r.pe = s.pe;
                    r.last = s.last; r.ma5 = st.ma5Last;
                    r.biasPct = (r.last / r.ma5 - 1.0) * 100.0;
                    r.belowDays = daysValue;
                    m_results.push_back(r);
                }
            }
            ++m_done;
            emit progress(m_done, m_totalToDo);
            continue;
        }

        // 发起网络任务
        requestKlineInitial(s);
    }

    // ✅ 只有当队列空 + 无在途，才完成
    if (!m_cancelled && m_inFlight == 0 && m_queue.isEmpty()) {
        auto key = [&](const PickRow& r){
            switch (m_cfg.sortField) {
            case 1: return std::abs(r.biasPct);
            case 2: return r.pe;
            default: return r.biasPct;
            }
        };
        std::sort(m_results.begin(), m_results.end(), [&](const PickRow& a, const PickRow& b){
            return m_cfg.sortDesc ? (key(a) > key(b)) : (key(a) < key(b));
        });

        saveCache();
        emit stageChanged(QString("完成：%1 只满足条件").arg(m_results.size()));
        emit finished(m_results);
    }
}

// ------------------- kline task (fixed retry logic) -------------------
QString Ma5Scanner::secidFor(const Spot& s, int marketOverride) const
{
    const int m = (marketOverride >= 0) ? marketOverride : s.market;
    return QString("%1.%2").arg(m).arg(s.code);
}

QList<int> Ma5Scanner::fallbackMarketsFor(const Spot& s) const
{
    QList<int> ms;
    ms << s.market;
    if (!ms.contains(0)) ms << 0;
    if (!ms.contains(1)) ms << 1;
    if (!ms.contains(2)) ms << 2;
    return ms;
}

void Ma5Scanner::requestKlineInitial(const Spot& s)
{
    Task t;
    t.s = s;
    t.retry = 0;
    t.marketTryList = fallbackMarketsFor(s);
    t.marketTryIndex = 0;
    sendKlineTask(t);
}

void Ma5Scanner::sendKlineTask(Task t)
{
    if (m_cancelled) return;

    const int marketUsed = t.marketTryList.value(t.marketTryIndex, t.s.market);
    t.secidUsed = secidFor(t.s, marketUsed);

    const int aboveDays = (m_cfg.mode == ScanConfig::Mode::PullbackToMa5) ? m_cfg.pullbackAboveDays : 0;
    const int needLmt = qMax(40, qMax(m_cfg.belowDays, aboveDays) + 15);

    QUrl url(m_cfg.klineBaseUrl);
    QUrlQuery q;
    if (m_cfg.provider == ScanConfig::Provider::Sina) {
        const bool isShanghai = (t.s.market == 1 || t.s.code.startsWith("6"));
        const QString symbol = QString("%1%2").arg(isShanghai ? "sh" : "sz", t.s.code);
        q.addQueryItem("symbol", symbol);
        q.addQueryItem("scale", "240");
        q.addQueryItem("ma", "no");
        q.addQueryItem("datalen", QString::number(needLmt));
    } else if (m_cfg.provider == ScanConfig::Provider::Tonghuashun) {
        const QString symbol = QString("hs_%1").arg(t.s.code);
        QString path = url.path();
        if (!path.endsWith('/')) path += '/';
        path += QString("%1/01/last.js").arg(symbol);
        url.setPath(path);
    } else {
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
    }
    url.setQuery(q);

    QNetworkRequest req(url);
    FillCommonHeaders(req);
    if (m_cfg.provider == ScanConfig::Provider::Tonghuashun) {
        req.setRawHeader("Referer", "https://q.10jqka.com.cn/");
    }

    auto* reply = m_nam->get(req);
    ++m_inFlight;
    m_tasks.insert(reply, t);

    QTimer::singleShot(m_cfg.timeoutMs, reply, [reply](){
        if (reply && reply->isRunning()) reply->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        Task t = m_tasks.take(reply); // 保留 task 状态
        const QByteArray raw = reply->readAll();
        const auto err = reply->error();
        reply->deleteLater();

        if (m_inFlight > 0) --m_inFlight;

        // 取消后：不再做任何统计/续跑（避免崩）
        if (m_cancelled) {
            return;
        }

        QVector<QString> dates;
        QVector<double> closes;
        bool okBars = (err == QNetworkReply::NoError) && parseKlineBars(normalizeJsonMaybeJsonp(raw), m_cfg, dates, closes);

        if (!okBars) {
            // ✅ 失败：优先换 market；都试过再按 retry 次数重试
            if (t.marketTryIndex + 1 < t.marketTryList.size()) {
                ++t.marketTryIndex;
                // 不算 done，继续发
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

            // ✅ 最终放弃：现在才算 done
            ++m_done;
            emit progress(m_done, m_totalToDo);
            pumpKline();
            return;
        }

        // 成功：写缓存
        if (dates.size() > 80) {
            const int drop = dates.size() - 80;
            dates = dates.mid(drop);
            closes = closes.mid(drop);
        }
        cachePut(t.secidUsed, dates, closes);

        KlineStats st;
        const int aboveDays = (m_cfg.mode == ScanConfig::Mode::PullbackToMa5) ? m_cfg.pullbackAboveDays : 0;
        if (computeStatsFromBars(dates, closes, m_cfg.belowDays, aboveDays, st) && st.ok) {
            const bool slopeOk = !m_cfg.requireMa5SlopeUp || (st.ma5Last > st.ma5Prev);
            const bool isBreakAbove = (t.s.last > st.ma5Last && st.prevNDaysCloseBelowMA5 && slopeOk);
            const double tol = m_cfg.pullbackTolerancePct / 100.0;
            const bool nearMa5 = (t.s.last >= st.ma5Last * (1.0 - tol) && t.s.last <= st.ma5Last * (1.0 + tol));
            const bool isPullback = (st.prevNDaysCloseAboveMA5 && slopeOk && nearMa5);
            const bool match = (m_cfg.mode == ScanConfig::Mode::BreakAboveMa5) ? isBreakAbove : isPullback;
            if (match) {
                const int daysValue = (m_cfg.mode == ScanConfig::Mode::PullbackToMa5)
                                          ? m_cfg.pullbackAboveDays
                                          : m_cfg.belowDays;
                PickRow r;
                r.code = t.s.code; r.name = t.s.name; r.market = t.s.market;
                r.sector = t.s.sector; r.pe = t.s.pe;
                r.last = t.s.last; r.ma5 = st.ma5Last;
                r.biasPct = (r.last / r.ma5 - 1.0) * 100.0;
                r.belowDays = daysValue;
                m_results.push_back(r);
            }
        }

        // ✅ 成功：算 done 一次
        ++m_done;
        emit progress(m_done, m_totalToDo);
        pumpKline();
    });
}

bool Ma5Scanner::parseKlineBars(const QByteArray& body, const ScanConfig& cfg, QVector<QString>& dates, QVector<double>& closes)
{
    if (cfg.provider == ScanConfig::Provider::Sina) {
        return parseKlineBarsSina(body, dates, closes);
    }
    if (cfg.provider == ScanConfig::Provider::Tonghuashun) {
        return parseKlineBarsTonghuashun(body, dates, closes);
    }
    return parseKlineBarsEastmoney(body, dates, closes);
}

bool Ma5Scanner::parseKlineBarsEastmoney(const QByteArray& body, QVector<QString>& dates, QVector<double>& closes)
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

bool Ma5Scanner::parseKlineBarsSina(const QByteArray& body, QVector<QString>& dates, QVector<double>& closes)
{
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) return false;

    const auto arr = doc.array();
    if (arr.size() < 6) return false;

    dates.clear();
    closes.clear();
    dates.reserve(arr.size());
    closes.reserve(arr.size());

    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        const auto o = v.toObject();
        dates.push_back(o.value("day").toString());
        closes.push_back(o.value("close").toString().toDouble());
    }
    return closes.size() >= 6;
}

bool Ma5Scanner::parseKlineBarsTonghuashun(const QByteArray& body, QVector<QString>& dates, QVector<double>& closes)
{
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return false;

    const auto root = doc.object();
    const auto dataVal = root.value("data");
    if (!dataVal.isString()) return false;

    const auto raw = dataVal.toString();
    const auto rows = raw.split(';', Qt::SkipEmptyParts);
    if (rows.size() < 6) return false;

    dates.clear();
    closes.clear();
    dates.reserve(rows.size());
    closes.reserve(rows.size());

    for (const auto& row : rows) {
        const auto parts = row.split(',');
        if (parts.size() < 5) continue;
        dates.push_back(parts[0]);
        closes.push_back(parts[4].toDouble());
    }

    return closes.size() >= 6;
}

bool Ma5Scanner::computeStatsFromBars(const QVector<QString>& dates, const QVector<double>& closes, int belowDays, int aboveDays, KlineStats& out)
{
    out = KlineStats{};
    if (dates.size() != closes.size()) return false;
    const int requiredDays = qMax(belowDays, aboveDays);
    if (closes.size() < requiredDays + 5) return false;

    QVector<QString> d = dates;
    QVector<double>  c = closes;

    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    if (!d.isEmpty() && d.back() == today) {
        d.pop_back();
        c.pop_back();
    }

    const int n = c.size();
    if (n < requiredDays + 5) return false;

    auto ma5At = [&](int idx)->double {
        double sum = 0;
        for (int j = idx - 4; j <= idx; ++j) sum += c[j];
        return sum / 5.0;
    };

    out.lastClose = c.back();
    out.ma5Last = ma5At(n - 1);
    out.ma5Prev = (n >= 6) ? ma5At(n - 2) : out.ma5Last;

    bool allBelow = true;
    for (int offset = belowDays; offset >= 1; --offset) {
        const int idx = n - 1 - offset;
        if (idx < 4) { allBelow = false; break; }
        const double ma = ma5At(idx);
        if (!(c[idx] < ma)) { allBelow = false; break; }
    }

    bool allAbove = true;
    if (aboveDays <= 0) {
        allAbove = false;
    } else {
        for (int offset = aboveDays; offset >= 1; --offset) {
            const int idx = n - 1 - offset;
            if (idx < 4) { allAbove = false; break; }
            const double ma = ma5At(idx);
            if (!(c[idx] > ma)) { allAbove = false; break; }
        }
    }

    out.ok = true;
    out.prevNDaysCloseBelowMA5 = allBelow;
    out.prevNDaysCloseAboveMA5 = allAbove;
    return true;
}

// ------------------- cache -------------------
QString Ma5Scanner::cachePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/kline_cache.json";
}

void Ma5Scanner::loadCache()
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

void Ma5Scanner::saveCache()
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

bool Ma5Scanner::cacheGet(const QString& secid, QVector<QString>& dates, QVector<double>& closes) const
{
    if (!m_cacheDate.isValid() || m_cacheDate != QDate::currentDate()) return false;

    auto it = m_cache.find(secid);
    if (it == m_cache.end()) return false;

    dates = it.value().dates;
    closes = it.value().closes;
    return !closes.isEmpty() && dates.size() == closes.size();
}

void Ma5Scanner::cachePut(const QString& secid, const QVector<QString>& dates, const QVector<double>& closes)
{
    CacheItem ci;
    ci.dates = dates;
    ci.closes = closes;
    m_cache.insert(secid, ci);
}
