#include "NoteModel.h"
#include <QDateTime>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QSize>
#include <QFileInfo>
#include <QUrl>
#include <QDebug>
#include <QImageReader>
#include <QBuffer>
#include <QRegularExpression>
#include "../core/DatabaseManager.h"
#include "../ui/StringUtils.h"
#include "../ui/IconHelper.h"

NoteModel::NoteModel(QObject* parent) : QAbstractListModel(parent) {
    updateCategoryMap();
    connect(&DatabaseManager::instance(), &DatabaseManager::categoriesChanged, this, &NoteModel::updateCategoryMap);
}

void NoteModel::updateCategoryMap() {
    m_categoryMap.clear();
    auto cats = DatabaseManager::instance().getAllCategories();
    for (const auto& cat : cats) {
        m_categoryMap[cat["id"].toInt()] = cat["name"].toString();
    }
}

void NoteModel::setNotes(const QList<QVariantMap>& notes) {
    beginResetModel();
    m_notes = notes;
    m_thumbnailCache.clear();
    m_tooltipCache.clear();
    endResetModel();
}

void NoteModel::prependNote(const QVariantMap& note) {
    beginInsertRows(QModelIndex(), 0, 0);
    m_notes.prepend(note);
    m_tooltipCache.clear();
    endInsertRows();
}

int NoteModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_notes.count();
}

static QString getIconHtml(const QString& name, const QString& color, int size = 16) {
    QPixmap pix = IconHelper::getIcon(name, color).pixmap(size, size);
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    pix.save(&buffer, "PNG");
    return QString("<img src='data:image/png;base64,%1' width='%2' height='%2' style='vertical-align: middle;'>")
           .arg(QString(ba.toBase64())).arg(size);
}

QVariant NoteModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return QVariant();
    int row = index.row();
    if (row < 0 || row >= m_notes.count()) return QVariant();

    const QVariantMap& note = m_notes[row];

    switch (role) {
        case Qt::DecorationRole: {
            QString type = note.value("item_type").toString();
            QString content = note.value("content").toString();
            QString iconName = "text";
            QString iconColor = "#aaaaaa";

            if (type == "image") {
                int id = note.value("id").toInt();
                if (m_thumbnailCache.contains(id)) return m_thumbnailCache[id];
                
                QByteArray ba = note.value("data_blob").toByteArray();
                QImage img;
                img.loadFromData(ba);
                if (!img.isNull()) {
                    QPixmap pix = QPixmap::fromImage(img).scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    m_thumbnailCache[id] = QIcon(pix);
                    return m_thumbnailCache[id];
                }
                iconName = "image";
                iconColor = "#3498db";
            } else if (type == "color") {
                iconName = "palette";
                iconColor = content; // 直接用色码作为图标颜色
            } else if (type == "link") {
                iconName = "link";
                iconColor = "#3498db";
            } else if (type == "file" || type == "files" || type == "folder") {
                iconName = (type == "folder") ? "folder" : "file";
                iconColor = (type == "files") ? "#FF4858" : "#f1c40f";
            } else if (type == "ocr_text") {
                iconName = "screenshot_ocr";
                iconColor = "#007acc";
            } else {
                // 文本：如果包含泰文则用紫色区分
                if (StringUtils::containsThai(content)) {
                    iconColor = "#9b59b6";
                }
            }
            return IconHelper::getIcon(iconName, iconColor, 32);
        }
        case Qt::ToolTipRole: {
            int id = note.value("id").toInt();
            if (m_tooltipCache.contains(id)) return m_tooltipCache[id];

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
                QString plainText = StringUtils::extractPlainText(content);
                QString escaped = plainText.left(400).toHtmlEscaped().replace("\n", "<br>").trimmed();
                // 尝试恢复高亮效果 (如果有定义)
                preview = StringUtils::applyMarkdownHighlighting(escaped);
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
                                .arg(getIconHtml("edit", "#4fc3f7"),
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
                     getIconHtml("branch", "#4a90e2"), catName,
                     getIconHtml("tag", "#FFAB91"), tags,
                     getIconHtml("star", "#f39c12"), ratingStr,
                     getIconHtml("pin_tilted", "#aaa"), statusStr,
                     getIconHtml("monitor", "#aaaaaa"), sourceApp,
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
        case RemarkRole:
            return note.value("remark");
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
            }
        }
    }

    if (!ids.isEmpty()) mimeData->setData("application/x-note-ids", ids.join(",").toUtf8());
    if (!plainTexts.isEmpty()) mimeData->setText(plainTexts.join("\n\n---\n\n"));
    if (!htmlTexts.isEmpty()) mimeData->setHtml(htmlTexts.join("<hr>"));
    if (!urls.isEmpty()) mimeData->setUrls(urls);

    return mimeData;
}
