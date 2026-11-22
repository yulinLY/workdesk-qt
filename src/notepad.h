#pragma once
#include <QPlainTextEdit>
#include <QString>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSplitter;
class QTextBrowser;
class QToolButton;
class QTimer;

namespace wd {

class FontCombo;
class MdStyle;  // 输入区 Markdown 实时样式（定义在 notepad.cpp）

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

  QString PlainText() const;
  // 只取文档开头 maxChars 个字符，避免把整篇大文件复制成 QString（超大文档会 bad_alloc）
  QString PlainTextSnippet(int maxChars) const;
  QString DisplayName() const;
  // AI 写回：整篇替换（保留一次可撤销），写完立即标脏并滚到文末
  void ReplaceAllText(const QString& text);
  // AI 写回之后底部的「同意 / 不同意」：同意=保留改动，不同意=整篇退回写回前
  void AcceptAiWrite();
  void RejectAiWrite();

 signals:
  void Notice(const QString& text);
  void TitleChanged(const QString& text);

 protected:
  bool eventFilter(QObject* obj, QEvent* ev) override;

 private:
  void BuildUi();
  void BuildFindBar();
  void BuildAiBar();
  void ShowAiBar(bool on);
  void ScrollToEnd();
  void SaveNow();
  void OpenWithDialog();
  bool SaveTo(const QString& path);
  void UpdateInfo();
  void SetWrap(bool on);
  void SetMarkdown(bool on);     // Markdown 渲染总开关：关掉就回到纯文本编辑
  void SetHideMarkers(bool on);  // 隐藏/显示 Markdown 标记符，只影响显示
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
  MdStyle* mdStyle_ = nullptr;  // 编辑区 Markdown 实时样式渲染
  QLabel* info_ = nullptr;
  QWidget* findBar_ = nullptr;
  QWidget* aiBar_ = nullptr;     // AI 写回后的确认条：同意 / 不同意
  QLabel* aiLab_ = nullptr;
  QString aiBefore_;             // 写回前的全文，用于「不同意」整篇退回
  bool aiWasDirty_ = false;      // 写回前是否本来就脏，退回时还原
  QLineEdit* find_ = nullptr;
  QLineEdit* repl_ = nullptr;
  QLabel* findInfo_ = nullptr;
  QCheckBox* caseBox_ = nullptr;
  QToolButton* wrapBtn_ = nullptr;
  QToolButton* mdOnBtn_ = nullptr;    // Markdown 渲染总开关
  QToolButton* mdHideBtn_ = nullptr;  // 隐藏 Markdown 标记符开关
  QToolButton* encBtn_ = nullptr;
  QToolButton* undoBtn_ = nullptr;
  QToolButton* redoBtn_ = nullptr;
  FontCombo* fontBox_ = nullptr;

  QString path_;
  QString encoding_ = QStringLiteral("UTF-8");
  QByteArray raw_;
  bool dirty_ = false;
  bool wrap_ = true;
  bool mdOn_ = true;    // Markdown 渲染开关
  bool hideMd_ = false;  // 隐藏 Markdown 标记符
  qreal pt_ = 10.5;
};

}  // namespace wd
