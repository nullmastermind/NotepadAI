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

#include "AcpAgentSettingsDialog.h"
#include "ui_AcpAgentSettingsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

#include "AcpAgentBrand.h"
#include "AcpAgentIcons.h"
#include "AcpAgentRegistry.h"
#include "ApplicationSettings.h"
#include "GoalAgentSettings.h"


namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kSubtitleRole = Qt::UserRole + 1;
constexpr int kBadgesRole = Qt::UserRole + 2;

// Row geometry follows the 4 / 6 / 8 px rhythm in openspec/ui-dna.md.
constexpr int kRowPadH = 8;
constexpr int kRowPadV = 6;
constexpr int kRowIconSize = 24;
constexpr int kIconTextGap = 8;
constexpr int kLineGap = 2;
constexpr int kBadgeGap = 4;
constexpr int kBadgePadH = 6;
constexpr qreal kBadgeRadius = 4.0;
constexpr int kMinNameWidth = 48;

// Data value of the "Custom image…" icon choice; never a brand key or a path.
constexpr char kCustomIconData[] = "\x01" "custom";

QString joinArgsForDisplay(const QStringList &args)
{
    return args.join(QLatin1Char(' '));
}

QStringList splitArgsFromInput(const QString &text)
{
    return parseShellLikeArgs(text);
}

QString joinArgsForEdit(const QStringList &args)
{
    return args.join(QLatin1Char('\n'));
}

QString brandDisplayName(const QString &key)
{
    for (const AcpAgentBrandInfo &brand : acpAgentBrands()) {
        if (key == QLatin1String(brand.key)) {
            return QString::fromLatin1(brand.displayName);
        }
    }
    return QString();
}

QFont scaledFont(const QFont &base, qreal factor)
{
    QFont f = base;
    if (base.pointSizeF() > 0) {
        f.setPointSizeF(base.pointSizeF() * factor);
    } else if (base.pixelSize() > 0) {
        f.setPixelSize(std::max(1, qRound(base.pixelSize() * factor)));
    }
    return f;
}

// Two-line agent row: icon, name with "Default" / "Built-in" badges, and the
// command line underneath in the muted text color. Selection, hover and
// focus come from the style so the row matches every other item view.
class AgentItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        const QFontMetrics fm(option.font);
        const int textHeight = fm.height() * 2 + kLineGap;
        return QSize(kRowIconSize + kIconTextGap + fm.averageCharWidth() * 16,
                     std::max(kRowIconSize, textHeight) + 2 * kRowPadV);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();

        const QString name = opt.text;
        const QIcon icon = opt.icon;
        opt.text.clear();
        opt.icon = QIcon();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        const bool enabled = opt.state.testFlag(QStyle::State_Enabled);
        const bool selected = opt.state.testFlag(QStyle::State_Selected);
        const QPalette::ColorGroup group = !enabled ? QPalette::Disabled
            : opt.state.testFlag(QStyle::State_Active) ? QPalette::Normal : QPalette::Inactive;
        const QColor textColor = opt.palette.color(group,
            selected ? QPalette::HighlightedText : QPalette::Text);
        QColor mutedColor = opt.palette.color(group, QPalette::PlaceholderText);
        QColor borderColor = opt.palette.color(group, QPalette::Mid);
        if (selected) {
            mutedColor = textColor;
            mutedColor.setAlphaF(0.75F);
            borderColor = mutedColor;
        }

        const QRect content = opt.rect.adjusted(kRowPadH, kRowPadV, -kRowPadH, -kRowPadV);
        const QRect iconRect(content.left(), content.center().y() - kRowIconSize / 2 + 1,
                             kRowIconSize, kRowIconSize);
        icon.paint(painter, iconRect, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);

        const QFontMetrics fm(opt.font);
        const int lineHeight = fm.height();
        const int textLeft = iconRect.right() + 1 + kIconTextGap;
        const int textWidth = content.right() - textLeft + 1;
        if (textWidth <= 0) {
            return;
        }
        const int blockTop = content.top() + (content.height() - (lineHeight * 2 + kLineGap)) / 2;
        QRect nameLine(textLeft, blockTop, textWidth, lineHeight);
        const QRect subLine(textLeft, blockTop + lineHeight + kLineGap, textWidth, lineHeight);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // Badges hug the right edge of the name line and give up their space
        // before the name does.
        const QStringList badges = index.data(kBadgesRole).toStringList();
        if (!badges.isEmpty()) {
            const QFont badgeFont = scaledFont(opt.font, 0.85);
            const QFontMetrics bfm(badgeFont);
            painter->setFont(badgeFont);
            int right = nameLine.right();
            for (auto it = badges.crbegin(); it != badges.crend(); ++it) {
                const int w = bfm.horizontalAdvance(*it) + 2 * kBadgePadH;
                const int h = bfm.height() + 2;
                const QRect r(right - w + 1, nameLine.center().y() - h / 2 + 1, w, h);
                if (r.left() < nameLine.left() + kMinNameWidth) {
                    break;
                }
                painter->setPen(QPen(borderColor, 1.0));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5),
                                         kBadgeRadius, kBadgeRadius);
                painter->setPen(mutedColor);
                painter->drawText(r, Qt::AlignCenter, *it);
                right = r.left() - kBadgeGap;
                nameLine.setRight(right - kBadgeGap);
            }
        }

        QFont nameFont = opt.font;
        nameFont.setWeight(QFont::DemiBold);
        painter->setFont(nameFont);
        painter->setPen(textColor);
        painter->drawText(nameLine, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, nameLine.width()));

        painter->setFont(opt.font);
        painter->setPen(mutedColor);
        painter->drawText(subLine, Qt::AlignLeft | Qt::AlignVCenter,
            fm.elidedText(index.data(kSubtitleRole).toString(), Qt::ElideRight, subLine.width()));

        painter->restore();
    }
};

// Add / edit form for one agent definition. Writes back into `def` on accept.
class EditAgentDialog : public QDialog
{
public:
    EditAgentDialog(QWidget *parent, AcpAgentDefinition *def, const QString &title)
        : QDialog(parent)
        , m_def(def)
    {
        setWindowTitle(title);

        m_nameEdit = new QLineEdit(def->name, this);
        m_nameEdit->setPlaceholderText(tr("e.g. Claude Code"));

        m_iconCombo = new QComboBox(this);
        m_iconCombo->addItem(tr("Automatic"), QString());
        for (const AcpAgentBrandInfo &brand : acpAgentBrands()) {
            const QString key = QLatin1String(brand.key);
            m_iconCombo->addItem(acpAgentBrandIcon(key, palette()),
                                 QString::fromLatin1(brand.displayName), key);
        }
        m_iconCombo->insertSeparator(m_iconCombo->count());
        m_iconCombo->addItem(tr("Custom image…"), QString::fromLatin1(kCustomIconData));
        m_customIconIndex = m_iconCombo->count() - 1;

        m_iconPathEdit = new QLineEdit(this);
        m_iconPathEdit->setPlaceholderText(tr("Path to an SVG, PNG or ICO file"));
        auto *browseButton = new QPushButton(tr("Browse…"), this);
        browseButton->setAutoDefault(false);
        m_iconPathRow = new QWidget(this);
        auto *pathLayout = new QHBoxLayout(m_iconPathRow);
        pathLayout->setContentsMargins(0, 0, 0, 0);
        pathLayout->setSpacing(4);
        pathLayout->addWidget(m_iconPathEdit, 1);
        pathLayout->addWidget(browseButton);

        m_commandEdit = new QLineEdit(def->command, this);
        m_commandEdit->setPlaceholderText(tr("e.g. npx"));

        const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        m_argsEdit = new QPlainTextEdit(joinArgsForEdit(def->args), this);
        m_argsEdit->setFont(mono);
        m_argsEdit->setTabChangesFocus(true);
        m_argsEdit->setPlaceholderText(tr("Whitespace-separated; quote values with spaces"));
        setMinimumLines(m_argsEdit, 3);

        QString envText;
        for (auto it = def->env.constBegin(); it != def->env.constEnd(); ++it) {
            envText += it.key() + QLatin1Char('=') + it.value() + QLatin1Char('\n');
        }
        m_envEdit = new QPlainTextEdit(envText, this);
        m_envEdit->setFont(mono);
        m_envEdit->setTabChangesFocus(true);
        m_envEdit->setPlaceholderText(tr("KEY=VALUE, one per line"));
        setMinimumLines(m_envEdit, 4);

        m_form = new QFormLayout;
        m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        m_form->setHorizontalSpacing(8);
        m_form->setVerticalSpacing(6);
        m_form->addRow(tr("&Name:"), m_nameEdit);
        m_form->addRow(tr("&Icon:"), m_iconCombo);
        m_form->addRow(QString(), m_iconPathRow);
        m_form->addRow(tr("&Command:"), m_commandEdit);
        m_form->addRow(tr("&Arguments:"), m_argsEdit);
        m_form->addRow(tr("&Environment:"), m_envEdit);

        m_interactiveCheck = new QCheckBox(tr("Question forms (grill_me)"), this);
        m_interactiveCheck->setChecked(def->interactiveQuestions);
        m_interactiveCheck->setToolTip(tr("Let the agent put question rounds (grilling, brainstorming, plan reviews) to you as forms "
                                          "in the chat through the grill_me tool. Applies to new sessions."));
        m_form->addRow(QString(), m_interactiveCheck);

        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto *root = new QVBoxLayout(this);
        root->setSpacing(8);
        root->addLayout(m_form, 1);
        root->addWidget(buttons);

        const QString icon = def->icon.trimmed();
        if (icon.isEmpty()) {
            m_iconCombo->setCurrentIndex(0);
        } else if (isAcpAgentBrandKey(icon)) {
            m_iconCombo->setCurrentIndex(m_iconCombo->findData(icon));
        } else {
            m_iconPathEdit->setText(icon);
            m_iconCombo->setCurrentIndex(m_customIconIndex);
        }

        connect(m_iconCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this]() { updateIconPathRow(); });
        connect(browseButton, &QPushButton::clicked, this, [this]() { browseIcon(); });
        connect(m_iconPathEdit, &QLineEdit::textChanged, this, [this]() { updateCustomPreview(); });
        connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() { updateAutomaticItem(); });
        connect(m_commandEdit, &QLineEdit::textChanged, this, [this]() { updateAutomaticItem(); });
        connect(m_argsEdit, &QPlainTextEdit::textChanged, this, [this]() { updateAutomaticItem(); });

        updateAutomaticItem();
        updateCustomPreview();
        updateIconPathRow();

        setMinimumWidth(520);
        resize(560, minimumSizeHint().height());
    }

    void accept() override
    {
        const QString name = m_nameEdit->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), tr("Enter a name for the agent."));
            m_nameEdit->setFocus();
            return;
        }
        const QString command = m_commandEdit->text().trimmed();
        if (command.isEmpty()) {
            QMessageBox::warning(this, windowTitle(),
                tr("Enter the command that starts the agent."));
            m_commandEdit->setFocus();
            return;
        }

        QString iconValue = m_iconCombo->currentData().toString();
        if (isCustomIconSelected()) {
            iconValue = m_iconPathEdit->text().trimmed();
            if (iconValue.isEmpty() || !QFileInfo(iconValue).isFile()) {
                QMessageBox::warning(this, windowTitle(),
                    tr("Choose an existing image file for the custom icon."));
                m_iconPathEdit->setFocus();
                return;
            }
        }

        m_def->name = name;
        m_def->command = command;
        m_def->args = splitArgsFromInput(m_argsEdit->toPlainText());
        m_def->icon = iconValue;
        m_def->interactiveQuestions = m_interactiveCheck->isChecked();

        m_def->env.clear();
        const QStringList envLines = m_envEdit->toPlainText().split(QLatin1Char('\n'));
        for (const QString &line : envLines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            const int eq = trimmed.indexOf(QLatin1Char('='));
            if (eq <= 0) {
                continue;
            }
            const QString key = trimmed.left(eq).trimmed();
            const QString val = trimmed.mid(eq + 1);
            if (!key.isEmpty()) {
                m_def->env.insert(key, val);
            }
        }

        QDialog::accept();
    }

private:
    static void setMinimumLines(QPlainTextEdit *edit, int lines)
    {
        const QFontMetrics fm(edit->font());
        const int frame = 2 * edit->frameWidth();
        const int margins = qRound(2 * edit->document()->documentMargin());
        edit->setMinimumHeight(fm.lineSpacing() * lines + frame + margins);
    }

    bool isCustomIconSelected() const
    {
        return m_iconCombo->currentIndex() == m_customIconIndex;
    }

    void updateIconPathRow()
    {
        m_form->setRowVisible(m_iconPathRow, isCustomIconSelected());
    }

    // The "Automatic" choice names and shows the brand detected from the
    // fields as they are typed, so the user sees what they will get.
    void updateAutomaticItem()
    {
        AcpAgentDefinition probe;
        probe.id = m_def->id;
        probe.name = m_nameEdit->text();
        probe.command = m_commandEdit->text();
        probe.args = splitArgsFromInput(m_argsEdit->toPlainText());
        const QString key = detectAcpAgentBrand(probe);
        const QString brand = brandDisplayName(key);
        m_iconCombo->setItemText(0, brand.isEmpty() ? tr("Automatic")
                                                    : tr("Automatic (%1)").arg(brand));
        m_iconCombo->setItemIcon(0, acpAgentBrandIcon(key, palette()));
    }

    void updateCustomPreview()
    {
        const QString path = m_iconPathEdit->text().trimmed();
        const bool exists = !path.isEmpty() && QFileInfo(path).isFile();
        m_iconCombo->setItemIcon(m_customIconIndex, exists ? QIcon(path) : QIcon());
    }

    void browseIcon()
    {
        const QString current = m_iconPathEdit->text().trimmed();
        const QString startDir = current.isEmpty() ? QString() : QFileInfo(current).absolutePath();
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose agent icon"), startDir,
            tr("Images (*.svg *.png *.ico *.jpg *.jpeg *.bmp)"));
        if (!path.isEmpty()) {
            m_iconPathEdit->setText(path);
        }
    }

    AcpAgentDefinition *m_def;
    QFormLayout *m_form = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_iconCombo = nullptr;
    QCheckBox *m_interactiveCheck = nullptr;
    QWidget *m_iconPathRow = nullptr;
    QLineEdit *m_iconPathEdit = nullptr;
    QLineEdit *m_commandEdit = nullptr;
    QPlainTextEdit *m_argsEdit = nullptr;
    QPlainTextEdit *m_envEdit = nullptr;
    int m_customIconIndex = -1;
};

} // namespace


AcpAgentSettingsDialog::AcpAgentSettingsDialog(AcpAgentRegistry *registry,
                                               ApplicationSettings *appSettings,
                                               QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AcpAgentSettingsDialog)
    , m_registry(registry)
    , m_appSettings(appSettings)
{
    ui->setupUi(this);

    QListWidget *list = ui->agentList;
    list->setItemDelegate(new AgentItemDelegate(list));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setDropIndicatorShown(true);

    ui->autoApproveCombo->addItem(tr("Manual — ask before each action"),
        QStringLiteral("manual"));
    ui->autoApproveCombo->addItem(tr("Allow All — auto-approve everything"),
        QStringLiteral("allowAll"));

    ui->cloneButton->setToolTip(tr("Duplicate the selected agent (Ctrl+D)"));
    ui->removeButton->setToolTip(tr("Remove the selected agent (Delete)"));
    ui->moveUpButton->setToolTip(tr("Move the selected agent up (Alt+Up)"));
    ui->moveDownButton->setToolTip(tr("Move the selected agent down (Alt+Down)"));

    connect(ui->addButton, &QPushButton::clicked, this, &AcpAgentSettingsDialog::onAdd);
    connect(ui->editButton, &QPushButton::clicked, this, &AcpAgentSettingsDialog::onEdit);
    connect(ui->cloneButton, &QPushButton::clicked, this, &AcpAgentSettingsDialog::onClone);
    connect(ui->removeButton, &QPushButton::clicked, this, &AcpAgentSettingsDialog::onRemove);
    connect(ui->moveUpButton, &QPushButton::clicked, this, &AcpAgentSettingsDialog::onMoveUp);
    connect(ui->moveDownButton, &QPushButton::clicked, this, &AcpAgentSettingsDialog::onMoveDown);
    connect(list, &QListWidget::itemSelectionChanged,
        this, &AcpAgentSettingsDialog::onSelectionChanged);
    connect(list, &QListWidget::itemActivated, this, [this]() { onEdit(); });
    connect(list, &QWidget::customContextMenuRequested,
        this, &AcpAgentSettingsDialog::onAgentContextMenu);
    connect(list->model(), &QAbstractItemModel::rowsMoved, this,
        [this](const QModelIndex &, int start, int, const QModelIndex &, int row) {
            onAgentRowsMoved(start, row);
        });

    const auto addListShortcut = [this, list](const QKeySequence &seq,
                                              void (AcpAgentSettingsDialog::*slot)()) {
        auto *shortcut = new QShortcut(seq, list);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, slot);
    };
    addListShortcut(QKeySequence::Delete, &AcpAgentSettingsDialog::onRemove);
    addListShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), &AcpAgentSettingsDialog::onClone);
    addListShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), &AcpAgentSettingsDialog::onMoveUp);
    addListShortcut(QKeySequence(Qt::ALT | Qt::Key_Down), &AcpAgentSettingsDialog::onMoveDown);

    connect(ui->defaultAgentCombo, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &AcpAgentSettingsDialog::onDefaultAgentIndexChanged);
    connect(ui->autoApproveCombo, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &AcpAgentSettingsDialog::onAutoApproveIndexChanged);

    if (m_registry) {
        connect(m_registry, &AcpAgentRegistry::changed,
            this, &AcpAgentSettingsDialog::refreshAgentList);
        connect(m_registry, &AcpAgentRegistry::changed,
            this, &AcpAgentSettingsDialog::refreshDefaultAgentCombo);
        connect(m_registry, &AcpAgentRegistry::changed,
            this, &AcpAgentSettingsDialog::refreshGoalAgentCombo);
        // Only the list's "Default" badge: the combo is what changed it.
        connect(m_registry, &AcpAgentRegistry::defaultAgentIdChanged,
            this, &AcpAgentSettingsDialog::refreshAgentList);
    }

    refreshAgentList();
    refreshDefaultAgentCombo();
    refreshAutoApproveCombo();
    onSelectionChanged();

    buildGoalSection();
    loadGoalSettings();

    // Enter on the agent list edits the row; it must not also press a
    // dialog button that kept the default after being clicked.
    const QList<QPushButton *> buttons = findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        button->setAutoDefault(false);
    }

    applyMutedPalette();
    m_ready = true;
}

AcpAgentSettingsDialog::~AcpAgentSettingsDialog()
{
    delete ui;
}

void AcpAgentSettingsDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (!m_ready) {
        return;
    }
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        // Monochrome marks are tinted per palette and OpenCode swaps variants.
        applyMutedPalette();
        refreshAgentList();
        refreshDefaultAgentCombo();
        refreshGoalAgentCombo();
    }
}

void AcpAgentSettingsDialog::applyMutedPalette()
{
    const QColor muted = palette().color(QPalette::PlaceholderText);
    for (QLabel *label : {ui->agentsHintLabel, ui->goalHintLabel}) {
        QPalette pal = label->palette();
        pal.setColor(QPalette::WindowText, muted);
        label->setPalette(pal);
    }
}

void AcpAgentSettingsDialog::refreshAgentList()
{
    if (!m_registry) {
        return;
    }
    const QString previousSelection = selectedAgentId();
    const QString defaultId = m_registry->defaultAgentId();
    QListWidget *list = ui->agentList;
    const QPalette pal = list->palette();

    list->blockSignals(true);
    list->clear();
    const QList<AcpAgentDefinition> agents = m_registry->agents();
    int restoreRow = -1;
    for (int i = 0; i < agents.size(); ++i) {
        const AcpAgentDefinition &def = agents.at(i);
        QString commandLine = def.command;
        if (!def.args.isEmpty()) {
            commandLine += QLatin1Char(' ');
            commandLine += joinArgsForDisplay(def.args);
        }

        QStringList badges;
        if (def.id == defaultId) {
            badges << tr("Default");
        }
        if (def.builtin) {
            badges << tr("Built-in");
        }

        auto *item = new QListWidgetItem(acpAgentIcon(def, pal), def.name);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
        item->setData(kIdRole, def.id);
        item->setData(kSubtitleRole, commandLine);
        item->setData(kBadgesRole, badges);
        item->setData(Qt::AccessibleDescriptionRole, commandLine);
        item->setToolTip(commandLine);
        list->addItem(item);

        if (def.id == previousSelection) {
            restoreRow = i;
        }
    }
    list->blockSignals(false);

    if (restoreRow < 0 && list->count() > 0) {
        restoreRow = 0;
    }
    if (restoreRow >= 0) {
        list->setCurrentRow(restoreRow);
    }
    onSelectionChanged();
}

void AcpAgentSettingsDialog::refreshDefaultAgentCombo()
{
    if (!m_registry) {
        return;
    }
    const QPalette pal = ui->defaultAgentCombo->palette();
    ui->defaultAgentCombo->blockSignals(true);
    ui->defaultAgentCombo->clear();
    const QList<AcpAgentDefinition> agents = m_registry->agents();
    for (const AcpAgentDefinition &def : agents) {
        ui->defaultAgentCombo->addItem(acpAgentIcon(def, pal), def.name, def.id);
    }
    const QString current = m_registry->defaultAgentId();
    const int idx = ui->defaultAgentCombo->findData(current);
    if (idx >= 0) {
        ui->defaultAgentCombo->setCurrentIndex(idx);
    }
    ui->defaultAgentCombo->blockSignals(false);
}

void AcpAgentSettingsDialog::refreshAutoApproveCombo()
{
    if (!m_registry) {
        return;
    }
    ui->autoApproveCombo->blockSignals(true);
    const QString policy = m_registry->autoApprovePolicy();
    const int idx = ui->autoApproveCombo->findData(policy);
    ui->autoApproveCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    ui->autoApproveCombo->blockSignals(false);
}

int AcpAgentSettingsDialog::selectedRow() const
{
    const QList<QListWidgetItem *> selected = ui->agentList->selectedItems();
    return selected.isEmpty() ? -1 : ui->agentList->row(selected.first());
}

QString AcpAgentSettingsDialog::selectedAgentId() const
{
    const QList<QListWidgetItem *> selected = ui->agentList->selectedItems();
    if (selected.isEmpty()) {
        return QString();
    }
    return selected.first()->data(kIdRole).toString();
}

void AcpAgentSettingsDialog::selectAgent(const QString &id)
{
    QListWidget *list = ui->agentList;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(kIdRole).toString() == id) {
            list->setCurrentRow(i);
            list->scrollToItem(list->item(i));
            return;
        }
    }
}

void AcpAgentSettingsDialog::onSelectionChanged()
{
    const int row = selectedRow();
    const bool hasSelection = row >= 0;
    ui->editButton->setEnabled(hasSelection);
    ui->cloneButton->setEnabled(hasSelection);
    ui->removeButton->setEnabled(hasSelection);
    ui->moveUpButton->setEnabled(hasSelection && row > 0);
    ui->moveDownButton->setEnabled(hasSelection && row < ui->agentList->count() - 1);
}

void AcpAgentSettingsDialog::onAdd()
{
    if (!m_registry) {
        return;
    }
    AcpAgentDefinition def;
    def.id = QStringLiteral("user:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    def.builtin = false;

    EditAgentDialog edit(this, &def, tr("Add agent"));
    if (edit.exec() != QDialog::Accepted) {
        return;
    }
    if (!m_registry->addAgent(def)) {
        QMessageBox::warning(this, tr("Add agent failed"),
            tr("Could not add the agent. The id may already exist."));
        return;
    }
    selectAgent(def.id);
}

void AcpAgentSettingsDialog::onEdit()
{
    if (!m_registry) {
        return;
    }
    const QString id = selectedAgentId();
    if (id.isEmpty()) {
        return;
    }
    AcpAgentDefinition def = m_registry->agent(id);
    if (def.id.isEmpty()) {
        return;
    }

    EditAgentDialog edit(this, &def, tr("Edit agent"));
    if (edit.exec() != QDialog::Accepted) {
        return;
    }
    if (!m_registry->updateAgent(def)) {
        QMessageBox::warning(this, tr("Update agent failed"),
            tr("Could not update the agent."));
    }
}

void AcpAgentSettingsDialog::onClone()
{
    if (!m_registry) {
        return;
    }
    const QString id = selectedAgentId();
    if (id.isEmpty()) {
        return;
    }
    const QString cloneId = m_registry->cloneAgent(id);
    if (!cloneId.isEmpty()) {
        selectAgent(cloneId);
    }
}

void AcpAgentSettingsDialog::onRemove()
{
    if (!m_registry) {
        return;
    }
    const QString id = selectedAgentId();
    if (id.isEmpty()) {
        return;
    }
    const AcpAgentDefinition def = m_registry->agent(id);
    if (def.id.isEmpty()) {
        return;
    }
    const auto answer = QMessageBox::question(this, tr("Remove agent"),
        tr("Remove agent \"%1\"?").arg(def.name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_registry->removeAgent(id);
}

void AcpAgentSettingsDialog::moveSelectedBy(int delta)
{
    if (!m_registry) {
        return;
    }
    const int row = selectedRow();
    if (row < 0) {
        return;
    }
    // Selection follows the id through the refresh the move triggers.
    m_registry->moveAgent(selectedAgentId(), row + delta);
}

void AcpAgentSettingsDialog::onMoveUp()
{
    moveSelectedBy(-1);
}

void AcpAgentSettingsDialog::onMoveDown()
{
    moveSelectedBy(1);
}

void AcpAgentSettingsDialog::onAgentRowsMoved(int start, int destinationRow)
{
    if (!m_registry) {
        return;
    }
    // `destinationRow` is the pre-move insertion point, so an item moved
    // down now sits one row above it.
    const int newRow = destinationRow > start ? destinationRow - 1 : destinationRow;
    const QListWidgetItem *item = ui->agentList->item(newRow);
    const QString id = item ? item->data(kIdRole).toString() : QString();
    // This runs inside QListWidget::dropEvent; commit after it returns so the
    // registry refresh never rebuilds the list mid-drop.
    QTimer::singleShot(0, this, [this, id, newRow]() {
        if (!m_registry || !m_registry->moveAgent(id, newRow)) {
            refreshAgentList();
        }
    });
}

void AcpAgentSettingsDialog::onAgentContextMenu(const QPoint &pos)
{
    QListWidget *list = ui->agentList;
    QListWidgetItem *item = list->itemAt(pos);
    if (item) {
        list->setCurrentItem(item);
    }
    const int row = selectedRow();

    QMenu menu(this);
    if (item && row >= 0) {
        menu.addAction(tr("Edit…"), this, &AcpAgentSettingsDialog::onEdit);
        menu.addAction(tr("Clone"), this, &AcpAgentSettingsDialog::onClone)
            ->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));

        const QString id = item->data(kIdRole).toString();
        QAction *setDefault = menu.addAction(tr("Set as default"), this, [this, id]() {
            const int idx = ui->defaultAgentCombo->findData(id);
            if (idx >= 0) {
                ui->defaultAgentCombo->setCurrentIndex(idx);
            }
        });
        setDefault->setEnabled(m_registry && m_registry->defaultAgentId() != id);

        menu.addSeparator();
        QAction *up = menu.addAction(tr("Move up"), this, &AcpAgentSettingsDialog::onMoveUp);
        up->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
        up->setEnabled(row > 0);
        QAction *down = menu.addAction(tr("Move down"), this, &AcpAgentSettingsDialog::onMoveDown);
        down->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
        down->setEnabled(row < list->count() - 1);
        menu.addSeparator();
        menu.addAction(tr("Remove"), this, &AcpAgentSettingsDialog::onRemove)
            ->setShortcut(QKeySequence(QKeySequence::Delete));
    } else {
        menu.addAction(tr("Add…"), this, &AcpAgentSettingsDialog::onAdd);
    }
    menu.exec(list->viewport()->mapToGlobal(pos));
}

void AcpAgentSettingsDialog::onDefaultAgentIndexChanged(int index)
{
    if (!m_registry || index < 0) {
        return;
    }
    const QString id = ui->defaultAgentCombo->itemData(index).toString();
    if (!id.isEmpty()) {
        m_registry->setDefaultAgentId(id);
    }
}

void AcpAgentSettingsDialog::onAutoApproveIndexChanged(int index)
{
    if (!m_registry || index < 0) {
        return;
    }
    const QString policy = ui->autoApproveCombo->itemData(index).toString();
    if (!policy.isEmpty()) {
        m_registry->setAutoApprovePolicy(policy);
    }
}

// --- Goal Agent Settings Section ---

void AcpAgentSettingsDialog::buildGoalSection()
{
    if (!m_appSettings) {
        // No settings to persist into: the tab would be inert.
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->goalTab));
        return;
    }

    m_goalPage = ui->goalTab;
    auto *goalLayout = new QFormLayout;
    goalLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    goalLayout->setHorizontalSpacing(8);
    goalLayout->setVerticalSpacing(6);

    m_goalAgentCombo = new QComboBox(m_goalPage);
    m_goalAgentCombo->setObjectName(QStringLiteral("goalAgentCombo"));
    goalLayout->addRow(tr("Goal agent:"), m_goalAgentCombo);

    m_goalMaxIterSpin = new QSpinBox(m_goalPage);
    m_goalMaxIterSpin->setObjectName(QStringLiteral("goalMaxIterationsSpin"));
    m_goalMaxIterSpin->setRange(GoalAgentSettings::kMaxIterationsMin,
                                GoalAgentSettings::kMaxIterationsMax);
    goalLayout->addRow(tr("Default max iterations:"), m_goalMaxIterSpin);

    auto *tplRow = new QHBoxLayout();
    tplRow->setSpacing(4);
    m_goalTemplateCombo = new QComboBox(m_goalPage);
    m_goalTemplateCombo->setObjectName(QStringLiteral("goalTemplateCombo"));
    m_goalTemplateCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *tplNewBtn = new QPushButton(tr("New…"), m_goalPage);
    tplNewBtn->setObjectName(QStringLiteral("goalTemplateNewButton"));
    auto *tplDelBtn = new QPushButton(tr("Delete"), m_goalPage);
    tplDelBtn->setObjectName(QStringLiteral("goalTemplateDeleteButton"));
    tplRow->addWidget(m_goalTemplateCombo);
    tplRow->addWidget(tplNewBtn);
    tplRow->addWidget(tplDelBtn);
    goalLayout->addRow(tr("Prompt template:"), tplRow);

    m_goalTemplateEdit = new QPlainTextEdit(m_goalPage);
    m_goalTemplateEdit->setObjectName(QStringLiteral("goalTemplateEdit"));
    m_goalTemplateEdit->setMinimumHeight(120);
    m_goalTemplateEdit->setPlaceholderText(
        tr("Prompt template content. Required placeholders: "
           "{{goal}}, {{conversation}}, {{iteration}}, {{maxIterations}}, "
           "{{criterionIndex}}, {{totalCriteria}}"));
    goalLayout->addRow(tr("Content:"), m_goalTemplateEdit);

    // The template editor takes the tab's spare height.
    ui->goalTabLayout->addLayout(goalLayout, 1);

    connect(m_goalAgentCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AcpAgentSettingsDialog::onGoalAgentChanged);
    connect(m_goalMaxIterSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &AcpAgentSettingsDialog::onGoalMaxIterChanged);
    connect(m_goalTemplateCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AcpAgentSettingsDialog::onGoalTemplateChanged);
    connect(m_goalTemplateEdit, &QPlainTextEdit::textChanged,
            this, &AcpAgentSettingsDialog::onGoalTemplateContentChanged);
    connect(tplNewBtn, &QPushButton::clicked,
            this, &AcpAgentSettingsDialog::onGoalTemplateNew);
    connect(tplDelBtn, &QPushButton::clicked,
            this, &AcpAgentSettingsDialog::onGoalTemplateDelete);
}

void AcpAgentSettingsDialog::loadGoalSettings()
{
    if (!m_appSettings || !m_goalPage) return;
    m_goalLoading = true;

    const QString settingsJson = m_appSettings->get(
        "Ai/GoalAgentSettings", QString());
    GoalAgentSettings gs;
    if (!settingsJson.isEmpty()) {
        gs = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }

    // Populate goal-agent combo from registry
    refreshGoalAgentCombo();

    m_goalMaxIterSpin->setValue(gs.defaultMaxIterations);
    refreshGoalTemplateCombo();

    m_goalLoading = false;
}

void AcpAgentSettingsDialog::saveGoalSettings()
{
    if (!m_appSettings || m_goalLoading) return;

    const QString settingsJson = m_appSettings->get(
        "Ai/GoalAgentSettings", QString());
    GoalAgentSettings gs;
    if (!settingsJson.isEmpty()) {
        gs = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }

    gs.agentId = m_goalAgentCombo->currentData().toString();
    gs.defaultMaxIterations = m_goalMaxIterSpin->value();

    // Save current template content (skip builtin — it's never persisted)
    const QString tplId = m_goalTemplateCombo->currentData().toString();
    if (!tplId.isEmpty() && tplId != QLatin1String(GoalAgentSettings::kDefaultTemplateId)) {
        for (auto &tpl : gs.promptTemplates) {
            if (tpl.id == tplId) {
                tpl.content = m_goalTemplateEdit->toPlainText();
                break;
            }
        }
    }

    m_appSettings->setValue(QStringLiteral("Ai/GoalAgentSettings"),
                            QString::fromUtf8(QJsonDocument(gs.toJson()).toJson(QJsonDocument::Compact)));
}

void AcpAgentSettingsDialog::refreshGoalAgentCombo()
{
    if (!m_goalAgentCombo) return;

    // Effective selection precedence:
    //   1. id currently shown in the combo (preserves user's in-dialog choice)
    //   2. id persisted in Ai/GoalAgentSettings.agentId (preserves prior session)
    //   3. registry's default agent
    //   4. first agent in the list
    QString preferredId = m_goalAgentCombo->currentData().toString();

    QString persistedId;
    if (m_appSettings) {
        const QString settingsJson = m_appSettings->get(
            "Ai/GoalAgentSettings", QString());
        if (!settingsJson.isEmpty()) {
            const GoalAgentSettings gs = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
            persistedId = gs.agentId;
        }
    }

    const bool wasLoading = m_goalLoading;
    m_goalLoading = true;
    m_goalAgentCombo->blockSignals(true);
    m_goalAgentCombo->clear();

    if (m_registry) {
        const QPalette pal = m_goalAgentCombo->palette();
        const auto agents = m_registry->agents();
        for (const auto &a : agents) {
            m_goalAgentCombo->addItem(acpAgentIcon(a, pal), a.name, a.id);
        }
    }

    int idx = -1;
    if (!preferredId.isEmpty()) {
        idx = m_goalAgentCombo->findData(preferredId);
    }
    if (idx < 0 && !persistedId.isEmpty()) {
        idx = m_goalAgentCombo->findData(persistedId);
    }
    if (idx < 0 && m_registry) {
        idx = m_goalAgentCombo->findData(m_registry->defaultAgentId());
    }
    if (idx < 0 && m_goalAgentCombo->count() > 0) {
        idx = 0;
    }
    if (idx >= 0) {
        m_goalAgentCombo->setCurrentIndex(idx);
    }

    m_goalAgentCombo->blockSignals(false);
    m_goalLoading = wasLoading;

    // If the effective selection drifted from the persisted id (e.g. previously
    // selected agent was removed), persist the new value so external readers
    // (GoalAgent, SendWithGoalDialog) stop pointing at a missing agent.
    const QString effectiveId = m_goalAgentCombo->currentData().toString();
    if (!m_goalLoading && effectiveId != persistedId) {
        saveGoalSettings();
    }
}

void AcpAgentSettingsDialog::refreshGoalTemplateCombo()
{
    if (!m_appSettings || !m_goalTemplateCombo) return;

    const QString settingsJson = m_appSettings->get(
        "Ai/GoalAgentSettings", QString());
    GoalAgentSettings gs;
    if (!settingsJson.isEmpty()) {
        gs = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }

    m_goalTemplateCombo->blockSignals(true);
    const QString prevId = m_goalTemplateCombo->currentData().toString();
    m_goalTemplateCombo->clear();
    for (const auto &tpl : gs.promptTemplates) {
        QString label = tpl.name;
        if (tpl.id == QLatin1String(GoalAgentSettings::kDefaultTemplateId)) {
            label += tr(" (built-in)");
        }
        m_goalTemplateCombo->addItem(label, tpl.id);
    }
    int idx = m_goalTemplateCombo->findData(prevId);
    if (idx < 0) idx = 0;
    m_goalTemplateCombo->setCurrentIndex(idx);
    m_goalTemplateCombo->blockSignals(false);

    // Load content for selected template
    onGoalTemplateChanged(m_goalTemplateCombo->currentIndex());
}

void AcpAgentSettingsDialog::onGoalAgentChanged(int /*index*/)
{
    saveGoalSettings();
}

void AcpAgentSettingsDialog::onGoalMaxIterChanged(int /*value*/)
{
    saveGoalSettings();
}

void AcpAgentSettingsDialog::onGoalTemplateChanged(int index)
{
    if (!m_appSettings || index < 0) return;

    const QString tplId = m_goalTemplateCombo->itemData(index).toString();
    const bool isBuiltin = (tplId == QLatin1String(GoalAgentSettings::kDefaultTemplateId));

    const QString settingsJson = m_appSettings->get(
        "Ai/GoalAgentSettings", QString());
    GoalAgentSettings gs;
    if (!settingsJson.isEmpty()) {
        gs = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }

    const GoalPromptTemplate *tpl = gs.findTemplate(tplId);
    m_goalTemplateEdit->blockSignals(true);
    m_goalTemplateEdit->setPlainText(tpl ? tpl->content : QString());
    m_goalTemplateEdit->setReadOnly(isBuiltin);
    m_goalTemplateEdit->blockSignals(false);
}

void AcpAgentSettingsDialog::onGoalTemplateContentChanged()
{
    saveGoalSettings();
}

void AcpAgentSettingsDialog::onGoalTemplateNew()
{
    if (!m_appSettings) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Prompt Template"),
        tr("Template name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const QString settingsJson = m_appSettings->get(
        "Ai/GoalAgentSettings", QString());
    GoalAgentSettings gs;
    if (!settingsJson.isEmpty()) {
        gs = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }

    GoalPromptTemplate tpl;
    tpl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tpl.name = name.trimmed();
    tpl.content = GoalAgentSettings::builtinPromptContent();
    gs.promptTemplates.append(tpl);

    m_appSettings->setValue(QStringLiteral("Ai/GoalAgentSettings"),
                            QString::fromUtf8(QJsonDocument(gs.toJson()).toJson(QJsonDocument::Compact)));

    m_goalLoading = true;
    refreshGoalTemplateCombo();
    int idx = m_goalTemplateCombo->findData(tpl.id);
    if (idx >= 0) m_goalTemplateCombo->setCurrentIndex(idx);
    m_goalLoading = false;
}

void AcpAgentSettingsDialog::onGoalTemplateDelete()
{
    if (!m_appSettings) return;

    const QString tplId = m_goalTemplateCombo->currentData().toString();
    if (tplId.isEmpty() || tplId == QLatin1String(GoalAgentSettings::kDefaultTemplateId)) {
        QMessageBox::information(this, tr("Delete Template"),
            tr("The built-in default template cannot be deleted."));
        return;
    }

    const auto answer = QMessageBox::question(this, tr("Delete Template"),
        tr("Delete template \"%1\"?").arg(m_goalTemplateCombo->currentText()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    const QString settingsJson = m_appSettings->get(
        "Ai/GoalAgentSettings", QString());
    GoalAgentSettings gs;
    if (!settingsJson.isEmpty()) {
        gs = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }

    gs.promptTemplates.removeIf([&](const GoalPromptTemplate &t) {
        return t.id == tplId;
    });

    m_appSettings->setValue(QStringLiteral("Ai/GoalAgentSettings"),
                            QString::fromUtf8(QJsonDocument(gs.toJson()).toJson(QJsonDocument::Compact)));

    m_goalLoading = true;
    refreshGoalTemplateCombo();
    m_goalLoading = false;
}
