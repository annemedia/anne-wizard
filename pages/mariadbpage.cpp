#include "mariadbpage.h"
#include "../utils/concurrent.h"
#include "../utils/dbmanager.h"
#include "../utils/mariautils.h"
#include "../utils/netutils.h"
#include "../utils/utils.h"
#include "../utils/systemutils.h"
#include <QApplication>
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QIODevice>
#include <QMessageBox>
#include <QMetaObject>
#include <QNetworkRequest>
#include <QPalette>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <QIODevice>

MariaDBPage::MariaDBPage(QWidget *parent) : QWizardPage(parent) {
  setTitle("MariaDB Setup");
  setSubTitle("MariaDB 10.5+ is required. Wiz will install and secure it "
              "automatically.");

  statusLabel = new QLabel("Initializing...");
  statusLabel->setWordWrap(true);

  if (!m_nam) {
    m_nam = new QNetworkAccessManager(this);
  }

  Utils::updateStatus(statusLabel, "Checking MariaDB...", StatusType::Progress);
  progressBar = new QProgressBar();
  Utils::setProgressColor(progressBar, false);
  progressBar->setRange(0, 100);
  progressBar->setValue(0);
  progressBar->setVisible(false);

  installButton = new QPushButton("Install MariaDB 10.5+");
  installButton->setVisible(false);

  uninstallButton = new QPushButton("Uninstall MariaDB (purges all databases)");
  uninstallButton->setVisible(false);

  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->addWidget(statusLabel);
  mainLayout->addWidget(progressBar);
  mainLayout->addWidget(installButton, 0, Qt::AlignCenter);
  mainLayout->addWidget(uninstallButton, 0, Qt::AlignCenter);

  connect(installButton, &QPushButton::clicked, this, &MariaDBPage::installMariaDB);
  connect(uninstallButton, &QPushButton::clicked, this, &MariaDBPage::uninstallMariaDB);

  progressAnimation = new QPropertyAnimation(progressBar, "value", this);
  progressAnimation->setDuration(400);
  progressAnimation->setEasingCurve(QEasingCurve::OutQuad);

  statusUpdateTimer = new QTimer(this);
  statusUpdateTimer->setInterval(500);

  progressTimer = new QTimer(this);
  progressTimer->setInterval(3000);
  progressTimer->setSingleShot(false);

  connect(progressTimer, &QTimer::timeout, this, [this, lastValue = 0]() mutable {
    int current = progressBar->value();
    static int stuckCount = 0;

    if (current == lastValue) {
      stuckCount++;
    } else {
      stuckCount = 0;
      lastValue = current;
      return;
    }

    if (stuckCount >= 100) {
      if (inDownloadPhase && current < 90) {
        Utils::animateProgress(progressBar, qMin(current + 1, 90));
        qDebug() << "Anti-stall progress:" << current + 1 << "%";
      } else if (current < 100) {
        Utils::animateProgress(progressBar, current + 1);
      }
      stuckCount = 0;
    }
  });

  installProcess = nullptr;

  inDownloadPhase = false;
  downloadProgress = 0.0;
  downloadLineCount = 0;
  downloadFileCount = 0;
  completedFiles = 0;
  totalDownloadBytes = 0.0;
  lastProcessedLine.clear();
  accumulatedOutput.clear();
  lastStatusText.clear();
}

void MariaDBPage::initializePage() {
  qDebug() << "MariaDBPage::initializePage() called";
  m_wiz = qobject_cast<Wizard *>(wizard());
  Utils::updateStatus(statusLabel, "Checking MariaDB version...", StatusType::Progress);
  progressBar->setVisible(false);
  installButton->setVisible(false);
  uninstallButton->setVisible(false);
  mariaDBReady = false;
  isFreshInstall = false;

  QTimer::singleShot(100, this, [this]() {
    qDebug() << "MariaDB version check timer fired";

    QString output;
#ifdef Q_OS_WIN
    QString mariadbPath = findWindowsMariaDBPath();

    if (!mariadbPath.isEmpty()) {
      QProcessResult result = Utils::executeProcess(mariadbPath, {"--version"}, 5000);
      output = result.stdOut + result.stdErr;
      qDebug() << "Windows MariaDB check - path:" << mariadbPath << "output:" << output.simplified();
    } else {
      qDebug() << "Windows MariaDB check - no binary found";
      output = "";
    }
#else
    QString mariadbPath = QStandardPaths::findExecutable("mariadb");
    if (mariadbPath.isEmpty()) {
      mariadbPath = "mariadb";
      qDebug() << "Using fallback 'mariadb' (not in PATH yet)";
    } else {
      qDebug() << "Found mariadb executable at:" << mariadbPath;
    }

        QProcessResult result = Utils::executeProcess(mariadbPath, {"--version"}, 5000);
        output = result.stdOut + result.stdErr;
    qDebug() << "MariaDB --version raw output:" << output.simplified();
#endif

    if (isMariaDBVersionSupported(output)) {
      QRegularExpression re(R"((?:Distrib\s+|from\s+)(\d+)\.(\d+))");
      QRegularExpressionMatch m = re.match(output);
      QString ver = m.hasMatch() ? QString("%1.%2").arg(m.captured(1)).arg(m.captured(2)) : "Unknown";

      Utils::updateStatus(statusLabel, QString("MariaDB %1 detected (compatible)").arg(ver), StatusType::Success);
      mariaDBReady = true;
      installButton->setVisible(false);
      uninstallButton->setVisible(true);
      m_wiz->isFreshInstall = false;

      qDebug() << "Existing compatible MariaDB found → uninstall button shown";
    } else {
      Utils::updateStatus(statusLabel, "MariaDB not found or too old. Click to install latest version.", StatusType::Warning);
      installButton->setVisible(true);
      installButton->setFocus();
      uninstallButton->setVisible(false);
      qDebug() << "No compatible MariaDB → showing install button only";
    }

    completeChanged();
    qDebug() << "completeChanged() emitted — mariaDBReady:" << mariaDBReady;
  });
}

bool MariaDBPage::isMariaDBVersionSupported(const QString &versionOutput) {
  QRegularExpression re(R"((?:Distrib\s+|from\s+)(\d+)\.(\d+))");
  QRegularExpressionMatch match = re.match(versionOutput);

  if (!match.hasMatch()) {
    qDebug() << "Could not parse MariaDB version from:" << versionOutput;
    return false;
  }

  int major = match.captured(1).toInt();
  int minor = match.captured(2).toInt();
  bool supported = (major > 10) || (major == 10 && minor >= 5);
  qDebug() << "MariaDB version parsed:" << major << "." << minor << "(supported:" << supported << ")";
  return supported;
}

bool MariaDBPage::isComplete() const {
  qDebug() << "MariaDBPage::isComplete() called, returning:" << mariaDBReady;
  return mariaDBReady;
}

bool MariaDBPage::validatePage() {
  qDebug() << "MariaDBPage::validatePage() called, mariaDBReady:" << mariaDBReady;
  if (!mariaDBReady) {
    qDebug() << "mariaDBReady false, showing warning";
    QMessageBox::warning(this, "MariaDB Required", "Please complete MariaDB setup.");
    return false;
  }
  qDebug() << "validatePage returning true";
  return true;
}

void MariaDBPage::installMariaDB() {
  Utils::setWizardButtons();
  installButton->setVisible(false);

  Utils::updateStatus(statusLabel, "Starting installation process...", StatusType::Progress);
  progressBar->setRange(0, 100);
  progressBar->setValue(0);
  progressBar->setVisible(true);
  Utils::animateProgress(progressBar, 5);

  QTimer::singleShot(100, this, [this]() mutable { continueMariaDBInstallation(); });
}

void MariaDBPage::continueMariaDBInstallation() {
  if (installProcess) {
    installProcess->kill();
    installProcess->deleteLater();
  }

  installProcess = new QProcess(this);
  installProcess->setProcessChannelMode(QProcess::MergedChannels);

  connect(installProcess, &QProcess::readyReadStandardError, this, [this]() {
    QString err = installProcess->readAllStandardError();
    if (!err.isEmpty()) {
      qWarning() << "Install chain error:" << err.trimmed();
      accumulatedOutput += "\n[ERR] " + err;

      QRegularExpression re("Failed to fetch https?://dlm\\.mariadb\\.com/.*403\\s+Forbidden");
      if (re.match(err).hasMatch()) {
        qWarning() << "Detected 403 error from MariaDB repo - terminating "
                      "installation";
        Utils::updateStatus(statusLabel,
                            "Access denied to MariaDB repo (403) - likely Tor "
                            "issue. Canceling...",
                            StatusType::Error);
        installProcess->terminate();
        QTimer::singleShot(5000, this, [this]() {
          if (installProcess && installProcess->state() != QProcess::NotRunning) {
            installProcess->kill();
          }
        });
      }
    }
  });

  connect(installProcess, &QProcess::readyReadStandardOutput, this, &MariaDBPage::readInstallOutput);
  connect(installProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus) { QMetaObject::invokeMethod(this, "onInstallFinished", Qt::QueuedConnection, Q_ARG(bool, code == 0)); });

  QString osType = QSysInfo::productType();
  isLinux = (QSysInfo::kernelType() == "linux");

  if (osType.contains("windows", Qt::CaseInsensitive)) {
    handleWindowsInstall();
    return;
  }

  if (osType.contains("macos", Qt::CaseInsensitive) || osType == "osx") {
    handleMacInstall();
    return;
  }

  if (!isLinux) {
    Utils::updateStatus(statusLabel, "Unsupported Operating System!", StatusType::Error);
    installButton->setVisible(true);
    return;
  }

  const auto &info = static_cast<Wizard *>(wizard())->osInfo();
  Utils::fixLock(statusLabel, info.pkgType);

  if (info.pkgType == PkgManagerType::Unknown) {
    Utils::updateStatus(statusLabel, "Unsupported Linux distro", StatusType::Error);
    installButton->setVisible(true);
    return;
  }

  if (!doesRepoHaveSupportedVersion(info)) {
    Utils::updateStatus(statusLabel, "Failed to prepare MariaDB repository", StatusType::Error);
    installButton->setVisible(true);
    return;
  }

  auto isSupportedVer = [](const QVersionNumber &v) { return v >= QVersionNumber(10, 5); };

  progressBar->setVisible(true);
  Utils::updateStatus(statusLabel, "Search best available package", StatusType::Progress);

  QString package;
  if (info.pkgType == PkgManagerType::Dnf) {
    package = "mariadb-server mariadb";
  } else if (info.pkgType == PkgManagerType::Pacman) {
    package = "mariadb";
  } else {
    package = "mariadb-server";
  }

  QString detectedPackage = MariaUtils::findMariaPackages(info.pkgType, package.split(' ').first(), QVersionNumber(10, 5), isSupportedVer);

  if (!detectedPackage.isEmpty()) {
    qDebug() << "Package verified:" << package << "->" << detectedPackage;
  } else {
    package = MariaUtils::findMariaPackages(info.pkgType, "mariadb-server", QVersionNumber(10, 5), isSupportedVer);
    qDebug() << "Using auto-detected package:" << package;
  }

  if (package.isEmpty()) {
    Utils::updateStatus(statusLabel, "No suitable MariaDB package found.", StatusType::Error);
    installButton->setVisible(true);
    return;
  }

  QString cmd;
  switch (info.pkgType) {
  case PkgManagerType::Apt:
    cmd = QString("(apt update -qq 2>&1 | grep -v \"does not have a Release "
                  "file\" || true) && apt install -y %1")
              .arg(package);
    break;
  case PkgManagerType::Dnf:
    cmd = QString("dnf install -y %1").arg(package);
    break;
  case PkgManagerType::Pacman:
    cmd = QString("pacman -Syu --noconfirm %1").arg(package);
    break;
  default:
    Utils::updateStatus(statusLabel, "Unsupported package manager.", StatusType::Error);
    installButton->setVisible(true);
    return;
  }

  QString pass = Utils::generateSecurePassword();
  m_wiz->rootPass = pass;
  qDebug() << "Generated root pass for chaining:" << pass;

  QString sql = DBManager::secureMariaDB(pass, true);
  QString fullCmd;
  fullCmd = "systemctl stop mariadb mysql mysqld 2>/dev/null || true";
  fullCmd += " && " + cmd;
  fullCmd += " && . /etc/profile 2>/dev/null || true";

  if (info.pkgType == PkgManagerType::Pacman) {
    fullCmd += " && echo 'Initializing MariaDB data directory for Arch Linux...'";
    fullCmd += " && mariadb-install-db --user=mysql --basedir=/usr "
               "--datadir=/var/lib/mysql 2>/dev/null || true";
  }

  fullCmd += " && systemctl daemon-reload 2>/dev/null || true";
  fullCmd += " && systemctl unmask mariadb 2>/dev/null || true";
  fullCmd += " && systemctl reset-failed mariadb 2>/dev/null || true";
  fullCmd += " && systemctl enable --now mariadb 2>/dev/null || true";
  fullCmd += " && sleep 5";

  fullCmd += " && echo 'Waiting for MariaDB to be ready...'";
  fullCmd += " && for i in {1..30}; do if (which mariadb-admin >/dev/null 2>&1 && mariadb-admin --skip-password ping >/dev/null 2>&1) || (which mysqladmin >/dev/null 2>&1 && mysqladmin --skip-password ping >/dev/null 2>&1); then echo 'MariaDB is ready after '${i}' seconds'; break; fi; if [ $i -eq 30 ]; then echo 'ERROR: MariaDB failed to start within 30 seconds' >&2; exit 1; fi; sleep 1; done";

  fullCmd += " && ( (which mariadb >/dev/null 2>&1 && mariadb -u root --skip-password -e 'SELECT 1' >/dev/null 2>&1) || (which mysql >/dev/null 2>&1 && mysql -u root --skip-password -e 'SELECT 1' >/dev/null 2>&1) || echo 'Database clients not available yet' )";

  fullCmd += QString(" && ( (which mariadb >/dev/null 2>&1 && mariadb -u root --force -e \"%1\") || (which mysql >/dev/null 2>&1 && mysql -u root --force -e \"%1\") || echo 'Securing skipped - clients not available' )").arg(sql);

  fullCmd += " && systemctl restart mariadb 2>/dev/null || true";
  fullCmd += " && sleep 5";

  if (info.pkgType == PkgManagerType::Apt) {
    fullCmd = "export DEBIAN_FRONTEND=noninteractive && " + fullCmd;
  }

  qDebug() << "Final command:" << fullCmd;
  installProcess->setProgram("pkexec");
  installProcess->setArguments({"/bin/sh", "-c", fullCmd});
  installProcess->start();

  Utils::updateStatus(statusLabel, "Updating package lists...", StatusType::Progress);
  downloadFileCount = 0;
  completedFiles = 0;
  lastProcessedLine = "";
  totalDownloadBytes = 0;
  qDebug() << "Installation + securing started (single prompt)";
}

void MariaDBPage::readInstallOutput() {
  if (!installProcess)
    return;

  QString newData = installProcess->readAllStandardOutput();
  if (newData.isEmpty())
    return;

  QRegularExpression re("Failed to fetch https?://dlm\\.mariadb\\.com/.*403\\s+Forbidden");
  if (re.match(newData).hasMatch()) {
    qWarning() << "Detected 403 error from MariaDB repo in stdout - "
                  "terminating installation";
    Utils::updateStatus(statusLabel, "Access denied to MariaDB repo (403) - likely Tor issue. Canceling...", StatusType::Error);
    installProcess->terminate();
    QTimer::singleShot(5000, this, [this]() {
      if (installProcess && installProcess->state() != QProcess::NotRunning) {
        installProcess->kill();
      }
    });
  }

  if (newData.contains("Error:") || newData.contains("No package") || newData.contains("Unable to find")) {
    qWarning() << "Package installation error detected:" << newData.trimmed();
  }

  newData = Utils::cleanPackageOutput(newData);
  if (newData.isEmpty())
    return;

  accumulatedOutput += newData;
  const auto &info = static_cast<Wizard *>(wizard())->osInfo();

  Utils::parseInstallOutput(newData, inDownloadPhase, downloadProgress, downloadLineCount, downloadFileCount, completedFiles, totalDownloadBytes, lastProcessedLine, progressBar,
                            progressTimer, info.pkgType);

  QString currentPhase = Utils::detectCurrentPhase(inDownloadPhase, accumulatedOutput);
  Utils::updateStatusFromOutput(accumulatedOutput, lastStatusText, statusLabel, currentPhase);
}

void MariaDBPage::onInstallFinished(bool rawSuccess) {
  statusUpdateTimer->stop();
  progressTimer->stop();
  inDownloadPhase = false;
  lastStatusText.clear();
  if (progressBar->value() >= 95) {
    Utils::animateProgress(progressBar, 100);
  } else {
    qDebug() << "Installation finished at" << progressBar->value() << "%";
  }

  QTimer::singleShot(2000, this, [this, rawSuccess]() {
    progressBar->setVisible(false);
    Utils::setWizardButtons(true, true);

    QProcess::execute("rm", {"/tmp/mariadb_*"});

    Utils::updateStatus(statusLabel, "Performing final verification...", StatusType::Progress);
    progressBar->setVisible(true);
    progressBar->setRange(0, 0);
    
    qDebug() << "🤔 Let's see the pass again" << m_wiz->rootPass;
    Concurrent::withCallback(
        this,
        [this]() {
          QThread::msleep(3000);

          return verifyMariaDBInstalled();
        },
        [this, rawSuccess](bool verified) {
          progressBar->setVisible(false);

          if (!verified) {
            qWarning() << "Final verification failed";
            QString errorMsg = "Installation failed. Try installing again.";

            Utils::updateStatus(statusLabel, errorMsg, StatusType::Error);
            accumulatedOutput.clear();
            installButton->setText("Retry Install");
            installButton->setVisible(true);
            Utils::setWizardButtons(false, true);

            mariaDBReady = false;
            emit completeChanged();
          } else {
            accumulatedOutput.clear();
            qDebug() << "Install verified successful (raw:" << rawSuccess << ")";

            isFreshInstall = true;
            m_wiz->isFreshInstall = true;
            Utils::updateStatus(statusLabel, "✓ MariaDB successfully installed and secured!", StatusType::Success);
            installButton->setVisible(false);

            mariaDBReady = true;
            Utils::setWizardButtons(true, true);
            emit completeChanged();

            qDebug() << "Installation complete, ready to proceed";
          }
        });
  });
}

bool MariaDBPage::verifyMariaDBInstalled() {
  qDebug() << "Verifying MariaDB installation...";

#ifdef Q_OS_WIN
  QString mariadbPath = findWindowsMariaDBPath();
  if (mariadbPath.isEmpty()) {
    qDebug() << "MariaDB executable not found";
    return false;
  }

  qDebug() << "Found MariaDB at:" << mariadbPath;
  QProcessResult verResult = Utils::executeProcess(mariadbPath, {"--version"}, 5000);

  if (!isMariaDBVersionSupported(verResult.stdOut + verResult.stdErr)) {
    qWarning() << "Unsupported version on Windows";
    return false;
  }

  bool serviceExists = false;
  bool serviceRunning = false;
  QString serviceName;
  QStringList serviceNames = {"MariaDB", "MySQL"};

  for (const QString &name : serviceNames) {
    QProcessResult svcResult = Utils::executeProcess("sc", {"query", name}, 5000);
    if (svcResult.exitCode == 0) {
      serviceExists = true;
      serviceName = name;

      if (svcResult.stdOut.contains("RUNNING", Qt::CaseInsensitive)) {
        serviceRunning = true;
      }
      qDebug() << "Found service:" << name << (serviceRunning ? "(running)" : "(not running)");
      break;
    }
  }

  if (!serviceExists) {
    qDebug() << "MariaDB service not found";
    return false;
  }

  if (!serviceRunning) {
    qDebug() << "Service not running, attempting to start...";
    QProcessResult startResult = Utils::executeProcess("net", {"start", serviceName}, 10000);
    qDebug() << "Service start attempt for" << serviceName << "exit code:" << startResult.exitCode;

    if (startResult.exitCode == 0) {
      serviceRunning = true;
      QThread::msleep(2000);
    }
  }

  if (serviceRunning && !m_wiz->rootPass.isEmpty()) {
    QString sql = "SELECT 1;";
    QProcessResult connectResult = Utils::executeProcess(mariadbPath, {"-u", "root", "-p" + m_wiz->rootPass, "-e", sql}, 5000);

    if (connectResult.exitCode == 0) {
      qDebug() << "Successfully connected to MariaDB with password";
      return true;
    } else {
      qWarning() << "Failed to connect with password:" << connectResult.stdErr;

      QProcessResult noPassResult = Utils::executeProcess(mariadbPath, {"-u", "root", "--skip-password", "-e", sql}, 5000);

      if (noPassResult.exitCode == 0) {
        qDebug() << "Connected without password (password may not be set)";
        return true;
      }
    }
  }

  qDebug() << "Windows verification failed - service exists:" << serviceExists << ", running:" << serviceRunning;
  return false;
#else
  const auto &info = static_cast<Wizard *>(wizard())->osInfo();
  if(info.pkgType == PkgManagerType::Homebrew) {
    return verifyMacOSMariaDBInstalled();
  }
  bool packageInstalled = false;

  switch (info.pkgType) {
  case PkgManagerType::Apt: {
    QProcessResult pkgResult = Utils::executeProcess("dpkg", {"-s", "mariadb-server"}, 5000);
    if (pkgResult.exitCode != 0) {
      pkgResult = Utils::executeProcess("dpkg", {"-s", "mariadb"}, 5000);
    }
    packageInstalled = (pkgResult.exitCode == 0);
    break;
  }
  case PkgManagerType::Dnf: {
    QProcessResult rpmResult = Utils::executeProcess("rpm", {"-q", "mariadb-server"}, 5000);
    if (rpmResult.exitCode != 0) {
      rpmResult = Utils::executeProcess("rpm", {"-q", "mariadb"}, 5000);
    }
    packageInstalled = (rpmResult.exitCode == 0);
    break;
  }
  default: {
    packageInstalled = true;
    break;
  }
  }

  if (!packageInstalled) {
    qWarning() << "MariaDB package not found installed";
    return false;
  }

  QProcessResult verResult = Utils::executeProcess("mariadb", {"--version"}, 3000);
  if (verResult.exitCode != 0) {
    verResult = Utils::executeProcess("mysql", {"--version"}, 3000);
  }

  if (verResult.exitCode != 0) {
    qWarning() << "Neither mariadb nor mysql binaries work";
    return false;
  }

  QString verOut = verResult.stdOut + verResult.stdErr;
  if (!isMariaDBVersionSupported(verOut)) {
    qWarning() << "Version unsupported:" << verOut.left(100);
    return false;
  }

  QProcessResult svcResult = Utils::executeProcess("systemctl", {"is-active", "--quiet", "mariadb"}, 3000);
  if (svcResult.exitCode != 0) {
    qDebug() << "Service inactive—attempting start";
    QProcessResult startResult = Utils::executeProcess("systemctl", {"start", "mariadb"}, 10000);

    if (startResult.exitCode != 0) {
      qWarning() << "Service start failed:" << startResult.stdErr.trimmed();
      return false;
    }
  }

  bool authOK = false;
  if (!m_wiz->rootPass.isEmpty()) {
    QProcessResult passResult = Utils::executeProcess("mariadb", {"-u", "root", "-p" + m_wiz->rootPass, "-e", "SELECT 1;"}, 5000);
    if (passResult.exitCode != 0) {
      passResult = Utils::executeProcess("mysql", {"-u", "root", "-p" + m_wiz->rootPass, "-e", "SELECT 1;"}, 5000);
    }
    authOK = (passResult.exitCode == 0);
  } else {
    authOK = true;
  }

  qDebug() << "MariaDB verification result - package:" << packageInstalled << ", binary: OK, service: OK, auth:" << authOK;
  return packageInstalled && authOK;
#endif
}

void MariaDBPage::handleWindowsInstall() {
  if (installProcess && installProcess->state() != QProcess::NotRunning) {
    installProcess->terminate();
    installProcess->waitForFinished(3000);
    installProcess->kill();
    installProcess->deleteLater();
    installProcess = nullptr;
  }

  if (currentReply) {
    currentReply->abort();
    currentReply->deleteLater();
    currentReply = nullptr;
  }

  if (downloadFile.isOpen()) {
    downloadFile.close();
  }

  if (!msiTempPath.isEmpty()) {
    QFile::remove(msiTempPath);
    msiTempPath.clear();
  }

  Utils::updateStatus(statusLabel, "Querying package details for MariaDB...", StatusType::Progress);
  progressBar->setValue(0);
  progressBar->setVisible(true);

  QString packageId = "MariaDB.Server";

  QProcess *showProc = new QProcess(this);
  QStringList showArgs;
  showArgs << "show";
  showArgs << "--id" << packageId;
  showArgs << "--accept-source-agreements";
  showArgs << "--disable-interactivity";
  showArgs << "--nowarn";

  qDebug() << "Running winget (async):" << "winget" << showArgs;

  showProc->setProgram("winget");
  showProc->setArguments(showArgs);
  showProc->setProcessChannelMode(QProcess::MergedChannels);

  connect(showProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, showProc, packageId](int exitCode, QProcess::ExitStatus) {
    QString output = QString::fromUtf8(showProc->readAllStandardOutput());
    showProc->deleteLater();

    if (exitCode != 0) {
      Utils::updateStatus(statusLabel, QString("Winget failed: exit code %1").arg(exitCode), StatusType::Error);
      progressBar->setVisible(false);
      installButton->setVisible(true);
      Utils::setWizardButtons(false, true);
      return;
    }

    QRegularExpression urlRe(R"(Installer\s+Url\s*:\s*(https?://[^\s]+?(?:\.msi|\.exe)))", QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    QRegularExpressionMatch match = urlRe.match(output);

    if (!match.hasMatch()) {
      Utils::updateStatus(statusLabel, "Installing MariaDB via Winget...", StatusType::Progress);
      progressBar->setValue(20);

      QStringList installArgs;
      installArgs << "install";
      installArgs << "--id" << packageId;
      installArgs << "--accept-package-agreements";
      installArgs << "--accept-source-agreements";
      installArgs << "--silent";
      installArgs << "--disable-interactivity";

      installProcess = new QProcess(this);
      installProcess->setProcessChannelMode(QProcess::MergedChannels);

      connect(installProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) { handleWindowsInstallFinished(exitCode == 0 || exitCode == 3010); });

      installProcess->start("winget", installArgs);
      progressBar->setValue(30);
      return;
    }

    QString installerUrl = match.captured(1);
    qDebug() << "Found installer URL:" << installerUrl;

    QString tempDir = QDir::tempPath();
    QString extension = installerUrl.endsWith(".msi", Qt::CaseInsensitive) ? "msi" : "exe";
    QString safeFileName = QString("MariaDB-Server-%1.%2").arg(QDateTime::currentSecsSinceEpoch()).arg(extension);
    msiTempPath = QDir(tempDir).absoluteFilePath(safeFileName);

    downloadFile.setFileName(msiTempPath);
    if (!downloadFile.open(QIODevice::WriteOnly)) {
      Utils::updateStatus(statusLabel, "Failed to create temp installer file", StatusType::Error);
      progressBar->setVisible(false);
      installButton->setVisible(true);
      return;
    }

    QUrl downloadUrl(installerUrl);
    QNetworkRequest req = NetUtils::createSslConfiguredRequest(downloadUrl, m_nam, 1800000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(req);
    currentReply = reply;

    Utils::updateStatus(statusLabel, "Downloading MariaDB installer...", StatusType::Progress);
    inDownloadPhase = true;
    progressBar->setValue(5);

    connect(currentReply, &QNetworkReply::downloadProgress, this, [this](qint64 bytesReceived, qint64 bytesTotal) {
      if (bytesTotal <= 0)
        return;

      double mbReceived = bytesReceived / (1024.0 * 1024.0);
      double mbTotal = bytesTotal / (1024.0 * 1024.0);

      int percent = qRound((bytesReceived * 100.0) / bytesTotal);
      progressBar->setValue(qMin(10 + (percent * 80 / 100), 90));

      QString receivedStr = QString::number(mbReceived, 'f', 1);
      QString totalStr = QString::number(mbTotal, 'f', 1);
      Utils::updateStatus(statusLabel, QString("Downloading MariaDB... %1 MB / %2 MB").arg(receivedStr).arg(totalStr), StatusType::Progress);
    });

    connect(currentReply, &QNetworkReply::readyRead, this, [this]() {
      if (currentReply) {
        QByteArray chunk = currentReply->readAll();
        if (!chunk.isEmpty() && downloadFile.write(chunk) == -1) {
          currentReply->abort();
        }
      }
    });

    connect(currentReply, &QNetworkReply::finished, this, [this, installerUrl]() {
      downloadFile.close();

      if (currentReply->error() != QNetworkReply::NoError) {
        Utils::updateStatus(statusLabel, "Download failed", StatusType::Error);
        QFile::remove(msiTempPath);
        msiTempPath.clear();
        currentReply->deleteLater();
        currentReply = nullptr;
        progressBar->setVisible(false);
        installButton->setVisible(true);
        return;
      }

      QProcessResult unblockResult = Utils::executeProcess("powershell", {"-NoProfile", "-Command", QString("Unblock-File -Path '%1'").arg(msiTempPath.replace("'", "''"))}, 8000);

      Utils::updateStatus(statusLabel, "Installing MariaDB...", StatusType::Progress);
      progressBar->setValue(92);

      installProcess = new QProcess(this);
      installProcess->setProcessChannelMode(QProcess::MergedChannels);

      QString nativePath = QDir::toNativeSeparators(msiTempPath);
      QString logPath = QDir::tempPath() + "/MariaDB-install-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".log";

      QStringList installArgs;

      if (installerUrl.endsWith(".msi", Qt::CaseInsensitive)) {
        installProcess->setProgram("msiexec");
        installArgs << "/i" << nativePath;
        installArgs << "/quiet" << "/norestart";
        installArgs << "/L*v" << logPath;
        installArgs << "SERVER=1";
      } else {
        installProcess->setProgram(nativePath);
        installArgs << "--silent";
        installArgs << "--server";
      }

      connect(installProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        QFile::remove(msiTempPath);
        msiTempPath.clear();
        handleWindowsInstallFinished(exitCode == 0 || exitCode == 3010);
      });

      qDebug() << "Starting installer:" << installProcess->program() << installArgs;
      installProcess->setArguments(installArgs);
      installProcess->start();

      currentReply->deleteLater();
      currentReply = nullptr;
    });
  });

  connect(showProc, &QProcess::errorOccurred, this, [this, showProc](QProcess::ProcessError error) {
    showProc->deleteLater();

    if (error == QProcess::FailedToStart) {
      Utils::updateStatus(statusLabel, "Failed to start winget", StatusType::Error);
    } else {
      Utils::updateStatus(statusLabel, "Winget process error", StatusType::Error);
    }
    progressBar->setVisible(false);
    installButton->setVisible(true);
    Utils::setWizardButtons(false, true);
  });

  showProc->start();
}

void MariaDBPage::handleMacInstall() {
    qDebug() << "[DEBUG] handleMacInstall called for MariaDB";

    Utils::updateStatus(statusLabel, "Preparing MariaDB installation...", StatusType::Progress);
    progressBar->setValue(1);

    QString pass = Utils::generateSecurePassword();
    m_wiz->rootPass = pass;
    qDebug() << "Generated root pass for macOS:" << pass;

    QString brewPath = SystemUtils::findBrew();
    if (brewPath.isEmpty()) {
        qDebug() << "[DEBUG] Homebrew not found";
        showManualMacInstructions();
        return;
    }

    QString wrapperScriptPath = QDir::tempPath() + "/brew_mariadb_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".sh";
    QFile wrapperScript(wrapperScriptPath);
    if (wrapperScript.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QString wrapperContent = QString(
            "#!/bin/bash\n"
            "# MariaDB installation for macOS - NO SUDO NEEDED\n"
            "export HOMEBREW_NO_AUTO_UPDATE=1\n"
            "export HOMEBREW_NO_ENV_HINTS=1\n"
            "\n"
            "echo \"[WRAPPER-INFO] Installing MariaDB via Homebrew...\"\n"
            "\"%1\" install mariadb 2>&1\n"
            "\n"
            "if [ $? -ne 0 ]; then\n"
            "    echo \"[WRAPPER-ERROR] MariaDB installation failed!\"\n"
            "    exit 1\n"
            "fi\n"
            "\n"
            "echo \"=== HOMEBREW INSTALL COMPLETE ===\"\n"
            "echo \"[WRAPPER-SUCCESS] MariaDB package installed\"\n"
            "\n"
            "# Start service\n"
            "echo \"[WRAPPER-INFO] Starting MariaDB service...\"\n"
            "\"%1\" services start mariadb 2>&1\n"
            "sleep 5\n"
            "\n"
            "# Wait for MariaDB to be ready (socket auth works without root)\n"
            "echo \"[WRAPPER-INFO] Waiting for MariaDB to be ready...\"\n"
            "CURRENT_USER=$(whoami)\n"
            "CONNECTED=0\n"
            "for i in {1..60}; do\n"
            "    if mysql -u $CURRENT_USER -e \"SELECT 1;\" >/dev/null 2>&1; then\n"
            "        echo \"[WRAPPER-INFO] Connected as '$CURRENT_USER' after ${i} seconds\"\n"
            "        CONNECTED=1\n"
            "        break\n"
            "    elif [ $i -eq 60 ]; then\n"
            "        echo \"[WRAPPER-ERROR] MariaDB failed to start within 60 seconds!\"\n"
            "        exit 1\n"
            "    else\n"
            "        sleep 1\n"
            "    fi\n"
            "done\n"
            "\n"
            "if [ $CONNECTED -eq 0 ]; then\n"
            "    echo \"[WRAPPER-ERROR] Could not connect to MariaDB!\"\n"
            "    exit 1\n"
            "fi\n"
            "\n"
            "# SET ROOT PASSWORD - Using current user socket auth\n"
            "echo \"[WRAPPER-INFO] Setting root password...\"\n"
            "mysql -u $CURRENT_USER -e \"ALTER USER 'root'@'localhost' IDENTIFIED BY '%2';\" 2>&1\n"
            "\n"
            "if [ $? -ne 0 ]; then\n"
            "    echo \"[WRAPPER-ERROR] Failed to set root password!\"\n"
            "    exit 1\n"
            "fi\n"
            "\n"
            "echo \"[WRAPPER-SUCCESS] Root password set\"\n"
            "\n"
            "# Securing installation\n"
            "echo \"[WRAPPER-INFO] Securing installation...\"\n"
            "mysql -u $CURRENT_USER -e \""
            "DELETE FROM mysql.user WHERE User='';"
            "DROP DATABASE IF EXISTS test;"
            "DELETE FROM mysql.db WHERE Db='test' OR Db='test\\\\\\\\_%';"
            "FLUSH PRIVILEGES;\" 2>&1\n"
            "\n"
            "# Test root connection\n"
            "echo \"[WRAPPER-INFO] Testing root connection...\"\n"
            "if mysql -u root -p'%2' -e \"SELECT 1;\" >/dev/null 2>&1; then\n"
            "    echo \"[WRAPPER-SUCCESS] Root password works!\"\n"
            "else\n"
            "    echo \"[WRAPPER-ERROR] Root password doesn't work!\"\n"
            "    exit 1\n"
            "fi\n"
            "\n"
            "# Restart service\n"
            "echo \"[WRAPPER-INFO] Restarting MariaDB service...\"\n"
            "\"%1\" services restart mariadb 2>&1\n"
            "sleep 3\n"
            "\n"
            "# FINAL VERIFICATION\n"
            "echo \"[WRAPPER-INFO] Final verification...\"\n"
            "if mysql -u root -p'%2' -e \"SELECT 1;\" >/dev/null 2>&1; then\n"
            "    echo \"[WRAPPER-SUCCESS] MariaDB fully installed and secured!\"\n"
            "    exit 0\n"
            "else\n"
            "    echo \"[WRAPPER-ERROR] FINAL VERIFICATION FAILED!\"\n"
            "    exit 1\n"
            "fi\n"
        ).arg(brewPath).arg(pass);
        
        wrapperScript.write(wrapperContent.toUtf8());
        wrapperScript.close();
        wrapperScript.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qDebug() << "[DEBUG] Created wrapper script with WRAPPER prefixes:" << wrapperScriptPath;
    } else {
        qCritical() << "[ERROR] Failed to create wrapper script";
        Utils::updateStatus(statusLabel, "Failed to create installation script", StatusType::Error);
        installButton->setVisible(true);
        return;
    }

    if (installProcess) {
        installProcess->kill();
        installProcess->deleteLater();
        installProcess = nullptr;
    }

    installProcess = new QProcess(this);
    installProcess->setProcessChannelMode(QProcess::SeparateChannels);

    connect(installProcess, &QProcess::readyReadStandardOutput, this, &MariaDBPage::readInstallOutput);
    
    connect(installProcess, &QProcess::readyReadStandardError, this, [this]() {
        QString err = installProcess->readAllStandardError();
        if (!err.isEmpty()) {
            qWarning() << "Install stderr:" << err.trimmed();
            accumulatedOutput += "\n[ERR] " + err;
        }
    });

    connect(installProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            this, [this, wrapperScriptPath](int exitCode, QProcess::ExitStatus) {
        
        qDebug() << "[DEBUG] MariaDB installation process finished. Cleaning up temp script.";
        QFile::remove(wrapperScriptPath);
        
        QMetaObject::invokeMethod(this, "onInstallFinished", Qt::QueuedConnection, Q_ARG(bool, exitCode == 0));
    });

    installProcess->setProgram("/bin/bash");
    installProcess->setArguments({"-c", QString("\"%1\"").arg(wrapperScriptPath)});
    installProcess->setWorkingDirectory(QDir::homePath());
    
    qDebug() << "[DEBUG] Starting MariaDB installation (no sudo needed)...";
    installProcess->start();

    if (!installProcess->waitForStarted(5000)) {
        qCritical() << "[ERROR] Process failed to start:" << installProcess->errorString();
        Utils::updateStatus(statusLabel, "Error: Failed to start installation", StatusType::Error);
        QFile::remove(wrapperScriptPath);
        installButton->setVisible(true);
        return;
    }

    qDebug() << "[DEBUG] Installation process started successfully";
    Utils::updateStatus(statusLabel, "MariaDB installation in progress...", StatusType::Progress);
    Utils::animateProgress(progressBar, 10);
}
void MariaDBPage::showManualMacInstructions() {
  Utils::updateStatus(statusLabel, "Homebrew not available", StatusType::Error);
  Utils::animateProgress(progressBar, 100);

  QMessageBox msgBox(this);
  msgBox.setIcon(QMessageBox::Information);
  msgBox.setWindowTitle("Manual Installation Required");
  msgBox.setText("Please install MariaDB manually:\n\n"
                 "Option 1: Install Homebrew first:\n"
                 "  /bin/bash -c \"$(curl -fsSL "
                 "https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\"\n"
                 "  Then run: brew install mariadb\n\n"
                 "Option 2: Download directly from:\n"
                 "  https://mariadb.org/download/");

  msgBox.setMinimumHeight(450);
  msgBox.exec();

  QMetaObject::invokeMethod(this, "onInstallFinished", Qt::QueuedConnection, Q_ARG(bool, false));
}

bool MariaDBPage::doesRepoHaveSupportedVersion(const OSInfo &info) {
  auto isSupportedVer = [](const QVersionNumber &v) { return v >= QVersionNumber(10, 5); };

  progressBar->setVisible(true);
  Utils::updateStatus(statusLabel, "Searching for MariaDB package...", StatusType::Progress);
  Utils::animateProgress(progressBar, 10);

  QString candidate;

  if (info.pkgType == PkgManagerType::Dnf) {
    qDebug() << "DNF: Using simplified package detection for Fedora";

    QProcessResult checkResult = Utils::executeProcess("dnf", {"list", "--available", "mariadb-server"}, 10000);
    if (checkResult.stdOut.contains("mariadb-server")) {
      candidate = "mariadb-server";
      qDebug() << "DNF: mariadb-server package is available";
    } else {
      checkResult = Utils::executeProcess("dnf", {"list", "--available", "mariadb"}, 10000);
      if (checkResult.stdOut.contains("mariadb")) {
        candidate = "mariadb";
        qDebug() << "DNF: mariadb package is available";
      }
    }

    if (!candidate.isEmpty()) {
      QString version = Utils::getPackageVersion(info.pkgType, "", {}, candidate);
      if (!version.isEmpty() && version != "999.999") {
        QVersionNumber ver = QVersionNumber::fromString(version.split('-').first());
        if (!ver.isNull() && ver >= QVersionNumber(10, 5)) {
          qDebug() << "DNF: Version" << ver.toString() << "is supported";
        } else if (!ver.isNull()) {
          qDebug() << "DNF: Version" << ver.toString() << "may be older but proceeding anyway";
        }
      } else {
        qDebug() << "DNF: Version detection failed but package exists, proceeding";
      }
    } else {
      candidate = "mariadb-server";
      qDebug() << "DNF: Using fallback candidate - package will be installed by dnf";
    }
  } else {
    candidate = MariaUtils::findMariaPackages(info.pkgType, "mariadb-server", QVersionNumber(10, 5), isSupportedVer);
  }

  if (!candidate.isEmpty()) {
    qDebug() << "Supported candidate found:" << candidate << "— using native repo";
    Utils::animateProgress(progressBar, 20);
    return true;
  }

  qDebug() << "No supported candidate (>=10.5) — adding official MariaDB repo";
  Utils::updateStatus(statusLabel, "Adding official MariaDB repository...", StatusType::Progress);
  Utils::animateProgress(progressBar, 30);

  bool ok = runMariaDBRepoSetup(info);

  if (ok) {
    Utils::updateStatus(statusLabel, "Verifying repository setup...", StatusType::Progress);
    Utils::animateProgress(progressBar, 50);

    QString candidateAfterRepo;
    if (info.pkgType == PkgManagerType::Dnf) {
      QProcessResult checkResult = Utils::executeProcess("dnf", {"list", "--available", "mariadb-server"}, 10000);
      if (checkResult.stdOut.contains("mariadb-server")) {
        candidateAfterRepo = "mariadb-server";
      }
    } else {
      candidateAfterRepo = MariaUtils::findMariaPackages(info.pkgType, "mariadb-server", QVersionNumber(10, 5), isSupportedVer);
    }

    if (!candidateAfterRepo.isEmpty()) {
      qDebug() << "Supported candidate found after repo setup:" << candidateAfterRepo;
      Utils::animateProgress(progressBar, 60);
      return true;
    } else {
      qDebug() << "No supported candidate even after repo setup";
      ok = false;
    }
  }

  if (!ok) {
    if (info.pkgType == PkgManagerType::Dnf) {
      QString fedoraVersion = QSysInfo::productVersion();
      qDebug() << "DNF repo setup failed, but Fedora" << fedoraVersion << "should have MariaDB - proceeding anyway";
      Utils::updateStatus(statusLabel, "Trying MariaDB packages...", StatusType::Progress);
      Utils::animateProgress(progressBar, 60);
      return true;
    }

    Utils::updateStatus(statusLabel, "Failed to prepare MariaDB repository.", StatusType::Error);
    installButton->setVisible(true);
    Utils::animateProgress(progressBar, 100);
    return false;
  }

  Utils::animateProgress(progressBar, 60);
  return true;
}

bool MariaDBPage::runMariaDBRepoSetup(const OSInfo &info) {
  qDebug() << "Downloading MariaDB repo setup script using QNetworkAccessManager...";

  QNetworkRequest request(QUrl("https://r.mariadb.com/downloads/mariadb_repo_setup"));

  QNetworkReply *reply = m_nam->get(request);

  QEventLoop loop;
  connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  if (reply->error() != QNetworkReply::NoError) {
    qDebug() << "Network error:" << reply->errorString();
    reply->deleteLater();
    return false;
  }

  QByteArray data = reply->readAll();
  QString scriptPath = "/tmp/mariadb_repo_setup";

  QFile file(scriptPath);
  if (!file.open(QIODevice::WriteOnly)) {
    qDebug() << "Cannot open file for writing:" << scriptPath;
    reply->deleteLater();
    return false;
  }

  file.write(data);
  file.close();
  file.setPermissions(QFile::ExeUser | QFile::ReadUser | QFile::WriteUser);

  reply->deleteLater();
  qDebug() << "Script downloaded successfully, size:" << data.size();

  return runRepoSetupScript(scriptPath);
}

bool MariaDBPage::runRepoSetupScript(const QString &scriptPath) {
  qDebug() << "Running MariaDB repo setup script...";

  QProcess *setupProcess = new QProcess(this);

  connect(setupProcess, &QProcess::readyReadStandardError, this, [setupProcess]() {
    QString error = setupProcess->readAllStandardError();
    if (!error.trimmed().isEmpty()) {
      qDebug() << "Repo setup stderr:" << error;
    }
  });

  connect(setupProcess, &QProcess::readyReadStandardOutput, this, [setupProcess]() {
    QString output = setupProcess->readAllStandardOutput();
    if (!output.trimmed().isEmpty()) {
      qDebug() << "Repo setup stdout:" << output;
    }
  });

  QString setupCmd = QString("bash %1 --mariadb-server-version=\"mariadb-11.8\"").arg(scriptPath);
  setupProcess->setProgram("pkexec");
  setupProcess->setArguments({"/bin/sh", "-c", setupCmd});
  setupProcess->start();

  if (!setupProcess->waitForStarted(5000)) {
    qDebug() << "Failed to start repo setup process";
    setupProcess->deleteLater();
    QFile::remove(scriptPath);
    return false;
  }

  if (!setupProcess->waitForFinished(60000)) {
    qDebug() << "Repo setup process timed out";
    setupProcess->kill();
    setupProcess->waitForFinished(5000);
    setupProcess->deleteLater();
    QFile::remove(scriptPath);
    return false;
  }

  int exitCode = setupProcess->exitCode();
  qDebug() << "Repo setup exit code:" << exitCode;
  bool success = (exitCode == 0);
  setupProcess->deleteLater();
  QFile::remove(scriptPath);

  if (!success) {
    qDebug() << "Failed to run MariaDB repo setup script";
    return false;
  }

  qDebug() << "Repo setup completed successfully";
  return true;
}
void MariaDBPage::uninstallMariaDB() {
  int ret = QMessageBox::warning(this, "DANGER: Complete MariaDB Removal",
                                 "<b>This will permanently delete MariaDB and ALL databases!</b><br><br>"
                                 "All databases will be erased.<br><br>"
                                 "Are you absolutely sure?",
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (ret != QMessageBox::Yes)
    return;

  Utils::setWizardButtons();
  uninstallButton->setEnabled(false);
  installButton->setVisible(false);

  const auto &info = static_cast<Wizard *>(wizard())->osInfo();

  if (info.pkgType == PkgManagerType::Winget) {
    handleWindowsUninstall();
    return;
  } else if (info.pkgType == PkgManagerType::Homebrew) {
    handleMacUninstall();
    return;
  }

  Utils::fixLock(statusLabel, info.pkgType);
  Utils::updateStatus(statusLabel, "Purging MariaDB and all data...", StatusType::Progress);
  progressBar->setRange(0, 0);
  progressBar->setVisible(true);

  if (installProcess) {
    installProcess->kill();
    installProcess->deleteLater();
  }

  installProcess = new QProcess(this);

  connect(installProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int exitCode, QProcess::ExitStatus) {
    progressBar->setVisible(false);
    uninstallButton->setEnabled(true);
    Utils::setWizardButtons(false, true);
    if (exitCode == 0) {
      mariaDBReady = false;
      isFreshInstall = false;
      m_wiz->isFreshInstall = false;
      completeChanged();
      QTimer::singleShot(500, this, &MariaDBPage::initializePage);
      qDebug() << "MariaDB purged successfully";
    } else {
      Utils::updateStatus(statusLabel, "Purge failed (exit code: " + QString::number(exitCode) + ")", StatusType::Error);
    }
  });

  QString removeCmd;
  if (info.pkgType == PkgManagerType::Apt) {
    removeCmd = "apt-get purge -y 'mariadb*' 'mysql*' || apt-get remove -y "
                "--purge 'mariadb*' 'mysql*' || true";
  } else if (info.pkgType == PkgManagerType::Dnf) {
    removeCmd = "dnf remove -y mariadb-server mariadb mariadb-config "
                "mariadb-client || true";
  } else if (info.pkgType == PkgManagerType::Pacman) {
    removeCmd = "pacman -Rns --noconfirm mariadb mariadb-clients mariadb-libs || true";
  } else if (info.pkgType == PkgManagerType::Homebrew) {
    removeCmd = "brew uninstall --force mariadb mysql || true";
  } else {
    removeCmd = "echo 'Unsupported package manager'";
  }

  QString fullCmd = "systemctl stop mariadb mysql mysqld 2>/dev/null || true && " + removeCmd + " && " +
                    "rm -rf /var/lib/mysql /etc/mysql /var/log/mysql /var/log/mariadb "
                    "2>/dev/null || true && " +
                    "rm -f /etc/my.cnf /etc/mysql.conf.d/* 2>/dev/null || true && " + "userdel -r mysql 2>/dev/null || true && " + "groupdel mysql 2>/dev/null || true";

  if (info.pkgType == PkgManagerType::Apt) {
    fullCmd = "export DEBIAN_FRONTEND=noninteractive && " + fullCmd;
  }

  qDebug() << "Running purge directly (already root):" << fullCmd;
  installProcess->setProgram("/bin/sh");
  installProcess->setArguments({"-c", fullCmd});
  installProcess->start();
}

void MariaDBPage::handleWindowsUninstall() {
  Utils::updateStatus(statusLabel, "Stopping MariaDB services...", StatusType::Progress);
  progressBar->setRange(0, 100);
  progressBar->setValue(10);
  progressBar->setVisible(true);

  Concurrent::withCallback(this, [this]() { return performWindowsUninstall(); }, [this](bool success) { onWindowsUninstallFinished(success); });
}

bool MariaDBPage::performWindowsUninstall() {

  QString productCode = extractMariaDBProductCode();

  if (productCode.isEmpty()) {
    qDebug() << "ERROR: Could not extract MariaDB Product Code from registry.";
    return false;
  }

  qDebug() << "Extracted Product Code:" << productCode;

  qDebug() << "Stopping MariaDB services...";
  Utils::executeProcess("net", {"stop", "MariaDB"}, 3000);
  QThread::msleep(1000);

  qDebug() << "Performing clean msiexec uninstall...";
  QProcessResult result = Utils::executeProcess("msiexec", {"/x", productCode, "/qn", "/norestart"}, 45000);

  bool success = (result.exitCode == 0 || result.exitCode == 3010);
  Utils::setWizardButtons(false, true);
  if (success) {
    qDebug() << "Clean msiexec uninstall succeeded!";
    QThread::msleep(3000);
    return findWindowsMariaDBPath().isEmpty();

  } else {
    qDebug() << "msiexec failed with code:" << result.exitCode;
    return false;
  }
}

QString MariaDBPage::extractMariaDBProductCode() {

  QProcessResult result = Utils::executeProcess("powershell",
                                                {"-Command", "$uninstall = Get-ItemProperty "
                                                             "'HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\*' | "
                                                             "Where-Object { $_.DisplayName -like '*MariaDB*' } | "
                                                             "Select-Object -First 1; "
                                                             "if ($uninstall.UninstallString -match '{([A-F0-9-]+)}') { $matches[0] "
                                                             "}"},
                                                5000);

  if (result.exitCode == 0) {
    QString output = result.stdOut.trimmed();
    if (!output.isEmpty() && output.startsWith('{') && output.endsWith('}')) {
      qDebug() << "PowerShell extracted GUID:" << output;
      return output;
    }
  }

  return QString();
}

void MariaDBPage::onWindowsUninstallFinished(bool success) {
  progressBar->setVisible(false);
  uninstallButton->setEnabled(true);
  Utils::setWizardButtons(true, true);
  if (success) {
    mariaDBReady = false;
    isFreshInstall = false;
    m_wiz->isFreshInstall = false;
    completeChanged();
    QTimer::singleShot(500, this, &MariaDBPage::initializePage);
    qDebug() << "Windows MariaDB purged successfully";
    Utils::updateStatus(statusLabel, "MariaDB successfully removed", StatusType::Success);
  } else {
    Utils::updateStatus(statusLabel,
                        "Failed to completely remove MariaDB. You may need to "
                        "uninstall manually.",
                        StatusType::Error);
  }
}

void MariaDBPage::handleWindowsInstallFinished(bool installSuccess) {
  progressBar->setValue(99);

  if (!installSuccess) {
    Utils::updateStatus(statusLabel, "Installation failed", StatusType::Error);
    installButton->setVisible(true);
    QMetaObject::invokeMethod(this, "onInstallFinished", Qt::QueuedConnection, Q_ARG(bool, false));
    return;
  }

  Utils::updateStatus(statusLabel, "MariaDB installed - configuring...", StatusType::Progress);

  Concurrent::withCallback(
      this,
      [this]() {
        QThread::msleep(2000);
        return configureWindowsMariaDB();
      },
      [this](bool success) {
        if (success) {
          Utils::updateStatus(statusLabel, "MariaDB installed and configured successfully!", StatusType::Success);
          QMetaObject::invokeMethod(this, "onInstallFinished", Qt::QueuedConnection, Q_ARG(bool, true));
        } else {
          Utils::updateStatus(statusLabel, "Installation complete but configuration failed", StatusType::Error);
          installButton->setVisible(true);
          QMetaObject::invokeMethod(this, "onInstallFinished", Qt::QueuedConnection, Q_ARG(bool, false));
        }
      });
}

bool MariaDBPage::configureWindowsMariaDB() {
  QString mariadbPath = findWindowsMariaDBPath();
  if (mariadbPath.isEmpty()) {
    qWarning() << "Could not find MariaDB binary for configuration";
    return false;
  }

  QString binDir = QFileInfo(mariadbPath).absolutePath();
  QString pass = Utils::generateSecurePassword();
  m_wiz->rootPass = pass;
  qDebug() << "Configuring Windows MariaDB with password...";

  if (!createWindowsMariaDBService(mariadbPath)) {
    qWarning() << "Failed to create MariaDB service";
    return false;
  }

  QString serviceName;
  QStringList serviceNames = {"MariaDB", "MySQL"};

  for (const QString &name : serviceNames) {
    QProcessResult svcResult = Utils::executeProcess("sc", {"query", name}, 5000);
    if (svcResult.exitCode == 0) {
      serviceName = name;
      break;
    }
  }

  if (serviceName.isEmpty()) {
    qWarning() << "Could not determine service name after creation";
    return false;
  }

  QProcessResult startResult = Utils::executeProcess("net", {"start", serviceName}, 15000);
  qDebug() << "Service start exit code:" << startResult.exitCode;
  QThread::msleep(5000);

  QString sql = DBManager::secureMariaDB(pass);
  QProcessResult sqlResult = Utils::executeProcess(mariadbPath, {"-u", "root", "--skip-password", "-e", sql}, 15000);

  if (sqlResult.exitCode != 0) {
    qWarning() << "Failed to secure MariaDB:" << sqlResult.stdErr;

    QString mysqladminPath = binDir + "\\mysqladmin.exe";
    if (QFile::exists(mysqladminPath)) {
      QProcessResult adminResult = Utils::executeProcess(mysqladminPath, {"-u", "root", "password", pass}, 10000);
      qDebug() << "mysqladmin exit code:" << adminResult.exitCode;
    }
  }

  QProcessResult stopResult = Utils::executeProcess("net", {"stop", serviceName}, 10000);
  QProcessResult restartResult = Utils::executeProcess("net", {"start", serviceName}, 10000);
  addMariaDBToSystemPath(binDir);
  updateCurrentProcessPath(binDir);

  return true;
}

bool MariaDBPage::addMariaDBToSystemPath(const QString &binDir) {
  QString modifiableDir = binDir;
  modifiableDir.replace("\\", "\\\\");

  QString psCommand = QString("$oldPath = [Environment]::GetEnvironmentVariable('Path', "
                              "'Machine'); "
                              "if ($oldPath -notlike '*%1*') { "
                              "    $newPath = '%1;' + $oldPath; "
                              "    [Environment]::SetEnvironmentVariable('Path', $newPath, "
                              "'Machine'); "
                              "    Write-Host 'PATH updated'; "
                              "    exit 0; "
                              "} else { "
                              "    Write-Host 'PATH already contains MariaDB'; "
                              "    exit 0; "
                              "}")
                          .arg(modifiableDir);

  QProcessResult psResult = Utils::executeProcess("powershell", {"-Command", psCommand}, 10000);
  return psResult.exitCode == 0;
}

void MariaDBPage::updateCurrentProcessPath(const QString &binDir) {
  QByteArray currentPath = qgetenv("PATH");
  if (!currentPath.contains(binDir.toUtf8())) {
    QByteArray newPath = binDir.toUtf8() + ";" + currentPath;
    qputenv("PATH", newPath);
    qDebug() << "Updated process PATH";
  }
}

QString MariaDBPage::findWindowsMariaDBPath() {
  QDir programFiles("C:\\Program Files");
  QStringList mariadbDirs = programFiles.entryList(QStringList() << "MariaDB *", QDir::Dirs);

  for (const QString &dir : mariadbDirs) {
    QString exePath = programFiles.absoluteFilePath(dir + "\\bin\\mariadb.exe");
    if (QFile::exists(exePath)) {
      return exePath;
    }
  }

  QDir programFilesX86("C:\\Program Files (x86)");
  mariadbDirs = programFilesX86.entryList(QStringList() << "MariaDB *", QDir::Dirs);

  for (const QString &dir : mariadbDirs) {
    QString exePath = programFilesX86.absoluteFilePath(dir + "\\bin\\mariadb.exe");
    if (QFile::exists(exePath)) {
      return exePath;
    }
  }

  return QString();
}

bool MariaDBPage::createWindowsMariaDBService(const QString &mariadbPath) {
  if (mariadbPath.isEmpty())
    return false;

  QString binDir = QFileInfo(mariadbPath).absolutePath();
  QString mysqldPath = binDir + "\\mysqld.exe";

  if (!QFile::exists(mysqldPath)) {
    qWarning() << "mysqld.exe not found at:" << mysqldPath;
    return false;
  }

  qDebug() << "Creating MariaDB Windows service using mysqld --install...";

  QProcessResult stopResult = Utils::executeProcess("net", {"stop", "MariaDB"}, 5000);
  QProcessResult removeResult = Utils::executeProcess("sc", {"delete", "MariaDB"}, 5000);

  QProcessResult installResult = Utils::executeProcess(mysqldPath, {"--install", "MariaDB"}, 15000);
  qDebug() << "mysqld --install stdout:" << installResult.stdOut.trimmed();
  qDebug() << "mysqld --install stderr:" << installResult.stdErr.trimmed();
  qDebug() << "mysqld --install exit code:" << installResult.exitCode;

  if (installResult.exitCode != 0) {
    qDebug() << "Trying mysqld --install without service name...";
    QProcessResult installResult2 = Utils::executeProcess(mysqldPath, {"--install"}, 15000);
    qDebug() << "mysqld --install (default) stdout:" << installResult2.stdOut.trimmed();
    qDebug() << "mysqld --install (default) stderr:" << installResult2.stdErr.trimmed();
    qDebug() << "mysqld --install (default) exit code:" << installResult2.exitCode;
    return installResult2.exitCode == 0;
  }

  return true;
}


void MariaDBPage::handleMacUninstall() {
    qDebug() << "[DEBUG] handleMacUninstall called";

    Utils::setWizardButtons();
    uninstallButton->setEnabled(false);
    installButton->setVisible(false);
    
    Utils::updateStatus(statusLabel, "Preparing MariaDB uninstall on macOS...", StatusType::Progress);
    progressBar->setRange(0, 0);
    progressBar->setVisible(true);

    
    QString brewPath = SystemUtils::findBrew();
    if (brewPath.isEmpty()) {
        Utils::updateStatus(statusLabel, "Homebrew not found - MariaDB may not be installed via Homebrew", StatusType::Warning);
        progressBar->setVisible(false);
        uninstallButton->setEnabled(true);
        mariaDBReady = false;
        completeChanged();
        QTimer::singleShot(500, this, &MariaDBPage::initializePage);
        return;
    }

    
    QString askpassScriptPath = QDir::tempPath() + "/brew_uninstall_askpass_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".sh";
    QFile askpassScript(askpassScriptPath);
    if (askpassScript.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QString scriptContent = QString(
            "#!/bin/bash\n"
            "# Askpass script for Homebrew's internal sudo\n"
            "osascript -e 'tell application \"System Events\"' \\\n"
            "          -e 'set answer to text returned of (display dialog \"MariaDB removal requires administrator privileges.\" & return & \"Please enter your password:\" default answer \"\" with hidden answer buttons {\"Cancel\", \"OK\"} default button 2 with icon caution)' \\\n"
            "          -e 'return answer' \\\n"
            "          -e 'end tell' 2>/dev/null\n"
        );
        askpassScript.write(scriptContent.toUtf8());
        askpassScript.close();
        askpassScript.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qDebug() << "[DEBUG] Created askpass script:" << askpassScriptPath;
    }

    
    QString wrapperScriptPath = QDir::tempPath() + "/brew_uninstall_wrapper_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".sh";
    QFile wrapperScript(wrapperScriptPath);
    if (wrapperScript.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QString wrapperContent = QString(
            "#!/bin/bash\n"
            "# Wrapper to run brew with askpass support for MariaDB uninstall\n"
            "export SUDO_ASKPASS=\"%1\"\n"
            "export HOMEBREW_NO_AUTO_UPDATE=1\n"
            "export HOMEBREW_NO_ENV_HINTS=1\n"
            "export HOMEBREW_NO_INSTALL_CLEANUP=1\n"
            "\n"
            "# Clear any cached sudo credentials to force use of askpass\n"
            "sudo -k\n"
            "\n"
            "# Run brew uninstall as the current user\n"
            "echo \"[INFO] Starting MariaDB removal via Homebrew...\"\n"
            "\n"
            "# CRITICAL: Use 'script' to fake a TTY and force brew to show progress\n"
            "# -q for quiet, /dev/null as output file, 2>&1 to capture all output\n"
            "# Stop services first\n"
            "script -q /dev/null \"%2\" services stop mariadb 2>&1\n"
            "# Then uninstall\n"
            "script -q /dev/null \"%2\" uninstall --force mariadb mysql 2>&1\n"
            "\n"
            "# Clean up data\n"
            "rm -rf /usr/local/var/mysql 2>/dev/null || true\n"
            "rm -rf ~/Library/Application\\\\ Support/mariadb 2>/dev/null || true\n"
            "\n"
            "EXIT_CODE=$?\n"
            "if [ $EXIT_CODE -eq 0 ]; then\n"
            "    echo \"[SUCCESS] MariaDB removal completed\"\n"
            "else\n"
            "    echo \"[ERROR] Removal failed with exit code: $EXIT_CODE\"\n"
            "fi\n"
            "exit $EXIT_CODE\n"
        ).arg(askpassScriptPath).arg(brewPath);
        
        wrapperScript.write(wrapperContent.toUtf8());
        wrapperScript.close();
        wrapperScript.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qDebug() << "[DEBUG] Created wrapper script:" << wrapperScriptPath;
    }

    
    if (installProcess) {
        installProcess->kill();
        installProcess->deleteLater();
    }

    installProcess = new QProcess(this);
    installProcess->setProcessChannelMode(QProcess::SeparateChannels);

    
    connect(installProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!installProcess) return;
        QString data = installProcess->readAllStandardOutput();
        if (!data.isEmpty()) {
            qDebug() << "Uninstall stdout:" << data.trimmed();
            
            accumulatedOutput += data;
        }
    });

    connect(installProcess, &QProcess::readyReadStandardError, this, [this]() {
        if (!installProcess) return;
        QString err = installProcess->readAllStandardError();
        if (!err.isEmpty()) {
            qDebug() << "Uninstall stderr:" << err.trimmed();
            accumulatedOutput += "\n[ERR] " + err;
        }
    });

    
    connect(installProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            this, [this, askpassScriptPath, wrapperScriptPath](int exitCode, QProcess::ExitStatus) {
        
        qDebug() << "[DEBUG] Uninstall process finished with exit code:" << exitCode;
        
        
        QFile::remove(askpassScriptPath);
        QFile::remove(wrapperScriptPath);
        
        progressBar->setVisible(false);
        uninstallButton->setEnabled(true);
        Utils::setWizardButtons(false, true);
        
        if (exitCode == 0) {
            mariaDBReady = false;
            isFreshInstall = false;
            if (m_wiz) m_wiz->isFreshInstall = false;
            completeChanged();
            QTimer::singleShot(500, this, &MariaDBPage::initializePage);
            qDebug() << "macOS MariaDB purged successfully";
            Utils::updateStatus(statusLabel, "MariaDB successfully removed", StatusType::Success);
        } else {
            QString errorMsg = "Removal failed (exit code: " + QString::number(exitCode) + ")";
            if (exitCode == 1) {
                errorMsg = "Uninstall cancelled or password not accepted";
            }
            Utils::updateStatus(statusLabel, errorMsg, StatusType::Error);
        }
    });

    
    installProcess->setProgram("/bin/bash");
    installProcess->setArguments({"-c", QString("\"%1\"").arg(wrapperScriptPath)});
    installProcess->setWorkingDirectory(QDir::homePath());
    
    qDebug() << "[DEBUG] Starting MariaDB uninstall via wrapper...";
    installProcess->start();

    if (!installProcess->waitForStarted(5000)) {
        qCritical() << "[ERROR] Process failed to start:" << installProcess->errorString();
        Utils::updateStatus(statusLabel, "Error: Failed to start uninstall", StatusType::Error);
        QFile::remove(askpassScriptPath);
        QFile::remove(wrapperScriptPath);
        uninstallButton->setEnabled(true);
        progressBar->setVisible(false);
        return;
    }

    qDebug() << "[DEBUG] Uninstall process started successfully";
    Utils::updateStatus(statusLabel, "MariaDB removal in progress...", StatusType::Progress);
}

bool MariaDBPage::verifyMacOSMariaDBInstalled() {
    qDebug() << "Verifying MariaDB installation on macOS...";
    
    
    QString brewPath = SystemUtils::findBrew();
    if (brewPath.isEmpty()) {
        qWarning() << "Homebrew not found on macOS";
        return false;
    }
    
    
    QProcessResult brewListResult = Utils::executeProcess(brewPath, {"list", "mariadb"}, 5000);
    if (brewListResult.exitCode != 0) {
        
        brewListResult = Utils::executeProcess(brewPath, {"list", "mysql"}, 5000);
        if (brewListResult.exitCode != 0) {
            qWarning() << "MariaDB/MySQL not found in Homebrew";
            return false;
        }
    }
    
    
    QString mariadbPath = "/usr/local/bin/mariadb";
    if (!QFile::exists(mariadbPath)) {
        mariadbPath = "/usr/local/bin/mysql";
        if (!QFile::exists(mariadbPath)) {
            
            QProcessResult whichResult = Utils::executeProcess("which", {"mariadb"}, 3000);
            if (whichResult.exitCode != 0) {
                whichResult = Utils::executeProcess("which", {"mysql"}, 3000);
            }
            if (whichResult.exitCode == 0) {
                mariadbPath = whichResult.stdOut.trimmed();
            } else {
                qWarning() << "Could not find mariadb/mysql binary";
                return false;
            }
        }
    }
    
    QProcessResult verResult = Utils::executeProcess(mariadbPath, {"--version"}, 3000);
    if (verResult.exitCode != 0) {
        qWarning() << "Failed to get MariaDB version";
        return false;
    }
    
    QString verOut = verResult.stdOut + verResult.stdErr;
    if (!isMariaDBVersionSupported(verOut)) {
        qWarning() << "Version unsupported:" << verOut.left(100);
        return false;
    }
    
    
    bool serviceRunning = false;
    
    
    QProcessResult brewServicesResult = Utils::executeProcess(brewPath, {"services", "list"}, 5000);
    if (brewServicesResult.exitCode == 0) {
        QString servicesOutput = brewServicesResult.stdOut;
        if (servicesOutput.contains("mariadb") && servicesOutput.contains("started")) {
            serviceRunning = true;
            qDebug() << "MariaDB service is running (brew services)";
        }
    }
    
    
    if (!serviceRunning) {
        QProcessResult launchctlResult = Utils::executeProcess("launchctl", {"list", "homebrew.mxcl.mariadb"}, 3000);
        if (launchctlResult.exitCode == 0 && !launchctlResult.stdOut.contains("not found")) {
            serviceRunning = true;
            qDebug() << "MariaDB service is running (launchctl)";
        }
    }
    
    
    if (!serviceRunning) {
        qDebug() << "MariaDB service not running - attempting to start...";
        QProcessResult startResult = Utils::executeProcess(brewPath, {"services", "start", "mariadb"}, 10000);
        if (startResult.exitCode == 0) {
            serviceRunning = true;
            QThread::msleep(2000); 
            qDebug() << "MariaDB service started successfully";
        } else {
            qWarning() << "Failed to start MariaDB service:" << startResult.stdErr.trimmed();
        }
    }
    
    
    bool authOK = false;
    if (!m_wiz->rootPass.isEmpty()) {
        QProcessResult passResult = Utils::executeProcess(mariadbPath, {"-u", "root", "-p" + m_wiz->rootPass, "-e", "SELECT 1;"}, 5000);
        if (passResult.exitCode != 0) {
            
            QProcessResult noPassResult = Utils::executeProcess(mariadbPath, {"-u", "root", "--skip-password", "-e", "SELECT 1;"}, 5000);
            authOK = (noPassResult.exitCode == 0);
            if (authOK) {
                qDebug() << "Connected without password (fresh install or password not set)";
            }
        } else {
            authOK = true;
            qDebug() << "Connected with password";
        }
    } else {
        
        QProcessResult noPassResult = Utils::executeProcess(mariadbPath, {"-u", "root", "--skip-password", "-e", "SELECT 1;"}, 5000);
        authOK = (noPassResult.exitCode == 0);
        if (authOK) {
            qDebug() << "Connected without password (no password in wizard)";
        }
    }
    
    
    if (!authOK && serviceRunning) {
        
        QProcessResult emptyPassResult = Utils::executeProcess(mariadbPath, {"-u", "root", "-e", "SELECT 1;"}, 3000);
        authOK = (emptyPassResult.exitCode == 0);
        if (authOK) {
            qDebug() << "Connected with empty password";
        }
    }
    
    qDebug() << "macOS MariaDB verification result - homebrew: installed, version: OK, service:" 
             << (serviceRunning ? "running" : "not running") << ", auth:" << authOK;
    
    
    
    
    return serviceRunning || authOK;
}
