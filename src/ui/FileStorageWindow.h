#ifndef FILESTORAGEWINDOW_H
#define FILESTORAGEWINDOW_H

#include "FramelessDialog.h"
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>
#include <QPoint>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QMouseEvent;
class QPushButton;

class FileStorageWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit FileStorageWindow(QWidget* parent = nullptr);
    void setCurrentCategory(int catId) { m_categoryId = catId; }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onSelectItems();

private:
    void initUI();
    void processStorage(const QStringList& paths);
    void storeFile(const QString& path);
    void storeFolder(const QString& path);
    void storeArchive(const QStringList& paths);

    QString getStorageRoot(); // 获取存储根目录
    QString getUniqueFilePath(const QString& dirPath, const QString& fileName); // 获取不重复的文件名
    bool copyRecursively(const QString& srcStr, const QString& dstStr); // 递归复制文件夹

    QPushButton* m_dropHint;
    QListWidget* m_statusList;
    QPoint m_dragPos;
    int m_categoryId = -1;
};

#endif // FILESTORAGEWINDOW_H
