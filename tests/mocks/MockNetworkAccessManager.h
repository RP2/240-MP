#pragma once
#include <QtNetwork/QNetworkAccessManager>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QUrl>
#include <QByteArray>
#include <QNetworkRequest>

class FakeNetworkReply;

// A QNetworkAccessManager that serves queued expectations instead of real
// network traffic. Set up expectations with expectGet/expectPost/expectDelete
// BEFORE invoking the backend method; the mock matches each request against
// the queue (FIFO among matching), returns the canned reply, and schedules its
// finished() signal. After the test, QVERIFY(mockNam->isDone()) ensures every
// expected request was actually issued.
//
// Implementation note: QNetworkAccessManager::get/post/deleteResource are NOT
// virtual — they all funnel through the protected virtual createRequest(). So
// that's what we override.
class MockNetworkAccessManager : public QNetworkAccessManager {
    Q_OBJECT
public:
    // Queue an expected GET to `url` (or the url of `request`).
    void expectGet(const QUrl &url, FakeNetworkReply *reply);
    void expectGet(const QNetworkRequest &request, FakeNetworkReply *reply);

    // Queue an expected POST. If `body` is empty the expectation matches ANY
    // body (wildcard); otherwise the request body must match exactly. The
    // actual body is always retained for later inspection via capturedRequests().
    void expectPost(const QUrl &url, const QByteArray &body, FakeNetworkReply *reply);
    void expectPost(const QNetworkRequest &request, const QByteArray &body, FakeNetworkReply *reply);

    // Queue an expected DELETE.
    void expectDelete(const QUrl &url, FakeNetworkReply *reply);

    // True when every queued expectation has been consumed.
    bool isDone() const;
    // Human-readable list of unconsumed expectations (for failure diagnostics).
    QString pendingExpectations() const;

    // True if the backend issued a request that matched no expectation.
    bool hadUnexpectedCalls() const { return m_unexpectedCalls > 0; }

    // Every request the backend actually issued, in order. Tests that need to
    // assert on Authorization headers or POST bodies read from here.
    struct CapturedRequest {
        QUrl url;
        QByteArray body;
        QString authorization;
        Operation op;
    };
    const QList<CapturedRequest> &capturedRequests() const { return m_captured; }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override;

private:
    struct Expectation {
        Operation op;
        QString normalizedUrl;   // canonical path + sorted-query form for matching
        QUrl originalUrl;        // for diagnostics
        QByteArray expectedBody; // empty == wildcard (match any body)
        QPointer<FakeNetworkReply> reply;
    };
    QList<Expectation> m_expectations;
    QList<CapturedRequest> m_captured;
    int m_unexpectedCalls = 0;

    QNetworkReply *match(Operation op, const QNetworkRequest &request, const QByteArray &body);
    static QString normalizeUrl(const QUrl &url);
};
