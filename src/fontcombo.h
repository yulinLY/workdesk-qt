#pragma once

#include <QComboBox>
#include <QString>

class QCompleter;

namespace wd {

// 可搜索的字体下拉框。
// 系统字体动辄一两百种，靠滚动条找太慢：编辑框直接当搜索框用，
// 敲关键字（包含匹配、大小写不敏感）就弹出匹配结果，回车或点选即应用。
class FontCombo : public QComboBox {
  Q_OBJECT
 public:
  explicit FontCombo(QWidget* parent = nullptr);

  QString Family() const { return family_; }
  void SetFamily(const QString& fam);  // 只更新显示与当前项，不发出 FamilyChanged

 signals:
  void FamilyChanged(const QString& fam);

 protected:
  void focusOutEvent(QFocusEvent* e) override;

 private:
  void ReloadFonts();
  void Commit(const QString& text);
  void RestoreText();

  QString family_;
  QCompleter* completer_ = nullptr;
};

}  // namespace wd
