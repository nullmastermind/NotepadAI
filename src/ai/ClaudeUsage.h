#ifndef AI_CLAUDE_USAGE_H
#define AI_CLAUDE_USAGE_H

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>

// Claude Code subscription usage (5-hour / weekly / Fable-weekly windows), read
// from `GET https://api.anthropic.com/api/oauth/usage` with the access token the
// Claude CLI already stored on disk. Everything here is pure (no I/O, no
// widgets) so the parsing and formatting rules are unit-testable.
namespace ai::claudeusage {

struct RateWindow {
    double usedPercent = 0.0;           // 0..100, clamped
    int windowMinutes = 0;
    std::optional<qint64> resetsAtMs;   // unix ms

    bool operator==(const RateWindow &o) const = default;
};

enum class Status : uint8_t { Ok, Unavailable, Error };

struct Usage {
    Status status = Status::Unavailable;
    QString error;
    std::optional<RateWindow> session;
    std::optional<RateWindow> weekly;
    std::optional<RateWindow> fableWeekly;
};

enum class Tone : uint8_t { Ok, Warning, Danger };
enum class PercentMode : uint8_t { Used, Remaining };

enum class WindowKey : uint8_t { Session, Weekly, FableWeekly };

struct NamedWindow {
    WindowKey key = WindowKey::Session;
    QString label;
    RateWindow window;
};

constexpr double kWarningPercent = 80.0;
constexpr double kDangerPercent = 95.0;
constexpr int kSessionMinutes = 300;
constexpr int kWeeklyMinutes = 10080;

// Epoch seconds, epoch ms, numeric string or RFC 3339 -> unix ms. 1e10 splits
// seconds (< year 2286) from ms (> 1970-04).
std::optional<qint64> parseTimestampMs(const QJsonValue &value);

// `{"claudeAiOauth":{"accessToken":"..."}}` -> trimmed token, empty when absent.
QString parseCredentialsJson(const QByteArray &raw);

// `$CLAUDE_CONFIG_DIR/.credentials.json`, else `<home>/.claude/.credentials.json`.
QString credentialsPath(const QString &configDirEnv, const QString &homeDir);

// Where Claude Code keeps the signed-in profile: `<configDir>/.claude.json`,
// else `<home>/.claude.json` (beside, not inside, the default ~/.claude).
QString accountJsonPath(const QString &configDirEnv, const QString &homeDir);

// `{"oauthAccount":{"emailAddress":"..."}}` -> trimmed address, empty when absent.
QString parseAccountEmail(const QByteArray &raw);

// Name of the config folder itself (".claude-sub"); the account label when no
// email is known.
QString configFolderLabel(const QString &configDirEnv, const QString &homeDir);

// The configured folder list as account keys: trimmed, '/'-separated, cleaned,
// empties and duplicates (case-insensitive on Windows) dropped, order kept.
// An empty result means one account in the default folder: `{QString()}`.
QStringList normalizeConfigDirs(const QStringList &dirs);

Usage parseUsage(const QJsonObject &body);

// 401/403 expired sign-in, 429 rate limited, anything else "HTTP <n>".
QString httpErrorMessage(int httpStatus);

Tone toneFor(double usedPercent);
int displayPercent(double usedPercent, PercentMode mode);
// Share of the window already elapsed (0..100); nullopt when unknown.
std::optional<double> elapsedPercent(const RateWindow &window, qint64 nowMs);

// "resetting now", "in 45m", "in 2h 15m", "in 3d 4h"; "reset time unknown" when null.
QString formatResetIn(std::optional<qint64> resetsAtMs, qint64 nowMs);
// Local clock time of the reset: "2:30 PM" same day, otherwise "Thu 2:30 PM".
QString formatResetAt(std::optional<qint64> resetsAtMs, qint64 nowMs, const QLocale &locale = QLocale());
// "26% used" / "74% left".
QString percentText(double usedPercent, PercentMode mode);

// Windows the provider reported, in display order (5-hour, weekly, Fable weekly).
QVector<NamedWindow> windowsOf(const Usage &usage);
// The shortest window the account reports (5-hour, else weekly, else Fable
// weekly): the one whose usage moves during a working session.
std::optional<NamedWindow> shortestWindow(const Usage &usage);

// Fetch health, kept apart from the last good `Usage` so a failed refresh never
// discards numbers that were already shown.
struct Health {
    QString error;            // empty = the last refresh succeeded
    qint64 lastOkMs = 0;      // wall clock of the last successful parse, 0 = never
    qint64 retryAtMs = 0;     // no request before this wall-clock instant, 0 = none
    int failures = 0;         // consecutive failures, drives the backoff
};

constexpr qint64 kBackoffBaseMs = 60'000;
constexpr qint64 kBackoffCapMs = 15 * 60'000;
constexpr qint64 kRetryAfterCapMs = 60 * 60'000;

// `Retry-After` header: delta-seconds or an HTTP-date -> milliseconds from
// `nowMs` (a past date is 0). nullopt for empty, negative or malformed values.
std::optional<qint64> parseRetryAfter(const QByteArray &header, qint64 nowMs);
// An HTTP-date in any of RFC 9110's three forms -> UTC milliseconds since the
// epoch. `nowMs` resolves RFC 850's two-digit year.
std::optional<qint64> parseHttpDate(const QByteArray &value, qint64 nowMs);

// Delay before the next attempt after `failures` consecutive failures: 60 s
// doubling to a 15 min cap, scaled by 0.9..1.1 from `jitterUnit` (0..1) and
// re-clamped to [60 s, 15 min]. A larger server `Retry-After` (ms) wins, up to 1 h.
qint64 backoffDelayMs(int failures, std::optional<qint64> retryAfterMs, double jitterUnit);

// "just now", "5m ago", "2h ago", "3d ago".
QString formatAgo(qint64 atMs, qint64 nowMs);
// Popover notice for a failed refresh: "Last synced 3m ago" when numbers are
// on screen; otherwise the error plus "Try again at 2:30 PM" while a cooldown
// is running.
QString staleNotice(const Health &health, qint64 nowMs, const QLocale &locale = QLocale());

} // namespace ai::claudeusage

#endif // AI_CLAUDE_USAGE_H
