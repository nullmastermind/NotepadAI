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

#ifndef ACP_SESSION_VIEW_H
#define ACP_SESSION_VIEW_H

#include <QHash>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

#include <cstdint>

#include "AcpErrorClassifier.h"
#include "AcpProtocol.h"

class AcpAgentRegistry;
class AcpConnection;
class AcpImageAttachmentList;
class AcpMessageWidget;
class AcpPlanWidget;
class AcpPermissionPrompt;
class AcpSessionModel;
class AcpToolCallCard;
class AcpUsageIndicator;

class QCheckBox;
class QComboBox;
class QDialog;
class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTimer;
class QToolButton;
class QVBoxLayout;

// The chat-view widget hosting an ACP session. Non-owning pointers; the
// manager owns model/connection/registry.
class AcpSessionView : public QWidget
{
    Q_OBJECT

public:
    enum class BannerKind : std::uint8_t { Info, Warning, Error };

    AcpSessionView(AcpSessionModel *model,
                   AcpConnection *connection,
                   AcpAgentRegistry *registry,
                   QWidget *parent = nullptr);
    ~AcpSessionView() override;

    void setBanner(const QString &text, BannerKind kind);
    void clearBanner();

    // Detach from the current model + connection and re-attach to a new pair
    // (typically after AcpAgentManager::restartSession). Clears the
    // transcript and the exit banner; re-runs hydrateFromModel() against the
    // new model. The view widget itself survives.
    void rebind(AcpSessionModel *model, AcpConnection *connection);

signals:
    void retryRequested();

private slots:
    void onShowDebugLogClicked();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onMessageAppended(int idx);
    void onMessageChunkAppended(int idx, const QString &chunk);
    void onThoughtAppended(int idx);
    void onThoughtChunkAppended(int idx, const QString &chunk);
    void onToolCallAddedOrUpdated(const QString &toolCallId);
    void onPlanUpdated();
    void onUsageChanged();
    void onMetadataChanged();
    void onCurrentModeChanged(const QString &modeId);
    void onIsProcessingChanged(bool processing);
    void onTurnEnded(int groupId);
    void onPermissionRequested(const AcpProtocol::AcpPermissionRequest &req);
    void onErrorOccurred(AcpErrorClassifier::AcpErrorKind kind, const QString &friendly);

    void onSendClicked();
    void onCancelClicked();
    void onAttachClicked();
    void onAutoApproveToggled(bool checked);
    void onModelComboChanged(int index);
    void onModeComboChanged(int index);
    void onEffortComboChanged(int index);
    void onAutoApprovePolicyChanged(const QString &policy);
    void onJumpToBottomClicked();

private:
    void buildUi();
    void wireSignals();
    void hydrateFromModel();
    void appendMessageWidget(int idx);
    // Insert a widget into the transcript timeline at the tail, just above
    // the inline heartbeat indicator (if present) and the trailing stretch.
    // All bubbles, tool-call cards, plan widgets, and permission prompts go
    // through here so the heartbeat always trails the freshest content.
    void insertTimelineWidget(QWidget *w);
    void scrollToBottomDeferred();
    void updateJumpButtonVisibility();
    void positionJumpButton();
    bool inputKeyEventIsSubmit(QKeyEvent *ke) const;
    // Heartbeat indicator at the tail of the transcript: shows time since the
    // last structural event (new message / thought / tool call / plan /
    // permission). Text/thought chunk streaming intentionally does NOT
    // reset — during steady streaming the counter grows so the user
    // sees "still working, last new event was X.Xs ago".
    void resetElapsed();
    void onElapsedTick();

    AcpSessionModel *m_model = nullptr;       // non-owning
    AcpConnection *m_connection = nullptr;    // non-owning
    AcpAgentRegistry *m_registry = nullptr;   // non-owning

    // Status banner
    QFrame *m_banner = nullptr;
    QLabel *m_bannerLabel = nullptr;
    QPushButton *m_bannerRetry = nullptr;
    QToolButton *m_bannerDebug = nullptr;

    // ACP debug log popup (non-modal, lazily created on first click).
    QPointer<QDialog> m_debugDialog;
    QPlainTextEdit *m_debugDialogText = nullptr;

    // Transcript
    QScrollArea *m_scroll = nullptr;
    QWidget *m_transcriptHost = nullptr;
    QVBoxLayout *m_transcriptLayout = nullptr;
    QToolButton *m_jumpToBottom = nullptr;

    // Selectors row
    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_effortCombo = nullptr;
    QString m_effortConfigOptionId;

    // Auto-approve
    QCheckBox *m_autoApproveCheck = nullptr;

    // Attachments
    AcpImageAttachmentList *m_attachmentList = nullptr;

    // Input + buttons
    QPlainTextEdit *m_input = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_attachBtn = nullptr;

    // Usage
    AcpUsageIndicator *m_usageIndicator = nullptr;

    // Heartbeat indicator shown only while the agent is processing.
    QLabel *m_elapsedLabel = nullptr;
    QTimer *m_elapsedTimer = nullptr;
    int m_elapsedMs = 0;

    // Tracking
    QHash<int, AcpMessageWidget *> m_messageWidgets;
    QHash<QString, AcpToolCallCard *> m_toolCallCards;
    QVector<AcpToolCallCard *> m_currentGroupCards;
    AcpPlanWidget *m_planWidget = nullptr;
    QPointer<AcpMessageWidget> m_activeThought;
    QPointer<AcpPermissionPrompt> m_activePermissionPrompt;

    bool m_updatingSelectors = false;

    // Auto-scroll state. The transcript is "stuck to the bottom" when the
    // user is parked at (or within a few px of) the maximum scroll value.
    // While stuck, content growth triggers auto-scroll; otherwise we leave
    // the user where they are and reveal the ↓ jump button. The flag flips
    // on when the user scrolls back to the bottom themselves, or when they
    // press Send / the ↓ button. m_programmaticScroll guards against our
    // own setValue() round-tripping through valueChanged and clobbering the
    // user-derived flag.
    bool m_stickToBottom = true;
    bool m_programmaticScroll = false;
};

#endif // ACP_SESSION_VIEW_H
