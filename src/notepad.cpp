#include "notepad.h"

#include "fontcombo.h"
#include "overflowbar.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
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
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringConverter>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
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

// ------------------------------------------------------- Markdown 实时样式
// 输入区直接按 Markdown 语义上样式，不再另开预览：标题放大加粗、**粗体**、*斜体*、
// ~~删除线~~、行内代码与 ``` 围栏块等宽底纹、# 与列表符号淡化、引用整行弱化、链接下划线。
// 只改显示不改文字：字符不隐藏，光标/选中/撤销/查找与原纯文本完全一致，AI 写回也不受影响。
class MdStyle : public QSyntaxHighlighter {
 public:
  explicit MdStyle(QTextDocument* doc) : QSyntaxHighlighter(doc) { Recolor(); }

  // 隐藏 Markdown 标记符：只把 # ** ` > - 这些符号画没，文字一个字符都不动，
  // 光标、选中、撤销、查找和 AI 写回仍按原文走。
  void SetHideMarkers(bool on) {
    if (hideMarkers_ == on) return;
    hideMarkers_ = on;
    Recolor();
  }
  bool HideMarkers() const { return hideMarkers_; }

  // 主题或字号变了重新取色、重算基准字号并全量刷一遍
  void Recolor() {
    const QColor base = QApplication::palette().color(QPalette::WindowText);
    const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;

    marker_ = QTextCharFormat();
    if (hideMarkers_) {
      // 双保险：颜色贴死编辑区底色 + 字号压到极小，符号在视觉上彻底消失且几乎不占宽
      const QColor bgc = QApplication::palette().color(QPalette::Base);
      marker_.setForeground(bgc);
      marker_.setBackground(bgc);
      marker_.setFontPointSize(0.8);
    } else {
      marker_.setForeground(QColor(base.red(), base.green(), base.blue(), 110));
    }

    head_ = QTextCharFormat();
    head_.setForeground(dark ? QColor(0x79, 0xb8, 0xff) : QColor(0x0a, 0x58, 0xca));
    head_.setFontWeight(QFont::DemiBold);

    bold_ = QTextCharFormat();
    bold_.setFontWeight(QFont::Bold);

    italic_ = QTextCharFormat();
    italic_.setFontItalic(true);

    strike_ = QTextCharFormat();
    strike_.setFontStrikeOut(true);
    strike_.setForeground(QColor(base.red(), base.green(), base.blue(), 170));

    code_ = QTextCharFormat();
    code_.setFontFamilies({QStringLiteral("Consolas"), QStringLiteral("Cascadia Mono")});
    code_.setFontFixedPitch(true);
    code_.setBackground(QColor(base.red(), base.green(), base.blue(), dark ? 30 : 22));
    code_.setForeground(dark ? QColor(0xe6, 0xb3, 0xf0) : QColor(0x9c, 0x27, 0xb0));

    quote_ = QTextCharFormat();
    quote_.setForeground(QColor(base.red(), base.green(), base.blue(), 150));
    quote_.setFontItalic(true);

    link_ = QTextCharFormat();
    link_.setForeground(dark ? QColor(0x6c, 0xb6, 0xff) : QColor(0x0b, 0x5c, 0xd6));
    link_.setFontUnderline(true);

    rehighlight();
  }

 protected:
  void highlightBlock(const QString& text) override {
    const int prev = previousBlockState();
    static const QRegularExpression fence(QStringLiteral("^\\s*(```+|~~~+)"));
    static const QRegularExpression head(QStringLiteral("^(#{1,6})(\\s*)(.*)$"));
    static const QRegularExpression quote(QStringLiteral("^\\s*>+"));
    static const QRegularExpression bullet(QStringLiteral("^(\\s*)([-*+]|\\d+[.)])(\\s)"));
    static const QRegularExpression hr(QStringLiteral("^\\s*(?:-{3,}|\\*{3,}|_{3,})\\s*$"));
    static const QRegularExpression bold(QStringLiteral("\\*\\*([^*\\n]+)\\*\\*"));
    static const QRegularExpression italic(QStringLiteral("(?<!\\*)\\*([^*\\n]+)\\*(?!\\*)"));
    static const QRegularExpression strike(QStringLiteral("~~([^~\\n]+)~~"));
    static const QRegularExpression code(QStringLiteral("`([^`\\n]+)`"));
    static const QRegularExpression link(QStringLiteral("\\[([^\\]\\n]*)\\]\\(([^)\\n]*)\\)"));

    // 围栏代码块：围栏行本身也按块内样式画，进/出各翻转一次状态
    const bool fenceLine = fence.match(text).hasMatch();
    if (prev == 1 || fenceLine) {
      setFormat(0, text.length(), code_);
      setCurrentBlockState(fenceLine ? (prev == 1 ? 0 : 1) : 1);
      return;
    }
    setCurrentBlockState(0);

    const qreal basePt = document()->defaultFont().pointSizeF() > 0
                             ? document()->defaultFont().pointSizeF()
                             : 10.5;

    // 标题：整行放大加粗，# 号本身淡化
    const auto hm = head.match(text);
    if (hm.hasMatch()) {
      const int level = hm.capturedLength(1);
      QTextCharFormat f = head_;
      f.setFontPointSize(basePt * (level == 1 ? 1.6 : level == 2 ? 1.38 : level == 3 ? 1.2 : 1.08));
      setFormat(0, text.length(), f);
      setFormat(0, level, marker_);
    }

    // 引用：整行弱化，> 号再淡一层
    const auto qm = quote.match(text);
    if (qm.hasMatch()) {
      setFormat(0, text.length(), quote_);
      setFormat(qm.capturedStart(), qm.capturedLength(), marker_);
    }

    // 列表符号（- * + / 1. 2) ）
    const auto bm = bullet.match(text);
    if (bm.hasMatch()) setFormat(bm.capturedStart(2), bm.capturedLength(2), marker_);

    // 分割线
    if (hr.match(text).hasMatch()) setFormat(0, text.length(), marker_);

    // 行内：粗体 / 斜体 / 删除线 / 代码 / 链接，标记符统一淡化，内容按语义上样式
    auto run = [this, &text](const QRegularExpression& re, int markLen, const QTextCharFormat& body,
                             bool markBothSides) {
      auto it = re.globalMatch(text);
      while (it.hasNext()) {
        const auto m = it.next();
        if (m.capturedStart(1) < 0) continue;
        if (markBothSides) {
          setFormat(m.capturedStart(), markLen, marker_);
          setFormat(m.capturedEnd() - markLen, markLen, marker_);
        }
        setFormat(m.capturedStart(1), m.capturedLength(1), body);
      }
    };
    run(bold, 2, bold_, true);
    run(italic, 1, italic_, true);
    run(strike, 2, strike_, true);
    run(code, 1, code_, true);

    // 链接：[文字](地址)：文字上链接色，括号部分淡化
    static const QRegularExpression img(QStringLiteral("!\\[([^\\]\\n]*)\\]\\(([^)\\n]*)\\)"));
    auto lit = link.globalMatch(text);
    while (lit.hasNext()) {
      const auto m = lit.next();
      // 图片 ![x](y) 也命中 [x](y)，感叹号交给图片规则淡掉
      setFormat(m.capturedStart(), 1, marker_);
      setFormat(m.capturedStart(1), m.capturedLength(1), link_);
      setFormat(m.capturedStart(2) - 2, 2 + m.capturedLength(2) + 1, marker_);
    }
    auto iit = img.globalMatch(text);
    while (iit.hasNext()) {
      const auto m = iit.next();
      setFormat(m.capturedStart(), 1, marker_);
    }

    // 任务列表：- [ ] / - [x]，方框按代码样式强调，省得看成普通列表
    static const QRegularExpression task(QStringLiteral("^\\s*[-*+]\\s+(\\[[ xX]\\])"));
    const auto tm = task.match(text);
    if (tm.hasMatch()) setFormat(tm.capturedStart(1), tm.capturedLength(1), code_);

    // 表格：竖线淡化，分隔行（|---|:--:|）整行弱化
    if (text.contains(QLatin1Char('|'))) {
      static const QRegularExpression tsep(QStringLiteral("^\\s*\\|[\\s:|-]*\\|\\s*$"));
      if (tsep.match(text).hasMatch()) setFormat(0, text.length(), quote_);
      static const QRegularExpression pipe(QStringLiteral("\\|"));
      auto pit = pipe.globalMatch(text);
      while (pit.hasNext()) setFormat(pit.next().capturedStart(), 1, marker_);
    }
  }

 private:
  QTextCharFormat marker_, head_, bold_, italic_, strike_, code_, quote_, link_;
  bool hideMarkers_ = false;  // 隐藏标记符：把 # ** ` 等画没，只留渲染效果
};

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

  // 输入区即时 Markdown 渲染：边打字边出标题/粗体/代码块等样式，不再另开预览面板
  mdStyle_ = new MdStyle(edit_->document());

  // ---- 顶栏（分两行；卡片窄的时候把次要按钮依次收进「⋯」，不再把按钮挤到互相叠压）
  auto* bar = new QWidget(this);
  auto* barCol = new QVBoxLayout(bar);
  barCol->setContentsMargins(0, 0, 0, 0);
  barCol->setSpacing(4);

  auto* row1 = new OverflowBar(bar);
  auto* row2 = new OverflowBar(bar);
  barCol->addWidget(row1);
  barCol->addWidget(row2);

  // prio：0 常驻；数字越大越早收进「⋯」菜单（打开/保存永远留在条上）
  auto mkBtn = [](OverflowBar* lay, const QString& text, const QString& tip, int prio = 0) {
    auto* b = new QPushButton(text);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    lay->Add(b, prio);
    return b;
  };
  auto mkTool = [](OverflowBar* lay, const QString& text, const QString& tip, int prio = 0) {
    auto* b = new QToolButton();
    b->setText(text);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    lay->Add(b, prio);
    return b;
  };
  auto mkSep = [](OverflowBar* lay) { lay->AddSeparator(); };

  auto* openBtn = mkBtn(row1, QStringLiteral("打开"), QStringLiteral("打开文本文件（Ctrl+O）"));
  connect(openBtn, &QPushButton::clicked, this, [this]() { OpenWithDialog(); });

  auto* saveBtn = mkBtn(row1, QStringLiteral("保存"), QStringLiteral("保存（Ctrl+S）"));
  connect(saveBtn, &QPushButton::clicked, this, [this]() { SaveNow(); });

  auto* saveAsBtn = mkBtn(row1, QStringLiteral("另存为"), QStringLiteral("另存为"), 2);
  connect(saveAsBtn, &QPushButton::clicked, this, [this]() {
    const QString p = QFileDialog::getSaveFileName(
        this, QStringLiteral("另存为"),
        path_.isEmpty() ? QStringLiteral("untitled.txt") : path_, QStringLiteral("文本 (*.txt)"));
    if (!p.isEmpty()) SaveTo(p);
  });

  mkSep(row1);
  undoBtn_ = mkTool(row1, QStringLiteral("撤销"), QStringLiteral("撤销（Ctrl+Z）"), 3);
  undoBtn_->setEnabled(false);
  connect(undoBtn_, &QToolButton::clicked, edit_, &QPlainTextEdit::undo);
  connect(edit_, &QPlainTextEdit::undoAvailable, undoBtn_, &QToolButton::setEnabled);

  redoBtn_ = mkTool(row1, QStringLiteral("重做"), QStringLiteral("重做（Ctrl+Y）"), 3);
  redoBtn_->setEnabled(false);
  connect(redoBtn_, &QToolButton::clicked, edit_, &QPlainTextEdit::redo);
  connect(edit_, &QPlainTextEdit::redoAvailable, redoBtn_, &QToolButton::setEnabled);

  mkSep(row1);
  auto* findBtn = mkTool(row1, QStringLiteral("查找"), QStringLiteral("查找（Ctrl+F）"), 4);
  connect(findBtn, &QToolButton::clicked, this, [this]() {
    ShowFind(!findBar_->isVisible());
  });
  auto* replBtn = mkTool(row1, QStringLiteral("替换"), QStringLiteral("替换（Ctrl+H）"), 4);
  connect(replBtn, &QToolButton::clicked, this, [this]() { ShowFind(true, true); });
  row1->AddStretch(1);

  fontBox_ = new FontCombo();
  fontBox_->setMinimumWidth(88);  // 可压缩：卡片窄时先让给按钮
  fontBox_->setMaximumWidth(200);
  fontBox_->SetFamily(edit_->font().families().value(0));
  row2->Add(fontBox_, 0, 1);
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

  wrapBtn_ = mkTool(row2, QStringLiteral("换行：开"), QStringLiteral("自动换行开关（Alt+Z）"), 2);
  connect(wrapBtn_, &QToolButton::clicked, this, [this]() { SetWrap(!wrap_); });

  // Markdown 渲染总开关：关掉连语法着色一起停，编辑区退回纯文本视图
  mdOnBtn_ = mkTool(row2, QStringLiteral("Markdown：开"),
                    QStringLiteral("Markdown 渲染开关（Alt+M）：关掉后不再着色、不再弱化标记符，回到纯文本编辑"),
                    3);
  connect(mdOnBtn_, &QToolButton::clicked, this, [this]() { SetMarkdown(!mdOn_); });

  mdHideBtn_ = mkTool(row2, QStringLiteral("标记：显示"),
                      QStringLiteral("隐藏 / 显示 Markdown 标记符（# ** ` > 等）：只改显示，文字一个字符都不动"),
                      3);
  connect(mdHideBtn_, &QToolButton::clicked, this, [this]() { SetHideMarkers(!hideMd_); });

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

  mkSep(row2);
  row2->AddStretch(1);

  root->addWidget(bar);

  BuildFindBar();
  root->addWidget(findBar_);

  // Markdown 就写在编辑区里，不再另开渲染分栏：Ctrl+Alt 唤起格式工具栏即可排版
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

  // AI 写回确认条：紧贴底部，写完文档抬出来问一句「同意不同意」
  BuildAiBar();
  root->addWidget(aiBar_);
  ShowAiBar(false);

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
  // 隐藏标记时其实是"只看排版"的浏览态：光标也一并藏起来。一旦点进输入区、
  // 敲键或走输入法，就自动切回显示状态，标记和光标一起回来，不用再去点按钮。
  if (mdOn_ && hideMd_ && edit_ && (obj == edit_ || obj == edit_->viewport())) {
    const QEvent::Type t = ev->type();
    if (t == QEvent::MouseButtonPress || t == QEvent::MouseButtonDblClick || t == QEvent::KeyPress ||
        t == QEvent::InputMethod) {
      SetHideMarkers(false);
    }
  }
  if (edit_ && obj == edit_->viewport() && ev->type() == QEvent::Wheel) {
    auto* we = static_cast<QWheelEvent*>(ev);
    if (we->modifiers() & Qt::ControlModifier) {
      SetFontSize(pt_ + (we->angleDelta().y() > 0 ? 1.0 : -1.0));
      return true;
    }
  }
  // 深色/亮色切换后，Markdown 样式按新调色板重取色
  if ((ev->type() == QEvent::PaletteChange || ev->type() == QEvent::ApplicationPaletteChange) &&
      mdStyle_) {
    mdStyle_->Recolor();
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
    if (alt && ke->key() == Qt::Key_M) { SetMarkdown(!mdOn_); return true; }
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

// Markdown 渲染总开关。关掉时把样式器从文档上摘掉，编辑区就是干净的纯文本，
// 字符、光标、撤销、查找、AI 写回全程不受影响；重新打开时再挂回去并全量刷一遍。
void NotepadPanel::SetMarkdown(bool on) {
  mdOn_ = on;
  if (edit_) {
    if (on) {
      if (!mdStyle_) {
        mdStyle_ = new MdStyle(edit_->document());
        mdStyle_->SetHideMarkers(hideMd_);
      }
      if (mdStyle_) mdStyle_->Recolor();
    } else {
      // 隐藏标记符是"看排版"的浏览态，Markdown 都关了就没有意义，顺手复位
      hideMd_ = false;
      if (mdHideBtn_) mdHideBtn_->setText(QStringLiteral("标记：显示"));
      if (mdStyle_) {
        mdStyle_->setDocument(nullptr);  // 纯文本视图：不再着色，原文原样显示
        delete mdStyle_;
        mdStyle_ = nullptr;
      }
    }
    edit_->setCursorWidth(1);
    edit_->viewport()->update();
    edit_->update();
  }
  if (mdOnBtn_) mdOnBtn_->setText(on ? QStringLiteral("Markdown：开") : QStringLiteral("Markdown：关"));
  if (mdHideBtn_) mdHideBtn_->setEnabled(on);
  SavePrefs();
}

// 只把 Markdown 标记符画没（# ** ` > - 等），正文一个字都不改：
// 想专心看排版时按一下，需要改源码时再按回来。
void NotepadPanel::SetHideMarkers(bool on) {
  if (!mdOn_) on = false;  // Markdown 关着时没有"标记符"可藏
  hideMd_ = on;
  if (mdStyle_) mdStyle_->SetHideMarkers(on);
  if (mdHideBtn_) mdHideBtn_->setText(on ? QStringLiteral("标记：隐藏") : QStringLiteral("标记：显示"));
  if (edit_) {
    // 隐藏标记是"只看排版"的浏览态，光标也一起收掉（宽度 0 即不绘制，编辑能力不受影响）；
    // 点进来输入时 eventFilter 会自动切回显示，光标随标记一起恢复。
    edit_->setCursorWidth(on ? 0 : 1);
    edit_->viewport()->update();
    edit_->update();
  }
  SavePrefs();
}

// 字体/字号/换行状态记住，下次打开还是上回的样子
void NotepadPanel::LoadPrefs() {
  QSettings st(QStringLiteral("WorkDesk"), QStringLiteral("WorkDeskQt"));
  const QString fam = st.value(QStringLiteral("notepad/font")).toString();
  const qreal pt = st.value(QStringLiteral("notepad/size"), pt_).toDouble();
  const bool wrap = st.value(QStringLiteral("notepad/wrap"), wrap_).toBool();
  const bool hideMd = st.value(QStringLiteral("notepad/hidemd"), hideMd_).toBool();
  const bool mdOn = st.value(QStringLiteral("notepad/markdown"), mdOn_).toBool();
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
  SetMarkdown(mdOn);
  SetHideMarkers(hideMd);
  edit_->RefreshGutter();
}

void NotepadPanel::SavePrefs() {
  QSettings st(QStringLiteral("WorkDesk"), QStringLiteral("WorkDeskQt"));
  const QStringList fams = edit_->font().families();
  if (!fams.isEmpty()) st.setValue(QStringLiteral("notepad/font"), fams.first());
  st.setValue(QStringLiteral("notepad/size"), pt_);
  st.setValue(QStringLiteral("notepad/wrap"), wrap_);
  st.setValue(QStringLiteral("notepad/markdown"), mdOn_);
  st.setValue(QStringLiteral("notepad/hidemd"), hideMd_);
}

void NotepadPanel::SetFontSize(qreal pt) {
  pt_ = qBound<qreal>(7.0, pt, 32.0);
  QFont f = edit_->font();
  f.setPointSizeF(pt_);
  edit_->setFont(f);
  edit_->setTabStopDistance(4 * QFontMetricsF(edit_->font()).horizontalAdvance(QLatin1Char(' ')));
  if (mdStyle_) mdStyle_->Recolor();  // 标题字号按新基准重算
  edit_->RefreshGutter();
  edit_->viewport()->update();
  SavePrefs();
}

QString NotepadPanel::PlainText() const { return edit_ ? edit_->toPlainText() : QString(); }

// 给 AI 的上下文只截前 maxChars 字符：超大文档整篇 toPlainText() 会瞬间吃掉几百 MB 内存，
// 在内存吃紧时直接抛 std::bad_alloc，异常穿过 Qt 事件处理器还会把进程 abort 掉。
QString NotepadPanel::PlainTextSnippet(int maxChars) const {
  if (!edit_) return QString();
  const QTextDocument* doc = edit_->document();
  if (!doc) return QString();
  const int total = doc->characterCount();  // 含结尾的段落分隔符
  if (total <= maxChars + 1) return edit_->toPlainText();
  QTextCursor c = edit_->textCursor();
  c.setPosition(0);
  c.setPosition(qMin(maxChars, total - 1), QTextCursor::KeepAnchor);
  QString s = c.selectedText();
  s.replace(QChar(0x2029), QLatin1Char('\n'));  // 段落分隔符换回换行
  return s;
}

QString NotepadPanel::DisplayName() const {
  if (path_.isEmpty()) return QStringLiteral("未命名文档");
  return QFileInfo(path_).fileName();
}

// AI 写回：整篇替换走一次编辑块，用户 Ctrl+Z 能整体退回
void NotepadPanel::ReplaceAllText(const QString& text) {
  if (!edit_) return;
  aiBefore_ = edit_->toPlainText();
  aiWasDirty_ = dirty_;
  QTextCursor c(edit_->document());
  c.beginEditBlock();
  c.select(QTextCursor::Document);
  c.insertText(text);
  c.endEditBlock();
  dirty_ = true;
  UpdateInfo();
  // 导入完直接落到文末：改动通常收在结尾，眼睛不用再往上找
  ScrollToEnd();
  if (aiLab_) {
    aiLab_->setText(QStringLiteral("AI 已改写《%1》，是否保留这次改动？").arg(DisplayName()));
  }
  ShowAiBar(true);
  emit Notice(QStringLiteral("已导入 AI 改动：%1").arg(DisplayName()));
}

void NotepadPanel::AcceptAiWrite() {
  ShowAiBar(false);
  ScrollToEnd();
  emit Notice(QStringLiteral("已保留 AI 改动，Ctrl+S 保存到文件"));
}

void NotepadPanel::RejectAiWrite() {
  if (!edit_) return;
  // 先试一次撤销（写回整段在一个编辑块里）；万一中间又编辑过，直接按快照回滚
  edit_->undo();
  if (edit_->toPlainText() != aiBefore_) {
    QSignalBlocker b(edit_);
    edit_->setPlainText(aiBefore_);
  }
  dirty_ = aiWasDirty_;
  UpdateInfo();
  ShowAiBar(false);
  emit Notice(QStringLiteral("已退回 AI 改动前的版本"));
}

void NotepadPanel::ShowAiBar(bool on) {
  if (!aiBar_) return;
  aiBar_->setVisible(on);
  if (on) ScrollToEnd();
}

void NotepadPanel::ScrollToEnd() {
  if (!edit_) return;
  edit_->moveCursor(QTextCursor::End);
  edit_->ensureCursorVisible();
  if (QScrollBar* sb = edit_->verticalScrollBar()) sb->setValue(sb->maximum());
  // 确认条一出现，编辑区高度会变，下一次事件循环再贴到底
  QTimer::singleShot(0, this, [this]() {
    if (!edit_) return;
    if (QScrollBar* sb = edit_->verticalScrollBar()) sb->setValue(sb->maximum());
  });
}

void NotepadPanel::BuildAiBar() {
  aiBar_ = new QWidget(this);
  aiBar_->setObjectName("AiWriteBar");
  aiBar_->setAutoFillBackground(true);
  {  // 底色取调色板的 AlternateBase：深浅主题下自动有区分，不用写死颜色
    QPalette p = aiBar_->palette();
    p.setColor(QPalette::Window, p.color(QPalette::AlternateBase));
    aiBar_->setPalette(p);
  }
  auto* lay = new QHBoxLayout(aiBar_);
  lay->setContentsMargins(8, 4, 8, 4);
  lay->setSpacing(8);
  aiLab_ = new QLabel(aiBar_);
  aiLab_->setObjectName("ModuleHint");
  aiLab_->setWordWrap(true);
  lay->addWidget(aiLab_, 1);
  auto* keep = new QPushButton(QStringLiteral("同意"), aiBar_);
  keep->setObjectName("CodeCopy");
  keep->setCursor(Qt::PointingHandCursor);
  keep->setToolTip(QStringLiteral("保留 AI 写进记事本的内容"));
  auto* drop = new QPushButton(QStringLiteral("不同意"), aiBar_);
  drop->setObjectName("CodeCopy");
  drop->setCursor(Qt::PointingHandCursor);
  drop->setToolTip(QStringLiteral("整篇退回 AI 写回之前的版本"));
  lay->addWidget(keep);
  lay->addWidget(drop);
  connect(keep, &QPushButton::clicked, this, &NotepadPanel::AcceptAiWrite);
  connect(drop, &QPushButton::clicked, this, &NotepadPanel::RejectAiWrite);
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
    const QString p = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存为"), QStringLiteral("untitled.txt"),
        QStringLiteral("文本 (*.txt);;Markdown (*.md);;所有文件 (*.*)"));
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
    const QString p = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存为"), QStringLiteral("untitled.txt"),
        QStringLiteral("文本 (*.txt);;Markdown (*.md);;所有文件 (*.*)"));
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
