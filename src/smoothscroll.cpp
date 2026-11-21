#include "smoothscroll.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QFontMetrics>
#include <QScrollBar>
#include <QTimer>
#include <QWidget>
#include <QWheelEvent>
#include <QtMath>

namespace wd {

SmoothScroll::SmoothScroll(QObject* parent) : QObject(parent) {
  tick_ = new QTimer(this);
  tick_->setInterval(16);
  connect(tick_, &QTimer::timeout, this, &SmoothScroll::Tick);
}

SmoothScroll* SmoothScroll::Install() {
  static SmoothScroll* inst = nullptr;
  if (!inst && qApp) {
    inst = new SmoothScroll(qApp);
    qApp->installEventFilter(inst);
  }
  return inst;
}

void SmoothScroll::Push(QScrollBar* bar, int delta) {
  if (!bar || delta == 0) return;
  // 滚动条会随控件销毁（切会话、关记事本），挂一次 destroyed 把残留项清掉，避免野指针
  if (!watched_.contains(bar)) {
    watched_.insert(bar);
    connect(bar, &QObject::destroyed, this, [this, bar]() {
      targets_.remove(bar);
      lastSet_.remove(bar);
      watched_.remove(bar);
    });
  }
  const int cur = bar->value();
  // 动画进行中就接着目标累加，连滚多格不会每次从头开始
  const int base = targets_.contains(bar) ? targets_.value(bar) : cur;
  const int target = qBound(bar->minimum(), base + delta, bar->maximum());
  if (target == cur) return;
  targets_[bar] = target;
  lastSet_[bar] = cur;
  if (!tick_->isActive()) tick_->start();
}

void SmoothScroll::Tick() {
  QList<QScrollBar*> done;
  for (auto it = targets_.constBegin(); it != targets_.constEnd(); ++it) {
    QScrollBar* bar = it.key();
    const int target = qBound(bar->minimum(), it.value(), bar->maximum());
    const int cur = bar->value();
    // 值被别人改过（自动贴底 / 拖动滚动条 / 键盘翻页），这一轮就放弃，不和程序自己的定位逻辑抢
    if (lastSet_.value(bar, cur) != cur) {
      done << bar;
      continue;
    }
    if (cur == target) {
      done << bar;
      continue;
    }
    int next = cur + qRound((target - cur) * 0.3);
    if (qAbs(target - cur) <= 2) next = target;
    if (next == cur) {
      done << bar;
      continue;
    }
    lastSet_[bar] = next;
    bar->setValue(next);
  }
  for (QScrollBar* bar : done) {
    targets_.remove(bar);
    lastSet_.remove(bar);
  }
  if (targets_.isEmpty()) tick_->stop();
}

bool SmoothScroll::eventFilter(QObject* obj, QEvent* ev) {
  if (ev->type() != QEvent::Wheel) return QObject::eventFilter(obj, ev);
  auto* we = static_cast<QWheelEvent*>(ev);
  // Ctrl+滚轮是缩放（记事本字号等），原样交给控件
  if (we->modifiers() & Qt::ControlModifier) return false;

  auto* w = qobject_cast<QWidget*>(obj);
  if (!w) return false;
  QAbstractScrollArea* area = nullptr;
  for (QWidget* p = w; p; p = p->parentWidget()) {
    if (auto* a = qobject_cast<QAbstractScrollArea*>(p)) {
      area = a;
      break;
    }
  }
  if (!area) return false;

  const QPoint ad = we->angleDelta();
  const QPoint pd = we->pixelDelta();
  if (ad.isNull() && pd.isNull()) return false;

  const bool shift = (we->modifiers() & Qt::ShiftModifier) != 0;
  const bool horiz = shift || (qAbs(ad.x()) > qAbs(ad.y()));

  QScrollBar* bar = horiz ? area->horizontalScrollBar() : area->verticalScrollBar();
  if (!bar || bar->maximum() <= bar->minimum()) {
    QScrollBar* other = horiz ? area->verticalScrollBar() : area->horizontalScrollBar();
    if (!other || other->maximum() <= other->minimum()) return false;
    bar = other;
  }

  // 一格滚轮 ≈ 系统设置的滚动行数（默认 3 行）。文本控件的 singleStep 常被设成 1，
  // 直接用会一格只挪 3 像素，退化时改用字体行高当单位；按 item 滚动的列表则保持以 item 计。
  int unit = bar->singleStep();
  bool perItem = false;
  if (auto* iv = qobject_cast<QAbstractItemView*>(area)) {
    perItem = (horiz ? iv->horizontalScrollMode() : iv->verticalScrollMode()) ==
              QAbstractItemView::ScrollPerItem;
  }
  if (!perItem && unit <= 1) {
    const QFontMetrics fm(area->font());
    unit = fm.lineSpacing() > 0 ? fm.lineSpacing() : 20;
  }
  if (unit <= 0) unit = 1;

  int lines = QApplication::wheelScrollLines();
  if (lines <= 0) lines = 3;

  int delta = 0;
  if (!pd.isNull()) {
    delta = -(horiz ? pd.x() : pd.y());  // 触控板：像素级，按位移走
  } else {
    const int notches = horiz ? ad.x() : ad.y();
    delta = -qRound(notches / 120.0 * lines * unit);
  }
  if (delta == 0) return false;

  // 纵向滚动先广播出去：某些面板（聊天区）要靠滚轮方向决定是否交出 / 恢复自动贴底
  if (!horiz) {
    int notches = ad.isNull() ? (pd.y() > 0 ? 1 : -1) : ad.y() / 120;
    if (notches == 0) notches = ad.y() > 0 ? 1 : -1;
    emit wheeled(area, notches);
  }

  Push(bar, delta);
  return true;  // 吃掉了，不让控件再跳一次
}

}  // namespace wd
