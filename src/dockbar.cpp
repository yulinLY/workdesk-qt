#include "dockbar.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>

#include <QDateTime>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLayoutItem>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QProxyStyle>
#include <QStyleOption>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "theme.h"

namespace wd {
namespace {

constexpr int kStripW = 12;     // 收起状态露出的窄条宽度
constexpr int kContentW = 132;  // 展开后的内容宽度（只够放时间、图标和一列模块还原入口）
constexpr int kFullW = kStripW + kContentW;
constexpr int kEdgeGap = 2;     // 距窗口右边缘的缝隙，贴边放，鼠标一靠就命中

QToolButton* MakeDockButton(const QString& text, const QString& iconPath, QWidget* parent) {
  auto* b = new QToolButton(parent);
  b->setObjectName("DockBtn");
  b->setText(text);
  b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  b->setIcon(QIcon(iconPath));
  b->setIconSize(QSize(13, 13));
  b->setCursor(Qt::PointingHandCursor);
  b->setMinimumHeight(24);
  b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  return b;
}

// 主题按钮：只显示当前档位文字，不挂 QToolButton 的 menu，因此 Qt 从头到尾不会画下拉三角；
// 点击时自己把菜单弹在按钮下方，菜单里用蓝点标当前项。
class ThemeMenuButton : public QToolButton {
 public:
  explicit ThemeMenuButton(QWidget* parent) : QToolButton(parent) {
    setObjectName(QStringLiteral("DockBtn"));
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(24);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void SetDropMenu(QMenu* menu) {
    dropMenu_ = menu;
    connect(this, &QToolButton::clicked, this, [this]() { PopupDropMenu(); });
  }

 private:
  void PopupDropMenu() {
    if (!dropMenu_) return;
    if (dropMenu_->isVisible()) {
      dropMenu_->hide();
      return;
    }
    const QPoint pos = mapToGlobal(QPoint(0, height() + 2));
    dropMenu_->exec(pos);
  }

  QMenu* dropMenu_ = nullptr;
};

// 主题菜单的选中标记：默认那枚勾（✓）换成一颗蓝点，位置仍由样式给出的
// check 列矩形决定，所以和文字天然垂直居中，不会像自绘三角那样歪。
class DotMenuStyle : public QProxyStyle {
 public:
  void drawPrimitive(PrimitiveElement e, const QStyleOption* opt, QPainter* p,
                     const QWidget* w) const override {
    if (e == PE_IndicatorMenuCheckMark) {
      if (opt->state & State_On) {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(theme::Hex(QStringLiteral("accent"))));
        const QRectF r = QRectF(opt->rect);
        const qreal d = 8.0;
        p->drawEllipse(QRectF(r.center().x() - d / 2.0, r.center().y() - d / 2.0, d, d));
        p->restore();
      }
      return;
    }
    QProxyStyle::drawPrimitive(e, opt, p, w);
  }
};

}  // namespace

DockBar::DockBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("DockBar"));
  setAttribute(Qt::WA_StyledBackground, true);
  setMouseTracking(true);
  setCursor(Qt::ArrowCursor);

  BuildUi();

  anim_ = new QPropertyAnimation(this, "panelWidth", this);
  anim_->setDuration(170);
  anim_->setEasingCurve(QEasingCurve::OutCubic);

  // 鼠标离开后延迟收起，避免在条目之间移动时抖动
  hideTimer_ = new QTimer(this);
  hideTimer_->setSingleShot(true);
  hideTimer_->setInterval(280);
  connect(hideTimer_, &QTimer::timeout, this, [this]() {
    const QPoint p = mapFromGlobal(QCursor::pos());
    if (rect().contains(p)) return;
    Collapse();
  });

  clock_ = new QTimer(this);
  clock_->setInterval(1000);
  connect(clock_, &QTimer::timeout, this, &DockBar::TickClock);
  clock_->start();
  TickClock();

  SetPanelWidth(kStripW);
}

void DockBar::BuildUi() {
  // 收起时唯一可见的东西：一条圆角细竖条，不放任何文字
  strip_ = new QWidget(this);
  strip_->setObjectName("DockStrip");
  strip_->setGeometry(0, 0, kStripW, height());
  auto* sl = new QVBoxLayout(strip_);
  // 细竖线从头到尾铺满卡片高度，不再上下留白，
  // 否则竖线两端和模块边框之间会露出一截空隙，看着像悬空断开的
  sl->setContentsMargins(0, 0, 0, 0);
  sl->setSpacing(0);
  mark_ = new QLabel(strip_);
  mark_->setObjectName("DockMark");
  mark_->setFixedWidth(3);
  mark_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  sl->addWidget(mark_, 1, Qt::AlignHCenter);

  // 展开面板：内容宽度固定，靠 dock 整体宽度滑入，右侧超出部分由窗口裁掉
  panel_ = new QWidget(this);
  panel_->setObjectName("DockPanel");
  panelLay_ = new QVBoxLayout(panel_);
  panelLay_->setContentsMargins(9, 9, 9, 9);
  panelLay_->setSpacing(6);

  time_ = new QLabel(panel_);
  time_->setObjectName("DockTime");
  panelLay_->addWidget(time_);

  date_ = new QLabel(panel_);
  date_->setObjectName("DockDate");
  panelLay_->addWidget(date_);

  auto* hideBtn = MakeDockButton(QStringLiteral("隐藏边框"), QStringLiteral(":/assets/chrome.ico"), panel_);
  connect(hideBtn, &QToolButton::clicked, this, &DockBar::HideChromeToggled);
  panelLay_->addWidget(hideBtn);
  hideBtn_ = hideBtn;

  auto* setBtn = MakeDockButton(QStringLiteral("设置"), QStringLiteral(":/assets/settings.ico"), panel_);
  connect(setBtn, &QToolButton::clicked, this, &DockBar::SettingsRequested);
  panelLay_->addWidget(setBtn);

  auto* aboutBtn = MakeDockButton(QStringLiteral("关于"), QStringLiteral(":/assets/about.ico"), panel_);
  connect(aboutBtn, &QToolButton::clicked, this, &DockBar::AboutRequested);
  panelLay_->addWidget(aboutBtn);

  // 主题：按钮上直接显示当前档位，点开选四档（深色 / 亮色 / 跟随系统 / 跟随时间）
  themeBtn_ = new ThemeMenuButton(panel_);
  themeBtn_->setToolTip(QStringLiteral("界面主题"));
  themeMenu_ = new QMenu(themeBtn_);
  themeMenu_->setStyle(new DotMenuStyle);
  auto* group = new QActionGroup(themeMenu_);
  group->setExclusive(true);
  const theme::Mode kModes[] = {theme::Mode::Dark, theme::Mode::Light, theme::Mode::System,
                                theme::Mode::Time};
  for (theme::Mode m : kModes) {
    QAction* a = themeMenu_->addAction(theme::ModeLabel(m));
    a->setCheckable(true);
    a->setActionGroup(group);
    a->setData(static_cast<int>(m));
    connect(a, &QAction::triggered, this, [this, m]() {
      theme::ThemeManager::Instance().SetMode(m);
      SyncThemeButton();
    });
  }
  connect(themeMenu_, &QMenu::aboutToShow, this, &DockBar::SyncThemeButton);
  static_cast<ThemeMenuButton*>(themeBtn_)->SetDropMenu(themeMenu_);
  panelLay_->addWidget(themeBtn_);
  SyncThemeButton();

  // 第二条分隔线：其下只放一个纯图标按钮，点一下隐藏边框，再点一下恢复
  auto* sep2 = new QFrame(panel_);
  sep2->setObjectName("DockSep");
  sep2->setFixedHeight(1);
  panelLay_->addWidget(sep2);

  auto* row = new QWidget(panel_);
  row->setStyleSheet("background:transparent;");
  auto* rl = new QHBoxLayout(row);
  rl->setContentsMargins(0, 0, 0, 0);
  rl->setSpacing(0);
  borderIcon_ = new QToolButton(row);
  borderIcon_->setObjectName("DockIconBtn");
  borderIcon_->setIcon(QIcon(QStringLiteral(":/assets/chrome.ico")));
  borderIcon_->setIconSize(QSize(14, 14));
  borderIcon_->setFixedSize(26, 24);
  borderIcon_->setCursor(Qt::PointingHandCursor);
  borderIcon_->setToolTip(QStringLiteral("隐藏边框"));
  connect(borderIcon_, &QToolButton::clicked, this, &DockBar::HideChromeToggled);
  rl->addWidget(borderIcon_, 0, Qt::AlignLeft);
  rl->addStretch(1);
  panelLay_->addWidget(row);

  // 模块还原区：哪个模块被隐藏了，这里就出现一行它的名字，点一下回到界面
  modSep_ = new QFrame(panel_);
  modSep_->setObjectName("DockSep");
  modSep_->setFixedHeight(1);
  panelLay_->addWidget(modSep_);

  modBox_ = new QWidget(panel_);
  modBox_->setStyleSheet("background:transparent;");
  modLay_ = new QVBoxLayout(modBox_);
  modLay_->setContentsMargins(0, 0, 0, 0);
  modLay_->setSpacing(3);
  panelLay_->addWidget(modBox_);
  modSep_->setVisible(false);
  modBox_->setVisible(false);

  panelLay_->addStretch(1);

  panel_->setGeometry(kStripW, 0, kContentW, height());
}

int DockBar::PanelWidth() const { return width(); }

void DockBar::SetPanelWidth(int w) {
  if (w < kStripW) w = kStripW;
  if (w > kFullW) w = kFullW;
  const int h = height();
  const int px = parentWidget() ? parentWidget()->width() - w - kEdgeGap : x();
  const QRect before = geometry();
  setGeometry(px, y(), w, h);
  strip_->setGeometry(0, 0, kStripW, h);
  panel_->setGeometry(kStripW, 0, kContentW, h);
  // dock 挂在带 alpha 的主窗口上：变窄/变宽后，它原先占的那块像素没人覆盖，
  // 半透明窗口下就会留下上一次的面板图像（残影）。让父窗口把「旧区域 + 新区域」
  // 一次重画掉即可，代价只是一次 fillRect，动画 170ms 全程也就几十次。
  if (QWidget* p = parentWidget()) p->update(before.united(geometry()));
}

void DockBar::Reposition() {
  QWidget* p = parentWidget();
  if (!p) return;
  const int w = width() > 0 ? width() : kStripW;
  // 高度优先跟卡片列对齐：竖条与模块上下沿齐平，视觉上连成一体
  int h = anchorH_ > 0 ? anchorH_ : (panel_ ? panel_->sizeHint().height() : 260);
  const int contentMin = panel_ ? panel_->sizeHint().height() : 200;
  if (h < contentMin) h = contentMin;
  const int top = anchorTop_ > 0 ? anchorTop_ : 52;
  const int maxH = p->height() - top - 8;
  if (h > maxH) h = maxH;
  if (h < 120) h = 120;
  setGeometry(p->width() - w - kEdgeGap, top, w, h);
  strip_->setGeometry(0, 0, kStripW, height());
  panel_->setGeometry(kStripW, 0, kContentW, height());
}

void DockBar::SetAnchor(int top, int height) {
  anchorTop_ = top;
  anchorH_ = height;
  Reposition();
}

void DockBar::SetModules(const QList<QPair<QString, QString>>& modules) {
  if (!modLay_) return;
  while (QLayoutItem* it = modLay_->takeAt(0)) {
    if (QWidget* w = it->widget()) w->deleteLater();
    delete it;
  }
  for (const QPair<QString, QString>& m : modules) {
    auto* b = new QToolButton(modBox_);
    b->setObjectName(QStringLiteral("DockModBtn"));
    b->setText(m.second);
    b->setToolTip(QStringLiteral("显示 %1").arg(m.second));
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(22);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const QString id = m.first;
    connect(b, &QToolButton::clicked, this, [this, id]() { emit ModuleRestoreRequested(id); });
    modLay_->addWidget(b);
  }
  const bool has = !modules.isEmpty();
  if (modSep_) modSep_->setVisible(has);
  if (modBox_) modBox_->setVisible(has);
  Reposition();
}

void DockBar::SyncThemeButton() {
  const theme::Mode mode = theme::ThemeManager::Instance().mode();
  if (themeBtn_) {
    themeBtn_->setText(theme::ModeLabel(mode));
    themeBtn_->setToolTip(QStringLiteral("界面主题：%1").arg(theme::ModeLabel(mode)));
  }
  if (themeMenu_) {
    const auto actions = themeMenu_->actions();
    for (QAction* a : actions) a->setChecked(a->data().toInt() == static_cast<int>(mode));
  }
}

void DockBar::SetBorderHidden(bool hidden) {
  const QString text = hidden ? QStringLiteral("显示边框") : QStringLiteral("隐藏边框");
  if (hideBtn_) hideBtn_->setText(text);
  if (borderIcon_) borderIcon_->setToolTip(text);
}

int DockBar::StripWidth() { return kStripW; }

void DockBar::Expand() {
  hideTimer_->stop();
  SyncThemeButton();
  if (!collapsed_ && !animating_) return;
  collapsed_ = false;
  animating_ = true;
  panel_->show();
  anim_->stop();
  anim_->setStartValue(width());
  anim_->setEndValue(kFullW);
  connect(anim_, &QPropertyAnimation::finished, this, [this]() { animating_ = false; },
          Qt::UniqueConnection);
  anim_->start();
}

void DockBar::Collapse() {
  if (collapsed_ && !animating_) return;
  collapsed_ = true;
  animating_ = true;
  anim_->stop();
  anim_->setStartValue(width());
  anim_->setEndValue(kStripW);
  connect(anim_, &QPropertyAnimation::finished, this,
          [this]() {
            animating_ = false;
            if (collapsed_) panel_->hide();
          },
          Qt::UniqueConnection);
  anim_->start();
}

void DockBar::enterEvent(QEnterEvent* e) {
  QWidget::enterEvent(e);
  Expand();
}

void DockBar::leaveEvent(QEvent* e) {
  QWidget::leaveEvent(e);
  if (!collapsed_) hideTimer_->start();
}

void DockBar::TickClock() {
  const QDateTime now = QDateTime::currentDateTime();
  static const char* kWeek[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
  time_->setText(now.toString(QStringLiteral("HH:mm")));
  date_->setText(QStringLiteral("%1月%2日 %3")
                     .arg(now.date().month())
                     .arg(now.date().day())
                     .arg(QString::fromUtf8(kWeek[now.date().dayOfWeek() - 1])));
}

}  // namespace wd
