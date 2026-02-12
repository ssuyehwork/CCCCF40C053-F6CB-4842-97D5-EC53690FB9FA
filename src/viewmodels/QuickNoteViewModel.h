#ifndef QUICKNOTEVIEWMODEL_H
#define QUICKNOTEVIEWMODEL_H

#include <QObject>
#include <QVariantMap>
#include <QList>
#include <QString>
#include <QVariant>

class QuickNoteViewModel : public QObject {
    Q_OBJECT
public:
    explicit QuickNoteViewModel(QObject* parent = nullptr);

    // 状态属性
    int currentPage() const { return m_currentPage; }
    int totalPages() const { return m_totalPages; }
    QString currentFilterType() const { return m_currentFilterType; }
    QVariant currentFilterValue() const { return m_currentFilterValue; }

    // 数据操作
    void refreshData(const QString& keyword, const QString& filterType, const QVariant& filterValue, int page);
    void deleteNotes(const QList<int>& ids, bool physical);
    void toggleFavorite(const QList<int>& ids);
    void togglePin(const QList<int>& ids);
    void setRating(const QList<int>& ids, int rating);
    void moveToCategory(const QList<int>& ids, int catId);
    void addTags(const QList<int>& ids, const QStringList& tags);
    void updateTags(const QList<int>& ids, const QStringList& tags);

signals:
    void dataRefreshed(const QList<QVariantMap>& notes, int totalCount, int totalPages);
    void statusMessageRequested(const QString& message, bool isError = false);
    void sidebarRefreshRequested();

private:
    int m_currentPage = 1;
    int m_totalPages = 1;
    QString m_currentFilterType = "all";
    QVariant m_currentFilterValue = -1;
};

#endif // QUICKNOTEVIEWMODEL_H
