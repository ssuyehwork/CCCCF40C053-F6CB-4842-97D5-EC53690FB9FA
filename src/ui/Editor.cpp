#include "Editor.h"
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
    m_stack->setStyleSheet("background: transparent; border: none;");
    
    m_edit = new InternalEditor(this);
    m_edit->setStyleSheet("background: transparent; color: #D4D4D4; font-family: 'Consolas', 'Courier New'; font-size: 13pt; border: none; outline: none; padding: 15px;");
    m_highlighter = new MarkdownHighlighter(m_edit->document());

    m_preview = new QTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setStyleSheet("background: transparent; color: #D4D4D4; padding: 15px; border: none; outline: none;");

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

void Editor::setPlainText(const QString& text) {
    m_currentNote.clear();
    m_edit->setPlainText(text);
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
        if (m_isRichText) {
            // 如果已经是富文本，直接同步 HTML 到预览框
            m_preview->setHtml(m_edit->toHtml());
            m_stack->setCurrentWidget(m_preview);
            return;
        }

        QString text = m_edit->toPlainText();
        QString html = "<html><head><style>"
                       "body { font-family: 'Segoe UI', 'Microsoft YaHei'; color: #ddd; background-color: #1e1e1e; line-height: 1.6; padding: 20px; }"
                       "h1 { color: #569CD6; border-bottom: 1px solid #333; padding-bottom: 5px; }"
                       "h2 { color: #569CD6; border-bottom: 1px solid #222; }"
                       "h3 { color: #eee; margin-bottom: 5px; }"
                       "hr { border: 0; border-top: 1px solid #444; margin: 15px 0; }"
                       "code { background-color: #333; padding: 2px 4px; border-radius: 3px; font-family: Consolas; color: #98C379; }"
                       "pre { background-color: #252526; padding: 10px; border-radius: 5px; border: 1px solid #444; overflow-x: auto; }"
                       "blockquote { border-left: 4px solid #569CD6; padding-left: 15px; color: #888; font-style: italic; background: #252526; margin: 10px 0; }"
                       "p { margin: 10px 0; }"
                       "ul, ol { padding-left: 25px; }"
                       "li { margin: 5px 0; }"
                       "img { max-width: 100%; border-radius: 5px; border: 1px solid #333; margin: 10px 0; }"
                       "</style></head><body>";

        // 如果是图片笔记，且 text 没有包含图片标记（目前逻辑下 text 是 H1 + content）
        // 我们在预览模式下根据 m_currentNote 显式渲染
        QString type = m_currentNote["item_type"].toString();
        QByteArray blob = m_currentNote["data_blob"].toByteArray();
        
        QStringList lines = text.split("\n");
        bool inCodeBlock = false;
        bool imageRendered = false;

        for (const QString& line : std::as_const(lines)) {
            if (line.startsWith("```")) {
                if (!inCodeBlock) { html += "<pre><code>"; inCodeBlock = true; }
                else { html += "</code></pre>"; inCodeBlock = false; }
                continue;
            }
            
            if (inCodeBlock) {
                html += line.toHtmlEscaped() + "<br>";
                continue;
            }

            if (line.startsWith("###### ")) html += "<h6>" + line.mid(7).toHtmlEscaped() + "</h6>";
            else if (line.startsWith("##### ")) html += "<h5>" + line.mid(6).toHtmlEscaped() + "</h5>";
            else if (line.startsWith("#### ")) html += "<h4>" + line.mid(5).toHtmlEscaped() + "</h4>";
            else if (line.startsWith("### ")) html += "<h3>" + line.mid(4).toHtmlEscaped() + "</h3>";
            else if (line.startsWith("## ")) html += "<h2>" + line.mid(3).toHtmlEscaped() + "</h2>";
            else if (line.startsWith("# ")) html += "<h1>" + line.mid(2).toHtmlEscaped() + "</h1>" + "<hr>"; // 预览模式下大标题自带分割线
            else if (line.startsWith("> ")) html += "<blockquote>" + line.mid(2).toHtmlEscaped() + "</blockquote>";
            else if (line.startsWith("- [ ] ")) html += "<p><span style='color:#E5C07B;'>☐</span> " + line.mid(6).toHtmlEscaped() + "</p>";
            else if (line.startsWith("- [x] ")) html += "<p><span style='color:#6A9955;'>☑</span> " + line.mid(6).toHtmlEscaped() + "</p>";
            else if (line.startsWith("---") || line.startsWith("***")) html += "<hr>";
            else if (line.isEmpty()) html += "<br>";
            else {
                // 处理行内代码 `code`
                QString processedLine = line.toHtmlEscaped();
                QRegularExpression inlineCode("`(.*?)`");
                processedLine.replace(inlineCode, "<code>\\1</code>");
                html += "<p>" + processedLine + "</p>";
            }
        }
        
        html += "</body></html>";
        m_preview->setHtml(html);
        m_stack->setCurrentWidget(m_preview);
    } else {
        m_stack->setCurrentWidget(m_edit);
    }
}

void Editor::setReadOnly(bool ro) {
    m_edit->setReadOnly(ro);
}
