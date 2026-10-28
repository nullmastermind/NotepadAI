#include "UsagePopover.h"

#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>

#include "UsageStyle.h"
#include "UsageWindowBar.h"

using namespace ai::claudeusage;

namespace {

constexpr int kPadding = 9;       // 8px content inset + 1px border
constexpr int kSpacing = 8;
constexpr int kRadius = 4;
constexpr int kAnchorGap = 6;
constexpr int kScreenMargin = 8;
constexpr int kRefreshIcon = 12;
constexpr int kRefreshButton = 24;
constexpr int kProviderIcon = 14;
constexpr int kStaleTickMs = 15'000;

QLabel *boldLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

// 1px rule in the palette's border color (the spec's section border-top).
class Divider : public QWidget
{
public:
    explicit Divider(QWidget *parent) : QWidget(parent)
    {
        setFixedHeight(1);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter(this).fillRect(rect(), palette().color(QPalette::Mid));
    }
};

} // namespace

// One account: brand glyph + "Claude" + account label, then a hint line and/or
// one bar per window.
class UsageAccountSection : public QWidget
{
public:
    explicit UsageAccountSection(QWidget *parent) : QWidget(parent)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(kSpacing);

        auto *head = new QHBoxLayout;
        head->setContentsMargins(0, 0, 0, 0);
        head->setSpacing(4);
        m_icon = new QLabel(this);
        m_icon->setFixedSize(kProviderIcon, kProviderIcon);
        head->addWidget(m_icon);
        m_provider = boldLabel(QStringLiteral("Claude"), this);
        head->addWidget(m_provider);
        m_account = new QLabel(this);
        m_account->setForegroundRole(QPalette::PlaceholderText);
        head->addWidget(m_account, 1);
        root->addLayout(head);

        m_hint = new QLabel(this);
        m_hint->setWordWrap(true);
        m_hint->setForegroundRole(QPalette::PlaceholderText);
        root->addWidget(m_hint);

        applyChrome();
    }

    void setAccount(const UsagePopover::Account &a, PercentMode mode, qint64 now)
    {
        // Head: the label takes what "Claude" leaves of the popup width.
        const int room = UsagePopover::kWidth - 2 * kPadding - kProviderIcon - 8
                         - m_provider->fontMetrics().horizontalAdvance(m_provider->text());
        m_account->setText(m_account->fontMetrics().elidedText(a.label, Qt::ElideMiddle, std::max(0, room)));
        m_account->setToolTip(QStringLiteral("%1\n%2").arg(a.label, a.configDir));

        const QVector<NamedWindow> windows = windowsOf(a.usage);
        const bool failing = !a.health.error.isEmpty();

        QString hint;
        bool showBars = false;
        if (!a.loaded)
            hint = UsagePopover::tr("Loading usage…");
        else if (a.usage.status == Status::Unavailable)
            hint = UsagePopover::tr("Not signed in. Run claude and sign in.");
        else if (a.usage.status == Status::Error)
            hint = failing ? staleNotice(a.health, now)
                           : (a.usage.error.isEmpty() ? UsagePopover::tr("Usage unavailable") : a.usage.error);
        else if (windows.isEmpty())
            hint = UsagePopover::tr("No usage data.");
        else {
            // Last good numbers stay on screen; a failed refresh only adds the notice.
            showBars = true;
            if (failing)
                hint = staleNotice(a.health, now);
        }
        m_hint->setText(hint);
        m_hint->setVisible(!hint.isEmpty());

        const int want = showBars ? int(windows.size()) : 0;
        while (m_bars.size() > want)
            delete m_bars.takeLast();
        while (m_bars.size() < want) {
            auto *bar = new UsageWindowBar(this);
            layout()->addWidget(bar);
            m_bars.push_back(bar);
        }
        for (int i = 0; i < want; ++i)
            m_bars[i]->setWindow(windows[i].label, windows[i].window, mode, a.nowMs);
    }

    void applyChrome()
    {
        m_icon->setPixmap(usagestyle::brandPixmap(QStringLiteral(":/icons/claude.svg"), kProviderIcon,
                                                  devicePixelRatioF()));
    }

private:
    QLabel *m_icon = nullptr;
    QLabel *m_provider = nullptr;
    QLabel *m_account = nullptr;
    QLabel *m_hint = nullptr;
    QVector<UsageWindowBar *> m_bars;
};

UsagePopover::UsagePopover(QWidget *parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setObjectName(QStringLiteral("usagePopover"));
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(kWidth);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(kPadding, kPadding, kPadding, kPadding);
    root->setSpacing(kSpacing);

    // Header: title, "% used" toggle, refresh.
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(4);
    header->addWidget(boldLabel(tr("Usage"), this));
    header->addStretch(1);

    m_modeButton = new QToolButton(this);
    m_modeButton->setObjectName(QStringLiteral("buttonUsagePercentMode"));
    m_modeButton->setAutoRaise(true);
    m_modeButton->setFixedHeight(20);
    m_modeButton->setCursor(Qt::PointingHandCursor);
    connect(m_modeButton, &QToolButton::clicked, this, &UsagePopover::percentModeToggleRequested);
    header->addWidget(m_modeButton);

    m_refreshButton = new QToolButton(this);
    m_refreshButton->setObjectName(QStringLiteral("buttonUsageRefresh"));
    m_refreshButton->setAutoRaise(true);
    m_refreshButton->setFixedSize(kRefreshButton, kRefreshButton);
    m_refreshButton->setIconSize({kRefreshIcon, kRefreshIcon});
    m_refreshButton->setToolTip(tr("Refresh usage"));
    m_refreshButton->setAccessibleName(tr("Refresh usage"));
    connect(m_refreshButton, &QToolButton::clicked, this, &UsagePopover::refreshRequested);
    header->addWidget(m_refreshButton);
    root->addLayout(header);

    root->addWidget(new Divider(this));

    m_sectionsLayout = new QVBoxLayout;
    m_sectionsLayout->setContentsMargins(0, 0, 0, 0);
    m_sectionsLayout->setSpacing(kSpacing);
    root->addLayout(m_sectionsLayout);

    // Only runs while the popup is open and a failure notice is showing.
    m_staleTimer = new QTimer(this);
    m_staleTimer->setInterval(kStaleTickMs);
    connect(m_staleTimer, &QTimer::timeout, this, [this] {
        if (std::any_of(m_accounts.cbegin(), m_accounts.cend(),
                        [](const Account &a) { return !a.health.error.isEmpty(); }))
            rebuild();
    });

    m_spin = new QVariantAnimation(this);
    m_spin->setStartValue(0.0);
    m_spin->setEndValue(360.0);
    m_spin->setDuration(1000);
    m_spin->setLoopCount(-1);
    m_spin->setEasingCurve(QEasingCurve::Linear);
    connect(m_spin, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &v) { updateRefreshIcon(v.toReal()); });

    applyChrome();
    setPercentMode(PercentMode::Used);
}

void UsagePopover::setData(QVector<Account> accounts)
{
    m_accounts = std::move(accounts);
    rebuild();
}

void UsagePopover::setPercentMode(PercentMode mode)
{
    m_mode = mode;
    m_modeButton->setText(mode == PercentMode::Used ? tr("% used") : tr("% left"));
    m_modeButton->setToolTip(mode == PercentMode::Used ? tr("Show percent remaining") : tr("Show percent used"));
    rebuild();
}

void UsagePopover::rebuild()
{
    // Countdown text needs the real clock, not the last-completed stamp.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    const int want = int(m_accounts.size());
    while (m_sections.size() > want) {
        delete m_sections.takeLast();
        delete m_dividers.takeLast();
    }
    while (m_sections.size() < want) {
        auto *divider = new Divider(this);
        divider->setVisible(!m_sections.isEmpty());
        m_sectionsLayout->addWidget(divider);
        m_dividers.push_back(divider);
        auto *section = new UsageAccountSection(this);
        m_sectionsLayout->addWidget(section);
        m_sections.push_back(section);
    }

    // Refresh is a no-op only when every account is fetching or cooling down.
    bool anyLoading = false;
    bool anyRefreshable = false;
    for (int i = 0; i < want; ++i) {
        const Account &a = m_accounts[i];
        m_sections[i]->setAccount(a, m_mode, now);
        anyLoading = anyLoading || a.loading;
        anyRefreshable = anyRefreshable || (!a.loading && a.health.retryAtMs <= now);
    }

    m_refreshButton->setEnabled(anyRefreshable);
    setSpinning(anyLoading && isVisible());

    if (isVisible()) {
        layout()->activate();
        resize(kWidth, sizeHint().height());
    }
}

void UsagePopover::updateRefreshIcon(qreal angle)
{
    const qreal dpr = devicePixelRatioF();
    QPixmap base = usagestyle::tintedPixmap(QStringLiteral(":/icons/refresh-cw.svg"),
                                            palette().color(QPalette::PlaceholderText), kRefreshIcon, dpr);
    if (angle != 0.0 && !base.isNull()) {
        QPixmap rotated(base.size());
        rotated.setDevicePixelRatio(dpr);
        rotated.fill(Qt::transparent);
        QPainter p(&rotated);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const qreal c = kRefreshIcon / 2.0;
        p.translate(c, c);
        p.rotate(angle);
        p.translate(-c, -c);
        p.drawPixmap(0, 0, base);
        base = rotated;
    }
    m_refreshButton->setIcon(QIcon(base));
}

void UsagePopover::setSpinning(bool on)
{
    // The 1s loop only runs while the popup is open and a fetch is in flight.
    if (on == (m_spin->state() == QAbstractAnimation::Running))
        return;
    if (on) {
        m_spin->start();
    } else {
        m_spin->stop();
        updateRefreshIcon(0.0);
    }
}

void UsagePopover::showBelow(QWidget *anchor)
{
    rebuild();
    layout()->activate();
    resize(kWidth, sizeHint().height());

    const QPoint bottomRight = anchor->mapToGlobal(QPoint(anchor->width(), anchor->height()));
    const QRect avail = anchor->screen()->availableGeometry();

    QPoint pos(bottomRight.x() - width(), bottomRight.y() + kAnchorGap);
    pos.setX(std::clamp(pos.x(), avail.left() + kScreenMargin,
                        std::max(avail.left() + kScreenMargin, avail.right() - width() - kScreenMargin + 1)));
    if (pos.y() + height() > avail.bottom())
        pos.setY(std::max(avail.top(), anchor->mapToGlobal(QPoint(0, 0)).y() - height() - kAnchorGap));
    move(pos);

    show();
    setSpinning(std::any_of(m_accounts.cbegin(), m_accounts.cend(), [](const Account &a) { return a.loading; }));
    m_staleTimer->start();
}

void UsagePopover::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Raised surface: nudge the window color toward the text color so it lifts
    // off the chrome in both light and dark themes.
    const QColor win = palette().color(QPalette::Window);
    const QColor fg = palette().color(QPalette::WindowText);
    const QColor bg(win.red() + (fg.red() - win.red()) / 14,
                    win.green() + (fg.green() - win.green()) / 14,
                    win.blue() + (fg.blue() - win.blue()) / 14);

    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
}

void UsagePopover::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange
        || event->type() == QEvent::ApplicationPaletteChange)
        applyChrome();
}

// Theme-dependent bits that are not painted from the live palette: the tinted
// SVG pixmaps and the link-colored toggle.
void UsagePopover::applyChrome()
{
    for (UsageAccountSection *section : std::as_const(m_sections))
        section->applyChrome();
    updateRefreshIcon(0.0);

    // The "% used" toggle reads as a link.
    QPalette pal = m_modeButton->palette();
    pal.setColor(QPalette::ButtonText, palette().color(QPalette::Link));
    pal.setColor(QPalette::WindowText, palette().color(QPalette::Link));
    m_modeButton->setPalette(pal);
}

void UsagePopover::hideEvent(QHideEvent *event)
{
    setSpinning(false);
    m_staleTimer->stop();
    QFrame::hideEvent(event);
    emit closed();
}
