#include "glass.h"

#include <QWidget>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <dwmapi.h>
#include <windows.h>

namespace wd {
namespace glass {

namespace {

// 这些常量在旧 SDK 头里没有，自己定义，避免依赖 Windows SDK 版本
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
// 传这个值等于告诉 DWM「这块颜色别画」，材质才能一路铺到标题栏
constexpr COLORREF kColorNone = 0xFFFFFFFE;  // DWMWA_COLOR_NONE
constexpr int kBackdropAuto = 0;
constexpr int kBackdropNone = 1;
constexpr int kBackdropMica = 2;
constexpr int kBackdropAcrylic = 3;  // 亚克力：模糊窗口背后的一切内容，就是要的毛玻璃
constexpr int kBackdropTabbed = 4;

struct AccentPolicy {
  DWORD state;
  DWORD flags;
  DWORD gradientColor;
  DWORD animationId;
};

struct CompositionAttribData {
  DWORD attrib;
  PVOID data;
  SIZE_T size;
};

enum { kAccentEnableAcrylicBlurBehind = 4 };
enum { kWcaAccentPolicy = 19 };

using PfnSetWindowCompositionAttribute = BOOL(WINAPI*)(HWND, CompositionAttribData*);

bool g_enabled = false;
bool g_light = false;
QWidget* g_target = nullptr;

// 诊断日志：%APPDATA%\WorkDesk\WorkDeskQt\glass.log，设置 WORKDESK_NO_BOOTLOG 时同样沉默
void Log(const char* fmt, ...) {
  if (std::getenv("WORKDESK_NO_BOOTLOG")) return;
  const char* appdata = std::getenv("APPDATA");
  if (!appdata) return;
  wchar_t dir[1024];
  swprintf_s(dir, L"%hs\\WorkDesk\\WorkDeskQt", appdata);
  CreateDirectoryW(dir, nullptr);
  wchar_t path[1200];
  swprintf_s(path, L"%s\\glass.log", dir);
  FILE* f = nullptr;
  if (_wfopen_s(&f, path, L"a") == 0 && f) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
  }
}

// ABGR：AccentPolicy 的 GradientColor 是高字节起 A、B、G、R
DWORD TintArgb(int a, int r, int g, int b) {
  return (DWORD(a) << 24) | (DWORD(b) << 16) | (DWORD(g) << 8) | DWORD(r);
}

bool ApplySystemBackdrop(HWND hwnd) {
  // 让 DWM 把整个客户区当作扩展框架，材质才能铺满整窗
  MARGINS m = {-1, -1, -1, -1};
  const HRESULT frame = DwmExtendFrameIntoClientArea(hwnd, &m);
  int type = kBackdropAcrylic;
  const HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
  Log("systembackdrop: frame=0x%08lX set=0x%08lX type=%d", (unsigned long)frame,
      (unsigned long)hr, type);
  return SUCCEEDED(hr);
}

bool ApplyAccentAcrylic(HWND hwnd, bool light) {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (!user32) return false;
  auto fn = reinterpret_cast<PfnSetWindowCompositionAttribute>(
      GetProcAddress(user32, "SetWindowCompositionAttribute"));
  if (!fn) {
    Log("accent: SetWindowCompositionAttribute 不存在");
    return false;
  }
  AccentPolicy ap{};
  ap.state = kAccentEnableAcrylicBlurBehind;
  ap.flags = 2;  // 边框都画
  ap.gradientColor = light ? TintArgb(0xA6, 0xF6, 0xF8, 0xFC) : TintArgb(0x99, 0x18, 0x1B, 0x1F);
  CompositionAttribData data{};
  data.attrib = kWcaAccentPolicy;
  data.data = &ap;
  data.size = sizeof(ap);
  const BOOL ok = fn(hwnd, &data);
  Log("accent: ok=%d state=%lu color=0x%08lX", (int)ok, (unsigned long)ap.state,
      (unsigned long)ap.gradientColor);
  return ok != 0;
}

}  // namespace

bool Enable(QWidget* w, bool light) {
  if (!w) return false;
  g_target = w;
  g_light = light;
  HWND hwnd = reinterpret_cast<HWND>(w->winId());
  if (!hwnd) return false;

  // 暗色标题栏策略：无边框窗口用不上，但窗口描边色要用主题色，否则边缘一圈白
  BOOL dark = light ? FALSE : TRUE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  Log("enable: hwnd=%p exstyle=0x%08lX layered=%d", (void*)hwnd, (unsigned long)ex,
      (ex & WS_EX_LAYERED) ? 1 : 0);

  bool ok = ApplySystemBackdrop(hwnd);
  if (!ok) ok = ApplyAccentAcrylic(hwnd, light);
  if (ok) {
    // 材质挂上后立刻把标题栏色/边框色交还系统：先前若指定过实色，
    // DWM 会用那块纯色把标题栏的亚克力盖死，只剩客户区透出材质。
    COLORREF none = kColorNone;
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &none, sizeof(none));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &none, sizeof(none));
  }
  g_enabled = ok;
  Log("enable: result=%d", ok ? 1 : 0);
  return ok;
}

void Retint(bool light) {
  g_light = light;
  if (!g_target) return;
  HWND hwnd = reinterpret_cast<HWND>(g_target->winId());
  if (!hwnd) return;
  // 系统材质本身跟随系统深浅，只需要重设一次类型让它重画；accent 路线才需要换 tint
  const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if (!(ex & WS_EX_LAYERED)) {
    ApplySystemBackdrop(hwnd);
  } else {
    ApplyAccentAcrylic(hwnd, light);
  }
}

bool Enabled() { return g_enabled; }

}  // namespace glass
}  // namespace wd
