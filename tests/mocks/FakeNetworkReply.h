#pragma once
#include <QtNetwork/QNetworkReply>

// A controllable QNetworkReply for unit tests.
//
// Construct one with a fixed body, HTTP status, and optional network error,
// then hand it to a MockNetworkAccessManager expectation. When the mock
// dequeues the expectation it calls scheduleFinished(), which asynchronously
// emits readyRead()/finished() (and errorOccurred() if an error is set) on the
// next event-loop iteration — mirroring how real replies deliver results, so
// the backend's lambdas run only once the test spins the event loop.
class FakeNetworkReply : public QNetworkReply {
    Q_OBJECT
public:
    FakeNetworkReply(const QByteArray &body, int httpStatus = 200,
                     QNetworkReply::NetworkError error = QNetworkReply::NoError,
                     QObject *parent = nullptr);

    // Queue finished() (and, if an error is set, errorOccurred()) for the next
    // event-loop tick.
    void scheduleFinished();

    // QNetworkReply / QIODevice interface
    void abort() override;
    bool isSequential() const override { return true; }
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 bytesAvailable() const override;
    bool atEnd() const override { return m_pos >= m_body.size(); }

    int httpStatus() const { return m_httpStatus; }
    QByteArray body() const { return m_body; }

private:
    QByteArray m_body;
    int m_httpStatus;
    NetworkError m_error;
    qint64 m_pos = 0;
};
