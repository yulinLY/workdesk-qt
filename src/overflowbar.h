#pragma once
#include <QHash>
#include <QList>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QMenu;
class QToolButton;

namespace wd {

// 一行「会自己收放」的工具条。
//
// 三个模块同时打开、卡片被拖窄时，各面板里的单行 QHBoxLayout 会把每个按钮压到
// minimumSizeHint 以下，表现就是「打开/保存/另存为」被挤成「丁开/阝弄/存」，
// 相邻按钮互相叠压。这里换一套规则：
//   - 任何时候都不把控件压到最小尺寸以下；
//   - 宽度不够时，按优先级把次要按钮依次收进右端的「⋯」菜单，宽度回来后自动放回原处。
//
// 用法：
//   auto* bar = new OverflowBar(this);
//   bar->Add(openBtn);          // 优先级 0：常驻
//   bar->Add(saveAsBtn, 2);     // 优先级 2：比 1 先收
//   bar->Add(undoBtn, 3);
//   bar->Add(hintLabel, -1);    // 负优先级：不占菜单，空间不足时直接隐藏（状态文字之类）
//   bar->AddSeparator();
//   bar->AddStretch();
//   bar->Relayout();            // 内容加完调一次（尺寸已知时更准）
class OverflowBar : public QWidget {
  Q_OBJECT
 public:
  explicit OverflowBar(QWidget* parent = nullptr);

  // prio > 0：可收进「⋯」菜单，数字越大越早被收起；
  // prio == 0：常驻，永不收起；
  // prio < 0：不上菜单，|prio| 越大越早被直接隐藏。
  // stretch > 0 的控件按「可压缩」对待：量宽度时只看 minimumWidth（给输入框/下拉框用）。
  void Add(QWidget* w, int prio = 0, int stretch = 0);
  void AddSeparator();  //「|」分组竖线：两侧任一侧收空就跟着消失
  void AddStretch(int weight = 1);
  void SetSpacing(int px);

  void Relayout();  // 外部改了控件显隐后调用

 protected:
  void resizeEvent(QResizeEvent* e) override;
  void showEvent(QShowEvent* e) override;

 private:
  struct Item {
    QWidget* w = nullptr;
    int prio = 0;
    int stretch = 0;
    bool sep = false;
    bool hiddenByBar = false;
  };
  int RequiredWidth(const Item& it) const;
  int NeededWidth() const;
  void UpdateOverflow();
  void RebuildMenu();

  QHBoxLayout* lay_ = nullptr;
  QToolButton* more_ = nullptr;
  QMenu* menu_ = nullptr;
  QList<Item> items_;
  QHash<QAction*, QWidget*> actions_;
  bool busy_ = false;
};

}  // namespace wd