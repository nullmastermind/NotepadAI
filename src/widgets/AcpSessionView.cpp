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

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
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

// Treat any scrollbar value within this many pixels of the maximum as "at bottom".
constexpr int kAtBottomEpsilonPx = 8;
constexpr int kJumpButtonRevealThresholdPx = 64;

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
                const QByteArray raw = source->data(QStringLiteral("image/png"));
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
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    // 1. Status banner.
    m_banner = new QFrame(this);
    m_banner->setObjectName(QStringLiteral("AcpStatusBanner"));
    m_banner->setStyleSheet(QStringLiteral(
        "QFrame#AcpStatusBanner { background: palette(window); border: 1px solid palette(mid); border-radius: 4px; padding: 4px; }"
        "QFrame#AcpStatusBanner[bannerKind=\"warning\"] { background: #fff3cd; border-color: #ffeeba; }"
        "QFrame#AcpStatusBanner[bannerKind=\"error\"] { background: #f8d7da; border-color: #f5c6cb; }"));
    auto *banL = new QHBoxLayout(m_banner);
    banL->setContentsMargins(8, 4, 8, 4);
    m_bannerLabel = new QLabel(m_banner);
    m_bannerLabel->setWordWrap(true);
    m_bannerRetry = new QPushButton(tr("Retry"), m_banner);
    m_bannerRetry->hide();
    connect(m_bannerRetry, &QPushButton::clicked, this, [this]() {
        clearBanner();
        emit retryRequested();
    });
    banL->addWidget(m_bannerLabel, 1);
    banL->addWidget(m_bannerRetry);
    m_banner->hide();
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
    m_transcriptLayout->addStretch();

    m_scroll->setWidget(m_transcriptHost);
    outer->addWidget(m_scroll, 1);

    // Jump-to-bottom overlay.
    m_jumpToBottom = new QToolButton(m_transcriptHost);
    m_jumpToBottom->setText(QStringLiteral("↓"));
    m_jumpToBottom->setToolTip(tr("Jump to bottom"));
    m_jumpToBottom->setAutoRaise(false);
    m_jumpToBottom->setStyleSheet(QStringLiteral(
        "QToolButton { background: palette(button); border: 1px solid palette(mid); border-radius: 12px; min-width: 24px; min-height: 24px; }"));
    m_jumpToBottom->hide();
    connect(m_jumpToBottom, &QToolButton::clicked, this, &AcpSessionView::onJumpToBottomClicked);

    if (auto *vbar = m_scroll->verticalScrollBar()) {
        connect(vbar, &QScrollBar::valueChanged, this, [this](int) {
            updateJumpButtonVisibility();
        });
        connect(vbar, &QScrollBar::rangeChanged, this, [this](int, int) {
            updateJumpButtonVisibility();
        });
    }

    // 3. Selectors row.
    auto *selectorsRow = new QHBoxLayout();
    selectorsRow->setContentsMargins(0, 0, 0, 0);
    selectorsRow->setSpacing(4);

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setToolTip(tr("Model"));
    m_modelCombo->hide();
    m_modeCombo = new QComboBox(this);
    m_modeCombo->setToolTip(tr("Mode"));
    m_modeCombo->hide();
    m_effortCombo = new QComboBox(this);
    m_effortCombo->setToolTip(tr("Effort"));
    m_effortCombo->hide();
    selectorsRow->addWidget(m_modelCombo);
    selectorsRow->addWidget(m_modeCombo);
    selectorsRow->addWidget(m_effortCombo);
    selectorsRow->addStretch();
    outer->addLayout(selectorsRow);

    // 4. Auto-approve checkbox.
    m_autoApproveCheck = new QCheckBox(tr("Auto-approve permissions"), this);
    m_autoApproveCheck->setToolTip(tr("Automatically allow all tool calls this agent requests"));
    m_autoApproveCheck->setStyleSheet(QStringLiteral(
        "QCheckBox:checked { color: #856404; background-color: #fff3cd; padding: 2px 4px; border-radius: 2px; }"));
    outer->addWidget(m_autoApproveCheck);

    // 5. Image attachments.
    m_attachmentList = new AcpImageAttachmentList(this);
    outer->addWidget(m_attachmentList);

    // 6. Input.
    auto *cb = new ChatInputEditCb(m_attachmentList, this);
    cb->onSubmit = [this]() { onSendClicked(); };
    cb->setMaximumHeight(120);
    m_input = cb;
    outer->addWidget(m_input);

    // 7. Button row + usage.
    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(4);
    m_attachBtn = new QPushButton(tr("Attach"), this);
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_sendBtn = new QPushButton(tr("Send"), this);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->hide();
    btnRow->addWidget(m_attachBtn);
    btnRow->addStretch();
    m_usageIndicator = new AcpUsageIndicator(this);
    btnRow->addWidget(m_usageIndicator);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_sendBtn);
    outer->addLayout(btnRow);

    connect(m_sendBtn,   &QPushButton::clicked, this, &AcpSessionView::onSendClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &AcpSessionView::onCancelClicked);
    connect(m_attachBtn, &QPushButton::clicked, this, &AcpSessionView::onAttachClicked);

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
            if (!m_planWidget) {
                m_planWidget = new AcpPlanWidget(m_transcriptHost);
                const int insertAt = m_transcriptLayout->count() - 1;
                m_transcriptLayout->insertWidget(insertAt, m_planWidget);
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
            const int insertAt = m_transcriptLayout->count() - 1; // before the stretch
            m_transcriptLayout->insertWidget(insertAt, w);
            m_messageWidgets.insert(entry.messageIndex, w);
        } else if (entry.kind == AcpTimelineEntry::Kind::ToolCall) {
            auto it = toolCalls.find(entry.toolCallId);
            if (it != toolCalls.end()) {
                auto *card = new AcpToolCallCard(it.value(), m_transcriptHost);
                const int insertAt = m_transcriptLayout->count() - 1;
                m_transcriptLayout->insertWidget(insertAt, card);
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
    if (m_banner) m_banner->hide();
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

    // Clear the transcript: drop everything except the trailing stretch.
    while (m_transcriptLayout->count() > 1) {
        QLayoutItem *item = m_transcriptLayout->takeAt(0);
        if (!item) break;
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
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

    // Restore Send/Cancel default visibility.
    onIsProcessingChanged(model ? model->isProcessing() : false);

    // Re-attach signals to the fresh model + connection and re-hydrate from
    // the (presumably empty) new model state.
    wireSignals();
    hydrateFromModel();
}

void AcpSessionView::appendMessageWidget(int idx)
{
    if (!m_model) return;
    if (idx < 0 || idx >= m_model->messages().size()) return;
    const AcpMessage &msg = m_model->messages().at(idx);

    auto *w = new AcpMessageWidget(msg.role, m_transcriptHost);
    w->setContent(msg.content);
    const int insertAt = m_transcriptLayout->count() - 1;
    m_transcriptLayout->insertWidget(insertAt, w);
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

void AcpSessionView::onThoughtAppended(int idx)
{
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
    auto it = m_model->toolCalls().find(toolCallId);
    if (it == m_model->toolCalls().end()) return;
    const AcpProtocol::AcpToolCall &tc = it.value();

    auto *card = m_toolCallCards.value(toolCallId, nullptr);
    if (!card) {
        card = new AcpToolCallCard(tc, m_transcriptHost);
        const int insertAt = m_transcriptLayout->count() - 1;
        m_transcriptLayout->insertWidget(insertAt, card);
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

    // Effort/reasoning config-option combo.
    m_effortCombo->clear();
    m_effortConfigOptionId.clear();
    const auto &configOpts = m_model->configOptions();
    for (const auto &opt : configOpts) {
        const QString lower = opt.id.toLower();
        if (lower.contains(QLatin1String("effort"))
            || lower.contains(QLatin1String("reasoning"))) {
            m_effortConfigOptionId = opt.id;
            for (const auto &choiceVal : opt.choices) {
                const QString label = choiceVal.toString();
                if (!label.isEmpty()) {
                    m_effortCombo->addItem(label, label);
                }
            }
            const QString currentVal = opt.value.toString();
            if (!currentVal.isEmpty()) {
                const int idx = m_effortCombo->findData(currentVal);
                if (idx >= 0) m_effortCombo->setCurrentIndex(idx);
            }
            break;
        }
    }
    m_effortCombo->setVisible(m_effortCombo->count() > 0);

    m_updatingSelectors = false;
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
    if (m_registry && m_registry->autoApprovePolicy() == QLatin1String("allowAll")) {
        // Connection already auto-approved before emitting (defense in depth).
        return;
    }
    auto *prompt = new AcpPermissionPrompt(req, m_transcriptHost);
    const int insertAt = m_transcriptLayout->count() - 1;
    m_transcriptLayout->insertWidget(insertAt, prompt);
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
    if (!id.isEmpty()) m_connection->setModel(id);
}

void AcpSessionView::onModeComboChanged(int index)
{
    if (m_updatingSelectors || !m_connection || index < 0) return;
    const QString id = m_modeCombo->itemData(index).toString();
    if (!id.isEmpty()) m_connection->setMode(id);
}

void AcpSessionView::onEffortComboChanged(int index)
{
    if (m_updatingSelectors || !m_connection || index < 0) return;
    if (m_effortConfigOptionId.isEmpty()) return;
    const QString val = m_effortCombo->itemData(index).toString();
    m_connection->setConfigOption(m_effortConfigOptionId, val);
}

void AcpSessionView::onJumpToBottomClicked()
{
    if (!m_scroll) return;
    auto *vbar = m_scroll->verticalScrollBar();
    if (vbar) vbar->setValue(vbar->maximum());
    if (m_jumpToBottom) m_jumpToBottom->hide();
}

void AcpSessionView::scrollToBottomDeferred()
{
    if (!m_scroll) return;
    auto *vbar = m_scroll->verticalScrollBar();
    if (!vbar) return;
    const bool atBottom = vbar->value() >= vbar->maximum() - kAtBottomEpsilonPx;
    if (atBottom) {
        QPointer<AcpSessionView> guard(this);
        QTimer::singleShot(0, this, [guard]() {
            if (!guard) return;
            if (auto *vb = guard->m_scroll ? guard->m_scroll->verticalScrollBar() : nullptr) {
                vb->setValue(vb->maximum());
            }
        });
    }
    updateJumpButtonVisibility();
}

void AcpSessionView::updateJumpButtonVisibility()
{
    if (!m_scroll || !m_jumpToBottom) return;
    auto *vbar = m_scroll->verticalScrollBar();
    if (!vbar) return;
    const int distance = vbar->maximum() - vbar->value();
    const bool show = distance > kJumpButtonRevealThresholdPx;
    if (show) {
        positionJumpButton();
        m_jumpToBottom->show();
        m_jumpToBottom->raise();
    } else {
        m_jumpToBottom->hide();
    }
}

void AcpSessionView::positionJumpButton()
{
    if (!m_jumpToBottom || !m_transcriptHost) return;
    const QSize hostSize = m_transcriptHost->size();
    const QSize btnSize = m_jumpToBottom->sizeHint();
    const int x = qMax(0, hostSize.width() - btnSize.width() - 8);
    const int y = qMax(0, hostSize.height() - btnSize.height() - 8);
    m_jumpToBottom->move(x, y);
    m_jumpToBottom->resize(btnSize);
}

bool AcpSessionView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_transcriptHost && event->type() == QEvent::Resize) {
        positionJumpButton();
    }
    return QWidget::eventFilter(watched, event);
}

bool AcpSessionView::inputKeyEventIsSubmit(QKeyEvent *ke) const
{
    return (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
           && !(ke->modifiers() & Qt::ShiftModifier);
}
