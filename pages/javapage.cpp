#include "javapage.h"
#include "../utils/concurrent.h"
#include "../utils/netutils.h"
#include "../utils/systemutils.h"
#include "../utils/utils.h"
#include <QDebug>
#include <QDir>
#include <QFont>
#include <QFrame>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPalette>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
JavaPage::JavaPage(QWidget *parent) : QWizardPage(parent) {
  setTitle("Java Setup");
  setSubTitle("Java 11 or higher is required. Install any version if needed.");
  lastInstallMode = KeepExisting;
  packageManagerAvailable = false;
  packageManagerInstalling = false;
  javaInstalled = false;
  installProcess = nullptr;

  QVBoxLayout *mainLayout = new QVBoxLayout(this);

  if (!m_nam) {
    m_nam = new QNetworkAccessManager(this);
  }

  packageManagerLabel = new QLabel("Checking package manager...");
  mainLayout->addWidget(packageManagerLabel);

  installPackageManagerButton = new QPushButton("Install Package Manager");
  installPackageManagerButton->setVisible(false);
  connect(installPackageManagerButton, &QPushButton::clicked, this, &JavaPage::onInstallPackageManager);
  mainLayout->addWidget(installPackageManagerButton);

  QFrame *separator = new QFrame();
  separator->setFrameShape(QFrame::HLine);
  separator->setFrameShadow(QFrame::Sunken);
  mainLayout->addWidget(separator);

  QLabel *versionLabel = new QLabel("Select Java Version:");
  mainLayout->addWidget(versionLabel);

  javaVersionComboBox = new QComboBox();
  mainLayout->addWidget(javaVersionComboBox);
  connect(javaVersionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &JavaPage::onJavaVersionChanged);

  recommendationLabel = new QLabel("✓ Recommended: Java 17 (LTS)\n"
                                   "  All Java LTS 11+ versions (11, 17, 21, 25) are tested and compatible");
  QFont recommendFont = recommendationLabel->font();
  recommendFont.setItalic(true);
  recommendationLabel->setFont(recommendFont);
  recommendationLabel->setStyleSheet("color: #f0f0f0ff; background-color: #171717; padding: 5px; "
                                     "border-radius: 3px;");
  mainLayout->addWidget(recommendationLabel);

  installModeGroup = new QGroupBox("Installation Mode");
  QVBoxLayout *modeLayout = new QVBoxLayout(installModeGroup);
  installNewRadio = new QRadioButton("Install Java");
  switchDefaultRadio = new QRadioButton("Switch to different Java version");
  installAdditionalRadio = new QRadioButton("Install additional Java version");

  modeLayout->addWidget(installNewRadio);
  modeLayout->addWidget(installAdditionalRadio);
  modeLayout->addWidget(switchDefaultRadio);

  installModeGroup->setVisible(false);
  mainLayout->addWidget(installModeGroup);

  statusLabel = new QLabel("Initializing...");

  progressBar = new QProgressBar();
  Utils::setProgressColor(progressBar, false);
  progressBar->setRange(0, 100);
  progressBar->setValue(0);
  progressBar->setVisible(false);

  installButton = new QPushButton("Install Java");
  installButton->setVisible(false);

  mainLayout->addWidget(statusLabel);
  mainLayout->addWidget(progressBar);
  mainLayout->addWidget(installButton, 0, Qt::AlignCenter);

  connect(installButton, &QPushButton::clicked, this, &JavaPage::installJava);

  statusUpdateTimer = new QTimer(this);
  statusUpdateTimer->setInterval(500);

  connect(statusUpdateTimer, &QTimer::timeout, this, [this]() {
    QString currentPhase = Utils::detectCurrentPhase(inDownloadPhase, accumulatedOutput);
    Utils::updateStatusFromOutput(accumulatedOutput, lastStatusText, statusLabel, currentPhase);
  });

  progressTimer = new QTimer(this);
  progressTimer->setInterval(5000);
  progressTimer->setSingleShot(false);

  connect(progressTimer, &QTimer::timeout, this, [this, lastValue = 0]() mutable {
    if (!installProcess || !installProcess->isOpen()) {
      return;
    }
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

  connect(installNewRadio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked) {
      if (javaInstalled) {
        installButton->setVisible(false);
        installButton->setText("Install Java");
      } else {
        installButton->setVisible(true);
        installButton->setText("Install Java");
      }
    }
  });

  connect(installAdditionalRadio, &QRadioButton::toggled, this, [this](bool checked) {
    installButton->setVisible(checked);
    if (checked) {
      installButton->setText("Install Additional Java");
    }
  });

  connect(switchDefaultRadio, &QRadioButton::toggled, this, [this](bool checked) {
    if (switchDefaultRadio->isEnabled()) {
      installButton->setVisible(checked);
      if (checked) {
        installButton->setText("Switch Default Java");
      }
    } else {
      installButton->setVisible(false);
    }
  });
}

JavaPage::~JavaPage() {
  qDebug() << "JavaPage - cleaning installProcess";
  if (installProcess && installProcess->state() != QProcess::NotRunning) {
    installProcess->kill();
  }
}

void JavaPage::initializePage() {
  Utils::updateStatus(statusLabel, "Checking system, please wait...", StatusType::Progress);
  progressBar->setVisible(false);
  installButton->setVisible(false);
  javaVersionComboBox->clear();
  installModeGroup->setVisible(false);
  packageManagerLabel->setVisible(true);

  m_wiz = qobject_cast<Wizard *>(wizard());
  if (!m_wiz)
    return;

  const OSInfo &osInfo = m_wiz->osInfo();

  packageManagerAvailable = osInfo.pkgManagerAvailable;
  QString pmName = osInfo.ptype;

  if (packageManagerAvailable) {
    packageManagerLabel->setText(QString("✓ Package manager detected: %1").arg(pmName));
    packageManagerLabel->setStyleSheet("color: green;");
    installPackageManagerButton->setVisible(false);
  } else {
    packageManagerLabel->setText("✗ " + pmName + " not found. Required to install Java and MariaDB.");
    packageManagerLabel->setStyleSheet("color: red;");
    installPackageManagerButton->setText("Install " + pmName);
    installPackageManagerButton->setVisible(true);
    Utils::setWizardButtons();
    javaVersionComboBox->setEnabled(false);
    javaVersionComboBox->clear();
    javaVersionComboBox->addItem("Install package manager first");
    installButton->setVisible(false);
  }

  if (m_wiz->isJavaDetectionComplete()) {
    updateJavaInfoFromWizard();
  } else {
    Utils::updateStatus(statusLabel, "Detecting Java...", StatusType::Progress);
    connect(m_wiz, &Wizard::javaDetectionComplete, this, &JavaPage::onWizardJavaDetectionComplete, Qt::UniqueConnection);
  }

  completeChanged();

  if (!packageManagerAvailable) {
    qDebug() << "setting buttons to false";
    Utils::setWizardButtons();
  }
}

void JavaPage::updateJavaInfoFromWizard() {
  javaInstalled = m_wiz->javaDetected();
  detectedJavaVersion = m_wiz->javaVersion();
  availablePackages = m_wiz->availableJavaPackages();

  refreshJavaInfo(true);
  completeChanged();
}

void JavaPage::onWizardJavaDetectionComplete() { updateJavaInfoFromWizard(); }

void JavaPage::setupInstallModes() {
  installModeGroup->setVisible(true);

  if (!javaInstalled) {
    installNewRadio->setChecked(true);
    installNewRadio->setEnabled(true);
    installAdditionalRadio->setEnabled(false);
    switchDefaultRadio->setEnabled(false);
    installButton->setText("Install Java");
    installButton->setVisible(true);
    installModeGroup->setTitle("Install Java");
    installNewRadio->setText("Install Java");
    Utils::setWizardButtons();
  } else {
    QString currentDefault = JavaUtils::getCurrentDefaultJava();
    Utils::setWizardButtons(true, true);
    int installedCount = 0;
    for (const JavaPackageInfo &pkg : availablePackages) {
      if (pkg.isInstalled)
        installedCount++;
    }

    installModeGroup->setTitle(QString("Java Management"));

    if (installedCount == 1) {
      installNewRadio->setEnabled(true);
      installNewRadio->setChecked(true);
      installNewRadio->setText(QString("✓ Keep using %1").arg(detectedJavaVersion));
      installAdditionalRadio->setEnabled(true);
      switchDefaultRadio->setEnabled(false);
      installButton->setVisible(false);
    } else {
      installNewRadio->setEnabled(true);
      installNewRadio->setChecked(true);
      installNewRadio->setText(QString("✓ Keep %1 as default").arg(detectedJavaVersion));
      installAdditionalRadio->setEnabled(true);
      switchDefaultRadio->setEnabled(true);
      switchDefaultRadio->setStyleSheet("");
      installButton->setVisible(false);
    }
  }
}

void JavaPage::onInstallPackageManager() {

  packageManagerInstalling = true;
  installPackageManagerButton->setEnabled(false);
  packageManagerLabel->setText("Installing package manager...");

  progressBar->setVisible(true);
  progressBar->setRange(0, 0);
  Utils::setProgressColor(progressBar, true);
  Utils::updateStatus(statusLabel, "Preparing Homebrew installation...", StatusType::Progress);

  QMessageBox infoBox(this);
  infoBox.setWindowTitle("Homebrew Installation");
  infoBox.setTextFormat(Qt::RichText);
  infoBox.setText("<html>"
                  "<h3>Install Homebrew Package Manager</h3>"
                  "<p>This application requires Homebrew to install Java and other dependencies.</p>"
                  "<p>The installation will:</p>"
                  "<ul>"
                  "<li>Download the official Homebrew installer</li>"
                  "<li>Request administrator privileges via a system dialog</li>"
                  "<li>Install to the standard location (<code>/usr/local</code> or <code>/opt/homebrew</code>)</li>"
                  "</ul>"
                  "<p>Click <b>Continue</b> to proceed.</p>"
                  "</html>");
  infoBox.setIcon(QMessageBox::Information);
  infoBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
  infoBox.button(QMessageBox::Ok)->setText("Continue");
  infoBox.button(QMessageBox::Cancel)->setText("Cancel");

  if (infoBox.exec() != QMessageBox::Ok) {
    progressBar->setVisible(false);
    packageManagerInstalling = false;
    installPackageManagerButton->setEnabled(true);
    installPackageManagerButton->setText("Install Package Manager");
    return;
  }

  QString password;
  bool passwordValid = false;
  int attempts = 0;
  const int maxAttempts = 3;

  while (attempts < maxAttempts && !passwordValid) {
    attempts++;

    QString message;
    QString iconType;

    if (attempts == 1) {
      message = "Homebrew installation requires administrator privileges.\\nPlease enter your password:";
      iconType = "caution";
    } else {
      message = QString("Incorrect password. Attempt %1 of %2.\\nPlease try again:").arg(attempts).arg(maxAttempts);
      iconType = "stop";
    }

    QString appleScript = QString("set dialogResult to display dialog \"%1\" "
                                  "default answer \"\" "
                                  "with hidden answer "
                                  "with title \"Authentication Required\" "
                                  "with icon %2 "
                                  "buttons {\"Cancel\", \"OK\"} "
                                  "default button \"OK\"")
                              .arg(message, iconType);

    QProcess osascript;
    osascript.start("osascript", {"-e", appleScript});

    if (!osascript.waitForStarted(5000) || !osascript.waitForFinished(15000)) {
      qWarning() << "Password dialog failed";
      break;
    }

    QString output = QString::fromUtf8(osascript.readAllStandardOutput()).trimmed();
    if (output.isEmpty() || osascript.exitCode() != 0) {

      progressBar->setVisible(false);
      packageManagerInstalling = false;
      installPackageManagerButton->setEnabled(true);
      installPackageManagerButton->setText("Install Package Manager");
      return;
    }

    QRegularExpression re("text returned:(.+)");
    QRegularExpressionMatch match = re.match(output);
    if (!match.hasMatch()) {
      qWarning() << "Could not parse password from AppleScript";
      break;
    }

    password = match.captured(1).trimmed();
    if (password.isEmpty()) {

      progressBar->setVisible(false);
      packageManagerInstalling = false;
      installPackageManagerButton->setEnabled(true);
      installPackageManagerButton->setText("Install Package Manager");
      return;
    }

    QString tempAskpassPath = QDir::tempPath() + "/validate_askpass_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".sh";
    QFile tempAskpassFile(tempAskpassPath);

    if (!tempAskpassFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
      qWarning() << "Failed to create validation askpass";
      break;
    }

    QString escapedPassword = password;
    escapedPassword.replace("'", "'\"'\"'");

    QString tempAskpassContent = QString("#!/bin/bash\n"
                                         "echo '%1'\n")
                                     .arg(escapedPassword);

    tempAskpassFile.write(tempAskpassContent.toUtf8());
    tempAskpassFile.close();
    tempAskpassFile.setPermissions(QFile::ExeOwner | QFile::ReadOwner | QFile::WriteOwner);

    QProcess sudoTest;
    sudoTest.setProcessChannelMode(QProcess::MergedChannels);

    QProcess::execute("sudo", {"-k"});

    sudoTest.start("/bin/bash", {"-c", QString("export SUDO_ASKPASS='%1'; sudo -A whoami 2>&1").arg(tempAskpassPath)});

    sudoTest.waitForFinished(5000);

    QFile::remove(tempAskpassPath);

    QString sudoOutput = QString::fromUtf8(sudoTest.readAll()).trimmed();
    qDebug() << "[VALIDATION] sudo output:" << sudoOutput << "Exit code:" << sudoTest.exitCode();

    if (sudoTest.exitCode() == 0 && sudoOutput.contains("root")) {
      passwordValid = true;
      qDebug() << "[INFO] Password validated with SUDO_ASKPASS on attempt" << attempts;
    } else {
      qDebug() << "[INFO] Password validation FAILED on attempt" << attempts;

      password.clear();

      if (attempts == maxAttempts) {
        QMessageBox errorBox(this);
        errorBox.setWindowTitle("Authentication Failed");
        errorBox.setText("Too many incorrect password attempts.");
        errorBox.setInformativeText("Please restart the installation if you want to try again.");
        errorBox.setIcon(QMessageBox::Critical);
        errorBox.exec();

        progressBar->setVisible(false);
        packageManagerInstalling = false;
        installPackageManagerButton->setEnabled(true);
        installPackageManagerButton->setText("Install Package Manager");
        return;
      }

      continue;
    }
  }

  if (!passwordValid || password.isEmpty()) {
    progressBar->setVisible(false);
    packageManagerInstalling = false;
    installPackageManagerButton->setEnabled(true);
    installPackageManagerButton->setText("Install Package Manager");
    return;
  }

  QString askpassPath = QDir::tempPath() + "/brew_askpass_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".sh";
  QFile askpassFile(askpassPath);
  if (!askpassFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    qWarning() << "[ERROR] Failed to create askpass script";
    progressBar->setVisible(false);
    packageManagerInstalling = false;
    installPackageManagerButton->setEnabled(true);
    installPackageManagerButton->setText("Install Package Manager");
    return;
  }

  QString escapedPassword = password;
  escapedPassword.replace("'", "'\"'\"'");

  QString askpassContent = QString("#!/bin/bash\n"
                                   "echo '%1'\n")
                               .arg(escapedPassword);

  askpassFile.write(askpassContent.toUtf8());
  askpassFile.close();
  askpassFile.setPermissions(QFile::ExeOwner | QFile::ReadOwner | QFile::WriteOwner);

  QString installScriptPath = QDir::tempPath() + "/brew_install_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".sh";
  QFile installFile(installScriptPath);
  if (!installFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    qWarning() << "[ERROR] Failed to create install script";
    QFile::remove(askpassPath);
    progressBar->setVisible(false);
    packageManagerInstalling = false;
    installPackageManagerButton->setEnabled(true);
    installPackageManagerButton->setText("Install Package Manager");
    return;
  }

  QString installContent = QString("#!/bin/bash\n"
                                   "set -e\n"
                                   "echo 'Downloading Homebrew installer...'\n"
                                   "curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh -o /tmp/brew_installer.sh\n"
                                   "chmod +x /tmp/brew_installer.sh\n"
                                   "echo 'Starting installation...'\n"
                                   "export SUDO_ASKPASS='%1'\n"
                                   "export SUDO_FORCE_REMOVE_ASKPASS=yes\n"
                                   "cd /tmp\n"
                                   "/tmp/brew_installer.sh\n"
                                   "INSTALL_RESULT=$?\n"
                                   "rm -f /tmp/brew_installer.sh\n"
                                   "exit $INSTALL_RESULT\n")
                               .arg(askpassPath);

  installFile.write(installContent.toUtf8());
  installFile.close();
  installFile.setPermissions(QFile::ExeOwner | QFile::ReadOwner | QFile::WriteOwner);

  password.clear();
  password.fill('*', 8);

  QProcess *process = new QProcess(this);
  process->setProcessChannelMode(QProcess::MergedChannels);

  Utils::updateStatus(statusLabel, "Starting Homebrew installation...", StatusType::Progress);
  qDebug() << "[INFO] Starting Homebrew installation with validated password...";
  process->start("/bin/bash", {installScriptPath});

  connect(process, &QProcess::readyRead, [this, process]() {
    QByteArray rawOutput = process->readAll();
    QString output = QString::fromUtf8(rawOutput).trimmed();

    if (!output.isEmpty()) {
      qDebug() << "[BREW INSTALL]:" << output;

      if (output.contains("Sorry, try again")) {
        qWarning() << "[WARNING] Sudo authentication failed during installation!";
      }

      if (output.contains("==>")) {
        QString displayMsg = output;
        displayMsg.remove(QRegularExpression("Password:.*"));
        if (!displayMsg.trimmed().isEmpty()) {
          Utils::updateStatus(statusLabel, displayMsg, StatusType::Progress);
        }
      }
    }
  });

  connect(process, &QProcess::finished, this, [this, process, askpassPath, installScriptPath](int exitCode, QProcess::ExitStatus) {
    QFile::remove(askpassPath);
    QFile::remove(installScriptPath);

    qDebug() << "[INFO] Installation finished. Exit code:" << exitCode;

    progressBar->setRange(0, 100);
    progressBar->setVisible(false);
    packageManagerInstalling = false;

    if (exitCode == 0) {

      Utils::updateStatus(statusLabel, "✓ Homebrew installed successfully!", StatusType::Success);
      QProcess pathProc;
      pathProc.start("bash", {"-l", "-c", "echo $PATH"});
      pathProc.waitForFinished(3000);
      QString newPath = QString::fromUtf8(pathProc.readAllStandardOutput()).trimmed();
      if (!newPath.isEmpty()) {
        qputenv("PATH", newPath.toUtf8());
      }

      OSInfo newInfo = detectOSInfo();
      qDebug() << "OS detectOSInfo: osType=" << newInfo.osType << ", pkgManagerAvailable=" << newInfo.pkgManagerAvailable << ", ptype=" << newInfo.ptype;
      const_cast<OSInfo &>(m_wiz->osInfo()) = newInfo;
      packageManagerAvailable = newInfo.pkgManagerAvailable;
      Utils::updateStatus(statusLabel, "Detecting Java...", StatusType::Progress);
      m_wiz->startJavaDetectionAsync();
      initializePage();
      javaVersionComboBox->setEnabled(true);

    } else {

      QString errorMsg;
      if (exitCode == 1) {
        errorMsg = "Homebrew installation failed.\n\n"
                   "The password was correct, but:\n"
                   "• Brew installer detected it was running as root\n"
                   "• Or another error occurred\n\n"
                   "Please try again or install Homebrew manually.";
      } else {
        errorMsg = QString("Installation failed with error code %1.").arg(exitCode);
      }

      QMessageBox errorBox(this);
      errorBox.setWindowTitle("Installation Failed");
      errorBox.setText(errorMsg);
      errorBox.setIcon(QMessageBox::Critical);
      errorBox.exec();

      packageManagerLabel->setText("✗ Installation failed");
      packageManagerLabel->setStyleSheet("color: red;");
      installPackageManagerButton->setEnabled(true);
      installPackageManagerButton->setText("Install Package Manager");
      Utils::updateStatus(statusLabel, "Installation failed.", StatusType::Error);
    }

    process->deleteLater();
  });

  connect(process, &QProcess::errorOccurred, this, [this, process, askpassPath, installScriptPath](QProcess::ProcessError error) {
    QFile::remove(askpassPath);
    QFile::remove(installScriptPath);

    progressBar->setVisible(false);
    packageManagerInstalling = false;
    installPackageManagerButton->setEnabled(true);
    installPackageManagerButton->setText("Install Package Manager");

    QMessageBox::critical(this, "Process Error", QString("Failed to start installation: %1").arg(process->errorString()));

    process->deleteLater();
  });
}

void JavaPage::onJavaVersionChanged(int index) {
  if (index >= 0 && index < availablePackages.size()) {
    setupInstallModes();

    InstallMode mode = getSelectedInstallMode();
    if (javaInstalled) {
      bool shouldShow = (mode == InstallAdditional || mode == SwitchDefault);
      installButton->setVisible(shouldShow);
    } else {
      installButton->setVisible(mode == KeepExisting);
    }
  }
}

QString JavaPage::getSelectedPackageName() {
  int index = javaVersionComboBox->currentIndex();
  if (index < 0) {
    return QString();
  }

  QVariant itemData = javaVersionComboBox->itemData(index);
  if (itemData.isValid() && itemData.canConvert<QString>()) {
    QString packageName = itemData.toString();
    if (!packageName.isEmpty()) {
      return packageName;
    }
  }

  if (index < availablePackages.size()) {
    return availablePackages[index].packageName;
  }

  return QString();
}

InstallMode JavaPage::getSelectedInstallMode() {
  if (installNewRadio->isChecked()) {
    return KeepExisting;
  }
  if (installAdditionalRadio->isChecked())
    return InstallAdditional;
  if (switchDefaultRadio->isChecked())
    return SwitchDefault;
  return KeepExisting;
}

bool JavaPage::isComplete() const { return javaInstalled && packageManagerAvailable; }

void JavaPage::switchDefaultJava() {
  QString selectedPackage = getSelectedPackageName();
  if (selectedPackage.isEmpty()) {
    QMessageBox::warning(this, "No Selection", "Please select a Java version to switch to.");
    return;
  }

  bool isInstalled = false;
  for (const JavaPackageInfo &pkg : availablePackages) {
    if (pkg.packageName == selectedPackage && pkg.isInstalled) {
      isInstalled = true;
      break;
    }
  }

  if (!isInstalled) {
    QMessageBox::warning(
        this, "Not Available",
        "The selected Java version is either not installed or installed recently. Please install it first or close and reopen the ANNE Wizard before switching.");
    return;
  }

  installButton->setEnabled(false);
  javaVersionComboBox->setEnabled(false);
  Utils::updateStatus(statusLabel, "Switching default Java version...", StatusType::Progress);
  progressBar->setVisible(true);
  progressBar->setRange(0, 0);

  Concurrent::withCallback(
      this,
      [this, selectedPackage]() {
        bool success = false;
        QString javaVersionOutput;

        try {
          const auto &info = static_cast<Wizard *>(wizard())->osInfo();
          success = JavaUtils::setJavaDefault(selectedPackage, info.pkgType);

          if (success) {
            QProcessResult result = Utils::executeProcess("java", {"-version"}, 10000);
            javaVersionOutput = result.stdOut + result.stdErr;
          }
        } catch (const std::exception &e) {
          qWarning() << "Exception in switchDefaultJava background thread:" << e.what();
          success = false;
        }

        return std::make_pair(success, javaVersionOutput);
      },
      [this, selectedPackage](const std::pair<bool, QString> &result) {
        bool success = result.first;
        QString javaVersionOutput = result.second;

        installButton->setEnabled(true);
        javaVersionComboBox->setEnabled(true);
        progressBar->setVisible(false);
        progressBar->setRange(0, 100);

        if (success) {
          Utils::updateStatus(statusLabel, "Default Java switched successfully!", StatusType::Success);
          QPalette pal = statusLabel->palette();
          pal.setColor(QPalette::WindowText, Qt::green);
          statusLabel->setPalette(pal);

          updateDefaultJavaMarker(selectedPackage);

          if (!javaVersionOutput.isEmpty()) {
            QRegularExpression re("(?:java|openjdk)\\s+version\\s+[\"']?([^\"'\\s]+)");
            QRegularExpressionMatch match = re.match(javaVersionOutput);
            if (match.hasMatch()) {
              QString fullVersion = match.captured(1);
              if (fullVersion.startsWith("1.")) {
                detectedJavaVersion = "Java 1." + fullVersion.mid(2).split('.').first();
              } else {
                detectedJavaVersion = "Java " + fullVersion.split('.').first();
              }
            }
          }

          setupInstallModes();
        } else {
          Utils::updateStatus(statusLabel, "Failed to switch default Java.", StatusType::Error);
        }
      });
}

void JavaPage::updateDefaultJavaMarker(const QString &defaultPackage) {
  QString currentDefault = JavaUtils::getCurrentDefaultJava();
  qDebug() << "Updating default marker, current default version:" << currentDefault;

  for (int i = 0; i < javaVersionComboBox->count(); i++) {
    QString oldText = javaVersionComboBox->itemText(i);
    if (oldText.contains("← Current Default")) {
      QString cleanText = oldText;
      cleanText.remove(" ← Current Default");
      javaVersionComboBox->setItemText(i, cleanText);
    }
  }

  for (int i = 0; i < javaVersionComboBox->count(); i++) {
    QVariant itemData = javaVersionComboBox->itemData(i);
    QString itemText = javaVersionComboBox->itemText(i);

    if (itemData.isValid()) {
      QString packageName = itemData.toString();
      QRegularExpression verRe("Temurin\\.(\\d+)");
      QRegularExpressionMatch match = verRe.match(packageName);
      if (match.hasMatch()) {
        QString packageVersion = match.captured(1);
        if (packageVersion == currentDefault) {
          QString newText = itemText;
          if (!newText.contains("← Current Default")) {
            newText += " ← Current Default";
            javaVersionComboBox->setItemText(i, newText);
            qDebug() << "Marked package" << packageName << "as default";
          }
          break;
        }
      }
    }
  }
}

bool JavaPage::validatePage() {
  if (javaInstalled) {
    return true;
  }

  InstallMode mode = getSelectedInstallMode();
  if (mode == KeepExisting && installButton->isVisible()) {
    QMessageBox::warning(this, "Java Required", "Please install Java before continuing.");
    return false;
  }

  QMessageBox::warning(this, "Java Required", "Java 11 or higher is required. Please install Java before continuing.");
  return false;
}

void JavaPage::installJava() {
  InstallMode mode = getSelectedInstallMode();
  if (mode == SwitchDefault) {
    switchDefaultJava();
    return;
  }

  if (!javaInstalled && mode == KeepExisting) {
    mode = InstallNew;
  }

  if (mode == KeepExisting) {
    qDebug() << "KeepExisting mode selected - no installation needed";
    return;
  }

  if (!packageManagerAvailable) {
    QMessageBox::warning(this, "Package Manager Required", "Please install the package manager first.");
    return;
  }

  if (availablePackages.isEmpty()) {
    QMessageBox::warning(this, "No Java Versions", "No Java versions available. Please check your package manager.");
    return;
  }

  QString selectedPackage = getSelectedPackageName();
  if (selectedPackage.isEmpty()) {
    QMessageBox::warning(this, "No Selection", "Please select a Java version to install.");
    return;
  }

  cachedJavaPackage = selectedPackage;
  Utils::setWizardButtons();

  installButton->setVisible(false);
  progressBar->setRange(0, 100);
  progressBar->setValue(0);
  progressBar->setVisible(true);

  QPalette pal = statusLabel->palette();
  pal.setColor(QPalette::WindowText, QColor("#FEFE17"));
  statusLabel->setPalette(pal);

  downloadProgress = 1.0;
  downloadLineCount = 0;
  downloadFileCount = 0;
  completedFiles = 0;
  totalDownloadBytes = 0;
  lastProcessedLine = "";
  inDownloadPhase = false;
  accumulatedOutput = "";
  lastStatusText = "";

  Utils::animateProgress(progressBar, 1);
  Utils::updateStatus(statusLabel, QString("Installing %1...").arg(javaVersionComboBox->currentText().split(' ').first()), StatusType::Progress);
  statusUpdateTimer->start();
  progressTimer->start();

  QTimer::singleShot(100, this, [this, pal, mode]() mutable { continueJavaInstallation(pal, mode); });
}

void JavaPage::continueJavaInstallation(QPalette pal, InstallMode mode) {
  statusLabel->setPalette(pal);
  if (installProcess) {
    installProcess->kill();
    installProcess->deleteLater();
  }

  installProcess = new QProcess(this);
  installProcess->setProcessChannelMode(QProcess::MergedChannels);

  connect(installProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
    qDebug() << "Install process finished, exit code:" << exitCode;
    bool success = (exitCode == 0);

#ifdef Q_OS_WIN
    if (exitCode == 3010) {
      success = true;
      qDebug() << "Windows MSI installation succeeded (restart required)";
    }

    if (!msiTempPath.isEmpty() && QFile::exists(msiTempPath)) {
      QFile::remove(msiTempPath);
      msiTempPath.clear();
      qDebug() << "Cleaned up temporary MSI file";
    }
#endif

    onInstallFinished(success);
  });

  connect(installProcess, &QProcess::readyReadStandardOutput, this, &JavaPage::readInstallOutput);
  connect(installProcess, &QProcess::readyReadStandardError, this, [this]() {
    QString err = installProcess->readAllStandardError();
    if (!err.isEmpty()) {
      qDebug() << "[ERROR] Process stderr:" << err;
      err = Utils::cleanPackageOutput(err);
      accumulatedOutput += err;
    }
  });

  QString osName = QSysInfo::productType();
  const auto &info = static_cast<Wizard *>(wizard())->osInfo();

  QString selectedPackage = cachedJavaPackage;
  if (selectedPackage.isEmpty()) {
    Utils::updateStatus(statusLabel, "No Java version selected!", StatusType::Error);
    installButton->setVisible(true);
    return;
  }

  packageSizeBytes = Utils::getPackageSize(selectedPackage, info.pkgType);
  downloadStartTime = QTime::currentTime();

  if (packageSizeBytes > 0) {
    qDebug() << "Package size:" << packageSizeBytes << "bytes";
    totalDownloadBytes = packageSizeBytes;
  }

  qDebug() << "continueJavaInstallation called with mode:" << mode;
  InstallMode currentMode = getSelectedInstallMode();
  qDebug() << "Current actual mode:" << currentMode;

  lastInstallMode = currentMode;
  bool needSetDefault = (mode == InstallAdditional && javaInstalled) || !javaInstalled;

  if (osName.contains("windows", Qt::CaseInsensitive)) {
    handleWindowsInstall(selectedPackage, mode);
    return;
  } else if (osName.contains("macos", Qt::CaseInsensitive) || osName == "osx") {
    handleMacInstall(selectedPackage, mode);
    return;
  } else {
    if (!info.isLinux || info.pkgType == PkgManagerType::Unknown) {
      Utils::updateStatus(statusLabel, "Unsupported Linux distro", StatusType::Error);
      installButton->setVisible(true);
      return;
    }
  }

  Utils::updateStatus(statusLabel, "Starting Java installation...", StatusType::Progress);

  bool needReinstall = false;
  if (info.pkgType == PkgManagerType::Apt) {
    QProcessResult checkResult = Utils::executeProcess("dpkg-query", QStringList() << "-W" << "-f=${Status}\n" << selectedPackage, 60000);

    if (checkResult.exitCode == 0) {
      QString status = checkResult.stdOut.trimmed();
      if (status.contains("config-files") || status.contains("deinstall") || !status.contains("install ok")) {
        needReinstall = true;
        qDebug() << "Detected broken Java state → using --reinstall";
      }
    }
  }

  QString baseCmd;
  if (info.pkgType == PkgManagerType::Apt) {
    QString reinstall = needReinstall ? "--reinstall " : "";
    baseCmd = QString("apt update -qq || true && "
                      "DEBIAN_FRONTEND=noninteractive "
                      "apt install -y --no-install-recommends "
                      "-o Dpkg::Options::=--force-confdef "
                      "-o Dpkg::Options::=--force-confold "
                      "%1%2")
                  .arg(reinstall, selectedPackage);
  } else if (info.pkgType == PkgManagerType::Dnf) {
    baseCmd = QString("dnf install -y %1").arg(selectedPackage);
  } else if (info.pkgType == PkgManagerType::Pacman) {
    baseCmd = QString("pacman -Syu --noconfirm %1").arg(selectedPackage);
  } else {
    baseCmd = Utils::getPlatformCommand(info.pkgType, selectedPackage);
  }

  QString finalInstallCmd = baseCmd;
  Utils::fixLock(statusLabel, info.pkgType);
  QString fullCmd = finalInstallCmd;

  installProcess->setProgram("pkexec");
  installProcess->setArguments({"/bin/sh", "-c", fullCmd});

  qDebug() << "Starting Java install via pkexec /bin/sh -c";
  qDebug() << "Full command:" << fullCmd;
  qDebug() << "Selected package:" << selectedPackage;
  qDebug() << "Install mode:" << mode;

  installProcess->start();
  Utils::updateStatus(statusLabel, "Updating package lists...", StatusType::Progress);

  statusUpdateTimer->start();
  progressTimer->stop();
  downloadProgress = 0.0;
  downloadLineCount = 0;
  downloadFileCount = 0;
  lastProcessedLine = "";
  totalDownloadBytes = 0;
  inDownloadPhase = false;
  accumulatedOutput = "";
  lastStatusText = "";

  QString initialPhase = Utils::detectCurrentPhase(inDownloadPhase, accumulatedOutput);
  Utils::updateStatusFromOutput(accumulatedOutput, lastStatusText, statusLabel, initialPhase);
  qDebug() << "Installation started — timers active with initial phase:" << initialPhase;
}

void JavaPage::handleWindowsInstall(const QString &packageName, InstallMode mode) {

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

  QRegularExpression verRe(R"(Temurin\.(\d+))", QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatch verMatch = verRe.match(packageName);
  QString version = verMatch.hasMatch() ? verMatch.captured(1) : "21";
  QString packageId = QString("EclipseAdoptium.Temurin.%1.JDK").arg(version);

  Utils::updateStatus(statusLabel, QString("Querying package details for Java %1...").arg(version), StatusType::Progress);
  progressBar->setValue(0);
  progressBar->setVisible(true);

  QProcessResult showResult = Utils::executeProcess("winget", {"show", "--id", packageId, "--accept-source-agreements", "--disable-interactivity", "--nowarn"}, 60000);

  qDebug() << "Winget show result exit code:" << showResult.exitCode;

  if (showResult.exitCode != 0) {
    Utils::updateStatus(statusLabel, QString("Winget failed: exit code %1").arg(showResult.exitCode), StatusType::Error);
    progressBar->setVisible(false);
    installButton->setVisible(true);
    return;
  }

  QRegularExpression urlRe(R"(Installer\s+Url\s*:\s*(https?://[^\s]+?\.msi))", QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
  QRegularExpressionMatch match = urlRe.match(showResult.stdOut + showResult.stdErr);

  if (!match.hasMatch()) {
    Utils::updateStatus(statusLabel, "Could not locate Java URL", StatusType::Error);
    progressBar->setVisible(false);
    installButton->setVisible(true);
    return;
  }

  QString installerUrl = match.captured(1);
  qDebug() << "Found MSI URL:" << installerUrl;

  QString tempDir = QDir::tempPath();
  QString safeFileName = QString("Temurin-JDK-%1-%2.msi").arg(version).arg(QDateTime::currentSecsSinceEpoch());
  msiTempPath = QDir(tempDir).absoluteFilePath(safeFileName);

  downloadFile.setFileName(msiTempPath);
  if (!downloadFile.open(QIODevice::WriteOnly)) {
    Utils::updateStatus(statusLabel, "Failed to create temporary MSI file", StatusType::Error);
    progressBar->setVisible(false);
    installButton->setVisible(true);
    return;
  }

  QUrl downloadUrl(installerUrl);
  QNetworkRequest req = NetUtils::createSslConfiguredRequest(downloadUrl, m_nam, 1800000);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

  QNetworkReply *reply = m_nam->get(req);
  currentReply = reply;

  Utils::updateStatus(statusLabel, QString("Downloading Java %1 JDK...").arg(version), StatusType::Progress);
  inDownloadPhase = true;
  progressBar->setValue(5);

  connect(currentReply, &QNetworkReply::downloadProgress, this, [this, version](qint64 bytesReceived, qint64 bytesTotal) {
    if (bytesTotal <= 0)
      return;

    double mbReceived = bytesReceived / (1024.0 * 1024.0);
    double mbTotal = bytesTotal / (1024.0 * 1024.0);

    int percent = qRound((bytesReceived * 100.0) / bytesTotal);
    progressBar->setValue(qMin(10 + (percent * 80 / 100), 90));

    QString receivedStr = QString::number(mbReceived, 'f', 1);
    QString totalStr = QString::number(mbTotal, 'f', 1);
    Utils::updateStatus(statusLabel, QString("Downloading Java %1... %2 MB / %3 MB").arg(version).arg(receivedStr).arg(totalStr), StatusType::Progress);
  });

  connect(currentReply, &QNetworkReply::readyRead, this, [this]() {
    if (currentReply) {
      QByteArray chunk = currentReply->readAll();
      if (!chunk.isEmpty() && downloadFile.write(chunk) == -1) {
        currentReply->abort();
      }
    }
  });

  connect(currentReply, &QNetworkReply::finished, this, [this, version]() {
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

    QProcessResult unblockResult =
        Utils::executeProcess("powershell", {"-NoProfile", "-Command", QString("Unblock-File -Path '%1'").arg(msiTempPath.replace("'", "''"))}, 8000);

    Utils::updateStatus(statusLabel, QString("Installing Java %1...").arg(version), StatusType::Progress);
    progressBar->setValue(92);

    QString nativePath = QDir::toNativeSeparators(msiTempPath);
    QString logPath = QDir::tempPath() + "/Temurin-install-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".log";

    QStringList msiArgs = {"/i",
                           nativePath,
                           "/quiet",
                           "/norestart",
                           "ADDLOCAL=FeatureMain,FeatureEnvironment,"
                           "FeatureJarFileRunWith,FeatureJavaHome",
                           "/L*v",
                           logPath};

    installProcess->start("msiexec", msiArgs);
    currentReply->deleteLater();
    currentReply = nullptr;
  });
}

void JavaPage::handleMacInstall(const QString &packageName, InstallMode mode) {
  qDebug() << "[DEBUG] handleMacInstall called for package:" << packageName;

  Utils::updateStatus(statusLabel, "Preparing installation...", StatusType::Progress);
  progressBar->setValue(20);

  QString brewPath = SystemUtils::findBrew();
  if (brewPath.isEmpty()) {
    qDebug() << "[DEBUG] Homebrew not found";
    showManualMacInstructions();
    return;
  }

  QString askpassScriptPath = QDir::tempPath() + "/brew_askpass_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".sh";
  QFile askpassScript(askpassScriptPath);
  if (askpassScript.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QString scriptContent =
        QString("#!/bin/bash\n"
                "# Askpass script for Homebrew's internal sudo\n"
                "osascript -e 'tell application \"System Events\"' \\\n"
                "          -e 'set answer to text returned of (display dialog \"This Java installation requires administrator privileges.\" & return & \"Please enter "
                "your password:\" default answer \"\" with hidden answer buttons {\"Cancel\", \"OK\"} default button 2)' \\\n"
                "          -e 'return answer' \\\n"
                "          -e 'end tell' 2>/dev/null\n");
    askpassScript.write(scriptContent.toUtf8());
    askpassScript.close();
    askpassScript.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    qDebug() << "[DEBUG] Created askpass script:" << askpassScriptPath;
  }

  QString wrapperScriptPath = QDir::tempPath() + "/brew_wrapper_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".sh";
  QFile wrapperScript(wrapperScriptPath);
  if (wrapperScript.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QString wrapperContent = QString("#!/bin/bash\n"
                                     "# Wrapper to run brew with askpass support for internal sudo\n"
                                     "export SUDO_ASKPASS=\"%1\"\n"
                                     "export HOMEBREW_NO_AUTO_UPDATE=1\n"
                                     "export HOMEBREW_NO_ENV_HINTS=1\n"
                                     "export HOMEBREW_NO_INSTALL_CLEANUP=1\n"
                                     "\n"
                                     "# Clear any cached sudo credentials to force use of askpass\n"
                                     "sudo -k\n"
                                     "\n"
                                     "# Run brew install as the current user\n"
                                     "echo \"[INFO] Starting Homebrew installation...\"\n"
                                     "\n"
                                     "# CRITICAL: Use 'script' to fake a TTY and force brew to show progress\n"
                                     "# -q for quiet, /dev/null as output file, 2>&1 to capture all output\n"
                                     "script -q /dev/null \"%2\" install --cask %3 --force --verbose 2>&1\n"
                                     "\n"
                                     "EXIT_CODE=$?\n"
                                     "if [ $EXIT_CODE -eq 0 ]; then\n"
                                     "    echo \"[SUCCESS] Installation completed\"\n"
                                     "else\n"
                                     "    echo \"[ERROR] Installation failed with exit code: $EXIT_CODE\"\n"
                                     "fi\n"
                                     "exit $EXIT_CODE\n")
                                 .arg(askpassScriptPath)
                                 .arg(brewPath)
                                 .arg(packageName);

    wrapperScript.write(wrapperContent.toUtf8());
    wrapperScript.close();
    wrapperScript.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    qDebug() << "[DEBUG] Created wrapper script:" << wrapperScriptPath;
  }

  installProcess->setProgram("/bin/bash");
  installProcess->setArguments({"-c", QString("\"%1\"").arg(wrapperScriptPath)});
  installProcess->setWorkingDirectory(QDir::homePath());

  connect(installProcess, &QProcess::finished, this, [askpassScriptPath, wrapperScriptPath](int exitCode, QProcess::ExitStatus) {
    qDebug() << "[DEBUG] Installation process finished. Cleaning up temp scripts.";
    QFile::remove(askpassScriptPath);
    QFile::remove(wrapperScriptPath);
  });

  qDebug() << "[DEBUG] Starting Homebrew installation via wrapper...";
  installProcess->start();

  if (!installProcess->waitForStarted(5000)) {
    qCritical() << "[ERROR] Process failed to start:" << installProcess->errorString();
    Utils::updateStatus(statusLabel, "Error: Failed to start installation", StatusType::Error);
    QFile::remove(askpassScriptPath);
    QFile::remove(wrapperScriptPath);
    return;
  }

  qDebug() << "[DEBUG] Installation process started successfully";
}

void JavaPage::showManualMacInstructions() {
  Utils::updateStatus(statusLabel, "Homebrew not available", StatusType::Warning);
  Utils::animateProgress(progressBar, 100);

  QMessageBox::information(this, "Manual Installation Required",
                           "Please install Java manually:\n\n"
                           "Option 1: Install Homebrew first:\n"
                           "  /bin/bash -c \"$(curl -fsSL "
                           "https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\"\n"
                           "  Then run: brew install temurin\n\n"
                           "Option 2: Download directly from:\n"
                           "  https://adoptium.net/");

  QMetaObject::invokeMethod(this, "onInstallFinished", Qt::QueuedConnection, Q_ARG(bool, false));
}

void JavaPage::readInstallOutput() {
  if (!installProcess)
    return;

  QString data = installProcess->readAllStandardOutput();
  if (data.isEmpty())
    return;

  data = Utils::cleanPackageOutput(data);
  if (data.isEmpty())
    return;

  accumulatedOutput += data;
  const auto &info = static_cast<Wizard *>(wizard())->osInfo();

  Utils::parseInstallOutput(data, inDownloadPhase, downloadProgress, downloadLineCount, downloadFileCount, completedFiles, totalDownloadBytes, lastProcessedLine,
                            progressBar, progressTimer, info.pkgType);

  QString currentPhase = Utils::detectCurrentPhase(inDownloadPhase, accumulatedOutput);
  Utils::updateStatusFromOutput(accumulatedOutput, lastStatusText, statusLabel, currentPhase);
}

void JavaPage::refreshJavaInfo(bool updateLabel) {
  javaInstalled = m_wiz->javaDetected();
  detectedJavaVersion = m_wiz->javaVersion();
  availablePackages = m_wiz->availableJavaPackages();
  JavaCheckResult javaStatus = m_wiz->javaCheckResult();

  javaVersionComboBox->clear();

  if (availablePackages.isEmpty()) {
    javaVersionComboBox->addItem("No Java versions found");
    javaVersionComboBox->setEnabled(false);
    installButton->setVisible(false);

    if (updateLabel) {
      if (javaStatus.isJavaAvailable && !javaStatus.isCompleteJDK) {
        Utils::updateStatus(statusLabel, "Java found but incomplete (missing JDK). Please install full JDK.", StatusType::Warning);
      } else {
        Utils::updateStatus(statusLabel, "No available Java versions detected", StatusType::Error);
      }
    }
    return;
  }

  QList<JavaPackageInfo> installedVersions;
  QList<JavaPackageInfo> availableVersions;

  for (const JavaPackageInfo &pkg : availablePackages) {
    if (pkg.isInstalled) {
      installedVersions.append(pkg);
    } else {
      availableVersions.append(pkg);
    }
  }

  std::sort(installedVersions.begin(), installedVersions.end(), [](const JavaPackageInfo &a, const JavaPackageInfo &b) { return a.majorVersion > b.majorVersion; });

  std::sort(availableVersions.begin(), availableVersions.end(), [](const JavaPackageInfo &a, const JavaPackageInfo &b) { return a.majorVersion > b.majorVersion; });

  if (!installedVersions.isEmpty()) {
    QString currentDefault = JavaUtils::getCurrentDefaultJava();
    qDebug() << "Current default Java major version:" << currentDefault;

    for (const JavaPackageInfo &pkg : installedVersions) {
      QString displayText = QString("✓ %1").arg(pkg.displayName);

      if (!currentDefault.isEmpty() && QString::number(pkg.majorVersion) == currentDefault) {
        displayText += " ← Current Default";
        qDebug() << "Marking Java" << pkg.majorVersion << "as default";
      }
      javaVersionComboBox->addItem(displayText, pkg.packageName);
    }

    if (!availableVersions.isEmpty()) {
      javaVersionComboBox->insertSeparator(installedVersions.size());
    }
  }

  for (const JavaPackageInfo &pkg : availableVersions) {
    javaVersionComboBox->addItem(pkg.displayName, pkg.packageName);
  }

  int selectIndex = 0;
  QString currentDefault = JavaUtils::getCurrentDefaultJava();
  if (!currentDefault.isEmpty()) {
    for (int i = 0; i < javaVersionComboBox->count(); i++) {
      if (javaVersionComboBox->itemText(i).contains("Current Default")) {
        selectIndex = i;
        break;
      }
    }
  }
  javaVersionComboBox->setCurrentIndex(selectIndex);

  if (updateLabel) {
    QString labelText;
    StatusType labelType = StatusType::Progress;

    if (!installedVersions.isEmpty()) {
      labelText = QString("%1 installed").arg(detectedJavaVersion);
      if (!currentDefault.isEmpty() && javaInstalled) {
        labelText += " and set as default";
        labelType = StatusType::Success;
      }
    } else if (javaStatus.isJavaAvailable && javaStatus.majorVersion >= 11 && !javaStatus.isCompleteJDK) {
      labelText = QString("Java %1 found but incomplete (install full JDK)").arg(javaStatus.version);
      labelType = StatusType::Warning;
    } else if (javaStatus.isJavaAvailable && javaStatus.majorVersion < 11) {
      labelText = QString("Java %1 found but version too old (need 11+)").arg(javaStatus.version);
      labelType = StatusType::Warning;
    } else if (javaStatus.isJavaAvailable) {
      labelText = QString("Java %1 found but not recognized as JDK - annode may not "
                          "run. Please install full version using tools provided.")
                      .arg(javaStatus.version);
      labelType = StatusType::Warning;
    } else {
      labelType = StatusType::Normal;
      labelText = "Click to install the selected Java version.";
    }

    Utils::updateStatus(statusLabel, labelText, labelType);
  }

  setupInstallModes();
  completeChanged();
}

void JavaPage::updateUIAfterInstall(const QString &installedPackage, bool setAsDefault) {
  qDebug() << "Updating UI after installing:" << installedPackage;
  qDebug() << "Should set as default?" << setAsDefault;

  installButton->setEnabled(false);
  javaVersionComboBox->setEnabled(false);
  Utils::updateStatus(statusLabel, "Finalizing Java installation...", StatusType::Progress);
  progressBar->setVisible(true);
  progressBar->setRange(0, 0);

  Concurrent::withCallback(
      this,
      [this, installedPackage, setAsDefault]() {
        bool defaultSet = false;
        bool javaFound = false;
        QString detectedVersion;

        try {
          if (setAsDefault) {
            const auto &info = static_cast<Wizard *>(wizard())->osInfo();
            defaultSet = JavaUtils::setJavaDefault(installedPackage, info.pkgType);
            qDebug() << "setJavaDefault result:" << defaultSet;
          }

          QProcessResult result = Utils::executeProcess("java", {"-version"}, 3000);
          QString javaVersionOutput = result.stdOut + result.stdErr;

          QRegularExpression re("(?:java|openjdk)\\s+version\\s+[\"']?([^\"'\\s]+)");
          QRegularExpressionMatch match = re.match(javaVersionOutput);
          if (match.hasMatch()) {
            QString fullVersion = match.captured(1);
            if (fullVersion.startsWith("1.")) {
              detectedVersion = "Java 1." + fullVersion.mid(2).split('.').first();
            } else {
              detectedVersion = "Java " + fullVersion.split('.').first();
            }
            javaFound = true;
            qDebug() << "Updated detectedJavaVersion to:" << detectedVersion;
          }

        } catch (const std::exception &e) {
          qWarning() << "Exception in updateUIAfterInstall:" << e.what();
        }

        struct InstallResult {
          bool defaultSet;
          bool javaFound;
          QString detectedVersion;
        };

        return InstallResult{defaultSet, javaFound, detectedVersion};
      },
      [this, installedPackage, setAsDefault](const auto &result) {
        bool defaultSet = result.defaultSet;
        bool javaFound = result.javaFound;
        QString detectedVersion = result.detectedVersion;

        if (javaFound) {
          detectedJavaVersion = detectedVersion;
          javaInstalled = true;
        }

        QRegularExpression verRe("(\\d+)");
        QRegularExpressionMatch verMatch = verRe.match(installedPackage);
        QString versionNum = verMatch.hasMatch() ? verMatch.captured(1) : "?";

        bool foundInCombo = false;
        for (int i = 0; i < javaVersionComboBox->count(); i++) {
          QString itemText = javaVersionComboBox->itemText(i);
          QVariant itemData = javaVersionComboBox->itemData(i);

          if ((itemData.isValid() && itemData.toString() == installedPackage) || itemText.contains(installedPackage)) {
            QString newText = itemText;
            if (!itemText.startsWith("✓")) {
              newText = "✓ " + itemText;
            }

            QString currentDefault = JavaUtils::getCurrentDefaultJava();
            if (!currentDefault.isEmpty() && setAsDefault && defaultSet) {
              for (int j = 0; j < javaVersionComboBox->count(); j++) {
                QString oldText = javaVersionComboBox->itemText(j);
                if (oldText.contains("← Current Default")) {
                  QString cleanText = oldText;
                  cleanText.remove(" ← Current Default");
                  javaVersionComboBox->setItemText(j, cleanText);
                }
              }

              if (!newText.contains("← Current Default")) {
                newText += " ← Current Default";
              }
            }

            javaVersionComboBox->setItemText(i, newText);
            foundInCombo = true;
            break;
          }
        }

        if (!foundInCombo) {
          QString displayName = QString("Java %1").arg(versionNum);
          QString itemText = QString("✓ %1").arg(displayName);

          if (setAsDefault && defaultSet) {
            itemText += " ← Current Default";
          }

          int insertIndex = 0;
          for (int i = 0; i < javaVersionComboBox->count(); i++) {
            if (javaVersionComboBox->itemText(i).startsWith("✓")) {
              insertIndex = i + 1;
            }
          }

          javaVersionComboBox->insertItem(insertIndex, itemText, installedPackage);
        }

        QString statusText;
        if (setAsDefault && defaultSet) {
          statusText = QString("✓ Java %1 installed and set as default").arg(versionNum);
        } else if (javaFound) {
          statusText = QString("✓ Java %1 installed (not default)").arg(versionNum);
        } else {
          statusText = "Java installation completed but version check failed";
        }

        Utils::updateStatus(statusLabel, statusText, StatusType::Success);
        QPalette pal = statusLabel->palette();
        pal.setColor(QPalette::WindowText, Qt::green);
        statusLabel->setPalette(pal);

        setupInstallModes();
        installButton->setVisible(false);
        installButton->setEnabled(true);
        javaVersionComboBox->setEnabled(true);
        progressBar->setVisible(false);
        progressBar->setRange(0, 100);

        emit completeChanged();

        qDebug() << "UI update completed for:" << installedPackage;
        qDebug() << "javaInstalled state:" << javaInstalled;
      });
}
void JavaPage::onInstallFinished(bool success) {
  qDebug() << "=== onInstallFinished ===";
  qDebug() << "Success:" << success;
  qDebug() << "Cached package:" << cachedJavaPackage;

  statusUpdateTimer->stop();
  progressTimer->stop();

  if (installProcess) {
    installProcess->deleteLater();
    installProcess = nullptr;
  }

  progressBar->setValue(100);

  if (success) {

    updateUIAfterInstall(cachedJavaPackage, true);
  } else {

    Utils::updateStatus(statusLabel, "Installation failed", StatusType::Error);
    installButton->setVisible(true);
    installButton->setText("Retry Installation");
    installButton->setEnabled(true);

    Utils::setWizardButtons(false, true);
  }
}
