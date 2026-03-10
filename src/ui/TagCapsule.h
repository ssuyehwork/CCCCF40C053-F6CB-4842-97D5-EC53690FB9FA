#ifndef TAGCAPSULE_H
#define TAGCAPSULE_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QPushButton>
#include "IconHelper.h"

// 用户要求：修复由于 Q_OBJECT 类定义在 .cpp 中导致的 MOC 冲突。将 TagCapsule 移至独立头文件。
namespace SvgIcons {
    class TagCapsule : public QWidget {
        Q_OBJECT
    public:
        TagCapsule(const QString& text, QWidget* parent = nullptr) : QWidget(parent), m_tagText(text) {
            setAttribute(Qt::WA_StyledBackground, true);
            auto* layout = new QHBoxLayout(this);
            layout->setContentsMargins(10, 4, 8, 4);
            layout->setSpacing(4);

            setStyleSheet(
                "QWidget { background-color: #333333; border: none; border-radius: 11px; }"
                "QWidget:hover { background-color: #404040; }"
            );

            auto* label = new QLabel(text);
            label->setStyleSheet("color: #4facfe; font-size: 11px; font-weight: bold; background: transparent; border: none; padding-left: 2px;");
            layout->addWidget(label);

            auto* closeBtn = new QPushButton();
            closeBtn->setFixedSize(16, 16);
            closeBtn->setIconSize(QSize(10, 10));
            closeBtn->setCursor(Qt::PointingHandCursor);
            closeBtn->setIcon(IconHelper::getIcon("close", "#666", 16));
            closeBtn->setStyleSheet(
                "QPushButton {"
                "  background-color: transparent;"
                "  border-radius: 4px;"
                "  padding: 0px;"
                "}"
                "QPushButton:hover {"
                "  background-color: #E74C3C;"
                "}"
            );
            connect(closeBtn, &QPushButton::clicked, [this](){
                emit removeRequested(m_tagText);
            });
            layout->addWidget(closeBtn);
        }

    signals:
        void removeRequested(const QString& tag);

    private:
        QString m_tagText;
    };
}

#endif // TAGCAPSULE_H
