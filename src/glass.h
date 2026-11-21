#pragma once

class QWidget;

namespace wd {
namespace glass {

// 给无边框顶层窗口挂上系统毛玻璃：Win11 22H2+ 用 DWM 系统材质（亚克力），
// 不支持时退回 SetWindowCompositionAttribute 的 ACCENT_ENABLE_ACRYLICBLURBEHIND。
// 调用前窗口必须已设置 WA_TranslucentBackground 并 show 过（要有真实 HWND）。
// 返回 false 表示系统不给面子，此时界面按半透明深色兜底。
bool Enable(QWidget* w, bool light);

// 主题切换后重新上色（材质自身带 tint，深色/亮色要分别给）
void Retint(bool light);

// 材质是否真的挂上了；没挂上时界面不该依赖模糊
bool Enabled();

}  // namespace glass
}  // namespace wd
