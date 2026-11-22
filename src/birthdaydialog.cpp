#include "birthdaydialog.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "theme.h"

namespace wd {

namespace {

// 吹满需要按住多久（毫秒）。太短像误触，太长又显得费劲
constexpr int kBlowMs = 1800;
constexpr int kTickMs = 30;

QString PickPhrase() {
  // 每次进来随机一句，不重复同一个模板
  static const char* kPhrases[] = {
      "岁数可以不数，蛋糕一定要吃。",
      "愿你今年的 Bug 都只在别人的代码里出现。",
      "愿你写的东西都能跑起来，跑起来的东西都别崩。",
      "愿望这东西，你不说出口，反而更容易实现。",
      "又长大一岁，也又多了一个可以重来的理由。",
      "今天别改需求了，改什么都行，就是别改自己的心情。",
  };
  const int n = (int)(sizeof(kPhrases) / sizeof(kPhrases[0]));
  const int i = (int)(QRandomGenerator::global()->bounded(n));
  return QString::fromUtf8(kPhrases[i]);
}

}  // namespace

BirthdayDialog::BirthdayDialog(const QString& userName, QWidget* parent)
    : QDialog(parent), name_(userName) {
  setWindowTitle(QStringLiteral("生日快乐"));
  setModal(true);
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground, true);
  // 让空格能进到 keyPressEvent，而不是被某个按钮吃掉
  setFocusPolicy(Qt::StrongFocus);
  resize(520, 560);
  BuildUi();

  timer_ = new QTimer(this);
  timer_->setInterval(kTickMs);
  connect(timer_, &QTimer::timeout, this, &BirthdayDialog::Tick);
}

void BirthdayDialog::BuildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);

  // 整块内容放在一张自绘圆角卡片上，边距给足
  auto* card = new QWidget(this);
  card->setObjectName(QStringLiteral("BirthdayCard"));
  root->addWidget(card);

  auto* lay = new QVBoxLayout(card);
  lay->setContentsMargins(30, 26, 30, 22);
  lay->setSpacing(10);

  title_ = new QLabel(this);
  title_->setObjectName(QStringLiteral("BirthdayTitle"));
  title_->setAlignment(Qt::AlignHCenter);
  title_->setText(name_.isEmpty()
                      ? QStringLiteral("生日快乐")
                      : QStringLiteral("%1，生日快乐").arg(name_));
  lay->addWidget(title_);

  stage_ = new QWidget(card);
  stage_->setFixedHeight(230);
  stage_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  lay->addWidget(stage_);

  body_ = new QLabel(PickPhrase(), card);
  body_->setObjectName(QStringLiteral("BirthdayBody"));
  body_->setAlignment(Qt::AlignHCenter);
  body_->setWordWrap(true);
  lay->addWidget(body_);

  hint_ = new QLabel(QStringLiteral("按住空格，把蜡烛吹灭"), card);
  hint_->setObjectName(QStringLiteral("BirthdayHint"));
  hint_->setAlignment(Qt::AlignHCenter);
  hint_->setWordWrap(true);
  lay->addWidget(hint_);

  // 吹灭后才出现：许个愿 + 收下祝福
  wishLab_ = new QLabel(QStringLiteral("吹灭了。许个愿吧，写不写都行。"), card);
  wishLab_->setObjectName(QStringLiteral("BirthdayBody"));
  wishLab_->setAlignment(Qt::AlignHCenter);
  wishLab_->setWordWrap(true);
  wishLab_->hide();
  lay->addWidget(wishLab_);

  wishEdit_ = new QLineEdit(card);
  wishEdit_->setPlaceholderText(QStringLiteral("悄悄写一句，只存在本机……"));
  wishEdit_->hide();
  connect(wishEdit_, &QLineEdit::returnPressed, this, [this]() {
    wish_ = wishEdit_->text().trimmed();
    accept();
  });
  lay->addWidget(wishEdit_);

  auto* row = new QHBoxLayout();
  row->addStretch(1);
  doneBtn_ = new QPushButton(QStringLiteral("收下祝福"), card);
  doneBtn_->setObjectName(QStringLiteral("Primary"));
  doneBtn_->hide();
  connect(doneBtn_, &QPushButton::clicked, this, [this]() {
    wish_ = wishEdit_->text().trimmed();
    accept();
  });
  row->addWidget(doneBtn_);
  row->addStretch(1);
  lay->addLayout(row);
}

void BirthdayDialog::showEvent(QShowEvent* e) {
  QDialog::showEvent(e);
  setFocus();
  if (timer_) timer_->start();
}

void BirthdayDialog::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Space && !blown_) {
    if (!e->isAutoRepeat()) holding_ = true;
    e->accept();
    return;
  }
  // 吹灭了以后空格不该再触发任何东西，避免长按误关窗口
  if (e->key() == Qt::Key_Space) {
    e->accept();
    return;
  }
  if (e->key() == Qt::Key_Escape) {
    e->accept();  // 不让 Esc 直接关掉，得看完整段
    return;
  }
  QDialog::keyPressEvent(e);
}

void BirthdayDialog::keyReleaseEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
    holding_ = false;
    e->accept();
    return;
  }
  QDialog::keyReleaseEvent(e);
}

void BirthdayDialog::Tick() {
  ++frame_;
  breath_ += 0.045f;
  wobble_ += 0.16f;

  if (!blown_) {
    if (holding_) {
      // 按住时进度推进，带一点加速度，最后 30% 冲得更快，有"吹灭"的爽感
      const float speed = blow_ < 0.7f ? 1.0f : 1.6f;
      blow_ += (float)kTickMs / (float)kBlowMs * speed;
      if (blow_ >= 1.0f) {
        blow_ = 1.0f;
        OnBlownOut();
      }
    } else {
      // 松开就缓慢回弹，别让用户觉得吹到一半白吹了
      blow_ = qMax(0.0f, blow_ - (float)kTickMs / (float)kBlowMs * 0.8f);
    }
    flame_ = 1.0f - blow_ * blow_;  // 先慢后快
    if (stage_) stage_->update();
  }
}

void BirthdayDialog::OnBlownOut() {
  blown_ = true;
  holding_ = false;
  if (timer_) timer_->stop();
  if (stage_) stage_->update();

  // 火苗没了，正文换成结局那句，把许愿输入框放出来
  hint_->setText(QStringLiteral("蜡烛灭了。"));
  body_->setText(QStringLiteral("灯还亮着，人还在，这就挺好。"));
  wishLab_->show();
  wishEdit_->show();
  doneBtn_->show();
  wishEdit_->setFocus();
}

void BirthdayDialog::GoQuestion() { OnBlownOut(); }

void BirthdayDialog::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const QRectF cardR = QRectF(rect()).adjusted(2, 2, -2, -2);
  const QColor bg(theme::Hex(QStringLiteral("card")));
  const QColor line(theme::Hex(QStringLiteral("line")));
  const QColor accent(theme::Hex(QStringLiteral("accent")));
  const QColor text(theme::Hex(QStringLiteral("text")));
  const QColor muted(theme::Hex(QStringLiteral("muted")));

  // 卡片底 + 描边
  p.setPen(QPen(line, 1.2));
  p.setBrush(bg);
  p.drawRoundedRect(cardR, 18, 18);

  if (!stage_) return;

  // ---- 舞台坐标：以 stage 中心为基准画蛋糕
  const QPointF origin = stage_->mapTo(this, QPoint(0, 0));
  const QRectF srect(origin, QSizeF(stage_->width(), stage_->height()));
  const QPointF c(srect.center().x(), srect.center().y() + 18);

  // 呼吸光晕：有火苗时是暖橙色，灭掉后变成冷色的一圈余晖
  const qreal pulse = 0.5 + 0.5 * std::sin(breath_);
  const QColor glowBase = blown_ ? accent : QColor(0xFF, 0xB4, 0x5A);
  const qreal radius = blown_ ? 74.0 : 92.0 + pulse * 14.0;
  QRadialGradient rg(c, radius);
  const int a0 = blown_ ? 34 : (int)(54 + pulse * 40);
  rg.setColorAt(0.0, QColor(glowBase.red(), glowBase.green(), glowBase.blue(), a0));
  rg.setColorAt(0.72, QColor(glowBase.red(), glowBase.green(), glowBase.blue(), a0 / 4));
  rg.setColorAt(1.0, QColor(glowBase.red(), glowBase.green(), glowBase.blue(), 0));
  p.setPen(Qt::NoPen);
  p.setBrush(rg);
  p.drawEllipse(c, radius, radius);

  // ---- 蛋糕体
  const qreal cakeW = 176, cakeH = 74;
  const QRectF cake(c.x() - cakeW / 2, c.y() + 6, cakeW, cakeH);
  p.setPen(Qt::NoPen);
  p.setBrush(accent);
  p.drawRoundedRect(cake, 16, 16);
  // 奶油层
  p.setBrush(QColor(255, 255, 255, 52));
  p.drawRoundedRect(QRectF(cake.left(), cake.top(), cake.width(), 22), 12, 12);
  // 底部阴影
  p.setBrush(QColor(0, 0, 0, 26));
  p.drawRoundedRect(QRectF(cake.left(), cake.bottom() - 16, cake.width(), 22), 10, 10);

  // ---- 蜡烛
  const qreal candleW = 10, candleH = 46;
  const QRectF candle(c.x() - candleW / 2, cake.top() - candleH, candleW, candleH);
  // 蜡烛上的螺旋纹：一深一浅两段
  p.setBrush(QColor(theme::Hex(QStringLiteral("text2"))));
  p.drawRoundedRect(candle, 5, 5);
  p.setBrush(QColor(255, 255, 255, 120));
  for (int i = 0; i < 3; ++i) {
    const qreal y = candle.top() + 8 + i * 13;
    p.drawRoundedRect(QRectF(candle.left(), y, candleW, 3.4), 1.7, 1.7);
  }

  // ---- 火苗：flame_ 从 1 到 0；灭掉后画一缕烟
  const QPointF tip(c.x(), candle.top() - 1);
  if (!blown_ && flame_ > 0.02f) {
    const qreal wob = std::sin(wobble_) * 2.6 * flame_;
    const qreal h = 34 * flame_;
    const qreal w = 15 * flame_;
    QPainterPath outer;
    outer.moveTo(tip);
    outer.cubicTo(tip + QPointF(w * 0.9 + wob, -h * 0.42), tip + QPointF(w * 0.5 + wob, -h * 0.80),
                  tip + QPointF(wob * 0.5, -h));
    outer.cubicTo(tip + QPointF(-w * 0.5 + wob, -h * 0.80), tip + QPointF(-w * 0.9 + wob, -h * 0.42),
                  tip);
    p.setBrush(QColor(0xFF, 0x9A, 0x3C, 235));
    p.drawPath(outer);

    QPainterPath inner;
    const qreal h2 = h * 0.56, w2 = w * 0.52;
    inner.moveTo(tip);
    inner.cubicTo(tip + QPointF(w2, -h2 * 0.5), tip + QPointF(w2 * 0.6, -h2 * 0.85),
                  tip + QPointF(0, -h2));
    inner.cubicTo(tip + QPointF(-w2 * 0.6, -h2 * 0.85), tip + QPointF(-w2, -h2 * 0.5), tip);
    p.setBrush(QColor(0xFF, 0xE6, 0x9B, 245));
    p.drawPath(inner);
  } else if (blown_) {
    // 烟：三缕细线，随时间缓慢上升、淡出，靠 frame_ 做循环
    for (int i = 0; i < 3; ++i) {
      const qreal phase = (frame_ * 0.035 + i * 0.33);
      const qreal t = std::fmod(phase, 1.0);
      const qreal y = tip.y() - 10 - t * 54;
      const int alpha = (int)(120 * (1.0 - t));
      if (alpha <= 0) continue;
      p.setPen(QPen(QColor(muted.red(), muted.green(), muted.blue(), alpha), 2.0,
                    Qt::SolidLine, Qt::RoundCap));
      const qreal sx = tip.x() + (i - 1) * 6 + std::sin(t * 6 + i) * 5;
      p.drawLine(QPointF(sx, y), QPointF(sx + std::sin(t * 5 + i * 2) * 4, y - 14));
    }
    // 灭掉的烛芯
    p.setPen(QPen(QColor(muted.red(), muted.green(), muted.blue(), 180), 3, Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(QPointF(tip.x(), tip.y()), QPointF(tip.x(), tip.y() - 5));
  }

  // ---- 吹气进度：蛋糕上方一段细长条，只在按住时出现
  if (!blown_ && (blow_ > 0.01f || holding_)) {
    const qreal barW = 160, barH = 5;
    const QRectF bar(c.x() - barW / 2, cake.bottom() + 26, barW, barH);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(text.red(), text.green(), text.blue(), 40));
    p.drawRoundedRect(bar, barH / 2, barH / 2);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * blow_, bar.height()), barH / 2,
                      barH / 2);
  }
}

}  // namespace wd
