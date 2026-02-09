#ifndef UTILS_H
#define UTILS_H

#include <QString>
#include <QDateTime>
#include <QRandomGenerator>

class Utils {
public:
    static QString generatePassword(int length = 16) {
        const QString possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_=+");
        QString password;
        for(int i=0; i<length; ++i) {
            int index = QRandomGenerator::global()->bounded(possibleCharacters.length());
            password.append(possibleCharacters.at(index));
        }
        return password;
    }

    static QString getTimestamp() {
        return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    }
};

#endif // UTILS_H