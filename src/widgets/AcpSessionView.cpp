/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
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

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
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
    buildUi();
    wireSignals();
    hydrateFromModel();
}

AcpSessionView::~AcpSessionView() = default;

void AcpSessionView::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    // 1. Status banner.
    m_banner = new QFrame(this);
    m_banner->setObjectName(QStringLiteral("AcpStatusBanner"));
    m_banner->setStyleSheet(QStringLiteral(
        "QFrame#AcpStatusBanner { background: transparent; border: none; border-radius: 4px; padding: 0px; }"
        "QFrame#AcpStatusBanner[bannerKind=\"warning\"] { background: #fff3cd; border: 1px solid #ffeeba; padding: 4px; }"
        "QFrame#AcpStatusBanner[bannerKind=\"error\"] { background: #f8d7da; border: 1px solid #f5c6cb; padding: 4px; }"));
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

    m_transcriptLayout->addStretch();

    m_scroll->setWidget(m_transcriptHost);
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

    // 6. Input.
    auto *cb = new ChatInputEditCb(m_attachmentList, this);
    cb->onSubmit = [this]() { onSendClicked(); };
    cb->setMaximumHeight(120);
    m_input = cb;
    outer->addWidget(m_input);

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

    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_sendBtn);
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
        connect(m_connection, &AcpConnection::errorOccurred,
                this, &AcpSessionView::onErrorOccurred);
        connect(m_connection, &AcpConnection::planReceived,
                this, [this](const QList<AcpProtocol::AcpPlanEntry> &entries) {
            resetElapsed();
            if (!m_planWidget) {
                m_planWidget = new AcpPlanWidget(m_transcriptHost);
                insertTimelineWidget(m_planWidget);
            }
            m_planWidget->setEntries(entries);
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
            w->setContent(msg.content);
            insertTimelineWidget(w);
            m_messageWidgets.insert(entry.messageIndex, w);
        } else if (entry.kind == AcpTimelineEntry::Kind::ToolCall) {
            auto it = toolCalls.find(entry.toolCallId);
            if (it != toolCalls.end()) {
                auto *card = new AcpToolCallCard(it.value(), m_transcriptHost);
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
    m_banner->style()->unpolish(m_banner);
    m_banner->style()->polish(m_banner);
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

    // If the debug-log popup is open, repoint its content at the new
    // connection's log so the user sees the freshly-restarted session.
    if (m_debugDialog && m_debugDialogText) {
        if (m_connection) {
            m_debugDialogText->setPlainText(m_connection->debugLog().join(QLatin1Char('\n')));
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
}

void AcpSessionView::appendMessageWidget(int idx)
{
    if (!m_model) return;
    if (idx < 0 || idx >= m_model->messages().size()) return;
    const AcpMessage &msg = m_model->messages().at(idx);

    auto *w = new AcpMessageWidget(msg.role, m_transcriptHost);
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
        card->apply(upd);
    }
}

void AcpSessionView::onPlanUpdated()
{
    // The plan is actually populated via the connection's planReceived signal
    // (which carries the entries). The model's planUpdated() carries no
    // entries; we keep the slot wired for symmetry with future model changes.
}

void AcpSessionView::onUsageChanged()
{
    if (m_model) m_usageIndicator->setUsage(m_model->usage());
}

void AcpSessionView::onMetadataChanged()
{
    if (!m_model) return;
    m_updatingSelectors = true;

    // Models combo.
    m_modelCombo->clear();
    const auto &models = m_model->availableModels();
    for (const auto &m : models) {
        m_modelCombo->addItem(m.name.isEmpty() ? m.id : m.name, m.id);
    }
    if (!m_model->currentModelId().isEmpty()) {
        const int idx = m_modelCombo->findData(m_model->currentModelId());
        if (idx >= 0) m_modelCombo->setCurrentIndex(idx);
    }
    m_modelCombo->setVisible(!models.isEmpty());

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
    } else {
        if (m_elapsedTimer) m_elapsedTimer->stop();
        if (m_elapsedLabel) m_elapsedLabel->hide();
    }
}

void AcpSessionView::onTurnEnded(int groupId)
{
    Q_UNUSED(groupId);
    if (m_currentGroupCards.size() > 3) {
        for (auto *card : m_currentGroupCards) {
            if (card) card->setCollapsed(true);
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

void AcpSessionView::onErrorOccurred(AcpErrorClassifier::AcpErrorKind kind, const QString &friendly)
{
    BannerKind bk = BannerKind::Warning;
    if (kind == AcpErrorClassifier::AcpErrorKind::SpawnFailed
        || kind == AcpErrorClassifier::AcpErrorKind::InitFailed) {
        bk = BannerKind::Error;
    }
    setBanner(friendly, bk);
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
    // Sending is a fresh focus on the conversation — re-engage the stream
    // even if the user had scrolled up earlier.
    m_stickToBottom = true;
    scrollToBottomDeferred();
}

void AcpSessionView::onCancelClicked()
{
    if (m_connection) m_connection->cancelPrompt();
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
    m_connection->setModel(id);
    if (m_registry) {
        m_registry->setAgentPreference(m_connection->definition().id,
                                       QStringLiteral("model"), id);
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
    const QString savedModel = m_registry->agentPreference(agentId, QStringLiteral("model"));
    if (!savedModel.isEmpty() && savedModel != m_model->currentModelId()) {
        for (const auto &m : m_model->availableModels()) {
            if (m.id == savedModel) {
                m_connection->setModel(savedModel);
                break;
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

void AcpSessionView::onShowDebugLogClicked()
{
    auto refresh = [this]() {
        if (!m_debugDialogText) return;
        if (m_connection) {
            m_debugDialogText->setPlainText(m_connection->debugLog().join(QLatin1Char('\n')));
        } else {
            m_debugDialogText->setPlainText(tr("(no active connection)"));
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
    dlg->setWindowTitle(tr("ACP Debug Log"));
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
    auto *closeBtn = new QPushButton(tr("Close"), dlg);
    btnRow->addWidget(refreshBtn);
    btnRow->addWidget(copyBtn);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    m_debugDialog = dlg;
    m_debugDialogText = text;

    QPointer<AcpSessionView> self(this);
    connect(refreshBtn, &QPushButton::clicked, dlg, [self]() {
        if (!self || !self->m_debugDialogText) return;
        if (self->m_connection) {
            self->m_debugDialogText->setPlainText(
                self->m_connection->debugLog().join(QLatin1Char('\n')));
        } else {
            self->m_debugDialogText->setPlainText(tr("(no active connection)"));
        }
        self->m_debugDialogText->verticalScrollBar()->setValue(
            self->m_debugDialogText->verticalScrollBar()->maximum());
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
        if (self) self->m_debugDialogText = nullptr;
    });

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
        positionJumpButton();
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

bool AcpSessionView::inputKeyEventIsSubmit(QKeyEvent *ke) const
{
    return (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
           && !(ke->modifiers() & Qt::ShiftModifier);
}
