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

#include "GoalConversationSummary.h"

#include "AcpProtocol.h"
#include "AcpSessionModel.h"

namespace {

QString textContent(const AcpMessage &msg)
{
    QString text;
    for (const auto &block : msg.content) {
        if (block.kind == AcpProtocol::AcpContentBlock::Kind::Text)
            text += block.text;
    }
    return text;
}

} // namespace

QString GoalConversationSummary::fromModel(const AcpSessionModel *model, int startIndex)
{
    if (!model)
        return QStringLiteral("<conversation />");

    const auto &msgs = model->messages();
    if (startIndex < 0)
        startIndex = 0;
    if (startIndex > msgs.size())
        startIndex = msgs.size();

    QString xml;
    xml.reserve(32 + (msgs.size() - startIndex) * 96);
    xml = QStringLiteral("<conversation>\n");
    for (int i = startIndex; i < msgs.size(); ++i) {
        const auto &msg = msgs[i];
        if (msg.role == QLatin1String("user")) {
            xml += QStringLiteral("  <message role=\"user\">")
                   + textContent(msg).toHtmlEscaped()
                   + QStringLiteral("</message>\n");
        } else if (msg.role == QLatin1String("assistant")) {
            xml += QStringLiteral("  <message role=\"assistant\">")
                   + textContent(msg).toHtmlEscaped()
                   + QStringLiteral("</message>\n");
        }
    }
    xml += QStringLiteral("</conversation>");
    return xml;
}
