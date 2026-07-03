#include "TestHelpers.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QTest>

namespace TestHelpers {

// ── Temp Dir / Config Seeding ────────────────────────────────────────────────

QTemporaryDir* createTempDir()
{
    return new QTemporaryDir();
}

QString seedConfig(QTemporaryDir *dir, const QString &moduleId,
                   const QJsonObject &overrides)
{
    QJsonObject modules;
    modules[moduleId] = overrides;

    QJsonObject root;
    root["modules"] = modules;

    QString path = dir->path() + "/config.json";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
    }
    return dir->path();
}

QString seedAuth(QTemporaryDir *dir,
                 const QString &serverUrl,
                 const QString &accessToken,
                 const QString &userId,
                 const QString &userName,
                 const QString &serverName,
                 const QString &deviceId)
{
    QJsonObject auth;
    auth["serverUrl"]   = serverUrl;
    auth["accessToken"] = accessToken;
    auth["userId"]      = userId;
    auth["userName"]    = userName;
    auth["serverName"]  = serverName;
    auth["deviceId"]    = deviceId;

    QString path = dir->path() + "/jellyfin_auth.json";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(auth).toJson(QJsonDocument::Indented));
        f.close();
    }
    return dir->path();
}

// ── Signal / Event Loop Helpers ──────────────────────────────────────────────

bool waitForSignal(QSignalSpy &spy, int timeoutMs)
{
    // QSignalSpy has no "entered" signal; use its built-in wait(), which spins
    // the event loop and returns once a signal arrives. If the signal already
    // fired synchronously (count > 0), we're done immediately — wait() only
    // catches *new* emissions.
    if (spy.count() > 0)
        return true;
    return spy.wait(timeoutMs);
}

void spinLoop(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// ── JSON Fixture Builders ────────────────────────────────────────────────────

QJsonObject makeMovieItem(const QString &id,
                          const QString &title,
                          int year,
                          const QString &overview)
{
    QJsonObject item;
    item["Id"]          = id;
    item["Name"]        = title;
    item["Type"]        = "Movie";
    item["Year"]        = year;
    item["Overview"]    = overview;
    item["MediaType"]   = "Video";
    item["RunTimeTicks"] = static_cast<qint64>(72000000000LL); // 2 hours
    return item;
}

QJsonObject makeEpisodeItem(const QString &id,
                            const QString &seriesId,
                            const QString &seriesName,
                            int seasonNumber,
                            int episodeNumber)
{
    QJsonObject item;
    item["Id"]              = id;
    item["Name"]            = QString("Episode %1").arg(episodeNumber);
    item["Type"]            = "Episode";
    item["SeriesId"]        = seriesId;
    item["SeriesName"]      = seriesName;
    item["ParentIndexNumber"] = seasonNumber;
    item["IndexNumber"]     = episodeNumber;
    item["MediaType"]       = "Video";
    item["RunTimeTicks"]    = static_cast<qint64>(27000000000LL); // 45 min
    return item;
}

QJsonObject makeSeriesItem(const QString &id,
                           const QString &title)
{
    QJsonObject item;
    item["Id"]    = id;
    item["Name"]  = title;
    item["Type"]  = "Series";
    return item;
}

QJsonObject makeAudioStream(int index,
                            const QString &language,
                            const QString &codec,
                            bool isDefault)
{
    QJsonObject stream;
    stream["Type"]          = "Audio";
    stream["Index"]         = index;
    stream["Language"]      = language;
    stream["Codec"]         = codec;
    stream["IsDefault"]     = isDefault;
    stream["Channels"]      = 2;
    stream["ChannelLayout"] = "stereo";
    stream["DisplayTitle"]  = QString("%1 (%2)").arg(language.toUpper(), codec.toUpper());
    return stream;
}

QJsonObject makeSubtitleStream(int index,
                               const QString &language,
                               const QString &codec,
                               bool isDefault,
                               bool isForced,
                               bool isText)
{
    QJsonObject stream;
    stream["Type"]                    = "Subtitle";
    stream["Index"]                   = index;
    stream["Language"]                = language;
    stream["Codec"]                   = codec;
    stream["IsDefault"]               = isDefault;
    stream["IsForced"]                = isForced;
    stream["IsTextSubtitleStream"]    = isText;
    stream["DisplayTitle"]            = QString("%1 (%2)").arg(language.toUpper(), codec.toUpper());
    return stream;
}

QJsonObject makeMediaSource(const QString &id,
                            bool supportsDirectPlay,
                            bool supportsDirectStream,
                            const QString &transcodingUrl)
{
    QJsonObject source;
    source["Id"]                    = id;
    source["SupportsDirectPlay"]    = supportsDirectPlay;
    source["SupportsDirectStream"]  = supportsDirectStream;
    source["Container"]             = "mkv";

    if (!transcodingUrl.isEmpty())
        source["TranscodingUrl"] = transcodingUrl;

    QJsonArray streams;
    streams.append(makeAudioStream(0, "eng", "aac", true));
    streams.append(makeSubtitleStream(1, "eng", "subrip", false, false, true));
    source["MediaStreams"] = streams;

    return source;
}

QJsonObject makePlayableItem(const QString &id)
{
    QJsonObject item;
    item["Id"]        = id;
    item["Name"]      = "Playable Item";
    item["Type"]      = "Movie";
    item["MediaType"] = "Video";
    item["RunTimeTicks"] = static_cast<qint64>(72000000000LL);

    QJsonArray sources;
    sources.append(makeMediaSource("src-" + id, true, true,
                                   "/Videos/" + id + "/master.m3u8?static=false"));
    item["MediaSources"] = sources;

    return item;
}

QJsonObject makeLibraryItem(const QString &id,
                            const QString &name,
                            const QString &collectionType)
{
    QJsonObject item;
    item["Id"]             = id;
    item["Name"]           = name;
    item["CollectionType"] = collectionType;
    return item;
}

QJsonObject makeItemsResponse(const QJsonArray &items)
{
    QJsonObject resp;
    resp["Items"]    = items;
    resp["TotalRecordCount"] = items.size();
    return resp;
}

} // namespace TestHelpers
