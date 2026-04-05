#ifndef SEARCHLINEEDIT_H
#define SEARCHLINEEDIT_H

#include <QLineEdit>
#include <QMouseEvent>

class SearchHistoryPopup;

class SearchLineEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit SearchLineEdit(QWidget* parent = nullptr);
    void addHistoryEntry(const QString& text);
    QStringList getHistory() const;
    void clearHistory();
    void removeHistoryEntry(const QString& text);

protected:
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    void showPopup();
    SearchHistoryPopup* m_popup = nullptr;
};

#endif // SEARCHLINEEDIT_H
