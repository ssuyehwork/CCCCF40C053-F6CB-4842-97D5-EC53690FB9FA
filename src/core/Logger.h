#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QtLogging>

class Logger {
public:
    /**
     * @brief 2026-03-xx 按照用户要求：初始化日志系统，设置消息处理器并执行过期日志清理
     */
    static void init();

private:
    /**
     * @brief 自定义消息处理器
     */
    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

    /**
     * @brief 2026-03-xx 按照用户要求：清理超过 2 天的旧日志文件
     */
    static void cleanOldLogs();

    static QString s_logPath;
};

#endif // LOGGER_H
