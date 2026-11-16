#pragma once
#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

class QFrame;
class QLabel;
class QToolButton;
class QTimer;
class QPropertyAnimation;
class QVBoxLayout;

namespace wd {

// 右侧 dock 栏：默认收起成一条极窄的竖条，鼠标移上去展开，移开自动收回。
// 时钟、隐藏界面、设置、关于全部收进这里，主界面不再放任何文字提示。
class DockBar : public QWidget {
  Q_OBJECT
  Q_PROPERTY(int panelWidth READ PanelWidth WRITE SetPanelWidth)

 public:
  explicit DockBar(QWidget* parent = nullptr);

  bool IsCollapsed() const { return collapsed_; }
  void Expand();
  void Collapse();
  int PanelWidth() const;

  // 主窗口把"卡片列"（对话/记事本所在区域）的纵向几何传进来，
  // 让收起的竖条与卡片上下沿严格齐平，看起来像卡片的把手而不是浮在旁边的碎块。
  void SetAnchor(int top, int height);
  // 边框隐藏状态变化时同步按钮文案（隐藏边框 / 显示边框）
  void SetBorderHidden(bool hidden);
  // 模块被隐藏后，把「还原入口」列进面板：传 [id, 标题] 列表，空列表表示没有隐藏模块
  void SetModules(const QList<QPair<QString, QString>>& modules);
  static int StripWidth();

  // 父窗口尺寸变化后由主窗口调用，重新贴到右边缘
  void Reposition();

 signals:
  void HideChromeToggled();
  void ModuleRestoreRequested(const QString& id);
  void SettingsRequested();
  void AboutRequested();

 protected:
  void enterEvent(QEnterEvent* e) override;
  void leaveEvent(QEvent* e) override;

 private:
  void SetPanelWidth(int w);
  void BuildUi();
  void TickClock();

  QWidget* strip_ = nullptr;
  QWidget* panel_ = nullptr;
  QVBoxLayout* panelLay_ = nullptr;
  QLabel* time_ = nullptr;
  QLabel* date_ = nullptr;
  QLabel* mark_ = nullptr;
  QToolButton* hideBtn_ = nullptr;
  QToolButton* borderIcon_ = nullptr;
  QFrame* modSep_ = nullptr;
  QWidget* modBox_ = nullptr;
  QVBoxLayout* modLay_ = nullptr;
  QTimer* clock_ = nullptr;
  QTimer* hideTimer_ = nullptr;
  QPropertyAnimation* anim_ = nullptr;
  int anchorTop_ = 0;
  int anchorH_ = 0;
  bool collapsed_ = true;
  bool animating_ = false;
};

}  // namespace wd
