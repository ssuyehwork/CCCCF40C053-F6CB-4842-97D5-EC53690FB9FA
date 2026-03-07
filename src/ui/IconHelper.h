#ifndef ICONHELPER_H
#define ICONHELPER_H

#include <QIcon>
#include <QMenu>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include <QBuffer>
#include "SvgIcons.h"

class IconHelper {
public:
    static QString getIconHtml(const QString& name, const QString& color, int size = 16) {
        QIcon icon = getIcon(name, color, size);
        QPixmap pixmap = icon.pixmap(size, size);
        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        pixmap.save(&buffer, "PNG");
        return QString("<img src='data:image/png;base64,%1' width='%2' height='%3' style='vertical-align:middle;'>")
               .arg(QString(ba.toBase64())).arg(size).arg(size);
    }

    static QIcon getIcon(const QString& name, const QString& color = "#cccccc", int size = 64) {
        if (!SvgIcons::icons.contains(name)) return QIcon();

        QString svgData = SvgIcons::icons[name];
        svgData.replace("currentColor", color);

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

    static void setupMenu(QMenu* menu) {
        if (!menu) return;
        menu->setAttribute(Qt::WA_TranslucentBackground);
        menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    }
};

#endif // ICONHELPER_H
