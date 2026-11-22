#include "tooltip.h"

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QScreen>
#include <QTimer>
#include <QWidget>

namespace wd {
namespace {

constexpr int kPadX = 10;         // 文字左右内边距
constexpr int kPadY = 6;          // 文字上下内边距
constexpr int kRadius = 7;        // 圆角
constexpr int kMaxTextW = 420;    // 超过这个宽度就折行
constexpr int kShowMs = 8000;     // 无操作 8 秒自动收起
constexpr int kLeaveGrace = 160;  // 刚弹出的这一小段时间内，leave 不算数

// 自绘气泡：顶层工具窗 + 不吃鼠标 + 不抢焦点。
// 颜色每次绘制时从 QApplication::palette() 现取，所以深/浅主题切换后立刻跟着变。
class TipBubble : public QWidget {
 public:
  TipBubble()
      : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput |
                             Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus |
                             Qt::NoDropShadowWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(kShowMs);
    connect(timer_, &QTimer::timeout, this, &QWidget::hide);
  }

  bool Fresh() const { return QDateTime::currentMSecsSinceEpoch() - shownAt_ < kLeaveGrace; }

  void ShowAt(const QPoint& global, const QString& text) {
    text_ = text;
    QFont f = QApplication::font();
    f.setPointSizeF(9.5);
    setFont(f);

    const QFontMetrics fm(f);
    const QRect box = fm.boundingRect(QRect(0, 0, kMaxTextW, 1000), Qt::TextWordWrap, text_);
    const int w = qMin(box.width(), kMaxTextW) + 2 * kPadX + 2;
    const int h = box.height() + 2 * kPadY + 2;
    resize(w, h);

    // 默认贴光标右下；贴边出屏就翻到另一侧，保证整块都在屏幕可用区域内
    QPoint pos = global + QPoint(14, 20);
    const QScreen* scr = QGuiApplication::screenAt(global);
    if (!scr) scr = QGuiApplication::primaryScreen();
    if (scr) {
      const QRect area = scr->availableGeometry();
      if (pos.x() + w > area.right()) pos.setX(qMax(area.left(), global.x() - w - 10));
      if (pos.y() + h > area.bottom()) pos.setY(qMax(area.top(), global.y() - h - 10));
    }
    move(pos);
    show();
    raise();
    shownAt_ = QDateTime::currentMSecsSinceEpoch();
    update();
    timer_->start();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    const QPalette pal = QApplication::palette();
    QColor bg = pal.color(QPalette::ToolTipBase);
    QColor fg = pal.color(QPalette::ToolTipText);
    QColor line = pal.color(QPalette::Mid);
    if (bg.lightness() < 128) {  // 深色主题：ToolTipBase 与卡片同色，提亮一点才不糊在背景里
      bg = bg.lighter(124);
      line = line.lighter(132);
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, kRadius, kRadius);
    p.fillPath(path, bg);
    p.setPen(line);
    p.drawPath(path);

    p.setPen(fg);
    p.setFont(font());
    p.drawText(rect().adjusted(kPadX, kPadY, -kPadX, -kPadY),
               Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter, text_);
  }

 private:
  QString text_;
  QTimer* timer_ = nullptr;
  qint64 shownAt_ = 0;
};

TipBubble* Bubble() {
  static TipBubble* b = new TipBubble();  // 顶层窗口，随进程活到退出
  return b;
}

// 全局事件过滤器：吃掉 ToolTip 事件自己弹气泡；离开 / 按下 / 滚轮 / 按键时收起
class TipFilter : public QObject {
 public:
  bool eventFilter(QObject* o, QEvent* e) override {
    switch (e->type()) {
      case QEvent::ToolTip: {
        auto* w = qobject_cast<QWidget*>(o);
        if (!w) return false;
        const QString text = w->toolTip();
        if (text.isEmpty()) return false;
        auto* he = static_cast<QHelpEvent*>(e);
        Bubble()->ShowAt(he->globalPos(), text);
        return true;  // 吃掉，避免原生 QToolTip 再弹一个黑底气泡
      }
      case QEvent::Leave:
        if (!Bubble()->Fresh()) Bubble()->hide();
        return false;
      case QEvent::MouseButtonPress:
      case QEvent::Wheel:
      case QEvent::KeyPress:
      case QEvent::Hide:
        Bubble()->hide();
        return false;
      default:
        return false;
    }
  }
};

}  // namespace

void InstallTooltips() {
  static TipFilter* filter = nullptr;
  if (filter) return;
  filter = new TipFilter();
  qApp->installEventFilter(filter);
}

}  // namespace wd