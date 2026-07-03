// Comprehensive Qt6 unit tests for JellyfinBackend.
//
// These tests exercise the Jellyfin module's C++ backend (src/modules/jellyfin/)
// against a MockNetworkAccessManager so no real HTTP is performed. Pure helper
// functions (normalizeServerUrl, formatItem, buildImageUrl, video-quality
// tables) are tested directly via the UNIT_TEST friend declaration; HTTP-driven
// flows are tested by queueing expectations, invoking the Q_INVOKABLE method,
// and waiting for the backend's signals.
//
// Build: cmake -B build -DBUILD_TESTS=ON . && cmake --build build
// Run:   ./build/test_jellyfin

#include "modules/jellyfin/JellyfinBackend.h"
#include "mocks/MockNetworkAccessManager.h"
#include "mocks/FakeNetworkReply.h"
#include "TestHelpers.h"

#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QFile>
#include <QVariantList>
#include <QVariantMap>
#include <QCoreApplication>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// Spec vs. implementation notes
// ─────────────────────────────────────────────────────────────────────────────
// The task spec listed several backend members/methods that the *actual*
// JellyfinBackend does not expose, and a couple of free functions that are
// file-static in the .cpp (so the UNIT_TEST friend declaration can't reach
// them). Writing calls to symbols that don't exist would break compilation, so
// those test cases are omitted here (see the project summary for the full
// list). What remains tests the real, compiled backend end-to-end.
//
// Concretely omitted because they don't exist on the backend:
//   - test_fetchSegments_*            (no fetchSegments())
//   - test_probeCapabilities_*        (no probeCapabilities(), m_capabilitiesProbed,
//                                      m_hasCapability)
//   - test_setLastTrackLangs /        (no set_last_track_langs/get_last_*_lang*,
//     test_getLastTrackLangs            m_lastAudioLang/m_lastSubLang/...idx)
//
// Tested indirectly instead of via the (inaccessible) file-static functions:
//   - test_authHeaderValue            (authHeaderValue() is file-static in the
//                                      .cpp → verified via Authorization headers
//                                      on real requests)
//   - test_filterExpectedSslErrors    (filterExpectedSslErrors() is file-static;
//                                      would need live SSL errors to exercise,
//                                      so the data-driven direct test is dropped)

class JellyfinBackendTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ─── Pure function tests (no mocking) ───────────────────────────────
    void test_normalizeServerUrl();
    void test_normalizeServerUrl_data();

    void test_videoQualityBitrate();
    void test_videoQualityBitrate_data();

    void test_videoQualityMaxHeight();
    void test_videoQualityMaxHeight_data();

    void test_formatItem_movie();
    void test_formatItem_episode();
    void test_formatItem_streams();
    void test_formatItem_edgeCases();
    void test_formatItem_genres();

    void test_buildImageUrl();
    void test_buildImageUrl_data();

    void test_authHeaderValue();

    // ─── Auth persistence tests (QTemporaryDir) ─────────────────────────
    void test_authRoundTrip();
    void test_authMissingFile();
    void test_authCorruptFile();
    void test_clearAuthState();

    // ─── Config tests ───────────────────────────────────────────────────
    void test_configRoundTrip();
    void test_moduleConfig();
    void test_onSettingChanged();

    // ─── HTTP-mocked auth tests ─────────────────────────────────────────
    void test_hasAuth();
    void test_checkAuth_valid();
    void test_checkAuth_expired();
    void test_checkAuth_noAuth();
    void test_logout();
    void test_logout_revokesToken();

    // ─── Quick Connect tests ────────────────────────────────────────────
    void test_quickConnectInitiate_success();
    void test_quickConnectInitiate_emptyUrl();
    void test_quickConnectInitiate_401();
    void test_quickConnectPoll_approved();
    void test_quickConnectPoll_404();
    void test_quickConnectPoll_networkError();
    void test_quickConnectAuthenticate_success();
    void test_quickConnectAuthenticate_emptyBody();
    void test_quickConnectCancel();

    // ─── Browse tests ───────────────────────────────────────────────────
    void test_loadLibraries_withShelves();
    void test_loadLibraries_emptyLibraries();
    void test_loadLibraries_noAuth();
    void test_loadItems_success();
    void test_loadItems_empty();
    void test_loadItems_noAuth();
    void test_loadItemDetail_success();
    void test_loadItemDetail_error();
    void test_loadContinueWatching();
    void test_loadUpNext();

    // ─── Playback tests ─────────────────────────────────────────────────
    void test_getPlaybackUrl_directPlay();
    void test_getPlaybackUrl_transcode();
    void test_getPlaybackUrl_forceTranscode();
    void test_getPlaybackUrl_noSource();
    void test_getPlaybackUrl_noAuth();
    void test_loadNextEpisode_success();
    void test_loadNextEpisode_noNext();
    void test_loadNextEpisode_notAnEpisode();
    void test_reportPlaybackStart();
    void test_updatePlaybackProgress();
    void test_reportPlaybackStopped();

    // ─── Language preferences ───────────────────────────────────────────
    void test_loadServerPreferences();

private:
    // Auth/config constants matching TestHelpers::seedAuth defaults.
    static constexpr const char *kDefaultServer = "http://192.168.1.100:8096";
    static constexpr const char *kToken         = "test-token-123";
    static constexpr const char *kUser          = "user-abc";
    static constexpr const char *kDevice        = "device-456";

    // Owns a backend, its mock NAM, and the temp data dir for one test.
    // std::unique_ptr (not QScopedPointer) so the struct is movable and can be
    // returned by value from the make* helpers.
    struct TestContext {
        std::unique_ptr<QTemporaryDir> dir;
        std::unique_ptr<MockNetworkAccessManager> mockNam;
        std::unique_ptr<JellyfinBackend> backend;
    };

    TestContext makeAuthedBackend(const QString &serverUrl = QString::fromLatin1(kDefaultServer));
    TestContext makeUnauthedBackend();
    TestContext makeBackendWithConfig(const QJsonObject &configOverrides);
    TestContext makeAuthedBackendWithConfig(const QJsonObject &configOverrides,
                                            const QString &serverUrl = QString::fromLatin1(kDefaultServer));
    TestContext makeBackend(bool auth, const QString &serverUrl, const QJsonObject &configOverrides);
};

// ─── fixture / helper impl ───────────────────────────────────────────────────

JellyfinBackendTest::TestContext JellyfinBackendTest::makeBackend(bool auth, const QString &serverUrl, const QJsonObject &configOverrides)
{
    TestContext ctx;
    ctx.dir.reset(TestHelpers::createTempDir());
    ctx.mockNam.reset(new MockNetworkAccessManager);
    if (!configOverrides.isEmpty())
        TestHelpers::seedConfig(ctx.dir.get(), QStringLiteral("com.240mp.jellyfin"), configOverrides);
    if (auth)
        TestHelpers::seedAuth(ctx.dir.get(), serverUrl);
    ctx.backend.reset(new JellyfinBackend(ctx.dir->path(), ctx.dir->path(), ctx.mockNam.get()));
    return ctx;
}

JellyfinBackendTest::TestContext JellyfinBackendTest::makeAuthedBackend(const QString &serverUrl)
{
    return makeBackend(true, serverUrl, {});
}

JellyfinBackendTest::TestContext JellyfinBackendTest::makeUnauthedBackend()
{
    return makeBackend(false, QString::fromLatin1(kDefaultServer), {});
}

JellyfinBackendTest::TestContext JellyfinBackendTest::makeBackendWithConfig(const QJsonObject &configOverrides)
{
    return makeBackend(false, QString::fromLatin1(kDefaultServer), configOverrides);
}

JellyfinBackendTest::TestContext JellyfinBackendTest::makeAuthedBackendWithConfig(const QJsonObject &configOverrides, const QString &serverUrl)
{
    return makeBackend(true, serverUrl, configOverrides);
}

void JellyfinBackendTest::initTestCase()
{
    // authHeaderValue() embeds QCoreApplication::applicationVersion(); pin it so
    // the Authorization-header assertions are deterministic.
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));
}

// ─── Pure function tests ─────────────────────────────────────────────────────

void JellyfinBackendTest::test_normalizeServerUrl_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("empty")          << "" << "";
    QTest::newRow("trailing slash") << "http://server:8096/" << "http://server:8096";
    QTest::newRow("no slash")       << "http://server:8096" << "http://server:8096";
    QTest::newRow("multi slash")    << "http://server:8096///" << "http://server:8096";
    QTest::newRow("trim + slash")   << "  http://server:8096/  " << "http://server:8096";
}

void JellyfinBackendTest::test_normalizeServerUrl()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(JellyfinBackend::normalizeServerUrl(input), expected);
}

void JellyfinBackendTest::test_videoQualityBitrate_data()
{
    QTest::addColumn<QString>("quality");
    QTest::addColumn<int>("expected");
    QTest::newRow("auto")    << "auto"    << 0;
    QTest::newRow("1080p")   << "1080p"   << 10000000;
    QTest::newRow("720p")    << "720p"    << 6000000;
    QTest::newRow("576p")    << "576p"    << 4500000;
    QTest::newRow("480p")    << "480p"    << 4000000;
    QTest::newRow("empty")   << ""        << 4000000;
    QTest::newRow("garbage") << "garbage" << 4000000;
}

void JellyfinBackendTest::test_videoQualityBitrate()
{
    QFETCH(QString, quality);
    QFETCH(int, expected);
    QScopedPointer<QTemporaryDir> dir(TestHelpers::createTempDir());
    QScopedPointer<MockNetworkAccessManager> nam(new MockNetworkAccessManager);
    TestHelpers::seedConfig(dir.data(), QStringLiteral("com.240mp.jellyfin"),
                            QJsonObject{{"video_quality", quality}});
    JellyfinBackend b(dir->path(), dir->path(), nam.data());
    QCOMPARE(b.videoQualityBitrate(), expected);
}

void JellyfinBackendTest::test_videoQualityMaxHeight_data()
{
    QTest::addColumn<QString>("quality");
    QTest::addColumn<int>("expected");
    QTest::newRow("auto")    << "auto"    << 0;
    QTest::newRow("1080p")   << "1080p"   << 1080;
    QTest::newRow("720p")    << "720p"    << 720;
    QTest::newRow("576p")    << "576p"    << 576;
    QTest::newRow("480p")    << "480p"    << 480;
    QTest::newRow("empty")   << ""        << 480;
    QTest::newRow("garbage") << "garbage" << 480;
}

void JellyfinBackendTest::test_videoQualityMaxHeight()
{
    QFETCH(QString, quality);
    QFETCH(int, expected);
    QScopedPointer<QTemporaryDir> dir(TestHelpers::createTempDir());
    QScopedPointer<MockNetworkAccessManager> nam(new MockNetworkAccessManager);
    TestHelpers::seedConfig(dir.data(), QStringLiteral("com.240mp.jellyfin"),
                            QJsonObject{{"video_quality", quality}});
    JellyfinBackend b(dir->path(), dir->path(), nam.data());
    QCOMPARE(b.videoQualityMaxHeight(), expected);
}

void JellyfinBackendTest::test_formatItem_movie()
{
    auto ctx = makeAuthedBackend();
    QJsonObject item = TestHelpers::makeMovieItem("movie-1", "Test Movie", 2024, "An overview.");
    item["ProductionYear"] = 2024; // formatItem reads ProductionYear (not Year)
    QJsonObject userData;
    userData["PlaybackPositionTicks"] = qint64(36000000000LL);
    userData["Played"] = false;
    item["UserData"] = userData;
    item["ImageTags"] = QJsonObject{{"Primary", "tag123"}};
    item["Genres"] = QJsonArray{"Action", "Drama"};
    item["IsFolder"] = false;

    const QVariantMap m = ctx.backend->formatItem(item);
    QCOMPARE(m.value("itemId").toString(), QStringLiteral("movie-1"));
    QCOMPARE(m.value("title").toString(), QStringLiteral("Test Movie"));
    QCOMPARE(m.value("type").toString(), QStringLiteral("movie"));
    QCOMPARE(m.value("overview").toString(), QStringLiteral("An overview."));
    QCOMPARE(m.value("year").toInt(), 2024);
    QCOMPARE(m.value("duration").toLongLong(), qint64(72000000000LL / 10000));
    QCOMPARE(m.value("viewOffset").toLongLong(), qint64(36000000000LL / 10000));
    QCOMPARE(m.value("played").toBool(), false);
    QCOMPARE(m.value("isFolder").toBool(), false);
    QCOMPARE(m.value("imageTag").toString(), QStringLiteral("tag123"));
    const QStringList genres = m.value("genres").toStringList();
    QCOMPARE(genres.size(), 2);
    QCOMPARE(genres.at(0), QStringLiteral("Action"));
    QCOMPARE(genres.at(1), QStringLiteral("Drama"));
}

void JellyfinBackendTest::test_formatItem_episode()
{
    auto ctx = makeAuthedBackend();
    QJsonObject item = TestHelpers::makeEpisodeItem("ep-1", "series-1", "Test Series", 1, 2);
    const QVariantMap m = ctx.backend->formatItem(item);
    QCOMPARE(m.value("itemId").toString(), QStringLiteral("ep-1"));
    QCOMPARE(m.value("seriesId").toString(), QStringLiteral("series-1"));
    QCOMPARE(m.value("title").toString(), QStringLiteral("Episode 2"));
    QCOMPARE(m.value("type").toString(), QStringLiteral("episode"));
    QCOMPARE(m.value("grandparentTitle").toString(), QStringLiteral("Test Series"));
    QCOMPARE(m.value("index").toInt(), 2);
    QCOMPARE(m.value("parentIndex").toInt(), 1);
}

void JellyfinBackendTest::test_formatItem_streams()
{
    auto ctx = makeAuthedBackend();
    QJsonArray streams;
    streams.append(TestHelpers::makeAudioStream(0, "eng", "aac", true));
    streams.append(TestHelpers::makeSubtitleStream(1, "eng", "subrip", false, false, true));   // text
    streams.append(TestHelpers::makeSubtitleStream(2, "eng", "pgssub", false, false, false)); // image
    QJsonObject source;
    source["Id"] = "src-1";
    source["MediaStreams"] = streams;
    QJsonObject item = TestHelpers::makeMovieItem("item-1", "Title", 2024, "");
    QJsonArray sources;
    sources.append(source);
    item["MediaSources"] = sources;

    const QVariantMap m = ctx.backend->formatItem(item);
    const QVariantList audio = m.value("audioStreams").toList();
    QCOMPARE(audio.size(), 1);
    const QVariantMap a = audio.at(0).toMap();
    QCOMPARE(a.value("id").toString(), QStringLiteral("0"));
    QCOMPARE(a.value("language").toString(), QStringLiteral("eng"));
    QCOMPARE(a.value("codec").toString(), QStringLiteral("aac"));
    QCOMPARE(a.value("channels").toString(), QStringLiteral("stereo"));
    QCOMPARE(a.value("selected").toBool(), true);
    QVERIFY(!a.value("displayTitle").toString().isEmpty());

    const QVariantList subs = m.value("subtitleStreams").toList();
    QCOMPARE(subs.size(), 2);
    const QVariantMap textSub = subs.at(0).toMap();
    QCOMPARE(textSub.value("id").toString(), QStringLiteral("1"));
    QCOMPARE(textSub.value("imageSubtitle").toBool(), false);
    QVERIFY(!textSub.value("subUrl").toString().isEmpty());
    QVERIFY(textSub.value("subUrl").toString().contains("/Videos/item-1/src-1/Subtitles/1/Stream.srt"));
    QVERIFY(textSub.value("subUrl").toString().contains("api_key=test-token-123"));
    const QVariantMap imgSub = subs.at(1).toMap();
    QCOMPARE(imgSub.value("id").toString(), QStringLiteral("2"));
    QCOMPARE(imgSub.value("imageSubtitle").toBool(), true);
    QVERIFY(imgSub.value("subUrl").toString().isEmpty());
}

void JellyfinBackendTest::test_formatItem_edgeCases()
{
    auto ctx = makeAuthedBackend();
    // Empty object — all defaults, must not crash.
    const QVariantMap m = ctx.backend->formatItem(QJsonObject());
    QVERIFY(m.value("itemId").toString().isEmpty());
    QVERIFY(m.value("title").toString().isEmpty());
    QVERIFY(m.value("type").toString().isEmpty());
    QVERIFY(m.value("audioStreams").toList().isEmpty());
    QVERIFY(m.value("subtitleStreams").toList().isEmpty());
    QVERIFY(m.value("genres").toList().isEmpty());
    QCOMPARE(m.value("played").toBool(), false);
    QCOMPARE(m.value("isFolder").toBool(), false);

    // Null values behave as empty strings.
    QJsonObject nullItem;
    nullItem["Name"] = QJsonValue::Null;
    nullItem["Type"] = QJsonValue::Null;
    const QVariantMap m2 = ctx.backend->formatItem(nullItem);
    QVERIFY(m2.value("title").toString().isEmpty());
    QVERIFY(m2.value("type").toString().isEmpty());

    // Empty arrays stay empty.
    QJsonObject emptyArrays;
    emptyArrays["Genres"] = QJsonArray();
    emptyArrays["MediaSources"] = QJsonArray();
    const QVariantMap m3 = ctx.backend->formatItem(emptyArrays);
    QVERIFY(m3.value("genres").toList().isEmpty());
    QVERIFY(m3.value("audioStreams").toList().isEmpty());
}

void JellyfinBackendTest::test_formatItem_genres()
{
    auto ctx = makeAuthedBackend();
    QJsonObject item = TestHelpers::makeMovieItem("g1", "G", 2024, "");
    item["Genres"] = QJsonArray{"Sci-Fi", "Thriller", "Drama"};
    const QVariantMap m = ctx.backend->formatItem(item);
    const QStringList genres = m.value("genres").toStringList();
    QCOMPARE(genres.size(), 3);
    QCOMPARE(genres.at(0), QStringLiteral("Sci-Fi"));
    QCOMPARE(genres.at(1), QStringLiteral("Thriller"));
    QCOMPARE(genres.at(2), QStringLiteral("Drama"));
}

void JellyfinBackendTest::test_buildImageUrl_data()
{
    QTest::addColumn<QString>("itemId");
    QTest::addColumn<QString>("imageType");
    QTest::addColumn<QString>("imageTag");
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::addColumn<bool>("noAuth");
    QTest::addColumn<QString>("expected");

    const QString base = QStringLiteral("http://192.168.1.100:8096/Items/i1/Images/Primary?api_key=test-token-123");
    QTest::newRow("all params") << "i1" << "Primary" << "tag1" << 200 << 300 << false
                                << base + QStringLiteral("&tag=tag1&fillWidth=200&fillHeight=300");
    QTest::newRow("empty tag")  << "i1" << "Primary" << ""     << 200 << 300 << false
                                << base + QStringLiteral("&fillWidth=200&fillHeight=300");
    QTest::newRow("zero size")  << "i1" << "Primary" << "tag1" << 0   << 0   << false
                                << base + QStringLiteral("&tag=tag1");
    QTest::newRow("no auth")    << "i1" << "Primary" << "tag1" << 200 << 300 << true
                                << QString();
}

void JellyfinBackendTest::test_buildImageUrl()
{
    QFETCH(QString, itemId);
    QFETCH(QString, imageType);
    QFETCH(QString, imageTag);
    QFETCH(int, width);
    QFETCH(int, height);
    QFETCH(bool, noAuth);
    QFETCH(QString, expected);
    auto ctx = noAuth ? makeUnauthedBackend() : makeAuthedBackend();
    QCOMPARE(ctx.backend->buildImageUrl(itemId, imageType, imageTag, width, height), expected);
    QVERIFY(ctx.mockNam->isDone()); // buildImageUrl never issues HTTP
}

void JellyfinBackendTest::test_authHeaderValue()
{
    // authHeaderValue() is a file-static free function in JellyfinBackend.cpp —
    // the UNIT_TEST friend declaration only exposes *members*, so it can't be
    // called directly. Verify its output by inspecting the Authorization header
    // on requests the backend actually issues.
    {
        // With a token (authed → every request carries Token="...").
        auto ctx = makeAuthedBackend();
        ctx.mockNam->expectGet(QUrl(QStringLiteral("http://192.168.1.100:8096/Users/user-abc")),
                               new FakeNetworkReply("{}", 200));
        QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::authStateChanged);
        ctx.backend->check_auth();
        QVERIFY(TestHelpers::waitForSignal(spy));
        QCOMPARE(ctx.mockNam->capturedRequests().size(), 1);
        const QString auth = ctx.mockNam->capturedRequests().constFirst().authorization;
        QVERIFY(auth.startsWith(QStringLiteral("MediaBrowser Client=\"240-MP\"")));
        QVERIFY(auth.contains(QStringLiteral("DeviceId=\"device-456\"")));
        QVERIFY(auth.contains(QStringLiteral("Version=\"1.0\"")));
        QVERIFY(auth.contains(QStringLiteral("Token=\"test-token-123\"")));
        QVERIFY(ctx.mockNam->isDone());
    }
    {
        // Without a token (Quick Connect initiate posts with an empty token).
        auto ctx = makeUnauthedBackend();
        ctx.mockNam->expectPost(QUrl(QStringLiteral("http://server:8096/QuickConnect/Initiate")),
                                QByteArray(),
                                new FakeNetworkReply(R"({"Secret":"s","Code":"C"})", 200));
        QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::quickConnectCodeReady);
        ctx.backend->quick_connect_initiate(QStringLiteral("http://server:8096"));
        QVERIFY(TestHelpers::waitForSignal(spy));
        QCOMPARE(ctx.mockNam->capturedRequests().size(), 1);
        const QString auth = ctx.mockNam->capturedRequests().constFirst().authorization;
        QVERIFY(auth.startsWith(QStringLiteral("MediaBrowser Client=\"240-MP\"")));
        QVERIFY(!auth.contains(QStringLiteral("Token=")));
        QVERIFY(ctx.mockNam->isDone());
    }
}

// ─── Auth persistence tests ──────────────────────────────────────────────────

void JellyfinBackendTest::test_authRoundTrip()
{
    QScopedPointer<QTemporaryDir> dir(TestHelpers::createTempDir());
    QScopedPointer<MockNetworkAccessManager> nam1(new MockNetworkAccessManager);
    {
        JellyfinBackend b1(dir->path(), dir->path(), nam1.data());
        b1.m_serverUrl   = QStringLiteral("http://roundtrip:8096");
        b1.m_accessToken = QStringLiteral("rt-token");
        b1.m_userId      = QStringLiteral("rt-user");
        b1.m_userName    = QStringLiteral("rt-name");
        b1.m_serverName  = QStringLiteral("RT Server");
        b1.m_deviceId    = QStringLiteral("rt-device");
        b1.saveAuthState();
    }
    // A new backend pointed at the same data dir must reload everything.
    QScopedPointer<MockNetworkAccessManager> nam2(new MockNetworkAccessManager);
    JellyfinBackend b2(dir->path(), dir->path(), nam2.data());
    QCOMPARE(b2.m_serverUrl,   QStringLiteral("http://roundtrip:8096"));
    QCOMPARE(b2.m_accessToken, QStringLiteral("rt-token"));
    QCOMPARE(b2.m_userId,      QStringLiteral("rt-user"));
    QCOMPARE(b2.m_userName,    QStringLiteral("rt-name"));
    QCOMPARE(b2.m_serverName,  QStringLiteral("RT Server"));
    QCOMPARE(b2.m_deviceId,    QStringLiteral("rt-device"));
    QVERIFY(b2.has_auth());
}

void JellyfinBackendTest::test_authMissingFile()
{
    auto ctx = makeUnauthedBackend();
    QVERIFY(!ctx.backend->has_auth());
    QVERIFY(ctx.backend->m_accessToken.isEmpty());
    QVERIFY(ctx.backend->m_serverUrl.isEmpty());
    QVERIFY(ctx.backend->m_userId.isEmpty());
    // deviceId is generated on construction even without auth.
    QVERIFY(!ctx.backend->m_deviceId.isEmpty());
}

void JellyfinBackendTest::test_authCorruptFile()
{
    QScopedPointer<QTemporaryDir> dir(TestHelpers::createTempDir());
    {
        QFile f(dir->path() + "/jellyfin_auth.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ this is not valid json !!!");
        f.close();
    }
    QScopedPointer<MockNetworkAccessManager> nam(new MockNetworkAccessManager);
    JellyfinBackend b(dir->path(), dir->path(), nam.data());
    QVERIFY(!b.has_auth());
    QVERIFY(b.m_accessToken.isEmpty());
    QVERIFY(b.m_serverUrl.isEmpty());
    QVERIFY(!b.m_deviceId.isEmpty()); // still generated
}

void JellyfinBackendTest::test_clearAuthState()
{
    auto ctx = makeAuthedBackend();
    const QString oldDevice = ctx.backend->m_deviceId;
    QCOMPARE(oldDevice, QStringLiteral("device-456"));
    ctx.backend->clearAuthState();
    QVERIFY(ctx.backend->m_accessToken.isEmpty());
    QVERIFY(ctx.backend->m_serverUrl.isEmpty());
    QVERIFY(ctx.backend->m_userId.isEmpty());
    QVERIFY(ctx.backend->m_userName.isEmpty());
    QVERIFY(ctx.backend->m_serverName.isEmpty());
    QVERIFY(ctx.backend->m_currentPlaySessionId.isEmpty());
    QVERIFY(!ctx.backend->m_deviceId.isEmpty());
    QVERIFY(ctx.backend->m_deviceId != oldDevice); // rotated to a fresh UUID
    QVERIFY(!QFile::exists(ctx.dir->path() + "/jellyfin_auth.json"));
    QVERIFY(!ctx.backend->has_auth());
}

// ─── Config tests ────────────────────────────────────────────────────────────

void JellyfinBackendTest::test_configRoundTrip()
{
    auto ctx = makeBackendWithConfig(QJsonObject{{"video_quality", "720p"},
                                                 {"resume_playback", "always"}});
    const QJsonObject cfg = ctx.backend->moduleConfig();
    QCOMPARE(cfg.value("video_quality").toString(), QStringLiteral("720p"));
    QCOMPARE(cfg.value("resume_playback").toString(), QStringLiteral("always"));
    QCOMPARE(ctx.backend->videoQualityBitrate(), 6000000);
    QCOMPARE(ctx.backend->videoQualityMaxHeight(), 720);
}

void JellyfinBackendTest::test_moduleConfig()
{
    QScopedPointer<QTemporaryDir> dir(TestHelpers::createTempDir());
    QScopedPointer<MockNetworkAccessManager> nam(new MockNetworkAccessManager);
    JellyfinBackend b(dir->path(), dir->path(), nam.data()); // no config.json
    QVERIFY(b.moduleConfig().isEmpty());
    QCOMPARE(b.videoQualityBitrate(), 0);   // "auto" default → direct play, no cap
    QCOMPARE(b.videoQualityMaxHeight(), 0);
}

void JellyfinBackendTest::test_onSettingChanged()
{
    auto ctx = makeAuthedBackend(QStringLiteral("http://old:8096"));
    QCOMPARE(ctx.backend->m_serverUrl, QStringLiteral("http://old:8096"));
    ctx.backend->onSettingChanged(QStringLiteral("com.240mp.jellyfin"),
                                  QStringLiteral("server_url"),
                                  QVariant(QStringLiteral("http://new:8096")));
    QCOMPARE(ctx.backend->m_serverUrl, QStringLiteral("http://new:8096"));
    // A different module ID must be ignored.
    ctx.backend->onSettingChanged(QStringLiteral("com.240mp.wrong"),
                                  QStringLiteral("server_url"),
                                  QVariant(QStringLiteral("http://ignored:8096")));
    QCOMPARE(ctx.backend->m_serverUrl, QStringLiteral("http://new:8096"));
}

// ─── HTTP-mocked auth tests ──────────────────────────────────────────────────

void JellyfinBackendTest::test_hasAuth()
{
    auto authed = makeAuthedBackend();
    QVERIFY(authed.backend->has_auth());
    auto unauthed = makeUnauthedBackend();
    QVERIFY(!unauthed.backend->has_auth());
}

void JellyfinBackendTest::test_checkAuth_valid()
{
    auto ctx = makeAuthedBackend();
    ctx.mockNam->expectGet(QUrl(QStringLiteral("http://192.168.1.100:8096/Users/user-abc")),
                           new FakeNetworkReply("{}", 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::authStateChanged);
    ctx.backend->check_auth();
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QVERIFY(ctx.backend->has_auth());
    QVERIFY(ctx.mockNam->isDone());
    QVERIFY(!ctx.mockNam->hadUnexpectedCalls());
}

void JellyfinBackendTest::test_checkAuth_expired()
{
    auto ctx = makeAuthedBackend();
    // 401 with NoError so the backend's status==401 branch is taken (not the
    // generic network-error branch).
    ctx.mockNam->expectGet(QUrl(QStringLiteral("http://192.168.1.100:8096/Users/user-abc")),
                           new FakeNetworkReply("", 401, QNetworkReply::NoError));
    QSignalSpy spyAuth(ctx.backend.get(), &JellyfinBackend::authStateChanged);
    QSignalSpy spyRevoked(ctx.backend.get(), &JellyfinBackend::authRevoked);
    ctx.backend->check_auth();
    QVERIFY(TestHelpers::waitForSignal(spyAuth));
    QCOMPARE(spyAuth.count(), 1);
    QCOMPARE(spyRevoked.count(), 1);
    QVERIFY(!ctx.backend->has_auth());
    QVERIFY(ctx.backend->m_accessToken.isEmpty());
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_checkAuth_noAuth()
{
    auto ctx = makeUnauthedBackend();
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::authStateChanged);
    ctx.backend->check_auth();
    QCOMPARE(spy.count(), 1); // emitted synchronously — no HTTP needed
    QVERIFY(ctx.mockNam->isDone());
    QVERIFY(!ctx.mockNam->hadUnexpectedCalls());
}

void JellyfinBackendTest::test_logout()
{
    auto ctx = makeAuthedBackend();
    ctx.mockNam->expectPost(QUrl(QStringLiteral("http://192.168.1.100:8096/Sessions/Logout")),
                            QByteArray(), new FakeNetworkReply("", 200));
    QSignalSpy spyDone(ctx.backend.get(), &JellyfinBackend::logoutComplete);
    QSignalSpy spyAuth(ctx.backend.get(), &JellyfinBackend::authStateChanged);
    ctx.backend->logout();
    // logoutComplete/authStateChanged fire synchronously; the POST reply
    // finishes async, so give the loop a tick to clean it up.
    QCOMPARE(spyDone.count(), 1);
    QCOMPARE(spyAuth.count(), 1);
    TestHelpers::spinLoop(50);
    QVERIFY(!ctx.backend->has_auth());
    QVERIFY(!QFile::exists(ctx.dir->path() + "/jellyfin_auth.json"));
    QVERIFY(ctx.mockNam->isDone());
    QVERIFY(!ctx.mockNam->hadUnexpectedCalls());
}

void JellyfinBackendTest::test_logout_revokesToken()
{
    auto ctx = makeAuthedBackend();
    ctx.mockNam->expectPost(QUrl(QStringLiteral("http://192.168.1.100:8096/Sessions/Logout")),
                            QByteArray(), new FakeNetworkReply("", 200));
    ctx.backend->logout();
    TestHelpers::spinLoop(50);
    // The logout POST was issued with the token still in m_accessToken, so its
    // Authorization header must carry Token="test-token-123" — i.e. the
    // server-side revocation request happens before the token is wiped locally.
    bool foundLogout = false;
    for (const auto &c : ctx.mockNam->capturedRequests()) {
        if (c.url.path().endsWith(QStringLiteral("/Sessions/Logout"))) {
            QVERIFY(c.authorization.contains(QStringLiteral("Token=\"test-token-123\"")));
            foundLogout = true;
        }
    }
    QVERIFY(foundLogout);
    QVERIFY(ctx.backend->m_accessToken.isEmpty());
    QVERIFY(ctx.mockNam->isDone());
}

// ─── Quick Connect tests ─────────────────────────────────────────────────────

void JellyfinBackendTest::test_quickConnectInitiate_success()
{
    auto ctx = makeUnauthedBackend();
    ctx.mockNam->expectPost(QUrl(QStringLiteral("http://server:8096/QuickConnect/Initiate")),
                            QByteArray(),
                            new FakeNetworkReply(R"({"Secret":"sec-123","Code":"ABC-456"})", 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::quickConnectCodeReady);
    ctx.backend->quick_connect_initiate(QStringLiteral("http://server:8096"));
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("ABC-456"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("sec-123"));
    QCOMPARE(ctx.backend->m_quickConnectSecret, QStringLiteral("sec-123"));
    QCOMPARE(ctx.backend->m_quickConnectServerUrl, QStringLiteral("http://server:8096"));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_quickConnectInitiate_emptyUrl()
{
    auto ctx = makeUnauthedBackend();
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::errorOccurred);
    ctx.backend->quick_connect_initiate(QString());
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toString().contains(QStringLiteral("SERVER URL REQUIRED")));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_quickConnectInitiate_401()
{
    auto ctx = makeUnauthedBackend();
    ctx.mockNam->expectPost(QUrl(QStringLiteral("http://server:8096/QuickConnect/Initiate")),
                            QByteArray(), new FakeNetworkReply("", 401, QNetworkReply::NoError));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::quickConnectFailed);
    ctx.backend->quick_connect_initiate(QStringLiteral("http://server:8096"));
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("QUICK CONNECT NOT ENABLED ON SERVER"));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_quickConnectPoll_approved()
{
    auto ctx = makeUnauthedBackend();
    ctx.backend->m_quickConnectServerUrl = QStringLiteral("http://server:8096");
    ctx.backend->m_quickConnectSecret = QStringLiteral("sec-123");
    ctx.mockNam->expectGet(QUrl(QStringLiteral("http://server:8096/QuickConnect/Connect?secret=sec-123")),
                           new FakeNetworkReply(R"({"Authenticated":true})", 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::quickConnectApproved);
    ctx.backend->quick_connect_poll(QStringLiteral("sec-123"));
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_quickConnectPoll_404()
{
    auto ctx = makeUnauthedBackend();
    ctx.backend->m_quickConnectServerUrl = QStringLiteral("http://server:8096");
    ctx.mockNam->expectGet(QUrl(QStringLiteral("http://server:8096/QuickConnect/Connect?secret=sec-123")),
                           new FakeNetworkReply("", 404, QNetworkReply::NoError));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::quickConnectFailed);
    ctx.backend->quick_connect_poll(QStringLiteral("sec-123"));
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("CODE EXPIRED — RETRY"));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_quickConnectPoll_networkError()
{
    auto ctx = makeUnauthedBackend();
    ctx.backend->m_quickConnectServerUrl = QStringLiteral("http://server:8096");
    ctx.mockNam->expectGet(QUrl(QStringLiteral("http://server:8096/QuickConnect/Connect?secret=sec-123")),
                           new FakeNetworkReply("", 0, QNetworkReply::HostNotFoundError));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::quickConnectFailed);
    ctx.backend->quick_connect_poll(QStringLiteral("sec-123"));
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toString().contains(QStringLiteral("POLL FAILED")));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_quickConnectAuthenticate_success()
{
    auto ctx = makeUnauthedBackend();
    ctx.backend->m_quickConnectServerUrl = QStringLiteral("http://server:8096");
    ctx.backend->m_quickConnectSecret = QStringLiteral("sec-123");
    ctx.mockNam->expectPost(QUrl(QStringLiteral("http://server:8096/Users/AuthenticateWithQuickConnect")),
                            QByteArray(),
                            new FakeNetworkReply(R"({"AccessToken":"new-token","User":{"Id":"new-user","Name":"new-user-name"}})", 200));
    ctx.mockNam->expectGet(QUrl(QStringLiteral("http://server:8096/System/Info/Public")),
                           new FakeNetworkReply(R"({"ServerName":"My Server"})", 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::authStateChanged);
    ctx.backend->quick_connect_authenticate(QStringLiteral("sec-123"));
    QVERIFY(TestHelpers::waitForSignal(spy));
    // The server-name fetch is fire-and-forget; let it resolve before asserting
    // on m_serverName.
    TestHelpers::spinLoop(100);
    QCOMPARE(ctx.backend->m_accessToken, QStringLiteral("new-token"));
    QCOMPARE(ctx.backend->m_userId, QStringLiteral("new-user"));
    QCOMPARE(ctx.backend->m_userName, QStringLiteral("new-user-name"));
    QCOMPARE(ctx.backend->m_serverName, QStringLiteral("My Server"));
    QCOMPARE(ctx.backend->m_serverUrl, QStringLiteral("http://server:8096"));
    QCOMPARE(ctx.backend->m_quickConnectSecret, QString());
    QVERIFY(QFile::exists(ctx.dir->path() + "/jellyfin_auth.json"));
    QVERIFY(ctx.backend->has_auth());
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_quickConnectAuthenticate_emptyBody()
{
    auto ctx = makeUnauthedBackend();
    ctx.backend->m_quickConnectServerUrl = QStringLiteral("http://server:8096");
    ctx.backend->m_quickConnectSecret = QStringLiteral("sec-123");
    ctx.mockNam->expectPost(QUrl(QStringLiteral("http://server:8096/Users/AuthenticateWithQuickConnect")),
                            QByteArray(), new FakeNetworkReply("", 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::errorOccurred);
    ctx.backend->quick_connect_authenticate(QStringLiteral("sec-123"));
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toString().contains(QStringLiteral("INVALID AUTH RESPONSE")));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_quickConnectCancel()
{
    auto ctx = makeUnauthedBackend();
    ctx.backend->m_quickConnectServerUrl = QStringLiteral("http://server:8096");
    ctx.backend->m_quickConnectSecret = QStringLiteral("sec-123");
    ctx.mockNam->expectDelete(QUrl(QStringLiteral("http://server:8096/QuickConnect/Connect?secret=sec-123")),
                              new FakeNetworkReply("", 200));
    ctx.backend->quick_connect_cancel();
    QCOMPARE(ctx.backend->m_quickConnectSecret, QString());
    TestHelpers::spinLoop(50); // let the (ignored) DELETE reply finish
    QVERIFY(ctx.mockNam->isDone());
    QVERIFY(!ctx.mockNam->hadUnexpectedCalls());
}

// ─── Browse tests ────────────────────────────────────────────────────────────

void JellyfinBackendTest::test_loadLibraries_withShelves()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");

    QJsonArray libs = {
        TestHelpers::makeLibraryItem("lib-movie", "Movies", "movies"),
        TestHelpers::makeLibraryItem("lib-tv", "TV", "tvshows"),
        TestHelpers::makeLibraryItem("lib-music", "Music", "music"), // unsupported → filtered out
    };
    ctx.mockNam->expectGet(QUrl(base + "/Users/user-abc/Views"),
                           new FakeNetworkReply(QJsonDocument(TestHelpers::makeItemsResponse(libs)).toJson(QJsonDocument::Compact), 200));
    // Resume probe (limit=1) — non-empty → Continue Watching shelf.
    ctx.mockNam->expectGet(QUrl(base + "/Users/user-abc/Items/Resume?limit=1"),
                           new FakeNetworkReply(QJsonDocument(TestHelpers::makeItemsResponse({TestHelpers::makeMovieItem("r1")})).toJson(QJsonDocument::Compact), 200));
    // NextUp probe (limit=1) — non-empty → Up Next shelf.
    ctx.mockNam->expectGet(QUrl(base + "/Shows/NextUp?userId=user-abc&limit=1"),
                           new FakeNetworkReply(QJsonDocument(TestHelpers::makeItemsResponse({TestHelpers::makeEpisodeItem("n1")})).toJson(QJsonDocument::Compact), 200));

    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::librariesLoaded);
    ctx.backend->load_libraries();
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    const QVariantList result = spy.at(0).at(0).toList();
    QCOMPARE(result.size(), 4);
    // Order: continue_watching, up_next, movie lib, tv lib (music filtered).
    QCOMPARE(result.at(0).toMap().value("key").toString(), QStringLiteral("continue_watching"));
    QCOMPARE(result.at(0).toMap().value("title").toString(), QStringLiteral("CONTINUE WATCHING"));
    QCOMPARE(result.at(1).toMap().value("key").toString(), QStringLiteral("up_next"));
    QCOMPARE(result.at(1).toMap().value("title").toString(), QStringLiteral("NEXT UP"));
    QCOMPARE(result.at(2).toMap().value("key").toString(), QStringLiteral("lib-movie"));
    QCOMPARE(result.at(2).toMap().value("title").toString(), QStringLiteral("MOVIES"));
    QCOMPARE(result.at(3).toMap().value("key").toString(), QStringLiteral("lib-tv"));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadLibraries_emptyLibraries()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    ctx.mockNam->expectGet(QUrl(base + "/Users/user-abc/Views"),
                           new FakeNetworkReply(QJsonDocument(TestHelpers::makeItemsResponse({})).toJson(QJsonDocument::Compact), 200));
    ctx.mockNam->expectGet(QUrl(base + "/Users/user-abc/Items/Resume?limit=1"),
                           new FakeNetworkReply(QJsonDocument(TestHelpers::makeItemsResponse({})).toJson(QJsonDocument::Compact), 200));
    ctx.mockNam->expectGet(QUrl(base + "/Shows/NextUp?userId=user-abc&limit=1"),
                           new FakeNetworkReply(QJsonDocument(TestHelpers::makeItemsResponse({})).toJson(QJsonDocument::Compact), 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::librariesLoaded);
    ctx.backend->load_libraries();
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toList().isEmpty());
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadLibraries_noAuth()
{
    auto ctx = makeUnauthedBackend();
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::errorOccurred);
    ctx.backend->load_libraries();
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toString().contains(QStringLiteral("NOT AUTHENTICATED")));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadItems_success()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    QUrl url(base + "/Users/user-abc/Items");
    QUrlQuery q;
    q.addQueryItem("parentId", "lib-1");
    q.addQueryItem("recursive", "true");
    q.addQueryItem("fields", "Overview,Genres,UserData");
    q.addQueryItem("includeItemTypes", "Movie");
    q.addQueryItem("sortBy", "SortName");
    q.addQueryItem("sortOrder", "Ascending");
    url.setQuery(q);
    QJsonArray items = {
        TestHelpers::makeMovieItem("m1", "Movie One", 2023, "Overview one"),
        TestHelpers::makeMovieItem("m2", "Movie Two", 2024, "Overview two"),
    };
    ctx.mockNam->expectGet(url, new FakeNetworkReply(
        QJsonDocument(TestHelpers::makeItemsResponse(items)).toJson(QJsonDocument::Compact), 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::itemsLoaded);
    ctx.backend->load_items("lib-1", "Movie", "SortName");
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    const QVariantList result = spy.at(0).at(0).toList();
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.at(0).toMap().value("itemId").toString(), QStringLiteral("m1"));
    QCOMPARE(result.at(0).toMap().value("title").toString(), QStringLiteral("Movie One"));
    QCOMPARE(result.at(0).toMap().value("type").toString(), QStringLiteral("movie"));
    QCOMPARE(result.at(1).toMap().value("itemId").toString(), QStringLiteral("m2"));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadItems_empty()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    QUrl url(base + "/Users/user-abc/Items");
    QUrlQuery q;
    q.addQueryItem("parentId", "lib-1");
    q.addQueryItem("recursive", "true");
    q.addQueryItem("fields", "Overview,Genres,UserData");
    q.addQueryItem("includeItemTypes", "Movie");
    q.addQueryItem("sortBy", "SortName");
    q.addQueryItem("sortOrder", "Ascending");
    url.setQuery(q);
    ctx.mockNam->expectGet(url, new FakeNetworkReply(
        QJsonDocument(TestHelpers::makeItemsResponse({})).toJson(QJsonDocument::Compact), 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::itemsLoaded);
    ctx.backend->load_items("lib-1", "Movie", "SortName");
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toList().isEmpty());
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadItems_noAuth()
{
    auto ctx = makeUnauthedBackend();
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::errorOccurred);
    ctx.backend->load_items("lib-1", "Movie", "SortName");
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toString().contains(QStringLiteral("NOT AUTHENTICATED")));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadItemDetail_success()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    QUrl url(base + "/Users/user-abc/Items/item-1");
    QUrlQuery q;
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    url.setQuery(q);
    const QJsonObject item = TestHelpers::makePlayableItem("item-1");
    ctx.mockNam->expectGet(url, new FakeNetworkReply(
        QJsonDocument(item).toJson(QJsonDocument::Compact), 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::itemLoaded);
    ctx.backend->load_item_detail("item-1");
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    const QVariantMap detail = spy.at(0).at(0).toMap();
    QCOMPARE(detail.value("itemId").toString(), QStringLiteral("item-1"));
    QCOMPARE(detail.value("audioStreams").toList().size(), 1);
    QCOMPARE(detail.value("subtitleStreams").toList().size(), 1);
    QVERIFY(!detail.value("subtitleStreams").toList().at(0).toMap().value("subUrl").toString().isEmpty());
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadItemDetail_error()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    QUrl url(base + "/Users/user-abc/Items/item-1");
    QUrlQuery q;
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    url.setQuery(q);
    ctx.mockNam->expectGet(url, new FakeNetworkReply("", 0, QNetworkReply::HostNotFoundError));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::errorOccurred);
    ctx.backend->load_item_detail("item-1");
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toString().contains(QStringLiteral("LOAD ITEM DETAIL FAILED")));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadContinueWatching()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    QUrl url(base + "/Users/user-abc/Items/Resume");
    QUrlQuery q;
    q.addQueryItem("limit", "20");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    url.setQuery(q);
    QJsonArray items = { TestHelpers::makeMovieItem("c1", "Continue One", 2023, "") };
    ctx.mockNam->expectGet(url, new FakeNetworkReply(
        QJsonDocument(TestHelpers::makeItemsResponse(items)).toJson(QJsonDocument::Compact), 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::continueWatchingLoaded);
    ctx.backend->load_continue_watching();
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toList().size(), 1);
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadUpNext()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    QUrl url(base + "/Shows/NextUp");
    QUrlQuery q;
    q.addQueryItem("userId", "user-abc");
    q.addQueryItem("limit", "20");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    q.addQueryItem("enableUserData", "true");
    url.setQuery(q);
    QJsonArray items = { TestHelpers::makeEpisodeItem("u1", "s1", "Series", 1, 1) };
    ctx.mockNam->expectGet(url, new FakeNetworkReply(
        QJsonDocument(TestHelpers::makeItemsResponse(items)).toJson(QJsonDocument::Compact), 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::upNextLoaded);
    ctx.backend->load_up_next();
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toList().size(), 1);
    QVERIFY(ctx.mockNam->isDone());
}

// ─── Playback tests ──────────────────────────────────────────────────────────

void JellyfinBackendTest::test_getPlaybackUrl_directPlay()
{
    auto ctx = makeAuthedBackendWithConfig(QJsonObject{{"video_quality", "auto"}});
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    const QByteArray pbResp = R"({"MediaSources":[{"Id":"src-1","SupportsDirectPlay":true,"SupportsDirectStream":true}],"PlaySessionId":"ps-1"})";
    ctx.mockNam->expectPost(QUrl(base + "/Items/item-1/PlaybackInfo"),
                            QByteArray(), new FakeNetworkReply(pbResp, 200));
    // Direct-play path calls report_playback_start → POST /Sessions/Playing.
    ctx.mockNam->expectPost(QUrl(base + "/Sessions/Playing"),
                            QByteArray(), new FakeNetworkReply("", 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::streamUrlReady);
    ctx.backend->get_playback_url("item-1", "src-1", -1, -1, false);
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    const QString url = spy.at(0).at(0).toString();
    QVERIFY(url.contains("/Videos/item-1/stream"));
    QVERIFY(url.contains("static=true"));
    QVERIFY(url.contains("mediaSourceId=src-1"));
    QVERIFY(url.contains("PlaySessionId=ps-1"));
    QVERIFY(url.contains("api_key=test-token-123"));
    QCOMPARE(ctx.backend->m_currentPlayMethod, QStringLiteral("DirectPlay"));
    QCOMPARE(ctx.backend->m_currentPlaySessionId, QStringLiteral("ps-1"));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_getPlaybackUrl_transcode()
{
    auto ctx = makeAuthedBackendWithConfig(QJsonObject{{"video_quality", "720p"}});
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    const QByteArray pbResp = R"({"MediaSources":[{"Id":"src-1","TranscodingUrl":"/Videos/item-1/master.m3u8?PlaySessionId=ps-1"}],"PlaySessionId":"ps-1"})";
    ctx.mockNam->expectPost(QUrl(base + "/Items/item-1/PlaybackInfo"),
                            QByteArray(), new FakeNetworkReply(pbResp, 200));
    ctx.mockNam->expectPost(QUrl(base + "/Sessions/Playing"),
                            QByteArray(), new FakeNetworkReply("", 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::streamUrlReady);
    ctx.backend->get_playback_url("item-1", "src-1", 0, 0, false);
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    const QString url = spy.at(0).at(0).toString();
    QVERIFY(url.startsWith(base));
    QVERIFY(url.contains("MaxHeight=720"));
    QVERIFY(url.contains("PlaySessionId=ps-1"));
    // NOTE: the task spec said "api_key stripped", but the actual backend code
    // *appends* api_key to the transcode URL. This assertion follows the code.
    QVERIFY(url.contains("api_key=test-token-123"));
    QCOMPARE(ctx.backend->m_currentPlayMethod, QStringLiteral("Transcode"));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_getPlaybackUrl_forceTranscode()
{
    auto ctx = makeAuthedBackendWithConfig(QJsonObject{{"video_quality", "auto"}});
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    const QByteArray pbResp = R"({"MediaSources":[{"Id":"src-1","TranscodingUrl":"/Videos/item-1/master.m3u8?PlaySessionId=ps-1"}],"PlaySessionId":"ps-1"})";
    ctx.mockNam->expectPost(QUrl(base + "/Items/item-1/PlaybackInfo"),
                            QByteArray(), new FakeNetworkReply(pbResp, 200));
    ctx.mockNam->expectPost(QUrl(base + "/Sessions/Playing"),
                            QByteArray(), new FakeNetworkReply("", 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::streamUrlReady);
    ctx.backend->get_playback_url("item-1", "src-1", 0, 0, true);
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    // Verify the PlaybackInfo POST body disabled direct play/stream.
    bool foundPb = false;
    for (const auto &c : ctx.mockNam->capturedRequests()) {
        if (c.url.path().endsWith(QStringLiteral("/PlaybackInfo"))) {
            const QJsonObject body = QJsonDocument::fromJson(c.body).object();
            QCOMPARE(body.value("EnableDirectPlay").toBool(), false);
            QCOMPARE(body.value("EnableDirectStream").toBool(), false);
            // "auto" → maxBitrate/maxHeight are 0, so the backend omits those
            // keys entirely (it only adds them when > 0). The task spec's
            // "MaxStreamingBitrate=0, MaxHeight=0" expectation is therefore wrong
            // for the real code; assert absence instead.
            QVERIFY(!body.contains("MaxStreamingBitrate"));
            QVERIFY(!body.contains("MaxHeight"));
            foundPb = true;
        }
    }
    QVERIFY(foundPb);
    QCOMPARE(ctx.backend->m_currentPlayMethod, QStringLiteral("Transcode"));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_getPlaybackUrl_noSource()
{
    auto ctx = makeAuthedBackendWithConfig(QJsonObject{{"video_quality", "auto"}});
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    const QByteArray pbResp = R"({"MediaSources":[],"PlaySessionId":"ps-1"})";
    ctx.mockNam->expectPost(QUrl(base + "/Items/item-1/PlaybackInfo"),
                            QByteArray(), new FakeNetworkReply(pbResp, 200));
    QSignalSpy spyErr(ctx.backend.get(), &JellyfinBackend::errorOccurred);
    QSignalSpy spyUrl(ctx.backend.get(), &JellyfinBackend::streamUrlReady);
    ctx.backend->get_playback_url("item-1", "src-1", -1, -1, false);
    QVERIFY(TestHelpers::waitForSignal(spyErr));
    QCOMPARE(spyErr.count(), 1);
    QVERIFY(spyErr.at(0).at(0).toString().contains(QStringLiteral("NO PLAYABLE SOURCE")));
    QCOMPARE(spyUrl.count(), 0);
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_getPlaybackUrl_noAuth()
{
    auto ctx = makeUnauthedBackend();
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::errorOccurred);
    ctx.backend->get_playback_url("item-1", "src-1", -1, -1, false);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toString().contains(QStringLiteral("NOT AUTHENTICATED")));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadNextEpisode_success()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    QUrl detailUrl(base + "/Users/user-abc/Items/current-ep");
    QUrlQuery dq;
    dq.addQueryItem("fields", "MediaSources");
    detailUrl.setQuery(dq);
    const QJsonObject current = TestHelpers::makeEpisodeItem("current-ep", "sid-1", "Test Series", 1, 3);
    ctx.mockNam->expectGet(detailUrl, new FakeNetworkReply(
        QJsonDocument(current).toJson(QJsonDocument::Compact), 200));

    QUrl epUrl(base + "/Shows/sid-1/Episodes");
    QUrlQuery eq;
    eq.addQueryItem("userId", "user-abc");
    eq.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    eq.addQueryItem("enableUserData", "true");
    eq.addQueryItem("limit", "500");
    eq.addQueryItem("sortBy", "AiredEpisodeOrder");
    epUrl.setQuery(eq);
    QJsonArray eps = {
        TestHelpers::makeEpisodeItem("ep-2", "sid-1", "S", 1, 2),
        TestHelpers::makeEpisodeItem("ep-3", "sid-1", "S", 1, 3),
        TestHelpers::makeEpisodeItem("ep-4", "sid-1", "S", 1, 4),
        TestHelpers::makeEpisodeItem("ep-5", "sid-1", "S", 1, 5),
    };
    ctx.mockNam->expectGet(epUrl, new FakeNetworkReply(
        QJsonDocument(TestHelpers::makeItemsResponse(eps)).toJson(QJsonDocument::Compact), 200));

    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::nextEpisodeReady);
    ctx.backend->load_next_episode("current-ep");
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    // Current = S1E3 → next is S1E4 (smallest (same season, index > 3)).
    QCOMPARE(spy.at(0).at(0).toMap().value("itemId").toString(), QStringLiteral("ep-4"));
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadNextEpisode_noNext()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    QUrl detailUrl(base + "/Users/user-abc/Items/current-ep");
    QUrlQuery dq;
    dq.addQueryItem("fields", "MediaSources");
    detailUrl.setQuery(dq);
    const QJsonObject current = TestHelpers::makeEpisodeItem("current-ep", "sid-1", "Test Series", 1, 5);
    ctx.mockNam->expectGet(detailUrl, new FakeNetworkReply(
        QJsonDocument(current).toJson(QJsonDocument::Compact), 200));

    QUrl epUrl(base + "/Shows/sid-1/Episodes");
    QUrlQuery eq;
    eq.addQueryItem("userId", "user-abc");
    eq.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    eq.addQueryItem("enableUserData", "true");
    eq.addQueryItem("limit", "500");
    eq.addQueryItem("sortBy", "AiredEpisodeOrder");
    epUrl.setQuery(eq);
    QJsonArray eps = {
        TestHelpers::makeEpisodeItem("ep-3", "sid-1", "S", 1, 3),
        TestHelpers::makeEpisodeItem("ep-5", "sid-1", "S", 1, 5), // current is last
    };
    ctx.mockNam->expectGet(epUrl, new FakeNetworkReply(
        QJsonDocument(TestHelpers::makeItemsResponse(eps)).toJson(QJsonDocument::Compact), 200));

    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::nextEpisodeReady);
    ctx.backend->load_next_episode("current-ep");
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toMap().isEmpty());
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_loadNextEpisode_notAnEpisode()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    QUrl detailUrl(base + "/Users/user-abc/Items/movie-1");
    QUrlQuery dq;
    dq.addQueryItem("fields", "MediaSources");
    detailUrl.setQuery(dq);
    const QJsonObject movie = TestHelpers::makeMovieItem("movie-1", "A Movie", 2023, "");
    // No SeriesId, Type != "Episode" → backend short-circuits, no episodes fetch.
    ctx.mockNam->expectGet(detailUrl, new FakeNetworkReply(
        QJsonDocument(movie).toJson(QJsonDocument::Compact), 200));

    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::nextEpisodeReady);
    ctx.backend->load_next_episode("movie-1");
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toMap().isEmpty());
    QVERIFY(ctx.mockNam->isDone());
}

void JellyfinBackendTest::test_reportPlaybackStart()
{
    auto ctx = makeAuthedBackend();
    ctx.backend->m_currentPlaySessionId = QStringLiteral("ps-1");
    ctx.backend->m_currentPlayMethod = QStringLiteral("DirectPlay");
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    ctx.mockNam->expectPost(QUrl(base + "/Sessions/Playing"),
                            QByteArray(), new FakeNetworkReply("", 200));
    ctx.backend->report_playback_start("item-1", "src-1", "0", "1");
    TestHelpers::spinLoop(50); // fire-and-forget: let the reply finish
    QVERIFY(ctx.mockNam->isDone());
    QVERIFY(!ctx.mockNam->hadUnexpectedCalls());
    bool found = false;
    for (const auto &c : ctx.mockNam->capturedRequests()) {
        if (c.url.path().endsWith(QStringLiteral("/Sessions/Playing"))) {
            const QJsonObject body = QJsonDocument::fromJson(c.body).object();
            QCOMPARE(body.value("ItemId").toString(), QStringLiteral("item-1"));
            QCOMPARE(body.value("MediaSourceId").toString(), QStringLiteral("src-1"));
            QCOMPARE(body.value("PlaySessionId").toString(), QStringLiteral("ps-1"));
            QCOMPARE(body.value("PlayMethod").toString(), QStringLiteral("DirectPlay"));
            found = true;
        }
    }
    QVERIFY(found);
}

void JellyfinBackendTest::test_updatePlaybackProgress()
{
    auto ctx = makeAuthedBackend();
    ctx.backend->m_currentPlaySessionId = QStringLiteral("ps-1");
    ctx.backend->m_currentPlayMethod = QStringLiteral("Transcode");
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    ctx.mockNam->expectPost(QUrl(base + "/Sessions/Playing/Progress"),
                            QByteArray(), new FakeNetworkReply("", 200));
    ctx.backend->update_playback_progress("item-1", "src-1", 100000, false);
    TestHelpers::spinLoop(50);
    QVERIFY(ctx.mockNam->isDone());
    bool found = false;
    for (const auto &c : ctx.mockNam->capturedRequests()) {
        if (c.url.path().endsWith(QStringLiteral("/Sessions/Playing/Progress"))) {
            const QJsonObject body = QJsonDocument::fromJson(c.body).object();
            QCOMPARE(body.value("ItemId").toString(), QStringLiteral("item-1"));
            QCOMPARE(body.value("PositionTicks").toVariant().toLongLong(), qint64(100000));
            QCOMPARE(body.value("IsPaused").toBool(), false);
            found = true;
        }
    }
    QVERIFY(found);
}

void JellyfinBackendTest::test_reportPlaybackStopped()
{
    auto ctx = makeAuthedBackend();
    ctx.backend->m_currentPlaySessionId = QStringLiteral("ps-1");
    ctx.backend->m_currentPlayMethod = QStringLiteral("DirectPlay");
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    ctx.mockNam->expectPost(QUrl(base + "/Sessions/Playing/Stopped"),
                            QByteArray(), new FakeNetworkReply("", 200));
    ctx.backend->report_playback_stopped("item-1", "src-1", 500000, false);
    // The session id is cleared in the reply's finished lambda → needs a tick.
    TestHelpers::spinLoop(50);
    QVERIFY(ctx.backend->m_currentPlaySessionId.isEmpty());
    QVERIFY(ctx.mockNam->isDone());
    bool found = false;
    for (const auto &c : ctx.mockNam->capturedRequests()) {
        if (c.url.path().endsWith(QStringLiteral("/Sessions/Playing/Stopped"))) {
            const QJsonObject body = QJsonDocument::fromJson(c.body).object();
            QCOMPARE(body.value("PositionTicks").toVariant().toLongLong(), qint64(500000));
            QCOMPARE(body.value("PlaySessionId").toString(), QStringLiteral("ps-1"));
            found = true;
        }
    }
    QVERIFY(found);
}

// ─── Language preferences ────────────────────────────────────────────────────

void JellyfinBackendTest::test_loadServerPreferences()
{
    auto ctx = makeAuthedBackend();
    const QString base = QStringLiteral("http://192.168.1.100:8096");
    const QByteArray resp = R"({"Configuration":{"AudioLanguagePreference":"eng","SubtitleLanguagePreference":"spa","SubtitleMode":"Default"}})";
    ctx.mockNam->expectGet(QUrl(base + "/Users/user-abc"), new FakeNetworkReply(resp, 200));
    QSignalSpy spy(ctx.backend.get(), &JellyfinBackend::serverLanguagePreferencesReady);
    ctx.backend->load_server_preferences();
    QVERIFY(TestHelpers::waitForSignal(spy));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("eng"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("spa"));
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("Default"));
    QVERIFY(ctx.mockNam->isDone());
}

QTEST_MAIN(JellyfinBackendTest)
#include "JellyfinBackendTest.moc"
