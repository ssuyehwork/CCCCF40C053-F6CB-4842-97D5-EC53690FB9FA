#ifndef METADATAPANEL_H
#define METADATAPANEL_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QTimer>
#include <QVariant>
#include <QStringList>
#include <QMap>
#include <QStackedWidget>
#include <QFrame>
#include "ClickableLineEdit.h"

class MetadataPanel : public QWidget {
    Q_OBJECT
public:
    explicit MetadataPanel(QWidget* parent = nullptr);
    void setNote(const QVariantMap& note);
    // 2026-03-24 按照用户要求：适配文件/文件夹元数据展示
    void setFile(const std::wstring& path, const QJsonObject& meta);

    friend class MainWindow;
    void setMultipleNotes(int count);
    void clearSelection();

signals:
    void noteUpdated();
    void tagAdded(const QStringList& tags);
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    QWidget* createInfoWidget(const QString& icon, const QString& title, const QString& subtitle);
    QWidget* createMetadataDisplay();
    QWidget* createCapsule(const QString& label, const QString& key);
    void handleTagInput();
    void openTagSelector();
    void refreshTags(const QString& tagsStr);
    void removeTag(const QString& tag);

    QStackedWidget* m_stack;
    QWidget* m_metadataDisplayWidget;
    
    // Metadata Display widgets
    ClickableLineEdit* m_tagEdit;
    QFrame* m_separatorLine;
    QMap<QString, QLabel*> m_capsules;
    QMap<QString, QWidget*> m_capsuleRows;
    QWidget* m_tagContainer;
    class FlowLayout* m_tagFlowLayout;
    QTextEdit* m_remarkEdit = nullptr;
    QTimer* m_remarkSaveTimer = nullptr;

    // 2026-03-24 按照用户要求：支持物理文件的星级和颜色修改
    void setFileRating(int rating);
    void setFileColor(const QString& color);

    int m_currentNoteId = -1;
    std::wstring m_currentFilePath; // 2026-03-24 记录当前选中的文件路径
};

#endif // METADATAPANEL_H
