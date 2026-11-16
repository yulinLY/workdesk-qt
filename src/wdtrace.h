#pragma once
// 临时诊断打点：统一写 %APPDATA%\WorkDesk\WorkDeskQt\boot.log
#include <cstdio>
#include <cstdlib>

namespace wd {
inline void Trace(const char* s) {
  if (std::getenv("WORKDESK_NO_TRACE")) return;  // A/B 开关：设为 1 则不写日志
  const char* appdata = std::getenv("APPDATA");
  if (!appdata) return;
  char path[1024];
  snprintf(path, sizeof(path), "%s\\WorkDesk\\WorkDeskQt\\boot.log", appdata);
  FILE* f = nullptr;
  if (fopen_s(&f, path, "a") == 0 && f) {
    fprintf(f, "%s\n", s);
    fclose(f);
  }
}
}  // namespace wd
