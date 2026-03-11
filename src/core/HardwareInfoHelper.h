#ifndef HARDWAREINFOHELPER_H
#define HARDWAREINFOHELPER_H

#include <QString>

class HardwareInfoHelper {
public:
    /**
     * @brief 获取程序当前运行所在物理磁盘的物理序列号 (Serial Number)
     * @return 物理序列号字符串。如果获取失败，返回空字符串。
     */
    static QString getDiskPhysicalSerialNumber();

private:
    // 隐藏实现细节
};

#endif // HARDWAREINFOHELPER_H
