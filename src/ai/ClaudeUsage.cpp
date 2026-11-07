#include "ClaudeUsage.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace ai::claudeusage {

namespace {

std::optional<qint64> fromEpochNumber(double n)
{
    if (!std::isfinite(n) || n <= 0.0)
        return std::nullopt;
    return n > 1e10 ? static_cast<qint64>(n) : static_cast<qint64>(n * 1000.0);
}

// Qt's ISO parser rejects fractional seconds longer than 3 digits (the API
// sends microseconds), so clamp the fraction to milliseconds first.
QString clampFraction(const QString &iso)
{
    static const QRegularExpression re(QStringLiteral("^(.*T\\d{2}:\\d{2}:\\d{2})\\.(\\d+)(.*)$"));
    const QRegularExpressionMatch m = re.match(iso);
    if (!m.hasMatch())
        return iso;
    const QString ms = (m.captured(2) + QStringLiteral("000")).left(3);
    return m.captured(1) + QLatin1Char('.') + ms + m.captured(3);
}

std::optional<RateWindow> mapWindow(const QJsonValue &raw, int windowMinutes)
{
    if (!raw.isObject())
        return std::nullopt;
    const QJsonObject o = raw.toObject();
    QJsonValue used = o.value(QLatin1String("utilization"));
    if (!used.isDouble())
        used = o.value(QLatin1String("used_percentage"));
    if (!used.isDouble())
        return std::nullopt;

    RateWindow w;
    w.usedPercent = std::clamp(used.toDouble(), 0.0, 100.0);
    w.windowMinutes = windowMinutes;
    w.resetsAtMs = parseTimestampMs(o.value(QLatin1String("resets_at")));
    return w;
}

// Weekly window scoped to the Fable model: a `limits[]` entry or a top-level
// key, depending on the account rollout.
std::optional<RateWindow> mapFableWeekly(const QJsonObject &body)
{
    const QJsonArray limits = body.value(QLatin1String("limits")).toArray();
    for (const auto &v : limits) {
        const QJsonObject limit = v.toObject();
        if (limit.value(QLatin1String("kind")).toString() != QLatin1String("weekly_scoped"))
            continue;
        if (!limit.value(QLatin1String("percent")).isDouble())
            continue;
        const QString name = limit.value(QLatin1String("scope")).toObject()
                                  .value(QLatin1String("model")).toObject()
                                  .value(QLatin1String("display_name")).toString().trimmed();
        if (name.compare(QLatin1String("fable"), Qt::CaseInsensitive) != 0)
            continue;
        const QJsonObject synthetic{
            {QLatin1String("used_percentage"), limit.value(QLatin1String("percent"))},
            {QLatin1String("resets_at"), limit.value(QLatin1String("resets_at"))},
        };
        if (auto w = mapWindow(synthetic, kWeeklyMinutes))
            return w;
    }
    for (const char *key : {"fable_weekly", "fable_seven_day", "seven_day_fable"}) {
        if (auto w = mapWindow(body.value(QLatin1String(key)), kWeeklyMinutes))
            return w;
    }
    return std::nullopt;
}

QString normalizeSpaces(QString s)
{
    // CLDR 42+ puts a narrow no-break space before AM/PM.
    s.replace(QChar(0x202F), QLatin1Char(' '));
    s.replace(QChar(0x00A0), QLatin1Char(' '));
    return s;
}

} // namespace

std::optional<qint64> parseTimestampMs(const QJsonValue &value)
{
    if (value.isDouble())
        return fromEpochNumber(value.toDouble());
    if (!value.isString())
        return std::nullopt;

    const QString s = value.toString().trimmed();
    if (s.isEmpty())
        return std::nullopt;

    bool numeric = false;
    const double n = s.toDouble(&numeric);
    if (numeric)
        return fromEpochNumber(n);

    const QDateTime dt = QDateTime::fromString(clampFraction(s), Qt::ISODateWithMs);
    if (!dt.isValid())
        return std::nullopt;
    return dt.toMSecsSinceEpoch();
}

QString parseCredentialsJson(const QByteArray &raw)
{
    const QJsonValue token = QJsonDocument::fromJson(raw).object()
                                 .value(QLatin1String("claudeAiOauth")).toObject()
                                 .value(QLatin1String("accessToken"));
    return token.isString() ? token.toString().trimmed() : QString();
}

QString credentialsPath(const QString &configDirEnv, const QString &homeDir)
{
    const QString base = configDirEnv.isEmpty() ? homeDir + QStringLiteral("/.claude") : configDirEnv;
    return QDir::cleanPath(base + QStringLiteral("/.credentials.json"));
}

QString accountJsonPath(const QString &configDirEnv, const QString &homeDir)
{
    return QDir::cleanPath((configDirEnv.isEmpty() ? homeDir : configDirEnv) + QStringLiteral("/.claude.json"));
}

QString parseAccountEmail(const QByteArray &raw)
{
    const QJsonValue email = QJsonDocument::fromJson(raw).object()
                                 .value(QLatin1String("oauthAccount")).toObject()
                                 .value(QLatin1String("emailAddress"));
    return email.isString() ? email.toString().trimmed() : QString();
}

QString configFolderLabel(const QString &configDirEnv, const QString &homeDir)
{
    const QString base = configDirEnv.isEmpty() ? homeDir + QStringLiteral("/.claude") : configDirEnv;
    const QString cleaned = QDir::cleanPath(base);
    const QString name = QDir(cleaned).dirName();
    return name.isEmpty() ? cleaned : name;   // a drive root has no name
}

QStringList normalizeConfigDirs(const QStringList &dirs)
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    QStringList out;
    out.reserve(dirs.size());
    for (const QString &raw : dirs) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty())
            continue;
        const QString dir = QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
        if (!out.contains(dir, cs))
            out.append(dir);
    }
    if (out.isEmpty())
        out.append(QString());
    return out;
}

Usage parseUsage(const QJsonObject &body)
{
    Usage u;
    u.status = Status::Ok;
    u.session = mapWindow(body.value(QLatin1String("five_hour")), kSessionMinutes);
    u.weekly = mapWindow(body.value(QLatin1String("seven_day")), kWeeklyMinutes);
    u.fableWeekly = mapFableWeekly(body);
    return u;
}

QString httpErrorMessage(int httpStatus)
{
    switch (httpStatus) {
    case 401:
    case 403:
        return QStringLiteral("Sign-in expired - run claude once to refresh it");
    case 429:
        return QStringLiteral("Rate limited by provider");
    default:
        return QStringLiteral("Provider returned HTTP %1").arg(httpStatus);
    }
}

Tone toneFor(double usedPercent)
{
    if (usedPercent >= kDangerPercent)
        return Tone::Danger;
    if (usedPercent >= kWarningPercent)
        return Tone::Warning;
    return Tone::Ok;
}

int displayPercent(double usedPercent, PercentMode mode)
{
    const double used = std::clamp(usedPercent, 0.0, 100.0);
    return static_cast<int>(std::lround(mode == PercentMode::Used ? used : 100.0 - used));
}

std::optional<double> elapsedPercent(const RateWindow &window, qint64 nowMs)
{
    if (!window.resetsAtMs || window.windowMinutes <= 0)
        return std::nullopt;
    const double total = static_cast<double>(window.windowMinutes) * 60'000.0;
    const double elapsed = total - static_cast<double>(*window.resetsAtMs - nowMs);
    return std::clamp(elapsed / total * 100.0, 0.0, 100.0);
}

QString formatResetIn(std::optional<qint64> resetsAtMs, qint64 nowMs)
{
    if (!resetsAtMs)
        return QStringLiteral("reset time unknown");
    const qint64 minutes = (*resetsAtMs - nowMs) / 60'000;
    if (minutes <= 0)
        return QStringLiteral("resetting now");
    if (minutes < 60)
        return QStringLiteral("in %1m").arg(minutes);
    const qint64 hours = minutes / 60;
    if (hours < 24) {
        const qint64 rest = minutes % 60;
        return rest ? QStringLiteral("in %1h %2m").arg(hours).arg(rest)
                    : QStringLiteral("in %1h").arg(hours);
    }
    const qint64 days = hours / 24;
    const qint64 restHours = hours % 24;
    return restHours ? QStringLiteral("in %1d %2h").arg(days).arg(restHours)
                     : QStringLiteral("in %1d").arg(days);
}

QString formatResetAt(std::optional<qint64> resetsAtMs, qint64 nowMs, const QLocale &locale)
{
    if (!resetsAtMs)
        return {};
    const QDateTime at = QDateTime::fromMSecsSinceEpoch(*resetsAtMs);
    if (!at.isValid())
        return {};
    QString clock = normalizeSpaces(locale.toString(at.time(), QLocale::ShortFormat));
    if (at.date() == QDateTime::fromMSecsSinceEpoch(nowMs).date())
        return clock;
    return locale.dayName(at.date().dayOfWeek(), QLocale::ShortFormat) + QLatin1Char(' ') + clock;
}

QString percentText(double usedPercent, PercentMode mode)
{
    return QStringLiteral("%1% %2")
        .arg(displayPercent(usedPercent, mode))
        .arg(mode == PercentMode::Used ? QStringLiteral("used") : QStringLiteral("left"));
}

QVector<NamedWindow> windowsOf(const Usage &usage)
{
    QVector<NamedWindow> out;
    out.reserve(3);
    if (usage.session)
        out.push_back({WindowKey::Session, QStringLiteral("5-hour"), *usage.session});
    if (usage.weekly)
        out.push_back({WindowKey::Weekly, QStringLiteral("Weekly"), *usage.weekly});
    if (usage.fableWeekly)
        out.push_back({WindowKey::FableWeekly, QStringLiteral("Fable weekly"), *usage.fableWeekly});
    return out;
}

std::optional<NamedWindow> shortestWindow(const Usage &usage)
{
    // Same order as windowsOf (shortest first), without building the vector.
    if (usage.session)
        return NamedWindow{WindowKey::Session, QStringLiteral("5-hour"), *usage.session};
    if (usage.weekly)
        return NamedWindow{WindowKey::Weekly, QStringLiteral("Weekly"), *usage.weekly};
    if (usage.fableWeekly)
        return NamedWindow{WindowKey::FableWeekly, QStringLiteral("Fable weekly"), *usage.fableWeekly};
    return std::nullopt;
}

std::optional<qint64> parseRetryAfter(const QByteArray &header, qint64 nowMs)
{
    const QByteArray s = header.trimmed();
    if (s.isEmpty())
        return std::nullopt;

    // The int64 -> ms multiply must not overflow; a day of seconds is far past
    // any cap we apply later.
    constexpr qint64 kMaxSeconds = 86'400;
    if (std::all_of(s.cbegin(), s.cend(), [](char c) { return c >= '0' && c <= '9'; })) {
        bool ok = false;
        const qint64 seconds = s.toLongLong(&ok);
        if (!ok)
            return std::nullopt;
        return std::min(seconds, kMaxSeconds) * 1000;
    }

    const std::optional<qint64> at = parseHttpDate(s, nowMs);
    if (!at)
        return std::nullopt;
    return std::clamp<qint64>(*at - nowMs, 0, kMaxSeconds * 1000);
}

std::optional<qint64> parseHttpDate(const QByteArray &value, qint64 nowMs)
{
    // RFC 9110 5.6.7: IMF-fixdate, plus the obsolete RFC 850 and asctime
    // forms recipients must accept. Always UTC, so the date and the clock are
    // read apart (no local time, no DST gap). The weekday is redundant, and
    // unverifiable against RFC 850's two-digit year: it is dropped.
    QString text = QString::fromLatin1(value).simplified();
    const qsizetype comma = text.indexOf(QLatin1Char(','));
    const qsizetype space = text.indexOf(QLatin1Char(' '));
    if (space <= 0)
        return std::nullopt;
    text = comma > 0 && comma < space ? text.mid(comma + 1).trimmed() : text.mid(space + 1);
    const QStringList t = text.split(QLatin1Char(' '));
    const QLatin1String gmt("GMT");
    QDate date;
    QString clock;
    if (t.size() == 5 && t.at(4) == gmt) {          // 06 Nov 1994 08:49:37 GMT
        date = QDate::fromString(t.at(0) + QLatin1Char(' ') + t.at(1) + QLatin1Char(' ') + t.at(2),
                                 QStringLiteral("dd MMM yyyy"));
        clock = t.at(3);
    } else if (t.size() == 3 && t.at(2) == gmt) {   // 06-Nov-94 08:49:37 GMT
        // The century first: parsed as 19yy, 29-Feb-00 would not exist.
        const QStringList dmy = t.at(0).split(QLatin1Char('-'));
        const bool twoDigits = dmy.size() == 3 && dmy.at(2).size() == 2 && dmy.at(2).at(0).isDigit()
                               && dmy.at(2).at(1).isDigit();
        if (twoDigits) {
            const int yy = dmy.at(2).toInt();
            // The year with those digits not more than 50 years ahead of now.
            const int nowYear = QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::utc()).date().year();
            const int year = nowYear + 50 - ((nowYear + 50 - yy) % 100);
            date = QDate::fromString(dmy.at(0) + QLatin1Char(' ') + dmy.at(1) + QLatin1Char(' ')
                                         + QString::number(year),
                                     QStringLiteral("dd MMM yyyy"));
        }
        clock = t.at(1);
    } else if (t.size() == 4) {                     // Nov 6 08:49:37 1994
        date = QDate::fromString(t.at(0) + QLatin1Char(' ') + t.at(1) + QLatin1Char(' ') + t.at(3),
                                 QStringLiteral("MMM d yyyy"));
        clock = t.at(2);
    }
    const QTime time = QTime::fromString(clock, QStringLiteral("HH:mm:ss"));
    if (!date.isValid() || !time.isValid())
        return std::nullopt;
    return QDateTime(date, time, QTimeZone::utc()).toMSecsSinceEpoch();
}

qint64 backoffDelayMs(int failures, std::optional<qint64> retryAfterMs, double jitterUnit)
{
    // 60 s << 4 already passes the 15 min cap; clamping the shift avoids overflow.
    const int shift = std::clamp(failures - 1, 0, 4);
    const double base = static_cast<double>(kBackoffBaseMs << shift);
    const double factor = 0.9 + 0.2 * std::clamp(jitterUnit, 0.0, 1.0);
    const qint64 own = std::clamp(static_cast<qint64>(std::llround(base * factor)),
                                  kBackoffBaseMs, kBackoffCapMs);
    if (!retryAfterMs)
        return own;
    return std::max(own, std::min(*retryAfterMs, kRetryAfterCapMs));
}

QString formatAgo(qint64 atMs, qint64 nowMs)
{
    const qint64 minutes = (nowMs - atMs) / 60'000;
    if (minutes < 1)
        return QStringLiteral("just now");
    if (minutes < 60)
        return QStringLiteral("%1m ago").arg(minutes);
    const qint64 hours = minutes / 60;
    if (hours < 24)
        return QStringLiteral("%1h ago").arg(hours);
    return QStringLiteral("%1d ago").arg(hours / 24);
}

QString staleNotice(const Health &health, qint64 nowMs, const QLocale &locale)
{
    if (health.lastOkMs <= 0) {
        if (health.retryAtMs <= nowMs)
            return health.error;
        return health.error + QStringLiteral("\nTry again at ")
               + formatResetAt(health.retryAtMs, nowMs, locale);
    }
    return QStringLiteral("Last synced %1").arg(formatAgo(health.lastOkMs, nowMs));
}

} // namespace ai::claudeusage
