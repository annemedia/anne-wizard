#include "downloadpage.h"
#include "../utils/antorutils.h"
#include "../utils/concurrent.h"
#include "../utils/filehandler.h"
#include "../utils/netutils.h"
#include "../utils/systemutils.h"
#include "../utils/utils.h"
#include <QCheckBox>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

DownloadPage::DownloadPage(QWidget *parent) : QWizardPage(parent) {
  m_nam = new QNetworkAccessManager(this);
  antorUtils = nullptr;
  currentNid = "";
  m_currentPhase = Phase::NotStarted;

  setTitle("Download and Install ANNE Applications");
  setSubTitle("Select applications and install directory. Annode is required, "
              "extras are optional.");

  appsGroupBox = new QGroupBox("ANNE Applications Selection");

  annodeGroup = new QGroupBox("Annode");
  annodeGroup->setStyleSheet("QGroupBox { font-weight: bold; }");

  annodeCheckbox = new QCheckBox("Annode");
  annodeCheckbox->setChecked(true);
  annodeCheckbox->setEnabled(false);

  QLabel *annodeInfo = new QLabel("Full blockchain node for ANNE datacurrency.\n"
                                  "Required for data operations, native wallet, transactions, "
                                  "and network participation.\n"
                                  "Downloads automatically - this cannot be deselected.");
  annodeInfo->setWordWrap(true);
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  annodeInfo->setStyleSheet("font-size: 11pt; color: #666; margin-left: 20px;");
#else
  annodeInfo->setStyleSheet("font-size: 9pt; color: #666; margin-left: 20px;");
#endif
  QVBoxLayout *annodeLayout = new QVBoxLayout;
  annodeLayout->addWidget(annodeCheckbox);
  annodeLayout->addWidget(annodeInfo);
  annodeGroup->setLayout(annodeLayout);

  extrasGroup = new QGroupBox("Optional ANNE Tools");

  extrasCheckbox = new QCheckBox("ANNE Hasher && ANNE Miner");
  extrasCheckbox->setChecked(true);
  connect(extrasCheckbox, SIGNAL(stateChanged(int)), this, SLOT(onExtrasSelectionChanged()));

  QLabel *extrasInfo = new QLabel("ANNE Hasher creates nonces for mining by hashing against account ID.\n"
                                  "ANNE Miner mines annecoins using your nonces.\n"
                                  "These tools are a package deal - you can install both or none.\n"
                                  "Required if you want to mine on this machine.");
  extrasInfo->setWordWrap(true);
  extrasInfo->setStyleSheet("font-size: 10pt; color: #666; margin-left: 20px;");

  QVBoxLayout *extrasLayout = new QVBoxLayout;
  extrasLayout->addWidget(extrasCheckbox);
  extrasLayout->addWidget(extrasInfo);
  extrasGroup->setLayout(extrasLayout);

  QVBoxLayout *appsLayout = new QVBoxLayout;
  appsLayout->addWidget(annodeGroup);
  appsLayout->addSpacing(10);
  appsLayout->addWidget(extrasGroup);
  appsGroupBox->setLayout(appsLayout);

  QString defaultDir = SystemUtils::getInstallPath();
  installDirEdit = new QLineEdit(defaultDir);

  QPushButton *browseBtn = new QPushButton("Browse");
  connect(browseBtn, &QPushButton::clicked, [this]() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Install Directory");
    if (!dir.isEmpty()) {
      installDirEdit->setText(dir);
      onDirChanged();
    }
  });

  connect(installDirEdit, &QLineEdit::editingFinished, this, &DownloadPage::onDirChanged);

  downloadInstallBtn = new QPushButton("Download && Install");
  downloadInstallBtn->setVisible(true);
  downloadInstallBtn->setEnabled(true);
  connect(downloadInstallBtn, &QPushButton::clicked, this, &DownloadPage::onDownloadInstallClicked);

  statusLabel = new QLabel("Initializing...");
  Utils::updateStatus(statusLabel, "Ready to download and install ANNE applications.", StatusType::Success);

  progressBar = new QProgressBar();
  Utils::setProgressColor(progressBar, false);
  progressBar->setRange(0, 100);
  progressBar->setValue(0);
  progressBar->setVisible(false);

  dirLabel = new QLabel("Install Directory:");

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->addWidget(appsGroupBox);
  mainLayout->addSpacing(20);
  mainLayout->addWidget(statusLabel);
  mainLayout->addWidget(progressBar);
  mainLayout->addSpacing(20);

  QHBoxLayout *dirRow = new QHBoxLayout();
  dirRow->addWidget(dirLabel);
  dirRow->addWidget(installDirEdit, 1);
  dirRow->addWidget(browseBtn);
  mainLayout->addLayout(dirRow);
  mainLayout->addWidget(downloadInstallBtn);

  registerField("installDir*", installDirEdit);
  registerField("installExtras", extrasCheckbox);
}

DownloadPage::~DownloadPage() {}

void DownloadPage::initializePage() {
  qDebug() << "DownloadPage::initializePage() started";

  m_wiz = qobject_cast<Wizard *>(wizard());
  if (!m_wiz) {
    return;
  }
  installDirStr = field("installDir").toString();
  if (installDirStr.isEmpty()) {
    installDirStr = SystemUtils::getInstallPath();
    installDirEdit->setText(installDirStr);
  }
  userHome = SystemUtils::getRealUserHome();
  QDir installDir(installDirStr);
  if (!installDir.exists()) {
    if (!installDir.mkpath(".")) {
      Utils::updateStatus(statusLabel, "Failed to create install directory.", StatusType::Error);
      qDebug() << "Failed to create install dir:" << installDirStr;
      return;
    }
    qDebug() << "Created install dir:" << installDirStr;
  }

#ifdef Q_OS_UNIX
  SystemUtils::chownUser(installDirStr, true);
#endif

  m_wiz->installDir = installDirStr;
  qDebug() << "Set wizard installDir to:" << m_wiz->installDir;

  m_currentPhase = Phase::NotStarted;

  m_annodeZipPath.clear();
  m_hasherZipPath.clear();
  m_minerZipPath.clear();

  downloadInstallBtn->setEnabled(true);
  downloadInstallBtn->setText("Download and Install");
  downloadInstallBtn->setVisible(true);

  progressBar->setVisible(false);
  progressBar->setValue(0);
  progressBar->setRange(0, 100);

  currentNid = "";

  if (antorUtils) {
    antorUtils->disconnect();
    antorUtils->deleteLater();
    antorUtils = nullptr;
  }

  updateStatusForPhase();
#ifdef Q_OS_UNIX
  qDebug() << "Effective user home:" << userHome << "| Real UID:" << getuid() << "| Effective UID:" << geteuid() << "| SUDO_USER:" << qgetenv("SUDO_USER")
           << "| PKEXEC_UID:" << qgetenv("PKEXEC_UID");
#endif
}

void DownloadPage::onExtrasSelectionChanged() {
  qDebug() << "Extras selection changed, state:" << extrasCheckbox->isChecked();

  if (m_currentPhase == Phase::Complete) {

    m_currentPhase = Phase::NotStarted;
    progressBar->setVisible(false);
    progressBar->setValue(0);
    downloadInstallBtn->setVisible(true);
    downloadInstallBtn->setEnabled(true);
    downloadInstallBtn->setText("Download and Install");

    m_hasherZipPath.clear();
    m_minerZipPath.clear();
  }

  updateStatusForPhase();
}

QString DownloadPage::getAction() const {
  QString currentDirStr = installDirEdit->text();
  QDir currentDir(currentDirStr);
  bool isUpgrade = currentDir.exists() && !currentDir.entryList(QDir::NoDotAndDotDot | QDir::Files).isEmpty();
  return isUpgrade ? "Upgrade" : "Install";
}

void DownloadPage::updateStatusForPhase() {

  QString st = "Ready to download Annode.";
  StatusType sttype = StatusType::Progress;

  switch (m_currentPhase) {
  case Phase::NotStarted:
    if (extrasCheckbox->isChecked()) {
      st = "Ready to download Annode, Hasher, and Miner.";
    }
    sttype = StatusType::Success;
    downloadInstallBtn->setVisible(true);
    downloadInstallBtn->setEnabled(true);
    break;
  case Phase::DownloadingAnnode:
    st = "Downloading Annode...";
    downloadInstallBtn->setVisible(true);
    downloadInstallBtn->setEnabled(false);
    break;
  case Phase::AnnodeComplete:
    if (extrasCheckbox->isChecked()) {
      st = "Annode downloaded. Downloading extras...";
    } else {
      st = "Annode downloaded. Ready to install...";
    }
    downloadInstallBtn->setVisible(true);
    downloadInstallBtn->setEnabled(false);
    break;
  case Phase::DownloadingExtras:
    st = "Downloading ANNE Hasher & Miner...";
    downloadInstallBtn->setVisible(true);
    downloadInstallBtn->setEnabled(false);
    break;
  case Phase::ExtrasComplete:
    st = "All downloads complete. Installing...";
    downloadInstallBtn->setVisible(true);
    downloadInstallBtn->setEnabled(false);
    break;
  case Phase::Extracting:
    st = "Installing applications...";
    downloadInstallBtn->setVisible(true);
    downloadInstallBtn->setEnabled(false);
    break;
  case Phase::Complete: {
    QString successMsg = "Installation complete in locations:";
    QString basePath = installDirEdit->text();
    QDir baseDir(basePath);
    successMsg += "\n• Annode: " + baseDir.filePath("annode");

    if (extrasCheckbox->isChecked()) {
      successMsg += "\n• ANNE Hasher: " + baseDir.filePath("annehasher");
      successMsg += "\n• ANNE Miner: " + baseDir.filePath("anneminer");
    }

    st = successMsg;
    sttype = StatusType::Success;
    downloadInstallBtn->setVisible(true);
    downloadInstallBtn->setEnabled(true);
    downloadInstallBtn->setText("Install More / Reinstall");
  } break;
  case Phase::Error:
    st = "Error occurred. Please try again.";
    sttype = StatusType::Error;
    downloadInstallBtn->setVisible(true);
    downloadInstallBtn->setEnabled(true);
    downloadInstallBtn->setText("Retry Download and Install");
    break;
  }
  Utils::updateStatus(statusLabel, st, sttype);
}

void DownloadPage::onDirChanged() {

  m_wiz->installDir = installDirEdit->text();
  qDebug() << "Updated wizard installDir to:" << m_wiz->installDir;

  updateStatusForPhase();
}

void DownloadPage::onDownloadInstallClicked() {
  qDebug() << "Download and Install clicked, current phase:" << static_cast<int>(m_currentPhase);
  Utils::setWizardButtons();
  extrasCheckbox->setEnabled(false);
  if (m_currentPhase == Phase::Complete) {

    m_currentPhase = Phase::NotStarted;
    updateStatusForPhase();
  }

  QString installDir = installDirEdit->text().trimmed();
  if (installDir.isEmpty()) {
    QMessageBox::critical(this, "Invalid Directory", "Please select a valid installation directory.");
    return;
  }

  QDir dir(installDir);
  if (!dir.exists()) {
    if (!dir.mkpath(".")) {
      QMessageBox::critical(this, "Error",
                            "Cannot create installation directory. Please "
                            "choose a different location.");
      return;
    }
  }

  startDownloadProcess();
}

void DownloadPage::startDownloadProcess() {

  downloadInstallBtn->setEnabled(false);
  downloadInstallBtn->setText("Downloading...");

  progressBar->setVisible(true);
  progressBar->setRange(0, 100);
  progressBar->setValue(10);

  m_currentPhase = Phase::DownloadingAnnode;
  updateStatusForPhase();
  performAnnodeDownload();
}

void DownloadPage::performAnnodeDownload() {
  qDebug() << "Starting Annode download process";

  progressBar->setRange(0, 100);
  progressBar->setValue(10);

  if (!antorUtils) {
    antorUtils = new AntorUtils(this);

    connect(antorUtils, &AntorUtils::statusMessage, this, [this](const QString &msg) {
      if (m_currentPhase == Phase::DownloadingAnnode) {
        Utils::updateStatus(statusLabel, "Annode: " + msg, StatusType::Progress);
      }
    });

    connect(antorUtils, &AntorUtils::downloadProgress, this, &DownloadPage::onDownloadProgress);
    connect(antorUtils, &AntorUtils::antorProgress, this, &DownloadPage::onAntorProgress);

    connect(antorUtils, &AntorUtils::errorOccurred, this, [this](const QString &err) { handleDownloadError(err, "Annode"); });

    connect(antorUtils, &AntorUtils::downloadStalled, this, &DownloadPage::onDownloadStalled);
  }

  QString cacheDirStr;

#ifdef Q_OS_UNIX
  if (geteuid() == 0) {
    if (!userHome.isEmpty()) {
      cacheDirStr = userHome + QDir::separator() + QStringLiteral(".cache") + QDir::separator() + QStringLiteral("annode");
    }
  }
#endif

  if (cacheDirStr.isEmpty()) {

    QString baseCache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    QDir cacheDir(baseCache);

    if (!cacheDir.cd("annode")) {
      cacheDir.mkpath("annode");
      cacheDir.cd("annode");
    }

    cacheDirStr = cacheDir.absolutePath();
    qDebug() << "Normalized cache path:" << cacheDirStr;
  }

  QDir cacheDir(cacheDirStr);
  if (!cacheDir.exists()) {
    if (!cacheDir.mkpath(".")) {
      QMessageBox::critical(this, tr("Error"), tr("Cannot create cache directory."));
      Utils::updateStatus(statusLabel, "Failed to access cache directory", StatusType::Error);
      qDebug() << "Failed to create cache dir:" << cacheDirStr;

      m_currentPhase = Phase::Error;
      downloadInstallBtn->setEnabled(true);
      downloadInstallBtn->setText(tr("Download and Install"));
      progressBar->setVisible(false);
      return;
    }
  }

  antorUtils->getPlatformNid([this, cacheDirStr](bool success, const QString &nid) {
    if (!success) {
      handleDownloadError("Failed to fetch latest Annode release.", "Annode");
      return;
    }

    currentNid = nid;
    qDebug() << "Got NID:" << nid;

    QString localZip = cacheDirStr + QDir::separator() + nid + ".zip";
    qDebug() << "Checking cache for:" << localZip;

    if (QFile::exists(localZip)) {
      QFileInfo zipInfo(localZip);
      if (zipInfo.size() > 0) {

        if (FileHandler::isZipFileValid(localZip)) {
          qDebug() << "Using cached Annode ZIP for NID:" << nid << "size:" << zipInfo.size();
          m_annodeZipPath = localZip;
          m_currentPhase = Phase::AnnodeComplete;
          qDebug() << "We are in getPlatformNid isZipFileValid phase 1";

          updateStatusForPhase();
          onAnnodeDownloadComplete();
          return;
        } else {
          qDebug() << "Cached ZIP is corrupted, removing";
          QFile::remove(localZip);
          Utils::updateStatus(statusLabel, "Cached Annode ZIP corrupted. Re-downloading...", StatusType::Progress);
        }
      } else {
        qDebug() << "Cached ZIP is empty, removing";
        QFile::remove(localZip);
      }
    }

    qDebug() << "Starting download for NID:" << nid;
    progressBar->setValue(10);
    Utils::updateStatus(statusLabel, "Downloading Annode...", StatusType::Progress);
    antorUtils->downloadFileByNid(nid, localZip, [this](bool ok, const QString &path) {
      if (ok) {

        if (FileHandler::isZipFileValid(path)) {
          qDebug() << "Annode download callback success, path:" << path;
          m_annodeZipPath = path;
          m_currentPhase = Phase::AnnodeComplete;
          qDebug() << "We are in downloadFileByNid isZipFileValid phase 2";

          updateStatusForPhase();
          onAnnodeDownloadComplete();
        } else {
          qDebug() << "Downloaded Annode ZIP is corrupted";
          QFile::remove(path);
          handleDownloadError("Downloaded file is corrupted", "Annode");
        }
      } else {
        qDebug() << "Annode download callback failed";
        handleDownloadError("Download failed", "Annode");
      }
    });
  });
}

void DownloadPage::retryAnnodeDownload() {
  qDebug() << "Manual retry requested for Annode";

  m_currentPhase = Phase::DownloadingAnnode;
  updateStatusForPhase();
  progressBar->setRange(0, 100);
  progressBar->setValue(10);
  antorUtils->retryDownload(currentNid);
}

void DownloadPage::onAnnodeDownloadComplete() {
  qDebug() << "Annode download complete, checking for extras";
  progressBar->setValue(80);
  qDebug() << "We are in onAnnodeDownloadComplete phase progress is 80%";
  if (extrasCheckbox->isChecked()) {

    m_currentPhase = Phase::DownloadingExtras;
    updateStatusForPhase();
    performExtrasDownload();
  } else {

    m_currentPhase = Phase::Extracting;
    progressBar->setValue(95);
    updateStatusForPhase();
    performExtraction();
  }
}

void DownloadPage::performExtrasDownload() {
  qDebug() << "Starting extras download";
  progressBar->setValue(80);
  qDebug() << "We are in performExtrasDownload phase";
  QString cacheDirStr;

#ifdef Q_OS_UNIX
  if (geteuid() == 0) {
    if (!userHome.isEmpty()) {
      cacheDirStr = userHome + QDir::separator() + QStringLiteral(".cache") + QDir::separator() + QStringLiteral("annode");
    }
  }
#endif
  if (cacheDirStr.isEmpty()) {
    cacheDirStr = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    if (!cacheDirStr.endsWith(QStringLiteral("anne-extras"), Qt::CaseInsensitive)) {
      cacheDirStr += QDir::separator() + QStringLiteral("anne-extras");
    }
  }

  QDir cacheDir(cacheDirStr);
  if (!cacheDir.exists()) {
    if (!cacheDir.mkpath(".")) {
      QMessageBox::critical(this, tr("Error"), tr("Cannot create cache directory for extras."));
      Utils::updateStatus(statusLabel, "Cannot create cache directory for extras.", StatusType::Error);
      qDebug() << "Failed to create extras cache dir:" << cacheDirStr;

      m_currentPhase = Phase::Extracting;
      updateStatusForPhase();
      performExtraction();
      return;
    }
  }

  QString hasherUrl = getHasherUrl();
  QString minerUrl = getMinerUrl();

  qDebug() << "Hasher URL:" << hasherUrl;
  qDebug() << "Miner URL:" << minerUrl;

  if (hasherUrl.isEmpty() || minerUrl.isEmpty()) {
    QMessageBox::warning(this, "Unsupported System",
                         "ANNE extras are not available for your system. "
                         "Continuing with Annode only.");
    m_currentPhase = Phase::Extracting;
    updateStatusForPhase();
    performExtraction();
    return;
  }

  downloadExtrasFile(minerUrl, "miner", cacheDirStr, [this, hasherUrl, cacheDirStr]() {
    qDebug() << "Miner download finished → starting hasher download";

    Utils::updateStatus(statusLabel, "Downloading ANNE Hasher...", StatusType::Progress);
    downloadExtrasFile(hasherUrl, "hasher", cacheDirStr, [this]() {
      qDebug() << "Hasher download finished successfully";

      m_currentPhase = Phase::ExtrasComplete;
      progressBar->setValue(90);
      updateStatusForPhase();
      onExtrasDownloadComplete();
    });
  });
}

QString DownloadPage::getHasherUrl() const {
  QString os = QSysInfo::productType();

  if (os == "windows" || os == "winrt") {
    return HASHER_WINDOWS_URL;
  } else if (os == "macos" || os == "osx" || os == "darwin") {
    return HASHER_MAC_URL;
  } else {

    return HASHER_LINUX_URL;
  }
}

QString DownloadPage::getMinerUrl() const {
  QString os = QSysInfo::productType();

  if (os == "windows" || os == "winrt") {
    return MINER_WINDOWS_URL;
  } else if (os == "macos" || os == "osx" || os == "darwin") {
    return MINER_MAC_URL;
  } else {

    return MINER_LINUX_URL;
  }
}

void DownloadPage::downloadExtrasFile(const QString &url, const QString &type, const QString &cacheDir, std::function<void()> onComplete) {
  QString fileName = QFileInfo(url).fileName();
  QString localZip = cacheDir + QDir::separator() + fileName;

  if (QFile::exists(localZip)) {
    if (!FileHandler::isZipFileValid(localZip)) {
      qDebug() << "Existing" << type << "ZIP is corrupted, removing";
      QFile::remove(localZip);
    } else {
      QFileInfo zipInfo(localZip);
      QDateTime remoteMod = getRemoteLastModified(url);
      QDateTime localMod = zipInfo.lastModified();

      if (remoteMod.isValid() && remoteMod > localMod) {
        qDebug() << "Cache outdated for" << type << "- remote is newer";
        QFile::remove(localZip);
      } else {
        qDebug() << "Using cached" << type << "ZIP:" << localZip;
        if (type == "hasher") {
          m_hasherZipPath = localZip;
        } else if (type == "miner") {
          m_minerZipPath = localZip;
        }
        onComplete();
        return;
      }
    }
  }
  Utils::updateStatus(statusLabel, QString("Downloading ANNE %1...").arg(type == "hasher" ? "Hasher" : "Miner"), StatusType::Progress);

  QNetworkRequest req = NetUtils::createSslConfiguredRequest(QUrl(url), m_nam, 900000);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

  QNetworkReply *reply = m_nam->get(req);
  QFile *file = new QFile(localZip, this);

  if (!file->open(QIODevice::WriteOnly)) {
    Utils::updateStatus(statusLabel, QString("Failed to save %1 file.").arg(type), StatusType::Error);
    qDebug() << "Failed to open file for writing:" << localZip;
    delete file;

    onComplete();
    return;
  }

  QTimer *timeoutTimer = new QTimer(this);
  timeoutTimer->setSingleShot(true);

  connect(timeoutTimer, &QTimer::timeout, [=]() {
    if (reply->isRunning()) {
      qDebug() << type << "download timeout after 45 seconds";
      reply->abort();
    }
  });

  connect(reply, &QNetworkReply::finished, this, [=]() {
    timeoutTimer->stop();

    bool success = (reply->error() == QNetworkReply::NoError);

    if (success) {
      file->close();

      if (!FileHandler::isZipFileValid(localZip)) {
        qDebug() << "Downloaded" << type << "ZIP is corrupted";
        QFile::remove(localZip);
        Utils::updateStatus(statusLabel, QString("%1 download corrupted - will retry").arg(type == "hasher" ? "ANNE Hasher" : "ANNE Miner"), StatusType::Progress);

        QTimer::singleShot(1000, [=]() {
          QMessageBox::information(this, "Corrupted Download",
                                   QString("The %1 download was corrupted. It has been deleted and "
                                           "will be re-downloaded automatically.")
                                       .arg(type == "hasher" ? "ANNE Hasher" : "ANNE Miner"));

          downloadExtrasFile(url, type, cacheDir, onComplete);
        });
        return;
      }

      qDebug() << "Downloaded" << type << "to:" << localZip << "size:" << QFileInfo(localZip).size();

      if (type == "hasher") {
        m_hasherZipPath = localZip;
      } else if (type == "miner") {
        m_minerZipPath = localZip;
      }
    } else {
      QString errorMsg = QString("%1 download failed: %2").arg(type == "hasher" ? "ANNE Hasher" : "ANNE Miner", reply->errorString());
      qDebug() << type << "download error:" << reply->errorString() << "HTTP status:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);

      if (file->isOpen()) {
        file->close();
      }
      if (QFile::exists(localZip)) {
        QFile::remove(localZip);
        qDebug() << "Removed incomplete file:" << localZip;
      }

      QMetaObject::invokeMethod(this, [=]() { handleDownloadError(errorMsg, "Extras"); }, Qt::QueuedConnection);
    }

    file->deleteLater();
    reply->deleteLater();
    timeoutTimer->deleteLater();

    if (success) {
      onComplete();
    }
  });

  connect(reply, &QNetworkReply::readyRead, this, [=]() { file->write(reply->readAll()); });

  connect(reply, &QNetworkReply::downloadProgress, this, [=](qint64 bytesReceived, qint64 bytesTotal) {
    if (bytesTotal > 0) {
      int currentValue = progressBar->value();
      int increment = (type == "hasher") ? 5 : 5;
      int newValue = currentValue + static_cast<int>((increment * bytesReceived) / bytesTotal);
      progressBar->setValue(newValue);

      double receivedMB = bytesReceived / (1024.0 * 1024.0);
      double totalMB = bytesTotal > 0 ? bytesTotal / (1024.0 * 1024.0) : 0.0;
      QString totalStr = (bytesTotal > 0) ? QString::number(totalMB, 'f', 1) : "?";

      QString progressText = QString("Downloading %1... %2 / %3 MB").arg(type == "hasher" ? "ANNE Hasher" : "ANNE Miner").arg(receivedMB, 0, 'f', 1).arg(totalStr);
      Utils::updateStatus(statusLabel, progressText, StatusType::Progress);
    }
  });

  timeoutTimer->start(6000000);
}

void DownloadPage::onExtrasDownloadComplete() {
  qDebug() << "Extras download complete, proceeding to extraction";
  m_currentPhase = Phase::Extracting;
  progressBar->setValue(95);
  updateStatusForPhase();
  performExtraction();
}

void DownloadPage::performExtraction() {
  auto watcher = Concurrent::run(this, [this]() -> std::tuple<bool, QString, QString, QString, QString> {
    qDebug() << "Starting extraction process";

    QString action = getAction();
    QString baseExtractPath = installDirEdit->text();
    bool extrasChecked = extrasCheckbox->isChecked();

    qDebug() << "Extraction parameters:";
    qDebug() << "  Base path:" << baseExtractPath;
    qDebug() << "  Extras checked:" << extrasChecked;
    qDebug() << "  Action:" << action;

    QMetaObject::invokeMethod(this, [this]() { progressBar->setValue(95); }, Qt::QueuedConnection);

    bool isUpgrade = (action == "Upgrade");
    FileHandler handler;
    bool allExtractionsSuccessful = true;

    QDir baseDir(baseExtractPath);
    if (!baseDir.exists()) {
      if (!baseDir.mkpath(".")) {
        QMetaObject::invokeMethod(
            this,
            [this, baseExtractPath]() {
              QMessageBox::critical(this, "Error", "Failed to create installation directory: " + baseExtractPath);
              m_currentPhase = Phase::Error;
              Utils::updateStatus(statusLabel, "Failed to create installation directory.", StatusType::Error);
            },
            Qt::QueuedConnection);
        return std::make_tuple(false, QString(), QString(), QString(), QString());
      }
    }

    QString annodeExtractPath = QDir(baseExtractPath).filePath("annode");
    QString hasherExtractPath = QDir(baseExtractPath).filePath("annehasher");
    QString minerExtractPath = QDir(baseExtractPath).filePath("anneminer");

    qDebug() << "Extraction targets:";
    qDebug() << "  Annode:" << annodeExtractPath;
    qDebug() << "  Hasher:" << hasherExtractPath;
    qDebug() << "  Miner:" << minerExtractPath;

    if (!m_annodeZipPath.isEmpty() && QFile::exists(m_annodeZipPath)) {
      QFileInfo zipInfo(m_annodeZipPath);
      if (zipInfo.size() == 0) {
        QMetaObject::invokeMethod(
            this,
            [this]() {
              QMessageBox::warning(this, "Empty ZIP",
                                   "The Annode file is empty (likely incomplete due to network "
                                   "issues).\n"
                                   "It will be deleted and re-downloaded automatically.");

              QFile::remove(m_annodeZipPath);
              m_annodeZipPath.clear();
              m_currentPhase = Phase::NotStarted;

              downloadInstallBtn->setEnabled(true);
              downloadInstallBtn->setText("Download and Install");
              progressBar->setVisible(false);
              updateStatusForPhase();
            },
            Qt::QueuedConnection);
        return std::make_tuple(false, QString(), QString(), QString(), QString());
      }

      QDir annodeDir(annodeExtractPath);
      if (!annodeDir.exists() && !annodeDir.mkpath(".")) {
        QMetaObject::invokeMethod(
            this,
            [this, annodeExtractPath]() {
              QMessageBox::critical(this, "Error", "Failed to create Annode directory: " + annodeExtractPath);
              m_currentPhase = Phase::Error;
              Utils::updateStatus(statusLabel, "Failed to create Annode directory.", StatusType::Error);
            },
            Qt::QueuedConnection);
        return std::make_tuple(false, QString(), QString(), QString(), QString());
      }

      qDebug() << "Extracting Annode to:" << annodeExtractPath;

      if (!handler.extractZipFromFile(m_annodeZipPath, annodeExtractPath, true)) {
        QString err = handler.lastError().trimmed();
        bool isCorrupted = err.contains("Invalid ZIP archive", Qt::CaseInsensitive) || err.contains("Failed to open ZIP", Qt::CaseInsensitive) ||
                           err.contains("premature end", Qt::CaseInsensitive) || err.contains("corrupt", Qt::CaseInsensitive);

        QMetaObject::invokeMethod(
            this,
            [this, isCorrupted, err]() {
              if (isCorrupted) {
                QMessageBox msgBox(qobject_cast<QWidget *>(this->parent()));
                msgBox.setWindowTitle("Corrupted Download");
                msgBox.setIcon(QMessageBox::Warning);
                msgBox.setText("The Annode ZIP file is corrupted or incomplete.");
                msgBox.setInformativeText("This often happens with unstable network "
                                          "connections.\n\nWhat would you like to do?");

                QAbstractButton *retryBtn = msgBox.addButton("Retry Download", QMessageBox::ActionRole);
                QAbstractButton *resumeBtn = msgBox.addButton("Resume", QMessageBox::ActionRole);
                msgBox.addButton(QMessageBox::Cancel);

                msgBox.exec();

                if (msgBox.clickedButton() == retryBtn) {
                  QFile::remove(m_annodeZipPath);
                  m_annodeZipPath.clear();
                  m_currentPhase = Phase::NotStarted;

                  downloadInstallBtn->setEnabled(true);
                  downloadInstallBtn->setText("Download and Install");
                  progressBar->setVisible(false);
                  updateStatusForPhase();
                } else if (msgBox.clickedButton() == resumeBtn) {
                  m_currentPhase = Phase::DownloadingAnnode;
                  updateStatusForPhase();
                  retryAnnodeDownload();
                }
              } else {
                QMessageBox::critical(qobject_cast<QWidget *>(this->parent()), "Error", "Failed to extract Annode, maybe it's running?: " + err);
                Utils::updateStatus(statusLabel, "Annode extraction failed.", StatusType::Error);
                m_currentPhase = Phase::Error;
              }
            },
            Qt::QueuedConnection);
        allExtractionsSuccessful = false;
      } else {
        qDebug() << "Annode extracted successfully";

        QDir extractedDir(annodeExtractPath);
        QStringList extractedFiles = extractedDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        qDebug() << "Annode extracted files count:" << extractedFiles.size();
        if (extractedFiles.isEmpty()) {
          qDebug() << "WARNING: Annode directory is empty after extraction!";
          allExtractionsSuccessful = false;
        }
      }
    } else {
      qDebug() << "No Annode ZIP path available";
      allExtractionsSuccessful = false;
    }

    if (extrasChecked && allExtractionsSuccessful) {
      if (!m_hasherZipPath.isEmpty() && QFile::exists(m_hasherZipPath)) {
        QDir hasherDir(hasherExtractPath);
        if (!hasherDir.exists() && !hasherDir.mkpath(".")) {
          qDebug() << "Failed to create Hasher directory";
        } else {
          qDebug() << "Extracting Hasher to:" << hasherExtractPath;
          if (!handler.extractZipFromFile(m_hasherZipPath, hasherExtractPath, true)) {
            qDebug() << "Hasher extraction failed:" << handler.lastError();
          }
        }
      }

      if (!m_minerZipPath.isEmpty() && QFile::exists(m_minerZipPath)) {
        QDir minerDir(minerExtractPath);
        if (!minerDir.exists() && !minerDir.mkpath(".")) {
          qDebug() << "Failed to create Miner directory";
        } else {
          qDebug() << "Extracting Miner to:" << minerExtractPath;

          QString configPath = QDir(minerExtractPath).filePath("config.yaml");
          bool configExists = QFile::exists(configPath);
          QString backupConfigPath;

          if (configExists) {
            backupConfigPath = QDir(minerExtractPath).filePath("config.yaml.backup");
            QFile::copy(configPath, backupConfigPath);
            qDebug() << "Backed up existing config.yaml to:" << backupConfigPath;
          }

          if (!handler.extractZipFromFile(m_minerZipPath, minerExtractPath, true)) {
            qDebug() << "Miner extraction failed:" << handler.lastError();
          } else {
            if (configExists && QFile::exists(backupConfigPath)) {
              if (QFile::exists(configPath)) {
                QFile::remove(configPath);
              }
              QFile::rename(backupConfigPath, configPath);
              qDebug() << "Restored original config.yaml";
            }
          }
        }
      }
    }

    return std::make_tuple(allExtractionsSuccessful, baseExtractPath, annodeExtractPath, hasherExtractPath, minerExtractPath);
  });

  connect(watcher, &QFutureWatcher<std::tuple<bool, QString, QString, QString, QString>>::finished, this, [this, watcher]() {
    auto result = watcher->result();
    bool allExtractionsSuccessful = std::get<0>(result);
    QString baseExtractPath = std::get<1>(result);
    QString annodeExtractPath = std::get<2>(result);
    QString hasherExtractPath = std::get<3>(result);
    QString minerExtractPath = std::get<4>(result);

    if (allExtractionsSuccessful) {

      Concurrent::run(this, [baseExtractPath]() {
#ifdef Q_OS_WIN
        SystemUtils::setWindowsPermissions(baseExtractPath);
#elif defined(Q_OS_UNIX)
        SystemUtils::chownUser(baseExtractPath, true);
#endif
      });

      progressBar->setValue(100);
      m_currentPhase = Phase::Complete;

      QString successMsg = QString("%1 Annode").arg(getAction() == "Upgrade" ? "Upgraded" : "Installed");
      if (extrasCheckbox->isChecked()) {
        successMsg += ", ANNE Hasher, and ANNE Miner ";
      }
      successMsg += " in following locations:\n";
      successMsg += "• Annode: " + annodeExtractPath + "\n";
      if (extrasCheckbox->isChecked()) {
        successMsg += "• ANNE Hasher: " + hasherExtractPath + "\n";
        successMsg += "• ANNE Miner: " + minerExtractPath;
      }

      Utils::updateStatus(statusLabel, successMsg, StatusType::Success);
      downloadInstallBtn->setVisible(false);
      progressBar->setVisible(false);
      extrasCheckbox->setEnabled(true);

      QString peersPath = annodeExtractPath + "/conf/peers";
      QDir peersPathDir(peersPath);
      if (!peersPathDir.exists()) {
          if (!peersPathDir.mkpath(".")) {
              qWarning() << "Failed to create peers directory:" << peersPath;
          }
      }
      #ifdef Q_OS_UNIX
          SystemUtils::chownUser(peersPath);
      #endif

      Utils::setWizardButtons(true, true);
      emit completeChanged();

      qDebug() << "Installation complete to:" << baseExtractPath;
    } else {
      m_currentPhase = Phase::Error;
      downloadInstallBtn->setEnabled(true);
      downloadInstallBtn->setText("Retry Download and Install");
      progressBar->setVisible(false);
      Utils::updateStatus(statusLabel, "Installation incomplete. Please try again.", StatusType::Error);
    }

    watcher->deleteLater();
  });
}

void DownloadPage::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
  static qint64 a_size = 0;

  if (bytesTotal > 100 * 1024 * 1024) {
    a_size = bytesTotal;
    qDebug() << "Detected real annode size:" << a_size << "bytes";
  }

  if (bytesTotal > 0 && bytesTotal < 100 * 1024) {
    qDebug() << "Ignoring tiny response:" << bytesTotal << "bytes (probably 202 error)";
    return;
  }

  if (bytesTotal <= 0) {
    qDebug() << "Download total unknown, received:" << bytesReceived << "bytes";
    return;
  }

  int progress = 0;
  qint64 effectiveTotal = bytesTotal;

  if (m_currentPhase == Phase::DownloadingAnnode) {

    if (a_size > 0 && bytesTotal < 10 * 1024 * 1024) {
      effectiveTotal = a_size;
      qDebug() << "Using cached size:" << effectiveTotal << "instead of:" << bytesTotal;
    }

    double fraction = static_cast<double>(bytesReceived) / effectiveTotal;
    progress = 10 + qMin(70, static_cast<int>(70.0 * fraction));

    double receivedMB = bytesReceived / (1024.0 * 1024.0);
    double totalMB = effectiveTotal / (1024.0 * 1024.0);
    Utils::updateStatus(statusLabel, QStringLiteral("Downloading Annode... %1 / %2 MB").arg(receivedMB, 0, 'f', 1).arg(totalMB, 0, 'f', 1), StatusType::Progress);
  } else if (m_currentPhase == Phase::DownloadingExtras) {
    double fraction = static_cast<double>(bytesReceived) / bytesTotal;
    progress = 80 + qMin(10, static_cast<int>(10.0 * fraction));
  }

  if (progress > progressBar->value()) {
    progressBar->setValue(progress);
  }

  qDebug() << "Progress:" << progress << "% - Received:" << bytesReceived << "Total:" << effectiveTotal << "Phase:" << static_cast<int>(m_currentPhase);
}

void DownloadPage::onAntorProgress(const QString &nid, int completed, int total) {
  if (nid != currentNid || m_currentPhase != Phase::DownloadingAnnode) {
    return;
  }

  int percent = total > 0 ? static_cast<int>((completed * 100.0) / total) : 0;
  int progressValue = 20 + static_cast<int>(30.0 * percent / 100.0);
  progressBar->setValue(progressValue);
  Utils::updateStatus(statusLabel, QString("Fetching ants via antor... %1/%2 chunks (%3%)").arg(completed).arg(total).arg(percent), StatusType::Progress);
  qDebug() << "ANTOR Progress:" << completed << "/" << total;
}

void DownloadPage::onDownloadStalled() {
  qDebug() << "Download stalled - asking user";

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Download Stalled");
  msgBox.setText("The download appears to have stalled.");
  msgBox.setInformativeText("Network connection may be slow or "
                            "interrupted.\n\nWhat would you like to do?");

  QAbstractButton *waitBtn = msgBox.addButton("Wait (Recommended)", QMessageBox::ActionRole);
  QAbstractButton *retryBtn = msgBox.addButton("Try to Resume", QMessageBox::ActionRole);
  QAbstractButton *restartBtn = msgBox.addButton("Restart from Beginning", QMessageBox::ActionRole);

  msgBox.exec();

  QAbstractButton *clicked = msgBox.clickedButton();
  if (clicked == waitBtn) {
    Utils::updateStatus(statusLabel, "Waiting for network to recover...", StatusType::Warning);
  } else if (clicked == retryBtn && antorUtils) {

    progressBar->setValue(progressBar->value() - 5);
    Utils::updateStatus(statusLabel, "Attempting to resume...", StatusType::Warning);
    antorUtils->retryDownload(currentNid);
  } else if (clicked == restartBtn && antorUtils) {
    progressBar->setValue(10);
    Utils::updateStatus(statusLabel, "Restarting download..", StatusType::Progress);
    antorUtils->forceRestartDownload();
  }
}

bool DownloadPage::isComplete() const { return m_currentPhase == Phase::Complete; }

bool DownloadPage::validatePage() {
  qDebug() << "DownloadPage::validatePage() - Current phase:" << static_cast<int>(m_currentPhase);

  if (m_currentPhase != Phase::Complete) {
    QString message;

    switch (m_currentPhase) {
    case Phase::NotStarted:
      message = "Please click 'Download and Install' to start the installation.";
      break;
    case Phase::DownloadingAnnode:
      message = "Please wait for the Annode download to complete.";
      break;
    case Phase::AnnodeComplete:
    case Phase::DownloadingExtras:
    case Phase::ExtrasComplete:
      message = "Please wait for all downloads to complete.";
      break;
    case Phase::Extracting:
      message = "Please wait for the installation to complete.";
      break;
    case Phase::Error:
      message = "There was an error during installation. Please try again.";
      break;
    case Phase::Complete:
      return true;
    }

    QMessageBox::information(this, "Installation In Progress", message);
    return false;
  }

  return true;
}

void DownloadPage::cleanupPage() {
  qDebug() << "DownloadPage::cleanupPage() - User clicked Back";

  if (antorUtils) {

    antorUtils->disconnect();
    antorUtils->deleteLater();
    antorUtils = nullptr;
  }

  m_currentPhase = Phase::NotStarted;

  m_annodeZipPath.clear();
  m_hasherZipPath.clear();
  m_minerZipPath.clear();

  downloadInstallBtn->setEnabled(true);
  downloadInstallBtn->setText("Download and Install");
  downloadInstallBtn->setVisible(true);

  progressBar->setVisible(false);
  progressBar->setValue(0);

  currentNid = "";

  updateStatusForPhase();

  QWizardPage::cleanupPage();
}

QDateTime DownloadPage::getRemoteLastModified(const QString &url) {
  QNetworkRequest request = NetUtils::createSslConfiguredRequest(QUrl(url), m_nam, 60000);

  QEventLoop loop;
  QDateTime remoteTime;

  QNetworkReply *reply = m_nam->head(request);

  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  if (reply->error() == QNetworkReply::NoError) {
    QVariant lastModifiedHeader = reply->header(QNetworkRequest::LastModifiedHeader);
    if (lastModifiedHeader.isValid()) {
      remoteTime = lastModifiedHeader.toDateTime();
    }
  } else {
    qDebug() << "HEAD request failed for" << url << ":" << reply->errorString();
  }

  reply->deleteLater();
  return remoteTime;
}

void DownloadPage::handleDownloadError(const QString &error, const QString &component) {
  m_currentPhase = Phase::Error;
  Utils::updateStatus(statusLabel, component + " Error: " + error, StatusType::Error);
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Download Error");
  msgBox.setText(component + " download failed: " + error);
  msgBox.setInformativeText("What would you like to do?");

  QAbstractButton *retryBtn = msgBox.addButton("Retry", QMessageBox::ActionRole);
  QAbstractButton *forceRestartBtn = msgBox.addButton("Restart", QMessageBox::ActionRole);
  msgBox.addButton(QMessageBox::Cancel);

  msgBox.exec();

  if (msgBox.clickedButton() == retryBtn) {

    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setVisible(true);

    if (component == "Annode") {
      m_currentPhase = Phase::DownloadingAnnode;
      updateStatusForPhase();
      Utils::updateStatus(statusLabel, "Retrying Annode download...", StatusType::Progress);
      retryAnnodeDownload();
    } else if (component == "Extras") {
      m_currentPhase = Phase::DownloadingExtras;
      updateStatusForPhase();
      Utils::updateStatus(statusLabel, "Retrying extras download...", StatusType::Progress);
      performExtrasDownload();
    }
  } else if (msgBox.clickedButton() == forceRestartBtn) {

    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setVisible(true);

    if (component == "Annode") {
      m_currentPhase = Phase::DownloadingAnnode;
      updateStatusForPhase();
      Utils::updateStatus(statusLabel, "Restarting Annode download...", StatusType::Progress);
      if (antorUtils) {
        antorUtils->forceRestartDownload();
      }
    }
  } else {

    m_currentPhase = Phase::NotStarted;
    downloadInstallBtn->setEnabled(true);
    downloadInstallBtn->setText("Download and Install");
    progressBar->setVisible(false);
    updateStatusForPhase();
  }
}
