/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "AcpSessionView.h"

#include "AcpAgentRegistry.h"
#include "AcpConnection.h"
#include "AcpImageAttachmentList.h"
#include "AcpMessageWidget.h"
#include "AcpPermissionPrompt.h"
#include "AcpPlanWidget.h"
#include "AcpSessionModel.h"
#include "AcpToolCallCard.h"
#include "AcpUsageIndicator.h"
#include "AiAgentDock.h"
#include "ApplicationSettings.h"
#include "NotepadNextApplication.h"
#include "ai/CredentialStore.h"
#include "ai/PromptImprover.h"

#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// Treat any scrollbar value within this many pixels of the maximum as "at
// bottom". Used both for the user's at-bottom recognition (the flag flips on
// when they reach this band) and for the programmatic scroll target.
constexpr int kAtBottomEpsilonPx = 8;

// SVG icons that use stroke="currentColor" resolve to opaque black under Qt's
// svg icon engine, so they vanish on dark backgrounds. Re-render the icon at
// the sizes Qt is likely to ask for and tint each pixmap via SourceIn so the
// alpha (the strokes) is preserved while the rgb becomes the palette color.
QIcon tintedIcon(const QString &svgPath, const QColor &color)
{
    QIcon source(svgPath);
    if (source.isNull()) return source;

    QIcon dst;
    const QList<int> sizes{16, 20, 22, 24, 32, 48, 64};
    for (int sz : sizes) {
        QPixmap pm = source.pixmap(sz, sz);
        if (pm.isNull()) continue;
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), color);
        p.end();
        dst.addPixmap(pm);
    }
    return dst;
}

ApplicationSettings *appSettings()
{
    return qApp ? qApp->findChild<ApplicationSettings *>() : nullptr;
}

// Custom QPlainTextEdit that submits on Enter (Shift+Enter for newline) and
// forwards image pastes/drops to the AcpImageAttachmentList.
class ChatInputEditCb : public QPlainTextEdit
{
public:
    ChatInputEditCb(AcpImageAttachmentList *attachments, QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
        , m_attachments(attachments)
    {
        setPlaceholderText(QObject::tr("Send a message"));
        setTabChangesFocus(true);
    }

    std::function<void()> onSubmit;
    std::function<void()> onRestart;
    std::function<bool(QKeyEvent *)> onKeyFilter;

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::ShortcutOverride) {
            auto *ke = static_cast<QKeyEvent *>(e);
            if (ke->key() == Qt::Key_N && ke->modifiers() == Qt::ControlModifier && onRestart) {
                e->accept();
                return true;
            }
        }
        if (e->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(e);
            if (ke->key() == Qt::Key_Tab && onKeyFilter && onKeyFilter(ke))
                return true;
        }
        return QPlainTextEdit::event(e);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_N && event->modifiers() == Qt::ControlModifier) {
            if (onRestart) { onRestart(); return; }
        }
        if (onKeyFilter && onKeyFilter(event))
            return;
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && !(event->modifiers() & Qt::ShiftModifier)) {
            if (onSubmit) onSubmit();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }

    void insertFromMimeData(const QMimeData *source) override
    {
        if (m_attachments && (source->hasImage() || source->hasUrls())) {
            if (source->hasUrls()) {
                const QList<QUrl> urls = source->urls();
                for (const QUrl &u : urls) {
                    if (u.isLocalFile()) {
                        m_attachments->addFileByPath(u.toLocalFile());
                    }
                }
            }
            if (source->hasImage()) {
                // Snipping Tool / Print Screen put a raw bitmap on the clipboard,
                // not PNG bytes — fall back to the QImage payload and re-encode.
                QByteArray raw = source->data(QStringLiteral("image/png"));
                if (raw.isEmpty()) {
                    const QImage img = qvariant_cast<QImage>(source->imageData());
                    if (!img.isNull()) {
                        QBuffer buf(&raw);
                        buf.open(QIODevice::WriteOnly);
                        img.save(&buf, "PNG");
                    }
                }
                if (!raw.isEmpty()) {
                    m_attachments->tryAddImage(raw, QObject::tr("pasted.png"));
                }
            }
            return;
        }
        QPlainTextEdit::insertFromMimeData(source);
    }

private:
    AcpImageAttachmentList *m_attachments;
};

} // namespace

AcpSessionView::AcpSessionView(AcpSessionModel *model,
                               AcpConnection *connection,
                               AcpAgentRegistry *registry,
                               QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_connection(connection)
    , m_registry(registry)
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    buildUi();
    wireSignals();
    hydrateFromModel();
}

AcpSessionView::~AcpSessionView() = default;

QSize AcpSessionView::sizeHint() const
{
    return QSize(420, 300);
}

QSize AcpSessionView::minimumSizeHint() const
{
    return QSize(200, 100);
}

void AcpSessionView::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    // 1. Status banner.
    m_banner = new QFrame(this);
    m_banner->setObjectName(QStringLiteral("AcpStatusBanner"));
    // Soft warm backgrounds (Bootstrap "alert" pair) hold across themes; in
    // dark mode the widget text role flips to white, so we must pin a dark
    // text colour on the banner contents in the warning/error variants —
    // otherwise we get white-on-pink / white-on-yellow. Bootstrap's matching
    // text tokens are #856404 (warning) and #721c24 (error).
    m_banner->setStyleSheet(QStringLiteral(
        "QFrame#AcpStatusBanner { background: transparent; border: none; border-radius: 4px; padding: 0px; }"
        "QFrame#AcpStatusBanner[bannerKind=\"warning\"] { background: #fff3cd; border: 1px solid #ffeeba; padding: 4px; }"
        "QFrame#AcpStatusBanner[bannerKind=\"warning\"] QLabel,"
        "QFrame#AcpStatusBanner[bannerKind=\"warning\"] QPushButton,"
        "QFrame#AcpStatusBanner[bannerKind=\"warning\"] QToolButton { color: #856404; }"
        "QFrame#AcpStatusBanner[bannerKind=\"warning\"] QPushButton {"
        " background: rgba(255, 255, 255, 160); border: 1px solid #ffeeba; border-radius: 3px; padding: 2px 8px; }"
        "QFrame#AcpStatusBanner[bannerKind=\"warning\"] QPushButton:hover { background: rgba(255, 255, 255, 220); }"
        "QFrame#AcpStatusBanner[bannerKind=\"warning\"] QToolButton:hover {"
        " color: #533f03; border: 1px solid #856404; }"
        "QFrame#AcpStatusBanner[bannerKind=\"error\"] { background: #f8d7da; border: 1px solid #f5c6cb; padding: 4px; }"
        "QFrame#AcpStatusBanner[bannerKind=\"error\"] QLabel,"
        "QFrame#AcpStatusBanner[bannerKind=\"error\"] QPushButton,"
        "QFrame#AcpStatusBanner[bannerKind=\"error\"] QToolButton { color: #721c24; }"
        "QFrame#AcpStatusBanner[bannerKind=\"error\"] QPushButton {"
        " background: rgba(255, 255, 255, 160); border: 1px solid #f5c6cb; border-radius: 3px; padding: 2px 8px; }"
        "QFrame#AcpStatusBanner[bannerKind=\"error\"] QPushButton:hover { background: rgba(255, 255, 255, 220); }"
        "QFrame#AcpStatusBanner[bannerKind=\"error\"] QToolButton:hover {"
        " color: #491217; border: 1px solid #721c24; }"));
    auto *banL = new QHBoxLayout(m_banner);
    banL->setContentsMargins(0, 0, 0, 0);
    banL->setSpacing(4);
    m_bannerLabel = new QLabel(m_banner);
    m_bannerLabel->setWordWrap(true);
    m_bannerRetry = new QPushButton(tr("Retry"), m_banner);
    m_bannerRetry->hide();
    connect(m_bannerRetry, &QPushButton::clicked, this, [this]() {
        clearBanner();
        emit retryRequested();
    });
    m_bannerRestart = new QToolButton(m_banner);
    m_bannerRestart->setText(tr("Restart"));
    m_bannerRestart->setAutoRaise(true);
    m_bannerRestart->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_bannerRestart->setToolTip(tr("Restart this ACP session"));
    m_bannerRestart->setStyleSheet(QStringLiteral(
        "QToolButton { color: palette(placeholder-text); padding: 1px 6px; border: 1px solid transparent; border-radius: 3px; }"
        "QToolButton:hover { color: palette(text); border: 1px solid palette(mid); }"));
    connect(m_bannerRestart, &QToolButton::clicked, this, &AcpSessionView::restartSessionRequested);
    m_bannerDebug = new QToolButton(m_banner);
    m_bannerDebug->setText(tr("Debug"));
    m_bannerDebug->setAutoRaise(true);
    m_bannerDebug->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_bannerDebug->setToolTip(tr("Show ACP protocol log for this session"));
    m_bannerDebug->setStyleSheet(QStringLiteral(
        "QToolButton { color: palette(placeholder-text); padding: 1px 6px; border: 1px solid transparent; border-radius: 3px; }"
        "QToolButton:hover { color: palette(text); border: 1px solid palette(mid); }"));
    connect(m_bannerDebug, &QToolButton::clicked, this, &AcpSessionView::onShowDebugLogClicked);
    banL->addWidget(m_bannerLabel, 1);
    banL->addStretch();
    banL->addWidget(m_bannerRetry);
    banL->addWidget(m_bannerRestart);
    banL->addWidget(m_bannerDebug);
    // The banner stays visible at all times so the Debug button is always
    // reachable — even when there's no error to surface. clearBanner() hides
    // the label + Retry so the row looks neutral. In neutral state the banner
    // has no chrome (transparent background, no border, no padding) so the
    // small Debug toolbutton floats top-right without dominating the panel.
    m_bannerLabel->hide();
    m_banner->setProperty("bannerKind", QStringLiteral("info"));
    outer->addWidget(m_banner);

    // 2. Transcript area.
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_transcriptHost = new QWidget(m_scroll);
    m_transcriptHost->installEventFilter(this);
    m_transcriptLayout = new QVBoxLayout(m_transcriptHost);
    m_transcriptLayout->setContentsMargins(4, 4, 4, 4);
    m_transcriptLayout->setSpacing(6);

    // Inline heartbeat indicator. Lives at the tail of the transcript so it
    // visually trails the freshest bubble/card. Hidden outside processing;
    // shown while the agent is busy. Italic + muted = "status whisper".
    m_elapsedLabel = new QLabel(m_transcriptHost);
    m_elapsedLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: palette(placeholder-text); font-style: italic; }"));
    m_elapsedLabel->setToolTip(tr("Time since the last new event from the agent"));
    m_elapsedLabel->hide();
    m_transcriptLayout->addWidget(m_elapsedLabel);

    m_goalElapsedLabel = new QLabel(m_transcriptHost);
    m_goalElapsedLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: rgb(180, 140, 50); font-style: italic; font-weight: 600; font-size: 11px; }"));
    m_goalElapsedLabel->hide();
    m_transcriptLayout->addWidget(m_goalElapsedLabel);

    m_transcriptLayout->addStretch();

    m_scroll->setWidget(m_transcriptHost);
    syncTranscriptHostWidth();
    outer->addWidget(m_scroll, 1);

    // Jump-to-bottom overlay. Parented to the scroll area's viewport (not the
    // transcript host) so the button stays pinned to the bottom-right of the
    // visible region instead of scrolling away with the content.
    m_jumpToBottom = new QToolButton(m_scroll->viewport());
    m_jumpToBottom->setText(QStringLiteral("↓"));
    m_jumpToBottom->setToolTip(tr("Jump to bottom"));
    m_jumpToBottom->setAutoRaise(false);
    m_jumpToBottom->setStyleSheet(QStringLiteral(
        "QToolButton { background: palette(button); border: 1px solid palette(mid); border-radius: 12px; min-width: 24px; min-height: 24px; }"));
    m_jumpToBottom->hide();
    m_scroll->viewport()->installEventFilter(this);
    connect(m_jumpToBottom, &QToolButton::clicked, this, &AcpSessionView::onJumpToBottomClicked);

    if (auto *vbar = m_scroll->verticalScrollBar()) {
        // User-driven scroll updates the stick-to-bottom flag. Our own
        // setValue() calls are bracketed by m_programmaticScroll so they
        // don't flip the flag based on the value we just wrote.
        connect(vbar, &QScrollBar::valueChanged, this, [this](int v) {
            if (m_programmaticScroll) return;
            auto *vb = m_scroll ? m_scroll->verticalScrollBar() : nullptr;
            if (!vb) return;
            m_stickToBottom = (v >= vb->maximum() - kAtBottomEpsilonPx);
            updateJumpButtonVisibility();
        });
        // Content growth (new bubble, streamed chunk reflow) changes the
        // range. When stuck, follow the new bottom; otherwise leave the
        // viewport where the user parked it.
        connect(vbar, &QScrollBar::rangeChanged, this, [this](int, int) {
            if (m_stickToBottom) {
                scrollToBottomDeferred();
            } else {
                updateJumpButtonVisibility();
            }
        });
    }

    // 3. Selectors (model / mode / effort). Constructed here so other
    // buildUi() steps and hydrateFromModel() can populate them; placed into
    // the button row below so the chat surface keeps a single chrome row.
    // AdjustToContents lets each combo hug its current selection — no fixed
    // pixel widths, no row of equally-stretched dropdowns competing with the
    // Send button for space.
    const QString kCompactComboCss = QStringLiteral(
        "QComboBox { padding: 1px 4px; border: 1px solid palette(mid); border-radius: 3px; }"
        "QComboBox::drop-down { width: 14px; }");
    m_modelCombo = new QComboBox(this);
    m_modelCombo->setToolTip(tr("Model"));
    m_modelCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_modelCombo->setStyleSheet(kCompactComboCss);
    m_modelCombo->hide();
    m_modeCombo = new QComboBox(this);
    m_modeCombo->setToolTip(tr("Mode"));
    m_modeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_modeCombo->setStyleSheet(kCompactComboCss);
    m_modeCombo->hide();
    m_effortCombo = new QComboBox(this);
    m_effortCombo->setToolTip(tr("Effort"));
    m_effortCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_effortCombo->setStyleSheet(kCompactComboCss);
    m_effortCombo->hide();

    // 4. Auto-approve checkbox. The toggle is dangerous (the agent can run
    // any tool unattended), so the checked state must be unmistakable — but
    // a full-bleed warning-yellow block is chrome-shouting. Per the DNA, the
    // soft warning palette is reserved for transient banners; here we settle
    // for warning-tone bold text + the checkbox indicator itself.
    m_autoApproveCheck = new QCheckBox(tr("Auto-approve permissions"), this);
    m_autoApproveCheck->setToolTip(tr("Automatically allow all tool calls this agent requests"));
    m_autoApproveCheck->setStyleSheet(QStringLiteral(
        "QCheckBox:checked { color: #856404; font-weight: 600; }"));
    auto *autoApproveRow = new QHBoxLayout();
    autoApproveRow->setContentsMargins(0, 0, 0, 0);
    autoApproveRow->setSpacing(6);
    autoApproveRow->addWidget(m_autoApproveCheck);
    autoApproveRow->addStretch();
    m_usageIndicator = new AcpUsageIndicator(this);
    autoApproveRow->addWidget(m_usageIndicator);
    outer->addLayout(autoApproveRow);

    // 5. Image attachments.
    m_attachmentList = new AcpImageAttachmentList(this);
    outer->addWidget(m_attachmentList);

    // 5b. Goal status row — hidden until a goal is active.
    m_goalStatusRow = new QFrame(this);
    m_goalStatusRow->setFrameShape(QFrame::NoFrame);
    m_goalStatusRow->setStyleSheet(QStringLiteral(
        "QFrame { background: rgba(180, 140, 50, 32); border: 1px solid rgba(180, 140, 50, 60); border-radius: 4px; }"));
    auto *goalRowLayout = new QHBoxLayout(m_goalStatusRow);
    goalRowLayout->setContentsMargins(8, 4, 8, 4);
    goalRowLayout->setSpacing(6);
    m_goalStatusLabel = new QLabel(m_goalStatusRow);
    m_goalStatusLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; border: none; color: rgb(180, 140, 50); font-weight: 600; font-size: 11px; }"));
    goalRowLayout->addWidget(m_goalStatusLabel, 1);
    m_goalStopBtn = new QToolButton(m_goalStatusRow);
    m_goalStopBtn->setText(tr("Stop"));
    m_goalStopBtn->setAutoRaise(true);
    m_goalStopBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_goalStopBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; color: rgb(180, 100, 50); font-size: 11px; font-weight: 600; padding: 1px 6px; border-radius: 3px; }"
        "QToolButton:hover { background: rgba(180, 100, 50, 40); }"));
    m_goalStopBtn->setToolTip(tr("Stop the running goal"));
    connect(m_goalStopBtn, &QToolButton::clicked, this, &AcpSessionView::goalStopRequested);
    goalRowLayout->addWidget(m_goalStopBtn);
    m_goalStatusRow->hide();
    outer->addWidget(m_goalStatusRow);

    // 6. Input.
    auto *cb = new ChatInputEditCb(m_attachmentList, this);
    cb->onSubmit = [this]() { onSendClicked(); };
    cb->onRestart = [this]() { emit restartSessionRequested(); };
    cb->onKeyFilter = [this](QKeyEvent *ke) -> bool {
        // Esc cancels prompt improvement if streaming.
        if (ke->key() == Qt::Key_Escape
            && m_promptImprover
            && m_promptImprover->state() == ai::PromptImprover::State::Streaming) {
            onImproveClicked(); // toggles to cancel
            return true;
        }
        if (!m_commandPopup || !m_commandPopup->isVisible())
            return false;
        switch (ke->key()) {
        case Qt::Key_Up: {
            int cur = m_commandPopup->currentRow();
            int found = -1;
            for (int i = cur - 1; i >= 0; --i) {
                if (!m_commandPopup->item(i)->isHidden()) { found = i; break; }
            }
            if (found < 0) {
                for (int i = m_commandPopup->count() - 1; i > cur; --i) {
                    if (!m_commandPopup->item(i)->isHidden()) { found = i; break; }
                }
            }
            if (found >= 0) m_commandPopup->setCurrentRow(found);
            return true;
        }
        case Qt::Key_Down: {
            int cur = m_commandPopup->currentRow();
            int found = -1;
            for (int i = cur + 1; i < m_commandPopup->count(); ++i) {
                if (!m_commandPopup->item(i)->isHidden()) { found = i; break; }
            }
            if (found < 0) {
                for (int i = 0; i < cur; ++i) {
                    if (!m_commandPopup->item(i)->isHidden()) { found = i; break; }
                }
            }
            if (found >= 0) m_commandPopup->setCurrentRow(found);
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            acceptCommandCompletion();
            return true;
        case Qt::Key_Escape:
            hideCommandPopup();
            return true;
        case Qt::Key_Space:
            acceptCommandCompletion();
            return true;
        default:
            return false;
        }
    };
    cb->setMaximumHeight(120);
    m_input = cb;
    m_input->installEventFilter(this);
    m_input->viewport()->installEventFilter(this);
    outer->addWidget(m_input);

    // Slash-command completion popup (frameless, positioned above input).
    m_commandPopup = new QListWidget(this);
    m_commandPopup->setWindowFlags(Qt::ToolTip);
    m_commandPopup->setFocusPolicy(Qt::NoFocus);
    m_commandPopup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_commandPopup->hide();

    // 6b. Floating improve-prompt button (child of m_input frame, bottom-right).
    // Parent is m_input (not viewport) so it stays fixed when content scrolls.
    m_improveBtn = new QToolButton(m_input);
    m_improveBtn->setAutoRaise(true);
    m_improveBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_improveBtn->setText(QStringLiteral("✨"));
    m_improveBtn->setToolTip(tr("Improve prompt with AI (Ctrl+I)"));
    m_improveBtn->setAccessibleName(tr("Improve prompt with AI"));
    m_improveBtn->setAccessibleDescription(tr("Rewrite the current prompt to be clearer (Ctrl+I)"));
    m_improveBtn->setCursor(Qt::PointingHandCursor);
    m_improveBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; padding: 2px; border-radius: 3px; font-size: 14px; }"
        "QToolButton:hover { background: palette(midlight); }"
        "QToolButton:disabled { opacity: 0.3; }"));
    m_improveBtn->hide();
    connect(m_improveBtn, &QToolButton::clicked, this, &AcpSessionView::onImproveClicked);

    // Ctrl+I shortcut for improve (scoped to input widget).
    m_improveShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_I), m_input);
    m_improveShortcut->setContext(Qt::WidgetShortcut);
    connect(m_improveShortcut, &QShortcut::activated, this, &AcpSessionView::onImproveClicked);

    // 7. Button row + usage. Attach is a secondary chrome action — render it
    // as an icon-only flat tool-button so Send remains the only prominent
    // button and the row stays compact.
    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(4);
    m_attachBtn = new QToolButton(this);
    m_attachBtn->setAutoRaise(true);
    m_attachBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_attachBtn->setToolTip(tr("Attach an image (or paste from clipboard)"));
    m_attachBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: palette(placeholder-text); padding: 4px; border: 1px solid transparent; border-radius: 3px; }"
        "QToolButton:hover { color: palette(text); border: 1px solid palette(mid); }"));
    rebuildAttachIcon();
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_sendBtn = new QPushButton(tr("Send"), this);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->hide();
    btnRow->addWidget(m_attachBtn);
    btnRow->addWidget(m_modelCombo);
    btnRow->addWidget(m_modeCombo);
    btnRow->addWidget(m_effortCombo);
    btnRow->addStretch();

    m_elapsedTimer = new QTimer(this);
    m_elapsedTimer->setInterval(100); // 0.1 s precision
    connect(m_elapsedTimer, &QTimer::timeout, this, &AcpSessionView::onElapsedTick);

    m_goalElapsedTimer = new QTimer(this);
    m_goalElapsedTimer->setInterval(100);
    connect(m_goalElapsedTimer, &QTimer::timeout, this, [this]() {
        m_goalElapsedMs += 100;
        if (m_goalElapsedLabel) {
            const int whole = m_goalElapsedMs / 1000;
            const int tenths = (m_goalElapsedMs % 1000) / 100;
            m_goalElapsedLabel->setText(tr("Goal running · %1.%2s").arg(whole).arg(tenths));
        }
    });

    // Input-placeholder busy clock — repaints the elapsed-time placeholder once
    // a second while any agent is working. The underlying span is measured by
    // the monotonic m_busyClock; this timer only triggers the text refresh, so
    // a missed tick (event-loop stall) self-corrects on the next fire.
    m_busyPlaceholderTimer = new QTimer(this);
    m_busyPlaceholderTimer->setInterval(1000);
    connect(m_busyPlaceholderTimer, &QTimer::timeout,
            this, &AcpSessionView::updateBusyPlaceholderText);

    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_sendBtn);

    m_sendWithGoalBtn = new QToolButton(this);
    m_sendWithGoalBtn->setText(tr("Goal"));
    m_sendWithGoalBtn->setAutoRaise(true);
    m_sendWithGoalBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_sendWithGoalBtn->setToolTip(tr("Send with Goal — evaluate success criteria automatically"));
    m_sendWithGoalBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: rgb(180, 140, 50); padding: 1px 6px; border: 1px solid rgba(180, 140, 50, 80); border-radius: 3px; }"
        "QToolButton:hover { color: rgb(200, 160, 60); background: rgba(180, 140, 50, 32); border: 1px solid rgba(180, 140, 50, 120); }"));
    connect(m_sendWithGoalBtn, &QToolButton::clicked, this, &AcpSessionView::sendWithGoalRequested);
    btnRow->addWidget(m_sendWithGoalBtn);

    outer->addLayout(btnRow);

    connect(m_sendBtn,   &QPushButton::clicked, this, &AcpSessionView::onSendClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &AcpSessionView::onCancelClicked);
    connect(m_attachBtn, &QToolButton::clicked, this, &AcpSessionView::onAttachClicked);

    // Auto-approve state from registry.
    if (m_registry) {
        m_autoApproveCheck->setChecked(m_registry->autoApprovePolicy() == QLatin1String("allowAll"));
    }
    connect(m_autoApproveCheck, &QCheckBox::toggled,
            this, &AcpSessionView::onAutoApproveToggled);

    // Send-button enable state tracks attachment list non-empty / input text.
    connect(m_input, &QPlainTextEdit::textChanged, this, [this]() {
        const bool hasContent = !m_input->toPlainText().trimmed().isEmpty() || m_attachmentList->isNonEmpty();
        m_sendBtn->setEnabled(hasContent && (!m_model || !m_model->isProcessing()));
        filterCommandPopup();
        updateImproveButtonState();
    });
    connect(m_attachmentList, &AcpImageAttachmentList::contentsChanged, this, [this]() {
        const bool hasContent = !m_input->toPlainText().trimmed().isEmpty() || m_attachmentList->isNonEmpty();
        m_sendBtn->setEnabled(hasContent && (!m_model || !m_model->isProcessing()));
    });
    connect(m_attachmentList, &AcpImageAttachmentList::imageRejected, this, [this](const QString &reason) {
        setBanner(reason, BannerKind::Warning);
        QTimer::singleShot(2500, this, &AcpSessionView::clearBanner);
    });

    // Selector connections.
    connect(m_modelCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AcpSessionView::onModelComboChanged);
    connect(m_modeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AcpSessionView::onModeComboChanged);
    connect(m_effortCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AcpSessionView::onEffortComboChanged);

    // Apply the chat font preference to transcript + input, and follow live
    // preference edits. The chat font may be independent of the editor font:
    // when "Use default font" is on it tracks the editor's Default Font (the
    // first three signals); when off it uses the ChatFont/* settings (the last
    // four). Connecting all of them keeps the view live either way. Chrome
    // (banner, selectors, buttons) keeps the system font.
    if (auto *settings = appSettings()) {
        connect(settings, &ApplicationSettings::fontNameChanged,
                this, &AcpSessionView::applyChatFont);
        connect(settings, &ApplicationSettings::fontSizeChanged,
                this, &AcpSessionView::applyChatFont);
        connect(settings, &ApplicationSettings::fontHintingChanged,
                this, &AcpSessionView::applyChatFont);
        connect(settings, &ApplicationSettings::chatFontUseDefaultChanged,
                this, &AcpSessionView::applyChatFont);
        connect(settings, &ApplicationSettings::chatFontFamilyChanged,
                this, &AcpSessionView::applyChatFont);
        connect(settings, &ApplicationSettings::chatFontSizePtChanged,
                this, &AcpSessionView::applyChatFont);
        connect(settings, &ApplicationSettings::chatFontSharpenChanged,
                this, &AcpSessionView::applyChatFont);
    }
    applyChatFont();

    // Prompt improver — uses the same AI provider as commit message generation.
    {
        auto *npApp = qobject_cast<NotepadNextApplication *>(qApp);
        auto *settings = appSettings();
        ai::CredentialStore *credStore = npApp ? npApp->getCredentialStore() : nullptr;
        if (settings) {
            m_promptImprover = new ai::PromptImprover(settings, credStore, this);
            connect(m_promptImprover, &ai::PromptImprover::finished,
                    this, &AcpSessionView::onImproveFinished);
            connect(m_promptImprover, &ai::PromptImprover::errorOccurred,
                    this, &AcpSessionView::onImproveError);
            connect(m_promptImprover, &ai::PromptImprover::stateChanged,
                    this, [this]() { updateImproveButtonState(); });
            connect(m_promptImprover, &ai::PromptImprover::imagesBudgeted,
                    this, [this](int sent, int total) {
                setBanner(tr("Prompt improvement: sent %1 of %2 images (10 MB limit)")
                          .arg(sent).arg(total), BannerKind::Info);
                QTimer::singleShot(4000, this, &AcpSessionView::clearBanner);
            });
        }
    }
}

void AcpSessionView::wireSignals()
{
    if (m_model) {
        connect(m_model, &AcpSessionModel::messageAppended,
                this, &AcpSessionView::onMessageAppended);
        connect(m_model, &AcpSessionModel::messageChunkAppended,
                this, &AcpSessionView::onMessageChunkAppended);
        connect(m_model, &AcpSessionModel::messageReplaced,
                this, &AcpSessionView::onMessageReplaced);
        connect(m_model, &AcpSessionModel::thoughtAppended,
                this, &AcpSessionView::onThoughtAppended);
        connect(m_model, &AcpSessionModel::thoughtChunkAppended,
                this, &AcpSessionView::onThoughtChunkAppended);
        connect(m_model, &AcpSessionModel::toolCallAddedOrUpdated,
                this, &AcpSessionView::onToolCallAddedOrUpdated);
        connect(m_model, &AcpSessionModel::planUpdated,
                this, &AcpSessionView::onPlanUpdated);
        connect(m_model, &AcpSessionModel::usageChanged,
                this, &AcpSessionView::onUsageChanged);
        connect(m_model, &AcpSessionModel::metadataChanged,
                this, &AcpSessionView::onMetadataChanged);
        connect(m_model, &AcpSessionModel::currentModeChanged,
                this, &AcpSessionView::onCurrentModeChanged);
        connect(m_model, &AcpSessionModel::isProcessingChanged,
                this, &AcpSessionView::onIsProcessingChanged);
        connect(m_model, &AcpSessionModel::turnEnded,
                this, &AcpSessionView::onTurnEnded);
    }

    if (m_connection) {
        connect(m_connection, &AcpConnection::permissionRequested,
                this, &AcpSessionView::onPermissionRequested);
        connect(m_connection, &AcpConnection::requestFailed,
                this, &AcpSessionView::onRequestFailed);
        connect(m_connection, &AcpConnection::errorOccurred,
                this, &AcpSessionView::onErrorOccurred);
        connect(m_connection, &AcpConnection::planReceived,
                this, [this](const QList<AcpProtocol::AcpPlanEntry> &entries) {
            resetElapsed();
            if (!m_planWidget) {
                m_planWidget = new AcpPlanWidget(m_transcriptHost);
                connect(m_planWidget, &AcpPlanWidget::resumeRequested,
                        this, &AcpSessionView::onPlanResumeRequested);
                insertTimelineWidget(m_planWidget);
            }
            m_planWidget->setEntries(entries);
            m_planWidget->setAgentIdle(!m_model || !m_model->isProcessing());
            scrollToBottomDeferred();
        });
    }

    if (m_registry) {
        connect(m_registry, &AcpAgentRegistry::autoApprovePolicyChanged,
                this, &AcpSessionView::onAutoApprovePolicyChanged);
    }
}

void AcpSessionView::hydrateFromModel()
{
    if (!m_model) return;

    // Walk existing timeline entries and recreate widgets in order.
    const auto &timeline = m_model->timeline();
    const auto &messages = m_model->messages();
    const auto &toolCalls = m_model->toolCalls();
    for (const AcpTimelineEntry &entry : timeline) {
        if (entry.kind == AcpTimelineEntry::Kind::Message
            && entry.messageIndex >= 0
            && entry.messageIndex < messages.size()) {
            const AcpMessage &msg = messages.at(entry.messageIndex);
            auto *w = new AcpMessageWidget(msg.role, m_transcriptHost);
            w->setChatFont(chatFont()); // styled widget: must set font explicitly
            if (msg.fromGoalAgent) {
                w->setFromGoalAgent(true);
            }
            w->setContent(msg.content);
            insertTimelineWidget(w);
            m_messageWidgets.insert(entry.messageIndex, w);
        } else if (entry.kind == AcpTimelineEntry::Kind::ToolCall) {
            auto it = toolCalls.find(entry.toolCallId);
            if (it != toolCalls.end()) {
                auto *card = new AcpToolCallCard(it.value(), m_transcriptHost);
                card->setChatFont(chatFont()); // styled widget: must set font explicitly
                insertTimelineWidget(card);
                m_toolCallCards.insert(entry.toolCallId, card);
            }
        }
    }

    if (m_model->usage().has_value()) {
        m_usageIndicator->setUsage(m_model->usage());
    }

    onMetadataChanged();
    onIsProcessingChanged(m_model->isProcessing());
}

void AcpSessionView::setBanner(const QString &text, BannerKind kind)
{
    if (!m_banner) return;
    m_bannerLabel->setText(text);
    m_bannerLabel->setVisible(!text.isEmpty());
    QString kindStr;
    switch (kind) {
    case BannerKind::Info:    kindStr = QStringLiteral("info"); break;
    case BannerKind::Warning: kindStr = QStringLiteral("warning"); break;
    case BannerKind::Error:   kindStr = QStringLiteral("error"); break;
    }
    m_banner->setProperty("bannerKind", kindStr);
    // Re-polish parent + every child whose color is selected via the
    // [bannerKind=...] descendant rules. Without polishing the children,
    // Qt keeps their previously-resolved palette text colour.
    m_banner->style()->unpolish(m_banner);
    m_banner->style()->polish(m_banner);
    for (QWidget *child : m_banner->findChildren<QWidget *>()) {
        child->style()->unpolish(child);
        child->style()->polish(child);
    }
    m_bannerRetry->setVisible(kind == BannerKind::Error || kind == BannerKind::Warning);
    m_banner->show();
}

void AcpSessionView::clearBanner()
{
    if (!m_banner) return;
    // Keep the banner widget itself visible so the Debug button stays
    // reachable, but drop the colored error/warning styling and hide the
    // label + Retry button.
    m_bannerLabel->clear();
    m_bannerLabel->hide();
    m_bannerRetry->hide();
    m_banner->setProperty("bannerKind", QStringLiteral("info"));
    m_banner->style()->unpolish(m_banner);
    m_banner->style()->polish(m_banner);
    for (QWidget *child : m_banner->findChildren<QWidget *>()) {
        child->style()->unpolish(child);
        child->style()->polish(child);
    }
}

void AcpSessionView::rebind(AcpSessionModel *model, AcpConnection *connection)
{
    // Detach from old model/connection — manager has (or is about to) delete
    // them, so disconnecting defends against late queued signals.
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
    }
    if (m_connection) {
        disconnect(m_connection, nullptr, this, nullptr);
        // Also sever the debug-dialog's live-update lambda which uses
        // m_debugDialog as context (not `this`), so the generic disconnect
        // above doesn't cover it.
        if (m_debugDialog) {
            disconnect(m_connection, &AcpConnection::debugLogAppended,
                       m_debugDialog, nullptr);
        }
    }

    m_model = model;
    m_connection = connection;
    m_savedPrefsApplied = false;

    // Clear the transcript: drop bubbles, cards, plan, permission prompts.
    // Leave the trailing stretch in place, and skip the inline heartbeat
    // (it's owned by the view, reused across session swaps).
    while (m_transcriptLayout->count() > 1) {
        QLayoutItem *item = m_transcriptLayout->itemAt(0);
        if (!item) break;
        QWidget *w = item->widget();
        if (w == m_elapsedLabel) {
            // The heartbeat is always the last widget before the stretch,
            // so reaching it means we're done.
            break;
        }
        m_transcriptLayout->takeAt(0);
        if (w) w->deleteLater();
        delete item;
    }
    m_messageWidgets.clear();
    m_toolCallCards.clear();
    m_currentGroupCards.clear();
    m_planWidget = nullptr;
    m_activeThought.clear();
    if (m_activePermissionPrompt) {
        m_activePermissionPrompt->deleteLater();
        m_activePermissionPrompt.clear();
    }

    clearBanner();
    if (m_usageIndicator) {
        m_usageIndicator->setUsage(std::nullopt);
    }

    // Fresh session → reset the auto-scroll flag to its default.
    m_stickToBottom = true;

    // Stop and hide the heartbeat indicator; onIsProcessingChanged will
    // restart it if the new session is already mid-turn.
    if (m_elapsedTimer) m_elapsedTimer->stop();
    if (m_elapsedLabel) m_elapsedLabel->hide();
    resetElapsed();

    // Stop goal heartbeat on rebind.
    if (m_goalElapsedTimer) m_goalElapsedTimer->stop();
    if (m_goalElapsedLabel) m_goalElapsedLabel->hide();
    clearGoalStatus();

    // If the debug-log popup is open, repoint its content at the new
    // connection's log so the user sees the freshly-restarted session.
    // Also rewire the live-update connection to the new AcpConnection.
    if (m_debugDialog && m_debugDialogText) {
        if (m_connection) {
            m_debugDialogText->setPlainText(m_connection->debugLog().join(QLatin1Char('\n')));
            // Wire live updates from the new connection.
            QPointer<AcpSessionView> self(this);
            connect(m_connection, &AcpConnection::debugLogAppended, m_debugDialog,
                    [self](const QString &line) {
                if (!self || !self->m_debugDialogText) return;
                const bool onlyGoal = self->m_debugDialogOnlyGoal
                                      && self->m_debugDialogOnlyGoal->isChecked();
                if (onlyGoal) return;
                QScrollBar *bar = self->m_debugDialogText->verticalScrollBar();
                const bool wasAtBottom = bar ? (bar->value() == bar->maximum()) : true;
                self->m_debugDialogText->appendPlainText(line);
                if (wasAtBottom && bar) bar->setValue(bar->maximum());
            });
        } else {
            m_debugDialogText->setPlainText(tr("(no active connection)"));
        }
    }

    // Restore Send/Cancel default visibility.
    onIsProcessingChanged(model ? model->isProcessing() : false);

    // Re-attach signals to the fresh model + connection and re-hydrate from
    // the (presumably empty) new model state.
    wireSignals();
    hydrateFromModel();
}

void AcpSessionView::insertTimelineWidget(QWidget *w)
{
    if (!w || !m_transcriptLayout) return;
    // Default: just before the trailing stretch.
    int idx = m_transcriptLayout->count() - 1;
    // If the heartbeat indicator is in the layout, land above it so the
    // heartbeat keeps trailing the freshest content. Hidden labels still
    // occupy a layout slot, so this works during off-turn idle too.
    if (m_elapsedLabel) {
        const int hbIdx = m_transcriptLayout->indexOf(m_elapsedLabel);
        if (hbIdx >= 0) {
            idx = hbIdx;
        }
    }
    m_transcriptLayout->insertWidget(idx, w);
    syncTranscriptHostWidth();
}

void AcpSessionView::syncTranscriptHostWidth()
{
    if (!m_scroll || !m_transcriptHost) return;
    QWidget *vp = m_scroll->viewport();
    if (!vp) return;
    const int width = vp->width();
    if (width > 0 && m_transcriptHost->width() != width) {
        m_transcriptHost->setFixedWidth(width);
    }
}

void AcpSessionView::appendMessageWidget(int idx)
{
    if (!m_model) return;
    if (idx < 0 || idx >= m_model->messages().size()) return;
    const AcpMessage &msg = m_model->messages().at(idx);

    auto *w = new AcpMessageWidget(msg.role, m_transcriptHost);
    w->setChatFont(chatFont()); // styled widget: must set font explicitly
    if (msg.fromGoalAgent) {
        w->setFromGoalAgent(true);
    }
    w->setContent(msg.content);
    insertTimelineWidget(w);
    m_messageWidgets.insert(idx, w);

    // If a thought was streaming and an assistant message starts, collapse it.
    if (msg.role == QLatin1String("assistant") && !m_activeThought.isNull()) {
        m_activeThought->markStreamingDone();
        m_activeThought.clear();
    }

    scrollToBottomDeferred();
}

void AcpSessionView::onMessageAppended(int idx)
{
    resetElapsed();
    appendMessageWidget(idx);
}

void AcpSessionView::onMessageChunkAppended(int idx, const QString &chunk)
{
    auto *w = m_messageWidgets.value(idx, nullptr);
    if (!w) {
        appendMessageWidget(idx);
        w = m_messageWidgets.value(idx, nullptr);
    }
    if (w) {
        w->appendChunk(chunk);
        if (w->role() == QLatin1String("assistant") && !m_activeThought.isNull()) {
            m_activeThought->markStreamingDone();
            m_activeThought.clear();
        }
        scrollToBottomDeferred();
    }
}

void AcpSessionView::onMessageReplaced(int idx, const QString &fullText)
{
    auto *w = m_messageWidgets.value(idx, nullptr);
    if (!w) {
        appendMessageWidget(idx);
        w = m_messageWidgets.value(idx, nullptr);
    }
    if (w) {
        w->setText(fullText);
        if (w->role() == QLatin1String("assistant") && !m_activeThought.isNull()) {
            m_activeThought->markStreamingDone();
            m_activeThought.clear();
        }
        scrollToBottomDeferred();
    }
}

void AcpSessionView::onThoughtAppended(int idx)
{
    resetElapsed();
    appendMessageWidget(idx);
    auto *w = m_messageWidgets.value(idx, nullptr);
    if (w && w->role() == QLatin1String("thought")) {
        m_activeThought = w;
    }
}

void AcpSessionView::onThoughtChunkAppended(int idx, const QString &chunk)
{
    auto *w = m_messageWidgets.value(idx, nullptr);
    if (!w) {
        appendMessageWidget(idx);
        w = m_messageWidgets.value(idx, nullptr);
        if (w && w->role() == QLatin1String("thought")) {
            m_activeThought = w;
        }
    }
    if (w) {
        w->appendChunk(chunk);
        scrollToBottomDeferred();
    }
}

void AcpSessionView::onToolCallAddedOrUpdated(const QString &toolCallId)
{
    if (!m_model) return;
    resetElapsed();
    auto it = m_model->toolCalls().find(toolCallId);
    if (it == m_model->toolCalls().end()) return;
    const AcpProtocol::AcpToolCall &tc = it.value();

    auto *card = m_toolCallCards.value(toolCallId, nullptr);
    if (!card) {
        card = new AcpToolCallCard(tc, m_transcriptHost);
        card->setChatFont(chatFont()); // styled widget: must set font explicitly
        insertTimelineWidget(card);
        m_toolCallCards.insert(toolCallId, card);
        m_currentGroupCards.append(card);
        scrollToBottomDeferred();
    } else {
        // Apply as an update: build an update payload from the latest state.
        AcpProtocol::AcpToolCallUpdate upd;
        upd.id = tc.id;
        upd.status = tc.status;
        upd.content = tc.content;
        if (!tc.rawInput.isEmpty()) {
            upd.rawInput = tc.rawInput;
        }
        card->apply(upd);
    }
}

void AcpSessionView::onPlanUpdated()
{
    // The plan is actually populated via the connection's planReceived signal
    // (which carries the entries). The model's planUpdated() carries no
    // entries; we keep the slot wired for symmetry with future model changes.
}

void AcpSessionView::onPlanResumeRequested(const QString &prompt)
{
    if (!m_connection || !m_model) return;
    if (m_model->isProcessing()) return;

    m_model->appendUserMessage(prompt, {});
    m_connection->sendPrompt(prompt, {});
    m_stickToBottom = true;
    scrollToBottomDeferred();
}

void AcpSessionView::onUsageChanged()
{
    if (m_model) m_usageIndicator->setUsage(m_model->usage());
}

void AcpSessionView::onMetadataChanged()
{
    if (!m_model) return;
    m_updatingSelectors = true;

    // Models combo. Prefer the top-level models catalog; if the agent
    // doesn't surface one, fall back to a `model` config option (Claude Code
    // exposes the picker only via configOptions, never the models array).
    m_modelCombo->clear();
    m_modelConfigOptionId.clear();
    const auto &models = m_model->availableModels();
    if (!models.isEmpty()) {
        for (const auto &m : models) {
            m_modelCombo->addItem(m.name.isEmpty() ? m.id : m.name, m.id);
        }
        if (!m_model->currentModelId().isEmpty()) {
            const int idx = m_modelCombo->findData(m_model->currentModelId());
            if (idx >= 0) m_modelCombo->setCurrentIndex(idx);
        }
    } else {
        for (const auto &opt : m_model->configOptions()) {
            const QString idLower = opt.id.toLower();
            const QString catLower = opt.category.toLower();
            const bool matches = idLower == QLatin1String("model")
                || catLower == QLatin1String("model");
            if (!matches) continue;
            m_modelConfigOptionId = opt.id;
            for (const auto &ch : opt.options) {
                const QString label = ch.name.isEmpty() ? ch.value : ch.name;
                if (!label.isEmpty()) m_modelCombo->addItem(label, ch.value);
            }
            const QString currentVal = opt.currentValue.toString();
            if (!currentVal.isEmpty()) {
                const int idx = m_modelCombo->findData(currentVal);
                if (idx >= 0) m_modelCombo->setCurrentIndex(idx);
            }
            break;
        }
    }
    m_modelCombo->setVisible(m_modelCombo->count() > 0);

    // Modes combo.
    m_modeCombo->clear();
    const auto &modes = m_model->availableModes();
    for (const auto &m : modes) {
        m_modeCombo->addItem(m.name.isEmpty() ? m.id : m.name, m.id);
    }
    if (!m_model->currentModeId().isEmpty()) {
        const int idx = m_modeCombo->findData(m_model->currentModeId());
        if (idx >= 0) m_modeCombo->setCurrentIndex(idx);
    }
    m_modeCombo->setVisible(!modes.isEmpty());

    // Effort/reasoning config-option combo. Matches on id, category, or name
    // so we pick up Claude Code's `effort` (category: "thought_level") as well
    // as agents that surface a different label.
    m_effortCombo->clear();
    m_effortConfigOptionId.clear();
    const auto &configOpts = m_model->configOptions();
    for (const auto &opt : configOpts) {
        const QString idLower = opt.id.toLower();
        const QString catLower = opt.category.toLower();
        const QString nameLower = opt.name.toLower();
        const bool matches = idLower.contains(QLatin1String("effort"))
            || idLower.contains(QLatin1String("reasoning"))
            || catLower.contains(QLatin1String("thought"))
            || catLower.contains(QLatin1String("reasoning"))
            || nameLower.contains(QLatin1String("effort"))
            || nameLower.contains(QLatin1String("reasoning"));
        if (!matches) continue;
        m_effortConfigOptionId = opt.id;
        for (const auto &ch : opt.options) {
            const QString label = ch.name.isEmpty() ? ch.value : ch.name;
            if (!label.isEmpty()) {
                m_effortCombo->addItem(label, ch.value);
            }
        }
        const QString currentVal = opt.currentValue.toString();
        if (!currentVal.isEmpty()) {
            const int idx = m_effortCombo->findData(currentVal);
            if (idx >= 0) m_effortCombo->setCurrentIndex(idx);
        }
        break;
    }
    m_effortCombo->setVisible(m_effortCombo->count() > 0);

    m_updatingSelectors = false;

    // Once the agent's catalogs have arrived, push any saved per-agent
    // selections back into the session (one-shot per session).
    applySavedPreferences();

    // After the initial restore, persist any agent-side config changes
    // (e.g. Codex CLI's config_option_update for model/mode/effort) so the
    // next session/new picks them up.
    if (m_savedPrefsApplied && m_registry && m_connection) {
        const QString agentId = m_connection->definition().id;
        if (!agentId.isEmpty()) {
            const QString curModel = m_model->currentModelId();
            if (!curModel.isEmpty()) {
                const QString modelKey = m_modelConfigOptionId.isEmpty()
                    ? QStringLiteral("model") : m_modelConfigOptionId;
                m_registry->setAgentPreference(agentId, modelKey, curModel);
            }

            const QString curMode = m_model->currentModeId();
            if (!curMode.isEmpty())
                m_registry->setAgentPreference(agentId, QStringLiteral("mode"), curMode);

            if (!m_effortConfigOptionId.isEmpty()) {
                for (const auto &opt : m_model->configOptions()) {
                    if (opt.id == m_effortConfigOptionId) {
                        const QString val = opt.currentValue.toString();
                        if (!val.isEmpty())
                            m_registry->setAgentPreference(agentId, m_effortConfigOptionId, val);
                        break;
                    }
                }
            }
        }
    }
}

void AcpSessionView::onCurrentModeChanged(const QString &modeId)
{
    if (!m_modeCombo) return;
    const int idx = m_modeCombo->findData(modeId);
    if (idx >= 0 && idx != m_modeCombo->currentIndex()) {
        m_updatingSelectors = true;
        m_modeCombo->setCurrentIndex(idx);
        m_updatingSelectors = false;
    }
}

void AcpSessionView::onIsProcessingChanged(bool processing)
{
    if (m_sendBtn) {
        const bool hasContent = !m_input->toPlainText().trimmed().isEmpty() || m_attachmentList->isNonEmpty();
        m_sendBtn->setEnabled(!processing && hasContent);
        m_sendBtn->setVisible(!processing);
    }
    if (m_cancelBtn) {
        m_cancelBtn->setEnabled(processing);
        m_cancelBtn->setVisible(processing);
    }
    // Heartbeat indicator: visible only while the agent is processing.
    if (processing) {
        resetElapsed();
        if (m_elapsedLabel) m_elapsedLabel->show();
        if (m_elapsedTimer) m_elapsedTimer->start();
        // Hide goal heartbeat while agent heartbeat is active.
        if (m_goalElapsedLabel) m_goalElapsedLabel->hide();
    } else {
        if (m_elapsedTimer) m_elapsedTimer->stop();
        if (m_elapsedLabel) m_elapsedLabel->hide();
        // Restore goal heartbeat if goal timer is still running, and reset
        // the counter so it shows time since the last turn ended.
        if (m_goalElapsedTimer && m_goalElapsedTimer->isActive()) {
            m_goalElapsedMs = 0;
            if (m_goalElapsedLabel) {
                m_goalElapsedLabel->setText(tr("Goal running · 0.0s"));
                m_goalElapsedLabel->show();
            }
        }
    }
    if (m_planWidget) m_planWidget->setAgentIdle(!processing);

    // Recompute the input busy-placeholder against the new ACP state (it also
    // depends on m_goalRunning, so the union is resolved inside).
    refreshBusyPlaceholder();
}

void AcpSessionView::onTurnEnded(int groupId)
{
    Q_UNUSED(groupId);
    if (m_currentGroupCards.size() > 3) {
        for (auto *card : m_currentGroupCards) {
            if (card && !card->shouldPreserveExpanded()) card->setCollapsed(true);
        }
    }
    m_currentGroupCards.clear();
}

void AcpSessionView::onPermissionRequested(const AcpProtocol::AcpPermissionRequest &req)
{
    resetElapsed();
    if (m_registry && m_registry->autoApprovePolicy() == QLatin1String("allowAll")) {
        // Connection already auto-approved before emitting (defense in depth).
        return;
    }
    auto *prompt = new AcpPermissionPrompt(req, m_transcriptHost);
    insertTimelineWidget(prompt);
    m_activePermissionPrompt = prompt;
    connect(prompt, &AcpPermissionPrompt::choiceMade,
            this, [this, prompt](const QString &reqId, const QString &outcome, const QString &optionId) {
        if (m_connection) {
            m_connection->respondToPermission(reqId, outcome, optionId);
        }
        prompt->deleteLater();
        if (m_activePermissionPrompt == prompt) {
            m_activePermissionPrompt.clear();
        }
    });
    scrollToBottomDeferred();
}

void AcpSessionView::onRequestFailed(const QString &message)
{
    const QString trimmed = message.trimmed();
    setBanner(trimmed.isEmpty()
                  ? tr("Agent request failed — open Debug for details")
                  : tr("Agent request failed — %1").arg(trimmed),
              BannerKind::Error);
}

void AcpSessionView::onErrorOccurred(AcpErrorClassifier::AcpErrorKind kind, const QString &friendly)
{
    BannerKind bk = BannerKind::Warning;
    if (kind == AcpErrorClassifier::AcpErrorKind::SpawnFailed
        || kind == AcpErrorClassifier::AcpErrorKind::InitFailed) {
        bk = BannerKind::Error;
    }
    setBanner(friendly, bk);
}

void AcpSessionView::insertTextToInput(const QString &text)
{
    if (!m_input) return;
    QTextCursor cursor = m_input->textCursor();
    const int pos = cursor.position();
    const QString doc = m_input->toPlainText();

    if (pos > 0) {
        const QChar before = doc.at(pos - 1);
        if (before != QLatin1Char(' ') && before != QLatin1Char('\n')) {
            cursor.insertText(QStringLiteral(" "));
        }
    }

    QString toInsert = text;
    const bool atEnd = pos >= doc.size();
    const QChar after = atEnd ? QLatin1Char('\n') : doc.at(pos);
    if (toInsert.endsWith(QLatin1Char(' ')) && (after == QLatin1Char(' ') || after == QLatin1Char('\n'))) {
        toInsert.chop(1);
    }

    m_input->setTextCursor(cursor);
    m_input->insertPlainText(toInsert);
    m_input->setFocus();
}

QString AcpSessionView::takeInputText()
{
    if (!m_input) return {};
    const QString text = m_input->toPlainText().trimmed();
    m_input->clear();
    return text;
}

QVector<QPair<QByteArray, QString>> AcpSessionView::takeInputImages()
{
    if (!m_attachmentList) return {};
    return m_attachmentList->takeAll();
}

QStringList AcpSessionView::goalDebugLog() const
{
    // parentWidget() returns QDockWidget's internal container, not the dock
    // itself. Walk up the ancestor chain to find the owning AiAgentDock.
    QWidget *w = parentWidget();
    while (w) {
        if (auto *dock = qobject_cast<AiAgentDock *>(w))
            return dock->goalDebugLog();
        w = w->parentWidget();
    }
    return {};
}

void AcpSessionView::setGoalActive(int criterionIndex, int totalCriteria, int iteration, int maxIterations)
{
    if (!m_goalStatusRow) return;
    m_goalRunning = true;
    refreshBusyPlaceholder();
    QString text;
    if (totalCriteria > 1) {
        text = tr("Goal %1/%2 · iter %3/%4")
                   .arg(criterionIndex).arg(totalCriteria)
                   .arg(iteration).arg(maxIterations);
    } else {
        text = tr("Goal · iter %1/%2").arg(iteration).arg(maxIterations);
    }
    m_goalStatusLabel->setText(text);
    m_goalStopBtn->show();
    m_goalStatusRow->show();
    ++m_goalTerminalGeneration; // invalidate any pending auto-hide from a prior goal

    // Start the goal transcript heartbeat only on first activation.
    // Subsequent calls (iteration/criterion updates) keep the counter running.
    bool alreadyRunning = m_goalElapsedTimer && m_goalElapsedTimer->isActive();
    if (!alreadyRunning) {
        m_goalElapsedMs = 0;
        if (m_goalElapsedLabel) {
            m_goalElapsedLabel->setText(tr("Goal running · 0.0s"));
            bool agentProcessing = m_model && m_model->isProcessing();
            if (!agentProcessing) {
                m_goalElapsedLabel->show();
            }
        }
        if (m_goalElapsedTimer) {
            m_goalElapsedTimer->start();
        }
    }
}

void AcpSessionView::setGoalTerminal(const QString &statusText)
{
    if (!m_goalStatusRow) return;
    m_goalRunning = false;
    refreshBusyPlaceholder();
    m_goalStatusLabel->setText(statusText);
    m_goalStopBtn->hide();
    m_goalStatusRow->show();

    // Stop the goal transcript heartbeat.
    if (m_goalElapsedTimer) m_goalElapsedTimer->stop();
    if (m_goalElapsedLabel) m_goalElapsedLabel->hide();

    const int gen = ++m_goalTerminalGeneration;
    QTimer::singleShot(5000, this, [this, gen]() {
        if (gen != m_goalTerminalGeneration) return;
        clearGoalStatus();
    });
}

void AcpSessionView::clearGoalStatus()
{
    if (!m_goalStatusRow) return;
    m_goalRunning = false;
    refreshBusyPlaceholder();
    m_goalStatusRow->hide();
    if (m_goalElapsedTimer) m_goalElapsedTimer->stop();
    if (m_goalElapsedLabel) m_goalElapsedLabel->hide();
}

void AcpSessionView::onSendClicked()
{
    if (!m_connection || !m_model) return;
    if (m_model->isProcessing()) return;

    const QString text = m_input->toPlainText().trimmed();
    QVector<QPair<QByteArray, QString>> images = m_attachmentList->takeAll();
    if (text.isEmpty() && images.isEmpty()) return;

    m_model->appendUserMessage(text, images);

    QList<QPair<QByteArray, QString>> imageList;
    imageList.reserve(images.size());
    for (const auto &p : images) imageList.append(p);
    m_connection->sendPrompt(text, imageList);

    m_input->clear();
    m_currentGroupCards.clear();
    emit inputFocused();
    // Sending is a fresh focus on the conversation — re-engage the stream
    // even if the user had scrolled up earlier.
    m_stickToBottom = true;
    scrollToBottomDeferred();
}

void AcpSessionView::onCancelClicked()
{
    if (m_connection) m_connection->cancelPrompt();
    // Let the owning dock stop any GoalAgent running on this session. Cancel
    // means "stop everything", not just the current ACP turn — otherwise the
    // goal supervisor would simply re-prompt the target after the cancel.
    emit cancelRequested();
}

void AcpSessionView::onAttachClicked()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Attach images"),
        QString(),
        tr("Images (*.png *.jpg *.jpeg *.gif *.webp)"));
    for (const QString &path : files) {
        m_attachmentList->addFileByPath(path);
    }
}

void AcpSessionView::onAutoApproveToggled(bool checked)
{
    if (!m_registry) return;
    m_registry->setAutoApprovePolicy(checked
                                         ? QStringLiteral("allowAll")
                                         : QStringLiteral("manual"));
}

void AcpSessionView::onAutoApprovePolicyChanged(const QString &policy)
{
    if (!m_autoApproveCheck) return;
    const bool wantChecked = (policy == QLatin1String("allowAll"));
    if (m_autoApproveCheck->isChecked() != wantChecked) {
        QSignalBlocker blocker(m_autoApproveCheck);
        m_autoApproveCheck->setChecked(wantChecked);
    }
}

void AcpSessionView::onModelComboChanged(int index)
{
    if (m_updatingSelectors || !m_connection || index < 0) return;
    const QString id = m_modelCombo->itemData(index).toString();
    if (id.isEmpty()) return;
    // Config-option-backed picker (Claude Code) → session/set_config; the
    // dedicated session/set_model channel only applies to the models array.
    const QString prefKey = m_modelConfigOptionId.isEmpty()
        ? QStringLiteral("model") : m_modelConfigOptionId;
    if (!m_modelConfigOptionId.isEmpty()) {
        m_connection->setConfigOption(m_modelConfigOptionId, id);
    } else {
        m_connection->setModel(id);
    }
    if (m_registry) {
        m_registry->setAgentPreference(m_connection->definition().id, prefKey, id);
    }
}

void AcpSessionView::onModeComboChanged(int index)
{
    if (m_updatingSelectors || !m_connection || index < 0) return;
    const QString id = m_modeCombo->itemData(index).toString();
    if (id.isEmpty()) return;
    m_connection->setMode(id);
    if (m_registry) {
        m_registry->setAgentPreference(m_connection->definition().id,
                                       QStringLiteral("mode"), id);
    }
}

void AcpSessionView::onEffortComboChanged(int index)
{
    if (m_updatingSelectors || !m_connection || index < 0) return;
    if (m_effortConfigOptionId.isEmpty()) return;
    const QString val = m_effortCombo->itemData(index).toString();
    m_connection->setConfigOption(m_effortConfigOptionId, val);
    if (m_registry && !val.isEmpty()) {
        m_registry->setAgentPreference(m_connection->definition().id,
                                       m_effortConfigOptionId, val);
    }
}

void AcpSessionView::applySavedPreferences()
{
    if (m_savedPrefsApplied) return;
    if (!m_registry || !m_connection || !m_model) return;
    // Wait until the agent's catalogs have been populated, otherwise we can't
    // tell whether a saved id is still valid.
    const bool catalogsReady = !m_model->availableModels().isEmpty()
        || !m_model->availableModes().isEmpty()
        || !m_model->configOptions().isEmpty();
    if (!catalogsReady) return;

    m_savedPrefsApplied = true;
    const QString agentId = m_connection->definition().id;
    if (agentId.isEmpty()) return;

    // Model: send only if the saved id exists in the catalog and differs.
    // Config-option-backed pickers (Claude Code) restore via session/set_config.
    const QString modelPrefKey = m_modelConfigOptionId.isEmpty()
        ? QStringLiteral("model") : m_modelConfigOptionId;
    const QString savedModel = m_registry->agentPreference(agentId, modelPrefKey);
    if (!savedModel.isEmpty() && savedModel != m_model->currentModelId()) {
        if (!m_modelConfigOptionId.isEmpty()) {
            for (const auto &opt : m_model->configOptions()) {
                if (opt.id != m_modelConfigOptionId) continue;
                for (const auto &ch : opt.options) {
                    if (ch.value == savedModel) {
                        m_connection->setConfigOption(m_modelConfigOptionId, savedModel);
                        break;
                    }
                }
                break;
            }
        } else {
            for (const auto &m : m_model->availableModels()) {
                if (m.id == savedModel) {
                    m_connection->setModel(savedModel);
                    break;
                }
            }
        }
    }

    // Mode.
    const QString savedMode = m_registry->agentPreference(agentId, QStringLiteral("mode"));
    if (!savedMode.isEmpty() && savedMode != m_model->currentModeId()) {
        for (const auto &m : m_model->availableModes()) {
            if (m.id == savedMode) {
                m_connection->setMode(savedMode);
                break;
            }
        }
    }

    // Effort / reasoning level: keyed by the matched config-option id.
    if (!m_effortConfigOptionId.isEmpty()) {
        const QString savedEffort = m_registry->agentPreference(agentId, m_effortConfigOptionId);
        if (!savedEffort.isEmpty()) {
            for (const auto &opt : m_model->configOptions()) {
                if (opt.id != m_effortConfigOptionId) continue;
                if (opt.currentValue.toString() == savedEffort) break;
                for (const auto &ch : opt.options) {
                    if (ch.value == savedEffort) {
                        m_connection->setConfigOption(m_effortConfigOptionId, savedEffort);
                        break;
                    }
                }
                break;
            }
        }
    }
}

void AcpSessionView::onJumpToBottomClicked()
{
    if (!m_scroll) return;
    // Explicit user intent to re-engage the stream → re-stick.
    m_stickToBottom = true;
    auto *vbar = m_scroll->verticalScrollBar();
    if (vbar) {
        m_programmaticScroll = true;
        vbar->setValue(vbar->maximum());
        m_programmaticScroll = false;
    }
    updateJumpButtonVisibility();
}

void AcpSessionView::resetElapsed()
{
    m_elapsedMs = 0;
    if (m_elapsedLabel) {
        m_elapsedLabel->setText(QStringLiteral("0.0s"));
    }
}

void AcpSessionView::onElapsedTick()
{
    m_elapsedMs += 100;
    if (m_elapsedLabel) {
        const int whole = m_elapsedMs / 1000;
        const int tenths = (m_elapsedMs % 1000) / 100;
        m_elapsedLabel->setText(QStringLiteral("%1.%2s").arg(whole).arg(tenths));
    }
}

void AcpSessionView::refreshBusyPlaceholder()
{
    if (!m_input) return;

    // Union of every running-agent surface. Either an in-flight ACP turn or an
    // active goal keeps the input "busy"; the placeholder reflects that union,
    // not whichever finished last.
    const bool acpBusy = m_model && m_model->isProcessing();
    const bool busy = acpBusy || m_goalRunning;

    if (busy == m_busyPlaceholderActive) {
        // No edge — the other surface was already keeping us busy. Leave the
        // monotonic clock untouched so the elapsed span keeps accumulating.
        return;
    }
    m_busyPlaceholderActive = busy;

    if (busy) {
        m_busyClock.start();                 // idle→busy edge: anchor the clock once
        updateBusyPlaceholderText();         // paint 0m 0s immediately, don't wait 1 s
        m_busyPlaceholderTimer->start();
    } else {
        m_busyPlaceholderTimer->stop();
        // Busy→idle edge. setInputPlaceholder() handles the viewport
        // invalidation so the input reverts to the idle text immediately
        // instead of staying frozen on the last "Agent is working… (Xm Ys)".
        setInputPlaceholder(tr("Send a message"));
    }
}

void AcpSessionView::updateBusyPlaceholderText()
{
    if (!m_input || !m_busyPlaceholderActive) return;
    const qint64 elapsedSec = m_busyClock.elapsed() / 1000;
    const qint64 minutes = elapsedSec / 60;
    const qint64 seconds = elapsedSec % 60;
    setInputPlaceholder(tr("Agent is working… (%1m %2s)").arg(minutes).arg(seconds));
}

void AcpSessionView::setInputPlaceholder(const QString &text)
{
    if (!m_input) return;
    m_input->setPlaceholderText(text);
    // QPlainTextEdit::setPlaceholderText() does not reliably invalidate the
    // viewport on a pure text change (its internal repaint is conditional and
    // varies across Qt 6.5↔6.10), so without this poke a placeholder transition
    // stays frozen on whatever was last painted by an unrelated relayout. The
    // placeholder is drawn in the viewport's paintEvent — invalidate it
    // directly, but only when it's actually on screen (empty document); when
    // the user has typed text the placeholder is hidden and Qt repaints the
    // content edit itself. Routed through one helper so no transition can skip
    // the poke. At most one repaint per second while busy — negligible cost.
    if (m_input->document()->isEmpty()) {
        m_input->viewport()->update();
    }
}

void AcpSessionView::onShowDebugLogClicked()
{
    // Find the owning AiAgentDock so we can scope the dialog title and goal
    // log signals to this specific session/tab.
    AiAgentDock *ownerDock = nullptr;
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        if (auto *d = qobject_cast<AiAgentDock *>(w)) { ownerDock = d; break; }
    }

    auto refresh = [this]() {
        if (!m_debugDialogText) return;
        if (m_debugDialogOnlyGoal && m_debugDialogOnlyGoal->isChecked()) {
            m_debugDialogText->setPlainText(goalDebugLog().join(QLatin1Char('\n')));
        } else if (m_connection) {
            QStringList combined = m_connection->debugLog();
            combined.append(goalDebugLog());
            m_debugDialogText->setPlainText(combined.join(QLatin1Char('\n')));
        } else {
            m_debugDialogText->setPlainText(goalDebugLog().join(QLatin1Char('\n')));
        }
        m_debugDialogText->verticalScrollBar()->setValue(
            m_debugDialogText->verticalScrollBar()->maximum());
    };

    if (m_debugDialog) {
        refresh();
        m_debugDialog->show();
        m_debugDialog->raise();
        m_debugDialog->activateWindow();
        return;
    }

    auto *dlg = new QDialog(window());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Per-session title so multiple debug windows (one per AI tab) are
    // distinguishable in the OS task list and on screen. Falls back to a
    // generic title when the owning dock can't be located.
    if (ownerDock && !ownerDock->windowTitle().isEmpty()) {
        dlg->setWindowTitle(tr("ACP Debug Log — %1").arg(ownerDock->windowTitle()));
    } else {
        dlg->setWindowTitle(tr("ACP Debug Log"));
    }
    dlg->resize(800, 500);

    auto *layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *text = new QPlainTextEdit(dlg);
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = text->font();
    mono.setFamily(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    text->setFont(mono);
    layout->addWidget(text, 1);

    auto *btnRow = new QHBoxLayout();
    auto *refreshBtn = new QPushButton(tr("Refresh"), dlg);
    auto *copyBtn = new QPushButton(tr("Copy all"), dlg);
    auto *clearBtn = new QPushButton(tr("Clear buffer"), dlg);
    auto *onlyGoalCheck = new QCheckBox(tr("Only Goal"), dlg);
    auto *closeBtn = new QPushButton(tr("Close"), dlg);
    btnRow->addWidget(refreshBtn);
    btnRow->addWidget(copyBtn);
    btnRow->addWidget(clearBtn);
    btnRow->addWidget(onlyGoalCheck);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    m_debugDialog = dlg;
    m_debugDialogText = text;
    m_debugDialogOnlyGoal = onlyGoalCheck;

    QPointer<AcpSessionView> self(this);
    connect(refreshBtn, &QPushButton::clicked, dlg, [self]() {
        if (!self || !self->m_debugDialogText) return;
        if (self->m_debugDialogOnlyGoal && self->m_debugDialogOnlyGoal->isChecked()) {
            self->m_debugDialogText->setPlainText(self->goalDebugLog().join(QLatin1Char('\n')));
        } else if (self->m_connection) {
            QStringList combined = self->m_connection->debugLog();
            combined.append(self->goalDebugLog());
            self->m_debugDialogText->setPlainText(combined.join(QLatin1Char('\n')));
        } else {
            self->m_debugDialogText->setPlainText(self->goalDebugLog().join(QLatin1Char('\n')));
        }
        self->m_debugDialogText->verticalScrollBar()->setValue(
            self->m_debugDialogText->verticalScrollBar()->maximum());
    });
    connect(onlyGoalCheck, &QCheckBox::toggled, dlg, [self, refreshBtn]() {
        if (refreshBtn) refreshBtn->click();
    });
    connect(copyBtn, &QPushButton::clicked, dlg, [text]() {
        QGuiApplication::clipboard()->setText(text->toPlainText());
    });
    connect(clearBtn, &QPushButton::clicked, dlg, [self, text]() {
        if (self && self->m_connection) {
            self->m_connection->clearDebugLog();
        }
        text->clear();
    });
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
    connect(dlg, &QDialog::destroyed, this, [self]() {
        if (self) {
            self->m_debugDialogText = nullptr;
            self->m_debugDialogOnlyGoal = nullptr;
        }
    });

    // Live updates — append each new line as it arrives so the dialog
    // streams events instead of needing a manual Refresh. Each AI tab owns
    // its own AcpConnection, so this scoping naturally keeps tab A's events
    // out of tab B's debug window.
    auto appendLive = [self](const QString &line) {
        if (!self || !self->m_debugDialogText) return;
        const bool onlyGoal = self->m_debugDialogOnlyGoal
                              && self->m_debugDialogOnlyGoal->isChecked();
        if (onlyGoal) return; // goal-only filter; ignore connection-level entries
        QScrollBar *bar = self->m_debugDialogText->verticalScrollBar();
        const bool wasAtBottom = bar ? (bar->value() == bar->maximum()) : true;
        self->m_debugDialogText->appendPlainText(line);
        if (wasAtBottom && bar) bar->setValue(bar->maximum());
    };
    if (m_connection) {
        connect(m_connection, &AcpConnection::debugLogAppended, dlg, appendLive);
    }
    if (ownerDock) {
        connect(ownerDock, &AiAgentDock::goalDebugLogAppended, dlg,
                [self](const QString &line) {
            if (!self || !self->m_debugDialogText) return;
            QScrollBar *bar = self->m_debugDialogText->verticalScrollBar();
            const bool wasAtBottom = bar ? (bar->value() == bar->maximum()) : true;
            self->m_debugDialogText->appendPlainText(line);
            if (wasAtBottom && bar) bar->setValue(bar->maximum());
        });
        // Close the debug dialog when the owning dock is destroyed (user
        // closes the AI tab). The dialog is parented to the main window,
        // not the dock, so it would otherwise survive as an orphan.
        connect(ownerDock, &QObject::destroyed, dlg, &QDialog::close);
    }

    refresh();
    dlg->show();
}

void AcpSessionView::scrollToBottomDeferred()
{
    if (!m_scroll) return;
    auto *vbar = m_scroll->verticalScrollBar();
    if (!vbar) return;
    if (!m_stickToBottom) {
        updateJumpButtonVisibility();
        return;
    }
    QPointer<AcpSessionView> guard(this);
    QTimer::singleShot(0, this, [guard]() {
        if (!guard) return;
        auto *vb = guard->m_scroll ? guard->m_scroll->verticalScrollBar() : nullptr;
        if (!vb) return;
        guard->m_programmaticScroll = true;
        vb->setValue(vb->maximum());
        guard->m_programmaticScroll = false;
    });
    updateJumpButtonVisibility();
}

void AcpSessionView::updateJumpButtonVisibility()
{
    if (!m_scroll || !m_jumpToBottom) return;
    // The button mirrors the inverse of the stick flag: visible exactly when
    // the user has scrolled away from the bottom, hidden when they're parked
    // there (or auto-scrolling alongside the stream).
    if (!m_stickToBottom) {
        positionJumpButton();
        m_jumpToBottom->show();
        m_jumpToBottom->raise();
    } else {
        m_jumpToBottom->hide();
    }
}

void AcpSessionView::positionJumpButton()
{
    if (!m_jumpToBottom || !m_scroll) return;
    QWidget *vp = m_scroll->viewport();
    if (!vp) return;
    const QSize vpSize = vp->size();
    const QSize btnSize = m_jumpToBottom->sizeHint();
    constexpr int kMargin = 8;
    const int x = qMax(0, vpSize.width() - btnSize.width() - kMargin);
    const int y = qMax(0, vpSize.height() - btnSize.height() - kMargin);
    m_jumpToBottom->move(x, y);
    m_jumpToBottom->resize(btnSize);
}

bool AcpSessionView::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize
        && (watched == m_transcriptHost
            || (m_scroll && watched == m_scroll->viewport()))) {
        syncTranscriptHostWidth();
        positionJumpButton();
    }
    if (event->type() == QEvent::Resize
        && (watched == m_input || (m_input && watched == m_input->viewport()))) {
        positionImproveButton();
    }
    if (event->type() == QEvent::FocusIn && watched == m_input) {
        emit inputFocused();
    }
    return QWidget::eventFilter(watched, event);
}

void AcpSessionView::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
    case QEvent::ApplicationPaletteChange:
        rebuildAttachIcon();
        break;
    default:
        break;
    }
}

void AcpSessionView::rebuildAttachIcon()
{
    if (!m_attachBtn) return;
    m_attachBtn->setIcon(tintedIcon(QStringLiteral(":/icons/paperclip.svg"),
                                    palette().color(QPalette::WindowText)));
}

QFont AcpSessionView::chatFont() const
{
    auto *settings = appSettings();
    if (!settings) return QFont();

    // Two modes (see ApplicationSettings ChatFont group). The hinting policy is
    // set on the QFont directly in BOTH branches: these are plain Qt widgets, so
    // Scintilla's platform-layer flag doesn't reach them — without it, thin fonts
    // (e.g. Lilex) look blurry here while the Scintilla editor looks sharp. See
    // EditorManager / PlatQt for the editor side.
    if (settings->chatFontUseDefault()) {
        // Default mode: follow the editor's Default Font (historical behavior).
        QFont f(settings->fontName(), settings->fontSize());
        f.setHintingPreference(!settings->fontHinting()
            ? QFont::PreferNoHinting
            : QFont::PreferFullHinting);
        return f;
    }

    // Custom mode: a chat-specific family/size/sharpen, independent of the editor.
    QFont f(settings->chatFontFamily(), settings->chatFontSizePt());
    f.setHintingPreference(!settings->chatFontSharpen()
        ? QFont::PreferNoHinting
        : QFont::PreferFullHinting);
    return f;
}

void AcpSessionView::applyChatFont()
{
    if (!appSettings()) return;

    const QFont f = chatFont();
    // The transcript host carries no stylesheet, so its setFont() is a cheap,
    // harmless default for any unstyled descendants. But the message bubbles
    // (AcpMessageWidget) AND their inner QTextBrowsers are stylesheet'd, and Qt
    // does NOT propagate an inherited font into a styled widget — so we must
    // push the font into each bubble explicitly via setChatFont().
    if (m_transcriptHost) m_transcriptHost->setFont(f);
    if (m_input) m_input->setFont(f);

    for (AcpMessageWidget *w : m_messageWidgets) {
        if (w) w->setChatFont(f);
    }
    if (m_activeThought) m_activeThought->setChatFont(f);
    for (AcpToolCallCard *c : m_toolCallCards) {
        if (c) c->setChatFont(f);
    }
}

bool AcpSessionView::inputKeyEventIsSubmit(QKeyEvent *ke) const
{
    return (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
           && !(ke->modifiers() & Qt::ShiftModifier);
}

void AcpSessionView::showCommandPopup()
{
    if (!m_commandPopup || !m_model) return;
    const auto &cmds = m_model->availableCommands();
    if (cmds.isEmpty()) { hideCommandPopup(); return; }

    m_commandPopup->clear();
    for (const auto &cmd : cmds) {
        QString label = QStringLiteral("/") + cmd.name;
        if (!cmd.description.isEmpty())
            label += QStringLiteral("  ") + cmd.description;
        auto *item = new QListWidgetItem(label, m_commandPopup);
        item->setData(Qt::UserRole, cmd.name);
    }
    m_commandPopup->setCurrentRow(0);
    m_commandPopup->show();
    resizeCommandPopup();
}

void AcpSessionView::hideCommandPopup()
{
    if (m_commandPopup) m_commandPopup->hide();
}

void AcpSessionView::resizeCommandPopup()
{
    if (!m_commandPopup || !m_commandPopup->isVisible() || !m_input) return;
    int visibleCount = 0;
    for (int i = 0; i < m_commandPopup->count(); ++i) {
        if (!m_commandPopup->item(i)->isHidden()) ++visibleCount;
    }
    if (visibleCount == 0) { hideCommandPopup(); return; }

    const int rowH = m_commandPopup->sizeHintForRow(0);
    const int rows = qMin(visibleCount, 8);
    const int popupH = rowH * rows + 4;
    const QPoint inputTopLeft = m_input->mapToGlobal(QPoint(0, 0));
    m_commandPopup->setGeometry(inputTopLeft.x(), inputTopLeft.y() - popupH,
                                m_input->width(), popupH);
}

void AcpSessionView::filterCommandPopup()
{
    if (!m_input || !m_model) { hideCommandPopup(); return; }
    const QString text = m_input->toPlainText();

    if (!text.startsWith(QLatin1Char('/')) || text.contains(QLatin1Char('\n'))) {
        hideCommandPopup();
        return;
    }

    const auto &cmds = m_model->availableCommands();
    if (cmds.isEmpty()) { hideCommandPopup(); return; }

    const QString prefix = text.mid(1).toLower();

    if (!m_commandPopup->isVisible()) {
        showCommandPopup();
    }

    int firstVisible = -1;
    for (int i = 0; i < m_commandPopup->count(); ++i) {
        auto *item = m_commandPopup->item(i);
        const QString name = item->data(Qt::UserRole).toString();
        const bool match = prefix.isEmpty() || name.toLower().startsWith(prefix);
        item->setHidden(!match);
        if (match && firstVisible < 0) firstVisible = i;
    }

    if (firstVisible < 0) {
        hideCommandPopup();
    } else {
        if (m_commandPopup->currentItem() && m_commandPopup->currentItem()->isHidden())
            m_commandPopup->setCurrentRow(firstVisible);
        resizeCommandPopup();
    }
}

void AcpSessionView::acceptCommandCompletion()
{
    if (!m_commandPopup || !m_commandPopup->isVisible()) return;
    auto *item = m_commandPopup->currentItem();
    if (!item || item->isHidden()) { hideCommandPopup(); return; }

    const QString name = item->data(Qt::UserRole).toString();
    m_input->setPlainText(QStringLiteral("/") + name + QStringLiteral(" "));
    QTextCursor cur = m_input->textCursor();
    cur.movePosition(QTextCursor::End);
    m_input->setTextCursor(cur);
    hideCommandPopup();
}

void AcpSessionView::positionImproveButton()
{
    if (!m_improveBtn || !m_input) return;
    constexpr int kPad = 4;
    const QSize btnSz = m_improveBtn->sizeHint();
    int rightOffset = kPad;
    if (auto *sb = m_input->verticalScrollBar(); sb && sb->isVisible())
        rightOffset += sb->width();
    const int x = m_input->width() - btnSz.width() - rightOffset;
    const int y = m_input->height() - btnSz.height() - kPad;
    m_improveBtn->move(x, y);
    m_improveBtn->raise();
}

void AcpSessionView::updateImproveButtonState()
{
    if (!m_improveBtn || !m_input) return;

    const QString text = m_input->toPlainText().trimmed();

    // Hide when empty.
    if (text.isEmpty()) {
        m_improveBtn->hide();
        return;
    }

    // Hide when bare slash command (no arguments).
    static const QRegularExpression bareCmd(QStringLiteral("^\\s*/\\w+\\s*$"));
    if (bareCmd.match(text).hasMatch()) {
        m_improveBtn->hide();
        return;
    }

    m_improveBtn->show();
    positionImproveButton();
    m_improveBtn->setEnabled(true);
}

void AcpSessionView::onImproveClicked()
{
    if (!m_promptImprover || !m_input) return;

    // If already streaming, cancel.
    if (m_promptImprover->state() == ai::PromptImprover::State::Streaming) {
        m_promptImprover->cancel();
        m_input->setReadOnly(false);
        m_improveBtn->setText(QStringLiteral("✨"));
        m_improveBtn->setToolTip(tr("Improve prompt with AI (Ctrl+I)"));
        updateImproveButtonState();
        return;
    }

    QString whyNot;
    if (!m_promptImprover->canImprove(&whyNot)) {
        setBanner(whyNot, BannerKind::Warning);
        QTimer::singleShot(5000, this, &AcpSessionView::clearBanner);
        return;
    }

    const QString draft = m_input->toPlainText().trimmed();
    if (draft.isEmpty()) return;

    const auto images = m_attachmentList->peekAll();

    // Find the owning dock to get the working directory.
    QString workingDir;
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        if (auto *dock = qobject_cast<AiAgentDock *>(w)) {
            workingDir = dock->workingDirectory();
            break;
        }
    }

    const QList<AcpProtocol::AcpCommandInfo> commands =
        m_model ? m_model->availableCommands() : QList<AcpProtocol::AcpCommandInfo>{};

    // Build a condensed chat history from recent messages (budget: ~4000 chars).
    QString chatHistory;
    if (m_model) {
        constexpr int kHistoryBudget = 4000;
        const auto &msgs = m_model->messages();
        int totalChars = 0;
        // Walk backwards to get the most recent messages first.
        for (int i = msgs.size() - 1; i >= 0; --i) {
            const auto &msg = msgs[i];
            if (msg.role != QLatin1String("user") && msg.role != QLatin1String("assistant"))
                continue;
            QString text;
            for (const auto &block : msg.content) {
                if (block.kind == AcpProtocol::AcpContentBlock::Kind::Text)
                    text += block.text;
            }
            text = text.trimmed();
            if (text.isEmpty()) continue;
            // Truncate individual messages that are too long.
            if (text.size() > 800)
                text = text.left(800) + QStringLiteral("...");
            const QString entry = QStringLiteral("[%1]: %2\n").arg(msg.role, text);
            if (totalChars + entry.size() > kHistoryBudget)
                break;
            chatHistory.prepend(entry);
            totalChars += entry.size();
        }
        chatHistory = chatHistory.trimmed();
    }

    m_originalDraftBeforeImprove = m_input->toPlainText();
    m_input->setReadOnly(true);
    m_improveBtn->setText(QStringLiteral("■"));
    m_improveBtn->setToolTip(tr("Stop improving (Esc)"));
    m_improveBtn->setEnabled(true);

    m_promptImprover->trigger(draft, workingDir, commands, chatHistory, images);
}

void AcpSessionView::onImproveFinished(const QString &improvedText)
{
    if (!m_input) return;

    // Replace via QTextCursor so Ctrl+Z restores the original.
    QTextCursor cursor(m_input->document());
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.insertText(improvedText);
    cursor.endEditBlock();

    m_input->setReadOnly(false);
    m_improveBtn->setText(QStringLiteral("✨"));
    m_improveBtn->setToolTip(tr("Improve prompt with AI (Ctrl+I)"));
    m_input->setFocus();
    updateImproveButtonState();
}

void AcpSessionView::onImproveError(const QString &message)
{
    if (!m_input) return;

    m_input->setReadOnly(false);
    m_improveBtn->setText(QStringLiteral("✨"));
    m_improveBtn->setToolTip(tr("Improve prompt with AI (Ctrl+I)"));
    updateImproveButtonState();

    setBanner(message, BannerKind::Warning);
    QTimer::singleShot(4000, this, &AcpSessionView::clearBanner);
}

