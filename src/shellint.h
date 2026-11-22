#pragma once
#include <QString>

namespace wd {

// 让 WorkDesk 出现在「控制面板 / 程序和功能」里。
// 绿色程序本来没有安装过程，系统列表里自然找不到——这里补的就是那条登记：
// HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\WorkDesk，
// 写清显示名、版本、安装位置、卸载命令（Uninstall.exe）与大小，
// 系统和第三方卸载工具（Geek、CCleaner 之类）就能认出它并直接调用卸载程序。
//
// 不想被登记（纯绿色带走）时，下面任一条即可：
//   - 在 exe 旁边放一个空文件 portable.txt
//   - 设环境变量 WORKDESK_NO_REGISTRY=1
bool RegisterUninstallEntry();

// 卸载程序调用：删掉上面那条登记，顺带清掉程序自己的 HKCU\Software\WorkDesk
void UnregisterUninstallEntry();

}  // namespace wd