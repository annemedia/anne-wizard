#ifndef MARIADBPAGE_H
#define MARIADBPAGE_H
#include "../wizard.h"
#include <QLabel>
#include <QProcess>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QWizardPage>

struct OSInfo;
struct PkgQuery;
enum class PkgManagerType;

class MariaDBPage : public QWizardPage {
  Q_OBJECT

public:
  explicit MariaDBPage(QWidget *parent = nullptr);

protected:
  void initializePage() override;
  bool isComplete() const override;
  bool validatePage() override;

private slots:
  void installMariaDB();
  void readInstallOutput();
  void onInstallFinished(bool success);
  void uninstallMariaDB();
  void continueMariaDBInstallation();
private:
  Wizard *m_wiz;
  QNetworkAccessManager *m_nam = nullptr;
  static bool isMariaDBVersionSupported(const QString &versionOutput);
  bool doesRepoHaveSupportedVersion(const OSInfo &info);
  bool runMariaDBRepoSetup(const OSInfo &info);
  bool runRepoSetupScript(const QString &scriptPath);
  QString findWindowsMariaDBPath(); 
  bool createWindowsMariaDBService(const QString &mariadbPath);
  void handleWindowsInstall();
  void handleWindowsInstallFinished(bool installSuccess);
  void processWingetQueryResult(const QString &output, const QString &packageId);
  bool configureWindowsMariaDB();
  void handleMacInstall();
  void showManualMacInstructions();
  bool verifyMariaDBInstalled();
  bool addMariaDBToSystemPath(const QString &binDir);
  void updateCurrentProcessPath(const QString &binDir);
  void handleWindowsUninstall();
  bool performWindowsUninstall();
  void onWindowsUninstallFinished(bool success);
  QString extractMariaDBProductCode();
  void handleMacUninstall();
  bool verifyMacOSMariaDBInstalled();
  QLabel *statusLabel = nullptr;
  QPushButton *installButton = nullptr;
  QProgressBar *progressBar = nullptr;
    QNetworkReply *currentReply = nullptr;
  QTimer *statusUpdateTimer = nullptr;
  QTimer *progressTimer = nullptr;
  QPropertyAnimation *progressAnimation = nullptr;
  QProcess *installProcess = nullptr;
  QString msiTempPath;
  QFile downloadFile;
  bool mariaDBReady = false;
  bool isFreshInstall = false;
  bool isLinux = false;
  int pollAttempts = 0;
  bool inDownloadPhase = false;
  QString accumulatedOutput = "";
  QString lastStatusText;
  double downloadProgress = 0.0;
  int downloadLineCount = 0;
  int downloadFileCount = 0;
  int completedFiles = 0;
  double totalDownloadBytes = 0.0;
  QString lastProcessedLine;
  QPushButton *uninstallButton = nullptr;
};

#endif
    