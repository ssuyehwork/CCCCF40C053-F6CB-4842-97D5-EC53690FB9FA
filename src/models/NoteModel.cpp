#include "NoteModel.h"
#include <QIcon>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>
#include "../ui/IconHelper.h"
#include "../ui/StringUtils.h"
#include "../core/DatabaseManager.h"

NoteModel::NoteModel(QObject* parent) : QAbstractListModel(parent) {}

int NoteModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_notes.count();
}

QVariant NoteModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_notes.count()) return QVariant();

    const QVariantMap& note = m_notes.at(index.row());
    switch (role) {
        case Qt::BackgroundRole:
            return QVariant();
        case Qt::DecorationRole: {
            QString type = note.value("item_type").toString();
            QString content = note.value("content").toString().trimmed();
            QString iconName = "text";
            QString iconColor = "#95A5A6";

            if (type == "link") {
                iconName = "link";
                iconColor = "#17B345";
                return IconHelper::getIcon(iconName, iconColor, 32);
            } else if (type == "code") {
                iconName = "code";
                iconColor = "#00FF00";
                return IconHelper::getIcon(iconName, iconColor, 32);
            }

            if (type == "image") {
                int id = note.value("id").toInt();
                if (m_thumbnailCache.contains(id)) return m_thumbnailCache[id];
                
                QImage img;
                img.loadFromData(note.value("data_blob").toByteArray());
                if (!img.isNull()) {
                    if (m_thumbnailCache.size() > 100) m_thumbnailCache.clear();
                    QIcon thumb(QPixmap::fromImage(img.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                    m_thumbnailCache[id] = thumb;
                    return thumb;
                }
                iconName = "image";
                iconColor = "#FF00FF";
            } else if (type == "file" || type == "files" || type == "folder" || type == "folders") {
                if (type == "folder") {
                    iconName = "folder";
                    iconColor = "#FF8C00";
                } else if (content.contains(";")) {
                    iconName = "files_multiple";
                    iconColor = "#FF0000";
                } else {
                    iconName = "file";
                    iconColor = "#FFFF00";
                }
            } else if (type == "ocr_text") {
                iconName = "screenshot_ocr";
                iconColor = "#00FFFF";
            }
            return IconHelper::getIcon(iconName, iconColor, 32);
        }
        case Qt::ToolTipRole: {
            int id = note.value("id").toInt();
            if (m_tooltipCache.contains(id)) return m_tooltipCache[id];

            QString title = note.value("title").toString();
            QString content = note.value("content").toString();
            int catId = note.value("category_id").toInt();
            QString tags = note.value("tags").toString();
            bool pinned = note.value("is_pinned").toBool();
            bool favorite = note.value("is_favorite").toBool();
            int rating = note.value("rating").toInt();

            QString catName = m_categoryMap.value(catId, "未分类");
            if (tags.isEmpty()) tags = "无";

            QString preview;
            if (note.value("item_type").toString() == "image") {
                preview = "[图片预览]";
            } else {
                // [PERF] 极致性能优化：ToolTip 预览优先使用懒加载缓存
                QString plainText;
                if (m_plainContentCache.contains(id)) {
                    plainText = m_plainContentCache[id];
                } else {
                    plainText = StringUtils::htmlToPlainText(content).simplified();
                    if (m_plainContentCache.size() < 500) m_plainContentCache[id] = plainText;
                }
                preview = plainText.left(400).toHtmlEscaped().replace("\n", "<br>").trimmed();
            }

            QString html = QString("<html><body>"
                                   "<b>标题:</b> %1<br>"
                                   "<b>分类:</b> %2<br>"
                                   "<b>标签:</b> %3<br>"
                                   "<hr>%4"
                                   "</body></html>")
                .arg(title.toHtmlEscaped(), catName, tags, preview);
            
            if (m_tooltipCache.size() < 100) m_tooltipCache[id] = html;
            return html;
        }
        case Qt::DisplayRole: {
            // [PERF] 2026-04-xx 旧版本-3 模式：基于懒加载缓存的 DisplayRole，确保渲染零开销
            QString type = note.value("item_type").toString();
            if (type == "text" || type.isEmpty() || type == "ocr_text" || type == "file" || type == "folder") {
                int id = note.value("id").toInt();
                QString display;
                if (m_plainContentCache.contains(id)) {
                    display = m_plainContentCache[id];
                } else {
                    QString content = note.value("content").toString();
                    display = StringUtils::htmlToPlainText(content).simplified();
                    if (m_plainContentCache.size() < 1000) m_plainContentCache[id] = display;
                }
                return display.left(150);
            }
            if (type == "image") return QString("[图片]");
            return note.value("title");
        }
        case IdRole: return note.value("id");
        case TitleRole: return note.value("title");
        case ContentRole: return note.value("content");
        case TagsRole: return note.value("tags");
        case TimeRole: return note.value("updated_at");
        case PinnedRole: return note.value("is_pinned");
        case FavoriteRole: return note.value("is_favorite");
        case TypeRole: return note.value("item_type");
        case RatingRole: return note.value("rating");
        case CategoryIdRole: return note.value("category_id");
        case ColorRole: return note.value("color");
        case SourceAppRole: return note.value("source_app");
        case SourceTitleRole: return note.value("source_title");
        case BlobRole: return note.value("data_blob");
        case RemarkRole: return note.value("remark");
        case PlainContentRole: {
            int id = note.value("id").toInt();
            if (m_plainContentCache.contains(id)) return m_plainContentCache[id];
            QString content = note.value("content").toString();
            QString plain = StringUtils::htmlToPlainText(content).simplified();
            if (m_plainContentCache.size() < 1000) m_plainContentCache[id] = plain;
            return plain;
        }
        default: return QVariant();
    }
}

Qt::ItemFlags NoteModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::ItemIsEnabled;
    return QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled;
}

QStringList NoteModel::mimeTypes() const {
    return {"text/plain", "text/html", "application/x-note-ids"};
}

QMimeData* NoteModel::mimeData(const QModelIndexList& indexes) const {
    QMimeData* mimeData = new QMimeData();
    QStringList ids;
    for (const QModelIndex& index : indexes) {
        if (index.isValid()) ids << QString::number(data(index, IdRole).toInt());
    }
    mimeData->setData("application/x-note-ids", ids.join(",").toUtf8());
    return mimeData;
}

void NoteModel::setNotes(const QList<QVariantMap>& notes) {
    updateCategoryMap();
    m_thumbnailCache.clear();
    m_tooltipCache.clear();
    // [FIX] 不再执行 UI 线程全量预处理，回归旧版本-3 的按需懒加载缓存模式
    beginResetModel();
    m_notes = notes;
    endResetModel();
}

void NoteModel::prependNote(const QVariantMap& note) {
    beginInsertRows(QModelIndex(), 0, 0);
    m_notes.prepend(note);
    endInsertRows();
}

void NoteModel::updateCategoryMap() {
    auto categories = DatabaseManager::instance().getAllCategories();
    m_categoryMap.clear();
    for (const auto& cat : categories) {
        m_categoryMap[cat["id"].toInt()] = cat["name"].toString();
    }
}
