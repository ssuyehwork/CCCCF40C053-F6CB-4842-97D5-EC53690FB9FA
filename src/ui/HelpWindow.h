#ifndef HELPWINDOW_H
#define HELPWINDOW_H

#include "FramelessDialog.h"
#include <QScrollArea>
#include <QLabel>

class HelpWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit HelpWindow(QWidget* parent = nullptr);

private:
    void initUI();
    QString getHelpHtml();
};

#endif // HELPWINDOW_H
