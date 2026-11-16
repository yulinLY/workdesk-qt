#pragma once
#include <QString>
#include <QWidget>

class QLabel;
class QPlainTextEdit;

namespace wd {

// 记事本模块：可打开任意文本文件，UTF-8 / GB18030 自动识别，保存回 UTF-8。
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

 private:
  void BuildUi();
  bool SaveTo(const QString& path);
  void UpdateInfo();

  QPlainTextEdit* edit_ = nullptr;
  QLabel* info_ = nullptr;
  QString path_;
  bool dirty_ = false;
};

}  // namespace wd
