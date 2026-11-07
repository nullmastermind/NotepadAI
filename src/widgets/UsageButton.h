#ifndef USAGE_BUTTON_H
#define USAGE_BUTTON_H

#include <QElapsedTimer>
#include <QEnterEvent>
#include <QTimer>
#include <QToolButton>

#include "ai/ClaudeUsage.h"

namespace ai { class ClaudeUsageAccounts; }
class UsagePopover;

// Menu-bar chip "[Claude glyph] 26%": per signed-in account, its shortest Claude
// window (5-hour, else weekly; "26% · 70%" with several accounts), text tinted
// warning / danger from 80% / 95% on the hottest shown window. Hidden until the first response
// and while every account is signed out. Click opens the UsagePopover.
class UsageButton : public QToolButton
{
    Q_OBJECT

public:
    explicit UsageButton(ai::ClaudeUsageAccounts *accounts, QWidget *parent = nullptr);

    void setPercentMode(ai::claudeusage::PercentMode mode);

signals:
    void percentModeChanged(ai::claudeusage::PercentMode mode);

protected:
    void changeEvent(QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void onServiceChanged();
    void togglePopover();
    void applyTone(ai::claudeusage::Tone tone);
    void rebuildIcon();
    void relayoutMenuBar();
    void pushDataToPopover();

    ai::ClaudeUsageAccounts *m_accounts = nullptr;
    UsagePopover *m_popover = nullptr;
    QTimer m_hoverTimer;          // debounced refresh while the pointer rests on the chip
    QElapsedTimer m_lastClosed;   // swallows the click that dismissed the popup
    ai::claudeusage::PercentMode m_mode = ai::claudeusage::PercentMode::Used;
    ai::claudeusage::Tone m_tone = ai::claudeusage::Tone::Ok;
    bool m_applyingTone = false;
};

#endif // USAGE_BUTTON_H
