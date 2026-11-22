#include "markdowntoolbar.h"
#include "theme.h"


#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace wd {
namespace {

// ---------------- 命令表：Markdown 全部常用格式 ----------------
// kind: 0=行级前缀（标题/列表/引用）  1=行内包裹（粗体/链接等）  2=块级多行（代码块/表格/分割线）
struct MdCmd {
  const char* label;
  const char* prefix;   // 行级前缀 / 包裹左标记 / 块级模板
  const char* suffix;   // 包裹右标记（行级、块级为空）
  const char* holder;   // 无选中时的占位文字；块级为插入后要选中的片段
  int kind;
};

const MdCmd kCmds[] = {
    {"标题 1", "# ", "", "", 0},
    {"标题 2", "## ", "", "", 0},
    {"标题 3", "### ", "", "", 0},
    {"粗体", "**", "**", "粗体文字", 1},
    {"斜体", "*", "*", "斜体文字", 1},
    {"删除线", "~~", "~~", "删除文字", 1},
    {"行内代码", "`", "`", "code", 1},
    {"引用", "> ", "", "", 0},
    {"无序列表", "- ", "", "", 0},
    {"有序列表", "1. ", "", "", 0},
    {"任务列表", "- [ ] ", "", "", 0},
    {"标题 4", "#### ", "", "", 0},
    {"标题 5", "##### ", "", "", 0},
    {"标题 6", "###### ", "", "", 0},
    {"链接", "[", "](https://)", "链接文字", 1},
    {"图片", "![", "](图片路径)", "图片说明", 1},
    {"代码块", "```\n代码\n```", "", "代码", 2},
    {"表格", "| 列 1 | 列 2 |\n| --- | --- |\n| 内容 | 内容 |", "", "内容", 2},
    {"分割线", "---", "", "", 2},
};

constexpr int kCmdCount = int(sizeof(kCmds) / sizeof(kCmds[0]));

bool IsEditableTarget(QWidget* w) {
  if (!w) return false;
  bool ok = false;
  if (auto* p = qobject_cast<QPlainTextEdit*>(w)) {
    ok = !p->isReadOnly();
  } else if (auto* t = qobject_cast<QTextEdit*>(w)) {
    ok = !t->isReadOnly();
  } else if (auto* l = qobject_cast<QLineEdit*>(w)) {
    ok = !l->isReadOnly() && l->echoMode() == QLineEdit::Normal;
  }
  if (!ok) return false;
  // 设置等弹窗里不弹；需要单独屏蔽的控件加 setProperty("mdNoTools", true)
  for (QWidget* p = w; p; p = p->parentWidget()) {
    if (p->property("mdNoTools").toBool()) return false;
    if (qobject_cast<QDialog*>(p)) return false;
  }
  return true;
}

// ---------------- 文本控件通用：QPlainTextEdit / QTextEdit ----------------
template <typename T>
void ApplyToTextEdit(T* e, const MdCmd& c) {
  QTextCursor cur = e->textCursor();
  const QString prefix = QString::fromUtf8(c.prefix);
  const QString suffix = QString::fromUtf8(c.suffix);

  if (c.kind == 2) {
    // 块级格式：整段模板直接落到光标处，插完选中占位片段，直接改字即可
    const QString tpl = QString::fromUtf8(c.prefix);
    const QString hold = QString::fromUtf8(c.holder);
    QTextCursor cur2 = cur;
    cur2.beginEditBlock();
    if (cur2.hasSelection()) cur2.removeSelectedText();
    QString ins = tpl;
    if (cur2.positionInBlock() > 0) ins.prepend(QLatin1Char('\n'));
    const int start = cur2.position();
    cur2.insertText(ins);
    cur2.endEditBlock();
    const int h = hold.isEmpty() ? -1 : ins.indexOf(hold);
    if (h >= 0) {
      cur2.setPosition(start + h);
      cur2.setPosition(start + h + hold.size(), QTextCursor::KeepAnchor);
    } else {
      cur2.setPosition(start + ins.size());
    }
    e->setTextCursor(cur2);
    return;
  }

  if (c.kind == 0) {
    QTextCursor c2 = cur;
    c2.beginEditBlock();
    if (cur.hasSelection()) {
      QTextCursor head(e->document());
      head.setPosition(cur.selectionStart());
      QTextCursor tail(e->document());
      tail.setPosition(cur.selectionEnd());
      int from = head.blockNumber();
      int to = tail.blockNumber();
      if (to > from && tail.positionInBlock() == 0) --to;  // 选区正好卡在下一行行首
      // 逆序插入，避免前面的插入把后面块的位置顶偏
      for (int i = to; i >= from; --i) {
        const QTextBlock b = e->document()->findBlockByNumber(i);
        if (!b.isValid() || b.text().trimmed().isEmpty()) continue;
        QTextCursor ci(e->document());
        ci.setPosition(b.position());
        ci.insertText(prefix);
      }
    } else {
      QTextBlock b = cur.block();
      QTextCursor ci(e->document());
      ci.setPosition(b.position());
      ci.insertText(prefix);
    }
    c2.endEditBlock();
    return;
  }

  // 包裹类
  const QString holder = QString::fromUtf8(c.holder);
  if (cur.hasSelection()) {
    const QString sel = cur.selectedText();
    cur.beginEditBlock();
    cur.insertText(prefix + sel + suffix);
    cur.endEditBlock();
    e->setTextCursor(cur);
  } else {
    const int start = cur.position();
    cur.beginEditBlock();
    cur.insertText(prefix + holder + suffix);
    cur.endEditBlock();
    cur.setPosition(start + prefix.size());
    cur.setPosition(start + prefix.size() + holder.size(), QTextCursor::KeepAnchor);
    e->setTextCursor(cur);
  }
}

// ---------------- 单行输入框 ----------------
void ApplyToLineEdit(QLineEdit* e, const MdCmd& c) {
  const QString prefix = QString::fromUtf8(c.prefix);
  const QString suffix = QString::fromUtf8(c.suffix);
  if (c.kind == 0) {
    e->home(false);
    e->insert(prefix);
    return;
  }
  if (c.kind == 2) {
    // 单行框放不下多行模板，退化成纯文字占位
    const QString hold = QString::fromUtf8(c.holder);
    if (!hold.isEmpty()) {
      const int at = e->cursorPosition();
      e->insert(hold);
      e->setSelection(at, hold.size());
    }
    return;
  }
  const QString holder = QString::fromUtf8(c.holder);
  const QString sel = e->selectedText();
  const QString body = sel.isEmpty() ? holder : sel;
  e->insert(prefix + body + suffix);
  const int start = e->cursorPosition() - suffix.size() - body.size();
  e->setSelection(start, body.size());
}

void ApplyToTarget(QWidget* w, int idx) {
  if (!w || idx < 0 || idx >= kCmdCount) return;
  const MdCmd& c = kCmds[idx];
  if (auto* p = qobject_cast<QPlainTextEdit*>(w)) {
    ApplyToTextEdit(p, c);
  } else if (auto* t = qobject_cast<QTextEdit*>(w)) {
    ApplyToTextEdit(t, c);
  } else if (auto* l = qobject_cast<QLineEdit*>(w)) {
    ApplyToLineEdit(l, c);
  }
}

// ---------------- 弹出面板 ----------------
class MdPanel : public QWidget {
 public:
  MdPanel() {
    // Popup 自带 grab：弹着的时候键盘落在面板上，1~9 直接选，Esc / 点外面自动关
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    box_ = new QFrame(this);
    box_->setObjectName(QStringLiteral("mdbox"));
    outer->addWidget(box_);

    auto* lay = new QVBoxLayout(box_);
    lay->setContentsMargins(8, 7, 8, 8);
    lay->setSpacing(3);

    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    const int kCols = 4;
    for (int i = 0; i < kCmdCount; ++i) {
      // 按键只认 1~9，所以只有前九个带序号，其余纯点击
      const QString text = i < 9 ? QStringLiteral("%1   %2").arg(i + 1).arg(QString::fromUtf8(kCmds[i].label))
                                 : QString::fromUtf8(kCmds[i].label);
      auto* b = new QPushButton(text, box_);
      b->setCursor(Qt::PointingHandCursor);
      b->setFocusPolicy(Qt::NoFocus);
      b->setMinimumWidth(78);
      connect(b, &QPushButton::clicked, this, [this, i]() { Fire(i); });
      grid->addWidget(b, i / kCols, i % kCols);
    }
    lay->addLayout(grid);
  }

  // 面板是独立顶层窗口（Qt::Popup），吃不到主界面的应用 QSS，
  // 所以每次弹出都按当前主题状态现算一套配色，亮色模式下不会是黑块。
  void ApplyTheme() {
    const bool light = theme::IsLightActive();
    const QString bg = light ? QStringLiteral("#FFFFFF") : QStringLiteral("#1C1C20");
    const QString line = light ? QStringLiteral("#C9D3E0") : QStringLiteral("#3A414A");
    const QString fg = light ? QStringLiteral("#22303F") : QStringLiteral("#F0F0F2");
    const QString hov = light ? QStringLiteral("#E7EEF9") : QStringLiteral("#2E343C");
    box_->setStyleSheet(
        QStringLiteral("QFrame#mdbox{background-color:%1;border:1px solid %2;border-radius:10px;}"
                       "QPushButton{color:%3;background:transparent;border:none;border-radius:6px;"
                       "padding:5px 10px;text-align:left;font-size:12px;}"
                       "QPushButton:hover{background:%4;}")
            .arg(bg, line, fg, hov));
  }

  void ShowFor(QWidget* target) {
    target_ = target;
    ApplyTheme();
    adjustSize();
    QPoint pos = CursorPos(target);
    // 光标右下角起弹，出界就收回屏幕内
    QScreen* sc = QGuiApplication::screenAt(pos);
    if (!sc) sc = QGuiApplication::primaryScreen();
    if (sc) {
      const QRect avail = sc->availableGeometry();
      if (pos.x() + width() > avail.right()) pos.setX(avail.right() - width());
      if (pos.y() + height() > avail.bottom()) pos.setY(pos.y() - height() - 22);
      if (pos.x() < avail.left()) pos.setX(avail.left());
      if (pos.y() < avail.top()) pos.setY(avail.top());
    }
    move(pos);
    show();
    raise();
  }

  QWidget* target() const { return target_; }

 protected:
  void keyPressEvent(QKeyEvent* ev) override {
    const int k = ev->key();
    if (k == Qt::Key_Escape) {
      close();
      return;
    }
    if (k >= Qt::Key_1 && k <= Qt::Key_9) {
      Fire(k - Qt::Key_1);
      return;
    }
    QWidget::keyPressEvent(ev);
  }

  void hideEvent(QHideEvent* ev) override {
    QWidget::hideEvent(ev);
    close();  // 点外面 / 失焦即关
  }

 private:
  static QPoint CursorPos(QWidget* w) {
    if (auto* p = qobject_cast<QPlainTextEdit*>(w)) {
      return p->viewport()->mapToGlobal(p->cursorRect().bottomRight()) + QPoint(2, 3);
    }
    if (auto* t = qobject_cast<QTextEdit*>(w)) {
      return t->viewport()->mapToGlobal(t->cursorRect().bottomRight()) + QPoint(2, 3);
    }
    if (auto* l = qobject_cast<QLineEdit*>(w)) {
      // QLineEdit::cursorRect 是 protected，单行框统一贴在输入框右下沿弹
      return l->mapToGlobal(QPoint(l->width() / 3, l->height())) + QPoint(0, 3);
    }
    return QCursor::pos();
  }

  void Fire(int idx) {
    QWidget* w = target_.data();
    hide();
    if (!w) return;
    w->setFocus(Qt::ShortcutFocusReason);
    ApplyToTarget(w, idx);
  }

  QPointer<QWidget> target_;
  QFrame* box_ = nullptr;
};

// ---------------- 全局守护 ----------------
class Watcher : public QObject {
 public:
  using QObject::QObject;

  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (ev->type() != QEvent::KeyPress) return false;
    auto* ke = static_cast<QKeyEvent*>(ev);
    if (ke->key() != Qt::Key_Alt) return false;
    // 焦点在输入框里按下 Alt 且 Ctrl 处于按下状态：等价于 Ctrl+Alt。
    // 只校验 Ctrl，避免各平台 Alt 自身的 modifiers 报法差异导致不触发。
    if (!(ke->modifiers() & Qt::ControlModifier)) return false;
    if (panel_ && panel_->isVisible()) {
      panel_->close();
      panel_ = nullptr;
      return true;
    }
    QWidget* w = QApplication::focusWidget();
    if (!w) w = qobject_cast<QWidget*>(obj);
    if (!IsEditableTarget(w)) return false;
    if (!panel_) {
      panel_ = new MdPanel();
    }
    panel_->ShowFor(w);
    return true;  // 吞掉，别让 Ctrl+Alt 半步命令漏进输入框
  }

 private:
  QPointer<MdPanel> panel_;
};

}  // namespace

void InstallMarkdownTools() {
  static bool done = false;
  if (done) return;
  done = true;
  qApp->installEventFilter(new Watcher(qApp));
}

}  // namespace wd
