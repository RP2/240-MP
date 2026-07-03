#include "FakeNetworkReply.h"
#include <QTimer>
#include <cstring>

FakeNetworkReply::FakeNetworkReply(const QByteArray &body, int httpStatus,
                                   QNetworkReply::NetworkError error, QObject *parent)
    : QNetworkReply(parent)
    , m_body(body)
    , m_httpStatus(httpStatus)
    , m_error(error)
{
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_httpStatus);
    setAttribute(QNetworkRequest::HttpReasonPhraseAttribute, QStringLiteral("OK"));
    if (m_error != QNetworkReply::NoError)
        setError(m_error, QStringLiteral("FakeNetworkReply error"));
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}

void FakeNetworkReply::scheduleFinished()
{
    QTimer::singleShot(0, this, [this]() {
        emit readyRead();
        emit finished();
    });
}

void FakeNetworkReply::abort()
{
    // No-op: the reply is already complete.
}

qint64 FakeNetworkReply::readData(char *data, qint64 maxlen)
{
    if (m_pos >= m_body.size())
        return 0;
    const qint64 toRead = qMin(maxlen, static_cast<qint64>(m_body.size() - m_pos));
    std::memcpy(data, m_body.constData() + m_pos, static_cast<size_t>(toRead));
    m_pos += toRead;
    return toRead;
}

qint64 FakeNetworkReply::bytesAvailable() const
{
    return qint64(m_body.size() - m_pos);
}
