#pragma once

#include <QImage>
#include <QColor>

class QPainter;
class QRect;
class QWidget;

namespace wd {
namespace frost {

// 自绘磨砂背景。
//
// 之前用 DWM 亚克力 + WA_TranslucentBackground 的做法有两个硬伤：
//   1) 窗口自身像素带 alpha，拖动/缩放时系统来不及重新合成，整窗会闪白；
//   2) 子控件（dock 栏展开收回）动画期间父窗口的旧像素没被覆盖，留下残影。
// 现在改成窗口完全不动系统合成：开机时抓一张桌面快照做重模糊，窗口不透明，
// 由 paintEvent 按自己所在的屏幕坐标把对应那一块铺回背景，再叠一层主题色遮罩。
// 观感就是玻璃后面透出模糊的桌面，但拖动、缩放、子控件动画全走普通重绘，不会白屏也不会残影；
// 桌面壁纸本身完全没被改动，只是被"拍了一张照片"当背景用。
//
// 必须在主窗口 show 之前调用 Capture()，否则快照里会拍进自己的窗口。
void Capture();

// 快照是否可用；不可用时界面退回主题实色底。
bool Ready();

// 按窗口当前在屏幕上的位置，从模糊快照里取对应的一块铺到局部坐标 rect 上。
void Paint(QWidget* w, QPainter& p, const QRect& rect);

// 叠一层磨砂颗粒（静态噪声平铺）。壁纸是纯色时，玻璃"砂面"靠它体现。
void PaintGrain(QPainter& p, const QRect& rect);

// 桌面内容变化后（换壁纸、切换全屏应用）重新抓取并模糊；可在窗口隐藏后调用。
void Recapture();

}  // namespace frost
}  // namespace wd
