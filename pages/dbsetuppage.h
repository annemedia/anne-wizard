#ifndef DBSETUPPAGE_H
#define DBSETUPPAGE_H
#include "../utils/antorutils.h"
#include "../wizard.h"
#include <QByteArray>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWizardPage>

enum class ImportState { Ready, Initializing, Downloading, Stalled, Extracting, Importing, Complete, Error, None };

class DBSetupPage : public QWizardPage {
  Q_OBJECT

public:
  explicit DBSetupPage(QWidget *parent = nullptr);
  ~DBSetupPage();
  void cleanupPage() override;

protected:
  void initializePage() override;
  bool isComplete() const override;
  bool validatePage() override;

private slots:
  void togglePasswordVisibility(bool show);
  void performSetupOperations();
  void copyPassword();
  void confirmAndImport();
  void performSnapshotDownload();
  void retrySnapshotDownload();
  void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
  void onAntorProgress(const QString &nid, int completed, int total);
  void onDownloadStalled();

private:
  Wizard *m_wiz;
  std::shared_ptr<std::atomic<bool>> currentImportCancelled;
  void prepImport(const QString &rootPass);
  void getPlatformNid(const QString &cacheStr);
  void startDatabaseImport(const QString &rootPass, std::shared_ptr<QTemporaryDir> tempDirPtr, const QString &sqlPath);
  void getReady();
  void skipImport();
  QString cacheDirStr;
  ImportState currentState = ImportState::Initializing;
  QLabel *statusLabel = nullptr;
  QProgressBar *progressBar = nullptr;
  QLabel *noteLabel = nullptr;
  QLineEdit *rootPassEdit = nullptr;
  QPushButton *eyeButton = nullptr;
  QPushButton *copyButton = nullptr;
  QPushButton *confirmButton = nullptr;
  QPushButton *skipButton = nullptr;

  AntorUtils *antorUtils = nullptr;
  QString currentNid;
  QString m_localZipPath;
  bool downloadReady = false;
  bool dbSetupDone = false;
  bool forceComplete = false;
  bool isFresh = false;
  QString dbName = "annedb";
  QByteArray zipData;
};

#endif