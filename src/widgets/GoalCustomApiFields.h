#ifndef GOAL_CUSTOM_API_FIELDS_H
#define GOAL_CUSTOM_API_FIELDS_H

#include <QWidget>

class ApplicationSettings;
class QLabel;
class QLineEdit;

class GoalCustomApiFields : public QWidget
{
    Q_OBJECT

public:
    explicit GoalCustomApiFields(QWidget *parent = nullptr);

    void setSettings(ApplicationSettings *settings);
    void loadFromSettings();
    void persistPending();
    QString validationError() const;
    void setLoading(bool loading);
    void refreshStatus();

private:
    void persistConfig();
    void persistKey();

    ApplicationSettings *m_settings = nullptr;
    QLineEdit *m_baseUrlEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLineEdit *m_modelEdit = nullptr;
    QLabel *m_status = nullptr;
    bool m_keyStored = false;
    bool m_loading = false;
};

#endif // GOAL_CUSTOM_API_FIELDS_H
