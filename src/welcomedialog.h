#pragma once
#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QDoubleSpinBox;
class QStackedWidget;

namespace wd {

struct Config;
class Probe;

// 新用户首次启动的欢迎向导：一步步带用户填好 AI 模型，最后填生日。
// 走完后往 config.json 写 onboard_done=true，之后不再弹。
// 点「跳过」也会写这个标记，只是不保存已填内容。
class WelcomeDialog : public QDialog {
  Q_OBJECT
 public:
  explicit WelcomeDialog(Config* cfg, QWidget* parent = nullptr);
  ~WelcomeDialog() override;

  // 用户是否走完了向导（false = 跳过了）
  bool Finished() const { return finished_; }

 private:
  void BuildUi();
  void GoTo(int page);
  void RefreshNav();
  bool CanLeavePage() const;
  void DoTest();
  void PollProbe();
  void Finish(bool save);
  void ApplyPreset(int idx);

  Config* cfg_ = nullptr;
  bool finished_ = false;
  int page_ = 0;

  QStackedWidget* stack_ = nullptr;
  QLabel* stepLab_ = nullptr;
  QPushButton* backBtn_ = nullptr;
  QPushButton* nextBtn_ = nullptr;
  QPushButton* skipBtn_ = nullptr;

  // 第 1 页：怎么填
  // 第 2 页：服务商 / 地址 / 模型 / Key
  QComboBox* provider_ = nullptr;
  QLineEdit* baseUrl_ = nullptr;
  QLineEdit* model_ = nullptr;
  QLineEdit* apiKey_ = nullptr;
  QDoubleSpinBox* temp_ = nullptr;
  QLabel* probeLab_ = nullptr;
  QPushButton* testBtn_ = nullptr;
  Probe* probe_ = nullptr;
  class QTimer* probeTimer_ = nullptr;

  // 第 3 页：性格
  QComboBox* persona_ = nullptr;
  QComboBox* verbosity_ = nullptr;
  QLineEdit* address_ = nullptr;

  // 第 4 页：生日
  QComboBox* monthBox_ = nullptr;
  QComboBox* dayBox_ = nullptr;
  QLabel* birthdayTip_ = nullptr;
};

}  // namespace wd
