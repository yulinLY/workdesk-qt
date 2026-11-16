#pragma once
#include <QDialog>

#include "config.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;

namespace wd {

class Probe;

// 设置：多套 AI 配置（接口 + 性格），可新建 / 复制 / 重命名 / 删除 / 切换，
// 「保存并启用」立即写回 %APPDATA%\WorkDesk\config.json 并生效。
class SettingsDialog : public QDialog {
  Q_OBJECT
 public:
  explicit SettingsDialog(Config* cfg, QWidget* parent = nullptr);
  ~SettingsDialog() override;

 private:
  void BuildUi();

  void ReloadProfileList(int select);
  void LoadFormFromProfile(int idx);
  void SaveFormToProfile(int idx);
  void SwitchProfile(int idx);

  void OnNewProfile();
  void OnDuplicateProfile();
  void OnRenameProfile();
  void OnDeleteProfile();

  void ApplyProviderPreset(int idx);
  void TestConnection();
  void PollProbe();
  bool Commit();
  void SetState(const QString& text, bool ok);

  Config* cfg_ = nullptr;

  QComboBox* profileBox_ = nullptr;
  QPushButton* newBtn_ = nullptr;
  QPushButton* dupBtn_ = nullptr;
  QPushButton* renameBtn_ = nullptr;
  QPushButton* delBtn_ = nullptr;

  QComboBox* provider_ = nullptr;
  QLineEdit* baseUrl_ = nullptr;
  QLineEdit* apiKey_ = nullptr;
  QLineEdit* model_ = nullptr;
  QDoubleSpinBox* temp_ = nullptr;

  QComboBox* persona_ = nullptr;
  QLineEdit* tone_ = nullptr;
  QComboBox* verbosity_ = nullptr;
  QLineEdit* address_ = nullptr;
  QCheckBox* emoji_ = nullptr;
  QCheckBox* markdown_ = nullptr;
  QPlainTextEdit* extra_ = nullptr;
  QPlainTextEdit* sysPrompt_ = nullptr;

  QLabel* probeLab_ = nullptr;
  QLabel* stateLab_ = nullptr;
  QPushButton* testBtn_ = nullptr;

  class Probe* probe_ = nullptr;
  QTimer* probeTimer_ = nullptr;
};

}  // namespace wd
