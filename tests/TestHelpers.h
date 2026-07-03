#pragma once
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QSignalSpy>
#include <QObject>
#include <functional>

namespace TestHelpers {

    // Create a QTemporaryDir and return it. The caller owns it
    // (keep it alive for the duration of the test).
    QTemporaryDir* createTempDir();

    // Seed the temp dir with config.json containing module settings.
    // `moduleId` is like "com.240mp.jellyfin".
    // `overrides` is a map of setting key -> value that goes under modules.moduleId.
    // Returns the full path to the temp dir.
    QString seedConfig(QTemporaryDir *dir, const QString &moduleId,
                       const QJsonObject &overrides = {});

    // Seed the temp dir with jellyfin_auth.json for an authenticated state.
    QString seedAuth(QTemporaryDir *dir,
                     const QString &serverUrl = "http://192.168.1.100:8096",
                     const QString &accessToken = "test-token-123",
                     const QString &userId = "user-abc",
                     const QString &userName = "testuser",
                     const QString &serverName = "Test Server",
                     const QString &deviceId = "device-456");

    // Helper: wait for a signal to be emitted, with timeout.
    // Returns true if the signal was caught within timeoutMs.
    bool waitForSignal(QSignalSpy &spy, int timeoutMs = 5000);

    // Helper: spin the event loop for N ms (for testing async callback chains).
    void spinLoop(int ms = 100);

    // ── JSON Fixture Builders ──────────────────────────────────────────────

    // Build a minimal Jellyfin movie item as the server would return it.
    // All optional fields can be omitted for edge-case tests.
    QJsonObject makeMovieItem(const QString &id = "movie-1",
                              const QString &title = "Test Movie",
                              int year = 2024,
                              const QString &overview = "A test movie.");

    // Build a Jellyfin episode item.
    QJsonObject makeEpisodeItem(const QString &id = "ep-1",
                                const QString &seriesId = "series-1",
                                const QString &seriesName = "Test Series",
                                int seasonNumber = 1,
                                int episodeNumber = 1);

    // Build a Jellyfin series item.
    QJsonObject makeSeriesItem(const QString &id = "series-1",
                               const QString &title = "Test Series");

    // Build a Jellyfin item with media sources and streams (for playback tests).
    // Returns a full item JSON including MediaSources array with a single source
    // containing MediaStreams (one audio, one subtitle).
    QJsonObject makePlayableItem(const QString &id = "playable-1");

    // Build a library (view) item as returned by /Users/{id}/Views.
    QJsonObject makeLibraryItem(const QString &id, const QString &name,
                                const QString &collectionType);

    // Build a Jellyfin API response that wraps items in {"Items": [...]}
    QJsonObject makeItemsResponse(const QJsonArray &items);

    // Build a full MediaSource JSON object (for PlaybackInfo responses).
    QJsonObject makeMediaSource(const QString &id = "src-1",
                                bool supportsDirectPlay = true,
                                bool supportsDirectStream = true,
                                const QString &transcodingUrl = "");

    // Build a MediaStream JSON object (for audio or subtitle tracks).
    QJsonObject makeAudioStream(int index = 0,
                                const QString &language = "eng",
                                const QString &codec = "aac",
                                bool isDefault = true);

    // Subtitle stream: if isText is true, a subUrl should be derivable.
    QJsonObject makeSubtitleStream(int index = 0,
                                   const QString &language = "eng",
                                   const QString &codec = "subrip",
                                   bool isDefault = false,
                                   bool isForced = false,
                                   bool isText = true);

} // namespace TestHelpers
