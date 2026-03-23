#include "FileCategoryModel.h"
#include "../ui/IconHelper.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QMimeData>

FileCategoryModel::FileCategoryModel(Type type, QObject* parent) : QStandardItemModel(parent), m_type(type) {
    refresh();
}

void FileCategoryModel::refresh() {
    clear();
    QSqlDatabase db = QSqlDatabase::database("file_index_db");
    if (!db.isOpen()) return;

    if (m_type == System) {
        auto addSystemItem = [&](const QString& name, const QString& icon, const QString& type) {
            auto* item = new QStandardItem(IconHelper::getIcon(icon, "#aaaaaa"), name);
            item->setData(type, TypeRole);
            item->setData(name, NameRole);
            appendRow(item);
        };
        addSystemItem("全部文件", "all_data", "all");
        addSystemItem("今日文件", "today", "today");
        addSystemItem("收藏文件", "bookmark", "bookmark");
    } else {
        auto* root = new QStandardItem(IconHelper::getIcon("category", "#3498db"), "文件分类");
        root->setData("root", TypeRole);
        root->setData("文件分类", NameRole);
        appendRow(root);

        QSqlQuery q(db);
        q.exec("SELECT id, name, color, is_pinned FROM file_categories ORDER BY is_pinned DESC, sort_order ASC");
        while (q.next()) {
            int id = q.value(0).toInt();
            QString name = q.value(1).toString();
            QString color = q.value(2).toString();
            bool pinned = q.value(3).toBool();

            auto* item = new QStandardItem(IconHelper::getIcon(pinned ? "pin_vertical" : "branch", color), name);
            item->setData("category", TypeRole);
            item->setData(id, IdRole);
            item->setData(name, NameRole);
            item->setData(color, ColorRole);
            item->setData(pinned, PinnedRole);
            root->appendRow(item);
        }
    }
}

QVariant FileCategoryModel::data(const QModelIndex& index, int role) const {
    return QStandardItemModel::data(index, role);
}

bool FileCategoryModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role == Qt::EditRole) {
        int id = index.data(IdRole).toInt();
        QString newName = value.toString();
        QSqlDatabase db = QSqlDatabase::database("file_index_db");
        QSqlQuery q(db);
        q.prepare("UPDATE file_categories SET name = ? WHERE id = ?");
        q.addBindValue(newName);
        q.addBindValue(id);
        if (q.exec()) {
            bool ok = QStandardItemModel::setData(index, value, role);
            if (ok) refresh();
            return ok;
        }
    }
    return QStandardItemModel::setData(index, value, role);
}

Qt::DropActions FileCategoryModel::supportedDropActions() const {
    return Qt::CopyAction | Qt::MoveAction;
}

bool FileCategoryModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
    // 2026-03-24 按照用户要求：支持物理文件/文件夹关联至虚拟分类
    if (!parent.isValid()) return false;

    QString type = parent.data(TypeRole).toString();
    if (type != "category") return false;

    int categoryId = parent.data(IdRole).toInt();
    QSqlDatabase db = QSqlDatabase::database("file_index_db");

    if (data->hasFormat("application/x-file-paths")) {
        // 自定义格式，由文件列表提供
        QStringList paths = QString::fromUtf8(data->data("application/x-file-paths")).split(";", Qt::SkipEmptyParts);
        db.transaction();
        for (const QString& path : paths) {
            QSqlQuery q(db);
            q.prepare("INSERT OR IGNORE INTO item_categories (item_path, category_id) VALUES (?, ?)");
            q.addBindValue(path);
            q.addBindValue(categoryId);
            q.exec();
        }
        db.commit();
        return true;
    }

    return false;
}
