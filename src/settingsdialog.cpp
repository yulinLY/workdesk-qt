#include "settingsdialog.h"

#include <QCheckBox>
#include <QStyle>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "http.h"

namespace wd {

namespace {

// 状态文字（成功绿 / 失败红）走全局 QSS 的 StatusOk / StatusErr，两套主题各有一套色值；
// 换 objectName 后必须 unpolish/polish 一次，否则新规则不会套到已有控件上。
void SetLabelState(QLabel* lab, bool ok) {
  if (!lab) return;
  lab->setObjectName(ok ? QStringLiteral("StatusOk") : QStringLiteral("StatusErr"));
  lab->style()->unpolish(lab);
  lab->style()->polish(lab);
}

}  // namespace
namespace {

struct Preset {
  const char* name;
  const char* baseUrl;
  const char* models;  // 逗号分隔的候选：第一个是默认值，其余进「模型」下拉
};

const Preset kPresets[] = {
    {"智谱 GLM", "https://open.bigmodel.cn/api/paas/v4",
     "glm-4.6,glm-4.5,glm-4.5-air,glm-4-plus,glm-4-flash"},
    {"DeepSeek", "https://api.deepseek.com/v1", "deepseek-chat,deepseek-reasoner"},
    {"OpenAI", "https://api.openai.com/v1", "gpt-4o-mini,gpt-4o,gpt-4.1,gpt-4.1-mini,o4-mini"},
    {"通义千问（阿里云百炼）", "https://dashscope.aliyuncs.com/compatible-mode/v1",
     "qwen-plus,qwen-max,qwen-turbo,qwen3-max,qwen3-235b-a22b"},
    {"月之暗面 Kimi", "https://api.moonshot.cn/v1",
     "kimi-latest,moonshot-v1-32k,moonshot-v1-128k,kimi-k2-0711-preview"},
    {"字节 火山方舟（豆包）", "https://ark.cn-beijing.volces.com/api/v3",
     "doubao-seed-1-6-250615,doubao-1-5-pro-32k-250115,deepseek-v3-250324"},
    {"百度千帆", "https://qianfan.baidubce.com/v2",
     "ernie-4.5-turbo-128k,ernie-4.5-8k-preview,ernie-4.0-8k,deepseek-v3"},
    {"腾讯混元", "https://api.hunyuan.cloud.tencent.com/v1",
     "hunyuan-turbos-latest,hunyuan-large-latest,hunyuan-standard-latest"},
    {"硅基流动 SiliconFlow", "https://api.siliconflow.cn/v1",
     "Qwen/Qwen3-8B,deepseek-ai/DeepSeek-V3,Qwen/Qwen2.5-72B-Instruct"},
    {"阶跃星辰 StepFun", "https://api.stepfun.com/v1", "step-2-16k,step-1-8k,step-1-flash"},
    {"百川智能", "https://api.baichuan-ai.com/v1", "Baichuan4-Turbo,Baichuan4,Baichuan3-Turbo"},
    {"MiniMax", "https://api.minimax.chat/v1", "MiniMax-Text-01,abab6.5s-chat"},
    {"xAI Grok", "https://api.x.ai/v1", "grok-4,grok-3,grok-3-mini"},
    {"Mistral", "https://api.mistral.ai/v1",
     "mistral-large-latest,mistral-small-latest,open-mistral-nemo"},
    {"Groq", "https://api.groq.com/openai/v1", "llama-3.3-70b-versatile,llama-3.1-8b-instant"},
    {"OpenRouter", "https://openrouter.ai/api/v1",
     "openai/gpt-4o-mini,anthropic/claude-sonnet-4,google/gemini-2.5-flash,deepseek/deepseek-chat"},
    {"本地 Ollama", "http://127.0.0.1:11434/v1", "qwen2.5:7b,llama3.1:8b,deepseek-r1:7b"},
    {"本地 LM Studio", "http://127.0.0.1:1234/v1", "qwen2.5-7b-instruct"},
    {"自定义", "", ""},
};
const int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

QString ShowName(const Profile& p) {
  QString n = QString::fromStdString(p.name).trimmed();
  const QString m = QString::fromStdString(p.model).trimmed();
  if (n.isEmpty()) n = m;
  if (n.isEmpty()) return QStringLiteral("未命名");
  if (m.isEmpty() || n == m) return n;
  return QStringLiteral("%1  ·  %2").arg(n, m);
}

int ClampIndex(int idx, int size) {
  if (size <= 0) return -1;
  if (idx < 0) return 0;
  if (idx >= size) return size - 1;
  return idx;
}

}  // namespace

SettingsDialog::SettingsDialog(Config* cfg, QWidget* parent) : QDialog(parent), cfg_(cfg) {
  setWindowTitle(QStringLiteral("AI 设置"));
  resize(680, 640);
  probe_ = new Probe();
  probeTimer_ = new QTimer(this);
  probeTimer_->setInterval(120);
  connect(probeTimer_, &QTimer::timeout, this, &SettingsDialog::PollProbe);
  BuildUi();
}

SettingsDialog::~SettingsDialog() {
  if (probe_) {
    probe_->Shutdown();
    delete probe_;
    probe_ = nullptr;
  }
}

void SettingsDialog::BuildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 14, 16, 12);
  root->setSpacing(10);

  // ---- 配置条目条 ----
  auto* bar = new QHBoxLayout();
  bar->setSpacing(6);
  bar->addWidget(new QLabel(QStringLiteral("配置"), this));
  profileBox_ = new QComboBox(this);
  profileBox_->setMinimumWidth(190);
  profileBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  connect(profileBox_, QOverload<int>::of(&QComboBox::activated), this,
          &SettingsDialog::SwitchProfile);
  bar->addWidget(profileBox_, 1);

  newBtn_ = new QPushButton(QStringLiteral("新建"), this);
  dupBtn_ = new QPushButton(QStringLiteral("复制"), this);
  renameBtn_ = new QPushButton(QStringLiteral("重命名"), this);
  delBtn_ = new QPushButton(QStringLiteral("删除"), this);
  connect(newBtn_, &QPushButton::clicked, this, &SettingsDialog::OnNewProfile);
  connect(dupBtn_, &QPushButton::clicked, this, &SettingsDialog::OnDuplicateProfile);
  connect(renameBtn_, &QPushButton::clicked, this, &SettingsDialog::OnRenameProfile);
  connect(delBtn_, &QPushButton::clicked, this, &SettingsDialog::OnDeleteProfile);
  bar->addWidget(newBtn_);
  bar->addWidget(dupBtn_);
  bar->addWidget(renameBtn_);
  bar->addWidget(delBtn_);
  root->addLayout(bar);

  // ---- 分页：接口 / 性格 ----
  auto* tabs = new QTabWidget(this);
  // 内容较高的页各包一层滚动区，窗口高度不随内容膨胀
  auto wrapPage = [](QWidget* page) {
    auto* sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setFrameShape(QFrame::NoFrame);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setWidget(page);
    return sc;
  };

  // 接口页
  auto* apiPage = new QWidget();
  auto* apiLay = new QVBoxLayout(apiPage);
  apiLay->setContentsMargins(4, 10, 4, 4);
  apiLay->setSpacing(9);

  auto* form = new QFormLayout();
  form->setSpacing(9);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

  provider_ = new QComboBox(apiPage);
  for (int i = 0; i < kPresetCount; ++i)
    provider_->addItem(QString::fromUtf8(kPresets[i].name));
  connect(provider_, QOverload<int>::of(&QComboBox::activated), this,
          &SettingsDialog::ApplyProviderPreset);
  form->addRow(QStringLiteral("服务商"), provider_);

  baseUrl_ = new QLineEdit(apiPage);
  baseUrl_->setPlaceholderText(QStringLiteral("https://api.deepseek.com/v1"));
  form->addRow(QStringLiteral("接口地址"), baseUrl_);

  // 模型：可下拉选（跟服务商自动带出候选），也能直接手输表里没有的新模型
  model_ = new QComboBox(apiPage);
  model_->setEditable(true);
  model_->setInsertPolicy(QComboBox::NoInsert);
  model_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  model_->setMinimumWidth(260);
  model_->lineEdit()->setPlaceholderText(QStringLiteral("deepseek-chat"));
  model_->lineEdit()->setClearButtonEnabled(true);
  form->addRow(QStringLiteral("模型"), model_);

  apiKey_ = new QLineEdit(apiPage);
  apiKey_->setEchoMode(QLineEdit::Password);
  apiKey_->setPlaceholderText(QStringLiteral("sk-..."));
  form->addRow(QStringLiteral("API Key"), apiKey_);

  temp_ = new QDoubleSpinBox(apiPage);
  temp_->setRange(0.0, 2.0);
  temp_->setSingleStep(0.1);
  temp_->setDecimals(1);
  form->addRow(QStringLiteral("温度"), temp_);

  apiLay->addLayout(form);
  apiLay->addStretch(1);

  auto* apiTip = new QLabel(
      QStringLiteral("接口地址填到 /v1（或该服务商的等价前缀）即可，程序自动补 /chat/completions。\n"
                     "「模型」可以从下拉候选里选，也能直接手输新模型名。\n"
                     "全部配置存在 %APPDATA%\\WorkDesk\\config.json，与旧版通用。"),
      apiPage);
  apiTip->setObjectName("ModuleHint");
  apiTip->setWordWrap(true);
  apiLay->addWidget(apiTip);
  tabs->addTab(wrapPage(apiPage), QStringLiteral("接口"));

  // 性格页
  auto* tonePage = new QWidget();
  auto* toneLay = new QVBoxLayout(tonePage);
  toneLay->setContentsMargins(4, 10, 4, 4);
  toneLay->setSpacing(9);

  auto* form2 = new QFormLayout();
  form2->setSpacing(9);
  form2->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

  persona_ = new QComboBox(tonePage);
  int pc = 0;
  const PersonaPreset* ps = PersonaPresets(pc);
  for (int i = 0; i < pc; ++i) persona_->addItem(QString::fromUtf8(ps[i].label));
  form2->addRow(QStringLiteral("性格"), persona_);

  verbosity_ = new QComboBox(tonePage);
  verbosity_->addItem(QStringLiteral("简洁（一两句）"));
  verbosity_->addItem(QStringLiteral("适中"));
  verbosity_->addItem(QStringLiteral("详细（分步骤）"));
  form2->addRow(QStringLiteral("回答长度"), verbosity_);

  address_ = new QLineEdit(tonePage);
  address_->setPlaceholderText(QStringLiteral("留空则不特别称呼，例如：老板"));
  form2->addRow(QStringLiteral("怎么称呼我"), address_);

  tone_ = new QLineEdit(tonePage);
  tone_->setPlaceholderText(QStringLiteral("优先级最高，写具体行为，例如：只用短句、不要客套、像机器人一样没有情绪"));
  form2->addRow(QStringLiteral("语气补充"), tone_);

  auto* flags = new QHBoxLayout();
  emoji_ = new QCheckBox(QStringLiteral("允许 emoji"), tonePage);
  markdown_ = new QCheckBox(QStringLiteral("允许 Markdown"), tonePage);
  markdown_->setChecked(true);
  flags->addWidget(emoji_);
  flags->addSpacing(14);
  flags->addWidget(markdown_);
  flags->addStretch(1);
  form2->addRow(QStringLiteral("输出"), flags);
  toneLay->addLayout(form2);

  toneLay->addWidget(new QLabel(QStringLiteral("额外要求"), tonePage));
  extra_ = new QPlainTextEdit(tonePage);
  extra_->setMinimumHeight(54);
  extra_->setPlaceholderText(QStringLiteral("例如：涉及命令一律给出可直接粘贴的完整命令"));
  toneLay->addWidget(extra_);

  toneLay->addWidget(new QLabel(QStringLiteral("系统提示词"), tonePage));
  sysPrompt_ = new QPlainTextEdit(tonePage);
  sysPrompt_->setMinimumHeight(92);
  toneLay->addWidget(sysPrompt_);

  auto* toneTip = new QLabel(
      QStringLiteral("性格、长度、称呼等设定会自动拼在系统提示词后面，只对这套配置生效。"),
      tonePage);
  toneTip->setObjectName("ModuleHint");
  toneTip->setWordWrap(true);
  toneLay->addWidget(toneTip);
  toneLay->addStretch(1);
  tabs->addTab(wrapPage(tonePage), QStringLiteral("性格"));

  root->addWidget(tabs, 1);

  // ---- 测试 + 状态 ----
  auto* probeRow = new QHBoxLayout();
  testBtn_ = new QPushButton(QStringLiteral("测试连接"), this);
  connect(testBtn_, &QPushButton::clicked, this, &SettingsDialog::TestConnection);
  probeRow->addWidget(testBtn_);
  probeLab_ = new QLabel(QString(), this);
  probeLab_->setObjectName("Muted");
  probeRow->addWidget(probeLab_, 1);
  root->addLayout(probeRow);

  // ---- 底部按钮 ----
  auto* bottom = new QHBoxLayout();
  stateLab_ = new QLabel(QString(), this);
  stateLab_->setObjectName("Muted");
  bottom->addWidget(stateLab_, 1);

  auto* saveBtn = new QPushButton(QStringLiteral("保存并启用"), this);
  saveBtn->setObjectName("Primary");
  connect(saveBtn, &QPushButton::clicked, this, [this]() { Commit(); });
  bottom->addWidget(saveBtn);

  auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
  bottom->addWidget(closeBtn);
  root->addLayout(bottom);

  // ---- 回填 ----
  if (cfg_) {
    cfg_->EnsureProfiles();
    ReloadProfileList(cfg_->activeProfile);
    LoadFormFromProfile(profileBox_->currentIndex());
  }
}

void SettingsDialog::ReloadProfileList(int select) {
  if (!cfg_) return;
  const bool blocked = profileBox_->blockSignals(true);
  profileBox_->clear();
  for (const auto& p : cfg_->profiles) profileBox_->addItem(ShowName(p));
  profileBox_->setCurrentIndex(ClampIndex(select, (int)cfg_->profiles.size()));
  profileBox_->blockSignals(blocked);
}

void SettingsDialog::LoadFormFromProfile(int idx) {
  if (!cfg_ || idx < 0 || idx >= (int)cfg_->profiles.size()) return;
  const Profile& p = cfg_->profiles[(size_t)idx];

  int match = kPresetCount - 1;
  for (int i = 0; i < kPresetCount - 1; ++i) {
    if (p.baseUrl == kPresets[i].baseUrl) {
      match = i;
      break;
    }
  }
  provider_->setCurrentIndex(match);
  baseUrl_->setText(QString::fromStdString(p.baseUrl));
  // 候选按服务商重新灌一遍，再把当前模型放上去（手输过的自定义模型照样保留）
  FillModelCandidates(match, QString::fromStdString(p.model));
  apiKey_->setText(QString::fromStdString(p.apiKey));
  temp_->setValue(p.temperature);

  int pi = 0;
  int pc = 0;
  const PersonaPreset* ps = PersonaPresets(pc);
  for (int i = 0; i < pc; ++i) {
    if (p.persona == ps[i].key) {
      pi = i;
      break;
    }
  }
  persona_->setCurrentIndex(pi);
  verbosity_->setCurrentIndex(p.verbosity <= 0 ? 0 : (p.verbosity >= 2 ? 2 : 1));
  address_->setText(QString::fromStdString(p.addressAs));
  tone_->setText(QString::fromStdString(p.tone));
  emoji_->setChecked(p.useEmoji);
  markdown_->setChecked(p.useMarkdown);
  extra_->setPlainText(QString::fromStdString(p.extraPrompt));
  sysPrompt_->setPlainText(QString::fromStdString(p.systemPrompt));

  probeLab_->clear();
  stateLab_->clear();
}

void SettingsDialog::SaveFormToProfile(int idx) {
  if (!cfg_ || idx < 0 || idx >= (int)cfg_->profiles.size()) return;
  Profile& p = cfg_->profiles[(size_t)idx];

  p.provider = provider_->currentText().toStdString();
  p.baseUrl = baseUrl_->text().trimmed().toStdString();
  p.model = model_->currentText().trimmed().toStdString();
  p.apiKey = apiKey_->text().trimmed().toStdString();
  p.temperature = temp_->value();

  int pc = 0;
  const PersonaPreset* ps = PersonaPresets(pc);
  const int pi = persona_->currentIndex();
  p.persona = (pi >= 0 && pi < pc) ? ps[pi].key : "rational";
  p.verbosity = verbosity_->currentIndex();
  p.addressAs = address_->text().trimmed().toStdString();
  p.tone = tone_->text().trimmed().toStdString();
  p.useEmoji = emoji_->isChecked();
  p.useMarkdown = markdown_->isChecked();
  p.extraPrompt = extra_->toPlainText().trimmed().toStdString();
  p.systemPrompt = sysPrompt_->toPlainText().toStdString();

  if (p.name.empty()) p.name = p.model.empty() ? std::string("未命名") : p.model;
}

void SettingsDialog::SwitchProfile(int idx) {
  if (!cfg_ || idx < 0 || idx >= (int)cfg_->profiles.size()) return;
  SaveFormToProfile(profileBox_->currentIndex());
  LoadFormFromProfile(idx);
  SetState(QStringLiteral("已切到这套配置，点「保存并启用」才会生效"), true);
}

void SettingsDialog::OnNewProfile() {
  if (!cfg_) return;
  SaveFormToProfile(profileBox_->currentIndex());

  const int cur = ClampIndex(profileBox_->currentIndex(), (int)cfg_->profiles.size());
  Profile p = cfg_->profiles[(size_t)cur];
  p.name = QStringLiteral("配置 %1").arg(cfg_->profiles.size() + 1).toStdString();
  p.apiKey.clear();
  cfg_->profiles.push_back(p);
  const int idx = (int)cfg_->profiles.size() - 1;
  cfg_->activeProfile = idx;
  cfg_->SyncTopFromActive();
  cfg_->Save();

  ReloadProfileList(idx);
  LoadFormFromProfile(idx);
  SetState(QStringLiteral("已新建一套配置，填好后点「保存并启用」"), true);
}

void SettingsDialog::OnDuplicateProfile() {
  if (!cfg_) return;
  SaveFormToProfile(profileBox_->currentIndex());

  const int cur = ClampIndex(profileBox_->currentIndex(), (int)cfg_->profiles.size());
  Profile p = cfg_->profiles[(size_t)cur];
  QString base = QString::fromStdString(p.name).trimmed();
  if (base.isEmpty()) base = QStringLiteral("配置");
  p.name = (base + QStringLiteral(" 副本")).toStdString();
  cfg_->profiles.push_back(p);
  const int idx = (int)cfg_->profiles.size() - 1;
  cfg_->activeProfile = idx;
  cfg_->SyncTopFromActive();
  cfg_->Save();

  ReloadProfileList(idx);
  LoadFormFromProfile(idx);
  SetState(QStringLiteral("已复制成一套新的配置"), true);
}

void SettingsDialog::OnRenameProfile() {
  if (!cfg_) return;
  const int cur = ClampIndex(profileBox_->currentIndex(), (int)cfg_->profiles.size());
  if (cur < 0) return;

  bool ok = false;
  const QString name = QInputDialog::getText(
      this, QStringLiteral("重命名配置"), QStringLiteral("名称"), QLineEdit::Normal,
      QString::fromStdString(cfg_->profiles[(size_t)cur].name), &ok);
  if (!ok) return;

  QString n = name.trimmed();
  if (n.isEmpty()) n = QStringLiteral("未命名");
  cfg_->profiles[(size_t)cur].name = n.toStdString();
  cfg_->Save();
  ReloadProfileList(cur);
  SetState(QStringLiteral("已重命名"), true);
}

void SettingsDialog::OnDeleteProfile() {
  if (!cfg_) return;
  if (cfg_->profiles.size() <= 1) {
    SetState(QStringLiteral("至少要保留一套配置"), false);
    return;
  }
  const int cur = ClampIndex(profileBox_->currentIndex(), (int)cfg_->profiles.size());
  if (cur < 0) return;

  const QString name = QString::fromStdString(cfg_->profiles[(size_t)cur].name);
  if (QMessageBox::question(this, QStringLiteral("删除配置"),
                            QStringLiteral("删除配置「%1」？删除后无法恢复。").arg(name),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) != QMessageBox::Yes)
    return;

  cfg_->profiles.erase(cfg_->profiles.begin() + cur);
  cfg_->activeProfile = ClampIndex(cur, (int)cfg_->profiles.size());
  cfg_->SyncTopFromActive();
  cfg_->Save();

  ReloadProfileList(cfg_->activeProfile);
  LoadFormFromProfile(profileBox_->currentIndex());
  SetState(QStringLiteral("已删除"), true);
}

void SettingsDialog::ApplyProviderPreset(int idx) {
  if (idx < 0 || idx >= kPresetCount - 1) return;  // 自定义：不动已有内容
  baseUrl_->setText(QString::fromUtf8(kPresets[idx].baseUrl));
  // 换服务商时把该家的候选模型一起带出来，默认选第一个
  FillModelCandidates(idx, QString());
}

// 候选来自预设里逗号分隔的模型串；keep 非空则把它设成当前值（不在候选里就补一条），
// 这样手输的自定义模型在切换配置、重开设置时都不会被抹掉。
void SettingsDialog::FillModelCandidates(int presetIdx, const QString& keep) {
  if (!model_) return;
  const bool blocked = model_->blockSignals(true);
  model_->clear();
  if (presetIdx >= 0 && presetIdx < kPresetCount) {
    const char* csv = kPresets[presetIdx].models;
    const QString all = QString::fromUtf8(csv ? csv : "");
    for (const QString& one : all.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
      const QString t = one.trimmed();
      if (!t.isEmpty() && model_->findText(t, Qt::MatchFixedString) < 0) model_->addItem(t);
    }
  }
  const QString k = keep.trimmed();
  if (!k.isEmpty()) {
    if (model_->findText(k, Qt::MatchFixedString) < 0) model_->addItem(k);
    model_->setCurrentText(k);
  } else if (model_->count() > 0) {
    model_->setCurrentIndex(0);
  } else {
    model_->setEditText(QString());
  }
  model_->blockSignals(blocked);
}

bool SettingsDialog::Commit() {
  if (!cfg_) return false;
  const QString url = baseUrl_->text().trimmed();
  const QString modelName = model_->currentText().trimmed();
  if (url.isEmpty() || modelName.isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("配置不完整"),
                         QStringLiteral("接口地址和模型名都必须填写。"));
    return false;
  }

  const int idx = ClampIndex(profileBox_->currentIndex(), (int)cfg_->profiles.size());
  SaveFormToProfile(idx);
  cfg_->activeProfile = idx;
  cfg_->SyncTopFromActive();

  if (!cfg_->Save()) {
    QMessageBox::warning(this, QStringLiteral("保存失败"),
                         QStringLiteral("写入 config.json 失败，请检查磁盘权限。"));
    return false;
  }
  ReloadProfileList(idx);
  SetState(QStringLiteral("已保存并启用，下一条消息开始生效"), true);
  return true;
}

void SettingsDialog::TestConnection() {
  if (probe_ && probe_->Running()) return;
  const QString url = baseUrl_->text().trimmed();
  const QString modelName = model_->currentText().trimmed();
  if (url.isEmpty() || modelName.isEmpty()) {
    probeLab_->setText(QStringLiteral("先填接口地址和模型"));
    return;
  }
  probeLab_->setText(QStringLiteral("正在测试…"));
  testBtn_->setEnabled(false);
  // 用当前表单值探活，不写盘
  const std::string endpoint = NormalizeEndpoint(url.toStdString());
  if (probe_)
    probe_->Start(endpoint, apiKey_->text().trimmed().toStdString(), modelName.toStdString());
  probeTimer_->start();
}

void SettingsDialog::PollProbe() {
  if (!probe_) {
    probeTimer_->stop();
    return;
  }
  std::string status;
  bool ok = false;
  if (probe_->Consume(status, ok)) {
    probeLab_->setText(QString::fromStdString(status));
    SetLabelState(probeLab_, ok);
    testBtn_->setEnabled(true);
    probeTimer_->stop();
  } else if (!probe_->Running()) {
    probeLab_->setText(QStringLiteral("测试结束，无响应"));
    probeTimer_->stop();
    testBtn_->setEnabled(true);
  }
}

void SettingsDialog::SetState(const QString& text, bool ok) {
  if (!stateLab_) return;
  stateLab_->setText(text);
  SetLabelState(stateLab_, ok);
}

}  // namespace wd
