#pragma once
#include <QHash>
#include <QObject>
#include <QSet>

class QAbstractScrollArea;
class QEvent;
class QScrollBar;
class QTimer;

namespace wd {

// 全局平滑滚动：装在 QApplication 上的事件过滤器统一接管滚轮，不直接把滚动条跳到目标，
// 而是每 16ms 按指数逼近推一帧；一格滚轮 ≈ 系统滚动行数（默认 3 行），Shift 横向，
// 触控板按像素位移走，Ctrl+滚轮放行（留给字号缩放）。
// 外部（自动贴底、拖动滚动条、键盘翻页）改过滚动条值时立即收手，不和程序自己的定位逻辑抢。
class SmoothScroll : public QObject {
  Q_OBJECT
 public:
  static SmoothScroll* Install();

 signals:
  // 滚轮被这里接管后，控件自己收不到 Wheel；需要感知滚轮的面板（如聊天区自动贴底）连这个信号。
  // notches > 0 向上，< 0 向下。
  void wheeled(QAbstractScrollArea* area, int notches);

 protected:
  bool eventFilter(QObject* obj, QEvent* ev) override;

 private:
  explicit SmoothScroll(QObject* parent = nullptr);
  void Push(QScrollBar* bar, int delta);
  void Tick();

  QHash<QScrollBar*, int> targets_;  // 目标值
  QHash<QScrollBar*, int> lastSet_;  // 上一帧我们自己写进去的值，用来识别外部改动
  QSet<QScrollBar*> watched_;        // 已挂 destroyed 清理的滚动条，控件销毁时不留野指针
  QTimer* tick_ = nullptr;
};

}  // namespace wd
