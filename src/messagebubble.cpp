#include "messagebubble.h"
#include "wdtrace.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

#include "markdown.h"
#include "syntaxhl.h"

namespace wd {
namespace {

QFont MonoFont() {
  // 全部走系统已安装字体（QFont 按族名向系统字体库查找，不打包任何字体文件）
  QFont f;
  f.setFamilies({"Consolas", "Cascadia Mono", "DejaVu Sans Mono", "Microsoft YaHei UI"});
  f.setStyleHint(QFont::Monospace);
  f.setFixedPitch(true);
  f.setPointSizeF(10.0);
  return f;
}

QString FromUtf8(const std::string& s) { return QString::fromStdString(s); }

// 可点击的图片标签
class ClickableLabel : public QLabel {
 public:
  std::function<void()> onClick;
  using QLabel::QLabel;

 protected:
  void mouseReleaseEvent(QMouseEvent* e) override {
    if (e->button() == Qt::LeftButton && onClick) onClick();
    QLabel::mouseReleaseEvent(e);
  }
};

}  // namespace

// ------------------------------------------------------------------ 块基类
class MdBlockWidget : public QWidget {
 public:
  explicit MdBlockWidget(QWidget* p) : QWidget(p) {}
  virtual void SetContent(const MdBlock& b) = 0;
};

// ------------------------------------------------------------------ 文本块
// 高度自适应的 QTextBrowser。之前只在 TextBlock 自己的 resizeEvent 里重算高度，
// 会出现"用旧视口宽度算出的高度被 setFixedHeight 固定后，再没有任何时机纠正"，
// 结果气泡底部留出一大片空白。这里让视口一变化就回调重算。
class AutoHeightBrowser : public QTextBrowser {
 public:
  explicit AutoHeightBrowser(QWidget* p) : QTextBrowser(p) {}
  std::function<void()> onLayoutChanged;

 protected:
  void resizeEvent(QResizeEvent* e) override {
    QTextBrowser::resizeEvent(e);
    if (onLayoutChanged) onLayoutChanged();
  }
};

class TextBlock : public MdBlockWidget {
 public:
  explicit TextBlock(QWidget* p) : MdBlockWidget(p) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    view_ = new AutoHeightBrowser(this);
    view_->onLayoutChanged = [this]() { Adjust(); };
    view_->setOpenExternalLinks(true);
    view_->setFrameShape(QFrame::NoFrame);
    view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    view_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    view_->document()->setDocumentMargin(2);
    view_->setStyleSheet("QTextBrowser { background:transparent; border:none; }");
    lay->addWidget(view_);
  }

  void SetContent(const MdBlock& b) override {
    const QString md = FromUtf8(b.text);
    if (md == last_) return;
    if (!last_.isEmpty() && md.startsWith(last_)) {
      const QString extra = md.mid(last_.size());
      // 纯文本增量：直接往文末插入。原来每帧 setMarkdown 会把整段重新解析排版，
      // 文本越长越慢，表现出来的就是"打一段停一下"。
      if (!HasMdSyntax(extra)) {
        QTextCursor c(view_->document());
        c.movePosition(QTextCursor::End);
        c.insertText(extra);
        last_ = md;
        Adjust();
        return;
      }
      // 带 markdown 标记的尾巴必须整段重排才成型，那就限频到 80ms 一次
      if (throttle_.isValid() && throttle_.elapsed() < 80 && extra.size() < 400) return;
    }
    last_ = md;
    view_->setMarkdown(md);
    throttle_.restart();
    Adjust();
    // 布局尚未完成时视口宽度还不是最终值，补一次延迟重算
    QTimer::singleShot(0, this, [this]() { Adjust(); });
  }

 protected:
  void resizeEvent(QResizeEvent* e) override {
    MdBlockWidget::resizeEvent(e);
    Adjust();
  }

  void showEvent(QShowEvent* e) override {
    MdBlockWidget::showEvent(e);
    Adjust();
  }

 private:
  // 流式增量判断：这段新文本里有没有"必须整段重排才成立"的 markdown 标记
  static bool HasMdSyntax(const QString& s) {
    for (const QChar ch : s) {
      const ushort u = ch.unicode();
      if (u == '*' || u == '`' || u == '#' || u == '[' || u == ']' || u == '|' || u == '_')
        return true;
    }
    return false;
  }

  void Adjust() {
    if (adjusting_) return;
    adjusting_ = true;
    const int w = view_->viewport()->width();
    if (w > 20) {
      auto* doc = view_->document();
      if (std::fabs(doc->textWidth() - (qreal)w) > 0.5) doc->setTextWidth((qreal)w);
      const int h = (int)std::ceil(doc->documentLayout()->documentSize().height()) + 6;
      if (h != view_->height()) view_->setFixedHeight(h);
      if (h != height()) setFixedHeight(h);
    }
    adjusting_ = false;
  }

  AutoHeightBrowser* view_ = nullptr;
  QString last_;
  QElapsedTimer throttle_;  // 全量重排限频，别让流式把 UI 线程拖死
  bool adjusting_ = false;
};

// ------------------------------------------------------------------ 代码块
class CodeBlock : public MdBlockWidget {
 public:
  explicit CodeBlock(QWidget* p) : MdBlockWidget(p) {
    auto* frame = new QFrame(this);
    frame->setObjectName("CodeWrap");
    frame->setStyleSheet(
        "QFrame#CodeWrap { background:#1A1D22; border:1px solid #2F343B; border-radius:8px; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(frame);

    auto* lay = new QVBoxLayout(frame);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* head = new QWidget(frame);
    head->setObjectName("CodeHead");
    head->setStyleSheet(
        "QWidget#CodeHead { background:#212630; border:none; border-top-left-radius:8px;"
        " border-top-right-radius:8px; }");
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(10, 4, 6, 4);
    hl->setSpacing(8);

    langLab_ = new QLabel(head);
    langLab_->setStyleSheet(
        "color:#8FB6FF; font-size:11px; font-weight:600; background:transparent;");
    hl->addWidget(langLab_);

    info_ = new QLabel(head);
    info_->setStyleSheet("color:#6E7885; font-size:11px; background:transparent;");
    hl->addWidget(info_);
    hl->addStretch(1);

    copy_ = new QPushButton(QStringLiteral("复制"), head);
    copy_->setCursor(Qt::PointingHandCursor);
    copy_->setStyleSheet(
        "QPushButton { background:transparent; border:none; color:#8A929C; padding:2px 6px;"
        " font-size:11px; } QPushButton:hover { color:#CFE0FF; }");
    connect(copy_, &QPushButton::clicked, this, [this]() {
      QGuiApplication::clipboard()->setText(code_);
      copy_->setText(QStringLiteral("已复制"));
      QTimer::singleShot(1200, copy_, [this]() { copy_->setText(QStringLiteral("复制")); });
    });
    hl->addWidget(copy_);

    lay->addWidget(head);

    edit_ = new QPlainTextEdit(frame);
    edit_->setReadOnly(true);
    edit_->setFrameShape(QFrame::NoFrame);
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);  // 关键：长行不裁切，出横向滚动条
    edit_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    edit_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit_->setFont(MonoFont());
    edit_->setTabStopDistance(4 * QFontMetricsF(MonoFont()).horizontalAdvance(QLatin1Char(' ')));
    edit_->setStyleSheet(
        "QPlainTextEdit { background:#1A1D22; border:none; padding:4px 8px; }"
        "QPlainTextEdit QScrollBar:horizontal { height:9px; background:#15181C; }"
        "QPlainTextEdit QScrollBar::handle:horizontal { background:#4A5563; border-radius:4px;"
        " min-width:24px; }");
    edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lay->addWidget(edit_);
  }

  void SetContent(const MdBlock& b) override {
    const QString code = FromUtf8(b.text);
    const std::string lang = b.lang;
    if (code != code_ || lang != curLang_) {
      const bool langChanged = (lang != curLang_);
      // 流式追加：只把新尾巴插进文档，老内容一个字都不重排。
      // 原来每帧 setPlainText + 重建 highlighter，200 行代码等于每 16ms 全量重高亮一次，
      // 主线程直接被拖死，表现出来就是"打一段停一下"。
      const bool appendOnly = !langChanged && !code_.isEmpty() && code.startsWith(code_);
      if (appendOnly) {
        QTextCursor c(edit_->document());
        c.movePosition(QTextCursor::End);
        c.insertText(code.mid(code_.size()));  // 高亮器会自动只处理受影响的块
        code_ = code;
      } else {
        code_ = code;
        edit_->setPlainText(code);
        if (langChanged || !hl_) {
          curLang_ = lang;
          if (lang.empty() || lang == "text") {
            hl_.reset();
          } else {
            hl_ = std::make_unique<CodeHighlighter>(edit_->document(), lang);
            hl_->rehighlight();
          }
        }
      }
      copy_->setText(QStringLiteral("复制"));
    }
    langLab_->setText(FromUtf8(LangDisplayName(curLang_)));
    const int lines = code_.isEmpty() ? 1 : (code_.count(QLatin1Char('\n')) + 1);
    info_->setText(QStringLiteral("%1 行").arg(lines));
    Adjust(lines);
  }

 protected:
  void resizeEvent(QResizeEvent* e) override {
    MdBlockWidget::resizeEvent(e);
    const int lines = code_.isEmpty() ? 1 : (code_.count(QLatin1Char('\n')) + 1);
    Adjust(lines);
  }

 private:
  void Adjust(int lines) {
    const int lh = edit_->fontMetrics().lineSpacing();
    int h = lines * lh + 14;
    h = std::max(h, lh * 2 + 10);
    h = std::min(h, 460);  // 超长代码块内部滚动，避免一条消息吃掉整个视野
    if (h != edit_->height()) edit_->setFixedHeight(h);
  }

  QLabel* langLab_ = nullptr;
  QLabel* info_ = nullptr;
  QPushButton* copy_ = nullptr;
  QPlainTextEdit* edit_ = nullptr;
  std::unique_ptr<CodeHighlighter> hl_;
  QString code_;
  std::string curLang_;
};

// ------------------------------------------------------------------ 表格块
class TableBlock : public MdBlockWidget {
 public:
  explicit TableBlock(QWidget* p) : MdBlockWidget(p) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 2, 0, 2);
    table_ = new QTableWidget(this);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setWordWrap(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->verticalHeader()->setVisible(false);
    table_->setFont(MonoFont());
    lay->addWidget(table_);
  }

  void SetContent(const MdBlock& b) override {
    if (b.rows.empty()) return;
    const size_t cols = b.rows[0].size();
    table_->clear();
    table_->setColumnCount((int)cols);
    table_->setRowCount((int)b.rows.size() - 1);
    QStringList headers;
    for (size_t c = 0; c < cols; ++c) headers << FromUtf8(b.rows[0][c]);
    table_->setHorizontalHeaderLabels(headers);
    for (size_t r = 1; r < b.rows.size(); ++r) {
      for (size_t c = 0; c < cols; ++c) {
        const QString v = c < b.rows[r].size() ? FromUtf8(b.rows[r][c]) : QString();
        table_->setItem((int)r - 1, (int)c, new QTableWidgetItem(v));
      }
    }
    table_->resizeColumnsToContents();
    for (int c = 0; c < table_->columnCount(); ++c)
      if (table_->columnWidth(c) > 420) table_->setColumnWidth(c, 420);

    int h = table_->horizontalHeader()->height() + 2;
    for (int r = 0; r < table_->rowCount(); ++r) h += table_->rowHeight(r);
    h = std::max(h, 64);
    h = std::min(h, 380);
    table_->setFixedHeight(h);
  }

 private:
  QTableWidget* table_ = nullptr;
};

// ------------------------------------------------------------------ 气泡
MessageBubble::MessageBubble(const ChatMessage& msg, QWidget* parent)
    : QWidget(parent), msg_(msg), text_(FromUtf8(msg.text)) {
  wd::Trace("mb: ctor enter");
  // 只有 AI 的空消息才算"流式中"；用户消息即便一个字没打、只丢了附件/图片，也是已完成的
  streaming_ = !msg.user && msg.text.empty();
  wd::Trace("mb: before buildui");
  BuildUi();
  wd::Trace("mb: buildui ok");
  SyncBlocks(true);
}

MessageBubble::~MessageBubble() = default;

QString MessageBubble::PlainText() const { return text_; }

void MessageBubble::BuildUi() {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  row_ = new QWidget(this);
  auto* rowLay = new QHBoxLayout(row_);
  rowLay->setContentsMargins(0, 0, 0, 0);
  rowLay->setSpacing(8);

  if (msg_.user) {
    rowLay->addStretch(1);
  } else {
    avatar_ = new QLabel(QStringLiteral("AI"), row_);
    avatar_->setFixedSize(28, 28);
    avatar_->setAlignment(Qt::AlignCenter);
    avatar_->setStyleSheet(
        "background:#34405A; color:#CFE0FF; border-radius:14px; font-size:11px; font-weight:700;");
    rowLay->addWidget(avatar_, 0, Qt::AlignTop);
  }

  bubble_ = new QFrame(row_);
  bubble_->setObjectName("Bubble");
  bubble_->setStyleSheet(msg_.user
                             ? "QFrame#Bubble { background:#3A5B96; border:1px solid #47679F;"
                               " border-radius:10px; }"
                             : "QFrame#Bubble { background:#262A30; border:1px solid #2F343B;"
                               " border-radius:10px; }");
  bubble_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

  bubbleLay_ = new QVBoxLayout(bubble_);
  bubbleLay_->setContentsMargins(12, 9, 12, 9);
  bubbleLay_->setSpacing(4);

  content_ = new QWidget(bubble_);
  content_->setStyleSheet("background:transparent;");
  contentLay_ = new QVBoxLayout(content_);
  contentLay_->setContentsMargins(0, 0, 0, 0);
  contentLay_->setSpacing(6);
  bubbleLay_->addWidget(content_);

  meta_ = new QLabel(QStringLiteral("正在生成…"), bubble_);
  meta_->setStyleSheet("color:#9FB4D8; font-size:11px; background:transparent;");
  meta_->setVisible(streaming_);
  bubbleLay_->addWidget(meta_);

  // 附件区
  attach_ = new QWidget(bubble_);
  attach_->setStyleSheet("background:transparent;");
  attachLay_ = new QHBoxLayout(attach_);
  attachLay_->setContentsMargins(0, 4, 0, 0);
  attachLay_->setSpacing(6);
  attach_->setVisible(false);
  bubbleLay_->addWidget(attach_);

  for (int i = 0; i < (int)msg_.images.size(); ++i) {
    const ChatImage& img = msg_.images[i];
    auto* lab = new ClickableLabel(attach_);
    QImage qimg;
    if (!img.path.empty()) qimg.load(QString::fromStdWString(img.path));
    if (qimg.isNull() && !img.b64.empty()) {
      qimg.loadFromData(QByteArray::fromBase64(QByteArray::fromStdString(img.b64)));
    }
    if (!qimg.isNull()) {
      const QPixmap pix = QPixmap::fromImage(qimg).scaled(220, 220, Qt::KeepAspectRatio,
                                                          Qt::SmoothTransformation);
      lab->setPixmap(pix);
      lab->setFixedSize(pix.size());
      lab->setCursor(Qt::PointingHandCursor);
      lab->setStyleSheet("border-radius:6px; background:transparent;");
      lab->setToolTip(QStringLiteral("点击查看原图"));
      lab->onClick = [this, img, i]() {
        const QString p = QString::fromStdWString(img.path);
        emit ImageClicked(p);
        emit FileClicked(p, i);
      };
    } else {
      lab->setText(QStringLiteral("[图片] ") + FromUtf8(img.name));
      lab->setStyleSheet("color:#C3CAD3;");
    }
    attachLay_->addWidget(lab, 0, Qt::AlignTop);
  }
  for (const auto& f : msg_.files) {
    // 附件必须可点：以前用纯 QLabel，鼠标点了没有任何反应，用户看到的就是"附件打不开"
    auto* lab = new ClickableLabel(attach_);
    lab->setText(QStringLiteral("附件  ") + FromUtf8(f.name) + QStringLiteral("  ") +
                 FromUtf8(HumanSize(f.size)));
    lab->setStyleSheet(
        "background:#1E2126; color:#C3CAD3; border:1px solid #3A414A; border-radius:6px;"
        " padding:3px 8px; font-size:12px;");
    lab->setCursor(Qt::PointingHandCursor);
    lab->setToolTip(QStringLiteral("点击用默认程序打开"));
    const QString path = QString::fromStdWString(f.path);
    lab->onClick = [this, path]() { emit FileClicked(path, -1); };
    attachLay_->addWidget(lab, 0, Qt::AlignTop);
  }
  if (attachLay_->count() > 0) attach_->setVisible(true);

  rowLay->addWidget(bubble_, 0, Qt::AlignTop);
  if (!msg_.user) rowLay->addStretch(1);

  outer->addWidget(row_);
}

void MessageBubble::UpdateBubbleWidth() {
  if (!bubble_) return;
  const int avail = width() > 60 ? width() : 600;

  // 用户消息：保持紧凑，按内容收敛，不跟着 AI 一起铺宽。
  if (msg_.user) {
    int maxW = std::max(200, (int)(avail * 0.62));
    if (maxW > 600) maxW = 600;
    int want = maxW;
    if (text_.trimmed().isEmpty()) {
      if (attach_ && attach_->isVisible()) {
        const int hint = attach_->sizeHint().width() + 24;
        want = std::min(maxW, std::max(hint, 200));
      } else {
        want = std::min(maxW, 240);
      }
    }
    if (want < 120) want = 120;
    bubble_->setMaximumWidth(maxW);
    if (lastBubbleW_ != want) {
      lastBubbleW_ = want;
      bubble_->setFixedWidth(want);
    }
    return;
  }

  // AI 消息：容器宽度固定铺满可用宽度（扣掉左侧头像占位）。
  // 固定宽度而不是"随内容伸缩"，打字机逐字吐字时气泡左右不会来回抖动，
  // 整段回复从第一个字到结束阅读区宽度完全一致。
  const int inner = std::max(240, avail - 40);  // 40 = 头像 28 + 间距 8 + 余量
  int maxW = std::max(320, (int)(inner * 0.98));
  if (maxW > 1100) maxW = 1100;
  const bool empty = text_.trimmed().isEmpty() && !(attach_ && attach_->isVisible());
  const int want = empty ? std::min(maxW, 200) : maxW;  // 空占位（正在生成…）不占满屏
  bubble_->setMaximumWidth(maxW);
  if (lastBubbleW_ != want) {
    lastBubbleW_ = want;
    bubble_->setFixedWidth(want);
  }
}

void MessageBubble::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  UpdateBubbleWidth();
}

bool MessageBubble::eventFilter(QObject* obj, QEvent* ev) { return QWidget::eventFilter(obj, ev); }

void MessageBubble::SyncBlocks(bool force) {
  if (syncing_) return;
  syncing_ = true;

  const std::vector<MdBlock> blocks = ParseBlocks(text_.toStdString());

  // 块结构指纹：类型 + 语言（内容变化不算，内容由各块自己增量刷新）
  int sig = 1;
  for (const auto& b : blocks) {
    sig = sig * 131 + (int)b.kind * 7919 + (int)b.lang.size();
    for (unsigned char c : b.lang) sig = sig * 31 + c;
  }

  const bool sameShape = (!force && sig == blockSignature_ && blocks.size() == blocks_.size());
  if (!sameShape) {
    for (auto* w : blocks_) {
      contentLay_->removeWidget(w);
      w->deleteLater();
    }
    blocks_.clear();
    for (const auto& b : blocks) {
      MdBlockWidget* w = nullptr;
      if (b.kind == MdBlock::Code)
        w = new CodeBlock(content_);
      else if (b.kind == MdBlock::Table)
        w = new TableBlock(content_);
      else
        w = new TextBlock(content_);
      contentLay_->addWidget(w);
      blocks_.push_back(w);
    }
    blockSignature_ = sig;
  }

  const size_t n = std::min(blocks.size(), blocks_.size());
  for (size_t i = 0; i < n; ++i) blocks_[i]->SetContent(blocks[i]);

  if (blocks.empty() && streaming_) meta_->setVisible(true);

  // 块增删或高度变化后立刻让外层重新收敛，否则气泡会留上一轮的高度（表现为底部空白）
  contentLay_->activate();
  if (layout()) layout()->activate();
  updateGeometry();
  QTimer::singleShot(0, this, [this]() {
    contentLay_->activate();
    if (layout()) layout()->activate();
    updateGeometry();
  });

  syncing_ = false;

  // 正文从空到有（流式首包、打字机推进）会改变气泡该占多宽，这里补一次宽度收敛
  UpdateBubbleWidth();
}

void MessageBubble::SetText(const QString& text) {
  if (text == text_) return;
  text_ = text;
  msg_.text = text.toStdString();
  SyncBlocks(false);
}

void MessageBubble::Finalize() {
  streaming_ = false;
  meta_->setVisible(false);
  SyncBlocks(true);  // 收尾重刷一次，保证 markdown / 高亮按最终内容渲染
}

}  // namespace wd
