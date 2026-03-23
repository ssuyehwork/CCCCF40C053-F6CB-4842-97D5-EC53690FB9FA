#include "NoteModel.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QIcon>
#include "../ui/IconHelper.h"
#include "../ui/StringUtils.h"
#include "../core/DatabaseManager.h"
#include <QFileInfo>
#include <QBuffer>
#include <QPixmap>
#include <QByteArray>
#include <QUrl>
#include <QDir>
#include <QCoreApplication>

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

NoteModel::NoteModel(QObject* parent) : QAbstractItemModel(parent) {
    m_rootNode = new Node{true, {}, 0, nullptr, {}};
    updateCategoryMap();
}

NoteModel::~NoteModel() {
    delete m_rootNode;
}

void NoteModel::clearNodes() {
    qDeleteAll(m_rootNode->children);
    m_rootNode->children.clear();
}

void NoteModel::buildTree(const QList<QVariantMap>& notes) {
    clearNodes();
    m_categoryNodeMap.clear();

    // 获取分类层级信息
    auto categories = DatabaseManager::instance().getAllCategories();

    // 1. 创建所有分类节点
    for (const auto& cat : categories) {
        int id = cat["id"].toInt();
        Node* node = new Node{true, cat, id, nullptr, {}};
        m_categoryNodeMap[id] = node;
    }

    // 2. 建立分类层级
    for (const auto& cat : categories) {
        int id = cat["id"].toInt();
        int parentId = cat["parent_id"].toInt();
        Node* node = m_categoryNodeMap[id];

        if (parentId > 0 && m_categoryNodeMap.contains(parentId)) {
            node->parentNode = m_categoryNodeMap[parentId];
            m_categoryNodeMap[parentId]->children.append(node);
        } else {
            node->parentNode = m_rootNode;
            m_rootNode->children.append(node);
        }
    }

    // 3. 挂载笔记
    for (const auto& note : notes) {
        int catId = note.value("category_id").toInt();
        // 如果分类存在于树中，挂载到分类节点；否则挂载到根部（未分类）
        Node* parent = (catId > 0 && m_categoryNodeMap.contains(catId)) ? m_categoryNodeMap[catId] : m_rootNode;
        Node* noteNode = new Node{false, note, note.value("id").toInt(), parent, {}};
        parent->children.append(noteNode);
    }
}

QModelIndex NoteModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent)) return QModelIndex();
    Node* parentNode = parent.isValid() ? static_cast<Node*>(parent.internalPointer()) : m_rootNode;
    if (row < parentNode->children.count())
        return createIndex(row, column, parentNode->children.at(row));
    return QModelIndex();
}

QModelIndex NoteModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return QModelIndex();
    Node* childNode = static_cast<Node*>(child.internalPointer());
    Node* parentNode = childNode->parentNode;
    if (parentNode == m_rootNode) return QModelIndex();

    // 需要找到 parentNode 在其父节点中的 row
    Node* grandParentNode = parentNode->parentNode;
    int row = grandParentNode->children.indexOf(parentNode);
    return createIndex(row, 0, parentNode);
}

int NoteModel::rowCount(const QModelIndex& parent) const {
    if (parent.column() > 0) return 0;
    Node* parentNode = parent.isValid() ? static_cast<Node*>(parent.internalPointer()) : m_rootNode;
    return parentNode->children.count();
}

QVariant NoteModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return QVariant();
    Node* node = static_cast<Node*>(index.internalPointer());
    const QVariantMap& note = node->data;

    if (node->isCategory) {
        if (role == Qt::DisplayRole) {
             QString name = note["name"].toString();
             int count = countNotes(node);
             return QString("%1 (%2)").arg(name).arg(count);
        }
        if (role == Qt::DecorationRole) {
            return IconHelper::getIcon("branch", note["color"].toString(), 18);
        }
        if (role == IsCategoryRole) return true;
        return QVariant();
    }

    switch (role) {
        case IsCategoryRole: return false;
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
            } else if (type == "color") {
                iconName = "palette";
                iconColor = content;
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
                if (type == "folder" || (!content.contains(";") && QFileInfo(content.trimmed().remove('\"').remove('\'')).isDir())) {
                    iconName = "folder";
                    iconColor = "#FF8C00";
                } else if (type == "folders") {
                    iconName = "folders_multiple";
                    iconColor = "#483D8B";
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
            } else if (type == "captured_message") {
                iconName = "message";
                iconColor = "#00FFFF";
            } else if (type == "local_file" || type == "local_batch") {
                iconName = (type == "local_file") ? "file_import" : "batch_import";
                iconColor = "#FFFF00";
            } else if (type == "local_folder") {
                iconName = "folder_import";
                iconColor = "#FF8C00";
            } else if (type == "color" || type == "palette") {
                iconName = "palette";
                iconColor = content;
            } else if (type == "pixel_ruler") {
                iconName = "pixel_ruler";
                iconColor = "#FF5722";
            } else {
                QString stripped = content.trimmed();
                QString cleanPath = stripped;
                if ((cleanPath.startsWith("\"") && cleanPath.endsWith("\"")) || 
                    (cleanPath.startsWith("'") && cleanPath.endsWith("'"))) {
                    cleanPath = cleanPath.mid(1, cleanPath.length() - 2);
                }

                QString plain = StringUtils::htmlToPlainText(content).trimmed();
                if (stripped.startsWith("http://") || stripped.startsWith("https://") || stripped.startsWith("www.")) {
                    iconName = "link";
                    iconColor = "#17B345";
                } else if (static const QRegularExpression hexColorRegex("^#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})$"); hexColorRegex.match(plain).hasMatch()) {
                    iconName = "palette";
                    iconColor = plain;
                } else if (plain.startsWith("import ") || plain.startsWith("class ") || 
                           plain.startsWith("def ") || plain.startsWith("function") || 
                           plain.startsWith("var ") || plain.startsWith("const ") ||
                           (plain.startsWith("<") && [](){
                               static const QRegularExpression htmlTagRegex("^<(html|div|p|span|table|h[1-6]|script|!DOCTYPE|body|head)", QRegularExpression::CaseInsensitiveOption);
                               return htmlTagRegex;
                           }().match(plain).hasMatch()) ||
                           (plain.startsWith("{") && plain.contains("\":") && plain.contains("}"))) {
                    iconName = "code";
                    iconColor = "#00FF00";
                } else if (cleanPath.length() < 260 && (
                           (cleanPath.length() > 2 && cleanPath[1] == ':') || 
                           cleanPath.startsWith("\\") || cleanPath.startsWith("/") ||
                           cleanPath.startsWith("./") || cleanPath.startsWith("../"))) {
                    QFileInfo info(cleanPath);
                    if (info.exists()) {
                        if (info.isDir()) {
                            iconName = "folder";
                            iconColor = "#FF8C00";
                        } else {
                            iconName = "file";
                            iconColor = "#FFFF00";
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
            bool favorite = note.value("is_favorite").toBool();
            int rating = note.value("rating").toInt();

            QString catName = m_categoryMap.value(catId, "未分类");
            if (tags.isEmpty()) tags = "无";

            QString statusStr;
            if (pinned) statusStr += getIconHtml("pin_vertical", "#FF551C") + " 置顶 ";
            statusStr += getIconHtml(favorite ? "bookmark_filled" : "bookmark", favorite ? "#F2B705" : "#aaaaaa") + (favorite ? " 收藏 " : " 未收藏 ");

            QString ratingStr;
            for(int i=0; i<rating; ++i) ratingStr += getIconHtml("star_filled", "#f39c12") + " ";
            if (ratingStr.isEmpty()) ratingStr = "无";

            QString preview;
            if (note.value("item_type").toString() == "image") {
                QByteArray ba = note.value("data_blob").toByteArray();
                preview = QString("<img src='data:image/png;base64,%1' width='300'>").arg(QString(ba.toBase64()));
            } else {
                QString plainText = StringUtils::htmlToPlainText(content).trimmed();
                if (plainText != title.trimmed()) {
                    preview = plainText.left(400).toHtmlEscaped().replace("\n", "<br>").trimmed();
                    if (plainText.length() > 400) preview += "...";
                }
            }

            QString titleHtml = !title.isEmpty() ? QString("<div style='font-size: 13px; font-weight: bold; color: #fff; border-bottom: 1px solid #444; padding-bottom: 5px; margin-bottom: 5px;'>%1</div>").arg(title.toHtmlEscaped()) : "";
            QString remarkRow = !remark.isEmpty() ? QString("<tr><td width='22'>%1</td><td><b>备注:</b> <span style='color:#b3e5fc;'>%2</span></td></tr>").arg(getIconHtml("edit", "#4fc3f7"), remark.left(120).toHtmlEscaped().replace("\n", "<br>") + (remark.length() > 120 ? "..." : "")) : "";
            QString previewHtml = !preview.isEmpty() ? QString("<hr style='border: 0; border-top: 1px solid #555; margin: 5px 0;'><div style='color: #ccc; font-size: 12px; line-height: 1.4;'>%1</div>").arg(preview) : "";

            return QString("<html><body style='color: #ddd;'>%1<table border='0' cellpadding='2' cellspacing='0'><tr><td width='22'>%2</td><td><b>分类:</b> %3</td></tr><tr><td width='22'>%4</td><td><b>标签:</b> %5</td></tr><tr><td width='22'>%6</td><td><b>评级:</b> %7</td></tr><tr><td width='22'>%8</td><td><b>状态:</b> %9</td></tr>%10</table>%11</body></html>")
                .arg(titleHtml, getIconHtml("branch", "#4a90e2"), catName, getIconHtml("tag", "#FFAB91"), tags, getIconHtml("star", "#f39c12"), ratingStr, getIconHtml("pin_tilted", "#aaa"), statusStr, remarkRow, previewHtml);
        }
        case Qt::DisplayRole: {
            QString type = note.value("item_type").toString();
            QString content = note.value("content").toString();
            if (type == "text" || type.isEmpty() || type == "ocr_text" || type == "captured_message" || 
                type == "file" || type == "folder" || type == "files" || type == "folders") {
                QString plain = StringUtils::htmlToPlainText(content);
                return plain.replace('\n', ' ').replace('\r', ' ').trimmed().left(150);
            }
            if (type == "image") return QString("[图片]");
            return QString();
        }
        case TitleRole: return note.value("title");
        case ContentRole: return note.value("content");
        case IdRole: return note.value("id");
        case TagsRole: return note.value("tags");
        case TimeRole: return note.value("updated_at");
        case PinnedRole: return note.value("is_pinned");
        case LockedRole: return false;
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
        case PlainContentRole: {
            int id = note.value("id").toInt();
            if (m_plainContentCache.contains(id)) return m_plainContentCache[id];
            if (m_plainContentCache.size() > 500) m_plainContentCache.clear();
            QString plain = StringUtils::htmlToPlainText(note.value("content").toString()).simplified();
            m_plainContentCache[id] = plain;
            return plain;
        }
        default: return QVariant();
    }
}

Qt::ItemFlags NoteModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::ItemIsDropEnabled;
    return QAbstractItemModel::flags(index) | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}

QStringList NoteModel::mimeTypes() const {
    return {"text/plain", "text/html", "text/uri-list", "application/x-note-ids"};
}

QMimeData* NoteModel::mimeData(const QModelIndexList& indexes) const {
    QMimeData* mimeData = new QMimeData();
    QStringList ids;
    for (const QModelIndex& index : indexes) {
        if (index.isValid()) {
            Node* node = static_cast<Node*>(index.internalPointer());
            if (!node->isCategory) ids << QString::number(node->id);
        }
    }
    mimeData->setData("application/x-note-ids", ids.join(",").toUtf8());
    return mimeData;
}

void NoteModel::setNotes(const QList<QVariantMap>& notes) {
    beginResetModel();
    updateCategoryMap();
    buildTree(notes);
    m_thumbnailCache.clear();
    m_tooltipCache.clear();
    m_plainContentCache.clear();
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
    int id = note.value("id").toInt();
    int catId = note.value("category_id").toInt();

    // 确定父节点
    Node* parentNode = (catId > 0 && m_categoryNodeMap.contains(catId)) ? m_categoryNodeMap[catId] : m_rootNode;

    // 获取父节点的索引
    QModelIndex parentIndex = indexForNode(parentNode);

    // 执行增量插入
    beginInsertRows(parentIndex, 0, 0);
    Node* newNode = new Node{false, note, id, parentNode, {}};
    parentNode->children.prepend(newNode); // 总是插入到该分类的最顶端
    endInsertRows();
}

QModelIndex NoteModel::indexForNode(NoteModel::Node* node) const {
    if (!node || node == m_rootNode) return QModelIndex();
    Node* parentNode = node->parentNode;
    if (!parentNode) return QModelIndex();

    int row = parentNode->children.indexOf(node);
    if (row == -1) return QModelIndex();

    return createIndex(row, 0, node);
}

int NoteModel::countNotes(NoteModel::Node* node) const {
    if (!node) return 0;
    int count = 0;
    for (Node* child : node->children) {
        if (child->isCategory) count += countNotes(child);
        else count++;
    }
    return count;
}
