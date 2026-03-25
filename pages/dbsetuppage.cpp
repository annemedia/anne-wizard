#include "dbsetuppage.h"
#include "../utils/antorutils.h"
#include "../utils/concurrent.h"
#include "../utils/dbmanager.h"
#include "../utils/filehandler.h"
#include "../utils/systemutils.h"
#include "../utils/utils.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMetaObject>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

DBSetupPage::DBSetupPage(QWidget *parent) : QWizardPage(parent) {
  setTitle("Database Setup");
  setSubTitle("Configuring the MariaDB with annechain dataset import.");
  antorUtils = nullptr;
  currentImportCancelled = nullptr;
  currentNid = "";
  m_localZipPath = "";
  downloadReady = false;
  statusLabel = new QLabel("Initializing...");
  statusLabel->setWordWrap(true);

  progressBar = new QProgressBar();
  Utils::setProgressColor(progressBar, false);
  progressBar->setRange(0, 100);
  progressBar->setValue(0);
  progressBar->setVisible(false);

  noteLabel = new QLabel("Please copy the database root password for future reference:");
  noteLabel->setWordWrap(true);

  rootPassEdit = new QLineEdit();
  rootPassEdit->setEchoMode(QLineEdit::Password);
  rootPassEdit->setPlaceholderText("Enter database root password");

  eyeButton = new QPushButton("👁️");
  eyeButton->setFixedSize(30, 30);
  eyeButton->setCheckable(true);

  auto *passLayout = new QHBoxLayout();
  passLayout->addWidget(rootPassEdit);
  passLayout->addWidget(eyeButton);
  passLayout->setContentsMargins(0, 0, 0, 0);

  copyButton = new QPushButton("Copy Password");
  confirmButton = new QPushButton("Confirm Import");
  skipButton = new QPushButton("Skip Import");

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->addWidget(statusLabel);
  mainLayout->addWidget(progressBar);
  mainLayout->addWidget(noteLabel);
  mainLayout->addLayout(passLayout);
  mainLayout->addWidget(copyButton);
  mainLayout->addWidget(confirmButton);
  mainLayout->addWidget(skipButton);

  connect(copyButton, &QPushButton::clicked, this, &DBSetupPage::copyPassword);
  connect(confirmButton, &QPushButton::clicked, this, &DBSetupPage::confirmAndImport);
  connect(skipButton, &QPushButton::clicked, this, &DBSetupPage::skipImport);

  copyButton->setEnabled(true);
  skipButton->setEnabled(true);
  confirmButton->setEnabled(true);

  noteLabel->setVisible(true);
  rootPassEdit->setVisible(true);
  eyeButton->setVisible(true);
  copyButton->setVisible(true);
  confirmButton->setVisible(true);
  skipButton->setVisible(true);
}

DBSetupPage::~DBSetupPage() { cleanupPage(); }

void DBSetupPage::initializePage() {
  qDebug() << "DBSetupPage::initializePage() called";

  if (antorUtils) {
    antorUtils->cancelCurrentDownload();
    antorUtils->deleteLater();
    antorUtils = nullptr;
  }

  Concurrent::cancelAll();
  currentImportCancelled = nullptr;
  m_wiz = qobject_cast<Wizard *>(wizard());
  if (!m_wiz)
    return;

  Utils::updateStatus(statusLabel, "Initializing...", StatusType::Progress);
  progressBar->setVisible(false);
  progressBar->setValue(0);

  dbSetupDone = false;
  forceComplete = false;
  downloadReady = false;
  currentState = ImportState::None;
  zipData.clear();
  m_localZipPath = "";
  currentNid = "";

  rootPassEdit->setText("");
  rootPassEdit->setReadOnly(false);
  eyeButton->setChecked(false);
  eyeButton->setText("👁️");

  copyButton->setEnabled(true);
  confirmButton->setEnabled(true);
  skipButton->setEnabled(true);

  confirmButton->setText("Import Dataset");

  isFresh = m_wiz->isFreshInstall && !m_wiz->rootPass.isEmpty();
  if (!isFresh) {
    copyButton->setVisible(false);
    noteLabel->setVisible(false);
  } else {
    copyButton->setVisible(true);
    noteLabel->setVisible(true);
  }

  if (!m_wiz->rootPass.isEmpty()) {
    rootPassEdit->setText(m_wiz->rootPass);
    qDebug() << "Pre-filled existing password";
  }

  antorUtils = new AntorUtils(this);

  connect(antorUtils, &AntorUtils::statusMessage, this, [this](const QString &msg) { Utils::updateStatus(statusLabel, msg, StatusType::Progress); });

  connect(antorUtils, &AntorUtils::downloadProgress, this, &DBSetupPage::onDownloadProgress);
  connect(antorUtils, &AntorUtils::antorProgress, this, &DBSetupPage::onAntorProgress);
  connect(eyeButton, &QPushButton::toggled, this, &DBSetupPage::togglePasswordVisibility);
  connect(antorUtils, &AntorUtils::errorOccurred, this, [this](const QString &err) {
    qDebug() << "AntorUtils error occurred:" << err;
    Utils::updateStatus(statusLabel, "Error: " + err, StatusType::Error);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Download Error");
    msgBox.setText(err);
    msgBox.setInformativeText("What would you like to do?");

    QPushButton *retryButton = msgBox.addButton("Retry Download", QMessageBox::ActionRole);
    QPushButton *skipButton = msgBox.addButton("Skip for Now", QMessageBox::ActionRole);
    QPushButton *cancelButton = msgBox.addButton(QMessageBox::Cancel);

    msgBox.exec();

    QAbstractButton *clickedButton = msgBox.clickedButton();

    if (clickedButton == retryButton) {
      qDebug() << "User chose to retry download";
      Utils::updateStatus(statusLabel, "Retrying download...", StatusType::Progress);
      progressBar->setRange(0, 0);
      progressBar->setVisible(true);
      confirmButton->setEnabled(false);
      skipButton->setEnabled(false);
      Utils::setWizardButtons();
      retrySnapshotDownload();
    } else if (clickedButton == skipButton) {
      qDebug() << "User chose to skip download";
      skipImport();
    } else {
      qDebug() << "User canceled or closed dialog";
      Utils::updateStatus(statusLabel, "Download failed. Please retry or skip.", StatusType::Error);
      progressBar->setRange(0, 0);
      confirmButton->setEnabled(true);
      skipButton->setEnabled(true);
      Utils::setWizardButtons(false, true);
    }
  });

  connect(antorUtils, &AntorUtils::downloadStalled, this, &DBSetupPage::onDownloadStalled);

  QTimer::singleShot(50, this, [this]() { performSetupOperations(); });

  qDebug() << "DBSetupPage::initializePage() finished (delayed operations scheduled)";
}

void DBSetupPage::performSetupOperations() {
  qDebug() << "DBSetupPage::performSetupOperations() called";

  qDebug() << "DBSetupPage isFresh:" << isFresh;

  if (isFresh) {
    rootPassEdit->setText(m_wiz->rootPass);
    rootPassEdit->setReadOnly(true);

    if (!DBManager::createDatabase(m_wiz->rootPass, dbName)) {
      Utils::updateStatus(statusLabel, "Failed to create database. Check database root password.", StatusType::Error);
      return;
    }
    progressBar->setVisible(true);
    currentState = ImportState::Ready;
    Utils::updateStatus(statusLabel, "Database created. Ready to download annechain dataset...", StatusType::Progress);
    confirmButton->setText("Import Dataset");
    confirmButton->setEnabled(true);
  } else {
    rootPassEdit->setPlaceholderText("Enter existing database root password");
    Utils::updateStatus(statusLabel, "Ready to fetch annechain dataset version...", StatusType::Progress);
    progressBar->setVisible(true);
    currentState = ImportState::Ready;
    confirmButton->setText("Import Dataset");
    confirmButton->setEnabled(true);
  }
  skipButton->setEnabled(true);
  qDebug() << "DBSetupPage::performSetupOperations() finished";
}

bool DBSetupPage::isComplete() const {
  Utils::setWizardButtons(true, true);
  if (forceComplete) {
    qDebug() << "DBSetupPage::isComplete() - forceComplete is true";
    return true;
  }

  qDebug() << "DBSetupPage::isComplete() called, dbSetupDone:" << dbSetupDone;
  return dbSetupDone;
}
bool DBSetupPage::validatePage() {
  qDebug() << "DBSetupPage::validatePage() called, dbSetupDone:" << dbSetupDone << "forceComplete:" << forceComplete;

  if (dbSetupDone || forceComplete) {
    m_wiz->dbName = dbName;
    m_wiz->dbUser = "root";
    m_wiz->dbUserPass = m_wiz->rootPass;
    m_wiz->configReplacements["{DB_NAME}"] = dbName;
    m_wiz->configReplacements["{DB_USER}"] = "root";
    m_wiz->configReplacements["{DB_PASS}"] = m_wiz->rootPass;
    m_wiz->configReplacements["{DB_HOST}"] = "localhost";

    qDebug() << "validatePage returning true (dbSetupDone:" << dbSetupDone << "forceComplete:" << forceComplete << ")";
    return true;
  }

  qDebug() << "validatePage returning false - neither import done nor skipping";
  return false;
}

void DBSetupPage::togglePasswordVisibility(bool show) {
  rootPassEdit->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
  eyeButton->setText(show ? "🙈" : "👁️");
}

void DBSetupPage::performSnapshotDownload() {
  qDebug() << "DBSetupPage::performSnapshotDownload() started";
  currentState = ImportState::Downloading;
  downloadReady = false;
  progressBar->setRange(0, 0);
  progressBar->setVisible(true);
  skipButton->setEnabled(false);
  confirmButton->setEnabled(false);

  Utils::setWizardButtons();

#ifdef Q_OS_UNIX
  if (geteuid() == 0) {
    QString userHome = SystemUtils::getRealUserHome();
    if (!userHome.isEmpty()) {
      cacheDirStr = userHome + QDir::separator() + QStringLiteral(".cache") + QDir::separator() + QStringLiteral("annode");
    }
  }
#endif

  if (cacheDirStr.isEmpty()) {
    cacheDirStr = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    if (!cacheDirStr.endsWith(QStringLiteral("annode"), Qt::CaseInsensitive)) {
      cacheDirStr += QDir::separator() + QStringLiteral("annode");
    }
  }

  QDir cacheDir(cacheDirStr);
  if (!cacheDir.exists()) {
    if (!cacheDir.mkpath(".")) {
      QMessageBox::critical(this, tr("Error"), tr("Cannot create cache directory."));
      Utils::updateStatus(statusLabel, "Cannot create cache directory.", StatusType::Error);
      qDebug() << "Failed to create cache dir:" << cacheDirStr;
      confirmButton->setEnabled(true);
      skipButton->setEnabled(true);
      Utils::setWizardButtons(false, true);
      return;
    }
  }

  getPlatformNid(cacheDirStr);
}

void DBSetupPage::getPlatformNid(const QString &cacheStr) {
  antorUtils->getPlatformNid(
      [this, cacheStr](bool success, const QString &nid) {
        if (!success) {
          Utils::updateStatus(statusLabel, "Failed to fetch latest release information.", StatusType::Error);
          qDebug() << "Failed to get platform NID";

          confirmButton->setText("Retry Connection");
          confirmButton->setEnabled(true);
          skipButton->setEnabled(true);
          Utils::setWizardButtons(false, true);
          return;
        }

        currentNid = nid;
        qDebug() << "Got NID:" << nid;

        QString localZip = cacheDirStr + QDir::separator() + nid + ".sql.zip";
        qDebug() << "Checking cache for:" << localZip;

        if (QFile::exists(localZip)) {
          QFileInfo zipInfo(localZip);
          if (zipInfo.size() > 50 * 1024 * 1024) {
            qDebug() << "Found existing file:" << zipInfo.size() << "bytes";
            m_localZipPath = localZip;

            Utils::updateStatus(statusLabel, "Found existing snapshot. Checking password...", StatusType::Success);
            progressBar->setValue(50);

            getReady();
            return;
          } else {
            qDebug() << "File too small or empty, deleting:" << zipInfo.size() << "bytes";
            QFile::remove(localZip);
          }
        }

        qDebug() << "Starting fresh download for NID:" << nid;
        Utils::updateStatus(statusLabel, "Downloading annechain dataset...", StatusType::Progress);
        antorUtils->downloadFileByNid(nid, localZip, [this](bool ok, const QString &path) {
          if (ok) {
            qDebug() << "Download completed successfully:" << path;
            m_localZipPath = path;
            downloadReady = true;

            progressBar->setValue(50);
            Utils::setWizardButtons();
            getReady();
          } else {
            qDebug() << "Download failed";
            Utils::updateStatus(statusLabel, "Download failed. Please try again.", StatusType::Error);
            progressBar->setRange(0, 0);
            downloadReady = false;
            skipButton->setEnabled(true);
            confirmButton->setText("Retry Download");
            confirmButton->setEnabled(true);
            Utils::setWizardButtons(false, true);
          }
        });
      },
      true);
}

void DBSetupPage::getReady() {
  QString pass = rootPassEdit->text();
  if (pass.isEmpty()) {
    Utils::updateStatus(statusLabel, "Please enter database root password to continue...", StatusType::Progress);
    confirmButton->setEnabled(true);
    skipButton->setEnabled(true);
    Utils::setWizardButtons(false, true);
    return;
  }
  m_wiz->rootPass = pass;

  if (!DBManager::validateRootPassword(pass)) {
    QMessageBox::warning(this, "Invalid Password", "Invalid database root password. Please check and try again.");
    Utils::updateStatus(statusLabel, "Invalid password. Please check and try again.", StatusType::Error);
    confirmButton->setEnabled(true);
    skipButton->setEnabled(true);
    Utils::setWizardButtons(false, true);
    return;
  }

  if (!DBManager::createDatabase(pass, dbName)) {
    Utils::updateStatus(statusLabel, "Failed to create database.", StatusType::Error);
    confirmButton->setText("Retry");
    confirmButton->setEnabled(true);
    skipButton->setEnabled(true);
    Utils::setWizardButtons(false, true);
    return;
  }

  progressBar->setValue(50);
  Utils::updateStatus(statusLabel, "Preparing database for import...", StatusType::Progress);

  progressBar->setValue(55);
  prepImport(pass);
}

void DBSetupPage::retrySnapshotDownload() {
  qDebug() << "Manual database download retry requested, current state:" << static_cast<int>(currentState);

  Utils::updateStatus(statusLabel, "Retrying database download...", StatusType::Progress);
  progressBar->setRange(0, 0);
  progressBar->setVisible(true);

  confirmButton->setEnabled(false);
  Utils::setWizardButtons();

  currentState = ImportState::Downloading;

  qDebug() << "[DBSetupPage] retrySnapshotDownload currentNid:" << currentNid;

  if (!currentNid.isEmpty()) {
    antorUtils->retryDownload(currentNid);
  } else {
    antorUtils->getPlatformNid(
        [this](bool success, const QString &nid) {
          if (success) {
            currentNid = nid;
            antorUtils->retryDownload(currentNid);
          } else {
            qWarning() << "Failed to get NID for retry";
            Utils::updateStatus(statusLabel, "Failed to get snapshot info. Please try again.", StatusType::Error);
            Utils::setWizardButtons(false, true);
            if (isFresh) {
              confirmButton->setText("Import Dataset");
              confirmButton->setEnabled(true);
              QTimer::singleShot(3000, this, [this]() {
                Utils::updateStatus(statusLabel, "Retrying connection...", StatusType::Progress);
                confirmButton->setEnabled(false);
                retrySnapshotDownload();
              });
            } else {
              confirmButton->setText("Retry Connection");
              confirmButton->setEnabled(true);
            }
            skipButton->setEnabled(true);
          }
        },
        true);
  }
}

void DBSetupPage::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
  Utils::setProgressColor(progressBar, false);
  if (bytesTotal > 0) {
    progressBar->setMaximum(static_cast<int>(bytesTotal));
    progressBar->setValue(static_cast<int>(bytesReceived));
  } else {
    progressBar->setRange(0, 0);
  }
  double receivedMB = bytesReceived / (1024.0 * 1024.0);
  double totalMB = bytesTotal > 0 ? bytesTotal / (1024.0 * 1024.0) : 0.0;
  QString totalStr = (bytesTotal > 0) ? QString::number(totalMB, 'f', 1) : "?";
  Utils::updateStatus(statusLabel, QString("Downloading annechain dataset... %1 / %2 MB").arg(receivedMB, 0, 'f', 1).arg(totalStr), StatusType::Progress);
  qDebug() << "Download Progress:" << bytesReceived << "/" << bytesTotal;
}

void DBSetupPage::onAntorProgress(const QString &nid, int completed, int total) {
  if (nid != currentNid) {
    return;
  }
  Utils::setProgressColor(progressBar, true);
  progressBar->setRange(0, total);
  progressBar->setValue(completed);
  int percent = total > 0 ? static_cast<int>((completed * 100.0) / total) : 0;
  Utils::updateStatus(statusLabel, QString("Fetching ants via antor... %1/%2 chunks (%3%)").arg(completed).arg(total).arg(percent), StatusType::Progress);
  qDebug() << "ANTOR Progress:" << completed << "/" << total;
}

void DBSetupPage::onDownloadStalled() {
  qDebug() << "Database download stalled - showing retry option";
  Utils::updateStatus(statusLabel, "Download stalled - check network connection", StatusType::Warning);

  if (isFresh) {
    Utils::updateStatus(statusLabel, "Download stalled - retrying in 15 seconds...", StatusType::Warning);
    confirmButton->setText("Retry Now");
    confirmButton->setEnabled(true);
    Utils::setWizardButtons(false, true);
    QTimer::singleShot(15000, this, [this]() {
      if (currentState == ImportState::Downloading || currentState == ImportState::Stalled) {
        qDebug() << "Auto-retrying stalled download for fresh install";
        retrySnapshotDownload();
      }
    });
  } else {
    confirmButton->setText("Retry Download");
    confirmButton->setEnabled(true);
    skipButton->setEnabled(true);
    Utils::setWizardButtons(false, true);
  }

  progressBar->setRange(0, 0);

  currentState = ImportState::Stalled;
}

void DBSetupPage::skipImport() {

  qDebug() << "User confirmed to cancel ongoing process and skip";
  Utils::updateStatus(statusLabel, "Skipping...", StatusType::Progress);
  QString pass = rootPassEdit->text();

  if (pass.isEmpty()) {
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Skip Without Password?",
                                                              "You have not entered a database root password.\n\n"
                                                              "Database import will be skipped.\n"
                                                              "You can configure database settings later.",
                                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes) {

      confirmButton->setEnabled(true);
      confirmButton->setText("Import Dataset");
      Utils::updateStatus(statusLabel, "Ready to Import Dataset...", StatusType::Progress);
      return;
    }

    qDebug() << "User chose to skip import without any password";
    m_wiz->rootPass = "";

    forceComplete = true;
    emit completeChanged();

    QTimer::singleShot(100, wizard(), &QWizard::next);
    return;
  }

  Concurrent::withCallback(
      this, [pass]() -> bool { return DBManager::validateRootPassword(pass); },
      [this, pass](bool valid) {
        QMetaObject::invokeMethod(this, [this, pass, valid]() {
          if (!valid) {
            QMessageBox::warning(this, "Invalid Password",
                                 "Invalid database root password.\n\n"
                                 "You can still skip, but database won't be configured.\n"
                                 "You can configure it later in the settings.");

            QMessageBox::StandardButton reply = QMessageBox::question(this, "Skip Anyway?",
                                                                      "Database password is invalid.\n\n"
                                                                      "Do you want to skip anyway and configure database later?",
                                                                      QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
              m_wiz->rootPass = "";

              forceComplete = true;
              emit completeChanged();

              Utils::updateStatus(statusLabel, "Skipping import...", StatusType::Progress);
              progressBar->setValue(100);
              QTimer::singleShot(100, wizard(), &QWizard::next);
            } else {

              confirmButton->setEnabled(true);
              confirmButton->setText("Import Dataset");
              Utils::updateStatus(statusLabel, "Ready to Import Dataset...", StatusType::Progress);
            }
            return;
          }

          qDebug() << "Skipping import with valid password";
          m_wiz->rootPass = pass;

          forceComplete = true;
          emit completeChanged();

          Utils::updateStatus(statusLabel, "Skipping import...", StatusType::Progress);
          progressBar->setValue(100);
          QTimer::singleShot(100, wizard(), &QWizard::next);
        });
      });
}

void DBSetupPage::copyPassword() {
  if (rootPassEdit->text().isEmpty()) {
    Utils::updateStatus(statusLabel, "No password to copy.", StatusType::Warning);
    return;
  }

  QApplication::clipboard()->setText(rootPassEdit->text());

  if (currentState == ImportState::Importing || currentState == ImportState::Complete) {
    qDebug() << "Password copied during import – no UI override.";
    return;
  }

  QString originalStatus = statusLabel->text();
  Utils::updateStatus(statusLabel, "Password copied to clipboard!", StatusType::Success);

  QTimer::singleShot(1500, [this, originalStatus]() {
    if (currentState != ImportState::Importing && currentState != ImportState::Complete) {
      Utils::updateStatus(statusLabel, originalStatus, StatusType::Progress);
    }
  });

  qDebug() << "Password copied – status will fade back.";
}

void DBSetupPage::confirmAndImport() {
  qDebug() << "confirmAndImport called, dbSetupDone:" << dbSetupDone << "forceComplete:" << forceComplete;

  if (forceComplete) {
    forceComplete = false;
    emit completeChanged();
    qDebug() << "Reset forceComplete since user wants to import";
  }
  if (dbSetupDone) {
    QTimer::singleShot(0, wizard(), &QWizard::next);
    return;
  }

  QString buttonText = confirmButton->text();

  if (buttonText == "Import Dataset") {
    confirmButton->setEnabled(false);

    performSnapshotDownload();
    return;
  }

  if (buttonText == "Retry Connection" || buttonText == "Retry Download" || buttonText == "Retry Now") {
    confirmButton->setEnabled(false);
    confirmButton->setText("Retrying...");
    retrySnapshotDownload();
    return;
  }

  if (buttonText == "Import Dataset" || buttonText == "Confirm Import") {
    QString pass = rootPassEdit->text();
    if (pass.isEmpty()) {
      QMessageBox::warning(this, "Password Required", "Database root password is required.");
      return;
    }
    m_wiz->rootPass = pass;

    if (!DBManager::validateRootPassword(pass)) {
      QMessageBox::warning(this, "Invalid Password", "Invalid database root password. Please check and try again.");
      skipButton->setEnabled(true);
      return;
    }

    if (!DBManager::createDatabase(pass, dbName)) {
      Utils::updateStatus(statusLabel, "Database not found.", StatusType::Error);
      skipButton->setEnabled(true);
      return;
    }

    progressBar->setValue(50);
    Utils::updateStatus(statusLabel, "Resetting existing data...", StatusType::Progress);
    confirmButton->setEnabled(false);
    Utils::setWizardButtons();

    progressBar->setValue(55);

    prepImport(pass);
    return;
  }

  if (buttonText == "Cancel Import") {

    return;
  }

  if (buttonText == "Retry Import") {
    QString pass = rootPassEdit->text();
    if (pass.isEmpty()) {
      QMessageBox::warning(this, "Password Required", "Database root password is required.");
      return;
    }
    confirmButton->setEnabled(false);
    confirmButton->setText("Importing...");
    Utils::setWizardButtons();
    prepImport(pass);
    return;
  }
}

void DBSetupPage::prepImport(const QString &rootPass) {
  qDebug() << "prepImport called";
  currentState = ImportState::Extracting;
  skipButton->setEnabled(false);
  Utils::updateStatus(statusLabel, "Extracting annechain dataset...", StatusType::Progress);
  progressBar->setValue(55);

  confirmButton->setEnabled(false);

  if (m_localZipPath.isEmpty() || !QFile::exists(m_localZipPath)) {
    QMessageBox::critical(this, "Error", "Snapshot file not found.");
    Utils::updateStatus(statusLabel, "Extraction failed: File not found.", StatusType::Error);
    confirmButton->setText("Retry Import");
    confirmButton->setEnabled(true);
    skipButton->setEnabled(true);
    Utils::setWizardButtons(false, true);
    return;
  }

  FileHandler handler;

  QTemporaryDir tempDir(QDir::tempPath() + QDir::separator() + "ANNEWizard-XXXXXX");
  if (!tempDir.isValid()) {
    currentState = ImportState::Error;
    Utils::updateStatus(statusLabel, "Failed to create temporary directory.", StatusType::Error);
    confirmButton->setText("Retry Import");
    confirmButton->setEnabled(true);
    skipButton->setEnabled(true);
    Utils::setWizardButtons(false, true);
    return;
  }
  tempDir.setAutoRemove(false);
  qDebug() << "Created persistent temp dir:" << tempDir.path();

  auto tempDirPtr = std::make_shared<QTemporaryDir>(std::move(tempDir));

  QPointer<DBSetupPage> safeThis(this);

  Concurrent::withCallback(
      this,
      [this, tempDirPtr]() -> bool {
        FileHandler handler;
        return handler.extractZipFromFile(m_localZipPath, tempDirPtr->path(), true);
      },
      [safeThis, tempDirPtr, rootPass, this](bool extractionSuccess) {
        qDebug() << "[CALLBACK] Extraction callback, safeThis valid:" << (bool)safeThis
                 << "currentImportCancelled:" << (currentImportCancelled ? currentImportCancelled->load() : false);

        if (!safeThis || (currentImportCancelled && currentImportCancelled->load())) {
          qDebug() << "[CALLBACK] Page destroyed or import cancelled, cleaning up temp dir";
          if (tempDirPtr) {
            QDir(tempDirPtr->path()).removeRecursively();
          }
          return;
        }

        QMetaObject::invokeMethod(safeThis, [safeThis, tempDirPtr, rootPass, extractionSuccess, this]() {
          if (!safeThis || (currentImportCancelled && currentImportCancelled->load())) {
            qDebug() << "[CALLBACK] Page destroyed or import cancelled in invokeMethod";
            if (tempDirPtr) {
              QDir(tempDirPtr->path()).removeRecursively();
            }
            return;
          }

          if (!extractionSuccess) {
            QString err = FileHandler().lastError().trimmed();

            qDebug() << "Extraction failed - ZIP corrupted:" << err;
            QFile::remove(m_localZipPath);
            m_localZipPath.clear();
            downloadReady = false;

            QMessageBox::warning(this, "Corrupted Download",
                                 "The snapshot file is corrupted.\n"
                                 "It will be deleted and re-downloaded automatically.");

            Utils::updateStatus(statusLabel, "File corrupted. Re-downloading...", StatusType::Progress);
            progressBar->setValue(0);
            QDir(tempDirPtr->path()).removeRecursively();
            confirmButton->setText("Import Dataset");
            confirmButton->setEnabled(true);
            skipButton->setEnabled(true);
            Utils::setWizardButtons(false, true);
            return;
          }

          QDir extractedDir(tempDirPtr->path());
          QStringList extractedFiles = extractedDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
          qDebug() << "Extracted files in temp dir:" << extractedFiles;

          if (extractedFiles.isEmpty()) {
            qDebug() << "WARNING: No files extracted!";
            currentState = ImportState::Error;
            Utils::updateStatus(statusLabel, "Extraction incomplete: No files found in ZIP.", StatusType::Error);
            QDir(tempDirPtr->path()).removeRecursively();
            confirmButton->setText("Retry Import");
            confirmButton->setEnabled(true);
            skipButton->setEnabled(true);
            Utils::setWizardButtons(false, true);
            return;
          }

          QString sqlPath;
          for (const QString &file : extractedFiles) {
            if (file.endsWith(".sql", Qt::CaseInsensitive)) {
              sqlPath = extractedDir.filePath(file);
              qDebug() << "Found SQL file:" << sqlPath << "(" << QFileInfo(sqlPath).size() << "bytes)";
              break;
            }
          }

          if (sqlPath.isEmpty()) {
            qDebug() << "No .sql file found in extracted files:" << extractedFiles;
            currentState = ImportState::Error;
            Utils::updateStatus(statusLabel, "Extraction incomplete: No SQL file found in ZIP.", StatusType::Error);
            QDir(tempDirPtr->path()).removeRecursively();
            confirmButton->setText("Retry Import");
            confirmButton->setEnabled(true);
            skipButton->setEnabled(true);
            Utils::setWizardButtons(false, true);
            return;
          }

          QFile sqlFile(sqlPath);
          if (!sqlFile.open(QIODevice::ReadOnly)) {
            currentState = ImportState::Error;
            Utils::updateStatus(statusLabel, "SQL file unreadable.", StatusType::Error);
            QDir(tempDirPtr->path()).removeRecursively();
            confirmButton->setText("Retry Import");
            confirmButton->setEnabled(true);
            skipButton->setEnabled(true);
            Utils::setWizardButtons(false, true);
            return;
          }
          sqlFile.close();
          qDebug() << "Validated SQL file (" << QFileInfo(sqlPath).size() << " bytes)";

          startDatabaseImport(rootPass, tempDirPtr, sqlPath);
        });
      });
}

void DBSetupPage::startDatabaseImport(const QString &rootPass, std::shared_ptr<QTemporaryDir> tempDirPtr, const QString &sqlPath) {
  currentState = ImportState::Importing;
  progressBar->setRange(0, 100);
  progressBar->setValue(0);
  Utils::updateStatus(statusLabel, "Starting database import...", StatusType::Progress);
  progressBar->setVisible(true);

  confirmButton->setText("Cancel Import");
  confirmButton->setEnabled(true);
  skipButton->setEnabled(false);
  qApp->processEvents();

  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  currentImportCancelled = cancelled;

  static QMetaObject::Connection cancelConnection;

  if (cancelConnection) {
    disconnect(cancelConnection);
  }

  cancelConnection = connect(confirmButton, &QPushButton::clicked, this, [this, tempDirPtr, cancelled]() {
    if (currentState == ImportState::Importing) {
      cancelled->store(true);
      currentState = ImportState::None;
      currentImportCancelled.reset();

      Utils::updateStatus(statusLabel, "Import cancelled by user.", StatusType::Warning);

      if (tempDirPtr) {
        QDir(tempDirPtr->path()).removeRecursively();
      }

      confirmButton->setText("Import Dataset");
      confirmButton->setEnabled(true);
      skipButton->setEnabled(true);
      progressBar->setValue(0);

      disconnect(cancelConnection);

      Utils::setWizardButtons(false, true);
    }
  });

  DBManager *dbManager = new DBManager();

  connect(dbManager, &DBManager::importProgress, this, [this](qint64 bytesProcessed, qint64 totalBytes) {
    if (totalBytes > 0) {
      int percent = static_cast<int>((bytesProcessed * 100) / totalBytes);
      progressBar->setValue(percent);

      qint64 currentMB = bytesProcessed / (1024 * 1024);
      qint64 totalMB = totalBytes / (1024 * 1024);
      Utils::updateStatus(statusLabel, QString("Importing... %1 MB of %2 MB").arg(currentMB).arg(totalMB), StatusType::Progress);
    }
  });

  connect(dbManager, &DBManager::importStatus, this, [this](const QString &message) {
    if (!message.contains("MB of")) {
      Utils::updateStatus(statusLabel, message, StatusType::Progress);
    }
  });

  QPointer<DBSetupPage> safeThis(this);

  Concurrent::withCallback(
      this,
      [dbManager, rootPass, sqlPath, cancelled, dbName = this->dbName]() -> QPair<bool, QString> {
        if (cancelled->load()) {
          dbManager->deleteLater();
          return qMakePair(false, QString("Import cancelled"));
        }

        QPair<bool, QString> result = dbManager->importSql(rootPass, dbName, sqlPath, cancelled.get());

        dbManager->deleteLater();
        return result;
      },
      [safeThis, tempDirPtr, cancelled, this](QPair<bool, QString> result) {
        qDebug() << "[CALLBACK] Import callback, safeThis valid:" << (bool)safeThis << "cancelled flag:" << (cancelled ? cancelled->load() : false);

        if (!safeThis) {
          qDebug() << "[CALLBACK] Page destroyed, cleaning up temp dir";
          if (tempDirPtr) {
            QDir(tempDirPtr->path()).removeRecursively();
          }
          return;
        }

        QMetaObject::invokeMethod(safeThis, [safeThis, tempDirPtr, cancelled, result, this]() {
          if (!safeThis) {
            qDebug() << "[CALLBACK] Page destroyed in invokeMethod";
            if (tempDirPtr) {
              QDir(tempDirPtr->path()).removeRecursively();
            }
            return;
          }

          disconnect(cancelConnection);

          currentImportCancelled.reset();

          QDir(tempDirPtr->path()).removeRecursively();

          if (cancelled->load()) {
            qDebug() << "[CALLBACK] Import was cancelled";

            return;
          }

          bool importSuccess = result.first;
          QString errorMessage = result.second;

          if (importSuccess) {
            currentState = ImportState::Complete;
            progressBar->setValue(100);
            Utils::updateStatus(statusLabel, "Database setup complete! (100%)", StatusType::Success);
            dbSetupDone = true;
            emit completeChanged();
            qDebug() << "Import complete, dbSetupDone = true";

            confirmButton->setEnabled(true);
            confirmButton->setText("Import Dataset");
            skipButton->setEnabled(true);

            QMessageBox::information(this, "Import Successful",
                                     "Database import completed successfully!\n\n"
                                     "The annechain dataset has been imported and is ready to use.");
          } else {
            currentState = ImportState::None;

            QString detailedError = errorMessage;

            if (errorMessage.contains("Access denied", Qt::CaseInsensitive)) {
              detailedError = "Database access denied. Please check if:\n"
                              "1. The root password is correct\n"
                              "2. MariaDB service is running\n"
                              "3. You have proper permissions";
            } else if (errorMessage.contains("Can't connect", Qt::CaseInsensitive)) {
              detailedError = "Cannot connect to MariaDB server. Please ensure:\n"
                              "1. MariaDB service is running\n"
                              "2. The server is accessible on localhost";
            } else if (errorMessage.contains("file not found", Qt::CaseInsensitive)) {
              detailedError = "SQL file not found. The file may have been corrupted or deleted.";
            }

            QMessageBox::critical(this, "Import Failed",
                                  QString("Database import failed!\n\n"
                                          "Error: %1\n\n"
                                          "Please check your database configuration and try again.")
                                      .arg(detailedError));

            Utils::updateStatus(statusLabel, "Import failed: " + errorMessage, StatusType::Error);
            confirmButton->setText("Retry Import");
            confirmButton->setEnabled(true);
            skipButton->setEnabled(true);
            Utils::setWizardButtons(false, true);
          }
        });
      });
}

void DBSetupPage::cleanupPage() {
  qDebug() << "DBSetupPage::cleanupPage() called - cleaning up resources";

  if (antorUtils) {
    antorUtils->cancelCurrentDownload();
    antorUtils->deleteLater();
    antorUtils = nullptr;
  }

  Concurrent::cancelAll();
  currentImportCancelled.reset();

  currentState = ImportState::None;
  dbSetupDone = false;
  forceComplete = false;
  downloadReady = false;
  currentNid = "";
  m_localZipPath = "";

  zipData.clear();

  progressBar->setVisible(false);
  progressBar->setValue(0);
  statusLabel->setText("Initializing...");

  confirmButton->setEnabled(true);
  confirmButton->setText("Import Dataset");
  skipButton->setEnabled(true);

  rootPassEdit->setText("");
  rootPassEdit->setReadOnly(false);
  eyeButton->setChecked(false);
  eyeButton->setText("👁️");

  noteLabel->setVisible(true);
  rootPassEdit->setVisible(true);
  eyeButton->setVisible(true);
  copyButton->setVisible(true);
  confirmButton->setVisible(true);
  skipButton->setVisible(true);

  QWizardPage::cleanupPage();
}
