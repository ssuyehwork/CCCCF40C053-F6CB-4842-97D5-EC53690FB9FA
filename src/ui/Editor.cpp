#include "Editor.h"
#include "StringUtils.h"
#include <QMimeData>
#include <QFileInfo>
#include <utility>
#include <QUrl>
#include <QTextList>

MarkdownHighlighter::MarkdownHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
    HighlightingRule rule;

    // 标题 (Headers) - 蓝色
    QTextCharFormat headerFormat;
    headerFormat.setForeground(QColor("#569CD6"));
    headerFormat.setFontWeight(QFont::Bold);
    rule.pattern = QRegularExpression("^#{1,6}\\s.*");
    rule.format = headerFormat;
    m_highlightingRules.append(rule);

    // 粗体 (**bold**) - 红色
    QTextCharFormat boldFormat;
    boldFormat.setFontWeight(QFont::Bold);
    boldFormat.setForeground(QColor("#E06C75"));
    rule.pattern = QRegularExpression("\\*\\*.*?\\*\\*");
    rule.format = boldFormat;
    m_highlightingRules.append(rule);

    // 待办事项 ([ ] [x]) - 黄色/绿色
    QTextCharFormat uncheckedFormat;
    uncheckedFormat.setForeground(QColor("#E5C07B"));
    rule.pattern = QRegularExpression("-\\s\\[\\s\\]");
    rule.format = uncheckedFormat;
    m_highlightingRules.append(rule);

    QTextCharFormat checkedFormat;
    checkedFormat.setForeground(QColor("#6A9955"));
    rule.pattern = QRegularExpression("-\\s\\[x\\]");
    rule.format = checkedFormat;
    m_highlightingRules.append(rule);

    // 代码 (Code) - 绿色
    QTextCharFormat codeFormat;
    codeFormat.setForeground(QColor("#98C379"));
    codeFormat.setFontFamilies({"Consolas", "Monaco", "monospace"});
    rule.pattern = QRegularExpression("`[^`]+`|```.*");
    rule.format = codeFormat;
    m_highlightingRules.append(rule);

    // 引用 (> Quote) - 灰色
    QTextCharFormat quoteFormat;
    quoteFormat.setForeground(QColor("#808080"));
    quoteFormat.setFontItalic(true);
    rule.pattern = QRegularExpression("^\\s*>.*");
    rule.format = quoteFormat;
    m_highlightingRules.append(rule);
    
    // 列表 (Lists) - 紫色
    QTextCharFormat listFormat;
    listFormat.setForeground(QColor("#C678DD"));
    rule.pattern = QRegularExpression("^\\s*[\\-\\*]\\s");
    rule.format = listFormat;
    m_highlightingRules.append(rule);

    // 链接 (Links) - 浅蓝
    QTextCharFormat linkFormat;
    linkFormat.setForeground(QColor("#61AFEF"));
    linkFormat.setFontUnderline(true);
    rule.pattern = QRegularExpression("\\[.*?\\]\\(.*?\\)|https?://\\S+");
    rule.format = linkFormat;
    m_highlightingRules.append(rule);
}

void MarkdownHighlighter::highlightBlock(const QString& text) {
    for (const HighlightingRule& rule : m_highlightingRules) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}

#include <QVBoxLayout>
#include <QFrame>
#include <QMimeData>
#include <QUrl>

InternalEditor::InternalEditor(QWidget* parent) : QTextEdit(parent) {
    setStyleSheet("background: #1E1E1E; color: #D4D4D4; font-family: 'Consolas', 'Courier New'; font-size: 13pt; border: none; outline: none; padding: 10px;");
    setAcceptRichText(true); // 允许富文本以支持高亮和图片
}

void InternalEditor::insertTodo() {
    QTextCursor cursor = textCursor();
    if (!cursor.atBlockStart()) {
        cursor.insertText("\n");
    }
    cursor.insertText("- [ ] ");
    setTextCursor(cursor);
    setFocus();
}

void InternalEditor::highlightSelection(const QColor& color) {
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) return;
    QTextCharFormat format;
    format.setBackground(color);
    cursor.mergeCharFormat(format);
    setTextCursor(cursor);
}

void InternalEditor::insertFromMimeData(const QMimeData* source) {
    if (source->hasImage()) {
        QImage image = qvariant_cast<QImage>(source->imageData());
        if (!image.isNull()) {
            // 自动缩放宽图
            if (image.width() > 600) {
                image = image.scaledToWidth(600, Qt::SmoothTransformation);
            }
            textCursor().insertImage(image);
            return;
        }
    }
    if (source->hasUrls()) {
        for (const QUrl& url : source->urls()) {
            if (url.isLocalFile()) insertPlainText(QString("\n[文件引用: %1]\n").arg(url.toLocalFile()));
            else insertPlainText(QString("\n[链接: %1]\n").arg(url.toString()));
        }
        return;
    }
    QTextEdit::insertFromMimeData(source);
}

Editor::Editor(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); 

    m_stack = new QStackedWidget(this);
    // [CRITICAL] 强制设定深色背景 (#1e1e1e)，防止在特定系统主题或高对比度模式下背景变白，导致浅灰色文字不可见。
    m_stack->setStyleSheet("background-color: #1e1e1e; border: none;");
    
    m_edit = new InternalEditor(this);
    m_edit->setStyleSheet("background: transparent; color: #D4D4D4; font-family: 'Consolas', 'Courier New'; font-size: 13pt; border: none; outline: none; padding: 15px;");
    m_highlighter = new MarkdownHighlighter(m_edit->document());

    m_preview = new QTextEdit(this);
    m_preview->setReadOnly(true);
    // [CRITICAL] UI 偏移锁定：padding-top 必须保持 15px 左右，以确保预览内容（标题、分割线、正文）与顶部标题栏有视觉间隔。
    m_preview->setStyleSheet("QTextEdit { background: transparent; color: #D4D4D4; padding: 15px 10px 10px 15px; border: none; outline: none; }");

    m_stack->addWidget(m_edit);
    m_stack->addWidget(m_preview);
    
    layout->addWidget(m_stack);
}

void Editor::setNote(const QVariantMap& note, bool isPreview) {
    m_currentNote = note;
    QString title = note.value("title").toString();
    QString content = note.value("content").toString();
    QString type = note.value("item_type").toString();
    QByteArray blob = note.value("data_blob").toByteArray();

    m_edit->clear();
    
    // 增强 HTML 检测
    QString trimmed = content.trimmed();
    m_isRichText = trimmed.startsWith("<!DOCTYPE", Qt::CaseInsensitive) || 
                   trimmed.startsWith("<html", Qt::CaseInsensitive) || 
                   trimmed.contains("<style", Qt::CaseInsensitive) ||
                   Qt::mightBeRichText(content);

    // [UX] 如果是预览模式，注入格式化标题
    if (isPreview) {
        // 智能去重：如果正文第一行就是标题（或者标题的开头），预览时不再重复注入标题
        QString firstLine = content.section('\n', 0, 0).trimmed();
        bool titleAlreadyPresent = (firstLine == title || 
                                   firstLine == "# " + title || 
                                   (title.length() > 10 && firstLine.startsWith(title.left(20))));

        if (!titleAlreadyPresent) {
            QTextCharFormat titleFmt;
            titleFmt.setFontWeight(QFont::Bold);
            titleFmt.setFontPointSize(16);
            titleFmt.setForeground(QColor("#569CD6"));
            
            QTextCursor cursor = m_edit->textCursor();
            // 使用 # 标记，使其在阅读模式下能被识别为大标题
            cursor.insertText("# " + title, titleFmt);
            cursor.insertText("\n");
            
            QTextCharFormat hrFmt;
            hrFmt.setFontPointSize(2);
            cursor.insertText("\n", hrFmt);
            
            QTextBlockFormat blockFmt;
            blockFmt.setBottomMargin(10);
            cursor.setBlockFormat(blockFmt);
            cursor.insertHtml("<hr>");
        }
    }

    if (m_isRichText) {
        // 如果是 HTML 内容，加载为 HTML
        if (isPreview) {
            QString htmlWithTitle = QString("<h1 style='color: #569CD6;'>%1</h1><hr>%2")
                                    .arg(title.toHtmlEscaped(), content);
            m_edit->setHtml(htmlWithTitle);
        } else {
            m_edit->setHtml(content);
        }
        return;
    }

    // 纯文本/Markdown 逻辑
    QTextCursor cursor = m_edit->textCursor();
    cursor.movePosition(QTextCursor::End);
    
    // [FIX] 重置格式，防止正文继承注入标题的蓝色加粗样式
    QTextCharFormat defaultFmt;
    cursor.setCharFormat(defaultFmt);

    if (type == "image" && !blob.isEmpty()) {
        QImage img;
        img.loadFromData(blob);
        if (!img.isNull()) {
            if (img.width() > 550) {
                img = img.scaledToWidth(550, Qt::SmoothTransformation);
            }
            cursor.insertImage(img);
            cursor.insertText("\n\n");
        }
    } else if (type == "local_file" || type == "local_folder" || type == "local_batch") {
        QTextCharFormat linkFmt;
        linkFmt.setForeground(QColor("#569CD6"));
        linkFmt.setFontUnderline(true);
        cursor.setCharFormat(linkFmt);
        cursor.insertText("📂 本地托管项目: " + title + "\n");
        cursor.setCharFormat(QTextCharFormat());
        cursor.insertText("相对路径: " + content + "\n\n");
        cursor.insertText("(双击左侧列表项可直接在资源管理器中打开)\n\n");
    } else if (type == "color") {
        cursor.insertHtml(QString("<div style='margin: 20px; text-align: center;'>"
                                  "  <div style='background-color: %1; width: 100%; height: 200px; border-radius: 12px; border: 1px solid #555;'></div>"
                                  "  <h1 style='color: white; margin-top: 20px; font-family: Consolas; font-size: 32px;'>%1</h1>"
                                  "</div>").arg(content));
    } else {
        cursor.insertText(content);
    }
    
    // 滚动到顶部
    m_edit->moveCursor(QTextCursor::Start);
}

/**
 * [CRITICAL] 状态显示锁定：MainWindow 已移除行内编辑，故当无选中项或多选时，
 * 必须在此通过 HTML 手动渲染提示文字，否则预览区域将显示上一次笔记的残余内容。
 */
void Editor::setPlainText(const QString& text) {
    m_currentNote.clear();
    m_edit->setPlainText(text);
    if (text.isEmpty()) {
        m_preview->clear();
    } else {
        m_preview->setHtml(QString("<div style='color: #666; text-align: center; margin-top: 60px; font-size: 14px; font-family: \"Microsoft YaHei\", sans-serif;'>%1</div>")
                           .arg(text.toHtmlEscaped()));
    }
}

QString Editor::toPlainText() const {
    return m_edit->toPlainText();
}

QString Editor::toHtml() const {
    return m_edit->toHtml();
}

void Editor::setPlaceholderText(const QString& text) {
    m_edit->setPlaceholderText(text);
}

void Editor::clearFormatting() {
    QTextCursor cursor = m_edit->textCursor();
    if (cursor.hasSelection()) {
        QTextCharFormat format;
        m_edit->setCurrentCharFormat(format);
        cursor.setCharFormat(format);
    } else {
        m_edit->setCurrentCharFormat(QTextCharFormat());
    }
}

void Editor::toggleList(bool ordered) {
    QTextCursor cursor = m_edit->textCursor();
    cursor.beginEditBlock();
    QTextList* list = cursor.currentList();
    QTextListFormat format;
    format.setStyle(ordered ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
    
    if (list) {
        if (list->format().style() == format.style()) {
            QTextBlockFormat blockFmt;
            blockFmt.setObjectIndex(-1);
            cursor.setBlockFormat(blockFmt);
        } else {
            list->setFormat(format);
        }
    } else {
        cursor.createList(format);
    }
    cursor.endEditBlock();
}

bool Editor::findText(const QString& text, bool backward) {
    if (text.isEmpty()) return false;
    QTextDocument::FindFlags flags;
    if (backward) flags |= QTextDocument::FindBackward;
    
    bool found = m_edit->find(text, flags);
    if (!found) {
        // 循环搜索
        QTextCursor cursor = m_edit->textCursor();
        cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        m_edit->setTextCursor(cursor);
        found = m_edit->find(text, flags);
    }
    return found;
}

void Editor::togglePreview(bool preview) {
    if (preview) {
        QString title = m_currentNote.value("title").toString();
        QString content = m_currentNote.value("content").toString();
        QString type = m_currentNote.value("item_type").toString();
        QByteArray data = m_currentNote.value("data_blob").toByteArray();

        QString html = StringUtils::generateNotePreviewHtml(title, content, type, data);
        m_preview->setHtml(html);
        m_stack->setCurrentWidget(m_preview);
    } else {
        m_stack->setCurrentWidget(m_edit);
    }
}

void Editor::setReadOnly(bool ro) {
    m_edit->setReadOnly(ro);
}
