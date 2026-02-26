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

static QString getIconHtml(const QString& name, const QString& color) {
    QIcon icon = IconHelper::getIcon(name, color, 16);
    QPixmap pixmap = icon.pixmap(16, 16);
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return QString("<img src='data:image/png;base64,%1' width='16' height='16' style='vertical-align:middle;'>")
           .arg(QString(ba.toBase64()));
}

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
            return QVariant(); // 强制不返回任何背景色，由 Delegate 控制
        case Qt::DecorationRole: {
            QString type = note.value("item_type").toString();
            QString content = note.value("content").toString().trimmed();
            QString iconName = "text"; // Default
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
                iconName = "file";
                iconColor = "#f1c40f";
            } else if (type == "ocr_text") {
                // [CRITICAL] 识别提取的文字专用图标
                iconName = "screenshot_ocr";
                iconColor = "#007ACC";
            } else if (type == "captured_message") {
                // 自动捕获的消息
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
                iconName = "category"; // 或使用专门的分类包图标
                iconColor = "#95a5a6";
            } else if (type == "color") {
                iconName = "palette";
                iconColor = content;
            } else if (type == "pixel_ruler") {
                iconName = "pixel_ruler";
                iconColor = "#ff5722";
            } else {
                // 【核心修复】智能检测文本内容，对齐 Python 版逻辑
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
                    // 优先识别 HEX 色码，防止被识别为代码
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
            if (m_tooltipCache.contains(id)) return m_tooltipCache[id];

            QString title = note.value("title").toString();
            QString content = note.value("content").toString();
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
            if (pinned) statusStr += getIconHtml("pin_vertical", "#e74c3c") + " 置顶 ";
            if (locked) statusStr += getIconHtml("lock", "#aaaaaa") + " 锁定 ";
            if (favorite) statusStr += getIconHtml("bookmark_filled", "#ff6b81") + " 书签 ";
            if (statusStr.isEmpty()) statusStr = "无";

            if (sourceApp.isEmpty()) sourceApp = "未知应用";

            QString ratingStr;
            for(int i=0; i<rating; ++i) ratingStr += getIconHtml("star_filled", "#f39c12") + " ";
            if (ratingStr.isEmpty()) ratingStr = "无";

            QString preview;
            if (note.value("item_type").toString() == "image") {
                QByteArray ba = note.value("data_blob").toByteArray();
                preview = QString("<img src='data:image/png;base64,%1' width='300'>").arg(QString(ba.toBase64()));
            } else {
                // 【核心修复】剥离 HTML 标签以显示纯文本预览 (防止样式代码进入 ToolTip)
                QString plainText = StringUtils::htmlToPlainText(content);
                preview = plainText.left(400).toHtmlEscaped().replace("\n", "<br>").trimmed();
                if (plainText.length() > 400) preview += "...";
            }
            if (preview.isEmpty()) preview = title.toHtmlEscaped();

            QString html = QString("<html><body style='color: #ddd;'>"
                           "<table border='0' cellpadding='2' cellspacing='0'>"
                           "<tr><td width='22'>%1</td><td><b>分区:</b> %2</td></tr>"
                           "<tr><td width='22'>%3</td><td><b>标签:</b> %4</td></tr>"
                           "<tr><td width='22'>%5</td><td><b>评级:</b> %6</td></tr>"
                           "<tr><td width='22'>%7</td><td><b>状态:</b> %8</td></tr>"
                           "<tr><td width='22'>%9</td><td><b>来源:</b> %10</td></tr>"
                           "</table>"
                           "<hr style='border: 0; border-top: 1px solid #555; margin: 5px 0;'>"
                           "<div style='color: #ccc; font-size: 12px; line-height: 1.4;'>%11</div>"
                           "</body></html>")
                .arg(getIconHtml("branch", "#4a90e2"), catName,
                     getIconHtml("tag", "#FFAB91"), tags,
                     getIconHtml("star", "#f39c12"), ratingStr,
                     getIconHtml("pin_tilted", "#aaa"), statusStr,
                     getIconHtml("monitor", "#aaaaaa"))
                .arg(sourceApp, preview);
            
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
        case TitleRole:
            return note.value("title");
        case ContentRole:
            return note.value("content");
        case IdRole:
            return note.value("id");
        case TagsRole:
            return note.value("tags");
        case TimeRole:
            return note.value("updated_at");
        case PinnedRole:
            return note.value("is_pinned");
        case LockedRole:
            return note.value("is_locked");
        case FavoriteRole:
            return note.value("is_favorite");
        case TypeRole:
            return note.value("item_type");
        case RatingRole:
            return note.value("rating");
        case CategoryIdRole:
            return note.value("category_id");
        case CategoryNameRole:
            return m_categoryMap.value(note.value("category_id").toInt(), "未分类");
        case ColorRole:
            return note.value("color");
        case SourceAppRole:
            return note.value("source_app");
        case SourceTitleRole:
            return note.value("source_title");
        case BlobRole:
            return note.value("data_blob");
        default:
            return QVariant();
    }
}

Qt::ItemFlags NoteModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::ItemIsEnabled;
    return QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled;
}

QStringList NoteModel::mimeTypes() const {
    // 【核心修复】优先级调整：text/plain 必须放在第一位，确保浏览器等外部应用优先识别
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
                    if (QFileInfo::exists(path)) {
                        urls << QUrl::fromLocalFile(path);
                    }
                }
                plainTexts << content;
                htmlTexts << content.toHtmlEscaped().replace("\n", "<br>");
            }
        }
    }
    
    mimeData->setData("application/x-note-ids", ids.join(",").toUtf8());
    
    if (!plainTexts.isEmpty()) {
        // 1. 设置纯文本格式 (使用 \r\n 换行)
        QString combinedPlain = plainTexts.join("\n---\n").replace("\n", "\r\n");
        mimeData->setText(combinedPlain);
        
        // 2. 仅在确实包含 HTML 内容时提供 HTML 分支，防止纯文本拖拽时出现 HTML 源码泄漏
        bool hasActualHtml = false;
        for (const QModelIndex& index : indexes) {
            if (StringUtils::isHtml(data(index, ContentRole).toString())) {
                hasActualHtml = true;
                break;
            }
        }

        if (hasActualHtml) {
            if (indexes.size() == 1) {
                mimeData->setHtml(data(indexes.first(), ContentRole).toString());
            } else {
                QString combinedHtml = htmlTexts.join("<br><hr><br>");
                mimeData->setHtml(QString(
                    "<html>"
                    "<head><meta charset='utf-8'></head>"
                    "<body>%1</body>"
                    "</html>"
                ).arg(combinedHtml));
            }
        }
    }
    
    if (!urls.isEmpty()) {
        mimeData->setUrls(urls);
    }
    
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
    for (const auto& cat : categories) {
        m_categoryMap[cat["id"].toInt()] = cat["name"].toString();
    }
}

// 【新增】函数的具体实现
void NoteModel::prependNote(const QVariantMap& note) {
    // 通知视图：我要在第0行插入1条数据
    beginInsertRows(QModelIndex(), 0, 0);
    m_notes.prepend(note);
    endInsertRows();
}