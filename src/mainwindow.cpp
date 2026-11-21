#include "mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPair>
#include <QResizeEvent>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QVariantAnimation>
#include <QEvent>
#include <QEasingCurve>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSplitterHandle>
#include <QUrl>
#include <QVBoxLayout>
#include <dwmapi.h>
#include <windows.h>

#include "chatpanel.h"
#include "dockbar.h"
#include "frost.h"
#include "glass.h"
#include "notepad.h"
#include "settingsdialog.h"
#include "theme.h"
#include "wdtrace.h"


namespace wd {
namespace {

// 模块之间的分隔竖线：整条铺满 handle 会戳进模块圆角区域，看着像错位、漏缝。
// 这里改用自绘——只在 handle 中线上画 1px 线，上下各内缩一个圆角半径，
// 线两端正好落在模块直边起点，与模块边框同色，圆角处留空，不再互相顶撞。
constexpr int kCardRadius = 10;   // 与 theme.h 中 QFrame#ModuleCard 的 border-radius 一致
constexpr int kSplitHandleW = 9;  // 模块间距

class CardSplitterHandle : public QSplitterHandle {
 public:
  CardSplitterHandle(Qt::Orientation o, QSplitter* parent) : QSplitterHandle(o, parent) {}

 protected:
  void enterEvent(QEnterEvent*) override { update(); }
  void leaveEvent(QEvent*) override { update(); }

  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const QColor col = underMouse() ? QColor(theme::Hex(QStringLiteral("accent")))
                                    : QColor(theme::Hex(QStringLiteral("line")));
    if (orientation() == Qt::Horizontal) {
      const int h = height() - kCardRadius * 2;
      if (h > 0) p.fillRect(QRect(width() / 2, kCardRadius, 1, h), col);
    } else {
      const int w = width() - kCardRadius * 2;
      if (w > 0) p.fillRect(QRect(kCardRadius, height() / 2, w, 1), col);
    }
  }
};

class CardSplitter : public QSplitter {
 public:
  using QSplitter::QSplitter;

 protected:
  QSplitterHandle* createHandle() override { return new CardSplitterHandle(orientation(), this); }
};

// 布局记忆存放位置：%APPDATA%\WorkDesk\WorkDeskQt\layout.ini，和配置/会话同处
QString LayoutIniPath() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + QStringLiteral("/layout.ini");
}

// 这些扩展名视为“文本”，拖入时交给记事本模块
bool IsTextPath(const QString& p) {
  static const char* kExt[] = {"txt", "md", "markdown", "log", "json", "ini", "cfg", "conf",
                               "yaml", "yml", "toml", "csv", "cpp", "cc", "cxx", "c",     "h",
                               "hpp", "hh", "py",  "js",   "ts",   "java", "cs",  "go",    "rs",
                               "sh",  "bat", "ps1", "html", "css",  "xml",  "sql", "cmake"};
  const QString e = QFileInfo(p).suffix().toLower();
  if (e.isEmpty()) return true;
  for (const char* k : kExt)
    if (e == QLatin1String(k)) return true;
  return false;
}

// 窗口标题栏要跟界面主题走：深色界面配深色标题栏，亮色界面配亮色标题栏。
// 属性 20/19 控制深浅（Win10 1809+），34/35/36（Win11）分别指定边框色/标题栏色/标题文字色，
// 调用失败不影响功能，只是标题栏回到系统默认。
void ApplyTitleBar(QWidget* w, bool light) {
  HWND hwnd = reinterpret_cast<HWND>(w->winId());
  if (!hwnd) return;
  BOOL dark = light ? FALSE : TRUE;
  if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
  const QColor txt = QColor(theme::Hex(QStringLiteral("text")));
  COLORREF text = RGB(txt.red(), txt.green(), txt.blue());
  DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
  // 挂了系统材质时不能再指定标题栏色和边框色：一旦给了实色，DWM 就用纯色把亚克力盖死，
  // 只剩客户区透出材质，标题栏会硬生生截成一块实心板。这里显式交给系统（DWMWA_COLOR_NONE）。
  constexpr COLORREF kColorNone = 0xFFFFFFFE;
  if (wd::glass::Enabled()) {
    COLORREF none = kColorNone;
    DwmSetWindowAttribute(hwnd, 35, &none, sizeof(none));
    DwmSetWindowAttribute(hwnd, 34, &none, sizeof(none));
  } else {
    const QColor cap = QColor(theme::Hex(QStringLiteral("card")));
    const QColor brd = QColor(theme::Hex(QStringLiteral("line")));
    COLORREF caption = RGB(cap.red(), cap.green(), cap.blue());
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    COLORREF border = RGB(brd.red(), brd.green(), brd.blue());
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
  }
  // 已显示的窗口改完属性后需要让非客户区重画一次，否则标题栏颜色要等下次重绘才生效
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// 走一遍子树强制重绘：QSS 换掉以后，自绘控件（分隔线、图标等）不会自己重画
void RepaintAll(QWidget* w) {
  if (!w) return;
  w->update();
  for (QWidget* c : w->findChildren<QWidget*>()) c->update();
}

void Repolish(QWidget* w) {
  if (!w) return;
  w->style()->unpolish(w);
  w->style()->polish(w);
  w->update();
}

// 拖放路由：QTextBrowser / QPlainTextEdit / QTableWidget 这些子控件会自己吞掉拖放事件，
// 拖文件落在对话区或记事本区时主窗口根本收不到 drop，表现就是“拖不进文件”。
// 这里在应用级事件过滤器里先截获含本地文件的拖放，统一交给主窗口按扩展名分流。
class WindowDropRouter : public QObject {
 public:
  explicit WindowDropRouter(MainWindow* win) : QObject(win), win_(win) {}

  bool eventFilter(QObject* obj, QEvent* ev) override {
    const QEvent::Type t = ev->type();
    if (t != QEvent::DragEnter && t != QEvent::DragMove && t != QEvent::Drop &&
        t != QEvent::DragLeave)
      return false;

    auto* w = qobject_cast<QWidget*>(obj);
    if (!w || (w != win_ && !win_->isAncestorOf(w))) return false;

    if (t == QEvent::DragLeave) {
      win_->ShowDropHint(false);
      return false;
    }

    auto* de = static_cast<QDropEvent*>(ev);
    if (!de->mimeData() || !de->mimeData()->hasUrls()) return false;
    if (QApplication::activeModalWidget()) return false;  // 模态框自己处理

    bool hasLocal = false;
    for (const QUrl& u : de->mimeData()->urls()) {
      if (u.isLocalFile()) {
        hasLocal = true;
        break;
      }
    }
    if (!hasLocal) return false;

    if (t == QEvent::Drop)
      win_->HandleDropUrls(de->mimeData()->urls(), w);
    else
      win_->ShowDropHint(true);
    de->setDropAction(Qt::CopyAction);
    de->acceptProposedAction();
    return true;  // 吃掉事件，避免子控件自己插入文本/图片
  }

 private:
  MainWindow* win_;
};

// 拖动模块时跟在鼠标上的那块半透明缩略图：顶层窗口 + 鼠标穿透，画一块带描边的圆角卡片影子
class DragGhost : public QWidget {
 public:
  explicit DragGhost(const QPixmap& pm) : pm_(pm) {
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput |
                   Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    resize(pm.size());
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRect r = rect().adjusted(0, 0, -1, -1);
    QPainterPath path;
    path.addRoundedRect(r, 10, 10);
    p.setClipPath(path);
    p.setOpacity(0.90);
    p.drawPixmap(0, 0, pm_);
    p.setOpacity(1.0);
    p.setClipping(false);
    p.setPen(QPen(QColor(0x4C, 0x8D, 0xFF), 1));
    p.drawRoundedRect(r, 10, 10);
  }

 private:
  QPixmap pm_;
};

}  // namespace

MainWindow::MainWindow() {
  wd::Trace("mw: enter");
  cfg_.Load();
  wd::Trace("mw: cfg ok");
  setWindowTitle(QStringLiteral("WorkDesk"));
  resize(1440, 920);
  setAcceptDrops(true);
  // 实时毛玻璃的前提：窗口像素必须带 alpha，DWM 才有地方透出系统材质。
  // 这两条必须在第一次 show 之前设好，show 之后再改会让 Qt 重建窗口。
  // WA_NoSystemBackground 关掉 Qt 自己的背景擦除，窗口底全由 paintEvent 画，
  // 脏区由我们自己清干净，dock 展开/收起就不再留残影。
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  BuildUi();
  wd::Trace("mw: buildui ok");
  BuildDock();
  wd::Trace("mw: builddock ok");
  RestoreLayout();
  wd::Trace("mw: restorelayout ok");
  ApplyTitleBar(this, theme::ThemeManager::Instance().light());
  // 主题切换后：标题栏重新上色 + 自绘控件重绘（QSS 由 ThemeManager 统一刷新）
  connect(&theme::ThemeManager::Instance(), &theme::ThemeManager::Applied, this,
          [this](bool light) {
            ApplyTitleBar(this, light);
            RepaintAll(this);
            if (dock_) dock_->SyncThemeButton();
          });
  wd::Trace("mw: titlebar ok");
  qApp->installEventFilter(new WindowDropRouter(this));
  // 状态栏那行"就绪"纯属噪音，直接不显示
  statusBar()->hide();
  wd::Trace("mw: leave");
}

MainWindow::~MainWindow() = default;

QWidget* MainWindow::MakeCard(const QString& title, QWidget* content, QWidget* parent) {
  auto* card = new QFrame(parent ? parent : this);
  card->setObjectName("ModuleCard");
  auto* lay = new QVBoxLayout(card);
  lay->setContentsMargins(10, 8, 10, 10);
  lay->setSpacing(6);

  auto* head = new QFrame(card);
  head->setObjectName("ModuleHead");
  head->setCursor(Qt::OpenHandCursor);
  head->setToolTip(QStringLiteral("按住拖动可与另一个模块互换位置"));
  auto* hl = new QHBoxLayout(head);
  hl->setContentsMargins(2, 2, 2, 6);
  auto* t = new QLabel(title, head);
  t->setObjectName("ModuleTitle");
  // 标题文字不吃鼠标事件，事件直接落到标题栏，拖动命中率 100%
  t->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  hl->addWidget(t);
  hl->addStretch(1);
  // 模块自己的隐藏按钮：点掉哪个模块，哪个模块就从界面上消失，dock 里留一条还原入口
  auto* closeBtn = new QToolButton(head);
  closeBtn->setObjectName(QStringLiteral("ModuleClose"));
  closeBtn->setText(QString(QChar(0x00D7)));
  closeBtn->setFixedSize(18, 18);
  closeBtn->setCursor(Qt::PointingHandCursor);
  closeBtn->setToolTip(QStringLiteral("隐藏此模块"));
  connect(closeBtn, &QToolButton::clicked, this, [this, card]() { HideModule(card); });
  hl->addWidget(closeBtn, 0, Qt::AlignVCenter);
  head->installEventFilter(this);
  head->setProperty("card", QVariant::fromValue<QWidget*>(card));
  lay->addWidget(head);
  lay->addWidget(content, 1);

  cards_ << card;
  chromeHeads_ << head;
  return card;
}

void MainWindow::BuildUi() {
  root_ = new QWidget(this);
  root_->setObjectName("Root");
  root_->setAttribute(Qt::WA_StyledBackground, true);
  auto* rl = new QVBoxLayout(root_);
  // 右侧只留 dock 收起竖条的宽度 + 一点缝隙：竖条紧贴卡片右边缘，两者连成一体
  rl->setContentsMargins(10, 8, DockBar::StripWidth(), 8);
  rl->setSpacing(8);

  // 顶栏：只剩应用名，功能入口全部搬到 dock
  top_ = new QWidget(root_);
  auto* tl = new QHBoxLayout(top_);
  tl->setContentsMargins(2, 0, 2, 0);
  tl->setSpacing(8);

  auto* appName = new QLabel(QStringLiteral("WorkDesk"), top_);
  appName->setObjectName("AppName");  // 颜色/字号交给全局 QSS，随主题切换
  tl->addWidget(appName);
  tl->addStretch(1);

  rl->addWidget(top_);

  // 主体：左聊天 / 右记事本，分隔条可拖动
  chat_ = new ChatPanel(&cfg_, root_);
  connect(chat_, &ChatPanel::RequestSettings, this, &MainWindow::OpenSettings);
  connect(chat_, &ChatPanel::Notice, this, [this](const QString& text) {
    statusBar()->showMessage(text, 4000);
  });
  QWidget* chatCard = MakeCard(QStringLiteral("对话"), chat_, root_);
  chatCard->setProperty("cardId", QStringLiteral("chat"));

  notepad_ = new NotepadPanel(root_);
  connect(notepad_, &NotepadPanel::Notice, this, [this](const QString& text) {
    statusBar()->showMessage(text, 4000);
  });
  wd::Trace("mw: notepad built");
  QWidget* noteCard = MakeCard(QStringLiteral("记事本"), notepad_, root_);
  wd::Trace("mw: notepad card ok");
  noteCard->setProperty("cardId", QStringLiteral("notepad"));

  hsplit_ = new CardSplitter(Qt::Horizontal, root_);
  hsplit_->setHandleWidth(kSplitHandleW);
  hsplit_->addWidget(chatCard);
  hsplit_->addWidget(noteCard);
  hsplit_->setChildrenCollapsible(false);
  hsplit_->setStretchFactor(0, 3);
  hsplit_->setStretchFactor(1, 2);

  rl->addWidget(hsplit_, 1);

  setCentralWidget(root_);
  chat_->FocusInput();
}

void MainWindow::BuildDock() {
  dock_ = new DockBar(this);
  connect(dock_, &DockBar::HideChromeToggled, this, &MainWindow::ToggleChrome);
  connect(dock_, &DockBar::SettingsRequested, this, &MainWindow::OpenSettings);
  connect(dock_, &DockBar::AboutRequested, this, &MainWindow::ShowAbout);
  connect(dock_, &DockBar::ModuleRestoreRequested, this, &MainWindow::RestoreModule);
  dock_->raise();
}

void MainWindow::LayoutDock() {
  if (!dock_) return;
  // 把卡片列的纵向几何交给 dock，竖条与卡片上下沿严格齐平
  if (hsplit_ && hsplit_->isVisible()) {
    const QPoint tl = hsplit_->mapTo(this, QPoint(0, 0));
    dock_->SetAnchor(tl.y(), hsplit_->height());
  } else {
    dock_->Reposition();
  }
  dock_->raise();
}

void MainWindow::resizeEvent(QResizeEvent* e) {
  QMainWindow::resizeEvent(e);
  LayoutDock();
}

// 主题 token 里的玻璃色写成 rgba(r, g, b, a)，QColor 不认这种函数写法，这里自己拆。
// 返回无效色时由调用方用兜底色。
static QColor TokenColor(const QString& token) {
  const QString s = theme::Hex(token).trimmed();
  if (s.startsWith(QLatin1Char('#'))) return QColor(s);
  const int lp = s.indexOf(QLatin1Char('('));
  const int rp = s.lastIndexOf(QLatin1Char(')'));
  if (lp <= 0 || rp <= lp) return QColor();
  const QStringList parts = s.mid(lp + 1, rp - lp - 1).split(QLatin1Char(','));
  if (parts.size() < 3) return QColor();
  const int r = parts[0].trimmed().toInt();
  const int g = parts[1].trimmed().toInt();
  const int b = parts[2].trimmed().toInt();
  int a = 255;
  if (parts.size() >= 4) {
    const QString as = parts[3].trimmed();
    a = as.contains(QLatin1Char('.')) ? int(as.toDouble() * 255.0) : as.toInt();
  }
  return QColor(r, g, b, qBound(0, a, 255));
}

// 窗口背景全部由这里画。三条路线，按能力从高到低：
//   1) DWM 系统材质（亚克力）：DWM 每帧自己模糊窗口背后的真实内容，实时、走 GPU 合成，CPU 零开销；
//      这里只叠主题遮罩 + 一层光泽渐变。
//   2) 冻结的桌面快照磨砂（frost）：系统材质挂不上时的兜底，观感近似但不是实时。
//   3) 主题实色底：兜底中的兜底。
void MainWindow::paintEvent(QPaintEvent* e) {
  QPainter p(this);
  const QRect r = e->rect();
  const QColor solid = TokenColor(QStringLiteral("bg"));
  if (wd::glass::Enabled()) {
    // 关键：窗口像素带 alpha，脏区必须先刷成完全透明，再叠遮罩。
    // 少了这一步，新一帧盖在旧像素上，dock 展开/收起就会拖出一片残影。
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(r, Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QColor tint = TokenColor(QStringLiteral("glass"));
    if (!tint.isValid()) tint = QColor(0, 0, 0, 96);
    p.fillRect(r, tint);

    // 玻璃有厚度：顶上一道反光、底部压一点暗，平面才不会显得像纸
    const bool light = theme::IsLightActive();
    QLinearGradient gloss(0, 0, 0, height());
    gloss.setColorAt(0.0, QColor(255, 255, 255, light ? 44 : 26));
    gloss.setColorAt(0.42, QColor(255, 255, 255, 0));
    gloss.setColorAt(1.0, QColor(0, 0, 0, light ? 8 : 20));
    p.fillRect(r, gloss);
    return;
  }
  if (wd::frost::Ready()) {
    wd::frost::Paint(this, p, r);
    QColor tint = TokenColor(QStringLiteral("glass"));
    if (!tint.isValid()) tint = solid;
    p.fillRect(r, tint);
    wd::frost::PaintGrain(p, r);

    // 玻璃有厚度：顶上一道反光、底部压一点暗，平面才不会显得像纸
    const bool light = theme::IsLightActive();
    QLinearGradient gloss(0, 0, 0, height());
    gloss.setColorAt(0.0, QColor(255, 255, 255, light ? 54 : 32));
    gloss.setColorAt(0.42, QColor(255, 255, 255, 0));
    gloss.setColorAt(1.0, QColor(0, 0, 0, light ? 10 : 26));
    p.fillRect(r, gloss);
  } else {
    p.fillRect(r, solid.isValid() ? solid : palette().window().color());
  }
}

void MainWindow::showEvent(QShowEvent* e) {
  QMainWindow::showEvent(e);
  LayoutDock();
}

void MainWindow::ToggleChrome() { SetChromeHidden(!chromeHidden_); }

void MainWindow::SetChromeHidden(bool hide) {
  chromeHidden_ = hide;
  if (top_) top_->setVisible(!hide);
  for (QWidget* h : chromeHeads_) h->setVisible(!hide);

  // 隐藏边框时把四周内边距一起收紧，内容铺满窗口
  if (auto* v = qobject_cast<QVBoxLayout*>(root_->layout())) {
    v->setContentsMargins(hide ? 0 : 10, hide ? 0 : 8, hide ? 0 : (DockBar::StripWidth()),
                          hide ? 0 : 8);
    v->setSpacing(hide ? 0 : 8);
  }
  for (QWidget* c : cards_) {
    if (auto* v = qobject_cast<QVBoxLayout*>(c->layout()))
      v->setContentsMargins(hide ? 8 : 10, hide ? 8 : 8, hide ? 8 : 10, hide ? 8 : 10);
  }
  if (dock_) dock_->SetBorderHidden(hide);
  if (hide && dock_) dock_->Collapse();
  LayoutDock();
}

void MainWindow::RestoreLayout() {
  QSettings st(LayoutIniPath(), QSettings::IniFormat);
  const QByteArray h = st.value("layout/hsplit").toByteArray();
  if (!h.isEmpty()) hsplit_->restoreState(h);
  // 布局记忆可能来自不同分辨率或异常退出，尺寸不合理就回落到默认，避免某侧被压成 0
  if (hsplit_->sizes().value(0) < 320 || hsplit_->sizes().value(1) < 320)
    hsplit_->setSizes({900, 520});
  // 模块左右顺序也记下来，下次启动保持用户拖出来的排列
  const QString order = st.value("layout/order", QStringLiteral("chat,notepad")).toString();
  ApplyCardOrder(order.split(',', Qt::SkipEmptyParts));
  // 隐藏的模块也要一起恢复，否则重启后界面和上次关窗时长得不一样
  hiddenIds_ = st.value(QStringLiteral("layout/hidden")).toString().split(',', Qt::SkipEmptyParts);
  for (const QString& id : hiddenIds_) {
    if (QWidget* c = CardById(id)) c->hide();
  }
  SyncDockModules();
  const QByteArray geo = st.value("layout/geometry").toByteArray();
  if (!geo.isEmpty()) restoreGeometry(geo);
  if (width() < 900 || height() < 600) resize(1440, 920);
  LayoutDock();
}

void MainWindow::SaveLayout() {
  QSettings st(LayoutIniPath(), QSettings::IniFormat);
  st.setValue("layout/hsplit", hsplit_->saveState());
  st.setValue("layout/order", CardOrder());
  st.setValue("layout/hidden", hiddenIds_.join(QLatin1Char(',')));
  st.setValue("layout/geometry", saveGeometry());
}

// ------------------------------------------------------------------ 模块换位
QString MainWindow::CardOrder() const {
  QStringList ids;
  for (int i = 0; i < hsplit_->count(); ++i) {
    if (QWidget* w = hsplit_->widget(i))
      ids << w->property("cardId").toString();
  }
  return ids.join(QLatin1Char(','));
}

void MainWindow::ApplyCardOrder(const QStringList& order) {
  if (!hsplit_ || order.size() != hsplit_->count()) return;
  // 按目标顺序重排：先全部摘下来，再按顺序插回去
  QList<QWidget*> want;
  for (const QString& id : order) {
    QWidget* found = nullptr;
    for (int i = 0; i < hsplit_->count(); ++i) {
      QWidget* w = hsplit_->widget(i);
      if (w && w->property("cardId").toString() == id) {
        found = w;
        break;
      }
    }
    if (found) want << found;
  }
  if (want.size() != hsplit_->count()) return;
  const QList<int> sizes = hsplit_->sizes();
  for (QWidget* w : want) w->setParent(root_);
  for (int i = 0; i < want.size(); ++i) hsplit_->insertWidget(i, want[i]);
  hsplit_->setSizes(sizes);
}

QWidget* MainWindow::CardAtGlobal(const QPoint& gp) const {
  for (QWidget* c : cards_) {
    if (!c || !c->isVisible()) continue;
    if (c->rect().contains(c->mapFromGlobal(gp))) return c;
  }
  return nullptr;
}

void MainWindow::SetMoveHighlight(QWidget* card) {
  if (dropCard_ == card) return;
  if (dropCard_) {
    dropCard_->setProperty("moveHi", false);
    Repolish(dropCard_);
  }
  dropCard_ = card;
  if (dropCard_) {
    dropCard_->setProperty("moveHi", true);
    Repolish(dropCard_);
  }
}

void MainWindow::MoveCard(QWidget* from, QWidget* to) {
  if (!hsplit_ || !from || !to || from == to) return;
  const int iFrom = hsplit_->indexOf(from);
  const int iTo = hsplit_->indexOf(to);
  if (iFrom < 0 || iTo < 0) return;
  const QList<int> before = hsplit_->sizes();
  from->setParent(root_);  // 先摘下来，避免 insertWidget 的索引语义歧义
  hsplit_->insertWidget(iTo, from);
  // 宽度跟着模块一起走：先摆回交换前的尺寸，再平滑滑到交换后的尺寸，
  // 视觉上是两块互相让位，而不是瞬间对调
  QList<int> target = before;
  if (iFrom < target.size() && iTo < target.size()) qSwap(target[iFrom], target[iTo]);
  AnimateSizes(before, target);
  LayoutDock();
}

void MainWindow::AnimateSizes(const QList<int>& from, const QList<int>& to) {
  if (!hsplit_ || from.size() != to.size()) return;
  if (sizeAnim_) {
    sizeAnim_->stop();
    sizeAnim_->deleteLater();
    sizeAnim_ = nullptr;
  }
  hsplit_->setSizes(from);
  auto* a = new QVariantAnimation(hsplit_);
  a->setDuration(190);
  a->setEasingCurve(QEasingCurve::OutCubic);
  a->setStartValue(0.0);
  a->setEndValue(1.0);
  connect(a, &QVariantAnimation::valueChanged, this, [this, from, to](const QVariant& v) {
    const double t = v.toDouble();
    QList<int> cur;
    for (int i = 0; i < from.size(); ++i) cur << int(from[i] + (to[i] - from[i]) * t);
    if (hsplit_) hsplit_->setSizes(cur);
  });
  connect(a, &QVariantAnimation::finished, this, [this, a]() {
    if (sizeAnim_ == a) sizeAnim_ = nullptr;
    a->deleteLater();
  });
  sizeAnim_ = a;
  a->start();
}

// ------------------------------------------------------------------ 模块隐藏
QWidget* MainWindow::CardById(const QString& id) const {
  for (QWidget* c : cards_) {
    if (c && c->property("cardId").toString() == id) return c;
  }
  return nullptr;
}

int MainWindow::VisibleCardCount() const {
  int n = 0;
  for (QWidget* c : cards_) {
    if (c && c->isVisible()) ++n;
  }
  return n;
}

void MainWindow::HideModule(QWidget* card) {
  if (!card) return;
  if (VisibleCardCount() <= 1) {
    statusBar()->showMessage(QStringLiteral("至少保留一个模块"), 2500);
    return;
  }
  const QString id = card->property("cardId").toString();
  if (id.isEmpty()) return;
  card->hide();
  if (!hiddenIds_.contains(id)) hiddenIds_ << id;
  SyncDockModules();
  SaveLayout();
  LayoutDock();
}

void MainWindow::RestoreModule(const QString& id) {
  QWidget* card = CardById(id);
  if (!card) return;
  hiddenIds_.removeAll(id);
  card->show();
  // 刚回来的模块可能被压成一条缝，按原比例重新摊开
  QList<int> sz = hsplit_->sizes();
  if (sz.size() == 2 && (sz.value(0) < 120 || sz.value(1) < 120)) {
    const int total = (sz.value(0) + sz.value(1)) > 400 ? (sz.value(0) + sz.value(1)) : 1320;
    hsplit_->setSizes({total * 3 / 5, total * 2 / 5});
  }
  SyncDockModules();
  SaveLayout();
  LayoutDock();
}

void MainWindow::SyncDockModules() {
  if (!dock_) return;
  QList<QPair<QString, QString>> mods;
  for (QWidget* c : cards_) {
    if (!c) continue;
    const QString id = c->property("cardId").toString();
    if (!hiddenIds_.contains(id)) continue;
    QString title = id;
    if (QLabel* t = c->findChild<QLabel*>(QStringLiteral("ModuleTitle"))) title = t->text();
    mods << qMakePair(id, title);
  }
  dock_->SetModules(mods);
}

// ------------------------------------------------------------------ 拖动拖影
void MainWindow::BeginGhost(QWidget* card, const QPoint& gp) {
  EndGhost();
  if (!card) return;
  QPixmap pm = card->grab();
  if (pm.isNull()) return;
  const int w = qBound(160, card->width() * 3 / 5, 260);
  pm = pm.scaledToWidth(w, Qt::SmoothTransformation);
  ghost_ = new DragGhost(pm);
  ghost_->show();
  MoveGhost(gp);
}

void MainWindow::MoveGhost(const QPoint& gp) {
  if (!ghost_) return;
  ghost_->move(gp.x() - ghost_->width() / 2, gp.y() - 22);
}

void MainWindow::EndGhost() {
  if (!ghost_) return;
  ghost_->hide();
  ghost_->deleteLater();
  ghost_ = nullptr;
}

// 标题栏拖动：按下记录起点，移动超过阈值进入拖动态，松手落在哪个模块上就换位
bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
  auto* head = qobject_cast<QWidget*>(obj);
  if (head && head->objectName() == QLatin1String("ModuleHead")) {
    QWidget* card = head->property("card").value<QWidget*>();
    switch (ev->type()) {
      case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton && card) {
          dragCard_ = card;
          dragStart_ = me->globalPosition().toPoint();
          dragging_ = false;
        }
        break;
      }
      case QEvent::MouseMove: {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (!dragCard_ || !(me->buttons() & Qt::LeftButton)) break;
        const QPoint gp = me->globalPosition().toPoint();
        if (!dragging_ && (gp - dragStart_).manhattanLength() > 6) {
          dragging_ = true;
          head->setCursor(Qt::ClosedHandCursor);
          BeginGhost(dragCard_, gp);
          dragCard_->setProperty("moveSrc", true);
          Repolish(dragCard_);
        }
        if (dragging_) {
          MoveGhost(gp);
          SetMoveHighlight(CardAtGlobal(gp));
        }
        break;
      }
      case QEvent::MouseButtonRelease: {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() != Qt::LeftButton) break;
        head->setCursor(Qt::OpenHandCursor);
        QWidget* target = dragging_ ? CardAtGlobal(me->globalPosition().toPoint()) : nullptr;
        const bool wasDrag = dragging_ && target && target != dragCard_;
        QWidget* from = dragCard_;
        dragCard_ = nullptr;
        dragging_ = false;
        EndGhost();
        SetMoveHighlight(nullptr);
        if (from && from->property("moveSrc").toBool()) {
          from->setProperty("moveSrc", false);
          Repolish(from);
        }
        if (wasDrag) MoveCard(from, target);
        break;
      }
      case QEvent::Leave:
        if (!dragging_ && head) head->setCursor(Qt::OpenHandCursor);
        break;
      default:
        break;
    }
  }
  return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::OpenSettings() {
  SettingsDialog dlg(&cfg_, this);
  dlg.exec();
  if (chat_) chat_->RefreshModelInfo();
}

void MainWindow::ShowAbout() {
  QMessageBox::about(
      this, QStringLiteral("关于 WorkDesk"),
      QStringLiteral("<b>WorkDesk（Qt 版）</b><br><br>"
                     "从 ImGui + GLFW 自绘版本移植到 Qt Widgets，界面交互交给 Qt 原生控件。<br>"
                     "数据层（配置、会话、SSE 流式、代码高亮规则）与原版保持一致。<br><br>"
                     "配置：%APPDATA%\\WorkDesk\\config.json<br>"
                     "会话：%APPDATA%\\WorkDesk\\chats\\*.json"));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
  if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
  if (!e->mimeData()->hasUrls()) return;
  e->acceptProposedAction();
  HandleDropUrls(e->mimeData()->urls());
}

void MainWindow::OpenFiles(const QStringList& paths) {
  if (paths.isEmpty()) return;
  RoutePaths(paths, false);
}

void MainWindow::HandleDropUrls(const QList<QUrl>& urls, QWidget* target) {
  QStringList paths;
  for (const QUrl& u : urls) {
    if (u.isLocalFile()) paths << u.toLocalFile();
  }
  ShowDropHint(false);
  if (paths.isEmpty()) return;

  // 落点在聊天卡片里就一律当作"发给 AI"，不再被记事本截走
  QWidget* hit = target ? target : QApplication::widgetAt(QCursor::pos());
  bool preferChat = false;
  if (chat_ && hit) {
    preferChat = (hit == chat_ || chat_->isAncestorOf(hit) || hit->isAncestorOf(chat_) ||
                  chat_->isAncestorOf(hit->window()));
  }
  RoutePaths(paths, preferChat);
}

// 拖放反馈只做边框高亮，不再弹任何文字提示
void MainWindow::ShowDropHint(bool on) {
  for (QWidget* c : cards_) {
    c->setProperty("dropHi", on);
    Repolish(c);
  }
}

void MainWindow::RoutePaths(const QStringList& paths, bool preferChat) {
  QStringList toChat;
  QStringList toNote;
  for (const QString& p : paths) {
    QFileInfo fi(p);
    if (!fi.exists()) continue;
    if (fi.isDir()) continue;
    if (preferChat || !IsTextPath(p))
      toChat << p;
    else
      toNote << p;
  }
  if (!toNote.isEmpty()) {
    if (notepad_->HasUnsaved() && !notepad_->SaveIfDirty()) return;
    if (!notepad_->OpenPath(toNote.first())) {
      toChat << toNote;
    } else if (toNote.size() > 1) {
      for (int i = 1; i < toNote.size(); ++i) toChat << toNote[i];
    }
  }
  if (!toChat.isEmpty()) chat_->AttachPaths(toChat);
}

void MainWindow::closeEvent(QCloseEvent* e) {
  if (notepad_ && notepad_->HasUnsaved()) {
    if (!notepad_->SaveIfDirty()) {
      e->ignore();
      return;
    }
  }
  SaveLayout();
  e->accept();
}

}  // namespace wd
