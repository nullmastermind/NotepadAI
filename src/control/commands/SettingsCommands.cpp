/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// settings.*, lua.*, macro.* (docs/adr/0007).

#include "Registrar.h"

#include "ApplicationSettings.h"
#include "Macro.h"
#include "MacroManager.h"
#include "MacroStep.h"
#include "NotepadNextApplication.h"
#include "ScintillaNext.h"
#include "dialogs/MainWindow.h"
#include "docks/LuaConsoleDock.h"

#include <QJsonArray>
#include <QMetaEnum>
#include <QMetaMethod>

#include <algorithm>
#include <cstring>

namespace control::commands {

namespace {

// Settings an agent may read but not change here: they need the user's
// confirmation in Preferences (the data folder moves every file) or are
// bookkeeping the app writes itself.
const QStringList &readOnlySettings()
{
    static const QStringList keys{QStringLiteral("dataDir"), QStringLiteral("commitMessageApiKeyConfigured"),
                                  QStringLiteral("lastProxyType"), QStringLiteral("lastProxyHost"),
                                  QStringLiteral("lastProxyPort"), QStringLiteral("lastProxyBypassList")};
    return keys;
}

struct Setting
{
    QString key;
    QMetaMethod getter;
    QMetaMethod setter;
};

// Every DEFINE_SETTING: a set<Name>(T) slot with an invokable <name>() getter.
QList<Setting> allSettings()
{
    QList<Setting> out;
    const QMetaObject &mo = ApplicationSettings::staticMetaObject;
    for (int i = mo.methodOffset(); i < mo.methodCount(); ++i) {
        const QMetaMethod setter = mo.method(i);
        const QByteArray name = setter.name();
        if (setter.methodType() != QMetaMethod::Slot || !name.startsWith("set") || setter.parameterCount() != 1
            || name.size() < 4)
            continue;
        QByteArray getterName = name.mid(3);
        getterName[0] = char(QChar::toLower(uint(getterName[0])));
        const int g = mo.indexOfMethod(getterName + "()");
        if (g < 0)
            continue;
        const QMetaMethod getter = mo.method(g);
        if (getter.methodType() != QMetaMethod::Method || getter.returnMetaType() != setter.parameterMetaType(0))
            continue;
        out.append({QString::fromLatin1(getterName), getter, setter});
    }
    return out;
}

QMetaEnum enumOf(const QMetaType &type)
{
    if (!(type.flags() & QMetaType::IsEnumeration))
        return {};
    const QMetaObject *scope = type.metaObject();
    if (!scope)
        return {};
    QByteArray name = type.name();
    name = name.mid(name.lastIndexOf(':') + 1);
    const int index = scope->indexOfEnumerator(name);
    return index < 0 ? QMetaEnum() : scope->enumerator(index);
}

qint64 enumStorage(const QVariant &v)
{
    qint64 value = 0;
    std::memcpy(&value, v.constData(), size_t(qMin<qsizetype>(v.metaType().sizeOf(), qsizetype(sizeof(value)))));
    return value;
}

QJsonValue settingValue(ApplicationSettings *settings, const Setting &s)
{
    const QMetaType type = s.getter.returnMetaType();
    QVariant v(type);
    s.getter.invoke(settings, Qt::DirectConnection, QGenericReturnArgument(type.name(), v.data()));
    if (const QMetaEnum e = enumOf(type); e.isValid())
        return QString::fromLatin1(e.valueToKey(int(enumStorage(v))));
    return QJsonValue::fromVariant(v);
}

QJsonObject settingJson(ApplicationSettings *settings, const Setting &s)
{
    const QMetaType type = s.getter.returnMetaType();
    QString typeName = QString::fromLatin1(type.name());
    QJsonObject o{{QStringLiteral("key"), s.key}, {QStringLiteral("value"), settingValue(settings, s)}};
    if (const QMetaEnum e = enumOf(type); e.isValid()) {
        QJsonArray values;
        for (int i = 0; i < e.keyCount(); ++i)
            values.append(QString::fromLatin1(e.key(i)));
        o.insert(QStringLiteral("values"), values);
        typeName = QStringLiteral("enum");
    } else if (type.id() == QMetaType::Bool) {
        typeName = QStringLiteral("bool");
    } else if (type.id() == QMetaType::Int) {
        typeName = QStringLiteral("int");
    } else if (type.id() == QMetaType::QString) {
        typeName = QStringLiteral("string");
    } else if (type.id() == QMetaType::QStringList) {
        typeName = QStringLiteral("string list");
    }
    o.insert(QStringLiteral("type"), typeName);
    if (readOnlySettings().contains(s.key))
        o.insert(QStringLiteral("readOnly"), true);
    return o;
}

// The JSON value as the setter's type; an error when it does not fit.
QString toSettingValue(const Setting &s, const QJsonValue &json, QVariant *out)
{
    const QMetaType type = s.setter.parameterMetaType(0);
    if (const QMetaEnum e = enumOf(type); e.isValid()) {
        bool ok = false;
        const int value = e.keyToValue(json.toString().toLatin1().constData(), &ok);
        if (!ok) {
            QStringList keys;
            for (int i = 0; i < e.keyCount(); ++i)
                keys.append(QString::fromLatin1(e.key(i)));
            return QStringLiteral("%1 is one of: %2").arg(s.key, keys.join(QStringLiteral(", ")));
        }
        QVariant v(type);
        const qint64 wide = value;
        std::memcpy(v.data(), &wide, size_t(qMin<qsizetype>(type.sizeOf(), qsizetype(sizeof(wide)))));
        *out = v;
        return QString();
    }
    const bool fits = (type.id() == QMetaType::Bool && json.isBool())
                      || (type.id() == QMetaType::Int && json.isDouble() && json.toDouble() == double(json.toInt()))
                      || (type.id() == QMetaType::QString && json.isString())
                      || (type.id() == QMetaType::QStringList && json.isArray());
    if (!fits)
        return QStringLiteral("%1 takes a %2").arg(s.key, QString::fromLatin1(type.name()));
    QVariant v = json.toVariant();
    if (!v.convert(type))
        return QStringLiteral("%1 takes a %2").arg(s.key, QString::fromLatin1(type.name()));
    *out = v;
    return QString();
}

} // namespace

void Registrar::settings()
{
    add(QStringLiteral("settings.list"), QStringLiteral("List every setting with its type and current value"), false,
        {optional(QStringLiteral("filter"), ArgType::String, QStringLiteral("only keys containing this text (case-insensitive)"))},
        [w = w](const CommandContext &ctx) {
            ApplicationSettings *settings = w->app->getSettings();
            const QString filter = ctx.str(QStringLiteral("filter"));
            QJsonArray list;
            for (const Setting &s : allSettings()) {
                if (filter.isEmpty() || s.key.contains(filter, Qt::CaseInsensitive))
                    list.append(settingJson(settings, s));
            }
            ctx.ok({{QStringLiteral("settings"), list}});
        });

    add(QStringLiteral("settings.get"), QStringLiteral("Read one setting"), false,
        {required(QStringLiteral("key"), ArgType::String, QStringLiteral("setting key from settings.list, e.g. wordWrap"))},
        [w = w](const CommandContext &ctx) {
            const QString key = ctx.str(QStringLiteral("key"));
            for (const Setting &s : allSettings()) {
                if (s.key == key) {
                    ctx.ok(settingJson(w->app->getSettings(), s));
                    return;
                }
            }
            ctx.fail(404, QStringLiteral("no setting '%1'; settings.list lists them").arg(key));
        });

    add(QStringLiteral("settings.set"),
        QStringLiteral("Change a setting; it applies at once, as saving it in Preferences does"), true,
        {required(QStringLiteral("key"), ArgType::String, QStringLiteral("setting key from settings.list, e.g. wordWrap")),
         required(QStringLiteral("value"), ArgType::Any,
                  QStringLiteral("the new value: true/false, a number, a string, a list of strings, or an enum name"))},
        [w = w](const CommandContext &ctx) {
            const QString key = ctx.str(QStringLiteral("key"));
            for (const Setting &s : allSettings()) {
                if (s.key != key)
                    continue;
                if (readOnlySettings().contains(key)) {
                    ctx.fail(409, QStringLiteral("%1 is changed by the user in Preferences").arg(key));
                    return;
                }
                QVariant value;
                if (const QString error = toSettingValue(s, ctx.args().value(QStringLiteral("value")), &value);
                    !error.isEmpty()) {
                    ctx.fail(400, error);
                    return;
                }
                ApplicationSettings *settings = w->app->getSettings();
                s.setter.invoke(settings, Qt::DirectConnection,
                                QGenericArgument(s.setter.parameterTypeName(0).constData(), value.constData()));
                if (ScintillaNext *editor = w->currentEditor())
                    ctx.reveal(editor);
                ctx.ok(settingJson(settings, s));
                return;
            }
            ctx.fail(404, QStringLiteral("no setting '%1'; settings.list lists them").arg(key));
        });
}

void Registrar::scripting()
{
    add(QStringLiteral("lua.run"),
        QStringLiteral("Run Lua in the Lua console (shown), against the current editor; returns what it printed"), true,
        {required(QStringLiteral("code"), ArgType::String, QStringLiteral("Lua code; an expression's value is printed"))},
        [w = w](const CommandContext &ctx) {
            auto *console = w->findChild<LuaConsoleDock *>();
            if (!console) {
                ctx.fail(422, QStringLiteral("the Lua console is not available"));
                return;
            }
            console->show();
            ctx.reveal(console);
            ctx.ok({{QStringLiteral("output"), console->runCommand(ctx.str(QStringLiteral("code")))}});
        });

    add(QStringLiteral("macro.list"), QStringLiteral("List the saved macros and whether one is recording"), false, {},
        [w = w](const CommandContext &ctx) {
            QJsonArray names;
            for (Macro *m : w->macroManager.availableMacros())
                names.append(m->getName());
            ctx.ok({{QStringLiteral("macros"), names},
                    {QStringLiteral("recording"), w->macroManager.isRecording()},
                    {QStringLiteral("unsavedRecording"), w->macroManager.hasCurrentUnsavedMacro()}});
        });

    add(QStringLiteral("macro.record"), QStringLiteral("Start or stop recording a macro in an editor"), true,
        {oneOf(QStringLiteral("action"), {QStringLiteral("start"), QStringLiteral("stop")},
               QStringLiteral("start or stop"), true),
         tabArg(QStringLiteral("editor tab to record in; default: the current editor"))},
        [w = w](const CommandContext &ctx) {
            const bool start = ctx.str(QStringLiteral("action")) == QLatin1String("start");
            if (start == w->macroManager.isRecording()) {
                ctx.fail(409, start ? QStringLiteral("a macro is already recording") : QStringLiteral("no macro is recording"));
                return;
            }
            if (start) {
                ScintillaNext *editor = editorArg(ctx, w);
                if (!editor)
                    return;
                w->switchToEditor(editor);
                ctx.reveal(editor);
                w->macroManager.startRecording(editor);
            } else {
                w->macroManager.stopRecording();
            }
            ctx.ok({{QStringLiteral("recording"), w->macroManager.isRecording()}});
        });

    add(QStringLiteral("macro.save"), QStringLiteral("Save the macro just recorded under a name"), true,
        {required(QStringLiteral("name"), ArgType::String, QStringLiteral("macro name"))},
        [w = w](const CommandContext &ctx) {
            if (!w->macroManager.hasCurrentUnsavedMacro()) {
                ctx.fail(409, QStringLiteral("no recorded macro to save; record one with macro.record"));
                return;
            }
            w->macroManager.saveCurrentMacro(ctx.str(QStringLiteral("name")));
            ctx.ok();
        });

    // What the Edit Macros dialog does, on the live macros; the list is saved
    // right away (the dialog's edits are kept at exit).
    auto macroNamed = [w = w](const CommandContext &ctx) -> Macro * {
        const QString name = ctx.str(QStringLiteral("name"));
        for (Macro *m : w->macroManager.availableMacros()) {
            if (m->getName() == name)
                return m;
        }
        ctx.fail(404, QStringLiteral("no macro '%1'; macro.list lists them").arg(name));
        return nullptr;
    };
    auto stepsJson = [](Macro *m) {
        QJsonArray steps;
        int i = 0;
        for (const MacroStep &step : m->getSteps()) {
            QJsonObject o{{QStringLiteral("index"), i++}, {QStringLiteral("action"), step.getName()}};
            if (MacroStep::MessageHasString(step.message))
                o.insert(QStringLiteral("text"), QString::fromUtf8(step.str));
            steps.append(o);
        }
        return steps;
    };
    const Arg macroName = required(QStringLiteral("name"), ArgType::String, QStringLiteral("saved macro name (macro.list)"));

    add(QStringLiteral("macro.steps"), QStringLiteral("List a saved macro's steps"), false, {macroName},
        [macroNamed, stepsJson](const CommandContext &ctx) {
            if (Macro *m = macroNamed(ctx))
                ctx.ok({{QStringLiteral("name"), m->getName()}, {QStringLiteral("steps"), stepsJson(m)}});
        });

    add(QStringLiteral("macro.actions"), QStringLiteral("List the step actions macro.insertStep accepts"), false, {},
        [](const CommandContext &ctx) {
            QJsonArray actions;
            for (Scintilla::Message message : MacroStep::RecordableMacroMessages())
                actions.append(QJsonObject{{QStringLiteral("action"), MacroStep::NameOfMessage(message)},
                                           {QStringLiteral("takesText"), MacroStep::MessageHasString(message)}});
            ctx.ok({{QStringLiteral("actions"), actions}});
        });

    add(QStringLiteral("macro.rename"), QStringLiteral("Rename a saved macro"), true,
        {macroName, required(QStringLiteral("to"), ArgType::String, QStringLiteral("the new name"))},
        [w = w, macroNamed](const CommandContext &ctx) {
            Macro *m = macroNamed(ctx);
            if (!m)
                return;
            const QString to = ctx.str(QStringLiteral("to")).trimmed();
            if (to.isEmpty()) {
                ctx.fail(400, QStringLiteral("the new name is empty"));
                return;
            }
            for (Macro *other : w->macroManager.availableMacros()) {
                if (other != m && other->getName() == to) {
                    ctx.fail(409, QStringLiteral("a macro named '%1' already exists").arg(to));
                    return;
                }
            }
            m->setName(to);
            w->macroManager.saveSettings();
            ctx.ok({{QStringLiteral("name"), to}});
        });

    add(QStringLiteral("macro.copy"), QStringLiteral("Copy a saved macro under a new name"), true,
        {macroName, optional(QStringLiteral("to"), ArgType::String, QStringLiteral("the copy's name; default: '<name> (Copy)'"))},
        [w = w, macroNamed](const CommandContext &ctx) {
            Macro *m = macroNamed(ctx);
            if (!m)
                return;
            auto *copy = new Macro();
            copy->setName(ctx.str(QStringLiteral("to"), m->getName() + QStringLiteral(" (Copy)")));
            for (const MacroStep &step : m->getSteps())
                copy->addMacroStep(step);
            QVector<Macro *> &macros = w->macroManager.availableMacros();
            macros.insert(macros.indexOf(m) + 1, copy);
            w->macroManager.saveSettings();
            ctx.ok({{QStringLiteral("name"), copy->getName()}});
        });

    add(QStringLiteral("macro.delete"), QStringLiteral("Delete a saved macro"), true, {macroName},
        [w = w, macroNamed](const CommandContext &ctx) {
            Macro *m = macroNamed(ctx);
            if (!m)
                return;
            if (m == w->macroManager.getCurrentMacro()) {
                ctx.fail(409, QStringLiteral("'%1' is the macro last recorded; record another first").arg(m->getName()));
                return;
            }
            w->macroManager.availableMacros().removeOne(m);
            delete m;
            w->macroManager.saveSettings();
            ctx.ok();
        });

    add(QStringLiteral("macro.insertStep"), QStringLiteral("Insert a step into a saved macro"), true,
        {macroName, required(QStringLiteral("action"), ArgType::String, QStringLiteral("a step action from macro.actions")),
         optional(QStringLiteral("text"), ArgType::String, QStringLiteral("the step's text, for actions that take one")),
         optional(QStringLiteral("at"), ArgType::Int, QStringLiteral("index to insert at; default: the end"))},
        [w = w, macroNamed, stepsJson](const CommandContext &ctx) {
            Macro *m = macroNamed(ctx);
            if (!m)
                return;
            const QString action = ctx.str(QStringLiteral("action"));
            for (Scintilla::Message message : MacroStep::RecordableMacroMessages()) {
                if (MacroStep::NameOfMessage(message) != action)
                    continue;
                MacroStep step(message, 0, 0);
                if (MacroStep::MessageHasString(message))
                    step.str = ctx.str(QStringLiteral("text")).toUtf8();
                QVector<MacroStep> &steps = m->getSteps();
                const qint64 at = std::clamp<qint64>(ctx.integer(QStringLiteral("at"), steps.size()), 0, steps.size());
                steps.insert(int(at), step);
                w->macroManager.saveSettings();
                ctx.ok({{QStringLiteral("steps"), stepsJson(m)}});
                return;
            }
            ctx.fail(400, QStringLiteral("no step action '%1'; macro.actions lists them").arg(action));
        });

    add(QStringLiteral("macro.deleteStep"), QStringLiteral("Delete a step from a saved macro"), true,
        {macroName, required(QStringLiteral("index"), ArgType::Int, QStringLiteral("step index (macro.steps)"))},
        [w = w, macroNamed, stepsJson](const CommandContext &ctx) {
            Macro *m = macroNamed(ctx);
            if (!m)
                return;
            const qint64 index = ctx.integer(QStringLiteral("index"));
            if (index < 0 || index >= m->size()) {
                ctx.fail(404, QStringLiteral("no step %1; the macro has %2").arg(index).arg(m->size()));
                return;
            }
            m->getSteps().remove(int(index));
            w->macroManager.saveSettings();
            ctx.ok({{QStringLiteral("steps"), stepsJson(m)}});
        });

    add(QStringLiteral("macro.moveStep"), QStringLiteral("Move a step within a saved macro"), true,
        {macroName, required(QStringLiteral("index"), ArgType::Int, QStringLiteral("step index (macro.steps)")),
         required(QStringLiteral("to"), ArgType::Int, QStringLiteral("its new index"))},
        [w = w, macroNamed, stepsJson](const CommandContext &ctx) {
            Macro *m = macroNamed(ctx);
            if (!m)
                return;
            const qint64 from = ctx.integer(QStringLiteral("index"));
            const qint64 to = ctx.integer(QStringLiteral("to"));
            if (from < 0 || from >= m->size() || to < 0 || to >= m->size()) {
                ctx.fail(404, QStringLiteral("step indexes run 0..%1").arg(m->size() - 1));
                return;
            }
            m->getSteps().move(int(from), int(to));
            w->macroManager.saveSettings();
            ctx.ok({{QStringLiteral("steps"), stepsJson(m)}});
        });

    add(QStringLiteral("macro.run"), QStringLiteral("Play a macro in an editor"), true,
        {optional(QStringLiteral("name"), ArgType::String, QStringLiteral("saved macro name; default: the one last recorded")),
         optional(QStringLiteral("times"), ArgType::Int, QStringLiteral("how many times; default 1")),
         optional(QStringLiteral("toEnd"), ArgType::Bool, QStringLiteral("repeat until the end of the file")),
         tabArg(QStringLiteral("editor tab; default: the current editor"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *editor = editorArg(ctx, w);
            if (!editor)
                return;
            Macro *macro = w->macroManager.getCurrentMacro();
            if (ctx.has(QStringLiteral("name"))) {
                macro = nullptr;
                for (Macro *m : w->macroManager.availableMacros()) {
                    if (m->getName() == ctx.str(QStringLiteral("name")))
                        macro = m;
                }
            }
            if (!macro) {
                ctx.fail(404, ctx.has(QStringLiteral("name"))
                                  ? QStringLiteral("no macro '%1'; macro.list lists them").arg(ctx.str(QStringLiteral("name")))
                                  : QStringLiteral("no macro has been recorded"));
                return;
            }
            w->switchToEditor(editor);
            ctx.reveal(editor);
            if (ctx.flag(QStringLiteral("toEnd")))
                macro->replayTillEndOfFile(editor);
            else
                macro->replay(editor, int(qMax<qint64>(1, ctx.integer(QStringLiteral("times"), 1))));
            ctx.ok();
        });
}

} // namespace control::commands
