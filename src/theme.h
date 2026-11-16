#pragma once
#include <QString>

// 深色主题：配色沿用 ImGui 版（背景 #2D3137 / 卡片 #23262B / 主色 #4C8DFF）
namespace wd {
namespace theme {

inline const char* kQss = R"QSS(
* { font-size: 13px; }
/* 关键：Qt 默认调色板是系统浅色，自定义容器若不显式给色会露出 #F3F3F3 白底。
   这里统一让所有控件透明、由卡片/根窗口的深色透上来，再逐个控件覆盖。 */
QWidget { background: transparent; color: #E6E9ED; }
QMainWindow, QDialog, QMessageBox { background: #1B1E22; }
QWidget#Root { background: #1B1E22; }
QFrame#ModuleCard {
  background: #23262B; border: 1px solid #2F343B; border-radius: 10px;
}
QFrame#ModuleHead {
  background: transparent; border: none; border-bottom: 1px solid #2F343B;
  border-top-left-radius: 10px; border-top-right-radius: 10px;
}
QLabel#ModuleTitle { color: #E6E9ED; font-size: 13px; font-weight: 600; }
QLabel#ModuleHint  { color: #7C848F; font-size: 11px; }
QLabel#Muted { color: #8A929C; }
QLabel#Body  { color: #E6E9ED; }

QPushButton {
  background: #2C3138; color: #D7DCE2; border: 1px solid #3A414A;
  border-radius: 7px; padding: 5px 11px;
}
QPushButton:hover  { background: #343B44; border-color: #4A5460; }
QPushButton:pressed{ background: #272C33; }
QPushButton:disabled { color: #6B7280; background: #262A30; border-color: #333941; }
QPushButton#Primary { background: #4C8DFF; border: 1px solid #4C8DFF; color: #FFFFFF; font-weight: 600; }
QPushButton#Primary:hover { background: #5D98FF; border-color: #5D98FF; }
QPushButton#Primary:disabled { background: #34405A; border-color: #34405A; color: #93A0B4; }
QPushButton#Ghost { background: transparent; border: 1px solid transparent; color: #A9B2BD; padding: 4px 8px; }
QPushButton#Ghost:hover { background: #2E343C; color: #E6E9ED; }
QPushButton#Pill { background: #2C3138; border: 1px solid #3A414A; color: #C3CAD3;
  border-radius: 12px; padding: 3px 10px; font-size: 12px; }
QPushButton#Pill:checked { background: #34405A; border-color: #4C8DFF; color: #CFE0FF; }

QPlainTextEdit, QTextEdit {
  background: #1E2126; color: #E6E9ED; border: 1px solid #2F343B; border-radius: 8px;
  selection-background-color: #3B5C99; selection-color: #FFFFFF;
}
QTextBrowser {
  background: transparent; color: #E6E9ED; border: none;
  selection-background-color: #3B5C99; selection-color: #FFFFFF;
}
QLineEdit {
  background: #1E2126; color: #E6E9ED; border: 1px solid #2F343B; border-radius: 7px; padding: 5px 9px;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: #4C8DFF; }

QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #3A414A; border-radius: 5px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: #4A5563; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #3A414A; border-radius: 5px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: #4A5563; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QComboBox {
  background: #1E2126; color: #E6E9ED; border: 1px solid #2F343B; border-radius: 7px; padding: 4px 8px;
}
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
  background: #23262B; color: #E6E9ED; border: 1px solid #3A414A;
  selection-background-color: #34405A;
}
QSpinBox, QDoubleSpinBox {
  background: #1E2126; color: #E6E9ED; border: 1px solid #2F343B; border-radius: 7px; padding: 4px 8px;
}
QCheckBox { color: #C3CAD3; }
QCheckBox::indicator { width: 15px; height: 15px; border-radius: 4px;
  border: 1px solid #4A5460; background: #1E2126; }
QCheckBox::indicator:checked { background: #4C8DFF; border-color: #4C8DFF; }

QMenu { background: #23262B; color: #D7DCE2; border: 1px solid #3A414A; border-radius: 8px; padding: 5px; }
QMenu::item { padding: 6px 22px 6px 14px; border-radius: 5px; }
QMenu::item:selected { background: #34405A; }
QMenu::separator { height: 1px; background: #3A414A; margin: 4px 8px; }

QSplitter::handle { background: transparent; }

QTableWidget {
  background: #1E2126; color: #E6E9ED; border: 1px solid #2F343B; border-radius: 8px;
  gridline-color: #2F343B; selection-background-color: #3B5C99;
}
QHeaderView::section {
  background: #262A30; color: #A9B2BD; border: none; border-right: 1px solid #2F343B;
  border-bottom: 1px solid #2F343B; padding: 5px 8px; font-weight: 600;
}
QDialog { background: #23262B; }
QTabWidget::pane { border: 1px solid #2F343B; border-radius: 8px; background: #23262B; }
QTabBar::tab { background: transparent; color: #9AA3AE; padding: 7px 14px; border-radius: 6px; }
QTabBar::tab:selected { background: #2E343C; color: #E6E9ED; }
QListView, QListWidget, QTreeView, QTreeWidget {
  background: #1E2126; color: #E6E9ED; border: 1px solid #2F343B; border-radius: 8px;
  outline: none;
}
QListView::item, QListWidget::item { padding: 6px 8px; border-radius: 6px; }
QListView::item:hover, QListWidget::item:hover { background: #2C3138; }
QListView::item:selected, QListWidget::item:selected { background: #34405A; color: #FFFFFF; }
QGroupBox { border: 1px solid #2F343B; border-radius: 8px; margin-top: 10px; padding-top: 6px; }
QGroupBox::title { color: #A9B2BD; subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QRadioButton { color: #C3CAD3; }
QToolTip { background: #2C3138; color: #E6E9ED; border: 1px solid #3A414A; padding: 4px 7px; }
/* 拖入文件时只有边框变色，不弹任何文字提示 */
QFrame#ModuleCard[dropHi="true"] { border: 1px solid #4C8DFF; }
/* 右侧 dock：整体是一块圆角面板，收起时只剩左端细条，展开时向右滑出 */
QWidget#DockBar {
  background: #23262B; border: 1px solid #31363E; border-radius: 10px;
}
QLabel#DockTime { color: #E6E9ED; font-size: 15px; font-weight: 600; }
QLabel#DockDate { color: #7C848F; font-size: 10px; }
QLabel#DockMark { background: #3E454F; border-radius: 2px; }
QFrame#DockSep { background: #31363E; border: none; }
QToolButton#DockBtn {
  color: #C3CAD3; background: transparent; border: 1px solid transparent;
  border-radius: 6px; padding: 2px 6px; text-align: left; font-size: 11px;
}
QToolButton#DockBtn:hover { background: #2E343C; color: #E6E9ED; }
QToolButton#DockBtn:pressed { background: #272C33; }
QWidget#DockStrip { background: transparent; }
QWidget#DockPanel { background: transparent; }
QToolButton#DockIconBtn {
  background: transparent; border: 1px solid transparent; border-radius: 7px;
}
QToolButton#DockIconBtn:hover { background: #2E343C; border: 1px solid #3A414A; }
QToolButton#DockModBtn {
  background: transparent; border: 1px solid transparent; border-radius: 5px;
  color: #8A929C; font-size: 11px; padding: 1px 6px; text-align: left;
}
QToolButton#DockModBtn:hover { background: #2E343C; color: #E6E9ED; border: 1px solid #3A414A; }
QToolButton#DockIconBtn:pressed { background: #272C33; }
/* 拖动模块标题栏换位时的落点高亮 */
QFrame#ModuleCard[moveHi="true"] { border: 1px solid #4C8DFF; }
/* 正在被拖走的模块：原位置留一圈虚线，鼠标上另有一块跟随的拖影 */
QFrame#ModuleCard[moveSrc="true"] { border: 1px dashed #4C8DFF; }
/* 模块标题栏右上角的隐藏按钮 */
QToolButton#ModuleClose {
  background: transparent; border: none; border-radius: 5px;
  color: #6E767F; font-size: 13px; padding: 0;
}
QToolButton#ModuleClose:hover { background: #2E343C; color: #E6E9ED; }
)QSS";

}  // namespace theme
}  // namespace wd
