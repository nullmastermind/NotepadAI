#include "GoalConfigWidget.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <functional>

#include "AcpAgentRegistry.h"
#include "ApplicationSettings.h"
#include "GoalAgentSettings.h"
#include "GoalCustomApiFields.h"
#include "GoalHttpJudge.h"
#include "ProjectGoalPresets.h"

static constexpr int kMaxRows = GoalAgentSettings::kMaxCriteriaRows;

GoalConfigWidget::GoalConfigWidget(AcpAgentRegistry *registry,
                                   ApplicationSettings *settings,
                                   QWidget *parent)
    : QWidget(parent)
    , m_registry(registry)
    , m_settings(settings)
{
    buildUi();
    populateAgents();
    connect(m_agentCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                updateCustomApiVisibility();
                const QString agentId = m_agentCombo->currentData().toString();
                if (agentId.isEmpty() || !m_settings)
                    return;

                const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
                GoalAgentSettings goalSettings;
                if (!settingsJson.isEmpty()) {
                    goalSettings = GoalAgentSettings::fromJson(
                        QJsonDocument::fromJson(settingsJson.toUtf8()).object());
                }
                if (goalSettings.agentId == agentId)
                    return;

                goalSettings.agentId = agentId;
                m_settings->setValue(
                    QStringLiteral("Ai/GoalAgentSettings"),
                    QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
            });
    populateTemplates();
    connect(m_templateCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (!m_rememberPromptTemplate)
                    return;
                GoalAgentSettings::rememberPromptTemplateId(
                    m_settings, m_templateCombo->currentData().toString());
            });
    populatePresets();
    updateRowCount();
    updateTemplateButtons();
}

void GoalConfigWidget::setProjectRoot(const QString &root)
{
    if (m_projectRoot == root)
        return;
    m_projectRoot = root;
    if (m_presetMenu)
        populatePresets();
}

// PLACEHOLDER_BUILDUI

void GoalConfigWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(8);

    auto *criteriaLabel = new QLabel(tr("Success criteria"), this);
    criteriaLabel->setStyleSheet(QStringLiteral("font-weight: 500; font-size: 12px;"));
    mainLayout->addWidget(criteriaLabel);

    auto *helpLabel = new QLabel(
        tr("Each row is one milestone. The goal-agent advances to the next when "
           "it judges the previous one done. Up to %1 rows.").arg(kMaxRows), this);
    helpLabel->setWordWrap(true);
    helpLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: gray;"));
    mainLayout->addWidget(helpLabel);

    auto *presetLayout = new QHBoxLayout;
    m_loadPresetBtn = new QPushButton(tr("Load preset"), this);
    m_loadPresetBtn->setObjectName(QStringLiteral("loadPresetButton"));
    m_presetMenu = new QMenu(this);
    m_loadPresetBtn->setMenu(m_presetMenu);
    connect(m_presetMenu, &QMenu::aboutToShow, this, &GoalConfigWidget::populatePresets);
    presetLayout->addWidget(m_loadPresetBtn);
    m_savePresetBtn = new QPushButton(tr("Save as preset"), this);
    m_savePresetBtn->setObjectName(QStringLiteral("savePresetButton"));
    connect(m_savePresetBtn, &QPushButton::clicked, this, &GoalConfigWidget::onSavePreset);
    presetLayout->addWidget(m_savePresetBtn);
    presetLayout->addStretch();
    mainLayout->addLayout(presetLayout);

    m_criteriaScroll = new QScrollArea(this);
    m_criteriaScroll->setWidgetResizable(true);
    m_criteriaScroll->setFrameShape(QFrame::NoFrame);
    m_criteriaScroll->setMinimumHeight(100);
    m_criteriaScroll->setMaximumHeight(280);
    auto *criteriaHost = new QWidget(m_criteriaScroll);
    m_criteriaLayout = new QVBoxLayout(criteriaHost);
    m_criteriaLayout->setContentsMargins(0, 0, 0, 0);
    m_criteriaLayout->setSpacing(4);
    m_criteriaLayout->addStretch();
    m_criteriaScroll->setWidget(criteriaHost);
    mainLayout->addWidget(m_criteriaScroll);

    m_criteriaEdits.append(createCriterionEdit());

    auto *addRemoveLayout = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("+ Add criterion"), this);
    connect(m_addBtn, &QPushButton::clicked, this, &GoalConfigWidget::onAddCriterion);
    addRemoveLayout->addWidget(m_addBtn);
    m_removeBtn = new QPushButton(tr("Remove"), this);
    connect(m_removeBtn, &QPushButton::clicked, this, &GoalConfigWidget::onRemoveCriterion);
    addRemoveLayout->addWidget(m_removeBtn);
    addRemoveLayout->addStretch();
    m_rowCountLabel = new QLabel(this);
    m_rowCountLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: gray;"));
    addRemoveLayout->addWidget(m_rowCountLabel);
    mainLayout->addLayout(addRemoveLayout);

    // Goal-agent
    auto *agentLabel = new QLabel(tr("Goal-agent"), this);
    agentLabel->setStyleSheet(QStringLiteral("font-weight: 500; font-size: 12px;"));
    mainLayout->addWidget(agentLabel);
    m_agentCombo = new QComboBox(this);
    mainLayout->addWidget(m_agentCombo);

    m_customApi = new GoalCustomApiFields(this);
    m_customApi->setSettings(m_settings);
    m_customApi->hide();
    mainLayout->addWidget(m_customApi);

    // Prompt template
    auto *tplLabel = new QLabel(tr("Prompt template"), this);
    tplLabel->setStyleSheet(QStringLiteral("font-weight: 500; font-size: 12px;"));
    mainLayout->addWidget(tplLabel);
    auto *tplRow = new QHBoxLayout;
    m_templateCombo = new QComboBox(this);
    m_templateCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tplRow->addWidget(m_templateCombo);
    auto *tplNewBtn = new QPushButton(tr("New"), this);
    connect(tplNewBtn, &QPushButton::clicked, this, &GoalConfigWidget::onTemplateNew);
    tplRow->addWidget(tplNewBtn);
    m_tplRenameBtn = new QPushButton(tr("Rename"), this);
    connect(m_tplRenameBtn, &QPushButton::clicked, this, &GoalConfigWidget::onTemplateRename);
    tplRow->addWidget(m_tplRenameBtn);
    m_tplEditBtn = new QPushButton(tr("Edit"), this);
    connect(m_tplEditBtn, &QPushButton::clicked, this, &GoalConfigWidget::onTemplateEdit);
    tplRow->addWidget(m_tplEditBtn);
    m_tplDeleteBtn = new QPushButton(tr("Delete"), this);
    connect(m_tplDeleteBtn, &QPushButton::clicked, this, &GoalConfigWidget::onTemplateDelete);
    tplRow->addWidget(m_tplDeleteBtn);
    connect(m_templateCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() { updateTemplateButtons(); });
    mainLayout->addLayout(tplRow);

    // Max iterations
    auto *iterLabel = new QLabel(tr("Max iterations"), this);
    iterLabel->setStyleSheet(QStringLiteral("font-weight: 500; font-size: 12px;"));
    mainLayout->addWidget(iterLabel);
    m_maxIterSpin = new QSpinBox(this);
    m_maxIterSpin->setRange(GoalAgentSettings::kMaxIterationsMin,
                            GoalAgentSettings::kMaxIterationsMax);
    m_maxIterSpin->setToolTip(
        tr("Per criterion. Hitting the cap advances to the next and resets the turn count."));
    const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
    GoalAgentSettings goalSettings;
    if (!settingsJson.isEmpty()) {
        goalSettings = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }
    m_maxIterSpin->setValue(goalSettings.defaultMaxIterations);
    mainLayout->addWidget(m_maxIterSpin);
}

GoalConfigResult GoalConfigWidget::result() const
{
    GoalConfigResult r;
    for (auto *edit : m_criteriaEdits) {
        const QString text = edit->toPlainText().trimmed();
        if (!text.isEmpty())
            r.criteriaList.append(text);
    }
    r.agentId = m_agentCombo->currentData().toString();
    r.maxIterations = m_maxIterSpin->value();
    r.promptTemplateId = m_templateCombo->currentData().toString();
    return r;
}

void GoalConfigWidget::setCriteria(const QStringList &criteria)
{
    for (auto *edit : m_criteriaEdits)
        edit->deleteLater();
    m_criteriaEdits.clear();
    if (criteria.isEmpty()) {
        m_criteriaEdits.append(createCriterionEdit());
    } else {
        for (const QString &c : criteria)
            m_criteriaEdits.append(createCriterionEdit(c));
    }
    updateRowCount();
}

void GoalConfigWidget::setAgentId(const QString &agentId)
{
    int idx = m_agentCombo->findData(agentId);
    if (idx >= 0) m_agentCombo->setCurrentIndex(idx);
}

void GoalConfigWidget::setMaxIterations(int value)
{
    m_maxIterSpin->setValue(value);
}

void GoalConfigWidget::setPromptTemplateId(const QString &id)
{
    if (!m_templateCombo)
        return;
    const QString trimmed = id.trimmed();
    int idx = trimmed.isEmpty() ? -1 : m_templateCombo->findData(trimmed);
    if (idx < 0)
        idx = m_templateCombo->findData(QLatin1String(GoalAgentSettings::kDefaultTemplateId));
    if (idx < 0)
        return;
    const QSignalBlocker blocker(m_templateCombo);
    m_templateCombo->setCurrentIndex(idx);
    updateTemplateButtons();
}

void GoalConfigWidget::setRememberPromptTemplate(bool remember)
{
    m_rememberPromptTemplate = remember;
}

void GoalConfigWidget::populateAgents()
{
    m_agentCombo->clear();
    const auto agents = m_registry->agents();
    const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
    GoalAgentSettings goalSettings;
    if (!settingsJson.isEmpty()) {
        goalSettings = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }
    int selectedIdx = 0;
    for (int i = 0; i < agents.size(); ++i) {
        const auto &a = agents[i];
        m_agentCombo->addItem(a.name.isEmpty() ? a.id : a.name, a.id);
        if (a.id == goalSettings.agentId)
            selectedIdx = i;
    }
    const int customIdx = m_agentCombo->count();
    m_agentCombo->addItem(tr("Custom API"), QLatin1String(GoalHttpJudge::kAgentId));
    if (goalSettings.agentId == QLatin1String(GoalHttpJudge::kAgentId))
        selectedIdx = customIdx;

    if (m_customApi)
        m_customApi->loadFromSettings();

    if (m_agentCombo->count() == 0)
        return;

    m_agentCombo->setCurrentIndex(selectedIdx);
    const QString effectiveAgentId = m_agentCombo->currentData().toString();
    if (!goalSettings.agentId.isEmpty() && goalSettings.agentId != effectiveAgentId) {
        goalSettings.agentId = effectiveAgentId;
        m_settings->setValue(
            QStringLiteral("Ai/GoalAgentSettings"),
            QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
    }
    updateCustomApiVisibility();
}

void GoalConfigWidget::populateTemplates()
{
    if (!m_templateCombo)
        return;

    const QString selectedId = GoalAgentSettings::promptTemplateIdForUi(m_settings);
    GoalAgentSettings goalSettings;
    if (m_settings) {
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        if (!settingsJson.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(settingsJson.toUtf8());
            if (doc.isObject())
                goalSettings = GoalAgentSettings::fromJson(doc.object());
        }
    }

    const QSignalBlocker blocker(m_templateCombo);
    m_templateCombo->clear();
    int selectedIdx = 0;
    for (int i = 0; i < goalSettings.promptTemplates.size(); ++i) {
        const auto &t = goalSettings.promptTemplates.at(i);
        QString label = t.name;
        if (t.id == QLatin1String(GoalAgentSettings::kDefaultTemplateId))
            label += QStringLiteral(" (default)");
        m_templateCombo->addItem(label, t.id);
        if (t.id == selectedId)
            selectedIdx = i;
    }
    if (m_templateCombo->count() == 0)
        return;
    m_templateCombo->setCurrentIndex(selectedIdx);
    updateTemplateButtons();
}

void GoalConfigWidget::populatePresets()
{
    if (!m_presetMenu)
        return;
    m_presetMenu->clear();
    const QString settingsJson = m_settings ? m_settings->get("Ai/GoalAgentSettings", QString()) : QString();
    GoalAgentSettings goalSettings;
    if (!settingsJson.isEmpty()) {
        goalSettings = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }

    QList<ProjectGoalPresets::Listed> project;
    if (ProjectGoalPresets::projectScopeAvailable(m_projectRoot, false))
        project = ProjectGoalPresets::list(m_projectRoot);

    if (goalSettings.criteriaPresets.isEmpty() && project.isEmpty()) {
        m_presetMenu->addAction(tr("No saved presets"))->setEnabled(false);
        return;
    }

    auto applyCriteria = [this](const QStringList &criteria) {
        for (auto *edit : m_criteriaEdits)
            edit->deleteLater();
        m_criteriaEdits.clear();
        for (const QString &text : criteria)
            m_criteriaEdits.append(createCriterionEdit(text));
        if (m_criteriaEdits.isEmpty())
            m_criteriaEdits.append(createCriterionEdit());
        updateRowCount();
    };

    auto addRow = [this](const QString &label,
                         const std::function<void()> &onLoad,
                         const std::function<void()> &onDelete) {
        auto *wa = new QWidgetAction(m_presetMenu);
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(6, 2, 4, 2);
        hl->setSpacing(4);
        auto *loadBtn = new QPushButton(label, row);
        loadBtn->setFlat(true);
        loadBtn->setCursor(Qt::PointingHandCursor);
        loadBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        loadBtn->setStyleSheet(QStringLiteral("QPushButton { text-align: left; padding: 2px 4px; }"));
        hl->addWidget(loadBtn);
        auto *delBtn = new QToolButton(row);
        delBtn->setIcon(QIcon(QStringLiteral(":/icons/bin_closed.png")));
        delBtn->setAutoRaise(true);
        delBtn->setIconSize(QSize(14, 14));
        delBtn->setFixedSize(20, 20);
        delBtn->setToolTip(tr("Delete"));
        hl->addWidget(delBtn);
        wa->setDefaultWidget(row);
        m_presetMenu->addAction(wa);
        connect(loadBtn, &QPushButton::clicked, this, [this, onLoad]() {
            m_presetMenu->close();
            onLoad();
        });
        connect(delBtn, &QToolButton::clicked, this, [this, onDelete]() {
            m_presetMenu->close();
            onDelete();
        });
    };

    for (const auto &preset : goalSettings.criteriaPresets) {
        addRow(QStringLiteral("%1 (%2)").arg(preset.name).arg(preset.criteria.size()),
               [this, applyCriteria, criteria = preset.criteria]() { applyCriteria(criteria); },
               [this, id = preset.id, name = preset.name]() {
                   if (QMessageBox::question(this, tr("Delete preset"),
                           tr("Delete preset \"%1\"?").arg(name)) != QMessageBox::Yes)
                       return;
                   const QString json = m_settings->get("Ai/GoalAgentSettings", QString());
                   if (json.isEmpty())
                       return;
                   GoalAgentSettings gs = GoalAgentSettings::fromJson(
                       QJsonDocument::fromJson(json.toUtf8()).object());
                   gs.criteriaPresets.removeIf([&id](const GoalCriteriaPreset &p) { return p.id == id; });
                   m_settings->setValue(QStringLiteral("Ai/GoalAgentSettings"),
                       QString::fromUtf8(QJsonDocument(gs.toJson()).toJson(QJsonDocument::Compact)));
                   populatePresets();
               });
    }

    if (!goalSettings.criteriaPresets.isEmpty() && !project.isEmpty())
        m_presetMenu->addSeparator();

    for (const ProjectGoalPresets::Listed &preset : project) {
        const QString label = QStringLiteral("project/%1 (%2)").arg(preset.name).arg(preset.count);
        addRow(label,
               [this, applyCriteria, name = preset.name]() {
                   QStringList criteria;
                   QString error;
                   if (!ProjectGoalPresets::read(m_projectRoot, name, &criteria, &error)) {
                       QMessageBox::warning(this, tr("Load preset"), error);
                       populatePresets();
                       return;
                   }
                   applyCriteria(criteria);
               },
               [this, name = preset.name]() {
                   if (QMessageBox::question(this, tr("Delete preset"),
                           tr("Delete preset \"%1\"?").arg(name)) != QMessageBox::Yes)
                       return;
                   QString error;
                   if (!ProjectGoalPresets::remove(m_projectRoot, name, &error)) {
                       QMessageBox::warning(this, tr("Delete preset"), error);
                       return;
                   }
                   populatePresets();
               });
    }
}

void GoalConfigWidget::updateRowCount()
{
    m_rowCountLabel->setText(
        QStringLiteral("%1 / %2 rows").arg(m_criteriaEdits.size()).arg(kMaxRows));
    m_addBtn->setEnabled(m_criteriaEdits.size() < kMaxRows);
    m_removeBtn->setEnabled(m_criteriaEdits.size() > 1);
}

void GoalConfigWidget::updateTemplateButtons()
{
    const bool isBuiltin = m_templateCombo->currentData().toString()
                           == QLatin1String(GoalAgentSettings::kDefaultTemplateId);
    m_tplRenameBtn->setEnabled(!isBuiltin);
    m_tplEditBtn->setEnabled(!isBuiltin);
    m_tplDeleteBtn->setEnabled(!isBuiltin);
}

QPlainTextEdit *GoalConfigWidget::createCriterionEdit(const QString &text)
{
    auto *edit = new QPlainTextEdit(m_criteriaScroll->widget());
    edit->setPlainText(text);
    edit->setPlaceholderText(tr("Describe a success criterion..."));
    edit->setMinimumHeight(56);
    edit->setMaximumHeight(100);
    edit->setTabChangesFocus(true);
    const int insertIdx = m_criteriaLayout->count() - 1;
    m_criteriaLayout->insertWidget(insertIdx, edit);
    return edit;
}

void GoalConfigWidget::onAddCriterion()
{
    if (m_criteriaEdits.size() >= kMaxRows)
        return;
    auto *edit = createCriterionEdit();
    m_criteriaEdits.append(edit);
    edit->setFocus();
    updateRowCount();
}

void GoalConfigWidget::onRemoveCriterion()
{
    if (m_criteriaEdits.size() <= 1)
        return;
    QPlainTextEdit *target = nullptr;
    for (auto *edit : m_criteriaEdits) {
        if (edit->hasFocus()) {
            target = edit;
            break;
        }
    }
    if (!target)
        target = m_criteriaEdits.last();
    m_criteriaEdits.removeOne(target);
    target->deleteLater();
    updateRowCount();
}

void GoalConfigWidget::onSavePreset()
{
    QStringList criteria;
    for (auto *edit : m_criteriaEdits) {
        const QString text = edit->toPlainText().trimmed();
        if (!text.isEmpty())
            criteria.append(text);
    }
    if (criteria.isEmpty()) return;

    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Save criteria preset"));
    auto *layout = new QVBoxLayout(dlg);
    layout->addWidget(new QLabel(tr("Preset name:"), dlg));
    auto *nameEdit = new QLineEdit(dlg);
    nameEdit->setMaxLength(100);
    layout->addWidget(nameEdit);
    layout->addWidget(new QLabel(tr("Scope:"), dlg));
    auto *scope = new QComboBox(dlg);
    scope->setObjectName(QStringLiteral("presetScopeCombo"));
    scope->addItem(tr("Global"), QStringLiteral("global"));
    scope->addItem(tr("Project"), QStringLiteral("project"));
    const bool projectOk = ProjectGoalPresets::projectScopeAvailable(m_projectRoot, false);
    if (!projectOk) {
        if (auto *model = qobject_cast<QStandardItemModel *>(scope->model())) {
            if (QStandardItem *item = model->item(1)) {
                item->setEnabled(false);
                item->setToolTip(tr("No local project folder for this session."));
            }
        }
    }
    layout->addWidget(scope);
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    layout->addWidget(btnBox);
    connect(btnBox, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    bool savedProject = false;
    connect(btnBox, &QDialogButtonBox::accepted, dlg, [this, dlg, nameEdit, scope, criteria, &savedProject]() {
        const QString name = nameEdit->text().trimmed();
        if (name.isEmpty())
            return;
        if (scope->currentData().toString() == QLatin1String("project")) {
            QString error;
            if (!ProjectGoalPresets::save(m_projectRoot, name, criteria, &error)) {
                QMessageBox::warning(dlg, tr("Save criteria preset"), error);
                return;
            }
            savedProject = true;
        }
        dlg->accept();
    });
    if (dlg->exec() != QDialog::Accepted) { delete dlg; return; }
    const QString name = nameEdit->text().trimmed();
    delete dlg;
    if (savedProject) {
        populatePresets();
        return;
    }

    const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
    GoalAgentSettings goalSettings;
    if (!settingsJson.isEmpty()) {
        goalSettings = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }
    bool replaced = false;
    for (int i = 0; i < goalSettings.criteriaPresets.size(); ++i) {
        if (goalSettings.criteriaPresets[i].name.trimmed() == name) {
            goalSettings.criteriaPresets[i].criteria = criteria;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        GoalCriteriaPreset preset;
        preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        preset.name = name;
        preset.criteria = criteria;
        goalSettings.criteriaPresets.append(preset);
    }
    m_settings->setValue(QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
    populatePresets();
}

void GoalConfigWidget::onTemplateNew()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Prompt Template"),
        tr("Template name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
    GoalAgentSettings goalSettings;
    if (!settingsJson.isEmpty()) {
        goalSettings = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }
    const QString trimmed = name.trimmed();
    for (const auto &t : goalSettings.promptTemplates) {
        if (t.name.compare(trimmed, Qt::CaseInsensitive) == 0) {
            QMessageBox::information(this, tr("New Prompt Template"),
                tr("A template named \"%1\" already exists.").arg(t.name));
            return;
        }
    }
    GoalPromptTemplate tpl;
    tpl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tpl.name = trimmed;
    tpl.content = GoalAgentSettings::builtinPromptContent();
    goalSettings.promptTemplates.append(tpl);
    m_settings->setValue(QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
    populateTemplates();
    int idx = m_templateCombo->findData(tpl.id);
    if (idx >= 0) m_templateCombo->setCurrentIndex(idx);
}

void GoalConfigWidget::onTemplateRename()
{
    const QString tplId = m_templateCombo->currentData().toString();
    if (tplId.isEmpty() || tplId == QLatin1String(GoalAgentSettings::kDefaultTemplateId)) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Prompt Template"),
        tr("New name:"), QLineEdit::Normal, m_templateCombo->currentText(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
    GoalAgentSettings goalSettings;
    if (!settingsJson.isEmpty()) {
        goalSettings = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }
    const QString trimmed = name.trimmed();
    for (const auto &t : goalSettings.promptTemplates) {
        if (t.id != tplId && t.name.compare(trimmed, Qt::CaseInsensitive) == 0) {
            QMessageBox::information(this, tr("Rename Prompt Template"),
                tr("A template named \"%1\" already exists.").arg(t.name));
            return;
        }
    }
    for (auto &tpl : goalSettings.promptTemplates) {
        if (tpl.id == tplId) { tpl.name = trimmed; break; }
    }
    m_settings->setValue(QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
    populateTemplates();
    int idx = m_templateCombo->findData(tplId);
    if (idx >= 0) m_templateCombo->setCurrentIndex(idx);
}

void GoalConfigWidget::onTemplateEdit()
{
    const QString tplId = m_templateCombo->currentData().toString();
    if (tplId.isEmpty() || tplId == QLatin1String(GoalAgentSettings::kDefaultTemplateId)) return;

    const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
    if (settingsJson.isEmpty()) return;
    GoalAgentSettings goalSettings = GoalAgentSettings::fromJson(
        QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    const GoalPromptTemplate *current = goalSettings.findTemplate(tplId);
    if (!current) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Edit Prompt Template"));
    dlg.resize(640, 480);
    auto *layout = new QVBoxLayout(&dlg);
    auto *help = new QLabel(
        tr("Required placeholders: {{goal}}, {{conversation}}, {{iteration}}, "
           "{{maxIterations}}, {{criterionIndex}}, {{totalCriteria}}"), &dlg);
    help->setWordWrap(true);
    help->setStyleSheet(QStringLiteral("font-size: 11px; color: gray;"));
    layout->addWidget(help);
    auto *editor = new QPlainTextEdit(&dlg);
    editor->setPlainText(current->content);
    layout->addWidget(editor, 1);
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(btnBox);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    for (auto &tpl : goalSettings.promptTemplates) {
        if (tpl.id == tplId) { tpl.content = editor->toPlainText(); break; }
    }
    m_settings->setValue(QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
}

void GoalConfigWidget::onTemplateDelete()
{
    const QString tplId = m_templateCombo->currentData().toString();
    if (tplId.isEmpty()) return;
    if (tplId == QLatin1String(GoalAgentSettings::kDefaultTemplateId)) {
        QMessageBox::information(this, tr("Delete Template"),
            tr("The built-in default template cannot be deleted."));
        return;
    }
    if (QMessageBox::question(this, tr("Delete Template"),
            tr("Delete template \"%1\"?").arg(m_templateCombo->currentText()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
    GoalAgentSettings goalSettings;
    if (!settingsJson.isEmpty()) {
        goalSettings = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }
    goalSettings.promptTemplates.removeIf([&](const GoalPromptTemplate &t) { return t.id == tplId; });
    m_settings->setValue(QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
    populateTemplates();
}

void GoalConfigWidget::persistPendingCustomApi()
{
    if (m_customApi)
        m_customApi->persistPending();
}

QString GoalConfigWidget::customApiValidationError() const
{
    if (!GoalHttpJudge::isCustomApiAgent(m_agentCombo->currentData().toString()))
        return {};
    return m_customApi ? m_customApi->validationError() : QString();
}

void GoalConfigWidget::updateCustomApiVisibility()
{
    const bool custom = GoalHttpJudge::isCustomApiAgent(
        m_agentCombo->currentData().toString());
    if (!m_customApi)
        return;
    m_customApi->setVisible(custom);
    if (custom)
        m_customApi->refreshStatus();
}

void GoalConfigWidget::setJudgeLoading(bool loading)
{
    m_judgeLoading = loading;
    const bool custom = GoalHttpJudge::isCustomApiAgent(
        m_agentCombo->currentData().toString());
    if (!custom || !m_customApi)
        return;
    m_customApi->setLoading(loading);
}
