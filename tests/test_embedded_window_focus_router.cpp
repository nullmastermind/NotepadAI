/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QtTest>

#include "EmbeddedWindowFocusRouter.h"

class TestEmbeddedWindowFocusRouter : public QObject
{
    Q_OBJECT

private slots:
    void editorToEmbed_restoresMatchingHostExactlyOnce();
    void nonEmbedTab_isIgnored();
    void destroyedHost_cancelsQueuedRestore();
    void showFocusAndTabSignals_areCoalesced();
};

static void deliverQueuedRestores()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
}

void TestEmbeddedWindowFocusRouter::editorToEmbed_restoresMatchingHostExactlyOnce()
{
    EmbeddedWindowFocusRouter router;
    QWidget editor;
    QWidget embed;
    int restores = 0;
    router.registerHost(&embed, [&restores]() { ++restores; });

    router.request(&editor); // leave embed for an ordinary editor
    router.request(&embed);  // return to the embed tab
    deliverQueuedRestores();

    QCOMPARE(restores, 1);
}

void TestEmbeddedWindowFocusRouter::nonEmbedTab_isIgnored()
{
    EmbeddedWindowFocusRouter router;
    QWidget embed;
    QWidget browserPreview;
    int restores = 0;
    router.registerHost(&embed, [&restores]() { ++restores; });

    router.request(&browserPreview);
    deliverQueuedRestores();

    QCOMPARE(restores, 0);
}

void TestEmbeddedWindowFocusRouter::destroyedHost_cancelsQueuedRestore()
{
    EmbeddedWindowFocusRouter router;
    int restores = 0;
    auto *embed = new QWidget;
    router.registerHost(embed, [&restores]() { ++restores; });
    router.request(embed);

    delete embed;
    deliverQueuedRestores();

    QCOMPARE(restores, 0);
}

void TestEmbeddedWindowFocusRouter::showFocusAndTabSignals_areCoalesced()
{
    EmbeddedWindowFocusRouter router;
    QWidget embed;
    int restores = 0;
    router.registerHost(&embed, [&restores]() { ++restores; });

    // ADS tab activation plus QWidget Show and FocusIn can all occur in the same
    // event-loop turn. Native child mouse activation uses this same request path.
    router.request(&embed);
    router.request(&embed);
    router.request(&embed);
    deliverQueuedRestores();
    QCOMPARE(restores, 1);

    // A later user interaction is a new turn and must restore once again.
    router.request(&embed);
    deliverQueuedRestores();
    QCOMPARE(restores, 2);
}

QTEST_MAIN(TestEmbeddedWindowFocusRouter)
#include "test_embedded_window_focus_router.moc"
