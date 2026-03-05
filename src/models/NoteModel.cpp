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
#include <QRegularExpression>
#include <QMimeData>

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
                iconName = "image"; iconColor = "#9b59b6";
            } else if (type == "file" || type == "files") {
                iconName = "file"; iconColor = "#f1c40f";
            } else if (type == "multiple") {
                iconName = "multiple"; iconColor = "";
            } else if (type == "psd") {
                iconName = "psd"; iconColor = "";
            } else if (type == "ocr_text") {
                iconName = "screenshot_ocr"; iconColor = "#007ACC";
            } else if (type == "captured_message") {
                iconName = "message"; iconColor = "#4a90e2";
            } else if (type == "local_file") {
                iconName = "file_import"; iconColor = "#f1c40f";
            } else if (type == "local_batch") {
                iconName = "batch_import"; iconColor = "#f1c40f";
            } else if (type == "folder") {
                iconName = "folder"; iconColor = "#e67e22";
            } else if (type == "local_folder") {
                iconName = "folder_import"; iconColor = "#e67e22";
            } else if (type == "color") {
                iconName = "palette"; iconColor = content;
            } else if (type == "pixel_ruler") {
                iconName = "pixel_ruler"; iconColor = "#ff5722";
            } else if (type == "file_managed") {
                iconName = "file_managed"; iconColor = "#3498db";
            } else if (type == "folder_managed") {
                iconName = "folder_managed"; iconColor = "#e67e22";
            } else {
                if (content.startsWith("http://") || content.startsWith("https://") || content.startsWith("www.")) {
                    iconName = "link"; iconColor = "#3498db";
                } else if (QRegularExpression("^#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})$").match(content).hasMatch()) {
                    iconName = "palette"; iconColor = content;
                } else if (content.startsWith("#") || content.startsWith("import ") || content.startsWith("def ")) {
                    iconName = "code"; iconColor = "#2ecc71";
                }
            }
            return IconHelper::getIcon(iconName, iconColor, 32);
        }
        case Qt::ToolTipRole: {
            int id = note.value("id").toInt();
            if (m_tooltipCache.contains(id)) return m_tooltipCache[id];
            QString catName = m_categoryMap.value(note.value("category_id").toInt(), "未分类");
            QString preview;
            if (note.value("item_type").toString() == "image") {
                preview = QString("<img src='data:image/png;base64,%1' width='300'>").arg(QString(note.value("data_blob").toByteArray().toBase64()));
            } else {
                preview = StringUtils::htmlToPlainText(note.value("content").toString()).left(400).toHtmlEscaped().replace("\n", "<br>");
            }
            QString html = QString("<html><body style='color: #ddd;'>"
                           "<table border='0'><tr><td><b>分区:</b></td><td>%1</td></tr>"
                           "<tr><td><b>标签:</b></td><td>%2</td></tr>"
                           "<tr><td><b>来源:</b></td><td>%3</td></tr></table><hr>%4</body></html>")
                .arg(catName, note.value("tags").toString(), note.value("source_app").toString(), preview);
            m_tooltipCache[id] = html;
            return html;
        }
        case Qt::DisplayRole: {
            QString type = note.value("item_type").toString();
            QString title = note.value("title").toString();
            if (type == "text" || type.isEmpty()) {
                QString plain = StringUtils::htmlToPlainText(note.value("content").toString());
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
        case CategoryIdRole: return note.value("category_id");
        case BlobRole: return note.value("data_blob");
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
                plainTexts << StringUtils::htmlToPlainText(content);
                htmlTexts << content;
            } else if (type == "file" || type == "folder" || type == "files") {
                QStringList paths = content.split(';', Qt::SkipEmptyParts);
                for (const QString& p : paths) {
                    if (QFileInfo::exists(p.trimmed().remove('\"'))) urls << QUrl::fromLocalFile(p.trimmed().remove('\"'));
                }
            }
        }
    }
    mimeData->setData("application/x-note-ids", ids.join(",").toUtf8());
    if (!plainTexts.isEmpty()) mimeData->setText(plainTexts.join("\n---\n"));
    if (!htmlTexts.isEmpty()) mimeData->setHtml(htmlTexts.join("<br><hr>"));
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
    for (const auto& cat : categories) {
        m_categoryMap[cat["id"].toInt()] = cat["name"].toString();
    }
}

void NoteModel::prependNote(const QVariantMap& note) {
    beginInsertRows(QModelIndex(), 0, 0);
    m_notes.prepend(note);
    endInsertRows();
}
