#include "theme.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTime>
#include <QTimer>
#include <QToolTip>
#include <QVector>

namespace wd {
namespace theme {
namespace {

// token -> 深色 / 亮色 两套色值。深色沿用 ImGui 版那套灰蓝，亮色与官网 light 主题同源，
// 保证客户端和官网看着是一个产品。
struct ColorPair {
  const char* token;
  const char* dark;
  const char* light;
};

const ColorPair kColors[] = {
    // 背景 / 层次
    {"bg", "#1B1E22", "#F4F6FA"},
    {"card", "#23262B", "#FFFFFF"},
    {"input", "#1E2126", "#FFFFFF"},
    {"head", "#262A30", "#EEF2F8"},
    // 毛玻璃层：带 alpha，透出窗口底下由 DWM 亚克力提供的模糊背景。
    // 只有这两处用 alpha，卡片里的气泡/输入框仍是实心，保证文字清晰。
    {"glass", "rgba(18, 20, 24, 128)", "rgba(247, 249, 253, 104)"},
    {"glasscard", "rgba(38, 42, 48, 142)", "rgba(255, 255, 255, 180)"},
    {"line", "#2F343B", "#DCE2EC"},
    {"dockline", "#31363E", "#E2E7F0"},
    // 按钮 / 交互态
    {"btn", "#2C3138", "#EDF1F7"},
    {"btnhover", "#343B44", "#E4EAF3"},
    {"btndown", "#272C33", "#DDE4EE"},
    {"btnline", "#3A414A", "#CFD7E3"},
    {"hover", "#2E343C", "#E9EEF6"},
    {"disabled", "#6B7280", "#A6B0BE"},
    {"disabled2", "#93A0B4", "#A9B4C4"},
    {"disline", "#333941", "#DCE2EC"},
    // 文字
    {"text", "#E6E9ED", "#1B2230"},
    {"text2", "#D7DCE2", "#2B3441"},
    {"text3", "#C3CAD3", "#3C4756"},
    {"text4", "#A9B2BD", "#55606E"},
    {"text5", "#9AA3AE", "#5F6B7A"},
    {"muted", "#8A929C", "#6E7988"},
    {"faint", "#7C848F", "#7A8494"},
    {"faint2", "#6E767F", "#8A94A3"},
    {"white", "#FFFFFF", "#FFFFFF"},
    // 强调色
    {"accent", "#4C8DFF", "#2F6FE4"},
    {"accenthover", "#5D98FF", "#4C86F0"},
    {"accenttext", "#CFE0FF", "#1F5FD0"},
    {"sel", "#3B5C99", "#3E7BEF"},
    {"selsoft", "#34405A", "#E1EAFB"},
    // 滚动条 / 装饰
    {"handle", "#4A5460", "#B9C3D1"},
    {"handle2", "#4A5563", "#A9B4C4"},
    {"mark", "#3E454F", "#C3CBD8"},
    // 代码块
    {"codebg", "#1A1D22", "#F7F9FD"},
    {"codehead", "#212630", "#EEF2F9"},
    {"codescroll", "#15181C", "#E7ECF4"},
    {"codelang", "#8FB6FF", "#1F5FD0"},
    {"codeinfo", "#6E7885", "#6E7988"},
    // 气泡
    {"userbubble", "#3A5B96", "#3E7BEF"},
    {"userbubbleline", "#47679F", "#2F6FE4"},
    {"meta", "#9FB4D8", "#5C6C86"},
    // 状态
    {"ok", "#5FD08A", "#128A5C"},
    {"err", "#E06C6C", "#C23B3B"},
};

// 偏好单独存 ui.ini：不和 config.json 里的 AI 配置混在一起，切版本互不影响
QString SettingsPath() {
  QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (dir.isEmpty()) dir = QDir::homePath() + QStringLiteral("/.workdesk");
  QDir().mkpath(dir);
  return dir + QStringLiteral("/ui.ini");
}

QColor C(const char* hex) { return QColor(QString::fromLatin1(hex)); }

}  // namespace

QString ModeKey(Mode m) {
  switch (m) {
    case Mode::Light: return QStringLiteral("light");
    case Mode::System: return QStringLiteral("system");
    case Mode::Time: return QStringLiteral("time");
    case Mode::Dark:
    default: return QStringLiteral("dark");
  }
}

Mode ModeFromKey(const QString& key, Mode fallback) {
  const QString k = key.trimmed().toLower();
  if (k == QLatin1String("dark")) return Mode::Dark;
  if (k == QLatin1String("light")) return Mode::Light;
  if (k == QLatin1String("system") || k == QLatin1String("auto")) return Mode::System;
  if (k == QLatin1String("time")) return Mode::Time;
  return fallback;
}

QString ModeLabel(Mode m) {
  switch (m) {
    case Mode::Light: return QStringLiteral("亮色模式");
    case Mode::System: return QStringLiteral("跟随系统");
    case Mode::Time: return QStringLiteral("跟随时间");
    case Mode::Dark:
    default: return QStringLiteral("深色模式");
  }
}

QString Hex(const QString& token) {
  const bool light = IsLightActive();
  for (const ColorPair& p : kColors) {
    if (token == QLatin1String(p.token)) return QString::fromLatin1(light ? p.light : p.dark);
  }
  return light ? QStringLiteral("#1B2230") : QStringLiteral("#E6E9ED");
}

bool IsLightActive() { return ThemeManager::Instance().light(); }

QString Css(bool light) {
  QString q = QString::fromUtf8(kQssTemplate);
  for (const ColorPair& p : kColors) {
    q.replace(QLatin1Char('@') + QString::fromLatin1(p.token) + QLatin1Char('@'),
              QString::fromLatin1(light ? p.light : p.dark));
  }
  return q;
}

QPalette Palette(bool light) {
  QPalette p;
  if (light) {
    const QColor win = C("#F4F6FA"), base = C("#FFFFFF"), card = C("#EEF2F8");
    const QColor text = C("#1B2230"), muted = C("#6E7988"), btn = C("#EDF1F7");
    p.setColor(QPalette::Window, win);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, card);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, btn);
    p.setColor(QPalette::ButtonText, C("#2B3441"));
    p.setColor(QPalette::BrightText, C("#D93B3B"));
    p.setColor(QPalette::Highlight, C("#2F6FE4"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Light, C("#FFFFFF"));
    p.setColor(QPalette::Mid, C("#CFD7E3"));
    p.setColor(QPalette::Dark, C("#B9C3D1"));
    p.setColor(QPalette::Shadow, C("#C7D0DC"));
    return p;
  }
  const QColor win = C("#1B1E22"), base = C("#1E2126"), card = C("#23262B");
  const QColor text = C("#E6E9ED"), muted = C("#7C848F"), btn = C("#2C3138");
  p.setColor(QPalette::Window, win);
  p.setColor(QPalette::WindowText, text);
  p.setColor(QPalette::Base, base);
  p.setColor(QPalette::AlternateBase, card);
  p.setColor(QPalette::Text, text);
  p.setColor(QPalette::Button, btn);
  p.setColor(QPalette::ButtonText, C("#D7DCE2"));
  p.setColor(QPalette::BrightText, C("#FF6B6B"));
  p.setColor(QPalette::Highlight, C("#3B5C99"));
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::ToolTipBase, btn);
  p.setColor(QPalette::ToolTipText, text);
  p.setColor(QPalette::PlaceholderText, muted);
  p.setColor(QPalette::Light, C("#2F343B"));
  p.setColor(QPalette::Mid, C("#3A414A"));
  p.setColor(QPalette::Dark, C("#15181B"));
  p.setColor(QPalette::Shadow, C("#101215"));
  return p;
}

ThemeManager& ThemeManager::Instance() {
  static ThemeManager mgr;
  return mgr;
}

bool ThemeManager::Resolve(Mode m) const {
  switch (m) {
    case Mode::Light: return true;
    case Mode::Dark: return false;
    case Mode::System: {
      const Qt::ColorScheme cs = QGuiApplication::styleHints()->colorScheme();
      return cs == Qt::ColorScheme::Light;
    }
    case Mode::Time:
    default: return QTime::currentTime().hour() < kLightBeforeHour;
  }
}

void ThemeManager::Apply() {
  light_ = Resolve(mode_);
  const QPalette pal = theme::Palette(light_);
  if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
    app->setPalette(pal);
    // QSS 模板靠 @token@ 填色，提示气泡在部分平台上会忽略其中的半透明/圆角组合而回落到深色。
    // 这里再追加一条纯 hex 的兜底规则，保证亮色模式下气泡也是浅底深字。
    QString qss = theme::Css(light_);
    qss += QStringLiteral(
               "\nQToolTip{background-color:%1;color:%2;border:1px solid %3;border-radius:6px;"
               "padding:5px 8px;font-size:12px;}")
               .arg(pal.color(QPalette::ToolTipBase).name(), pal.color(QPalette::ToolTipText).name(),
                    pal.color(QPalette::Mid).name());
    app->setStyleSheet(qss);
  }
  // 提示气泡是顶层工具窗，用的是 QToolTip 自己的静态调色板（启动时从当时的主题克隆），
  // 只改 QApplication 的调色板不会刷新它，切到亮色后提示框依旧是黑底。这里强制同步一次。
  QToolTip::setPalette(pal);
  emit Applied(light_);
}

void ThemeManager::Init() {
  QSettings st(SettingsPath(), QSettings::IniFormat);
  mode_ = ModeFromKey(st.value(QStringLiteral("theme/mode"), QStringLiteral("dark")).toString());
  lastHour_ = QTime::currentTime().hour();
  Apply();

  if (!timer_) {
    timer_ = new QTimer(this);
    timer_->setInterval(30000);  // 时间模式：30 秒探一次，跨过 18:00 立即换肤
    connect(timer_, &QTimer::timeout, this, [this]() {
      if (mode_ != Mode::Time) return;
      const int h = QTime::currentTime().hour();
      if (h == lastHour_) return;
      lastHour_ = h;
      Apply();
    });
    timer_->start();

    // 系统配色变化：只在「跟随系统」档位响应
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
              if (mode_ == Mode::System) Apply();
            });
  }
}

void ThemeManager::SetMode(Mode m) {
  if (m == mode_) {
    Refresh();
    return;
  }
  mode_ = m;
  lastHour_ = QTime::currentTime().hour();
  QSettings st(SettingsPath(), QSettings::IniFormat);
  st.setValue(QStringLiteral("theme/mode"), ModeKey(m));
  st.sync();
  Apply();
  emit ModeChanged(static_cast<int>(m), light_);
}

void ThemeManager::Refresh() { Apply(); }

}  // namespace theme
}  // namespace wd
