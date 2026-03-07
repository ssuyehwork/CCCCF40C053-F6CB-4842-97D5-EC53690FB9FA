#include "NoteModel.h"
#include <QDateTime>
#include <QIcon>
#include "../ui/IconHelper.h"
#include "../ui/StringUtils.h"
#include "../core/DatabaseManager.h"
#include <QFileInfo>
#include <QBuffer>
#include <QPixmap>
#include <QByteArray>
#include <QUrl>

NoteModel::NoteModel(QObject* parent) : QAbstractListModel(parent) {
    updateCategoryMap();
}

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
            QString iconColor = "#95a5a6";

            if (type == "image") {
                int id = note.value("id").toInt();
                if (m_thumbnailCache.contains(id)) return m_thumbnailCache[id];
                
                QImage img;
                img.loadFromData(note.value("data_blob").toByteArray());
                if (!img.isNull()) {
                    QIcon thumb(QPixmap::fromImage(img.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                    m_thumbnailCache[id] = thumb;
                    return thumb;
                }
                iconName = "image";
                iconColor = "#9b59b6";
            } else if (type == "file" || type == "files") {
                QStringList paths = content.split(';', Qt::SkipEmptyParts);
                if (paths.size() > 1) {
                    iconName = "files_multiple";
                    iconColor = "#FF4858"; // 多文件使用红色
                } else {
                    iconName = "file";
                    iconColor = "#f1c40f"; // 单文件使用黄色
                }
            } else if (type == "ocr_text") {
                iconName = "screenshot_ocr";
                iconColor = "#007ACC";
            } else if (type == "captured_message") {
                iconName = "message";
                iconColor = "#4a90e2";
            } else if (type == "local_file") {
                iconName = "file_import";
                iconColor = "#f1c40f";
            } else if (type == "local_batch") {
                iconName = "batch_import";
                iconColor = "#f1c40f";
            } else if (type == "folder") {
                iconName = "folder";
                iconColor = "#e67e22";
            } else if (type == "local_folder") {
                iconName = "folder_import";
                iconColor = "#e67e22";
            } else if (type == "deleted_category") {
                iconName = "category";
                iconColor = "#95a5a6";
            } else if (type == "color") {
                iconName = "palette";
                iconColor = content;
            } else if (type == "pixel_ruler") {
                iconName = "pixel_ruler";
                iconColor = "#ff5722";
            } else {
                QString stripped = content.trimmed();
                QString cleanPath = stripped;
                if ((cleanPath.startsWith("\"") && cleanPath.endsWith("\"")) || 
                    (cleanPath.startsWith("'") && cleanPath.endsWith("'"))) {
                    cleanPath = cleanPath.mid(1, cleanPath.length() - 2);
                }

                if (stripped.startsWith("http://") || stripped.startsWith("https://") || stripped.startsWith("www.")) {
                    iconName = "link";
                    iconColor = "#3498db";
                } else if (QRegularExpression("^#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})$").match(stripped).hasMatch()) {
                    iconName = "palette";
                    iconColor = stripped;
                } else if (stripped.startsWith("#") || stripped.startsWith("import ") || stripped.startsWith("class ") || 
                           stripped.startsWith("def ") || stripped.startsWith("<") || stripped.startsWith("{") ||
                           stripped.startsWith("function") || stripped.startsWith("var ") || stripped.startsWith("const ")) {
                    iconName = "code";
                    iconColor = "#2ecc71";
                } else if (cleanPath.length() < 260 && (
                           (cleanPath.length() > 2 && cleanPath[1] == ':') || 
                           cleanPath.startsWith("\\\\") || cleanPath.startsWith("/") || 
                           cleanPath.startsWith("./") || cleanPath.startsWith("../"))) {
                    QFileInfo info(cleanPath);
                    if (info.exists()) {
                        if (info.isDir()) {
                            iconName = "folder";
                            iconColor = "#e67e22";
                        } else {
                            iconName = "file";
                            iconColor = "#f1c40f";
                        }
                    }
                }
            }
            return IconHelper::getIcon(iconName, iconColor, 32);
        }
        case Qt::ToolTipRole: {
            int id = note.value("id").toInt();
            QString title = note.value("title").toString();
            QString content = note.value("content").toString();
            QString remark = note.value("remark").toString().trimmed();
            int catId = note.value("category_id").toInt();
            QString tags = note.value("tags").toString();
            bool pinned = note.value("is_pinned").toBool();
            bool locked = note.value("is_locked").toBool();
            bool favorite = note.value("is_favorite").toBool();
            int rating = note.value("rating").toInt();
            QString sourceApp = note.value("source_app").toString();

            QString catName = m_categoryMap.value(catId, "未分类");
            if (tags.isEmpty()) tags = "无";

            QString statusStr;
            if (pinned) statusStr += IconHelper::getIconHtml("pin_vertical", "#e74c3c") + " 置顶 ";
            if (locked) statusStr += IconHelper::getIconHtml("lock", "#aaaaaa") + " 锁定 ";
            if (favorite) statusStr += IconHelper::getIconHtml("bookmark_filled", "#ff6b81") + " 书签 ";
            if (statusStr.isEmpty()) statusStr = "无";

            if (sourceApp.isEmpty()) sourceApp = "未知应用";

            QString ratingStr;
            for(int i=0; i<rating; ++i) ratingStr += IconHelper::getIconHtml("star_filled", "#f39c12") + " ";
            if (ratingStr.isEmpty()) ratingStr = "无";

            QString preview;
            if (note.value("item_type").toString() == "image") {
                QByteArray ba = note.value("data_blob").toByteArray();
                preview = QString("<img src='data:image/png;base64,%1' width='300'>").arg(QString(ba.toBase64()));
            } else {
                QString plainText = StringUtils::htmlToPlainText(content);
                preview = plainText.left(400).toHtmlEscaped().replace("\n", "<br>").trimmed();
                if (plainText.length() > 400) preview += "...";
            }
            if (preview.isEmpty()) preview = title.toHtmlEscaped();

            QString titleHtml;
            if (!title.isEmpty()) {
                titleHtml = QString("<div style='font-size: 13px; font-weight: bold; color: #fff; "
                                    "border-bottom: 1px solid #444; padding-bottom: 5px; margin-bottom: 5px;'>%1</div>")
                                .arg(title.toHtmlEscaped());
            }

            QString remarkRow;
            if (!remark.isEmpty()) {
                remarkRow = QString("<tr><td width='22'>%1</td><td><b>备注:</b> "
                                    "<span style='color:#b3e5fc;'>%2</span></td></tr>")
                                .arg(IconHelper::getIconHtml("edit", "#4fc3f7"),
                                     remark.left(120).toHtmlEscaped().replace("\n", "<br>")
                                     + (remark.length() > 120 ? "..." : ""));
            }

            QString html = QString("<html><body style='color: #ddd;'>"
                           "%1"
                           "<table border='0' cellpadding='2' cellspacing='0'>"
                           "<tr><td width='22'>%2</td><td><b>分区:</b> %3</td></tr>"
                           "<tr><td width='22'>%4</td><td><b>标签:</b> %5</td></tr>"
                           "<tr><td width='22'>%6</td><td><b>评级:</b> %7</td></tr>"
                           "<tr><td width='22'>%8</td><td><b>状态:</b> %9</td></tr>"
                           "<tr><td width='22'>%10</td><td><b>来源:</b> %11</td></tr>"
                           "%12"
                           "</table>"
                           "<hr style='border: 0; border-top: 1px solid #555; margin: 5px 0;'>"
                           "<div style='color: #ccc; font-size: 12px; line-height: 1.4;'>%13</div>"
                           "</body></html>")
                .arg(titleHtml,
                     IconHelper::getIconHtml("branch", "#4a90e2"), catName,
                     IconHelper::getIconHtml("tag", "#FFAB91"), tags,
                     IconHelper::getIconHtml("star", "#f39c12"), ratingStr,
                     IconHelper::getIconHtml("pin_tilted", "#aaa"), statusStr,
                     IconHelper::getIconHtml("monitor", "#aaaaaa"), sourceApp,
                     remarkRow, preview);
            
            m_tooltipCache[id] = html;
            return html;
        }
        case Qt::DisplayRole: {
            QString type = note.value("item_type").toString();
            QString title = note.value("title").toString();
            QString content = note.value("content").toString();
            if (type == "text" || type.isEmpty()) {
                QString plain = StringUtils::htmlToPlainText(content);
                QString display = plain.replace('\n', ' ').replace('\r', ' ').trimmed().left(150);
                return display.isEmpty() ? title : display;
            }
            return title;
        }
        case TitleRole: return note.value("title");
        case ContentRole: return note.value("content");
        case IdRole: return note.value("id");
        case TagsRole: return note.value("tags");
        case TimeRole: return note.value("updated_at");
        case PinnedRole: return note.value("is_pinned");
        case LockedRole: return note.value("is_locked");
        case FavoriteRole: return note.value("is_favorite");
        case TypeRole: return note.value("item_type");
        case RatingRole: return note.value("rating");
        case CategoryIdRole: return note.value("category_id");
        case CategoryNameRole: return m_categoryMap.value(note.value("category_id").toInt(), "未分类");
        case ColorRole: return note.value("color");
        case SourceAppRole: return note.value("source_app");
        case SourceTitleRole: return note.value("source_title");
        case BlobRole: return note.value("data_blob");
        case RemarkRole: return note.value("remark");
        default: return QVariant();
    }
}

Qt::ItemFlags NoteModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::ItemIsEnabled;
    return QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled;
}

QStringList NoteModel::mimeTypes() const {
    return {"text/plain", "text/html", "text/uri-list", "application/x-note-ids"};
}

QMimeData* NoteModel::mimeData(const QModelIndexList& indexes) const {
    QMimeData* mimeData = new QMimeData();
    QStringList ids;
    QStringList plainTexts;
    QStringList htmlTexts;
    QList<QUrl> urls;

    for (const QModelIndex& index : indexes) {
        if (index.isValid()) {
            ids << QString::number(data(index, IdRole).toInt());
            QString content = data(index, ContentRole).toString();
            QString type = data(index, TypeRole).toString();
            
            if (type == "text" || type.isEmpty()) {
                if (StringUtils::isHtml(content)) {
                    plainTexts << StringUtils::htmlToPlainText(content);
                    htmlTexts << content;
                } else {
                    plainTexts << content;
                    htmlTexts << content.toHtmlEscaped().replace("\n", "<br>");
                }
            } else if (type == "file" || type == "folder" || type == "files") {
                QStringList rawPaths = content.split(';', Qt::SkipEmptyParts);
                for (const QString& p : rawPaths) {
                    QString path = p.trimmed().remove('\"');
                    if (QFileInfo::exists(path)) urls << QUrl::fromLocalFile(path);
                }
                plainTexts << content;
                htmlTexts << content.toHtmlEscaped().replace("\n", "<br>");
            }
        }
    }
    
    mimeData->setData("application/x-note-ids", ids.join(",").toUtf8());
    if (!plainTexts.isEmpty()) {
        mimeData->setText(plainTexts.join("\n---\n").replace("\n", "\r\n"));
        bool hasActualHtml = false;
        for (const QModelIndex& index : indexes) {
            if (StringUtils::isHtml(data(index, ContentRole).toString())) { hasActualHtml = true; break; }
        }
        if (hasActualHtml) {
            if (indexes.size() == 1) mimeData->setHtml(data(indexes.first(), ContentRole).toString());
            else {
                mimeData->setHtml(QString("<html><head><meta charset='utf-8'></head><body>%1</body></html>")
                                 .arg(htmlTexts.join("<br><hr><br>")));
            }
        }
    }
    if (!urls.isEmpty()) mimeData->setUrls(urls);
    return mimeData;
}

void NoteModel::setNotes(const QList<QVariantMap>& notes) {
    updateCategoryMap();
    m_thumbnailCache.clear();
    m_tooltipCache.clear();
    beginResetModel();
    m_notes = notes;
    endResetModel();
}

void NoteModel::updateCategoryMap() {
    auto categories = DatabaseManager::instance().getAllCategories();
    m_categoryMap.clear();
    for (const auto& cat : categories) m_categoryMap[cat["id"].toInt()] = cat["name"].toString();
}

void NoteModel::prependNote(const QVariantMap& note) {
    beginInsertRows(QModelIndex(), 0, 0);
    m_notes.prepend(note);
    endInsertRows();
}
