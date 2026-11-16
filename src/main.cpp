#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QStringList>
#include <windows.h>
#include <cstdio>
#include <cstdlib>

#include "mainwindow.h"
#include "theme.h"

namespace {

// 启动日志：写到 %APPDATA%\WorkDesk\WorkDeskQt\boot.log，便于排查启动崩溃。
// 设置环境变量 WORKDESK_NO_BOOTLOG=1 可关闭。
void Boot(const char* s) {
  if (std::getenv("WORKDESK_NO_BOOTLOG")) return;
  const char* appdata = std::getenv("APPDATA");
  if (!appdata) return;
  char path[1024];
  snprintf(path, sizeof(path), "%s\\WorkDesk\\WorkDeskQt", appdata);
  CreateDirectoryA(path, nullptr);
  snprintf(path, sizeof(path), "%s\\WorkDesk\\WorkDeskQt\\boot.log", appdata);
  FILE* f = nullptr;
  if (fopen_s(&f, path, "a") == 0 && f) {
    fprintf(f, "%s\n", s);
    fclose(f);
  }
}

// 系统调色板默认是浅色，任何没被 QSS 命中的控件（滚动区 viewport、原生对话框等）
// 都会露出 #F3F3F3 白底。这里整体换成深色调色板兜底。
QPalette DarkPalette() {
  QPalette p;
  const QColor win(0x1B, 0x1E, 0x22), base(0x1E, 0x21, 0x26), card(0x23, 0x26, 0x2B);
  const QColor text(0xE6, 0xE9, 0xED), muted(0x7C, 0x84, 0x8F), btn(0x2C, 0x31, 0x38);
  const QColor hl(0x3B, 0x5C, 0x99);
  p.setColor(QPalette::Window, win);
  p.setColor(QPalette::WindowText, text);
  p.setColor(QPalette::Base, base);
  p.setColor(QPalette::AlternateBase, card);
  p.setColor(QPalette::Text, text);
  p.setColor(QPalette::Button, btn);
  p.setColor(QPalette::ButtonText, QColor(0xD7, 0xDC, 0xE2));
  p.setColor(QPalette::BrightText, QColor(0xFF, 0x6B, 0x6B));
  p.setColor(QPalette::Highlight, hl);
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::ToolTipBase, btn);
  p.setColor(QPalette::ToolTipText, text);
  p.setColor(QPalette::PlaceholderText, muted);
  p.setColor(QPalette::Light, QColor(0x2F, 0x34, 0x3B));
  p.setColor(QPalette::Mid, QColor(0x3A, 0x41, 0x4A));
  p.setColor(QPalette::Dark, QColor(0x15, 0x18, 0x1B));
  p.setColor(QPalette::Shadow, QColor(0x10, 0x12, 0x15));
  return p;
}

// 字体全部取自系统已安装字体库：按优先级挑第一个系统里真实存在的族，
// 不打包任何字体文件，跟随系统字体设置走。
QFont PickSystemUiFont() {
  const QStringList prefer = {
      QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei"),
      QStringLiteral("Segoe UI"),          QStringLiteral("SimSun"),
  };
  const QStringList installed = QFontDatabase::families();
  QFont f = QApplication::font();
  for (const QString& name : prefer) {
    if (installed.contains(name, Qt::CaseInsensitive)) {
      f.setFamily(name);
      break;
    }
  }
  f.setPointSizeF(9.5);
  return f;
}

}  // namespace

int main(int argc, char* argv[]) {
  Boot("-- start");
  QApplication app(argc, argv);
  Boot("qt app ok");
  app.setApplicationName(QStringLiteral("WorkDesk"));
  app.setApplicationDisplayName(QStringLiteral("WorkDesk"));
  app.setOrganizationName(QStringLiteral("WorkDesk"));

  // 窗口图标 / 任务栏图标：D:\画图\ico\WorkDesk.ico 已编进 qrc，exe 资源段另有一份
  app.setWindowIcon(QIcon(QStringLiteral(":/assets/WorkDesk.ico")));

  app.setFont(PickSystemUiFont());
  app.setPalette(DarkPalette());
  Boot("font ok");
  app.setStyleSheet(QString::fromUtf8(wd::theme::kQss));
  Boot("qss ok");

  wd::MainWindow win;
  Boot("mainwindow ok");
  win.show();
  Boot("show ok");
  const int r = app.exec();
  Boot("exit");
  return r;
}
