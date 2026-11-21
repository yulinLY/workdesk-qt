#include "notepad.h"

#include "fontcombo.h"

#include <QCheckBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStringConverter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace wd {
namespace {

struct DecodedText {
  QString text;
  QString encoding;
};

// 解码：BOM 优先，其次 UTF-8 严格解码，失败退 GB18030（中文 Windows 上即 GBK/GB18030 超集）。
// 两条路都失败时（彻底乱码文件）挑替换符少的那个，保证不丢文件、能人工再改。
DecodedText DecodeBytes(const QByteArray& raw, const QString& force = QString()) {
  const bool bom = raw.startsWith("\xEF\xBB\xBF");
  if (force.compare(QLatin1String("UTF-8"), Qt::CaseInsensitive) == 0) {
    QStringDecoder d(QStringDecoder::Utf8);
    return {d.decode(bom ? raw.mid(3) : raw), QStringLiteral("UTF-8")};
  }
  if (force.compare(QLatin1String("GB18030"), Qt::CaseInsensitive) == 0) {
    QStringDecoder d(QStringDecoder::System);
    return {d.decode(raw), QStringLiteral("GB18030")};
  }
  if (bom) {
    QStringDecoder d(QStringDecoder::Utf8);
    return {d.decode(raw.mid(3)), QStringLiteral("UTF-8")};
  }
  QStringDecoder utf8(QStringDecoder::Utf8);
  const QString s = utf8.decode(raw);
  if (!utf8.hasError() && !s.contains(QChar::ReplacementCharacter))
    return {s, QStringLiteral("UTF-8")};

  QStringDecoder gb(QStringDecoder::System);
  const QString s2 = gb.decode(raw);
  if (!gb.hasError() && !s2.isEmpty() && !s2.contains(QChar::ReplacementCharacter))
    return {s2, QStringLiteral("GB18030")};

  const int bad1 = s.count(QChar::ReplacementCharacter);
  const int bad2 = s2.count(QChar::ReplacementCharacter);
  return bad1 <= bad2 ? DecodedText{s, QStringLiteral("UTF-8")}
                      : DecodedText{s2, QStringLiteral("GB18030")};
}

QByteArray EncodeBytes(const QString& text, const QString& encoding) {
  if (encoding.compare(QLatin1String("GB18030"), Qt::CaseInsensitive) == 0) {
    QStringEncoder enc(QStringConverter::System);
    return enc.encode(text);
  }
  return text.toUtf8();
}

// 行号栏：只负责把绘制转交给 CodeEdit
class LineNumberArea : public QWidget {
 public:
  explicit LineNumberArea(CodeEdit* ed) : QWidget(ed), ed_(ed) {}
  QSize sizeHint() const override { return QSize(ed_->GutterWidth(), 0); }

 protected:
  void paintEvent(QPaintEvent* e) override { ed_->PaintGutter(e); }

 private:
  CodeEdit* ed_;
};

}  // namespace

// ---------------------------------------------------------------- CodeEdit

CodeEdit::CodeEdit(QWidget* parent) : QPlainTextEdit(parent) {
  gutter_ = new LineNumberArea(this);
  connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) { RefreshGutter(); });
  connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& r, int dy) {
    if (dy) gutter_->scroll(0, dy);
    else gutter_->update(0, r.y(), gutter_->width(), r.height());
  });
  connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() { gutter_->update(); });
  RefreshGutter();
}

int CodeEdit::GutterWidth() const {
  const int digits = QString::number(qMax(1, blockCount())).size();
  return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(2, digits);
}

void CodeEdit::RefreshGutter() {
  if (!gutter_) return;
  setViewportMargins(GutterWidth(), 0, 0, 0);
  const QRect cr = contentsRect();
  gutter_->setGeometry(QRect(cr.left(), cr.top(), GutterWidth(), cr.height()));
  gutter_->update();
}

void CodeEdit::resizeEvent(QResizeEvent* e) {
  QPlainTextEdit::resizeEvent(e);
  if (!gutter_) return;
  const QRect cr = contentsRect();
  gutter_->setGeometry(QRect(cr.left(), cr.top(), GutterWidth(), cr.height()));
}

void CodeEdit::PaintGutter(QPaintEvent* e) {
  if (!gutter_) return;
  QPainter p(gutter_);
  QColor bg = palette().color(QPalette::Base);
  p.fillRect(e->rect(), bg);

  const int curLine = textCursor().blockNumber();
  QColor curBg = palette().color(QPalette::Highlight);
  curBg.setAlpha(38);

  QColor fg = palette().color(QPalette::Text);
  fg.setAlpha(115);
  QColor fgCur = palette().color(QPalette::Text);
  fgCur.setAlpha(225);

  QTextBlock block = firstVisibleBlock();
  int num = block.blockNumber();
  int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
  int bottom = top + qRound(blockBoundingRect(block).height());
  const int lh = fontMetrics().height();
  const int w = gutter_->width() - 8;

  while (block.isValid() && top <= e->rect().bottom()) {
    if (block.isVisible() && bottom >= e->rect().top()) {
      if (num == curLine) p.fillRect(0, top, gutter_->width(), lh, curBg);
      p.setPen(num == curLine ? fgCur : fg);
      p.drawText(0, top, w, lh, Qt::AlignRight | Qt::AlignVCenter, QString::number(num + 1));
    }
    block = block.next();
    top = bottom;
    bottom = top + qRound(blockBoundingRect(block).height());
    ++num;
  }
}

// ------------------------------------------------------------ NotepadPanel

NotepadPanel::NotepadPanel(QWidget* parent) : QWidget(parent) {
  BuildUi();
  LoadPrefs();
}

void NotepadPanel::BuildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(6);

  // 编辑区先建好，顶栏里的字体/撤销按钮要直接绑它的信号
  edit_ = new CodeEdit(this);
  QFont f;
  f.setFamilies({"Consolas", "Cascadia Mono", "Microsoft YaHei UI"});
  f.setStyleHint(QFont::Monospace);
  f.setPointSizeF(pt_);
  edit_->setFont(f);
  edit_->setTabStopDistance(4 * QFontMetricsF(edit_->font()).horizontalAdvance(QLatin1Char(' ')));
  edit_->setPlaceholderText(QString());
  edit_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  edit_->viewport()->installEventFilter(this);
  edit_->installEventFilter(this);
  connect(edit_, &QPlainTextEdit::textChanged, this, [this]() {
    dirty_ = true;
    UpdateInfo();
    UpdateFindInfo();
  });
  connect(edit_, &QPlainTextEdit::selectionChanged, this, [this]() { UpdateInfo(); });

  // ---- 顶栏（分两行：卡片窄的时候不会把按钮挤成省略号）
  auto* bar = new QWidget(this);
  auto* barCol = new QVBoxLayout(bar);
  barCol->setContentsMargins(0, 0, 0, 0);
  barCol->setSpacing(4);

  auto makeRow = [bar, barCol]() {
    auto* row = new QWidget(bar);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    barCol->addWidget(row);
    return lay;
  };
  QHBoxLayout* row1 = makeRow();
  QHBoxLayout* row2 = makeRow();

  auto mkBtn = [](QHBoxLayout* lay, const QString& text, const QString& tip) {
    auto* b = new QPushButton(text);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    lay->addWidget(b);
    return b;
  };
  auto mkTool = [](QHBoxLayout* lay, const QString& text, const QString& tip) {
    auto* b = new QToolButton();
    b->setText(text);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    lay->addWidget(b);
    return b;
  };
  auto mkSep = [](QHBoxLayout* lay) {
    auto* s = new QLabel(QStringLiteral("|"));
    s->setObjectName("ModuleHint");
    lay->addWidget(s);
  };

  auto* openBtn = mkBtn(row1, QStringLiteral("打开"), QStringLiteral("打开文本文件（Ctrl+O）"));
  connect(openBtn, &QPushButton::clicked, this, [this]() { OpenWithDialog(); });

  auto* saveBtn = mkBtn(row1, QStringLiteral("保存"), QStringLiteral("保存（Ctrl+S）"));
  connect(saveBtn, &QPushButton::clicked, this, [this]() { SaveNow(); });

  auto* saveAsBtn = mkBtn(row1, QStringLiteral("另存为"), QStringLiteral("另存为"));
  connect(saveAsBtn, &QPushButton::clicked, this, [this]() {
    const QString p = QFileDialog::getSaveFileName(
        this, QStringLiteral("另存为"),
        path_.isEmpty() ? QStringLiteral("untitled.txt") : path_, QStringLiteral("文本 (*.txt)"));
    if (!p.isEmpty()) SaveTo(p);
  });

  mkSep(row1);
  undoBtn_ = mkTool(row1, QStringLiteral("撤销"), QStringLiteral("撤销（Ctrl+Z）"));
  undoBtn_->setEnabled(false);
  connect(undoBtn_, &QToolButton::clicked, edit_, &QPlainTextEdit::undo);
  connect(edit_, &QPlainTextEdit::undoAvailable, undoBtn_, &QToolButton::setEnabled);

  redoBtn_ = mkTool(row1, QStringLiteral("重做"), QStringLiteral("重做（Ctrl+Y）"));
  redoBtn_->setEnabled(false);
  connect(redoBtn_, &QToolButton::clicked, edit_, &QPlainTextEdit::redo);
  connect(edit_, &QPlainTextEdit::redoAvailable, redoBtn_, &QToolButton::setEnabled);

  mkSep(row1);
  auto* findBtn = mkTool(row1, QStringLiteral("查找"), QStringLiteral("查找（Ctrl+F）"));
  connect(findBtn, &QToolButton::clicked, this, [this]() {
    ShowFind(!findBar_->isVisible());
  });
  auto* replBtn = mkTool(row1, QStringLiteral("替换"), QStringLiteral("替换（Ctrl+H）"));
  connect(replBtn, &QToolButton::clicked, this, [this]() { ShowFind(true, true); });
  row1->addStretch(1);

  fontBox_ = new FontCombo();
  fontBox_->setMinimumWidth(150);
  fontBox_->setMaximumWidth(200);
  fontBox_->SetFamily(edit_->font().families().value(0));
  row2->addWidget(fontBox_);
  connect(fontBox_, &FontCombo::FamilyChanged, this, [this](const QString& fam) {
    QFont g = edit_->font();
    g.setFamilies({fam});
    edit_->setFont(g);
    edit_->setTabStopDistance(4 * QFontMetricsF(edit_->font()).horizontalAdvance(QLatin1Char(' ')));
    edit_->RefreshGutter();
    SavePrefs();
  });

  auto* minusBtn = mkTool(row2, QStringLiteral("A-"), QStringLiteral("减小字号（Ctrl+滚轮）"));
  connect(minusBtn, &QToolButton::clicked, this, [this]() { SetFontSize(pt_ - 1); });
  auto* plusBtn = mkTool(row2, QStringLiteral("A+"), QStringLiteral("增大字号（Ctrl+滚轮）"));
  connect(plusBtn, &QToolButton::clicked, this, [this]() { SetFontSize(pt_ + 1); });

  wrapBtn_ = mkTool(row2, QStringLiteral("换行：开"), QStringLiteral("自动换行开关（Alt+Z）"));
  connect(wrapBtn_, &QToolButton::clicked, this, [this]() { SetWrap(!wrap_); });

  encBtn_ = mkTool(row2, encoding_, QStringLiteral("文本编码：点开可切换并重新载入"));
  auto* encMenu = new QMenu(encBtn_);
  auto addEnc = [&](const QString& label, const QString& value) {
    auto* act = encMenu->addAction(label);
    connect(act, &QAction::triggered, this, [this, value]() { ApplyEncoding(value); });
  };
  addEnc(QStringLiteral("自动识别"), QString());
  addEnc(QStringLiteral("UTF-8"), QStringLiteral("UTF-8"));
  addEnc(QStringLiteral("GB18030 / GBK"), QStringLiteral("GB18030"));
  connect(encBtn_, &QToolButton::clicked, this, [encBtn = encBtn_, encMenu]() {
    encMenu->exec(encBtn->mapToGlobal(QPoint(0, encBtn->height())));
  });
  row2->addStretch(1);

  root->addWidget(bar);

  BuildFindBar();
  root->addWidget(findBar_);
  root->addWidget(edit_, 1);

  // 状态栏：文件 / 行 / 字 / 字符 / 选中 / 编码；放底部，免得把顶栏一排按钮挤变形
  auto* status = new QWidget(this);
  auto* statusLay = new QHBoxLayout(status);
  statusLay->setContentsMargins(2, 0, 2, 0);
  statusLay->setSpacing(0);
  info_ = new QLabel(QStringLiteral("未打开文件"), status);
  info_->setObjectName("ModuleHint");
  statusLay->addWidget(info_);
  statusLay->addStretch(1);
  root->addWidget(status);

  // ---- 快捷键
  UpdateInfo();
}

void NotepadPanel::BuildFindBar() {
  findBar_ = new QWidget(this);
  auto* lay = new QHBoxLayout(findBar_);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(4);

  find_ = new QLineEdit(findBar_);
  find_->setPlaceholderText(QStringLiteral("查找"));
  find_->setClearButtonEnabled(true);
  lay->addWidget(find_, 1);

  repl_ = new QLineEdit(findBar_);
  repl_->setPlaceholderText(QStringLiteral("替换为"));
  repl_->setClearButtonEnabled(true);
  lay->addWidget(repl_, 1);

  caseBox_ = new QCheckBox(QStringLiteral("Aa"), findBar_);
  caseBox_->setToolTip(QStringLiteral("区分大小写"));
  lay->addWidget(caseBox_);

  auto mkMini = [&](const QString& text, const QString& tip) {
    auto* b = new QToolButton(findBar_);
    b->setText(text);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    lay->addWidget(b);
    return b;
  };
  auto* prevBtn = mkMini(QStringLiteral("上一个"), QStringLiteral("上一个匹配（Shift+Enter）"));
  connect(prevBtn, &QToolButton::clicked, this, [this]() { FindNext(true); });
  auto* nextBtn = mkMini(QStringLiteral("下一个"), QStringLiteral("下一个匹配（Enter）"));
  connect(nextBtn, &QToolButton::clicked, this, [this]() { FindNext(false); });
  auto* repBtn = mkMini(QStringLiteral("替换"), QStringLiteral("替换当前匹配"));
  connect(repBtn, &QToolButton::clicked, this, [this]() { ReplaceCurrent(); });
  auto* repAllBtn = mkMini(QStringLiteral("全部替换"), QStringLiteral("替换全部匹配"));
  connect(repAllBtn, &QToolButton::clicked, this, [this]() { ReplaceAll(); });

  findInfo_ = new QLabel(findBar_);
  findInfo_->setObjectName("ModuleHint");
  findInfo_->setMinimumWidth(48);
  lay->addWidget(findInfo_);

  auto* closeBtn = mkMini(QStringLiteral("×"), QStringLiteral("关闭查找栏（Esc）"));
  connect(closeBtn, &QToolButton::clicked, this, [this]() { ShowFind(false); });

  connect(find_, &QLineEdit::textChanged, this, [this]() { UpdateFindInfo(); });
  connect(find_, &QLineEdit::returnPressed, this, [this]() { FindNext(false); });
  connect(repl_, &QLineEdit::returnPressed, this, [this]() { ReplaceCurrent(); });

  findBar_->setVisible(false);
}

bool NotepadPanel::eventFilter(QObject* obj, QEvent* ev) {
  if (edit_ && obj == edit_->viewport() && ev->type() == QEvent::Wheel) {
    auto* we = static_cast<QWheelEvent*>(ev);
    if (we->modifiers() & Qt::ControlModifier) {
      SetFontSize(pt_ + (we->angleDelta().y() > 0 ? 1.0 : -1.0));
      return true;
    }
  }
  if (ev->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(ev);
    const bool ctrl = ke->modifiers() & Qt::ControlModifier;
    const bool alt = ke->modifiers() & Qt::AltModifier;
    if (ke->key() == Qt::Key_Escape && findBar_ && findBar_->isVisible()) {
      ShowFind(false);
      return true;
    }
    if (ctrl && ke->key() == Qt::Key_O) { OpenWithDialog(); return true; }
    if (ctrl && ke->key() == Qt::Key_F) { ShowFind(true); return true; }
    if (ctrl && ke->key() == Qt::Key_H) { ShowFind(true, true); return true; }
    if (ctrl && ke->key() == Qt::Key_S) { SaveNow(); return true; }
    if (alt && ke->key() == Qt::Key_Z) { SetWrap(!wrap_); return true; }
    if (ctrl && (ke->key() == Qt::Key_Equal || ke->key() == Qt::Key_Plus)) {
      SetFontSize(pt_ + 1); return true;
    }
    if (ctrl && ke->key() == Qt::Key_Minus) { SetFontSize(pt_ - 1); return true; }
  }
  return QWidget::eventFilter(obj, ev);
}

void NotepadPanel::ShowFind(bool on, bool focusReplace) {
  if (!findBar_) return;
  findBar_->setVisible(on);
  if (on) {
    if (focusReplace) {
      repl_->setFocus();
      repl_->selectAll();
    } else {
      find_->setFocus();
      find_->selectAll();
    }
    // 有选中文字时带进查找框，省一次复制粘贴
    const QString sel = edit_->textCursor().selectedText();
    if (!sel.isEmpty() && !sel.contains(QChar(0x2029))) {
      find_->setText(sel);
      find_->selectAll();
    }
    UpdateFindInfo();
  } else {
    edit_->setFocus();
  }
}

int NotepadPanel::CountMatches() const {
  const QString t = find_ ? find_->text() : QString();
  if (t.isEmpty()) return 0;
  QTextDocument::FindFlags fl;
  if (caseBox_ && caseBox_->isChecked()) fl |= QTextDocument::FindCaseSensitively;
  int n = 0;
  QTextCursor c(edit_->document());
  while (true) {
    c = edit_->document()->find(t, c, fl);
    if (c.isNull()) break;
    ++n;
    if (n > 100000) break;
  }
  return n;
}

void NotepadPanel::UpdateFindInfo() {
  if (!findInfo_ || !findBar_ || !findBar_->isVisible()) return;
  if (find_->text().isEmpty()) {
    findInfo_->setText(QString());
    return;
  }
  findInfo_->setText(QStringLiteral("%1 处").arg(CountMatches()));
}

bool NotepadPanel::FindNext(bool backward) {
  const QString t = find_->text();
  if (t.isEmpty()) {
    repl_->setFocus();
    return false;
  }
  QTextDocument::FindFlags fl;
  if (caseBox_->isChecked()) fl |= QTextDocument::FindCaseSensitively;
  if (backward) fl |= QTextDocument::FindBackward;

  QTextCursor start = edit_->textCursor();
  if (start.hasSelection())
    start.setPosition(backward ? start.selectionStart() : start.selectionEnd());

  QTextCursor hit = edit_->document()->find(t, start, fl);
  if (hit.isNull()) {  // 到尾部（或头部）后回绕继续
    QTextCursor wrap = backward ? QTextCursor(edit_->document()->end())
                                : QTextCursor(edit_->document()->begin());
    hit = edit_->document()->find(t, wrap, fl);
  }
  if (hit.isNull()) {
    emit Notice(QStringLiteral("未找到：%1").arg(t));
    return false;
  }
  edit_->setTextCursor(hit);
  edit_->centerCursor();
  UpdateFindInfo();
  return true;
}

void NotepadPanel::ReplaceCurrent() {
  QTextCursor c = edit_->textCursor();
  const QString findText = find_->text();
  if (findText.isEmpty()) {
    repl_->setFocus();
    return;
  }
  if (c.hasSelection() && c.selectedText() == findText) {
    c.insertText(repl_->text());
    edit_->setTextCursor(c);
  }
  FindNext(false);
}

void NotepadPanel::ReplaceAll() {
  const QString findText = find_->text();
  if (findText.isEmpty()) return;
  QTextDocument* doc = edit_->document();
  QTextDocument::FindFlags fl;
  if (caseBox_->isChecked()) fl |= QTextDocument::FindCaseSensitively;

  QTextCursor cur(doc);
  cur.beginEditBlock();  // 一次撤销即可整体回退
  int n = 0;
  while (true) {
    QTextCursor hit = doc->find(findText, cur, fl);
    if (hit.isNull()) break;
    hit.insertText(repl_->text());
    cur = hit;
    ++n;
    if (n > 100000) break;
  }
  cur.endEditBlock();
  emit Notice(n > 0 ? QStringLiteral("已替换 %1 处").arg(n) : QStringLiteral("没有可替换的内容"));
  UpdateInfo();
  UpdateFindInfo();
}

void NotepadPanel::SetWrap(bool on) {
  wrap_ = on;
  edit_->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
  if (wrapBtn_) wrapBtn_->setText(on ? QStringLiteral("换行：开") : QStringLiteral("换行：关"));
  SavePrefs();
}

// 字体/字号/换行状态记住，下次打开还是上回的样子
void NotepadPanel::LoadPrefs() {
  QSettings st(QStringLiteral("WorkDesk"), QStringLiteral("WorkDeskQt"));
  const QString fam = st.value(QStringLiteral("notepad/font")).toString();
  const qreal pt = st.value(QStringLiteral("notepad/size"), pt_).toDouble();
  const bool wrap = st.value(QStringLiteral("notepad/wrap"), wrap_).toBool();
  if (!fam.isEmpty() && fontBox_) {
    QSignalBlocker blk(fontBox_);
    fontBox_->SetFamily(fam);
    QFont g = edit_->font();
    g.setFamilies({fam});
    edit_->setFont(g);
    edit_->setTabStopDistance(4 * QFontMetricsF(edit_->font()).horizontalAdvance(QLatin1Char(' ')));
  }
  SetFontSize(pt);
  SetWrap(wrap);
  edit_->RefreshGutter();
}

void NotepadPanel::SavePrefs() {
  QSettings st(QStringLiteral("WorkDesk"), QStringLiteral("WorkDeskQt"));
  const QStringList fams = edit_->font().families();
  if (!fams.isEmpty()) st.setValue(QStringLiteral("notepad/font"), fams.first());
  st.setValue(QStringLiteral("notepad/size"), pt_);
  st.setValue(QStringLiteral("notepad/wrap"), wrap_);
}

void NotepadPanel::SetFontSize(qreal pt) {
  pt_ = qBound<qreal>(7.0, pt, 32.0);
  QFont f = edit_->font();
  f.setPointSizeF(pt_);
  edit_->setFont(f);
  edit_->setTabStopDistance(4 * QFontMetricsF(edit_->font()).horizontalAdvance(QLatin1Char(' ')));
  edit_->RefreshGutter();
  edit_->viewport()->update();
  SavePrefs();
}

void NotepadPanel::LoadBytes(const QByteArray& raw, const QString& forceEncoding) {
  const DecodedText d = DecodeBytes(raw, forceEncoding);
  {
    QSignalBlocker block(edit_);
    edit_->setPlainText(d.text);
  }
  edit_->document()->clearUndoRedoStacks();
  raw_ = raw;
  encoding_ = d.encoding;
  if (encBtn_) encBtn_->setText(encoding_);
}

void NotepadPanel::ApplyEncoding(const QString& enc) {
  if (path_.isEmpty()) {
    // 未落盘的新文本：只切保存编码，不重新解码
    encoding_ = (enc.isEmpty() || enc.compare(QLatin1String("GB18030"), Qt::CaseInsensitive) == 0)
                    ? (enc.isEmpty() ? QStringLiteral("UTF-8") : QStringLiteral("GB18030"))
                    : QStringLiteral("UTF-8");
    if (encBtn_) encBtn_->setText(encoding_);
    UpdateInfo();
    return;
  }
  if (dirty_) {
    emit Notice(QStringLiteral("有未保存的修改，先保存再切换编码"));
    return;
  }
  QFile f(path_);
  if (!f.open(QIODevice::ReadOnly)) return;
  raw_ = f.readAll();
  f.close();
  LoadBytes(raw_, enc);
  UpdateInfo();
  emit Notice(QStringLiteral("已按 %1 重新载入").arg(encoding_));
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
  path_ = path;
  LoadBytes(raw, QString());
  if (findBar_ && findBar_->isVisible()) UpdateFindInfo();
  dirty_ = false;
  UpdateInfo();
  emit TitleChanged(QFileInfo(path).fileName());
  emit Notice(QStringLiteral("记事本已打开：%1（%2）").arg(path, encoding_));
  return true;
}

void NotepadPanel::OpenWithDialog() {
  if (!SaveIfDirty()) return;
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("打开文本文件"), QString(),
      QStringLiteral("文本 (*.txt *.md *.log *.json *.cpp *.h *.hpp *.c *.py *.ini *.csv);;"
                     "所有文件 (*.*)"));
  if (!path.isEmpty()) OpenPath(path);
}

void NotepadPanel::SaveNow() {
  if (path_.isEmpty()) {
    const QString p = QFileDialog::getSaveFileName(this, QStringLiteral("保存为"),
                                                   QStringLiteral("untitled.txt"),
                                                   QStringLiteral("文本 (*.txt)"));
    if (p.isEmpty()) return;
    SaveTo(p);
    return;
  }
  SaveTo(path_);
}

bool NotepadPanel::SaveTo(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, QStringLiteral("保存失败"),
                         QStringLiteral("无法写入：%1").arg(path));
    return false;
  }
  const QByteArray data = EncodeBytes(edit_->toPlainText(), encoding_);
  f.write(data);
  f.close();
  raw_ = data;
  path_ = path;
  dirty_ = false;
  UpdateInfo();
  emit TitleChanged(QFileInfo(path).fileName());
  emit Notice(QStringLiteral("已保存：%1（%2）").arg(path, encoding_));
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
  if (!info_ || !edit_) return;
  const QString name = path_.isEmpty() ? QStringLiteral("未命名") : QFileInfo(path_).fileName();
  const QString text = edit_->toPlainText();
  const int chars = text.size();
  const int lines = edit_->document()->blockCount();
  int words = 0;  // 字数：不含空白的字符数
  for (const QChar c : text) {
    if (!c.isSpace()) ++words;
  }
  QString extra;
  const QTextCursor cur = edit_->textCursor();
  if (cur.hasSelection()) {
    const QString sel = cur.selectedText();
    int selWords = 0;
    for (const QChar c : sel) {
      if (!c.isSpace() && c != QChar(0x2029)) ++selWords;
    }
    extra = QStringLiteral(" · 选中 %1").arg(selWords);
  }
  info_->setText(QStringLiteral("%1%2  ·  %3 行  ·  %4 字  ·  %5 字符%6  ·  %7")
                     .arg(name, dirty_ ? QStringLiteral(" *") : QString())
                     .arg(lines)
                     .arg(words)
                     .arg(chars)
                     .arg(extra, encoding_));
  info_->setToolTip(QStringLiteral("文件名 · 行数 · 字数（不含空白） · 字符数 · 选中 · 编码"));
}

}  // namespace wd
