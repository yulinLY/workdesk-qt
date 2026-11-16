#include "notepad.h"
#include "wdtrace.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringConverter>
#include <QVBoxLayout>

namespace wd {
namespace {

QString DecodeText(const QByteArray& raw) {
  QStringDecoder utf8(QStringDecoder::Utf8);
  const QString s = utf8.decode(raw);
  if (!utf8.hasError()) return s;
  QStringDecoder gb(QStringDecoder::System);  // 中文 Windows 上即 GB18030/GBK
  const QString s2 = gb.decode(raw);
  if (!gb.hasError() && !s2.isEmpty()) return s2;
  return s;  // 兜底：按 UTF-8 宽松解码（非法字节会变成替换符，不丢文件）
}

}  // namespace

NotepadPanel::NotepadPanel(QWidget* parent) : QWidget(parent) {
  wd::Trace("np: ctor enter"); BuildUi(); }

void NotepadPanel::BuildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(6);

  auto* bar = new QWidget(this);
  auto* barLay = new QHBoxLayout(bar);
  barLay->setContentsMargins(0, 0, 0, 0);
  barLay->setSpacing(6);

  auto* openBtn = new QPushButton(QStringLiteral("打开"), bar);
  connect(openBtn, &QPushButton::clicked, this, [this]() {
    if (!SaveIfDirty()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开文本文件"), QString(),
        QStringLiteral("文本 (*.txt *.md *.log *.json *.cpp *.h *.hpp *.c *.py *.ini *.csv);;"
                       "所有文件 (*.*)"));
    if (!path.isEmpty()) OpenPath(path);
  });
  barLay->addWidget(openBtn);

  auto* saveBtn = new QPushButton(QStringLiteral("保存"), bar);
  connect(saveBtn, &QPushButton::clicked, this, [this]() {
    if (path_.isEmpty()) {
      const QString p = QFileDialog::getSaveFileName(this, QStringLiteral("保存为"),
                                                     QStringLiteral("untitled.txt"),
                                                     QStringLiteral("文本 (*.txt)"));
      if (p.isEmpty()) return;
      SaveTo(p);
    } else {
      SaveTo(path_);
    }
  });
  barLay->addWidget(saveBtn);

  auto* saveAsBtn = new QPushButton(QStringLiteral("另存为"), bar);
  connect(saveAsBtn, &QPushButton::clicked, this, [this]() {
    const QString p = QFileDialog::getSaveFileName(
        this, QStringLiteral("另存为"),
        path_.isEmpty() ? QStringLiteral("untitled.txt") : path_, QStringLiteral("文本 (*.txt)"));
    if (!p.isEmpty()) SaveTo(p);
  });
  barLay->addWidget(saveAsBtn);

  barLay->addStretch(1);
  info_ = new QLabel(QStringLiteral("未打开文件"), bar);
  info_->setObjectName("ModuleHint");
  barLay->addWidget(info_);
  root->addWidget(bar);

  edit_ = new QPlainTextEdit(this);
  QFont f;
  f.setFamilies({"Consolas", "Cascadia Mono", "Microsoft YaHei UI"});
  f.setStyleHint(QFont::Monospace);
  f.setPointSizeF(10.5);
  edit_->setFont(f);
  edit_->setTabStopDistance(4 * QFontMetricsF(edit_->font()).horizontalAdvance(QLatin1Char(' ')));
  edit_->setPlaceholderText(QString());
  connect(edit_, &QPlainTextEdit::textChanged, this, [this]() {
    dirty_ = true;
    UpdateInfo();
  });
  root->addWidget(edit_, 1);
}

bool NotepadPanel::OpenPath(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, QStringLiteral("打开失败"),
                         QStringLiteral("无法读取：%1").arg(path));
    return false;
  }
  const QByteArray raw = f.readAll();
  f.close();
  if (raw.contains('\0')) {
    QMessageBox::warning(
        this, QStringLiteral("不是文本文件"),
        QStringLiteral("「%1」含二进制内容，没有按文本打开。").arg(QFileInfo(path).fileName()));
    return false;
  }
  QSignalBlocker block(edit_);
  edit_->setPlainText(DecodeText(raw));
  path_ = path;
  dirty_ = false;
  UpdateInfo();
  emit TitleChanged(QFileInfo(path).fileName());
  emit Notice(QStringLiteral("记事本已打开：") + path);
  return true;
}

bool NotepadPanel::SaveTo(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, QStringLiteral("保存失败"),
                         QStringLiteral("无法写入：%1").arg(path));
    return false;
  }
  const QByteArray data = edit_->toPlainText().toUtf8();
  f.write(data);
  f.close();
  path_ = path;
  dirty_ = false;
  UpdateInfo();
  emit TitleChanged(QFileInfo(path).fileName());
  emit Notice(QStringLiteral("已保存：") + path);
  return true;
}

bool NotepadPanel::SaveIfDirty() {
  if (!dirty_) return true;
  const auto ret = QMessageBox::question(
      this, QStringLiteral("未保存的修改"),
      QStringLiteral("记事本里有未保存的修改，要先保存吗？"),
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
  if (ret == QMessageBox::Cancel) return false;
  if (ret == QMessageBox::No) return true;
  if (path_.isEmpty()) {
    const QString p = QFileDialog::getSaveFileName(this, QStringLiteral("保存为"),
                                                   QStringLiteral("untitled.txt"),
                                                   QStringLiteral("文本 (*.txt)"));
    if (p.isEmpty()) return false;
    return SaveTo(p);
  }
  return SaveTo(path_);
}

void NotepadPanel::UpdateInfo() {
  const QString name = path_.isEmpty() ? QStringLiteral("未命名") : QFileInfo(path_).fileName();
  const int chars = edit_->toPlainText().size();
  const int lines = edit_->document()->blockCount();
  info_->setText(QStringLiteral("%1%2  ·  %3 行  ·  %4 字符")
                     .arg(name, dirty_ ? QStringLiteral(" *") : QString())
                     .arg(lines)
                     .arg(chars));
}

}  // namespace wd
