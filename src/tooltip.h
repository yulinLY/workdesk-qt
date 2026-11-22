#pragma once

namespace wd {

// 全局提示气泡：接管所有控件的 QEvent::ToolTip，用自绘气泡代替原生 QToolTip。
//
// 为什么不用原生气泡：Windows 11 样式下 QToolTip 是顶层工具窗，底板按系统深色材质画，
// 文字却取我们亮色调色板的深色文字，于是亮色模式出现「黑底黑字」；
// QSS 里的 QToolTip 规则和 QToolTip::setPalette 都盖不住它。
// 自绘气泡每次绘制时现取 QApplication::palette()，主题一换颜色立刻跟上。
void InstallTooltips();

}  // namespace wd