#pragma once

#include <QIcon>
#include <QString>
#include <QColor>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include <QMap>
#include <QSettings>
#include "../../SvgIcons.h"

namespace ArcMeta {

/**
 * @brief UI 辅助类
 * 提供统一的图标渲染、样式计算等工具函数
 */
class UiHelper {
public:
    /**
     * @brief 获取带颜色的 SVG 图标 (返回 QIcon)
     */
    static QIcon getIcon(const QString& key, const QColor& color, int size = 18) {
        return QIcon(getPixmap(key, QSize(size, size), color));
    }

    /**
     * @brief 获取带颜色的 SVG Pixmap (返回 QPixmap)
     */
    static QPixmap getPixmap(const QString& key, const QSize& size, const QColor& color) {
        if (!SvgIcons::icons.contains(key)) return QPixmap();

        QString svgData = SvgIcons::icons[key];
        // 渲染前替换颜色占位符
        if (svgData.contains("currentColor")) {
            svgData.replace("currentColor", color.name());
        } else {
            // 如果原本没有 currentColor 占位符但指定了颜色，尝试注入
            svgData.replace("fill=\"none\"", QString("fill=\"%1\"").arg(color.name()));
            svgData.replace("stroke=\"currentColor\"", QString("stroke=\"%1\"").arg(color.name()));
        }
        
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        QSvgRenderer renderer(svgData.toUtf8());
        renderer.render(&painter);
        
        return pixmap;
    }

    /**
     * @brief 获取扩展名对应的颜色 (哈希生成 + 持久化缓存)
     */
    static QColor getExtensionColor(const QString& ext) {
        static QMap<QString, QColor> s_cache;
        QString upperExt = ext.toUpper();
        
        // 1. 文件夹特殊处理
        if (upperExt == "DIR") return QColor(45, 65, 85, 200);
        if (upperExt.isEmpty()) return QColor(60, 60, 60, 180);

        // 2. 检查运行时缓存
        if (s_cache.contains(upperExt)) return s_cache[upperExt];

        // 3. 检查持久化存储 (QSettings)
        QSettings settings("ArcMeta团队", "ArcMeta");
        QString settingKey = QString("ExtensionColors/%1").arg(upperExt);
        if (settings.contains(settingKey)) {
            QColor color = settings.value(settingKey).value<QColor>();
            s_cache[upperExt] = color;
            return color;
        }

        // 4. 哈希法生成新颜色 (HSL 保证色彩分布)
        size_t hash = qHash(upperExt);
        int hue = static_cast<int>(hash % 360); // 0-359 色相
        // 固定 S=160, L=110 保证在深色背景下的可读性且色彩饱满
        QColor color = QColor::fromHsl(hue, 160, 110, 200); 
        
        // 写入缓存并持久化
        s_cache[upperExt] = color;
        settings.setValue(settingKey, color);
        return color;
    }
};

} // namespace ArcMeta
