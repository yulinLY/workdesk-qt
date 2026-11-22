#pragma once
#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

// 主题：一套控件样式模板 + 深色/亮色两套配色。
// 四档切换：深色 / 亮色 / 跟随系统 / 跟随时间（18:00 前亮色，之后深色）。
namespace wd {
namespace theme {

enum class Mode { Dark = 0, Light = 1, System = 2, Time = 3 };

// 时间模式的分界点：18:00 之前亮色，18:00 及以后深色
constexpr int kLightBeforeHour = 18;

QString ModeKey(Mode m);   // dark / light / system / time，用于持久化
Mode ModeFromKey(const QString& key, Mode fallback = Mode::Dark);
QString ModeLabel(Mode m);

// token 取色：返回当前生效主题下的 #RRGGBB（token 表见 theme.cpp）
QString Hex(const QString& token);
bool IsLightActive();

// 由模板生成的 QSS 与调色板
QString Css(bool light);
QPalette Palette(bool light);

// 主题管理器：判定、应用、持久化、广播
class ThemeManager : public QObject {
  Q_OBJECT

 public:
  static ThemeManager& Instance();

  void Init();  // 读偏好并应用，main 里调一次
  Mode mode() const { return mode_; }
  bool light() const { return light_; }
  void SetMode(Mode m);  // 切换并写盘
  void Refresh();        // 重新判定（系统配色变化 / 跨过 18 点）

 signals:
  void ModeChanged(int mode, bool light);
  void Applied(bool light);

 private:
  ThemeManager() = default;
  void Apply();
  bool Resolve(Mode m) const;

  Mode mode_ = Mode::Dark;
  bool light_ = false;
  class QTimer* timer_ = nullptr;
  int lastHour_ = -1;
};

// 控件样式模板：颜色全部用 @token@ 占位，由 Css(light) 填色
inline const char* kQssTemplate = R"QSS(

* { font-size: 13px; }
/* 关键：Qt 默认调色板是系统浅色，自定义容器若不显式给色会露出 #F3F3F3 白底。
   这里统一让所有控件透明、由卡片/根窗口的深色透上来，再逐个控件覆盖。 */
QWidget { background: transparent; color: @text@; }
/* 窗口底由 MainWindow::paintEvent 自绘（模糊桌面快照 + @glass@ 遮罩），
   这里必须保持透明：一旦给实色就会把自绘的磨砂整块盖死。
   磨砂只作用于窗口自身像素，桌面壁纸不受任何影响。 */
QMainWindow { background: transparent; }
QWidget#Root { background: transparent; }
/* 弹窗/对话框不挂材质，保持实心，避免小窗透花看不清 */
QDialog, QMessageBox { background: @bg@; }
QFrame#ModuleCard {
  background: @glasscard@; border: 1px solid @line@; border-radius: 10px;
}
QFrame#ModuleHead {
  background: transparent; border: none; border-bottom: 1px solid @line@;
  border-top-left-radius: 10px; border-top-right-radius: 10px;
}
QLabel#ModuleTitle { color: @text@; font-size: 13px; font-weight: 600; }
QLabel#ModuleHint  { color: @faint@; font-size: 11px; }
QLabel#Muted { color: @muted@; }
QLabel#Body  { color: @text@; }

QPushButton {
  background: @btn@; color: @text2@; border: 1px solid @btnline@;
  border-radius: 7px; padding: 5px 11px;
}
QPushButton:hover  { background: @btnhover@; border-color: @handle@; }
QPushButton:pressed{ background: @btndown@; }
QPushButton:disabled { color: @disabled@; background: @head@; border-color: @disline@; }
QPushButton#Primary { background: @accent@; border: 1px solid @accent@; color: @white@; font-weight: 600; }
QPushButton#Primary:hover { background: @accenthover@; border-color: @accenthover@; }
QPushButton#Primary:disabled { background: @selsoft@; border-color: @selsoft@; color: @disabled2@; }
QPushButton#Ghost { background: transparent; border: 1px solid transparent; color: @text4@; padding: 4px 8px; }
QPushButton#Ghost:hover { background: @hover@; color: @text@; }
QPushButton#Pill { background: @btn@; border: 1px solid @btnline@; color: @text3@;
  border-radius: 12px; padding: 3px 10px; font-size: 12px; }
QPushButton#Pill:checked { background: @selsoft@; border-color: @accent@; color: @accenttext@; }

QPlainTextEdit, QTextEdit {
  background: @input@; color: @text@; border: 1px solid @line@; border-radius: 8px;
  selection-background-color: @sel@; selection-color: @white@;
}
QTextBrowser {
  background: transparent; color: @text@; border: none;
  selection-background-color: @sel@; selection-color: @white@;
}
QLineEdit {
  background: @input@; color: @text@; border: 1px solid @line@; border-radius: 7px; padding: 5px 9px;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent@; }

QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @btnline@; border-radius: 5px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: @handle2@; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: @btnline@; border-radius: 5px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: @handle2@; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QComboBox {
  background: @input@; color: @text@; border: 1px solid @line@; border-radius: 7px; padding: 4px 8px;
}
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
  background: @card@; color: @text@; border: 1px solid @btnline@;
  selection-background-color: @selsoft@;
}
QSpinBox, QDoubleSpinBox {
  background: @input@; color: @text@; border: 1px solid @line@; border-radius: 7px; padding: 4px 8px;
}
QCheckBox { color: @text3@; }
QCheckBox::indicator { width: 15px; height: 15px; border-radius: 4px;
  border: 1px solid @handle@; background: @input@; }
QCheckBox::indicator:checked { background: @accent@; border-color: @accent@; }

QMenu { background: @card@; color: @text2@; border: 1px solid @btnline@; border-radius: 8px; padding: 5px; }
QMenu::item { padding: 6px 22px 6px 14px; border-radius: 5px; }
QMenu::item:selected { background: @selsoft@; }
QMenu::separator { height: 1px; background: @btnline@; margin: 4px 8px; }

QSplitter::handle { background: transparent; }

QTableWidget {
  background: @input@; color: @text@; border: 1px solid @line@; border-radius: 8px;
  gridline-color: @line@; selection-background-color: @sel@;
}
QHeaderView::section {
  background: @head@; color: @text4@; border: none; border-right: 1px solid @line@;
  border-bottom: 1px solid @line@; padding: 5px 8px; font-weight: 600;
}
QDialog { background: @card@; }
QTabWidget::pane { border: 1px solid @line@; border-radius: 8px; background: @card@; }
QTabBar::tab { background: transparent; color: @text5@; padding: 7px 14px; border-radius: 6px; }
QTabBar::tab:selected { background: @hover@; color: @text@; }
QListView, QListWidget, QTreeView, QTreeWidget {
  background: @input@; color: @text@; border: 1px solid @line@; border-radius: 8px;
  outline: none;
}
QListView::item, QListWidget::item { padding: 6px 8px; border-radius: 6px; }
QListView::item:hover, QListWidget::item:hover { background: @btn@; }
QListView::item:selected, QListWidget::item:selected { background: @selsoft@; color: @white@; }
QGroupBox { border: 1px solid @line@; border-radius: 8px; margin-top: 10px; padding-top: 6px; }
QGroupBox::title { color: @text4@; subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QRadioButton { color: @text3@; }
QToolTip { background: @btn@; color: @text@; border: 1px solid @btnline@; border-radius: 6px; padding: 5px 8px; font-size: 12px; }
/* 拖入文件时只有边框变色，不弹任何文字提示 */
QFrame#ModuleCard[dropHi="true"] { border: 1px solid @accent@; }
/* 右侧 dock：整体是一块圆角面板，收起时只剩左端细条，展开时向右滑出 */
QWidget#DockBar {
  background: @card@; border: 1px solid @dockline@; border-radius: 10px;
}
QLabel#DockTime { color: @text@; font-size: 15px; font-weight: 600; }
QLabel#DockDate { color: @faint@; font-size: 10px; }
QLabel#DockMark { background: @mark@; border-radius: 2px; }
QFrame#DockSep { background: @dockline@; border: none; }
QToolButton#DockBtn {
  color: @text3@; background: transparent; border: 1px solid transparent;
  border-radius: 6px; padding: 2px 6px; text-align: left; font-size: 11px;
}
QToolButton#DockBtn:hover { background: @hover@; color: @text@; }
QToolButton#DockBtn:pressed { background: @btndown@; }
QToolButton#DockBtn::menu-indicator { image: none; width: 0px; height: 0px; }
QWidget#DockStrip { background: transparent; }
QWidget#DockPanel { background: transparent; }
QToolButton#DockIconBtn {
  background: transparent; border: 1px solid transparent; border-radius: 7px;
}
QToolButton#DockIconBtn:hover { background: @hover@; border: 1px solid @btnline@; }
QToolButton#DockModBtn {
  background: transparent; border: 1px solid transparent; border-radius: 5px;
  color: @muted@; font-size: 11px; padding: 1px 6px; text-align: left;
}
QToolButton#DockModBtn:hover { background: @hover@; color: @text@; border: 1px solid @btnline@; }
QToolButton#DockIconBtn:pressed { background: @btndown@; }
/* 拖动模块标题栏换位时的落点高亮 */
QFrame#ModuleCard[moveHi="true"] { border: 1px solid @accent@; }
/* 正在被拖走的模块：原位置留一圈虚线，鼠标上另有一块跟随的拖影 */
QFrame#ModuleCard[moveSrc="true"] { border: 1px dashed @accent@; }
/* 模块标题栏右上角的隐藏按钮 */
QToolButton#ModuleClose {
  background: transparent; border: none; border-radius: 5px;
  color: @faint2@; font-size: 13px; padding: 0;
}
QToolButton#ModuleClose:hover { background: @hover@; color: @text@; }

/* ---- 以下为消息区/代码块/状态等原本写死在控件行内样式里的规则 ----
   原来是 setStyleSheet 硬编码深色，切主题改不到；搬进全局 QSS 后由 token 统一接管。 */
QFrame#CodeWrap { background: @codebg@; border: 1px solid @line@; border-radius: 8px; }
QWidget#CodeHead {
  background: @codehead@; border: none;
  border-top-left-radius: 8px; border-top-right-radius: 8px;
}
QLabel#CodeLang { color: @codelang@; font-size: 11px; font-weight: 600; background: transparent; }
QLabel#CodeInfo { color: @codeinfo@; font-size: 11px; background: transparent; }
QPushButton#CodeCopy {
  background: transparent; border: none; color: @muted@; padding: 2px 6px; font-size: 11px;
}
QPushButton#CodeCopy:hover { color: @accenttext@; }
QPlainTextEdit#CodeEdit { background: @codebg@; border: none; padding: 4px 8px; }
QPlainTextEdit#CodeEdit QScrollBar:horizontal { height: 9px; background: @codescroll@; }
QPlainTextEdit#CodeEdit QScrollBar::handle:horizontal {
  background: @handle2@; border-radius: 4px; min-width: 24px;
}
QLabel#Avatar {
  background: @selsoft@; color: @accenttext@; border-radius: 14px;
  font-size: 11px; font-weight: 700;
}
QFrame#Bubble { background: @card@; border: 1px solid @line@; border-radius: 10px; }
QFrame#Bubble[user="true"] { background: @userbubble@; border: 1px solid @userbubbleline@; }
QFrame#Bubble QTextBrowser, QFrame#Bubble QLabel#Body { color: @text@; }
QFrame#Bubble[user="true"] QTextBrowser, QFrame#Bubble[user="true"] QLabel#Body { color: #FFFFFF; }
QLabel#BubbleMeta { color: @meta@; font-size: 11px; background: transparent; }
QFrame#Bubble[user="true"] QLabel#BubbleMeta { color: #EAF0FF; }
QLabel#AttachFile {
  background: @input@; color: @text3@; border: 1px solid @btnline@; border-radius: 6px;
  padding: 3px 8px; font-size: 12px;
}
QLabel#AttachName { color: @text3@; background: transparent; }
QFrame#Bubble[user="true"] QLabel#AttachFile {
  background: rgba(255, 255, 255, 0.16); color: #FFFFFF; border-color: rgba(255, 255, 255, 0.30);
}
QFrame#Bubble[user="true"] QLabel#AttachName { color: #FFFFFF; }
QLabel#AppName { color: @text@; font-size: 15px; font-weight: 600; }
QLabel#StatusOk { color: @ok@; }
QLabel#StatusErr { color: @err@; }
/* dock 栏里的主题切换按钮：当前档位高亮 */
QToolButton#DockBtn[themeOn="true"] { background: @selsoft@; color: @accenttext@; }
/* ---- 欢迎向导 / 生日彩蛋 ---- */
QLabel#WelcomeTitle { color: @text@; font-size: 20px; font-weight: 600; }
QLabel#WelcomeBody { color: @text2@; font-size: 13px; }
QLabel#WelcomeHint { color: @muted@; font-size: 12px; }
QLabel#BirthdayTitle { color: @text@; font-size: 22px; font-weight: 600; }
QLabel#BirthdayBody { color: @text2@; font-size: 13px; }
QLabel#BirthdayHint { color: @muted@; font-size: 12px; }

/* ---- 音乐模块 ---- */
QLabel#MusicCover {
  background: @btn@; color: @accent@; border: 1px solid @btnline@;
  border-radius: 10px; font-size: 24px; font-weight: 600;
}
QLabel#MusicTitle { color: @text@; font-size: 14px; font-weight: 600; }
QLabel#MusicArtist { color: @muted@; font-size: 12px; }
QLabel#MusicAlbum { color: @faint@; font-size: 11px; }
QLabel#MusicTime { color: @faint@; font-size: 11px; }
QLabel#MusicLyricCur { color: @accent@; font-size: 13px; font-weight: 600; }
QLabel#MusicLyricSide { color: @faint@; font-size: 11px; }
QLabel#MusicSourceHint { color: @muted@; font-size: 11px; }
QLabel#DockMiniTitle { color: @muted@; font-size: 11px; }
QWidget#MusicPanel[dropHi="true"] { background: @hover@; border-radius: 8px; }
QWidget#MusicPanel QListWidget {
  background: transparent; border: none; outline: none; font-size: 12px;
}
QWidget#MusicPanel QListWidget::item {
  color: @muted@; padding: 3px 5px; border-radius: 5px;
}
QWidget#MusicPanel QListWidget::item:hover { background: @hover@; color: @text@; }
QWidget#MusicPanel QListWidget::item:selected {
  background: @selsoft@; color: @text@; font-weight: 600;
}
QSlider#MusicProgress::groove:horizontal {
  height: 4px; background: @btnline@; border-radius: 2px;
}
QSlider#MusicProgress::sub-page:horizontal { background: @accent@; border-radius: 2px; }
QSlider#MusicProgress::handle:horizontal {
  width: 10px; margin: -4px 0; border-radius: 5px; background: @text@;
}
QSlider#MusicVolume::groove:horizontal {
  height: 3px; background: @btnline@; border-radius: 2px;
}
QSlider#MusicVolume::sub-page:horizontal { background: @muted@; border-radius: 2px; }
QSlider#MusicVolume::handle:horizontal {
  width: 9px; margin: -3px 0; border-radius: 5px; background: @text2@;
}
QToolButton#MusicBtn {
  background: @btn@; color: @muted@; border: 1px solid @btnline@;
  border-radius: 7px; padding: 4px 9px; font-size: 11px;
}
QToolButton#MusicBtn:hover { background: @btnhover@; color: @text@; }
QToolButton#MusicBtn:disabled { color: @disabled@; border-color: @disline@; }
QToolButton#MusicPlayBtn {
  background: @accent@; color: @accenttext@; border: none;
  border-radius: 7px; padding: 5px 12px; font-size: 12px; font-weight: 600;
}
QToolButton#MusicPlayBtn:hover { background: @accenthover@; }

/* ---- 卸载程序（Uninstall.exe）---- */
QLabel#UninstallTitle { color: @text@; font-size: 15px; font-weight: 600; }
QLabel#UninstallHint { color: @muted@; font-size: 12px; }
QTextEdit#UninstallLog {
  background: @input@; color: @text3@; border: 1px solid @line@; border-radius: 8px;
  font-size: 11px; padding: 6px;
}
QToolButton#UninstallClose {
  background: transparent; border: none; border-radius: 6px;
  color: @muted@; font-size: 14px; padding: 0;
}
QToolButton#UninstallClose:hover { background: #E5484D; color: #FFFFFF; }
)QSS";

}  // namespace theme
}  // namespace wd
