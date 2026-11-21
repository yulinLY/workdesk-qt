#include "frost.h"

#include <QBrush>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QRandomGenerator>
#include <QRect>
#include <QScreen>
#include <QWidget>
#include <algorithm>

namespace wd {
namespace frost {

namespace {

QImage g_blur;              // 已模糊的桌面快照（按设备像素存，dpr 已归一到 1）
QImage g_noise;             // 磨砂颗粒纹理，平铺用；壁纸是纯色时全靠它体现"砂面"
qreal g_dpr = 1.0;          // 抓图时屏幕的缩放比
QColor g_edge(22, 24, 28);  // 快照边缘色：窗口超出快照范围时兜底，避免黑边

// 缩小再放大 = 低成本强低通，比真高斯便宜得多，磨砂观感足够
QImage Shrink(const QImage& src, int factor) {
  const int w = std::max(1, src.width() / factor);
  const int h = std::max(1, src.height() / factor);
  return src.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void Stretch(QImage& img, const QSize& size) {
  img = img.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage MakeBlur(const QImage& base) {
  QImage img = Shrink(base, 18);   // 先压掉高频细节
  img = Shrink(img, 4);            // 再压一遍，斑块消失
  Stretch(img, base.size());       // 放大回来，得到柔和的大色块
  img = Shrink(img, 3);
  Stretch(img, base.size());
  QImage out = img.convertToFormat(QImage::Format_RGB32);
  out.setDevicePixelRatio(1.0);
  return out;
}

// 磨砂颗粒：固定种子的静态噪声瓦片，只在启动时生成一次，绘制时平铺。
// 壁纸本身是纯浅色时，玻璃之所以"看得出是玻璃"，靠的就是这层极细的砂面。
//
// 两个必须守住的点：
//   1) 只能用非预乘格式 Format_ARGB32。之前写成 ARGB32_Premultiplied 却塞 qRgba(v,v,v,14)
//      这种非预乘值，RGB 通道比 alpha 大一个量级，Qt 按预乘解读后每个像素都近乎满亮 ——
//      整窗就变成电视雪花。这是上一版花屏的真正原因，跟壁纸和显卡无关。
//   2) 颗粒要淡但不能没有：预乘正确之后，全像素覆盖 + alpha 11 落地只有 8 个灰阶左右的
//      细微抖动，是砂面；砍到稀疏落砂反而看不出玻璃感。上一版之所以像雪花，纯粹是预乘错，
//      不是密度问题。
QImage MakeNoise() {
  constexpr int kTile = 128;
  QImage tile(kTile, kTile, QImage::Format_ARGB32);
  tile.fill(Qt::transparent);
  QRandomGenerator rng(0x5EED5EEDu);
  for (int y = 0; y < kTile; ++y) {
    for (int x = 0; x < kTile; ++x) {
      const int v = 40 + int(rng.bounded(191));  // 40~230，避开纯黑纯白
      tile.setPixel(x, y, qRgba(v, v, v, 11));
    }
  }
  return tile;
}

}  // namespace

void Capture() {
  QScreen* screen = QGuiApplication::primaryScreen();
  if (!screen) return;
  const QPixmap shot = screen->grabWindow(0);
  if (shot.isNull()) return;

  QImage base = shot.toImage();
  if (base.isNull()) return;
  g_dpr = base.devicePixelRatio() > 0.0 ? base.devicePixelRatio() : 1.0;
  base = base.convertToFormat(QImage::Format_RGB32);
  base.setDevicePixelRatio(1.0);

  // 边缘兜底色取全图平均色，窗口略微超出屏幕时不会出现明显硬边
  const QImage avg = base.scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  if (!avg.isNull()) g_edge = QColor(avg.pixel(0, 0));

  g_blur = MakeBlur(base);
  if (g_noise.isNull()) g_noise = MakeNoise();
}

void Recapture() {
  Capture();
}

bool Ready() {
  return !g_blur.isNull();
}

void Paint(QWidget* w, QPainter& p, const QRect& rect) {
  if (!w || g_blur.isNull() || rect.isEmpty()) return;

  // 局部坐标 -> 屏幕坐标 -> 快照像素坐标
  const QPoint gp = w->mapToGlobal(rect.topLeft());
  const qreal dpr = g_dpr > 0.0 ? g_dpr : 1.0;
  const QRect src(int(gp.x() * dpr), int(gp.y() * dpr),
                  int(rect.width() * dpr), int(rect.height() * dpr));

  const QRect bounds = g_blur.rect();
  if (bounds.contains(src)) {
    p.drawImage(rect, g_blur, src);
    return;
  }

  // 越界部分：先铺边缘色，再画交集，保证没有黑边
  p.fillRect(rect, g_edge);
  const QRect hit = src.intersected(bounds);
  if (hit.isEmpty()) return;
  const QRect target(rect.topLeft() + QPoint(int((hit.x() - src.x()) / dpr), int((hit.y() - src.y()) / dpr)),
                     QSize(int(hit.width() / dpr), int(hit.height() / dpr)));
  p.drawImage(target, g_blur, hit);
}

void PaintGrain(QPainter& p, const QRect& rect) {
  if (g_noise.isNull() || rect.isEmpty()) return;
  // 纹理锚定到控件原点，局部重绘时颗粒位置不会跟着漂
  p.save();
  p.setRenderHint(QPainter::SmoothPixmapTransform, false);
  p.setBrushOrigin(rect.topLeft());
  p.setPen(Qt::NoPen);
  p.setBrush(QBrush(g_noise));
  p.drawRect(rect);
  p.restore();
}

}  // namespace frost
}  // namespace wd
