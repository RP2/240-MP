#include "JellyfinBackend.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>
#include <QCoreApplication>
#include <QUuid>
#include <QSslError>
#include <QSysInfo>
#include <QSet>
#include <QRegularExpression>

static const QString kModuleId = QStringLiteral("com.240mp.jellyfin");

static QString authHeaderValue(const QString &token, const QString &deviceId) {
    QString auth = QStringLiteral("MediaBrowser Client=\"240-MP\", Device=\"%1\", DeviceId=\"%2\", Version=\"%3\"")
                       .arg(QSysInfo::machineHostName(), deviceId, QCoreApplication::applicationVersion());
    if (!token.isEmpty())
        auth += QStringLiteral(", Token=\"%1\"").arg(token);
    return auth;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

JellyfinBackend::JellyfinBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
    , m_nam(new QNetworkAccessManager(this))
{
    loadAuthState();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
}

// ---------------------------------------------------------------------------
// Auth state persistence
// ---------------------------------------------------------------------------

QString JellyfinBackend::normalizeServerUrl(const QString &url) {
    QString u = url.trimmed();
    while (u.endsWith('/'))
        u.chop(1);
    return u;
}

void JellyfinBackend::loadAuthState() {
    QFile f(m_dataRoot + "/jellyfin_auth.json");
    if (!f.open(QIODevice::ReadOnly))
        return;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    QJsonObject auth = doc.object();
    m_serverUrl  = normalizeServerUrl(auth["serverUrl"].toString());
    m_accessToken = auth["accessToken"].toString();
    m_userId     = auth["userId"].toString();
    m_userName   = auth["userName"].toString();
    m_serverName = auth["serverName"].toString();
    m_deviceId   = auth["deviceId"].toString();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
}

void JellyfinBackend::saveAuthState() {
    QJsonObject auth;
    auth["serverUrl"]   = m_serverUrl;
    auth["accessToken"] = m_accessToken;
    auth["userId"]      = m_userId;
    auth["userName"]    = m_userName;
    auth["serverName"]  = m_serverName;
    auth["deviceId"]    = m_deviceId;

    QFile f(m_dataRoot + "/jellyfin_auth.json");
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[JellyfinBackend] Could not write jellyfin_auth.json: %s", qPrintable(f.errorString()));
        return;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(auth).toJson(QJsonDocument::Indented));
    f.close();
}

void JellyfinBackend::clearAuthState() {
    m_serverUrl.clear();
    m_accessToken.clear();
    m_userId.clear();
    m_userName.clear();
    m_serverName.clear();
    m_currentPlaySessionId.clear();
    // Keep m_deviceId — it identifies this install across auth sessions
    // Re-save with just deviceId so loadAuthState can recover it
    QJsonObject keep;
    keep["deviceId"] = m_deviceId;
    QFile f(m_dataRoot + "/jellyfin_auth.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        f.write(QJsonDocument(keep).toJson());
        f.close();
    }
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------

QJsonObject JellyfinBackend::loadConfig() const {
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
    }
    return {};
}

void JellyfinBackend::saveConfig(const QJsonObject &cfg) const {
    QFile f(m_dataRoot + "/config.json");
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[JellyfinBackend] Could not write config.json: %s", qPrintable(f.errorString()));
        return;
    }
    f.write(QJsonDocument(cfg).toJson(QJsonDocument::Indented));
}

QJsonObject JellyfinBackend::moduleConfig() const {
    return loadConfig()["modules"].toObject()[kModuleId].toObject();
}

int JellyfinBackend::videoQualityBitrate() const {
    QString quality = moduleConfig()["video_quality"].toString("480p");
    if (quality == QLatin1String("1080p")) return 10000000;
    if (quality == QLatin1String("720p"))  return 6000000;
    if (quality == QLatin1String("576p"))  return 3500000;
    return 4000000; // 480p default
}

int JellyfinBackend::videoQualityMaxHeight() const {
    QString quality = moduleConfig()["video_quality"].toString("480p");
    if (quality == QLatin1String("1080p")) return 1080;
    if (quality == QLatin1String("720p"))  return 720;
    if (quality == QLatin1String("576p"))  return 576;
    return 480;
}

QString JellyfinBackend::resumePlaybackMode() const {
    return moduleConfig()["resume_playback"].toString("ask");
}

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

QNetworkRequest JellyfinBackend::jellyfinRequest(const QUrl &url) const {
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", authHeaderValue(m_accessToken, m_deviceId).toLatin1());
    return req;
}

QNetworkReply *JellyfinBackend::jellyfinGet(const QUrl &url) {
    auto *reply = m_nam->get(jellyfinRequest(url));
    ignoreSslErrors(reply);
    return reply;
}

QNetworkReply *JellyfinBackend::jellyfinPost(const QUrl &url, const QByteArray &body) {
    QNetworkRequest req = jellyfinRequest(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = m_nam->post(req, body);
    ignoreSslErrors(reply);
    return reply;
}

static QList<QSslError> filterExpectedSslErrors(const QList<QSslError> &errors) {
    static const QSet<QSslError::SslError> kExpected = {
        QSslError::SelfSignedCertificate,
        QSslError::HostNameMismatch,
        QSslError::UnableToGetLocalIssuerCertificate,
        QSslError::UnableToVerifyFirstCertificate,
    };
    QList<QSslError> allowed;
    for (const QSslError &e : errors) {
        if (kExpected.contains(e.error()))
            allowed.append(e);
    }
    return allowed;
}

void JellyfinBackend::ignoreSslErrors(QNetworkReply *reply) const {
    connect(reply, &QNetworkReply::sslErrors, reply, [this, reply](const QList<QSslError> &errors) {
        // Only relax for the configured Jellyfin server — typical of self-signed LAN certs
        QUrl serverUrl(m_serverUrl);
        if (reply->url().host() != serverUrl.host())
            return;
        QList<QSslError> allowed = filterExpectedSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

bool JellyfinBackend::has_auth() {
    return !m_accessToken.isEmpty() && !m_userId.isEmpty() && !m_serverUrl.isEmpty();
}

QString JellyfinBackend::get_server_name() {
    return m_serverName;
}

QString JellyfinBackend::get_user_name() {
    return m_userName;
}

QString JellyfinBackend::get_auth_state() {
    return has_auth() ? QStringLiteral("authed") : QStringLiteral("none");
}

void JellyfinBackend::check_auth() {
    if (!has_auth()) {
        emit authStateChanged();
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId);
    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401) {
            qWarning("[JellyfinBackend] Token rejected — signing out");
            clearAuthState();
            emit authRevoked();
            emit authStateChanged();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("AUTH CHECK FAILED: " + reply->errorString());
            return;
        }
        emit authStateChanged();
    });
}

void JellyfinBackend::logout() {
    // Revoke the access token server-side so it can't be reused
    if (has_auth()) {
        QUrl url(m_serverUrl + "/Sessions/Logout");
        auto *reply = m_nam->post(jellyfinRequest(url), QByteArray());
        connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    }
    clearAuthState();
    emit logoutComplete();
    emit authStateChanged();
}

// ---------------------------------------------------------------------------
// Quick Connect
// ---------------------------------------------------------------------------

void JellyfinBackend::quick_connect_initiate(const QString &serverUrl) {
    QString normalized = normalizeServerUrl(serverUrl);
    if (normalized.isEmpty()) {
        emit errorOccurred("SERVER URL REQUIRED");
        return;
    }

    m_quickConnectServerUrl = normalized;
    QUrl url(normalized + "/QuickConnect/Initiate");

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", authHeaderValue(QString(), m_deviceId).toLatin1());

    // Initiate uses empty POST body
    auto *reply = m_nam->post(req, QByteArray());
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
        for (const QSslError &e : errors)
            qDebug("[JellyfinBackend] QC SSL error (ignored): %s", qPrintable(e.errorString()));
        QList<QSslError> allowed = filterExpectedSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            emit quickConnectFailed("CONNECTION FAILED: " + reply->errorString());
            return;
        }
        if (status >= 400) {
            qWarning("[JellyfinBackend] QC Initiate HTTP %d", status);
            if (status == 401)
                emit quickConnectFailed("QUICK CONNECT NOT ENABLED ON SERVER");
            else
                emit quickConnectFailed("SERVER ERROR (HTTP " + QString::number(status) + ")");
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(body).object();
        QString secret = data["Secret"].toString();
        QString code   = data["Code"].toString();

        if (secret.isEmpty() || code.isEmpty()) {
            emit quickConnectFailed("INVALID RESPONSE FROM SERVER");
            return;
        }

        m_quickConnectSecret = secret;
        emit quickConnectCodeReady(code, secret);
    });
}

void JellyfinBackend::quick_connect_poll(const QString &secret) {
    if (secret.isEmpty()) {
        emit quickConnectFailed("NO SECRET");
        return;
    }

    QUrl url(m_quickConnectServerUrl + "/QuickConnect/Connect?secret=" + secret);
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", authHeaderValue(QString(), m_deviceId).toLatin1());

    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
        for (const QSslError &e : errors)
            qDebug("[JellyfinBackend] QC SSL error (ignored): %s", qPrintable(e.errorString()));
        QList<QSslError> allowed = filterExpectedSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, secret]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            emit quickConnectFailed("POLL FAILED: " + reply->errorString());
            return;
        }
        if (status >= 400) {
            // 404 means secret expired or never existed
            if (status == 404)
                emit quickConnectFailed("CODE EXPIRED — RETRY");
            else
                emit quickConnectFailed("SERVER ERROR (HTTP " + QString::number(status) + ")");
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(body).object();
        if (data["Authenticated"].toBool()) {
            emit quickConnectApproved();
        }
        // else: still waiting — QML Timer continues polling
    });
}

void JellyfinBackend::quick_connect_authenticate(const QString &secret) {
    if (secret.isEmpty()) {
        emit errorOccurred("NO SECRET");
        return;
    }

    QUrl url(m_quickConnectServerUrl + "/Users/AuthenticateWithQuickConnect");
    QJsonObject body;
    body["Secret"] = secret;
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", authHeaderValue(QString(), m_deviceId).toLatin1());

    auto *reply = m_nam->post(req, payload);
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
        for (const QSslError &e : errors)
            qDebug("[JellyfinBackend] QC SSL error (ignored): %s", qPrintable(e.errorString()));
        QList<QSslError> allowed = filterExpectedSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("AUTH FAILED: " + reply->errorString());
            return;
        }
        if (status >= 400) {
            qWarning("[JellyfinBackend] QC Auth HTTP %d", status);
            emit errorOccurred("AUTH FAILED (HTTP " + QString::number(status) + ")");
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(body).object();
        QString token = data["AccessToken"].toString();
        QJsonObject user = data["User"].toObject();

        if (token.isEmpty() || user["Id"].toString().isEmpty()) {
            emit errorOccurred("INVALID AUTH RESPONSE");
            return;
        }

        m_serverUrl   = m_quickConnectServerUrl;
        m_accessToken = token;
        m_userId      = user["Id"].toString();
        m_userName    = user["Name"].toString();
        m_serverName  = m_quickConnectServerUrl; // temp: fetch real name below
        m_quickConnectSecret.clear();

        // Fetch actual server name
        {
            QUrl infoUrl(m_serverUrl + "/System/Info/Public");
            QNetworkRequest infoReq(infoUrl);
            infoReq.setRawHeader("Accept", "application/json");
            infoReq.setRawHeader("Authorization", authHeaderValue(m_accessToken, m_deviceId).toLatin1());
            auto *infoReply = m_nam->get(infoReq);
            connect(infoReply, &QNetworkReply::sslErrors, infoReply, [](const QList<QSslError>&){});
            connect(infoReply, &QNetworkReply::finished, this, [this, infoReply]() {
                infoReply->deleteLater();
                if (infoReply->error() == QNetworkReply::NoError) {
                    QJsonObject infoData = QJsonDocument::fromJson(infoReply->readAll()).object();
                    QString name = infoData["ServerName"].toString();
                    if (!name.isEmpty()) {
                        m_serverName = name;
                        saveAuthState();
                    }
                }
            });
        }

        saveAuthState();

        QJsonObject cfg = loadConfig();
        QJsonObject modules = cfg["modules"].toObject();
        QJsonObject modCfg  = modules[kModuleId].toObject();
        modCfg["server_url"] = m_serverUrl;
        modules[kModuleId] = modCfg;
        cfg["modules"] = modules;
        saveConfig(cfg);

        emit authStateChanged();
    });
}

void JellyfinBackend::quick_connect_cancel() {
    if (m_quickConnectSecret.isEmpty())
        return;
    QUrl url(m_quickConnectServerUrl + "/QuickConnect/Connect?secret=" + m_quickConnectSecret);
    QNetworkRequest req(url);
    m_nam->deleteResource(req);
    m_quickConnectSecret.clear();
}

// ---------------------------------------------------------------------------
// Item formatting
// ---------------------------------------------------------------------------

QVariantMap JellyfinBackend::formatItem(const QJsonObject &item) const {
    QJsonObject userData = item["UserData"].toObject();
    QJsonObject imageTags = item["ImageTags"].toObject();
    QJsonArray mediaSources = item["MediaSources"].toArray();
    QJsonObject mediaSource = mediaSources.isEmpty() ? QJsonObject() : mediaSources[0].toObject();
    QJsonArray streams = mediaSource["MediaStreams"].toArray();

    QVariantList audioStreams;
    QVariantList subtitleStreams;
    for (const QJsonValue &v : streams) {
        QJsonObject s = v.toObject();
        QString type = s["Type"].toString();
        if (type == QLatin1String("Audio")) {
            QVariantMap as;
            as["id"]          = QString::number(s["Index"].toInt());
            as["language"]    = s["Language"].toString();
            as["codec"]       = s["Codec"].toString();
            as["channels"]    = s["ChannelLayout"].toString().isEmpty()
                                   ? s["Channels"].toVariant()
                                   : QVariant(s["ChannelLayout"].toString());
            as["selected"]    = s["IsDefault"].toBool();
            as["displayTitle"]= s["DisplayTitle"].toString();
            as["title"]       = s["Title"].toString();
            audioStreams.append(as);
        } else if (type == QLatin1String("Subtitle")) {
            QVariantMap ss;
            ss["id"]          = QString::number(s["Index"].toInt());
            ss["language"]    = s["Language"].toString();
            ss["codec"]       = s["Codec"].toString();
            ss["selected"]    = s["IsDefault"].toBool();
            ss["displayTitle"]= s["DisplayTitle"].toString();
            ss["title"]       = s["Title"].toString();
            subtitleStreams.append(ss);
        }
    }

    QVariantList genres;
    for (const QJsonValue &v : item["Genres"].toArray())
        genres.append(v.toVariant());

    QVariantMap map;
    map["itemId"]          = item["Id"].toString();
    map["seriesId"]        = item["SeriesId"].toString();
    map["title"]           = item["Name"].toString();
    map["type"]            = item["Type"].toString().toLower();
    map["overview"]        = item["Overview"].toString();
    map["year"]            = item["ProductionYear"].toVariant();
    map["genres"]          = genres;
    map["duration"]        = item["RunTimeTicks"].toDouble() / 10000.0;
    map["viewOffset"]      = userData["PlaybackPositionTicks"].toDouble() / 10000.0;
    map["leafCount"]       = item["ChildCount"].toInt();
    map["index"]           = item["IndexNumber"].toInt();
    map["parentIndex"]     = item["ParentIndexNumber"].toInt();
    map["grandparentTitle"]= item["SeriesName"].toString().isEmpty()
                                ? item["Album"].toString()
                                : item["SeriesName"].toString();
    map["imageTag"]        = imageTags["Primary"].toString();
    map["mediaSourceId"]   = mediaSource["Id"].toString();
    map["audioStreams"]    = audioStreams;
    map["subtitleStreams"]= subtitleStreams;
    return map;
}

// ---------------------------------------------------------------------------
// URL helpers
// ---------------------------------------------------------------------------

QString JellyfinBackend::buildImageUrl(const QString &itemId, const QString &imageType,
                                       const QString &imageTag, int width, int height) const {
    if (m_serverUrl.isEmpty() || m_accessToken.isEmpty())
        return QString();

    QString url = m_serverUrl + "/Items/" + itemId + "/Images/" + imageType
                + "?api_key=" + m_accessToken;
    if (!imageTag.isEmpty())
        url += "&tag=" + imageTag;
    if (width > 0)
        url += "&fillWidth=" + QString::number(width);
    if (height > 0)
        url += "&fillHeight=" + QString::number(height);
    return url;
}

QString JellyfinBackend::image_url(const QString &itemId, const QString &imageType, int width, int height) {
    return buildImageUrl(itemId, imageType, QString(), width, height);
}

// ---------------------------------------------------------------------------
// Browse
// ---------------------------------------------------------------------------

void JellyfinBackend::load_libraries() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Views");
    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD LIBRARIES FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList libraries;
        for (const QJsonValue &v : items) {
            QJsonObject item = v.toObject();
            libraries.append(QVariantMap{
                {"key",            item["Id"].toString()},
                {"itemId",         item["Id"].toString()},
                {"title",          item["Name"].toString().toUpper()},
                {"collectionType", item["CollectionType"].toString()},
            });
        }
        emit librariesLoaded(libraries);
    });
}

void JellyfinBackend::load_items(const QString &parentId, const QString &includeTypes, const QString &sortBy) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items");
    QUrlQuery q;
    q.addQueryItem("parentId", parentId);
    q.addQueryItem("recursive", "true");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    if (!includeTypes.isEmpty())
        q.addQueryItem("includeItemTypes", includeTypes);
    if (!sortBy.isEmpty()) {
        q.addQueryItem("sortBy", sortBy);
        q.addQueryItem("sortOrder", "Ascending");
    }
    q.addQueryItem("limit", "500");
    url.setQuery(q);

    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD ITEMS FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit itemsLoaded(result);
    });
}

void JellyfinBackend::load_item_detail(const QString &itemId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items/" + itemId);
    QUrlQuery q;
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    url.setQuery(q);

    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD ITEM DETAIL FAILED: " + reply->errorString());
            return;
        }

        QJsonObject item = QJsonDocument::fromJson(reply->readAll()).object();
        emit itemLoaded(formatItem(item));
    });
}

void JellyfinBackend::load_children(const QString &itemId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items");
    QUrlQuery q;
    q.addQueryItem("parentId", itemId);
    q.addQueryItem("recursive", "false");
    q.addQueryItem("includeItemTypes", "Season,Episode");
    q.addQueryItem("limit", "500");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    q.addQueryItem("sortBy", "SortName");
    q.addQueryItem("sortOrder", "Ascending");
    url.setQuery(q);

    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD CHILDREN FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit childrenLoaded(result);
    });
}

void JellyfinBackend::load_seasons(const QString &seriesId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Shows/" + seriesId + "/Seasons");
    QUrlQuery q;
    q.addQueryItem("userId", m_userId);
    q.addQueryItem("fields", "Overview,MediaSources,MediaStreams,UserData");
    q.addQueryItem("enableUserData", "true");
    url.setQuery(q);
    // [dev] qDebug("[JellyfinBackend] load_seasons series=%s", qPrintable(seriesId));

    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD SEASONS FAILED: " + reply->errorString());
            return;
        }
        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        // [dev] qDebug("[JellyfinBackend] load_seasons got %d seasons", items.size());
        emit seasonsLoaded(result);
    });
}

void JellyfinBackend::load_episodes(const QString &seriesId, const QString &seasonId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Shows/" + seriesId + "/Episodes");
    QUrlQuery q;
    q.addQueryItem("userId", m_userId);
    q.addQueryItem("seasonId", seasonId);
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    q.addQueryItem("enableUserData", "true");
    q.addQueryItem("limit", "500");
    q.addQueryItem("sortBy", "AiredEpisodeOrder");
    url.setQuery(q);
    // [dev] qDebug("[JellyfinBackend] load_episodes series=%s season=%s", qPrintable(seriesId), qPrintable(seasonId));

    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD EPISODES FAILED: " + reply->errorString());
            return;
        }
        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        // [dev] qDebug("[JellyfinBackend] load_episodes got %d episodes", items.size());
        emit episodesLoaded(result);
    });
}

void JellyfinBackend::load_continue_watching() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items/Resume");
    QUrlQuery q;
    q.addQueryItem("limit", "20");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    url.setQuery(q);

    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD CONTINUE WATCHING FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit continueWatchingLoaded(result);
    });
}

void JellyfinBackend::load_up_next() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Shows/NextUp");
    QUrlQuery q;
    q.addQueryItem("userId", m_userId);
    q.addQueryItem("limit", "20");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    q.addQueryItem("enableUserData", "true");
    url.setQuery(q);

    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD UP NEXT FAILED: " + reply->errorString());
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit upNextLoaded(result);
    });
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void JellyfinBackend::get_playback_url(const QString &itemId, const QString &mediaSourceId,
                                       int audioStreamIndex, int subtitleStreamIndex) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Items/" + itemId + "/PlaybackInfo");
    QJsonObject body;
    body["UserId"]                 = m_userId;
    body["MediaSourceId"]          = mediaSourceId;
    if (audioStreamIndex >= 0)
        body["AudioStreamIndex"]   = audioStreamIndex;
    if (subtitleStreamIndex >= 0)
        body["SubtitleStreamIndex"]= subtitleStreamIndex;
    body["MaxStreamingBitrate"]    = videoQualityBitrate();
    body["MaxHeight"]              = videoQualityMaxHeight();
    body["EnableDirectPlay"]       = false;
    body["EnableDirectStream"]     = false;

    // Minimal device profile — tells the server we need HLS transcode
    QJsonObject profile;
    QJsonArray transcodingProfiles;
    QJsonObject tp;
    tp["Container"]  = QStringLiteral("ts");
    tp["Type"]       = QStringLiteral("Video");
    tp["VideoCodec"]  = QStringLiteral("h264");
    tp["AudioCodec"]  = QStringLiteral("aac,mp3");
    tp["Protocol"]    = QStringLiteral("hls");
    transcodingProfiles.append(tp);
    profile["TranscodingProfiles"] = transcodingProfiles;
    QJsonArray subtitleProfiles;
    QJsonObject sp;
    sp["Format"] = QStringLiteral("vtt");
    sp["Method"] = QStringLiteral("Hls");
    subtitleProfiles.append(sp);
    QJsonObject sp2;
    sp2["Format"] = QStringLiteral("ass");
    sp2["Method"] = QStringLiteral("External");
    subtitleProfiles.append(sp2);
    // Burn-in fallback for image-based subtitles (PGS, DVB, DVDSUB)
    auto addBurnin = [&](const char *fmt) {
        QJsonObject s;
        s["Format"] = QString::fromLatin1(fmt);
        s["Method"] = QStringLiteral("Encode");
        subtitleProfiles.append(s);
    };
    addBurnin("pgssub");
    addBurnin("dvbsub");
    addBurnin("dvdsub");
    addBurnin("subrip");
    profile["SubtitleProfiles"] = subtitleProfiles;
    profile["DirectPlayProfiles"] = QJsonArray();
    body["DeviceProfile"] = profile;

    auto *reply = jellyfinPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    // [dev] qDebug("[JellyfinBackend] PlaybackInfo POST %s audio=%d sub=%d bitrate=%d",
    // [dev]        qPrintable(itemId), audioStreamIndex, subtitleStreamIndex, videoQualityBitrate());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId, audioStreamIndex, subtitleStreamIndex]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("PLAYBACK INFO FAILED: " + reply->errorString());
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonArray sources = data["MediaSources"].toArray();
        if (sources.isEmpty()) {
            emit errorOccurred("NO PLAYABLE SOURCE");
            return;
        }

        QJsonObject source = sources[0].toObject();
        QString transcodeUrl = source["TranscodingUrl"].toString();
        if (transcodeUrl.isEmpty()) {
            emit errorOccurred("NO TRANSCODE URL");
            return;
        }

        // Build the full URL from the TranscodingUrl, fixing:
        // 1. Only one api_key token
        // 2. Fresh PlaySessionId to force a new transcode
        // 3. SubtitleStreamIndex appended for subtitle selection
        QString freshPlaySessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString fullUrl = m_serverUrl + transcodeUrl;

        // Replace the old PlaySessionId with a fresh one
        fullUrl.replace(QRegularExpression("PlaySessionId=[^&]+"),
                        "PlaySessionId=" + freshPlaySessionId);

        // Append api_key only if not already present
        if (fullUrl.indexOf("apikey=", 0, Qt::CaseInsensitive) < 0)
            fullUrl += (fullUrl.contains('?') ? "&" : "?") + QString("api_key=") + m_accessToken;

        // Append subtitle stream index + force burn-in (handles PGS/DVB)
        if (subtitleStreamIndex >= 0) {
            fullUrl += "&SubtitleStreamIndex=" + QString::number(subtitleStreamIndex);
            fullUrl += "&SubtitleMethod=Encode";
        }

        // [dev] qDebug("[JellyfinBackend] PlaybackInfo URL ready audio=%d sub=%d psId=%s",
        // [dev]        audioStreamIndex, subtitleStreamIndex, qPrintable(freshPlaySessionId.left(8)));
        emit streamUrlReady(fullUrl);
    });
}

void JellyfinBackend::load_next_episode(const QString &currentItemId) {
    if (!has_auth()) {
        emit nextEpisodeReady(QVariantMap{});
        return;
    }

    // Step 1: fetch current episode to get seriesId + episode position
    QUrl detailUrl(m_serverUrl + "/Users/" + m_userId + "/Items/" + currentItemId);
    QUrlQuery detailQ;
    detailQ.addQueryItem("fields", "MediaSources");
    detailUrl.setQuery(detailQ);

    auto *detailReply = jellyfinGet(detailUrl);
    connect(detailReply, &QNetworkReply::finished, this, [this, detailReply]() {
        detailReply->deleteLater();
        if (detailReply->error() != QNetworkReply::NoError) {
            emit nextEpisodeReady(QVariantMap{});
            return;
        }
        QJsonObject item = QJsonDocument::fromJson(detailReply->readAll()).object();
        QString seriesId      = item["SeriesId"].toString();
        int     currentIndex  = item["IndexNumber"].toInt();
        int     currentSeason = item["ParentIndexNumber"].toInt();

        if (seriesId.isEmpty() || item["Type"].toString() != QLatin1String("Episode")) {
            emit nextEpisodeReady(QVariantMap{});
            return;
        }

        // Step 2: fetch all episodes for the series, sorted by air order
        QUrl epUrl(m_serverUrl + "/Shows/" + seriesId + "/Episodes");
        QUrlQuery epQ;
        epQ.addQueryItem("userId", m_userId);
        epQ.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
        epQ.addQueryItem("enableUserData", "true");
        epQ.addQueryItem("limit", "500");
        epQ.addQueryItem("sortBy", "AiredEpisodeOrder");
        epUrl.setQuery(epQ);

        auto *epReply = jellyfinGet(epUrl);
        connect(epReply, &QNetworkReply::finished, this,
                [this, epReply, currentIndex, currentSeason]() {
            epReply->deleteLater();
            if (epReply->error() != QNetworkReply::NoError) {
                emit nextEpisodeReady(QVariantMap{});
                return;
            }
            QJsonArray episodes = QJsonDocument::fromJson(epReply->readAll())
                                      .object()["Items"].toArray();

            // Find the next episode: smallest (season > currentSeason) or
            // (same season, episode index > currentIndex).
            QJsonObject nextEp;
            int nextSeason = 0;
            int nextIndex  = 0;
            for (const auto &ev : episodes) {
                QJsonObject e = ev.toObject();
                int s = e["ParentIndexNumber"].toInt();
                int i = e["IndexNumber"].toInt();

                if (s > currentSeason || (s == currentSeason && i > currentIndex)) {
                    if (nextEp.isEmpty() || s < nextSeason ||
                        (s == nextSeason && i < nextIndex)) {
                        nextEp     = e;
                        nextSeason = s;
                        nextIndex  = i;
                    }
                }
            }

            if (nextEp.isEmpty()) {
                emit nextEpisodeReady(QVariantMap{});
                return;
            }
            emit nextEpisodeReady(formatItem(nextEp));
        });
    });
}

void JellyfinBackend::report_playback_start(const QString &itemId, const QString &mediaSourceId,
                                            const QString &audioStreamId, const QString &subtitleStreamId,
                                            qint64 startPositionTicks) {
    if (!has_auth()) return;

    m_currentPlaySessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject body;
    body["ItemId"]            = itemId;
    body["MediaSourceId"]     = mediaSourceId;
    body["PlaySessionId"]     = m_currentPlaySessionId;
    body["PlayMethod"]        = QStringLiteral("Transcode");
    body["IsPaused"]          = false;
    body["CanSeek"]           = true;
    if (startPositionTicks > 0)
        body["StartPositionTicks"] = startPositionTicks;
    if (!audioStreamId.isEmpty())
        body["AudioStreamIndex"] = audioStreamId.toInt();
    if (!subtitleStreamId.isEmpty())
        body["SubtitleStreamIndex"] = subtitleStreamId.toInt();

    QUrl url(m_serverUrl + "/Sessions/Playing");
    auto *reply = jellyfinPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void JellyfinBackend::update_playback_progress(const QString &itemId, const QString &mediaSourceId,
                                               qint64 positionTicks, bool isPaused) {
    if (!has_auth()) return;

    QJsonObject body;
    body["ItemId"]         = itemId;
    body["MediaSourceId"]  = mediaSourceId;
    body["PositionTicks"]  = positionTicks;
    body["IsPaused"]       = isPaused;
    body["PlayMethod"]     = QStringLiteral("Transcode");
    body["CanSeek"]        = true;
    if (!m_currentPlaySessionId.isEmpty())
        body["PlaySessionId"] = m_currentPlaySessionId;

    QUrl url(m_serverUrl + "/Sessions/Playing/Progress");
    auto *reply = jellyfinPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void JellyfinBackend::report_playback_stopped(const QString &itemId, const QString &mediaSourceId,
                                              qint64 positionTicks, bool failed) {
    if (!has_auth()) return;

    QJsonObject body;
    body["ItemId"]        = itemId;
    body["MediaSourceId"] = mediaSourceId;
    body["PositionTicks"] = positionTicks;
    body["PlayMethod"]    = QStringLiteral("Transcode");
    body["Failed"]        = failed;
    if (!m_currentPlaySessionId.isEmpty())
        body["PlaySessionId"] = m_currentPlaySessionId;

    QUrl url(m_serverUrl + "/Sessions/Playing/Stopped");
    auto *reply = jellyfinPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_currentPlaySessionId.clear();
    });
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void JellyfinBackend::getLibraries() {
    if (!has_auth()) {
        emit dynamicOptionsReady("libraries", QVariantList());
        return;
    }

    QJsonObject libCfg = moduleConfig()["libraries"].toObject();

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Views");
    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, libCfg]() {
        reply->deleteLater();
        QVariantList options;
        if (reply->error() != QNetworkReply::NoError) {
            emit dynamicOptionsReady("libraries", options);
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        for (const QJsonValue &v : items) {
            QJsonObject item = v.toObject();
            QString id = item["Id"].toString();
            bool selected = libCfg[id].toBool(true);
            options.append(QVariantMap{
                {"id",      id},
                {"label",   item["Name"].toString().toUpper()},
            });
        }
        emit dynamicOptionsReady("libraries", options);
    });
}

void JellyfinBackend::getVideoQualities() {
    QVariantList options;
    auto add = [&](const QString &value, const QString &label) {
        QVariantMap m;
        m["id"]    = value;
        m["label"] = label;
        options.append(m);
    };
    add("480p",  "480p (NTSC CRT)");
    add("576p",  "576p (PAL CRT)");
    add("720p",  "720p");
    add("1080p", "1080p");
    emit dynamicOptionsReady("video_quality", options);
}

void JellyfinBackend::get_resume_playback_options() {
    QString current = resumePlaybackMode();
    QVariantList options;
    auto add = [&](const QString &value, const QString &label) {
        QVariantMap m;
        m["id"]    = value;
        m["label"] = label;
        options.append(m);
    };
    add("ask",   "Ask");
    add("always","Always");
    add("never", "Never");
    emit dynamicOptionsReady("resume_playback", options);
}

void JellyfinBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value) {
    if (moduleId != kModuleId)
        return;

    if (key == QLatin1String("server_url")) {
        m_serverUrl = normalizeServerUrl(value.toString());
    }
}

void JellyfinBackend::save_to_server(const QString &audioLang, const QString &subLang) {
    if (!has_auth()) return;
    // [dev] qDebug("[JellyfinBackend] save_to_server audio=%s sub=%s",
    // [dev]        qPrintable(audioLang), qPrintable(subLang));

    // Fetch current config, update only our fields, POST full config back.
    QUrl getUrl(m_serverUrl + "/Users/" + m_userId);
    auto *getReply = jellyfinGet(getUrl);
    connect(getReply, &QNetworkReply::finished, this, [this, getReply, audioLang, subLang]() {
        getReply->deleteLater();
        if (getReply->error() != QNetworkReply::NoError) return;
        QJsonObject user = QJsonDocument::fromJson(getReply->readAll()).object();
        QJsonObject config = user["Configuration"].toObject();
        if (!audioLang.isEmpty())
            config["AudioLanguagePreference"] = audioLang;
        if (!subLang.isEmpty())
            config["SubtitleLanguagePreference"] = subLang;

        QUrl postUrl(m_serverUrl + "/Users/" + m_userId + "/Configuration");
        auto *postReply = jellyfinPost(postUrl, QJsonDocument(config).toJson(QJsonDocument::Compact));
        connect(postReply, &QNetworkReply::finished, postReply, &QNetworkReply::deleteLater);
    });
}

void JellyfinBackend::load_server_preferences() {
    if (!has_auth()) {
        emit serverLanguagePreferencesReady(QString(), QString());
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId);
    auto *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit serverLanguagePreferencesReady(QString(), QString());
            return;
        }
        QJsonObject userData = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonObject config = userData["Configuration"].toObject();
        QString audioLang = config["AudioLanguagePreference"].toString();
        QString subLang   = config["SubtitleLanguagePreference"].toString();
        // [dev] qDebug("[JellyfinBackend] Server prefs: audio=%s sub=%s",
        // [dev]        qPrintable(audioLang), qPrintable(subLang));
        emit serverLanguagePreferencesReady(audioLang, subLang);
    });
}
