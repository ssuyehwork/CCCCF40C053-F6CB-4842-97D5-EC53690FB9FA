import sys
from PySide6.QtWidgets import QApplication, QTextEdit
from PySide6.QtGui import QFont

app = QApplication(sys.argv)
te = QTextEdit()
f = te.font()
f.setPointSize(12)
te.setFont(f)
print(f"Initial: {te.font().pointSize()}")
te.zoomIn(1)
print(f"After zoomIn(1): {te.font().pointSize()}")
te.zoomIn(5)
print(f"After zoomIn(5): {te.font().pointSize()}")
