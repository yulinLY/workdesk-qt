#include "fontcombo.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFocusEvent>
#include <QFont>
#include <QFontDatabase>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStringListModel>

namespace wd {
namespace {
constexpr int kPreviewPt = 10;  // 列表里每项预览字号，固定值便于横向比较
}

FontCombo::FontCombo(QWidget* parent) : QComboBox(parent) {
  setEditable(true);
  setInsertPolicy(QComboBox::NoInsert);
  setMaxVisibleItems(14);
  setMinimumWidth(150);
  setMaximumWidth(200);
  setToolTip(QStringLiteral("字体（可直接输入关键字搜索，回车或点选应用）"));

  if (QLineEdit* le = lineEdit()) {
    le->setPlaceholderText(QStringLiteral("搜索字体"));
    le->setClearButtonEnabled(true);
    // 敲字时先把整张字体列表收起来，只留匹配结果，别让两层弹窗叠在一起
    connect(le, &QLineEdit::textEdited, this, [this](const QString&) {
      if (QAbstractItemView* v = view(); v && v->isVisible()) hidePopup();
    });
    connect(le, &QLineEdit::returnPressed, this, [this]() {
      if (QLineEdit* edit = lineEdit()) Commit(edit->text());
    });
  }

  // 列表点选：两种信号都接，Commit 幂等，不会重复发通知
  connect(this, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) {
    if (idx >= 0 && idx < count()) Commit(itemText(idx));
  });
  connect(this, &QComboBox::textActivated, this, [this](const QString& text) { Commit(text); });

  ReloadFonts();
}

void FontCombo::ReloadFonts() {
  const QString keep = family_;
  const QStringList fams = QFontDatabase::families();
  {
    const QSignalBlocker blk(this);
    clear();
    for (const QString& fam : fams) {
      addItem(fam);
      QFont preview(fam);
      preview.setPointSize(kPreviewPt);  // 每项用自己的字体渲染，挑字体前能先看见字形
      setItemData(count() - 1, preview, Qt::FontRole);
    }
  }

  if (completer_) {
    delete completer_;
    completer_ = nullptr;
  }
  completer_ = new QCompleter(fams, this);
  completer_->setCaseSensitivity(Qt::CaseInsensitive);
  completer_->setFilterMode(Qt::MatchContains);
  completer_->setCompletionMode(QCompleter::PopupCompletion);
  completer_->setMaxVisibleItems(12);
  if (QLineEdit* le = lineEdit()) le->setCompleter(completer_);
  connect(completer_, QOverload<const QString&>::of(&QCompleter::activated), this,
          [this](const QString& text) { Commit(text); });

  SetFamily(keep);
}

void FontCombo::SetFamily(const QString& fam) {
  QString target = fam;
  if (target.isEmpty() && count() > 0) target = itemText(0);
  family_ = target;

  const int idx = target.isEmpty() ? -1 : findText(target, Qt::MatchFixedString);
  const QSignalBlocker blk(this);
  if (idx >= 0) setCurrentIndex(idx);
  if (QLineEdit* le = lineEdit()) {
    const QSignalBlocker lb(le);
    le->setText(target);  // 保证框里显示的是族名，而不是搜索词
  }
}

void FontCombo::Commit(const QString& text) {
  const QString key = text.trimmed();
  if (key.isEmpty()) {
    RestoreText();
    return;
  }

  int hit = findText(key, Qt::MatchFixedString);
  if (hit < 0) {  // 前缀优先
    for (int i = 0; i < count() && hit < 0; ++i) {
      if (itemText(i).startsWith(key, Qt::CaseInsensitive)) hit = i;
    }
  }
  if (hit < 0) {  // 其次包含
    for (int i = 0; i < count() && hit < 0; ++i) {
      if (itemText(i).contains(key, Qt::CaseInsensitive)) hit = i;
    }
  }
  if (hit < 0) {  // 一个都没匹配上：还原成当前字体，别把半截搜索词当成字体名
    RestoreText();
    return;
  }

  const QString fam = itemText(hit);
  const bool changed = (fam != family_);
  SetFamily(fam);
  if (changed) emit FamilyChanged(fam);
}

void FontCombo::RestoreText() {
  if (QLineEdit* le = lineEdit()) {
    const QSignalBlocker blk(le);
    le->setText(family_);
  }
}

void FontCombo::focusOutEvent(QFocusEvent* e) {
  QComboBox::focusOutEvent(e);
  // 打完字没回车就点别处：把搜索词还原成真正生效的字体
  if (QLineEdit* le = lineEdit(); le && le->text().trimmed() != family_) RestoreText();
}

}  // namespace wd
