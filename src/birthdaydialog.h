#pragma once
#include <QColor>
#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

namespace wd {

// 生日彩蛋：主窗口启动时如果发现今天是用户的生日（且今年还没弹过）就弹出来。
// 一盏会呼吸的小蛋糕 + 一段暖心的话，按住空格把火苗吹灭，然后可以许个愿。
class BirthdayDialog : public QDialog {
  Q_OBJECT
 public:
  explicit BirthdayDialog(const QString& userName, QWidget* parent = nullptr);

  // 吹灭之后用户填的愿望（可能为空）
  QString Wish() const { return wish_; }

 protected:
  void paintEvent(QPaintEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  void keyReleaseEvent(QKeyEvent* e) override;
  void showEvent(QShowEvent* e) override;

 private:
  void BuildUi();
  void Tick();          // 30ms 一帧：呼吸、火苗抖动、吹气进度
  void OnBlownOut();    // 吹灭后的收尾
  void GoQuestion();    // 吹灭后切到"许个愿"这一屏

  QWidget* stage_ = nullptr;   // 画蛋糕的那块
  QLabel* title_ = nullptr;
  QLabel* body_ = nullptr;
  QLabel* hint_ = nullptr;
  QLabel* wishLab_ = nullptr;
  QLineEdit* wishEdit_ = nullptr;
  QPushButton* doneBtn_ = nullptr;
  QTimer* timer_ = nullptr;

  QString name_;
  QString wish_;

  // 动画状态
  float flame_ = 1.0f;      // 火苗高度系数 1 -> 0
  float blow_ = 0.0f;       // 吹气进度 0 -> 1
  float breath_ = 0.0f;     // 光晕呼吸相位
  float wobble_ = 0.0f;     // 火苗左右摆动相位
  bool holding_ = false;    // 是否按住空格
  bool blown_ = false;
  int frame_ = 0;
};

}  // namespace wd
