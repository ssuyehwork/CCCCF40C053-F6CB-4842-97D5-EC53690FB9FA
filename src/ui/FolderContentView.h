#ifndef FOLDERCONTENTVIEW_H
#define FOLDERCONTENTVIEW_H

#include <QWidget>
#include <QListView>
#include <QFileSystemModel>
#include <QVBoxLayout>

class FolderContentView : public QWidget {
    Q_OBJECT
public:
    explicit FolderContentView(QWidget* parent = nullptr);
    
    // 2026-03-24 [NEW] 核心接口：设置要浏览的物理路径
    void setRootPath(const QString& path);

private:
    QListView* m_view;
    QFileSystemModel* m_model;
};

#endif // FOLDERCONTENTVIEW_H
