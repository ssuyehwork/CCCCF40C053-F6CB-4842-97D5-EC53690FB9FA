#ifndef FILESYSTEMTREEMODEL_H
#define FILESYSTEMTREEMODEL_H

#include <QStandardItemModel>
#include "../mft/MftReader.h"

class FileSystemTreeModel : public QStandardItemModel {
    Q_OBJECT
public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        FrnRole,
        IsDirRole
    };

    explicit FileSystemTreeModel(MftReader* mft, QObject* parent = nullptr);

    // 2026-03-24 [NEW] 初始化驱动器
    void initDrives();

    // 2026-03-24 [NEW] 懒加载：展开某节点时填充其子项
    void fetchMore(const QModelIndex& parent);
    bool canFetchMore(const QModelIndex& parent) const override;

private:
    MftReader* m_mft;
    void addDrive(const QString& driveName, const QString& label);
};

#endif // FILESYSTEMTREEMODEL_H
