#ifndef JAVAPAGE_H
#define JAVAPAGE_H
#include "../utils/javautils.h"
#include "../wizard.h"
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPalette>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QString>
#include <QTime>
#include <QTimer>
#include <QWizardPage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
struct OSInfo;
struct PkgQuery;
enum class PkgManagerType;

enum InstallMode { InstallNew, KeepExisting, InstallAdditional, SwitchDefault };
struct JavaPackageInfo;

class JavaPage : public QWizardPage {
  Q_OBJECT

public:
  explicit JavaPage(QWidget *parent = nullptr);
  ~JavaPage();
protected:
  void initializePage() override;
  bool isComplete() const override;
  bool validatePage() override;

private slots:
  void installJava();
  void onInstallFinished(bool success);
  void readInstallOutput();
  void continueJavaInstallation(QPalette pal, InstallMode mode);
  void onInstallPackageManager();
  void onJavaVersionChanged(int index);

private:
 Wizard *m_wiz;
 QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *currentReply = nullptr;
    QFile downloadFile;
    QString msiTempPath;
  QComboBox *javaVersionComboBox;
  QGroupBox *installModeGroup;
  QRadioButton *installNewRadio;
  QRadioButton *switchDefaultRadio;
  QRadioButton *installAdditionalRadio;
  QLabel *recommendationLabel;
  QLabel *packageManagerLabel;
  QPushButton *installPackageManagerButton;
  QList<JavaPackageInfo> availablePackages;
  bool packageManagerAvailable;
  bool packageManagerInstalling;
  InstallMode lastInstallMode;
  void updateJavaInfoFromWizard();
  void onWizardJavaDetectionComplete();
  void refreshJavaInfo(bool updateLabel);
  void setupInstallModes();
  void switchDefaultJava();
  QString getSelectedPackageName();
  InstallMode getSelectedInstallMode();
  void updateUIAfterInstall(const QString &installedPackage, bool setAsDefault);
  void updateDefaultJavaMarker(const QString &defaultPackage);

  QString cachedJavaPackage;
  QLabel *statusLabel;
  QProgressBar *progressBar;
  QPushButton *installButton;
  bool javaInstalled = false;
  QString detectedJavaVersion = "Java";

  QProcess *installProcess = nullptr;

  QTimer *progressTimer = nullptr;
  QTimer *statusUpdateTimer = nullptr;
  QString accumulatedOutput = "";
  bool inDownloadPhase = false;
  double downloadProgress = 0.0;
  int downloadLineCount = 0;
  double totalDownloadBytes = 0;
  int downloadFileCount = 0;
  int completedFiles = 0;
  QString lastProcessedLine = "";
  QString lastStatusText = "";
  int pollAttempts = 0;
  bool forceReinstallNext = false;
  void handleWindowsInstall(const QString &packageName, InstallMode mode);
  void handleMacInstall(const QString &packageName, InstallMode mode);
  void showManualMacInstructions();
  qint64 packageSizeBytes = 0;
    QTime downloadStartTime;
};

#endif
