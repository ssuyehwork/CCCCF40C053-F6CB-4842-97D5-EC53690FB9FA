#ifndef ICONHELPER_H
#define ICONHELPER_H

#include <QIcon>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include "SvgIcons.h"

class IconHelper {
public:
    static QIcon getIcon(const QString& name, const QString& color = "#cccccc", int size = 64) {
        if (!SvgIcons::icons.contains(name)) return QIcon();

        QString svgData = SvgIcons::icons[name];
        svgData.replace("currentColor", color);
        // 如果 svg 中没有 currentColor，强制替换所有可能的 stroke/fill 颜色（简易实现）
        // 这里假设 SVG 字符串格式标准，仅替换 stroke="currentColor" 或 fill="currentColor"
        // 实际上 Python 版是直接全量 replace "currentColor"

        QByteArray bytes = svgData.toUtf8();
        QSvgRenderer renderer(bytes);
        
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        
        QIcon icon;
        icon.addPixmap(pixmap, QIcon::Normal, QIcon::On);
        icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
        icon.addPixmap(pixmap, QIcon::Active, QIcon::On);
        icon.addPixmap(pixmap, QIcon::Active, QIcon::Off);
        icon.addPixmap(pixmap, QIcon::Selected, QIcon::On);
        icon.addPixmap(pixmap, QIcon::Selected, QIcon::Off);
        return icon;
    }
};

#endif // ICONHELPER_H
