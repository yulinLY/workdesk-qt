#pragma once

namespace wd {

// 全局 Markdown 工具条：
//   - 任何可编辑输入框（记事本、聊天输入、查找框等）里按 Ctrl+Alt 弹出
//   - 弹在光标旁，按 1~9 或点条目即套用标题 / 粗体 / 斜体 / 列表等 Markdown 格式
//   - 设置对话框（以及其他 QDialog、标了 mdNoTools 属性的控件）内不弹
void InstallMarkdownTools();

}  // namespace wd
