#ifndef AI_CLAUDE_USAGE_SERVICE_H
#define AI_CLAUDE_USAGE_SERVICE_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <optional>

#include "ClaudeUsage.h"

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace ai {

// One-shot GET against the usage endpoint. Seam so tests never touch the network.
class IClaudeUsageTransport : public QObject
{
    Q_OBJECT
public:
    struct Request {
        QUrl url;
        QString token;
    };

    explicit IClaudeUsageTransport(QObject *parent = nullptr) : QObject(parent) {}
    ~IClaudeUsageTransport() override = default;

    virtual void get(const Request &req) = 0;
    virtual void cancel() = 0;

signals:
    // Any HTTP response, success or not. `retryAfterMs` is the parsed
    // `Retry-After` header, -1 when absent or unusable.
    void finished(int httpStatus, const QByteArray &body, qint64 retryAfterMs);
    // No HTTP response at all (DNS, TLS, timeout, cancelled-by-peer).
    void failed();
};

// QNetworkAccessManager transport: bearer token + the beta header the endpoint
// requires, 10 s transfer timeout. Never logs the token or the URL-bearing
// QNetworkReply::errorString().
class ClaudeUsageHttpTransport : public IClaudeUsageTransport
{
    Q_OBJECT
public:
    explicit ClaudeUsageHttpTransport(QObject *parent = nullptr);
    ~ClaudeUsageHttpTransport() override;

    void get(const Request &req) override;
    void cancel() override;

private:
    void onFinished();

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
};

// Owns the latest Claude usage snapshot. UI-thread object; the only blocking
// work is a sub-millisecond read of the small credentials file, everything
// else is async. At most one request in flight.
//
// A failed refresh (429, 5xx, network, bad body) never discards a snapshot
// that was already loaded: the failure lands in health() and puts the service
// on a cooldown (Retry-After, else exponential backoff) that no trigger - not
// even a forced refresh - can bypass.
class ClaudeUsageService : public QObject
{
    Q_OBJECT
public:
    // Safety net only; real triggers are popover open, app activation and
    // finished agent turns, all behind the same gate in refresh().
    static constexpr int kPollIntervalMs = 5 * 60'000;
    // Non-forced refreshes closer together than this are dropped: a burst of
    // triggers (swarm of agent turns, popover hover) costs one request per minute.
    static constexpr qint64 kMinRefreshGapMs = 60'000;
    static QUrl usageUrl();

    // One Claude account: the config folder holding its `.credentials.json`
    // (`configDir` empty = $CLAUDE_CONFIG_DIR, else ~/.claude).
    explicit ClaudeUsageService(IClaudeUsageTransport *transport,
                                const QString &configDir = {},
                                QObject *parent = nullptr);

    // The folder as configured (empty = default); identifies the account.
    const QString &configDir() const { return m_configDir; }
    // Signed-in email from the folder's `.claude.json`, else the folder name.
    QString accountLabel() const { return m_email.isEmpty() ? m_folderLabel : m_email; }
    // Folder the credentials are read from, resolved for display.
    QString resolvedConfigDir() const;

    const claudeusage::Usage &usage() const { return m_usage; }
    bool loaded() const { return m_loaded; }
    bool loading() const { return m_inflight; }
    // Wall clock (ms) of the last completed refresh; drives the countdowns.
    qint64 nowMs() const { return m_nowMs; }
    // Shown only once a response arrived and the user is signed in; an error
    // keeps it visible so the message is reachable.
    bool buttonVisible() const;
    std::optional<claudeusage::NamedWindow> headline() const { return claudeusage::shortestWindow(m_usage); }
    // Outcome of the latest refresh: error text, last success, cooldown end.
    const claudeusage::Health &health() const { return m_health; }

    // `force` bypasses the min-gap guard (explicit refresh click) but not the
    // cooldown; an in-flight request is never duplicated either way.
    void refresh(bool force = false);
    // Starts the safety poll (ticks are skipped while the app is inactive).
    void startPolling();

    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }
    // 0..1 source for the backoff jitter; tests pin it.
    void setJitter(std::function<double()> jitter) { m_jitter = std::move(jitter); }

signals:
    void changed();

private:
    void sendWithToken(const QString &token);
    void publish();
    void succeed(claudeusage::Usage usage);
    void signedOut();
    void fail(const QString &error, qint64 retryAfterMs);
    void onResponse(int httpStatus, const QByteArray &body, qint64 retryAfterMs);
    void onTransportFailed();
    void readKeychainAsync();
    void readAccountEmail();

    IClaudeUsageTransport *m_transport = nullptr;
    QString m_configDir;
    QString m_credentialsFile;
    QString m_accountFile;
    QString m_folderLabel;
    QString m_email;
    // `.claude.json` is rewritten often and can be large: re-parse only when its
    // mtime or size moved.
    qint64 m_accountFileMtimeMs = -1;
    qint64 m_accountFileSize = -1;
    QTimer m_pollTimer;
    QProcess *m_keychain = nullptr;
    claudeusage::Usage m_usage;
    claudeusage::Health m_health;
    std::function<qint64()> m_clock;
    std::function<double()> m_jitter;
    qint64 m_nowMs = 0;
    qint64 m_lastCompletedMs = 0;
    bool m_loaded = false;
    bool m_inflight = false;
};

} // namespace ai

#endif // AI_CLAUDE_USAGE_SERVICE_H
