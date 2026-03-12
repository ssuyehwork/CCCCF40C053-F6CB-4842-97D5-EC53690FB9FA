#ifndef ICONHELPER_H
#define ICONHELPER_H

#include <QIcon>
#include <QMenu>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include "SvgIcons.h"

class IconHelper {
public:
    static QPixmap renderPixmap(const QString& name, const QString& color, int size = 64) {
        if (!SvgIcons::icons.contains(name)) return QPixmap();

        QString svgData = SvgIcons::icons[name];
        svgData.replace("currentColor", color);

        QByteArray bytes = svgData.toUtf8();
        QSvgRenderer renderer(bytes);
        
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        return pixmap;
    }

    static QIcon getIcon(const QString& name, const QString& color = "#cccccc", int size = 64) {
        // 2026-03-xx 按照用户要求，实现多状态图标感知：
        // 1. Normal 状态强制绑定全局专属色。
        // 2. Selected/Active 状态强制设为白色，确保高亮背景下的可见性。

        QString normalColor = color;
        if (SvgIcons::iconColors.contains(name)) {
            normalColor = SvgIcons::iconColors[name];
        }

        QPixmap normalPix = renderPixmap(name, normalColor, size);
        QPixmap whitePix = renderPixmap(name, "#FFFFFF", size);
        
        QIcon icon;
        // 正常态使用专属色
        icon.addPixmap(normalPix, QIcon::Normal, QIcon::On);
        icon.addPixmap(normalPix, QIcon::Normal, QIcon::Off);

        // 选中态与激活态强制使用白色
        icon.addPixmap(whitePix, QIcon::Selected, QIcon::On);
        icon.addPixmap(whitePix, QIcon::Selected, QIcon::Off);
        icon.addPixmap(whitePix, QIcon::Active, QIcon::On);
        icon.addPixmap(whitePix, QIcon::Active, QIcon::Off);

        return icon;
    }

    // 统一设置 QMenu 样式,移除系统原生直角阴影
    static void setupMenu(QMenu* menu) {
        if (!menu) return;
        // 移除系统原生阴影,使用自定义圆角
        menu->setAttribute(Qt::WA_TranslucentBackground);
        menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    }
};

#endif // ICONHELPER_H
