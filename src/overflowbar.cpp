#include "overflowbar.h"

#include <QAbstractButton>
#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMetaObject>
#include <QResizeEvent>
#include <QShowEvent>
#include <QToolButton>

#include <algorithm>

namespace wd {
namespace {

// 收进菜单时的条目文案：优先按钮文字，其次 tooltip
QString ItemText(QWidget* w) {
  if (auto* b = qobject_cast<QAbstractButton*>(w)) {
    const QString t = b->text().trimmed();
    if (!t.isEmpty()) return t;
  }
  const QString tip = w->toolTip();
  if (!tip.isEmpty()) return tip.split(QLatin1Char('\n')).value(0);
  return QStringLiteral("操作");
}

}  // namespace

OverflowBar::OverflowBar(QWidget* parent) : QWidget(parent) {
  lay_ = new QHBoxLayout(this);
  lay_->setContentsMargins(0, 0, 0, 0);
  lay_->setSpacing(4);

  //「⋯」按钮常驻在末尾，只有真的收起了东西才显示
  more_ = new QToolButton(this);
  more_->setObjectName(QStringLiteral("OverflowMore"));
  more_->setText(QString(QChar(0x22EF)));
  more_->setToolTip(QStringLiteral("更多操作：模块变窄时被收起的按钮都在这里"));
  more_->setCursor(Qt::PointingHandCursor);
  more_->setAutoRaise(true);
  more_->setPopupMode(QToolButton::InstantPopup);
  menu_ = new QMenu(more_);
  more_->setMenu(menu_);
  connect(menu_, &QMenu::aboutToShow, this, &OverflowBar::RebuildMenu);
  more_->hide();
  lay_->addWidget(more_);

  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void OverflowBar::SetSpacing(int px) { lay_->setSpacing(px); }

void OverflowBar::Add(QWidget* w, int prio, int stretch) {
  if (!w) return;
  Item it;
  it.w = w;
  it.prio = prio;
  it.stretch = stretch;
  items_ << it;
  // more_ 永远压在布局末尾，新控件插在它前面
  lay_->insertWidget(lay_->count() - 1, w, stretch);
}

void OverflowBar::AddSeparator() {
  auto* s = new QLabel(QStringLiteral("|"), this);
  s->setObjectName(QStringLiteral("ModuleHint"));
  Item it;
  it.w = s;
  it.sep = true;
  items_ << it;
  lay_->insertWidget(lay_->count() - 1, s);
}

void OverflowBar::AddStretch(int weight) { lay_->insertStretch(lay_->count() - 1, weight); }

void OverflowBar::Relayout() { UpdateOverflow(); }

// 控件「不被压变形」所需的最小宽度：按钮的 minimumSizeHint 就是它的自然宽度，
// 可压缩控件（下拉框、滑块）只认 minimumWidth，作者设多少就压到多少。
int OverflowBar::RequiredWidth(const Item& it) const {
  if (!it.w) return 0;
  if (it.stretch > 0) return qMax(0, it.w->minimumWidth());
  return qMax(it.w->minimumWidth(), it.w->minimumSizeHint().width());
}

int OverflowBar::NeededWidth() const {
  int need = 0;
  int n = 0;
  for (const Item& it : items_) {
    if (it.sep || !it.w || it.w->isHidden()) continue;
    need += RequiredWidth(it);
    ++n;
  }
  if (!more_->isHidden()) {
    need += qMax(0, more_->minimumSizeHint().width());
    ++n;
  }
  if (n > 1) need += lay_->spacing() * (n - 1);
  return need;
}

void OverflowBar::UpdateOverflow() {
  if (busy_) return;
  const int avail = width();
  if (avail <= 0) return;  // 还没被布局摆过，等 resize/show 再算

  busy_ = true;

  // 1) 上一轮被自己收起来的控件先全部放回
  for (Item& it : items_) {
    if (it.hiddenByBar) {
      it.w->setVisible(true);
      it.hiddenByBar = false;
    }
  }
  more_->hide();

  // 2) 候选：有显式优先级的控件（正数上菜单、负数只隐藏），按优先级从大到小依次收起
  QList<int> cand;
  for (int i = 0; i < items_.size(); ++i) {
    const Item& it = items_.at(i);
    if (it.sep || !it.w || it.prio == 0) continue;
    if (it.w->isHidden()) continue;  // 本来就被外面隐藏（如「停止」按钮平时不显示）
    cand << i;
  }
  std::sort(cand.begin(), cand.end(), [this](int a, int b) {
    const int pa = qAbs(items_.at(a).prio);
    const int pb = qAbs(items_.at(b).prio);
    if (pa != pb) return pa > pb;  // 优先级绝对值大的先收
    return a > b;                  // 同优先级先收靠右的
  });

  int pos = 0;
  bool moreNeeded = false;
  while (NeededWidth() > avail && pos < cand.size()) {
    Item& it = items_[cand.at(pos++)];
    it.w->hide();
    it.hiddenByBar = true;
    if (it.prio > 0) moreNeeded = true;
  }
  more_->setVisible(moreNeeded);

  // 3) 分组竖线：左右任一侧没有可见控件就一起收掉，不留孤零零的「|」
  for (int i = 0; i < items_.size(); ++i) {
    Item& it = items_[i];
    if (!it.sep) continue;
    bool left = false;
    bool right = false;
    for (int j = i - 1; j >= 0; --j) {
      const Item& o = items_.at(j);
      if (o.sep || !o.w) continue;
      if (!o.w->isHidden()) {
        left = true;
        break;
      }
    }
    for (int j = i + 1; j < items_.size(); ++j) {
      const Item& o = items_.at(j);
      if (o.sep || !o.w) continue;
      if (!o.w->isHidden()) {
        right = true;
        break;
      }
    }
    const bool want = left && right;
    if (want && it.w->isHidden()) {
      it.w->setVisible(true);
      it.hiddenByBar = false;
    } else if (!want && !it.w->isHidden()) {
      it.w->hide();
      it.hiddenByBar = true;
    }
  }

  if (moreNeeded) RebuildMenu();
  busy_ = false;
}

// 菜单项与按钮一一对应：文字、图标状态、可用/勾选状态都从按钮现取，
// 点条目等价于点原按钮（QPushButton / QToolButton 都有 click()）。
void OverflowBar::RebuildMenu() {
  menu_->clear();
  actions_.clear();
  for (const Item& it : items_) {
    if (it.sep || !it.w || it.prio <= 0 || !it.hiddenByBar) continue;
    QAction* act = menu_->addAction(ItemText(it.w));
    actions_.insert(act, it.w);
    act->setEnabled(it.w->isEnabled());
    if (auto* b = qobject_cast<QAbstractButton*>(it.w)) {
      if (b->isCheckable()) {
        act->setCheckable(true);
        act->setChecked(b->isChecked());
      }
    }
    QWidget* w = it.w;
    connect(act, &QAction::triggered, w, [w]() {
      QMetaObject::invokeMethod(w, "click", Qt::DirectConnection);
    });
  }
}

void OverflowBar::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  UpdateOverflow();
}

void OverflowBar::showEvent(QShowEvent* e) {
  QWidget::showEvent(e);
  UpdateOverflow();
}

}  // namespace wd