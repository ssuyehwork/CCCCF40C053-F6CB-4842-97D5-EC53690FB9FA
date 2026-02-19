#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QCoreApplication>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(":memory:");
    if (!db.open()) {
        qDebug() << "Cannot open database";
        return 1;
    }
    
    QSqlQuery query(db);
    // Check version
    if (query.exec("SELECT sqlite_version()")) {
        if (query.next()) qDebug() << "SQLite Version:" << query.value(0).toString();
    }

    // Check trigram
    bool trigram = query.exec("CREATE VIRTUAL TABLE test_fts USING fts5(content, tokenize='trigram')");
    if (trigram) {
        qDebug() << "Trigram tokenizer is SUPPORTED";
    } else {
        qDebug() << "Trigram tokenizer is NOT supported:" << query.lastError().text();
    }
    return 0;
}
