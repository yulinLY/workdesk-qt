#include "chatpanel.h"

#include <QBuffer>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <filesystem>

#include "http.h"
#include "markdown.h"
#include "messagebubble.h"
#include "smoothscroll.h"

namespace wd {
namespace {

QString FromUtf8(const std::string& s) { return QString::fromStdString(s); }

bool IsImagePath(const QString& p) {
  const QString e = QFileInfo(p).suffix().toLower();
  return e == "png" || e == "jpg" || e == "jpeg" || e == "webp" || e == "bmp" || e == "gif";
}

// 输入框：Enter 发送、Shift+Enter 换行；
// Ctrl+V / 右键粘贴时，剪贴板里是文件就挂成附件、是截图就挂成图片（不需要 moc，用回调）
class InputEdit : public QPlainTextEdit {
 public:
  std::function<void()> onSubmit;
  std::function<void(const QStringList&)> onPastePaths;
  std::function<void(const QImage&)> onPasteImage;
  explicit InputEdit(QWidget* p) : QPlainTextEdit(p) {
    // 占位提示用 QLabel 叠在文本区上：原生 placeholder 不认输入法 preedit，
    // 中文组字时拼音/候选会和提示文字叠在同一行。
    setPlaceholderText(QString());
    hint_ = new QLabel(viewport());
    hint_->setObjectName(QStringLiteral("InputHint"));
    hint_->setAttribute(Qt::WA_TransparentForMouseEvents);
    hint_->hide();
    connect(this, &QPlainTextEdit::textChanged, this, [this]() { RefreshHint(); });
  }

  void SetHint(const QString& h) {
    hintText_ = h;
    RefreshHint();
  }

 protected:
  void resizeEvent(QResizeEvent* e) override {
    QPlainTextEdit::resizeEvent(e);
    RefreshHint();
  }

  // 组字进行中不显示提示，否则会和拼音/候选重叠
  bool event(QEvent* e) override {
    if (e->type() == QEvent::InputMethod) {
      auto* ime = static_cast<QInputMethodEvent*>(e);
      const bool composing = !ime->preeditString().isEmpty();
      if (composing != composing_) {
        composing_ = composing;
        RefreshHint();
      }
    }
    return QPlainTextEdit::event(e);
  }

  void RefreshHint() {
    if (!hint_) return;
    const bool want = !hintText_.isEmpty() && !composing_ && toPlainText().isEmpty();
    if (!want) {
      hint_->hide();
      return;
    }
    QPalette pal = hint_->palette();
    pal.setColor(QPalette::WindowText, palette().color(QPalette::PlaceholderText));
    hint_->setPalette(pal);
    hint_->setFont(font());
    const QRect cr = cursorRect();
    hint_->setGeometry(cr.left() + 2, cr.top(), qMax(0, viewport()->width() - cr.left() - 8),
                       fontMetrics().height());
    hint_->setText(hintText_);
    hint_->show();
    hint_->raise();
  }


  void keyPressEvent(QKeyEvent* e) override {
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
        !(e->modifiers() & Qt::ShiftModifier) && !(e->modifiers() & Qt::ControlModifier)) {
      if (onSubmit) onSubmit();
      return;
    }
    QPlainTextEdit::keyPressEvent(e);
  }

  void insertFromMimeData(const QMimeData* src) override {
    if (src) {
      QStringList paths;
      if (src->hasUrls()) {
        for (const QUrl& u : src->urls())
          if (u.isLocalFile()) paths << u.toLocalFile();
      }
      if (!paths.isEmpty() && onPastePaths) {
        onPastePaths(paths);
        return;  // 不再把文件名当文本插进输入框
      }
      if (src->hasImage() && onPasteImage) {
        const QImage img = qvariant_cast<QImage>(src->imageData());
        if (!img.isNull()) {
          onPasteImage(img);
          return;
        }
      }
    }
    QPlainTextEdit::insertFromMimeData(src);
  }

 private:
  QLabel* hint_ = nullptr;
  QString hintText_;
  bool composing_ = false;
};

// 把图片压到最长边 1280 再 base64，避免请求体过大导致首字变慢
bool LoadImageAsChatImage(const QString& path, ChatImage& out) {
  QImage img(path);
  if (img.isNull()) return false;
  if (std::max(img.width(), img.height()) > 1280) {
    img = img.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  if (!img.save(&buf, "JPEG", 88)) img.save(&buf, "PNG");
  buf.close();
  out.name = QFileInfo(path).fileName().toStdString();
  out.b64 = bytes.toBase64().toStdString();
  out.path = path.toStdWString();
  out.w = img.width();
  out.h = img.height();
  return true;
}

}  // namespace

ChatPanel::ChatPanel(Config* cfg, QWidget* parent) : QWidget(parent), cfg_(cfg) {
  stream_ = new ChatStream();
  pollTimer_ = new QTimer(this);
  pollTimer_->setInterval(16);
  connect(pollTimer_, &QTimer::timeout, this, &ChatPanel::PollStream);

  scrollTimer_ = new QTimer(this);
  scrollTimer_->setInterval(16);
  connect(scrollTimer_, &QTimer::timeout, this, &ChatPanel::StepScroll);

  // 拖放统一由主窗口分流（文本进记事本、图片/二进制进聊天），这里不再抢事件
  setAcceptDrops(false);
  BuildUi();
  RefreshSessions();
  NewSession();
}

ChatPanel::~ChatPanel() {
  if (stream_) {
    stream_->Shutdown();
    delete stream_;
    stream_ = nullptr;
  }
}

bool ChatPanel::Busy() const { return streamingBubble_ != nullptr; }

// ------------------------------------------------------------------ UI
void ChatPanel::BuildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(8);

  // 顶部工具栏
  auto* bar = new QWidget(this);
  auto* barLay = new QHBoxLayout(bar);
  barLay->setContentsMargins(0, 0, 0, 0);
  barLay->setSpacing(6);

  sessionBox_ = new QComboBox(bar);
  sessionBox_->setMinimumWidth(220);
  sessionBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  connect(sessionBox_, QOverload<int>::of(&QComboBox::activated), this, &ChatPanel::LoadSessionAt);
  barLay->addWidget(sessionBox_, 1);

  auto* newBtn = new QPushButton(QStringLiteral("新对话"), bar);
  connect(newBtn, &QPushButton::clicked, this, &ChatPanel::NewSession);
  barLay->addWidget(newBtn);

  auto* impBtn = new QPushButton(QStringLiteral("导入"), bar);
  connect(impBtn, &QPushButton::clicked, this, &ChatPanel::ImportSession);
  barLay->addWidget(impBtn);

  auto* expBtn = new QPushButton(QStringLiteral("导出"), bar);
  connect(expBtn, &QPushButton::clicked, this, &ChatPanel::ExportCurrent);
  barLay->addWidget(expBtn);

  auto* delBtn = new QPushButton(QStringLiteral("删除"), bar);
  connect(delBtn, &QPushButton::clicked, this, &ChatPanel::DeleteCurrentSession);
  barLay->addWidget(delBtn);

  root->addWidget(bar);

  // 消息列表
  scroll_ = new QScrollArea(this);
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  listHost_ = new QWidget(scroll_);
  listLay_ = new QVBoxLayout(listHost_);
  listLay_->setContentsMargins(4, 6, 8, 6);
  listLay_->setSpacing(10);
  listLay_->addStretch(1);
  scroll_->setWidget(listHost_);
  root->addWidget(scroll_, 1);

  // 只有用户自己动手（滚轮 / 拖滑块）才算脱离跟随；
  // 内容长高导致距底变大属于正常生长，不能再误判成"用户上翻"而停掉跟随。
  {
    QScrollBar* vsb = scroll_->verticalScrollBar();
    connect(vsb, &QScrollBar::sliderPressed, this, [this]() { stickBottom_ = false; });
    connect(vsb, &QScrollBar::sliderReleased, this, [this]() {
      QScrollBar* s = scroll_->verticalScrollBar();
      if (s->maximum() - s->value() < 80) {
        stickBottom_ = true;
        ScrollToBottom(true);
      }
    });
  }
  // 滚轮已被全局平滑滚动接管，viewport 收不到 Wheel，方向由它的信号转达
  if (auto* ss = wd::SmoothScroll::Install()) {
    connect(ss, &wd::SmoothScroll::wheeled, this,
            [this](QAbstractScrollArea* area, int notches) {
              if (!scroll_ || area != scroll_) return;
              QScrollBar* sb = scroll_->verticalScrollBar();
              if (notches > 0) {
                stickBottom_ = false;  // 用户往上翻：立刻交出滚动条
              } else if (notches < 0 && sb->maximum() - sb->value() < 80) {
                stickBottom_ = true;  // 自己滚回底部：恢复跟随
                lastScrollMax_ = sb->maximum();
                if (!scrollTimer_->isActive()) scrollTimer_->start();
              }
            });
  }

  // 待发送附件 chips
  chips_ = new QWidget(this);
  chipsLay_ = new QHBoxLayout(chips_);
  chipsLay_->setContentsMargins(0, 0, 0, 0);
  chipsLay_->setSpacing(6);
  chips_->setVisible(false);
  root->addWidget(chips_);

  // 输入区
  auto* box = new QWidget(this);
  auto* boxLay = new QVBoxLayout(box);
  boxLay->setContentsMargins(0, 0, 0, 0);
  boxLay->setSpacing(6);

  auto* in = new InputEdit(box);
  input_ = in;
  in->SetHint(QStringLiteral("输入消息"));
  input_->installEventFilter(this);
  input_->viewport()->installEventFilter(this);
  input_->setMinimumHeight(72);
  input_->setMaximumHeight(190);
  input_->setAcceptDrops(false);
  in->onSubmit = [this]() { Send(); };
  in->onPastePaths = [this](const QStringList& paths) {
    AttachPaths(paths);
    input_->setFocus();
  };
  in->onPasteImage = [this](const QImage& src) {
    QImage img = src;
    if (img.isNull()) return;
    if (std::max(img.width(), img.height()) > 1280)
      img = img.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "JPEG", 88)) img.save(&buf, "PNG");
    buf.close();
    // 顺手落一份临时文件，消息里的缩略图点开才能看原图
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/WorkDeskPaste");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/paste-") +
                         QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")) +
                         QStringLiteral(".jpg");
    QFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
      out.write(bytes);
      out.close();
    }
    ChatImage ci;
    ci.name = QFileInfo(path).fileName().toStdString();
    ci.b64 = bytes.toBase64().toStdString();
    if (QFileInfo::exists(path)) ci.path = path.toStdWString();
    ci.w = img.width();
    ci.h = img.height();
    pendingImgs_.push_back(ci);
    UpdatePendingChips();
    if (status_) status_->setText(QStringLiteral("已从剪贴板添加 1 张图片"));
  };
  boxLay->addWidget(input_);

  auto* tool = new QWidget(box);
  auto* toolLay = new QHBoxLayout(tool);
  toolLay->setContentsMargins(0, 0, 0, 0);
  toolLay->setSpacing(6);

  auto* imgBtn = new QPushButton(QStringLiteral("图片"), tool);
  connect(imgBtn, &QPushButton::clicked, this, [this]() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("选择图片"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"));
    if (!files.isEmpty()) AttachPaths(files);
  });
  toolLay->addWidget(imgBtn);

  auto* fileBtn = new QPushButton(QStringLiteral("文件"), tool);
  connect(fileBtn, &QPushButton::clicked, this, [this]() {
    const QStringList files =
        QFileDialog::getOpenFileNames(this, QStringLiteral("选择文件"), QString());
    if (!files.isEmpty()) AttachPaths(files);
  });
  toolLay->addWidget(fileBtn);

  auto* regenBtn = new QPushButton(QStringLiteral("重新生成"), tool);
  connect(regenBtn, &QPushButton::clicked, this, &ChatPanel::Regenerate);
  toolLay->addWidget(regenBtn);

  status_ = new QLabel(tool);
  status_->setObjectName("ModuleHint");
  toolLay->addWidget(status_);
  toolLay->addStretch(1);

  profileBox_ = new QComboBox(tool);
  profileBox_->setMinimumWidth(120);
  profileBox_->setMaximumWidth(240);
  profileBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  profileBox_->setToolTip(QStringLiteral("切换 AI 配置（接口 + 性格）"));
  connect(profileBox_, QOverload<int>::of(&QComboBox::activated), this,
          &ChatPanel::OnProfilePicked);
  toolLay->addWidget(profileBox_);

  modelLab_ = new QLabel(tool);
  modelLab_->setObjectName("ModuleHint");
  toolLay->addWidget(modelLab_);

  stopBtn_ = new QPushButton(QStringLiteral("停止"), tool);
  stopBtn_->setVisible(false);
  connect(stopBtn_, &QPushButton::clicked, this, &ChatPanel::StopStreaming);
  toolLay->addWidget(stopBtn_);

  sendBtn_ = new QPushButton(QStringLiteral("发送"), tool);
  sendBtn_->setObjectName("Primary");
  sendBtn_->setMinimumWidth(78);
  connect(sendBtn_, &QPushButton::clicked, this, &ChatPanel::Send);
  toolLay->addWidget(sendBtn_);

  boxLay->addWidget(tool);
  root->addWidget(box);

  if (cfg_) {
    RefreshModelInfo();
    input_->setEnabled(true);
  }
}

void ChatPanel::RefreshProfileCombo() {
  if (!cfg_ || !profileBox_) return;
  cfg_->EnsureProfiles();
  const bool blocked = profileBox_->blockSignals(true);
  profileBox_->clear();
  for (const auto& p : cfg_->profiles) {
    QString n = QString::fromStdString(p.name).trimmed();
    if (n.isEmpty()) n = QString::fromStdString(p.model).trimmed();
    profileBox_->addItem(n.isEmpty() ? QStringLiteral("未命名") : n);
  }
  if (cfg_->activeProfile >= 0 && cfg_->activeProfile < (int)cfg_->profiles.size())
    profileBox_->setCurrentIndex(cfg_->activeProfile);
  profileBox_->blockSignals(blocked);
}

void ChatPanel::RefreshModelInfo() {
  if (!cfg_) return;
  RefreshProfileCombo();
  if (!modelLab_) return;
  // 配置名与模型名一致时不重复显示，省出工具条宽度
  QString name;
  if (cfg_->activeProfile >= 0 && cfg_->activeProfile < (int)cfg_->profiles.size())
    name = QString::fromStdString(cfg_->profiles[(size_t)cfg_->activeProfile].name).trimmed();
  const QString model = FromUtf8(cfg_->model);
  const bool same = !name.isEmpty() && name.compare(model, Qt::CaseInsensitive) == 0;
  modelLab_->setText(same ? QString() : model);
  modelLab_->setVisible(!same);
  if (profileBox_) {
    if (same)
      profileBox_->setToolTip(QStringLiteral("切换 AI 配置（接口 + 性格）\n当前：%1").arg(model));
    else
      profileBox_->setToolTip(
          QStringLiteral("切换 AI 配置（接口 + 性格）\n当前：%1 · %2").arg(name, model));
  }
}

void ChatPanel::OnProfilePicked(int idx) {
  if (!cfg_ || idx < 0 || idx >= (int)cfg_->profiles.size()) return;
  cfg_->activeProfile = idx;
  cfg_->SyncTopFromActive();
  cfg_->Save();
  RefreshModelInfo();
}

void ChatPanel::FocusInput() { input_->setFocus(); }

void ChatPanel::SetBusyUi(bool busy) {
  sendBtn_->setEnabled(!busy);
  stopBtn_->setVisible(busy);
  input_->setEnabled(true);
  if (profileBox_) profileBox_->setEnabled(!busy);
  if (busy) {
    sendBtn_->setText(QStringLiteral("发送中"));
  } else {
    sendBtn_->setText(QStringLiteral("发送"));
    streamingBubble_ = nullptr;
    streamingIndex_ = -1;
    typeTarget_.clear();
    typeShown_ = 0;
    RefreshModelInfo();
  }
}

// ------------------------------------------------------------------ 列表
void ChatPanel::AddBubble(const ChatMessage& msg, bool saveIt) {
  Q_UNUSED(saveIt);
  auto* b = new MessageBubble(msg, listHost_);
  connect(b, &MessageBubble::ImageClicked, this, [this](const QString& path) {
    if (!path.isEmpty()) emit Notice(path);
  });
  // 附件 / 图片点击后交给系统默认程序打开
  connect(b, &MessageBubble::FileClicked, this,
          [this, msg](const QString& path, int imageIndex) {
            QString b64;
            QString name;
            if (imageIndex >= 0 && imageIndex < static_cast<int>(msg.images.size())) {
              b64 = QString::fromStdString(msg.images[imageIndex].b64);
              name = QString::fromStdString(msg.images[imageIndex].name);
            }
            OpenAttachment(path, b64, name);
          });
  listLay_->insertWidget(listLay_->count() - 1, b);
  if (!msg.user && msg.text.empty()) {
    streamingBubble_ = b;
  }
}

// 打开附件：先按原路径找文件，找不到再看内存里有没有图片数据；
// 两者都没有就明确告知用户，不再像以前那样点了没反应。
void ChatPanel::OpenAttachment(const QString& path, const QString& b64, const QString& name) {
  QString target = path;
  if (target.isEmpty() || !QFileInfo::exists(target)) {
    if (!b64.isEmpty()) {
      const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                          QStringLiteral("/WorkDeskAttach");
      QDir().mkpath(dir);
      QString suffix = QFileInfo(name).suffix();
      if (suffix.isEmpty()) suffix = QStringLiteral("png");
      target = dir + QStringLiteral("/") + QUuid::createUuid().toString(QUuid::Id128) +
               QStringLiteral(".") + suffix;
      QFile f(target);
      if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArray::fromBase64(b64.toLatin1()));
        f.close();
      } else {
        target.clear();
      }
    } else {
      target.clear();
    }
  }
  if (target.isEmpty()) {
    QMessageBox::information(
        this, QStringLiteral("附件"),
        QStringLiteral("文件已不存在，可能被移动或删除：\n%1").arg(path));
    return;
  }
  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(target))) {
    QMessageBox::warning(this, QStringLiteral("附件"),
                         QStringLiteral("系统没有可用程序打开这个文件：\n%1").arg(target));
  }
}

void ChatPanel::ScrollToBottom(bool force) {
  if (!scroll_ || !scrollTimer_) return;
  QScrollBar* sb = scroll_->verticalScrollBar();
  const int max = sb->maximum();
  if (force) {
    stickBottom_ = true;
    // 强制场景（发送/切换会话/收尾）直接落地，不留追赶动画
    sb->setValue(max);
    lastScrollMax_ = max;
    return;
  }
  if (!stickBottom_) return;
  // 内容高度没变、滚动条却被拉远 → 才是用户自己往上翻。
  // 用 == 而不是 <=：块重建瞬间内容高度会掉到很低，这时距底必然很大，
  // 按 <= 判会被误当成"用户在翻"，跟随就此断掉，之后内容一直在下方长、视口不动。
  if (max == lastScrollMax_ && max - sb->value() > 220) {
    stickBottom_ = false;
    lastScrollMax_ = max;
    return;
  }
  lastScrollMax_ = max;
  // 落得太多（一次长高一大截 / 布局瞬时收敛过）直接补到底，避免"从顶上慢慢滑下来"
  if (max - sb->value() > 600) {
    sb->setValue(max);
    return;
  }
  // 其余交给 16ms 的缓动步进，内容连续长高时就是平滑跟随
  if (!scrollTimer_->isActive()) scrollTimer_->start();
}

void ChatPanel::StepScroll() {
  if (!scroll_) {
    scrollTimer_->stop();
    return;
  }
  if (!stickBottom_) {
    scrollTimer_->stop();
    return;
  }
  QScrollBar* sb = scroll_->verticalScrollBar();
  const int max = sb->maximum();
  const int dist = max - sb->value();
  if (dist <= 0) {
    scrollTimer_->stop();
    return;
  }
  // 指数逼近：越接近底部步长越小。分母取 2 保证落后时两三帧内贴回去，
  // 不会出现"内容一直在长、滚动条一直追不上"的假死感。
  int step = dist / 2;
  if (step < 3) step = 3;
  if (step > dist) step = dist;
  sb->setValue(sb->value() + step);
  lastScrollMax_ = max;
}

bool ChatPanel::eventFilter(QObject* obj, QEvent* ev) {
  return QWidget::eventFilter(obj, ev);
}

void ChatPanel::ClearList() {
  while (listLay_->count() > 1) {
    QLayoutItem* it = listLay_->takeAt(0);
    if (!it) break;
    if (QWidget* w = it->widget()) w->deleteLater();
    delete it;
  }
  streamingBubble_ = nullptr;
  streamingIndex_ = -1;
  typeTarget_.clear();
  typeShown_ = 0;
}

// ------------------------------------------------------------------ 会话
void ChatPanel::NewSession() {
  if (Busy()) StopStreaming();
  if (dirty_ && hasSession_) SaveCurrent(true);
  msgs_.clear();
  pendingImgs_.clear();
  pendingFiles_.clear();
  current_ = SessionInfo();
  current_.id = NewSessionId();
  current_.title = "新对话";
  current_.path = ChatDir() + L"\\" + std::wstring(current_.id.begin(), current_.id.end()) + L".json";
  hasSession_ = false;
  dirty_ = false;
  ClearList();
  UpdatePendingChips();
  if (sessionBox_->count() > 0) {
    QSignalBlocker block(sessionBox_);
    sessionBox_->setCurrentIndex(0);
  }
  if (status_) status_->setText(QStringLiteral("新对话已就绪"));
  input_->setFocus();
}

void ChatPanel::RefreshSessions(int keepIndex) {
  sessions_ = ScanSessions();
  QSignalBlocker block(sessionBox_);
  sessionBox_->clear();
  sessionBox_->addItem(QStringLiteral("＋ 新对话"));
  for (const auto& s : sessions_) {
    QString title = FromUtf8(s.title.empty() ? std::string("未命名") : s.title);
    if (title.size() > 26) title = title.left(26) + QStringLiteral("…");
    sessionBox_->addItem(QStringLiteral("%1  (%2)").arg(title).arg(s.msgCount));
  }
  if (keepIndex >= 0 && keepIndex < sessionBox_->count()) sessionBox_->setCurrentIndex(keepIndex);
}

void ChatPanel::LoadSessionAt(int idx) {
  if (Busy()) StopStreaming();
  if (dirty_ && hasSession_) SaveCurrent(true);
  if (idx <= 0) {
    NewSession();
    return;
  }
  const int si = idx - 1;
  if (si < 0 || si >= (int)sessions_.size()) return;
  std::vector<ChatMessage> loaded;
  if (!LoadSession(sessions_[si], loaded)) {
    QMessageBox::warning(this, QStringLiteral("读取失败"), QStringLiteral("会话文件无法读取。"));
    return;
  }
  msgs_ = std::move(loaded);
  current_ = sessions_[si];
  hasSession_ = true;
  dirty_ = false;
  ClearList();
  for (const auto& m : msgs_) AddBubble(m, false);
  ScrollToBottom(true);
  if (status_) status_->setText(QStringLiteral("已载入 %1 条消息").arg((int)msgs_.size()));
}

void ChatPanel::SaveCurrent(bool force) {
  if (!hasSession_ && msgs_.empty()) return;
  if (!force && !dirty_) return;
  if (!hasSession_) {
    current_.id = NewSessionId();
    current_.path = ChatDir() + L"\\" + std::wstring(current_.id.begin(), current_.id.end()) + L".json";
    hasSession_ = true;
  }
  current_.title = MakeSessionTitle(msgs_);
  if (SaveSession(current_, msgs_)) {
    dirty_ = false;
    RefreshSessions(sessionBox_->currentIndex());
  }
}

void ChatPanel::DeleteCurrentSession() {
  if (!hasSession_) {
    QMessageBox::information(this, QStringLiteral("删除"), QStringLiteral("当前还没有已保存的会话。"));
    return;
  }
  const auto ret = QMessageBox::question(
      this, QStringLiteral("删除会话"),
      QStringLiteral("确定删除当前会话「%1」？该操作会移除磁盘上的会话文件。")
          .arg(FromUtf8(current_.title)));
  if (ret != QMessageBox::Yes) return;
  DeleteSessionFile(current_);
  hasSession_ = false;
  dirty_ = false;
  msgs_.clear();
  ClearList();
  RefreshSessions();
  NewSession();
}

void ChatPanel::ExportCurrent() {
  if (msgs_.empty()) {
    QMessageBox::information(this, QStringLiteral("导出"), QStringLiteral("当前会话为空。"));
    return;
  }
  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("导出会话"),
      QDir::homePath() + QStringLiteral("/WorkDesk-chat.json"),
      QStringLiteral("JSON (*.json)"));
  if (path.isEmpty()) return;
  if (ExportSessionJson(path.toStdWString(), msgs_, current_.title)) {
    emit Notice(QStringLiteral("已导出：") + path);
    if (status_) status_->setText(QStringLiteral("已导出会话"));
  } else {
    QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("写入文件失败。"));
  }
}

void ChatPanel::ImportSession() {
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("导入会话"), QString(), QStringLiteral("JSON (*.json)"));
  if (path.isEmpty()) return;
  SessionInfo info;
  std::vector<ChatMessage> loaded;
  if (!ImportSessionJson(path.toStdWString(), info, loaded)) {
    QMessageBox::warning(this, QStringLiteral("导入失败"),
                         QStringLiteral("文件格式不是 WorkDesk 会话 JSON。"));
    return;
  }
  if (Busy()) StopStreaming();
  if (dirty_ && hasSession_) SaveCurrent(true);
  msgs_ = std::move(loaded);
  current_ = info;
  current_.id = NewSessionId();
  current_.path = ChatDir() + L"\\" + std::wstring(current_.id.begin(), current_.id.end()) + L".json";
  hasSession_ = false;
  dirty_ = true;
  ClearList();
  for (const auto& m : msgs_) AddBubble(m, false);
  SaveCurrent(true);
  RefreshSessions();
  ScrollToBottom(true);
  if (status_) status_->setText(QStringLiteral("已导入 %1 条消息").arg((int)msgs_.size()));
}

// ------------------------------------------------------------------ 附件
void ChatPanel::AttachPaths(const QStringList& paths) {
  int imgs = 0, files = 0;
  for (const QString& p : paths) {
    QFileInfo fi(p);
    if (!fi.exists() || !fi.isFile()) continue;
    if (IsImagePath(p)) {
      ChatImage ci;
      if (LoadImageAsChatImage(p, ci)) {
        pendingImgs_.push_back(ci);
        ++imgs;
        continue;
      }
    }
    ChatFile cf;
    cf.name = fi.fileName().toStdString();
    cf.path = p.toStdWString();
    cf.size = (unsigned long long)fi.size();
    pendingFiles_.push_back(cf);
    ++files;
  }
  UpdatePendingChips();
  if ((imgs + files) > 0 && status_) {
    status_->setText(QStringLiteral("已添加 %1 张图片、%2 个文件").arg(imgs).arg(files));
  }
}

void ChatPanel::UpdatePendingChips() {
  while (QLayoutItem* it = chipsLay_->takeAt(0)) {
    if (QWidget* w = it->widget()) w->deleteLater();
    delete it;
  }
  auto addChip = [this](const QString& text, std::function<void()> onRemove) {
    auto* b = new QPushButton(text + QStringLiteral("  ×"), chips_);
    b->setObjectName("Pill");
    b->setCursor(Qt::PointingHandCursor);
    connect(b, &QPushButton::clicked, this, [onRemove]() { onRemove(); });
    chipsLay_->addWidget(b);
  };
  for (size_t i = 0; i < pendingImgs_.size(); ++i) {
    const QString name = FromUtf8(pendingImgs_[i].name);
    addChip(QStringLiteral("图片 ") + name.left(18), [this, i]() {
      pendingImgs_.erase(pendingImgs_.begin() + i);
      UpdatePendingChips();
    });
  }
  for (size_t i = 0; i < pendingFiles_.size(); ++i) {
    const QString name = FromUtf8(pendingFiles_[i].name);
    addChip(QStringLiteral("文件 ") + name.left(18), [this, i]() {
      pendingFiles_.erase(pendingFiles_.begin() + i);
      UpdatePendingChips();
    });
  }
  chipsLay_->addStretch(1);
  chips_->setVisible(!(pendingImgs_.empty() && pendingFiles_.empty()));
}

// ------------------------------------------------------------------ 拖放
void ChatPanel::dragEnterEvent(QDragEnterEvent* e) {
  if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void ChatPanel::dropEvent(QDropEvent* e) {
  QStringList paths;
  for (const QUrl& u : e->mimeData()->urls()) {
    if (u.isLocalFile()) paths << u.toLocalFile();
  }
  if (paths.isEmpty()) return;
  AttachPaths(paths);
  e->acceptProposedAction();
  input_->setFocus();
}

// ------------------------------------------------------------------ 发送
void ChatPanel::EnsureConfigured() {
  if (cfg_ && cfg_->ok()) return;
  QMessageBox::information(this, QStringLiteral("需要先配置"),
                           QStringLiteral("还没填 API Key / 模型，先打开设置配置一下。"));
  emit RequestSettings();
}

int ChatPanel::LastUserIndex() const {
  for (int i = (int)msgs_.size() - 1; i >= 0; --i)
    if (msgs_[i].user) return i;
  return -1;
}

void ChatPanel::Send() {
  if (Busy()) return;
  const QString text = input_->toPlainText().trimmed();
  if (text.isEmpty() && pendingImgs_.empty() && pendingFiles_.empty()) return;
  EnsureConfigured();
  if (!cfg_ || !cfg_->ok()) return;

  ChatMessage u;
  u.user = true;
  u.text = text.toStdString();
  u.images = pendingImgs_;
  u.files = pendingFiles_;
  msgs_.push_back(u);
  AddBubble(u, true);
  pendingImgs_.clear();
  pendingFiles_.clear();
  UpdatePendingChips();
  input_->clear();

  ChatMessage a;
  a.user = false;
  msgs_.push_back(a);
  AddBubble(a, false);
  streamingIndex_ = (int)msgs_.size() - 1;
  ScrollToBottom(true);

  const int userIdx = (int)msgs_.size() - 2;
  const std::string body = BuildRequestBody(*cfg_, msgs_, userIdx);
  streamEnded_ = false;
  typeTarget_.clear();
  typeShown_ = 0;
  streamError_.clear();
  if (stream_) stream_->Start(cfg_->Endpoint(), cfg_->apiKey, body);
  pollTimer_->start(16);
  SetBusyUi(true);
  dirty_ = true;
  if (status_) status_->setText(QStringLiteral("正在请求模型…"));
}

void ChatPanel::Regenerate() {
  if (Busy()) return;
  const int uIdx = LastUserIndex();
  if (uIdx < 0) {
    emit Notice(QStringLiteral("没有可重新生成的内容。"));
    return;
  }
  EnsureConfigured();
  if (!cfg_ || !cfg_->ok()) return;

  msgs_.resize(uIdx + 1);
  ClearList();
  for (const auto& m : msgs_) AddBubble(m, false);

  ChatMessage a;
  a.user = false;
  msgs_.push_back(a);
  AddBubble(a, false);
  streamingIndex_ = (int)msgs_.size() - 1;
  ScrollToBottom(true);

  const std::string body = BuildRequestBody(*cfg_, msgs_, uIdx);
  streamEnded_ = false;
  typeTarget_.clear();
  typeShown_ = 0;
  streamError_.clear();
  if (stream_) stream_->Start(cfg_->Endpoint(), cfg_->apiKey, body);
  pollTimer_->start(16);
  SetBusyUi(true);
  dirty_ = true;
  if (status_) status_->setText(QStringLiteral("正在重新生成…"));
}

void ChatPanel::StopStreaming() {
  if (stream_) stream_->Stop();
  if (pollTimer_) pollTimer_->stop();
  if (streamingBubble_ && streamingIndex_ >= 0 && streamingIndex_ < (int)msgs_.size()) {
    if (msgs_[streamingIndex_].text.empty())
      msgs_[streamingIndex_].text = "（已停止）";
    typeTarget_ = FromUtf8(msgs_[streamingIndex_].text);
    PumpTypewriter(true);
    streamingBubble_->Finalize();
  }
  SetBusyUi(false);
  dirty_ = true;
  SaveCurrent(true);
  if (status_) status_->setText(QStringLiteral("已停止"));
}

// 打字机：把已经读到的文本按帧逐步吐到气泡上。
// 落后越多追得越快（lag/4，单帧最多 24 字符）：既不会整块闪现，
// 也不会在网络很快时拖出一条长尾巴。flush 用于停止/收尾，一次性补完。
void ChatPanel::PumpTypewriter(bool flush) {
  if (!streamingBubble_ || streamingIndex_ < 0) return;
  const int total = typeTarget_.size();
  if (typeShown_ >= total) return;

  int step;
  if (flush) {
    step = total - typeShown_;
  } else {
    const int lag = total - typeShown_;
    step = qMax(1, lag / 4);
    if (step > 24) step = 24;
  }
  int end = typeShown_ + step;
  if (end > total) end = total;
  // 别把代理对（emoji）劈成两半，否则尾部会闪半个字符
  if (end < total && typeTarget_.at(end - 1).isHighSurrogate()) ++end;
  typeShown_ = end;
  streamingBubble_->SetText(typeTarget_.left(typeShown_));
  ScrollToBottom(false);
}

void ChatPanel::PollStream() {
  if (!stream_) return;
  std::string delta, err;
  bool finished = false;
  const bool has = stream_->Consume(delta, err, finished);
  if (has) {
    if (!err.empty()) streamError_ = err;
    if (!delta.empty() && streamingIndex_ >= 0 && streamingIndex_ < (int)msgs_.size()) {
      // 增量只落到消息体，显示交给打字机逐帧推进：网络一次吐一大段也不会整块闪现
      msgs_[streamingIndex_].text += delta;
      typeTarget_ = FromUtf8(msgs_[streamingIndex_].text);
      ScrollToBottom(false);
    }
  }
  // 每帧推进打字机：网络空转的帧也继续吐字，尾巴不会停在半截
  PumpTypewriter(false);
  if (finished && !streamEnded_) {
    streamEnded_ = true;
    pollTimer_->stop();
    if (streamingIndex_ >= 0 && streamingIndex_ < (int)msgs_.size()) {
      ChatMessage& m = msgs_[streamingIndex_];
      if (m.text.empty()) {
        m.text = streamError_.empty() ? "（无内容返回）" : ("请求失败：" + streamError_);
      } else if (!streamError_.empty()) {
        m.text += "\n\n> " + streamError_;
      }
      if (streamingBubble_) {
        typeTarget_ = FromUtf8(m.text);
        PumpTypewriter(true);  // 没吐完的尾巴一次性补齐，避免收尾后缺字
        streamingBubble_->Finalize();
      }
    }
    SetBusyUi(false);
    dirty_ = true;
    SaveCurrent(true);
    ScrollToBottom(true);
    if (status_) {
      status_->setText(streamError_.empty() ? QStringLiteral("完成")
                                            : QStringLiteral("结束，有错误提示"));
    }
  }
}

}  // namespace wd
