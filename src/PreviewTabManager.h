#ifndef PREVIEWTABMANAGER_H
#define PREVIEWTABMANAGER_H

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <functional>

#include "PreviewContentWidget.h"
#include "DockWidget.h"

class ScintillaNext;
class NotepadNextApplication;
class DockedEditor;

class PreviewTabManager : public QObject
{
    Q_OBJECT

public:
    struct TypeRegistration {
        QStringList extensions;
        QString iconPath;
        std::function<PreviewContentWidget *(QWidget *parent)> factory;
    };

    explicit PreviewTabManager(NotepadNextApplication *app, DockedEditor *dockedEditor, QObject *parent = nullptr);

    void registerType(const QString &typeId, const TypeRegistration &reg);
    bool canPreview(const QString &filePath) const;
    void openOrFocusPreview(ScintillaNext *sourceEditor);
    void openPreviewFromFile(const QString &filePath);
    void closePreview(ScintillaNext *sourceEditor);
    PreviewContentWidget *previewForEditor(ScintillaNext *sourceEditor) const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void previewOpened(PreviewContentWidget *preview);
    void previewClosed(PreviewContentWidget *preview);

private:
    struct PreviewEntry {
        QPointer<PreviewContentWidget> widget;
        QPointer<ads::CDockWidget> dockWidget;
        QString iconPath;
        QTimer *debounceTimer = nullptr;
    };

    const TypeRegistration *findRegistration(const QString &filePath) const;
    void scheduleRender(ScintillaNext *sourceEditor);
    void performRender(ScintillaNext *sourceEditor);
    void syncScroll(ScintillaNext *sourceEditor);
    int debounceMs(int docLength) const;
    void pinTransientPreviewTab();

    NotepadNextApplication *m_app;
    DockedEditor *m_dockedEditor;
    QHash<QString, TypeRegistration> m_registry;
    QHash<ScintillaNext *, PreviewEntry> m_previews;
    QHash<QString, QPointer<ads::CDockWidget>> m_previewsByPath;
    QPointer<ads::CDockWidget> m_transientPreviewTab;
};

#endif // PREVIEWTABMANAGER_H
