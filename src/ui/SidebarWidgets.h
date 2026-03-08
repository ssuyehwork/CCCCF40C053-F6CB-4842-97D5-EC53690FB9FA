#ifndef SIDEBARWIDGETS_H
#define SIDEBARWIDGETS_H

#include <QListWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

/**
 * @brief 文件夹收藏侧边栏 (支持批量拖入)
 */
class FolderFavoriteListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit FolderFavoriteListWidget(QWidget* parent = nullptr) : QListWidget(parent) {
        setAcceptDrops(true);
    }
signals:
    void foldersDropped(const QStringList& paths);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
            event->acceptProposedAction();
        }
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        QStringList paths;
        // 1. 处理 URL (来自外部文件管理器)
        if (event->mimeData()->hasUrls()) {
            for (const QUrl& url : event->mimeData()->urls()) {
                QString p = url.toLocalFile();
                if (!p.isEmpty() && QDir(p).exists()) paths << p;
            }
        } 
        // 2. 处理文本 (支持多行路径)
        if (event->mimeData()->hasText()) {
            QStringList texts = event->mimeData()->text().split("\n", Qt::SkipEmptyParts);
            for (const QString& t : texts) {
                QString p = t.trimmed();
                if (!p.isEmpty() && QDir(p).exists() && !paths.contains(p)) paths << p;
            }
        }
        // 3. 处理内部拖拽 (如果有自定义数据)
        if (paths.isEmpty() && event->source()) {
            QListWidget* src = qobject_cast<QListWidget*>(event->source());
            if (src) {
                for (auto* item : src->selectedItems()) {
                    QString p = item->data(Qt::UserRole).toString();
                    if (!p.isEmpty() && QDir(p).exists() && !paths.contains(p)) paths << p;
                }
            }
        }
        
        if (!paths.isEmpty()) {
            emit foldersDropped(paths);
            event->acceptProposedAction();
        }
    }
};

/**
 * @brief 文件收藏侧边栏 (支持批量拖入)
 */
class FileFavoriteListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit FileFavoriteListWidget(QWidget* parent = nullptr) : QListWidget(parent) {
        setAcceptDrops(true);
        setSelectionMode(QAbstractItemView::ExtendedSelection);
    }
signals:
    void filesDropped(const QStringList& paths);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
            event->acceptProposedAction();
        }
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        QStringList paths;
        // 1. 处理 URL
        if (event->mimeData()->hasUrls()) {
            for (const QUrl& url : event->mimeData()->urls()) {
                QString p = url.toLocalFile();
                if (!p.isEmpty() && QFileInfo(p).isFile()) paths << p;
            }
        }
        // 2. 处理文本
        if (event->mimeData()->hasText()) {
            QStringList texts = event->mimeData()->text().split("\n", Qt::SkipEmptyParts);
            for (const QString& t : texts) {
                QString p = t.trimmed();
                if (!p.isEmpty() && QFileInfo(p).isFile() && !paths.contains(p)) paths << p;
            }
        }
        // 3. 处理内部拖拽
        if (paths.isEmpty() && event->source()) {
            QListWidget* src = qobject_cast<QListWidget*>(event->source());
            if (src) {
                for (auto* item : src->selectedItems()) {
                    QString p = item->data(Qt::UserRole).toString();
                    if (!p.isEmpty() && QFileInfo(p).isFile() && !paths.contains(p)) paths << p;
                }
            }
        }
        
        if (!paths.isEmpty()) {
            emit filesDropped(paths);
            event->acceptProposedAction();
        }
    }
};

#endif // SIDEBARWIDGETS_H
