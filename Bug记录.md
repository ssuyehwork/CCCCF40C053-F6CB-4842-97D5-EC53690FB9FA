G:\C++\Inspirenote\Inspirenote_release\src\ui\ScreenshotTool.cpp:461: error: use of deleted function 'void std::as_const(const _Tp&&) [with _Tp = QList<QString>]'
G:/C++/Inspirenote/Inspirenote_release/src/ui/ScreenshotTool.cpp: In member function 'void ScreenshotToolbar::createOptionWidget()':
G:/C++/Inspirenote/Inspirenote_release/src/ui/ScreenshotTool.cpp:461:45: error: use of deleted function 'void std::as_const(const _Tp&&) [with _Tp = QList<QString>]'
  461 |     for (const QString& name : std::as_const(settings.value("recentColors").toStringList())) {
      |                                ~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
C:\Qt\6.10.1\mingw_64\include\QtCore\qglobal.h:15: In file included from C:/Qt/6.10.1/mingw_64/include/QtCore/qglobal.h:15,
In file included from C:/Qt/6.10.1/mingw_64/include/QtCore/qglobal.h:15,
                 from C:/Qt/6.10.1/mingw_64/include/QtGui/qtguiglobal.h:7,
                 from C:/Qt/6.10.1/mingw_64/include/QtWidgets/qtwidgetsglobal.h:8,
                 from C:/Qt/6.10.1/mingw_64/include/QtWidgets/qwidget.h:8,
                 from C:/Qt/6.10.1/mingw_64/include/QtWidgets/QWidget:1,
                 from G:/C++/Inspirenote/Inspirenote_release/src/ui/ScreenshotTool.h:4,
                 from G:/C++/Inspirenote/Inspirenote_release/src/ui/ScreenshotTool.cpp:1:
C:/Qt/Tools/mingw1310_64/lib/gcc/x86_64-w64-mingw32/13.1.0/include/c++/utility:112:10: note: declared here
  112 |     void as_const(const _Tp&&) = delete;
      |          ^~~~~~~~
:-1: error: ninja: build stopped: subcommand failed.