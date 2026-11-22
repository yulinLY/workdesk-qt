#include "welcomedialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>

#include "config.h"
#include "http.h"
#include "theme.h"

namespace wd {

namespace {

struct Preset {
  const char* name;
  const char* baseUrl;
  const char* model;
  const char* hint;
};

// 和设置页保持同一套服务商，只是多一句「去哪拿 Key」的提示
const Preset kPresets[] = {
    {"DeepSeek", "https://api.deepseek.com/v1", "deepseek-chat",
     "打开 platform.deepseek.com 注册，在「API Keys」里新建一个，形如 sk-xxxx"},
    {"智谱 GLM", "https://open.bigmodel.cn/api/paas/v4", "glm-4.6",
     "打开 open.bigmodel.cn 注册，在「API Keys」里新建"},
    {"OpenAI", "https://api.openai.com/v1", "gpt-4o-mini", "需要在 platform.openai.com 拿到 Key"},
    {"本地 Ollama（OpenAI 兼容）", "http://127.0.0.1:11434/v1", "qwen2.5:7b",
     "本机装好 Ollama 并 pull 了模型，Key 随便填一个即可"},
    {"自定义 / 其他兼容服务", "", "", "任何 OpenAI 兼容接口都行，填到 /v1 为止，程序自动补 /chat/completions"},
};
const int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

QLabel* MkText(const QString& s, const QString& obj, bool wrap = true) {
  auto* l = new QLabel(s);
  if (!obj.isEmpty()) l->setObjectName(obj);
  l->setWordWrap(wrap);
  return l;
}

// 顶部插图。注意：这张图只属于欢迎向导，生日彩蛋有它自己的蛋糕，互不影响。
// 这里画的是一个「接入」的隐喻：一个圆环口子 + 一颗滑进来的圆点，表示把模型接进来。
class WelcomeArt : public QWidget {
 public:
  explicit WelcomeArt(QWidget* parent = nullptr) : QWidget(parent) {
    setFixedHeight(104);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor accent(theme::Hex(QStringLiteral("accent")));
    const QColor accentHover(theme::Hex(QStringLiteral("accenthover")));
    const QColor text(theme::Hex(QStringLiteral("text")));
    const QColor muted(theme::Hex(QStringLiteral("muted")));

    const QPointF c(width() / 2.0, height() / 2.0);

    // 后层光晕
    QRadialGradient rg(c, 52);
    rg.setColorAt(0.0, QColor(accent.red(), accent.green(), accent.blue(), 64));
    rg.setColorAt(1.0, QColor(accent.red(), accent.green(), accent.blue(), 0));
    p.setPen(Qt::NoPen);
    p.setBrush(rg);
    p.drawEllipse(c, 54, 54);

    // 两个相扣的圆环：左边实心、右边描边，表示「本机」和「远端模型」
    const qreal r = 26;
    const QPointF left(c.x() - 15, c.y());
    const QPointF right(c.x() + 15, c.y());

    p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 210));
    p.drawEllipse(left, r, r);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(muted, 3.4));
    p.drawEllipse(right, r, r);

    // 交叠处的高光弧
    p.setPen(QPen(QColor(255, 255, 255, 150), 2.2, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(right.x() - r, right.y() - r, r * 2, r * 2), 16 * 110, 16 * 70);

    // 中心的小点：信号
    p.setPen(Qt::NoPen);
    p.setBrush(accentHover);
    p.drawEllipse(c, 5.4, 5.4);

    // 底部平台线
    p.setPen(QPen(text, 2.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(c.x() - 46, c.y() + 40), QPointF(c.x() + 46, c.y() + 40));
  }
};

}  // namespace

WelcomeDialog::WelcomeDialog(Config* cfg, QWidget* parent) : QDialog(parent), cfg_(cfg) {
  setWindowTitle(QStringLiteral("欢迎使用 WorkDesk"));
  setModal(true);
  // 向导不要问号、不要最大最小化，就是个顺次点下去的卡片
  setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                 Qt::WindowCloseButtonHint);
  resize(560, 520);
  probe_ = new Probe();
  probeTimer_ = new QTimer(this);
  probeTimer_->setInterval(120);
  connect(probeTimer_, &QTimer::timeout, this, &WelcomeDialog::PollProbe);
  BuildUi();
  GoTo(0);
}

WelcomeDialog::~WelcomeDialog() {
  if (probe_) {
    probe_->Shutdown();
    delete probe_;
    probe_ = nullptr;
  }
}

void WelcomeDialog::BuildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(22, 18, 22, 16);
  root->setSpacing(12);

  root->addWidget(new WelcomeArt(this));

  auto* title = MkText(QStringLiteral("欢迎来到 WorkDesk"), QStringLiteral("WelcomeTitle"));
  title->setAlignment(Qt::AlignHCenter);
  root->addWidget(title);

  stepLab_ = MkText(QString(), QStringLiteral("ModuleHint"));
  stepLab_->setAlignment(Qt::AlignHCenter);
  root->addWidget(stepLab_);

  stack_ = new QStackedWidget(this);
  root->addWidget(stack_, 1);

  // ---------------- 第 1 页：说明
  {
    auto* page = new QWidget();
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 8, 0, 0);
    lay->setSpacing(10);
    lay->addWidget(MkText(QStringLiteral("这是一个直接连模型接口的本地客户端，没有中间服务器，"
                                         "API Key 只存在你自己电脑上。"),
                          QStringLiteral("WelcomeBody")));
    lay->addWidget(MkText(QStringLiteral("接下来花一分钟，我们分三步把它配好："), nullptr));
    for (const QString& line : {QStringLiteral("  1.  填接口地址、模型名和 API Key"),
                                QStringLiteral("  2.  挑一个对话性格"),
                                QStringLiteral("  3.  填个生日（可选，填了那天会有个小惊喜）")}) {
      auto* l = MkText(line, nullptr);
      l->setContentsMargins(6, 0, 0, 0);
      lay->addWidget(l);
    }
    lay->addStretch(1);
    lay->addWidget(MkText(
        QStringLiteral("随时可以在右上角 dock 栏的「设置」里改这些内容，也可以直接跳过，"
                       "先看看界面再说。"),
        QStringLiteral("ModuleHint")));
    stack_->addWidget(page);
  }

  // ---------------- 第 2 页：接口
  {
    auto* page = new QWidget();
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 4, 0, 0);
    lay->setSpacing(8);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);

    auto addRow = [grid](int r, const QString& label, QWidget* w) {
      auto* l = new QLabel(label);
      l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      grid->addWidget(l, r, 0);
      grid->addWidget(w, r, 1);
    };

    provider_ = new QComboBox(page);
    for (int i = 0; i < kPresetCount; ++i) provider_->addItem(QString::fromUtf8(kPresets[i].name));
    connect(provider_, QOverload<int>::of(&QComboBox::activated), this,
            &WelcomeDialog::ApplyPreset);
    addRow(0, QStringLiteral("服务商"), provider_);

    baseUrl_ = new QLineEdit(page);
    baseUrl_->setPlaceholderText(QStringLiteral("https://api.deepseek.com/v1"));
    addRow(1, QStringLiteral("接口地址"), baseUrl_);

    model_ = new QLineEdit(page);
    model_->setPlaceholderText(QStringLiteral("deepseek-chat"));
    addRow(2, QStringLiteral("模型"), model_);

    apiKey_ = new QLineEdit(page);
    apiKey_->setEchoMode(QLineEdit::Password);
    apiKey_->setPlaceholderText(QStringLiteral("sk-..."));
    addRow(3, QStringLiteral("API Key"), apiKey_);

    temp_ = new QDoubleSpinBox(page);
    temp_->setRange(0.0, 2.0);
    temp_->setSingleStep(0.1);
    temp_->setDecimals(1);
    temp_->setValue(0.7);
    addRow(4, QStringLiteral("温度"), temp_);

    lay->addLayout(grid);

    auto* hint = MkText(QString(), QStringLiteral("ModuleHint"));
    hint->setObjectName(QStringLiteral("WelcomeHint"));
    lay->addWidget(hint);
    connect(provider_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [hint](int i) {
      if (i >= 0 && i < kPresetCount) hint->setText(QString::fromUtf8(kPresets[i].hint));
    });

    auto* testRow = new QHBoxLayout();
    testBtn_ = new QPushButton(QStringLiteral("测试连接"), page);
    connect(testBtn_, &QPushButton::clicked, this, &WelcomeDialog::DoTest);
    testRow->addWidget(testBtn_);
    probeLab_ = MkText(QString(), QStringLiteral("ModuleHint"), false);
    testRow->addWidget(probeLab_, 1);
    lay->addLayout(testRow);

    lay->addStretch(1);
    lay->addWidget(MkText(QStringLiteral("地址填到 /v1 就行，程序会自动补 /chat/completions。"
                                         "现在不想填也可以，先点「跳过」进界面。"),
                          QStringLiteral("ModuleHint")));
    stack_->addWidget(page);

    ApplyPreset(0);
  }

  // ---------------- 第 3 页：性格
  {
    auto* page = new QWidget();
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 8, 0, 0);
    lay->setSpacing(10);
    lay->addWidget(MkText(QStringLiteral("同一个模型，换个说话方式，用起来手感完全不一样。"
                                         "这几项之后随时能改。"),
                          QStringLiteral("WelcomeBody")));

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);
    auto addRow = [grid](int r, const QString& label, QWidget* w) {
      auto* l = new QLabel(label);
      l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      grid->addWidget(l, r, 0);
      grid->addWidget(w, r, 1);
    };

    persona_ = new QComboBox(page);
    int pc = 0;
    const PersonaPreset* ps = PersonaPresets(pc);
    for (int i = 0; i < pc; ++i) persona_->addItem(QString::fromUtf8(ps[i].label));
    addRow(0, QStringLiteral("性格"), persona_);

    verbosity_ = new QComboBox(page);
    verbosity_->addItem(QStringLiteral("简洁（一两句）"));
    verbosity_->addItem(QStringLiteral("适中"));
    verbosity_->addItem(QStringLiteral("详细（分步骤）"));
    verbosity_->setCurrentIndex(1);
    addRow(1, QStringLiteral("回答长度"), verbosity_);

    address_ = new QLineEdit(page);
    address_->setPlaceholderText(QStringLiteral("留空也行，比如：老板"));
    addRow(2, QStringLiteral("怎么称呼你"), address_);

    lay->addLayout(grid);
    lay->addStretch(1);
    stack_->addWidget(page);
  }

  // ---------------- 第 4 页：生日
  {
    auto* page = new QWidget();
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 8, 0, 0);
    lay->setSpacing(10);
    lay->addWidget(MkText(QStringLiteral("最后一个，和 AI 无关：留个生日吧。"),
                          QStringLiteral("WelcomeBody")));
    lay->addWidget(MkText(QStringLiteral("到了那天，打开 WorkDesk 会有一盏蛋糕等着你。不填也没关系，"
                                         "随时能在设置里补。"),
                          QStringLiteral("ModuleHint")));

    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    monthBox_ = new QComboBox(page);
    monthBox_->addItem(QStringLiteral("不填"), 0);
    for (int m = 1; m <= 12; ++m)
      monthBox_->addItem(QStringLiteral("%1 月").arg(m), m);
    dayBox_ = new QComboBox(page);
    dayBox_->addItem(QStringLiteral("不填"), 0);
    for (int d = 1; d <= 31; ++d) dayBox_->addItem(QStringLiteral("%1 日").arg(d), d);
    row->addWidget(monthBox_, 1);
    row->addWidget(dayBox_, 1);
    lay->addLayout(row);

    birthdayTip_ = MkText(QStringLiteral("只存月和日，不存年份，也不会上传。"),
                          QStringLiteral("ModuleHint"));
    lay->addWidget(birthdayTip_);
    lay->addStretch(1);
    stack_->addWidget(page);

    // 月份变了就把当月天数对齐（2 月给 29，闰年不纠结）
    connect(monthBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
      const int m = monthBox_->currentData().toInt();
      static const int kDays[13] = {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      const int maxDay = (m >= 1 && m <= 12) ? kDays[m] : 31;
      const int keep = dayBox_->currentData().toInt();
      const bool blocked = dayBox_->blockSignals(true);
      dayBox_->clear();
      dayBox_->addItem(QStringLiteral("不填"), 0);
      for (int d = 1; d <= maxDay; ++d) dayBox_->addItem(QStringLiteral("%1 日").arg(d), d);
      dayBox_->setCurrentIndex(keep > 0 && keep <= maxDay ? keep : 0);
      dayBox_->blockSignals(blocked);
    });
  }

  // ---------------- 底部导航
  auto* nav = new QHBoxLayout();
  nav->setSpacing(8);
  skipBtn_ = new QPushButton(QStringLiteral("跳过"), this);
  connect(skipBtn_, &QPushButton::clicked, this, [this]() { Finish(false); });
  nav->addWidget(skipBtn_);
  nav->addStretch(1);

  backBtn_ = new QPushButton(QStringLiteral("上一步"), this);
  connect(backBtn_, &QPushButton::clicked, this, [this]() { GoTo(page_ - 1); });
  nav->addWidget(backBtn_);

  nextBtn_ = new QPushButton(QStringLiteral("下一步"), this);
  nextBtn_->setObjectName(QStringLiteral("Primary"));
  connect(nextBtn_, &QPushButton::clicked, this, [this]() {
    if (page_ >= stack_->count() - 1)
      Finish(true);
    else
      GoTo(page_ + 1);
  });
  nav->addWidget(nextBtn_);
  root->addLayout(nav);
}

void WelcomeDialog::ApplyPreset(int idx) {
  if (idx < 0 || idx >= kPresetCount) return;
  const bool custom = (idx == kPresetCount - 1);
  if (!custom) {
    baseUrl_->setText(QString::fromUtf8(kPresets[idx].baseUrl));
    model_->setText(QString::fromUtf8(kPresets[idx].model));
  }
}

void WelcomeDialog::GoTo(int page) {
  if (page < 0 || page >= stack_->count()) return;
  page_ = page;
  stack_->setCurrentIndex(page);
  const int total = stack_->count();
  stepLab_->setText(QStringLiteral("第 %1 / %2 步").arg(page + 1).arg(total));
  backBtn_->setEnabled(page > 0);
  nextBtn_->setText(page >= total - 1 ? QStringLiteral("完成") : QStringLiteral("下一步"));
  skipBtn_->setText(page >= total - 1 ? QStringLiteral("先不填") : QStringLiteral("跳过"));
}

bool WelcomeDialog::CanLeavePage() const { return true; }

void WelcomeDialog::DoTest() {
  if (probe_ && probe_->Running()) return;
  const QString url = baseUrl_->text().trimmed();
  const QString modelName = model_->text().trimmed();
  if (url.isEmpty() || modelName.isEmpty()) {
    probeLab_->setText(QStringLiteral("先填接口地址和模型"));
    return;
  }
  probeLab_->setText(QStringLiteral("正在测试…"));
  testBtn_->setEnabled(false);
  probe_->Start(NormalizeEndpoint(url.toStdString()), apiKey_->text().trimmed().toStdString(),
                modelName.toStdString());
  probeTimer_->start();
}

void WelcomeDialog::PollProbe() {
  if (!probe_) {
    probeTimer_->stop();
    return;
  }
  std::string status;
  bool ok = false;
  if (probe_->Consume(status, ok)) {
    probeLab_->setText(QString::fromStdString(status));
    probeLab_->setObjectName(ok ? QStringLiteral("StatusOk") : QStringLiteral("StatusErr"));
    probeLab_->style()->unpolish(probeLab_);
    probeLab_->style()->polish(probeLab_);
    testBtn_->setEnabled(true);
    probeTimer_->stop();
  } else if (!probe_->Running()) {
    probeLab_->setText(QStringLiteral("测试结束，无响应"));
    testBtn_->setEnabled(true);
    probeTimer_->stop();
  }
}

// save=true 走完成流程：把表单写进第一套 profile 并落盘；
// save=false 是跳过：不改配置内容，只写 onboard_done 标记。
void WelcomeDialog::Finish(bool save) {
  finished_ = save;
  if (cfg_ && save) {
    cfg_->EnsureProfiles();
    if (!cfg_->profiles.empty()) {
      Profile& p = cfg_->profiles.front();
      const QString url = baseUrl_->text().trimmed();
      const QString modelName = model_->text().trimmed();
      if (!url.isEmpty()) p.baseUrl = url.toStdString();
      if (!modelName.isEmpty()) p.model = modelName.toStdString();
      const QString key = apiKey_->text().trimmed();
      if (!key.isEmpty()) p.apiKey = key.toStdString();
      p.provider = provider_->currentText().toStdString();
      p.temperature = temp_->value();
      if (p.name.empty() || p.name == "默认")
        p.name = p.model.empty() ? std::string("默认") : p.model;

      int pc = 0;
      const PersonaPreset* ps = PersonaPresets(pc);
      const int pi = persona_->currentIndex();
      if (pi >= 0 && pi < pc) p.persona = ps[pi].key;
      p.verbosity = verbosity_->currentIndex();
      const QString addr = address_->text().trimmed();
      if (!addr.isEmpty()) p.addressAs = addr.toStdString();

      cfg_->activeProfile = 0;
      cfg_->SyncTopFromActive();
    }

    const int m = monthBox_->currentData().toInt();
    const int d = dayBox_->currentData().toInt();
    if (m >= 1 && m <= 12 && d >= 1 && d <= 31) {
      // 只存月日，年份固定占位 0000：不记录用户年龄，也没有隐私负担
      char buf[16];
      snprintf(buf, sizeof(buf), "0000-%02d-%02d", m, d);
      cfg_->birthday = buf;
    }
    cfg_->Save();
  }
  accept();
}

}  // namespace wd
