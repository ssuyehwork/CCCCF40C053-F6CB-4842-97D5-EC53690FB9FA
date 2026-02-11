#ifndef MAINNOTEVIEWMODEL_H
#define MAINNOTEVIEWMODEL_H

#include <QObject>
#include <QVariantMap>
#include <QList>
#include <QString>
#include <QVariant>

class MainNoteViewModel : public QObject {
    Q_OBJECT
public:
    explicit MainNoteViewModel(QObject* parent = nullptr);

    void refreshData(const QString& keyword, const QString& filterType, const QVariant& filterValue, int page, int pageSize, const QVariantMap& criteria);
    void deleteNotes(const QList<int>& ids, bool physical);
    void toggleFavorite(const QList<int>& ids);
    void togglePin(const QList<int>& ids);
    void setRating(const QList<int>& ids, int rating);
    void moveToCategory(const QList<int>& ids, int catId);

signals:
    void dataRefreshed(const QList<QVariantMap>& notes, int totalCount, int totalPages);
    void statusMessageRequested(const QString& message);

private:
    QString m_currentFilterType;
};

#endif // MAINNOTEVIEWMODEL_H
