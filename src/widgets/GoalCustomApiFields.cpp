#include "GoalCustomApiFields.h"

#include "ApplicationSettings.h"
#include "GoalAgentSettings.h"
#include "GoalHttpJudge.h"
#include "ai/CredentialStore.h"

#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QStringList>
#include <QVBoxLayout>

GoalCustomApiFields::GoalCustomApiFields(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("customApiFields"));
    auto *apiLayout = new QVBoxLayout(this);
    apiLayout->setContentsMargins(0, 0, 0, 0);
    apiLayout->setSpacing(8);

    auto *baseLabel = new QLabel(tr("Base URL"), this);
    baseLabel->setStyleSheet(QStringLiteral("font-weight: 500; font-size: 12px;"));
    apiLayout->addWidget(baseLabel);
    m_baseUrlEdit = new QLineEdit(this);
    m_baseUrlEdit->setPlaceholderText(QStringLiteral("https://api.anthropic.com"));
    connect(m_baseUrlEdit, &QLineEdit::editingFinished, this, &GoalCustomApiFields::persistConfig);
    apiLayout->addWidget(m_baseUrlEdit);

    auto *keyLabel = new QLabel(tr("API key"), this);
    keyLabel->setStyleSheet(QStringLiteral("font-weight: 500; font-size: 12px;"));
    apiLayout->addWidget(keyLabel);
    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(GoalHttpJudge::apiKeyPlaceholder(false));
    connect(m_apiKeyEdit, &QLineEdit::editingFinished, this, &GoalCustomApiFields::persistKey);
    apiLayout->addWidget(m_apiKeyEdit);

    auto *modelLabel = new QLabel(tr("Model"), this);
    modelLabel->setStyleSheet(QStringLiteral("font-weight: 500; font-size: 12px;"));
    apiLayout->addWidget(modelLabel);
    m_modelEdit = new QLineEdit(this);
    m_modelEdit->setPlaceholderText(QStringLiteral("claude-opus-5"));
    connect(m_modelEdit, &QLineEdit::editingFinished, this, &GoalCustomApiFields::persistConfig);
    apiLayout->addWidget(m_modelEdit);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("customApiStatus"));
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("font-size: 11px;"));
    apiLayout->addWidget(m_status);

    connect(m_baseUrlEdit, &QLineEdit::textChanged, this, &GoalCustomApiFields::refreshStatus);
    connect(m_apiKeyEdit, &QLineEdit::textChanged, this, &GoalCustomApiFields::refreshStatus);
    connect(m_modelEdit, &QLineEdit::textChanged, this, &GoalCustomApiFields::refreshStatus);
}

void GoalCustomApiFields::setSettings(ApplicationSettings *settings)
{
    m_settings = settings;
}

void GoalCustomApiFields::loadFromSettings()
{
    GoalAgentSettings gs;
    if (m_settings) {
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        if (!settingsJson.isEmpty()) {
            gs = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
        }
    }
    m_baseUrlEdit->setText(gs.customApiBaseUrl);
    m_modelEdit->setText(gs.customApiModel);
    ai::CredentialStore store;
    m_keyStored = !store.retrieveSecret(QLatin1String(GoalHttpJudge::kCredentialKey)).isEmpty();
    m_apiKeyEdit->setPlaceholderText(GoalHttpJudge::apiKeyPlaceholder(m_keyStored));
    refreshStatus();
}

void GoalCustomApiFields::persistPending()
{
    persistConfig();
    persistKey();
}

QString GoalCustomApiFields::validationError() const
{
    if (m_baseUrlEdit->text().trimmed().isEmpty())
        return tr("Enter a Base URL for Custom API.");
    if (!GoalHttpJudge::isUsableEndpointUrl(m_baseUrlEdit->text()))
        return tr("Enter a valid http(s) Base URL.");
    if (m_modelEdit->text().trimmed().isEmpty())
        return tr("Enter a model for Custom API.");
    if (m_apiKeyEdit->text().trimmed().isEmpty() && !m_keyStored)
        return tr("Enter an API key for Custom API.");
    return {};
}

void GoalCustomApiFields::setLoading(bool loading)
{
    m_loading = loading;
    m_baseUrlEdit->setReadOnly(loading);
    m_apiKeyEdit->setReadOnly(loading);
    m_modelEdit->setReadOnly(loading);
    if (loading) {
        m_status->setText(tr("Calling judge…"));
        m_status->setStyleSheet(QStringLiteral(
            "font-size: 11px; color: palette(placeholder-text);"));
        m_status->show();
    } else {
        refreshStatus();
    }
}

void GoalCustomApiFields::refreshStatus()
{
    if (m_loading)
        return;

    const QString url = m_baseUrlEdit->text().trimmed();
    const QString model = m_modelEdit->text().trimmed();
    const bool hasKey = !m_apiKeyEdit->text().trimmed().isEmpty() || m_keyStored;

    if (url.isEmpty() && model.isEmpty() && !hasKey) {
        m_status->setText(tr("Enter Base URL, API key, and model."));
        m_status->setStyleSheet(QStringLiteral(
            "font-size: 11px; color: palette(placeholder-text);"));
        m_status->show();
        return;
    }
    if (!url.isEmpty() && !GoalHttpJudge::isUsableEndpointUrl(url)) {
        m_status->setText(tr("Enter a valid http(s) Base URL."));
        m_status->setStyleSheet(QStringLiteral("font-size: 11px; color: red;"));
        m_status->show();
        return;
    }

    QStringList missing;
    if (url.isEmpty())
        missing.append(tr("Base URL"));
    if (!hasKey)
        missing.append(tr("API key"));
    if (model.isEmpty())
        missing.append(tr("model"));
    if (!missing.isEmpty()) {
        m_status->setText(tr("Enter %1.").arg(missing.join(QStringLiteral(", "))));
        m_status->setStyleSheet(QStringLiteral(
            "font-size: 11px; color: palette(placeholder-text);"));
        m_status->show();
        return;
    }

    m_status->clear();
    m_status->hide();
}

void GoalCustomApiFields::persistConfig()
{
    if (!m_settings)
        return;
    const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
    GoalAgentSettings gs;
    if (!settingsJson.isEmpty()) {
        gs = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }
    gs.customApiBaseUrl = m_baseUrlEdit->text().trimmed();
    gs.customApiModel = m_modelEdit->text().trimmed();
    m_settings->setValue(
        QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(gs.toJson()).toJson(QJsonDocument::Compact)));
}

void GoalCustomApiFields::persistKey()
{
    const QString key = m_apiKeyEdit->text().trimmed();
    if (key.isEmpty())
        return;
    ai::CredentialStore store;
    if (store.storeSecret(QLatin1String(GoalHttpJudge::kCredentialKey), key)) {
        m_apiKeyEdit->clear();
        m_keyStored = true;
        m_apiKeyEdit->setPlaceholderText(GoalHttpJudge::apiKeyPlaceholder(true));
        refreshStatus();
    } else {
        m_status->setText(tr("Could not store the API key in the keychain."));
        m_status->setStyleSheet(QStringLiteral("font-size: 11px; color: red;"));
        m_status->show();
    }
}
