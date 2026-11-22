#pragma once

// A stand-in for the network: every request is answered from the test's own
// table (recorded museum JSON, small generated images), so the museum client
// is tested without going online.

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <functional>

class FakeReply : public QNetworkReply {
    Q_OBJECT

public:
    FakeReply(const QNetworkRequest &request, int status, const QByteArray &type, const QByteArray &body, QObject *parent)
        : QNetworkReply(parent), m_data(body) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, type);
        setHeader(QNetworkRequest::ContentLengthHeader, body.size());
        QTimer::singleShot(0, this, [this] {
            if (m_done)
                return;
            emit metaDataChanged();
            if (m_done)
                return;
            emit downloadProgress(m_data.size(), m_data.size());
            if (m_done)
                return;
            emit readyRead();
            done();
        });
    }
    void abort() override {
        if (m_done)
            return;
        setError(OperationCanceledError, QStringLiteral("aborted"));
        done();
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return m_data.size() - m_pos + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 max) override {
        const qint64 n = std::min<qint64>(max, m_data.size() - m_pos);
        if (n <= 0)
            return m_done ? -1 : 0;
        memcpy(data, m_data.constData() + m_pos, size_t(n));
        m_pos += n;
        return n;
    }

private:
    void done() {
        m_done = true;
        setFinished(true);
        emit finished();
    }
    QByteArray m_data;
    qint64 m_pos = 0;
    bool m_done = false;
};

class FakeNetwork : public QNetworkAccessManager {
    Q_OBJECT

public:
    struct Answer {
        int status = 200;
        QByteArray type;
        QByteArray body;
    };
    std::function<Answer(const QNetworkRequest &)> respond;
    QList<QNetworkRequest> requests;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        requests << request;
        const Answer a = respond ? respond(request) : Answer{404, "text/plain", {}};
        return new FakeReply(request, a.status, a.type, a.body, this);
    }
};
