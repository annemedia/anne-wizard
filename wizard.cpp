#include "wizard.h"
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QTextStream>
#include <QtConcurrent>
#include <functional>
#ifdef Q_OS_UNIX
#include <sys/types.h>
#include <unistd.h>
#endif

#include "pages/configpage.h"
#include "pages/dbsetuppage.h"
#include "pages/downloadpage.h"
#include "pages/finalpage.h"
#include "pages/intropage.h"
#include "pages/javapage.h"
#include "pages/licensepage.h"
#include "pages/mariadbpage.h"
#include "pages/minerconfigpage.h"
#include "pages/netconfigpage.h"

Wizard::Wizard(QWidget *parent) : QWizard(parent), progressDialog(nullptr), m_osInfo(detectOSInfo()) {

  qDebug() << "DEBUG: Constructor starting, OS info loaded";
  setWindowTitle("ANNE Wizard");
  setWizardStyle(QWizard::ModernStyle);
  setOption(QWizard::NoBackButtonOnStartPage, true);
  setOption(QWizard::NoCancelButton, false);

  addPage(new IntroPage);
  addPage(new LicensePage);

  addPage(new JavaPage);
  addPage(new MariaDBPage);
  addPage(new DBSetupPage);
  addPage(new DownloadPage);
  addPage(new NetConfigPage);
  addPage(new ConfigPage);
  addPage(new MinerConfigPage);
  addPage(new FinalPage);
  setFixedSize(580, 620);
  connect(this, &QWizard::customButtonClicked, this, &Wizard::handleCustomButtonClicked);
  connect(this, &QWizard::currentIdChanged, this, &Wizard::onCurrentIdChanged);

  startJavaDetectionAsync();
}

void Wizard::handleCustomButtonClicked(int id) { Q_UNUSED(id); }

void Wizard::showProgress(const QString &title, std::function<void(QProgressDialog *)> task) {
  progressDialog = new QProgressDialog(title, QString(), 0, 0, this);
  progressDialog->setWindowModality(Qt::WindowModal);
  progressDialog->setCancelButton(nullptr);
  progressDialog->show();
  task(progressDialog);
  progressDialog->close();
  delete progressDialog;
  progressDialog = nullptr;
}

bool Wizard::isRunningElevated() const {
#ifdef Q_OS_WIN
  BOOL isAdmin = FALSE;
  SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
  PSID adminGroup;
  if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
    CheckTokenMembership(nullptr, adminGroup, &isAdmin);
    FreeSid(adminGroup);
  }
  return isAdmin;
#else
  return (geteuid() == 0);
#endif
}

bool Wizard::isNodeConfigEncrypted() const {
  if (currentId() == DownloadPageId) {
    QString configPath = installDir + "/annode/conf/node.properties";
    qDebug() << "isNodeConfigEncrypted: configPath " << configPath;
    QFile file(configPath);

    if (!file.exists()) {
      qDebug() << "isNodeConfigEncrypted: file doesn't exist " << configPath;
      const_cast<Wizard *>(this)->nodeConfigIsEncrypted = false;
      return false;
    }

    if (installDir.isEmpty()) {
      return false;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      return false;
    }

    QString content = QTextStream(&file).readAll();

    bool hasPlainKeys = content.contains("anne.nodeAccountId") || content.contains("anne.nodeSecret") || content.contains("anne.nodeURI");

    qDebug() << "isNodeConfigEncrypted: file hasPlainKeys " << hasPlainKeys;
    const_cast<Wizard *>(this)->nodeConfigIsEncrypted = !hasPlainKeys;

    qDebug() << "Encrypted config check:" << configPath << "→ encrypted =" << (!hasPlainKeys);

    return !hasPlainKeys;
  }

  return false;
}

void Wizard::onCurrentIdChanged(int id) {
  if (id == FinalPageId) {
    button(QWizard::NextButton)->setVisible(false);
    button(QWizard::FinishButton)->setVisible(true);
    button(QWizard::FinishButton)->setEnabled(true);
  }
}

int Wizard::nextId() const {
  qDebug() << "THE DownloadPageId ID IS: " << DownloadPageId;
  qDebug() << "THE PAGE ID IS: " << currentId();
  qDebug() << "isNodeConfigEncrypted: " << isNodeConfigEncrypted();

  if (isNodeConfigEncrypted() && currentId() >= DownloadPageId) {
    return FinalPageId;
  }

  switch (currentId()) {
  case IntroPageId:
    return WelcomePageId;
  case WelcomePageId:
    return JavaPageId;
  case JavaPageId:
    return MariaDBPageId;
  case MariaDBPageId:
    return DBSetupPageId;
  case DBSetupPageId:
    return DownloadPageId;
  case DownloadPageId:
    return NetConfigPageId;
  case NetConfigPageId:
    return ConfigPageId;
  case ConfigPageId:

    return areExtrasInstalled() ? MinerConfigPageId : FinalPageId;
  case MinerConfigPageId:
    return FinalPageId;
  default:
    return -1;
  }
}

bool Wizard::areExtrasInstalled() const {

  QWizardPage *downloadPage = page(DownloadPageId);
  if (!downloadPage) {
    qDebug() << "Download page not found";
    return false;
  }

  bool installExtras = field("installExtras").toBool();
  qDebug() << "Extras installation field value:" << installExtras;

  if (!installExtras) {
    return false;
  }

  QString minerDir = installDir + QDir::separator() + "anneminer";
  QDir minerDirectory(minerDir);
  bool minerExists = minerDirectory.exists();

  qDebug() << "Miner directory exists:" << minerExists << "at:" << minerDir;

  return minerExists;
}

void Wizard::resetSkippedPagesIfNeeded() {
  if (!isNodeConfigEncrypted()) {
    QWizardPage *netPage = page(NetConfigPageId);
    QWizardPage *configPage = page(ConfigPageId);
    QWizardPage *minerConfigPage = page(MinerConfigPageId);

    if (netPage) {
      configPage->setFinalPage(false);
      emit netPage->completeChanged();
    }

    if (configPage) {
      configPage->setFinalPage(false);
      emit configPage->completeChanged();
    }

    if (minerConfigPage) {

      bool extrasInstalled = areExtrasInstalled();
      minerConfigPage->setFinalPage(!extrasInstalled);
      emit minerConfigPage->completeChanged();
      qDebug() << "MinerConfigPage final page status:" << !extrasInstalled;
    }

    qDebug() << "Reset cached state for Config/NetConfig pages (now showing them)";
  }
}

void Wizard::debugWindowPosition() {
  qDebug() << "=== Window Position Debug ===";
  qDebug() << "Window geometry:" << this->geometry();
  qDebug() << "Window frame geometry:" << this->frameGeometry();
  qDebug() << "Window size:" << this->size();
  qDebug() << "Window pos:" << this->pos();
  qDebug() << "Is visible:" << this->isVisible();
  qDebug() << "Is window:" << this->isWindow();

  QScreen *screen = this->screen();
  if (screen) {
    qDebug() << "Screen name:" << screen->name();
    qDebug() << "Screen geometry:" << screen->geometry();
    qDebug() << "Screen available:" << screen->availableGeometry();
  }

  auto screens = QGuiApplication::screens();
  for (int i = 0; i < screens.size(); ++i) {
    qDebug() << "Screen" << i << ":" << screens[i]->name() << "geometry:" << screens[i]->availableGeometry();
  }

  qDebug() << "Cursor position:" << QCursor::pos();
  qDebug() << "=================================";
}

void Wizard::startJavaDetectionAsync() {
  m_javaDetectionComplete = false;
  connect(&m_javaDetectionWatcher, &QFutureWatcher<void>::finished, this, &Wizard::onJavaDetectionFinished);

  m_javaDetectionWatcher.setFuture(QtConcurrent::run([this]() {
    try {
      m_javaCheckResult = JavaUtils::checkSystemJava();
      m_javaDetected = m_javaCheckResult.isCompleteJDK && m_javaCheckResult.majorVersion >= 11;
      m_javaVersion = m_javaCheckResult.version;
      m_availablePackages = JavaUtils::getAvailableJavaPackages(m_osInfo.pkgType);
    } catch (const std::exception &e) {
      qWarning() << "Java detection failed:" << e.what();

      m_javaDetected = false;
      m_javaVersion = "Detection failed";
      m_availablePackages.clear();
    }

    m_javaDetectionComplete = true;
  }));
}

void Wizard::onJavaDetectionFinished() { emit javaDetectionComplete(); }

bool Wizard::confirmExit() {
  QMessageBox::StandardButton reply;
  reply = QMessageBox::question(this, "Confirm Exit", "Are you sure you want to exit the ANNE Wizard?\nAny current page progress may be lost.",
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  return (reply == QMessageBox::Yes);
}

void Wizard::reject() {
  if (confirmExit()) {
    QWizard::reject();
  }
}

void Wizard::closeEvent(QCloseEvent *event) {
  if (confirmExit()) {
    event->accept();
    QWizard::reject();
  } else {
    event->ignore();
  }
}
