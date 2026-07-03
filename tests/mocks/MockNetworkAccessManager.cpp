#include "MockNetworkAccessManager.h"
#include "FakeNetworkReply.h"
#include <QUrlQuery>
#include <QDebug>
#include <QTimer>
#include <algorithm>

static QString opName(QNetworkAccessManager::Operation op)
{
    switch (op) {
    case QNetworkAccessManager::GetOperation:     return QStringLiteral("GET");
    case QNetworkAccessManager::PostOperation:    return QStringLiteral("POST");
    case QNetworkAccessManager::DeleteOperation:  return QStringLiteral("DELETE");
    case QNetworkAccessManager::PutOperation:     return QStringLiteral("PUT");
    case QNetworkAccessManager::HeadOperation:    return QStringLiteral("HEAD");
    default: return QStringLiteral("OP");
    }
}

QString MockNetworkAccessManager::normalizeUrl(const QUrl &url)
{
    // Canonicalize to scheme://host[:port]/path + "?" + sorted(query items) so
    // the backend's QUrlQuery insertion order doesn't have to match the test's.
    QString base = url.toString(QUrl::RemoveQuery);
    QUrlQuery q(url);
    QList<QPair<QString, QString>> items = q.queryItems(QUrl::FullyDecoded);
    std::sort(items.begin(), items.end());
    QStringList parts;
    for (const auto &p : items)
        parts << (p.first + QLatin1Char('=') + p.second);
    QString n = base;
    if (!parts.isEmpty())
        n += QLatin1Char('?') + parts.join(QLatin1Char('&'));
    return n;
}

void MockNetworkAccessManager::expectGet(const QUrl &url, FakeNetworkReply *reply)
{
    Expectation e;
    e.op = GetOperation;
    e.normalizedUrl = normalizeUrl(url);
    e.originalUrl = url;
    e.expectedBody = QByteArray();
    e.reply = reply;
    if (reply) reply->setParent(this);
    m_expectations.append(e);
}

void MockNetworkAccessManager::expectGet(const QNetworkRequest &request, FakeNetworkReply *reply)
{
    expectGet(request.url(), reply);
}

void MockNetworkAccessManager::expectPost(const QUrl &url, const QByteArray &body, FakeNetworkReply *reply)
{
    Expectation e;
    e.op = PostOperation;
    e.normalizedUrl = normalizeUrl(url);
    e.originalUrl = url;
    e.expectedBody = body;
    e.reply = reply;
    if (reply) reply->setParent(this);
    m_expectations.append(e);
}

void MockNetworkAccessManager::expectPost(const QNetworkRequest &request, const QByteArray &body, FakeNetworkReply *reply)
{
    expectPost(request.url(), body, reply);
}

void MockNetworkAccessManager::expectDelete(const QUrl &url, FakeNetworkReply *reply)
{
    Expectation e;
    e.op = DeleteOperation;
    e.normalizedUrl = normalizeUrl(url);
    e.originalUrl = url;
    e.expectedBody = QByteArray();
    e.reply = reply;
    if (reply) reply->setParent(this);
    m_expectations.append(e);
}

bool MockNetworkAccessManager::isDone() const
{
    return m_expectations.isEmpty();
}

QString MockNetworkAccessManager::pendingExpectations() const
{
    QStringList out;
    for (const auto &e : m_expectations)
        out << (opName(e.op) + QLatin1Char(' ') + e.originalUrl.toString());
    return out.join(QStringLiteral("; "));
}

QNetworkReply *MockNetworkAccessManager::createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData)
{
    QByteArray body;
    if (outgoingData) {
        outgoingData->seek(0);
        body = outgoingData->readAll();
    }
    return match(op, request, body);
}

QNetworkReply *MockNetworkAccessManager::match(Operation op, const QNetworkRequest &request, const QByteArray &body)
{
    const QString norm = normalizeUrl(request.url());

    for (int i = 0; i < m_expectations.size(); ++i) {
        Expectation &e = m_expectations[i];
        if (e.op != op)
            continue;
        if (e.normalizedUrl != norm)
            continue;
        // Body: empty expected body == wildcard (match any body).
        if (op == PostOperation && !e.expectedBody.isEmpty() && e.expectedBody != body)
            continue;

        CapturedRequest cap;
        cap.url = request.url();
        cap.body = body;
        cap.authorization = QString::fromLatin1(request.rawHeader("Authorization"));
        cap.op = op;
        m_captured.append(cap);

        FakeNetworkReply *fr = e.reply.data();
        m_expectations.removeAt(i);
        if (fr) {
            fr->scheduleFinished();
            return fr;
        }
        break; // reply was destroyed before being consumed — fall through to error
    }

    ++m_unexpectedCalls;
    qWarning("[MockNAM] Unexpected %s %s (body %d bytes); pending: %s",
             qPrintable(opName(op)), qPrintable(request.url().toString()),
             body.size(), qPrintable(pendingExpectations()));
    auto *err = new FakeNetworkReply(QByteArray(), 0, QNetworkReply::HostNotFoundError, this);
    err->scheduleFinished();
    return err;
}
