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


#ifndef PREFERENCESPENDINGEDITS_H
#define PREFERENCESPENDINGEDITS_H

#include "ApplicationSettings.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QObject>
#include <QSignalBlocker>

#include <functional>
#include <vector>


class PreferencesPendingEdits
{
public:
    explicit PreferencesPendingEdits(ApplicationSettings *settings)
        : m_settings(settings)
    {}

    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

    template<typename Getter, typename Setter>
    void mapCheckBox(QCheckBox *checkBox, Getter getter, Setter setter)
    {
        auto load = [this, checkBox, getter]() {
            QSignalBlocker blocker(checkBox);
            checkBox->setChecked((m_settings->*getter)());
        };
        load();
        m_revert.push_back(load);
        QObject::connect(checkBox, &QCheckBox::toggled, checkBox, [this](bool) {
            markDirty();
        });
        m_apply.push_back([this, checkBox, setter]() {
            (m_settings->*setter)(checkBox->isChecked());
        });
    }

    template<typename Getter, typename Setter>
    void mapGroupBox(QGroupBox *groupBox, Getter getter, Setter setter)
    {
        auto load = [this, groupBox, getter]() {
            QSignalBlocker blocker(groupBox);
            groupBox->setChecked((m_settings->*getter)());
        };
        load();
        m_revert.push_back(load);
        QObject::connect(groupBox, &QGroupBox::toggled, groupBox, [this](bool) {
            markDirty();
        });
        m_apply.push_back([this, groupBox, setter]() {
            (m_settings->*setter)(groupBox->isChecked());
        });
    }

    void addApply(std::function<void()> fn)
    {
        m_apply.push_back(std::move(fn));
    }

    void addRevert(std::function<void()> fn)
    {
        m_revert.push_back(std::move(fn));
    }

    void apply()
    {
        for (auto &fn : m_apply)
            fn();
        m_dirty = false;
    }

    void revert()
    {
        for (auto &fn : m_revert)
            fn();
        m_dirty = false;
    }

private:
    ApplicationSettings *m_settings;
    bool m_dirty = false;
    std::vector<std::function<void()>> m_apply;
    std::vector<std::function<void()>> m_revert;
};

#endif
