#pragma once
#include <QPlainTextEdit>
#include <QString>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QToolButton;

namespace wd {

class FontCombo;

// 带行号栏的编辑区：行号画在左侧独立子控件上，跟随滚动，配色取自调色板（跟随主题）。
class CodeEdit : public QPlainTextEdit {
 public:
  explicit CodeEdit(QWidget* parent = nullptr);

  int GutterWidth() const;
  void PaintGutter(QPaintEvent* e);
  void RefreshGutter();

 protected:
  void resizeEvent(QResizeEvent* e) override;

 private:
  QWidget* gutter_ = nullptr;
};

// 记事本模块：真·文本编辑器。
// 支持打开/保存/另存为、查找替换、行号、字数统计、撤销重做、字体字号、自动换行；
// 编码按 UTF-8 / GB18030 自动识别，并记住原编码，保存时按原编码写回。
class NotepadPanel : public QWidget {
  Q_OBJECT
 public:
  explicit NotepadPanel(QWidget* parent = nullptr);

  bool OpenPath(const QString& path);
  bool SaveIfDirty();
  bool HasUnsaved() const { return dirty_; }
  QString CurrentPath() const { return path_; }

 signals:
  void Notice(const QString& text);
  void TitleChanged(const QString& text);

 protected:
  bool eventFilter(QObject* obj, QEvent* ev) override;

 private:
  void BuildUi();
  void BuildFindBar();
  void SaveNow();
  void OpenWithDialog();
  bool SaveTo(const QString& path);
  void UpdateInfo();
  void SetWrap(bool on);
  void LoadPrefs();
  void SavePrefs();
  void SetFontSize(qreal pt);
  void ShowFind(bool on, bool focusReplace = false);
  bool FindNext(bool backward);
  void ReplaceCurrent();
  void ReplaceAll();
  int CountMatches() const;
  void UpdateFindInfo();
  void ApplyEncoding(const QString& enc);  // 空串=自动识别
  void LoadBytes(const QByteArray& raw, const QString& forceEncoding);

  CodeEdit* edit_ = nullptr;
  QLabel* info_ = nullptr;
  QWidget* findBar_ = nullptr;
  QLineEdit* find_ = nullptr;
  QLineEdit* repl_ = nullptr;
  QLabel* findInfo_ = nullptr;
  QCheckBox* caseBox_ = nullptr;
  QToolButton* wrapBtn_ = nullptr;
  QToolButton* encBtn_ = nullptr;
  QToolButton* undoBtn_ = nullptr;
  QToolButton* redoBtn_ = nullptr;
  FontCombo* fontBox_ = nullptr;

  QString path_;
  QString encoding_ = QStringLiteral("UTF-8");
  QByteArray raw_;
  bool dirty_ = false;
  bool wrap_ = true;
  qreal pt_ = 10.5;
};

}  // namespace wd
