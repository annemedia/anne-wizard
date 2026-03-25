#include "filehandler.h"
#include "../utils/utils.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

bool FileHandler::downloadZip(const QString &url, QByteArray &zipData) {
  qDebug() << "=== DOWNLOADING ZIP FROM:" << url << "===";

  QNetworkAccessManager nam;
  QEventLoop loop;
  QUrl qurl(url);
  QNetworkRequest req(qurl);
  QNetworkReply *reply = nam.get(req);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  if (reply->error() != QNetworkReply::NoError) {
    qWarning() << "Download failed:" << reply->errorString();
    m_lastError = "Download failed: " + reply->errorString();
    reply->deleteLater();
    return false;
  }

  zipData = reply->readAll();
  reply->deleteLater();

  qDebug() << "Downloaded" << zipData.size() << "bytes";

  return true;
}

bool FileHandler::extractZip(const QByteArray &zipData, const QString &targetDir, bool overwrite) {
  qDebug() << "=== EXTRACTING ZIP TO:" << targetDir << "===";

  if (zipData.isEmpty()) {
    m_lastError = "Empty ZIP data.";
    qWarning() << "Empty ZIP data provided";
    return false;
  }

  qDebug() << "Creating temporary file for" << zipData.size() << "bytes";

  QTemporaryFile tempFile;
  tempFile.setAutoRemove(false);
  if (!tempFile.open()) {
    m_lastError = "Failed to create temporary file for ZIP.";
    qWarning() << "Failed to open temp file";
    return false;
  }

  qDebug() << "Temp file name:" << tempFile.fileName();

  qint64 written = tempFile.write(zipData);
  tempFile.flush();
  tempFile.close();

  qDebug() << "Wrote" << written << "bytes to temp file";

  if (written != zipData.size()) {
    m_lastError = QString("Failed to write all data (wrote %1 of %2 bytes)").arg(written).arg(zipData.size());
    qWarning() << m_lastError;
    return false;
  }

  QString tempPath = tempFile.fileName();
  qDebug() << "Original temp path:" << tempPath;

  QFileInfo fileInfo(tempPath);
  if (!fileInfo.exists()) {
    m_lastError = "Temporary file was not created";
    qWarning() << "Temp file doesn't exist:" << tempPath;
    return false;
  }

  qDebug() << "Temp file exists, size:" << fileInfo.size() << "bytes";

  if (!tempPath.endsWith(".zip", Qt::CaseInsensitive)) {
    QString newPath = tempPath + ".zip";
    qDebug() << "Renaming to add .zip extension:" << newPath;
    if (!QFile::rename(tempPath, newPath)) {
      m_lastError = "Failed to rename temp file to .zip";
      qWarning() << "Rename failed from" << tempPath << "to" << newPath;
      return false;
    }
    tempPath = newPath;
    qDebug() << "New path:" << tempPath;
  } else {
    qDebug() << "File already has .zip extension, keeping as-is";
  }

  if (!QFile::exists(tempPath)) {
    m_lastError = "Temporary ZIP file not found after rename";
    qWarning() << "File doesn't exist:" << tempPath;
    return false;
  }

  qDebug() << "Final temp file:" << tempPath << "size:" << QFileInfo(tempPath).size() << "bytes";
  qDebug() << "Calling extractZipFromFile...";

  bool result = extractZipFromFile(tempPath, targetDir, overwrite);

  if (QFile::exists(tempPath)) {
    qDebug() << "Cleaning up temp file:" << tempPath;
    QFile::remove(tempPath);
  }

  qDebug() << "=== EXTRACT COMPLETE, result:" << (result ? "SUCCESS" : "FAILED") << "===";
  return result;
}

bool installUnzipForPlatform() {
  qDebug() << "=== INSTALLING UNZIP FOR PLATFORM ===";

#ifdef Q_OS_WIN
  qDebug() << "Windows: Using PowerShell Expand-Archive (no installation needed)";
  return true;

#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  qDebug() << "macOS: Checking for unzip...";

  QProcessResult brewCheckResult = Utils::executeProcess("which", {"brew"});
  if (brewCheckResult.exitCode == 0) {
    qDebug() << "Homebrew found, installing unzip...";
    QProcessResult installResult = Utils::executeProcess("brew", {"install", "unzip"});

    if (installResult.exitCode == 0) {
      qDebug() << "unzip installed via Homebrew";
      return true;
    } else {
      qWarning() << "Homebrew installation failed:" << installResult.stdErr;
    }
  }

  QProcessResult portCheckResult = Utils::executeProcess("which", {"port"});
  if (portCheckResult.exitCode == 0) {
    qDebug() << "MacPorts found, installing unzip...";
    QProcessResult installResult = Utils::executeProcess("sudo", {"port", "install", "unzip"});

    if (installResult.exitCode == 0) {
      qDebug() << "unzip installed via MacPorts";
      return true;
    } else {
      qWarning() << "MacPorts installation failed:" << installResult.stdErr;
    }
  }

  qDebug() << "macOS might already have unzip or user needs to install manually";
  return false;

#elif defined(Q_OS_UNIX)
  qDebug() << "Linux: Detecting package manager...";

  auto detectPackageManager = []() -> PkgManagerType {
    QStringList managers = {"apt", "dnf", "pacman"};
    for (const QString &mg : managers) {
      QProcessResult whichResult = Utils::executeProcess("which", {mg});
      if (whichResult.exitCode == 0) {
        qDebug() << "Found package manager:" << mg;
        if (mg == "apt") {
          return PkgManagerType::Apt;
        } else if (mg == "dnf") {
          return PkgManagerType::Dnf;
        } else if (mg == "pacman") {
          return PkgManagerType::Pacman;
        }
      }
    }
    qDebug() << "No package manager found";
    return PkgManagerType::Unknown;
  };

  PkgManagerType pkgType = detectPackageManager();

  if (pkgType == PkgManagerType::Unknown) {
    qWarning() << "Could not detect package manager";
    return false;
  }

  QString installCmd = Utils::getPlatformCommand(pkgType, "unzip");
  if (installCmd.isEmpty()) {
    qWarning() << "Could not generate install command for unzip";
    return false;
  }

  qDebug() << "Install command:" << installCmd;
  QProcessResult installResult = Utils::executeProcess("bash", {"-c", installCmd});

  bool success = (installResult.exitCode == 0);
  if (!success) {
    qWarning() << "Installation failed:" << installResult.stdErr;
  }

  qDebug() << "Installation" << (success ? "succeeded" : "failed");
  return success;

#else
  qWarning() << "Unknown platform";
  return false;
#endif
}

bool canExtractZip() {
#ifdef Q_OS_WIN
  qDebug() << "Checking for PowerShell Expand-Archive...";
  QProcessResult psResult = Utils::executeProcess("powershell", {"-Command", "Get-Command Expand-Archive"});
  bool available = (psResult.exitCode == 0);
  qDebug() << "PowerShell Expand-Archive:" << (available ? "AVAILABLE" : "NOT AVAILABLE");
  return available;

#else
  qDebug() << "Checking for unzip command...";
  QProcessResult whichResult = Utils::executeProcess("which", {"unzip"});
  bool available = (whichResult.exitCode == 0);
  qDebug() << "unzip command:" << (available ? "AVAILABLE" : "NOT AVAILABLE");
  return available;
#endif
}

bool FileHandler::extractZipFromFile(const QString &zipPath, const QString &targetDir, bool overwrite) {
  qDebug() << "=== EXTRACTING FROM FILE ===";
  qDebug() << "ZIP file:" << zipPath;
  qDebug() << "Target dir:" << targetDir;
  qDebug() << "Overwrite:" << overwrite;

  if (!QFile::exists(zipPath)) {
    m_lastError = "ZIP file not found: " + zipPath;
    qWarning() << m_lastError;
    return false;
  }

  qDebug() << "ZIP file exists, size:" << QFileInfo(zipPath).size() << "bytes";

  QDir target(targetDir);
  if (!target.exists()) {
    qDebug() << "Creating target directory:" << targetDir;
    if (!target.mkpath(".")) {
      m_lastError = "Failed to create target directory: " + targetDir;
      qWarning() << m_lastError;
      return false;
    }
    qDebug() << "Target directory created";
  } else {
    qDebug() << "Target directory already exists";
  }

#ifdef Q_OS_WIN
  qDebug() << "Windows: Using tar.exe (built-in, fast)";

  QStringList args;
  args << "-xf" << zipPath;
  args << "-C" << targetDir;

  if (!overwrite) {
    args << "--keep-old-files";
  }

  qDebug() << "Running: tar.exe" << args;
  QProcessResult extractResult = Utils::executeProcess("tar.exe", args, -1);

  if (!extractResult.QPSuccess) {
    m_lastError = "Failed to start tar.exe: " + extractResult.stdErr;
    qWarning() << m_lastError;
    return false;
  }

  if (extractResult.exitCode != 0) {
    m_lastError = QString("tar.exe extraction failed (exit code %1):\n%2\n%3").arg(extractResult.exitCode).arg(extractResult.stdErr).arg(extractResult.stdOut);
    qWarning() << "Extraction failed:" << m_lastError;
    return false;
  }

  qDebug() << "tar.exe extraction succeeded";

#else
  qDebug() << "Unix/macOS: Using unzip command";

  if (!canExtractZip()) {
    qDebug() << "unzip not found, attempting to install...";
    if (!installUnzipForPlatform()) {
      m_lastError =
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
          "Failed to install unzip. Please install it manually:\n"
          "  brew install unzip\n"
          "  or download from: https://www.unzip.com"
#else
          "Failed to install unzip. Please install it manually:\n"
          "Ubuntu/Debian: sudo apt install unzip\n"
          "Fedora/RHEL: sudo dnf install unzip\n"
          "Arch: sudo pacman -S unzip\n"
#endif
          ;
      qWarning() << m_lastError;
      return false;
    }

    if (!canExtractZip()) {
      m_lastError = "ZIP extraction tool installation failed";
      qWarning() << m_lastError;
      return false;
    }
    qDebug() << "unzip installed successfully";
  }

  QStringList args;
  args << "-q";

  if (overwrite) {
    args << "-o";
  }

  args << zipPath << "-d" << targetDir;

  qDebug() << "Running command: unzip" << args;
  QProcessResult extractResult = Utils::executeProcess("unzip", args, -1);

  if (!extractResult.QPSuccess) {
    m_lastError = "Failed to start unzip process: " + extractResult.stdErr;
    qWarning() << m_lastError;
    return false;
  }

  if (extractResult.exitCode != 0) {
    m_lastError = QString("Unzip failed with exit code %1\nError: %2\nOutput: %3").arg(extractResult.exitCode).arg(extractResult.stdErr).arg(extractResult.stdOut);
    qWarning() << "unzip failed:" << m_lastError;
    return false;
  }

  qDebug() << "unzip succeeded. Output:" << extractResult.stdOut;
  if (!extractResult.stdErr.isEmpty()) {
    qDebug() << "unzip warnings:" << extractResult.stdErr;
  }

#ifdef Q_OS_UNIX
  uid_t targetUid = geteuid();
  gid_t targetGid = getegid();
  bool isRoot = (targetUid == 0);

  if (isRoot) {
    qDebug() << "Running as root, checking for user ownership...";

    QByteArray sudoUidStr = qgetenv("SUDO_UID");
    if (!sudoUidStr.isEmpty()) {
      targetUid = sudoUidStr.toUInt();
      QByteArray sudoGidStr = qgetenv("SUDO_GID");
      targetGid = sudoGidStr.isEmpty() ? targetUid : sudoGidStr.toUInt();
      qDebug() << "Using SUDO_UID/GID:" << targetUid << targetGid;
    } else {
      QByteArray pkexecUidStr = qgetenv("PKEXEC_UID");
      if (!pkexecUidStr.isEmpty()) {
        targetUid = pkexecUidStr.toUInt();
        qDebug() << "Using PKEXEC_UID:" << targetUid;
        struct passwd *pw = getpwuid(targetUid);
        if (pw) {
          targetGid = pw->pw_gid;
          qDebug() << "Got GID from passwd:" << targetGid;
        }
      }
    }

    if (targetUid != 0) {
      qDebug() << "Setting ownership to UID:" << targetUid << "GID:" << targetGid;
      QProcessResult chownResult = Utils::executeProcess("chown", {"-R", QString("%1:%2").arg(targetUid).arg(targetGid), targetDir}, -1);

      if (chownResult.exitCode != 0) {
        qWarning() << "chown failed:" << chownResult.stdErr;
      } else {
        qDebug() << "Ownership set successfully";
      }
    }
  }
#endif
#endif

  qDebug() << "=== FILE EXTRACTION SUCCESSFUL ===";
  return true;
}

QDateTime FileHandler::getRemoteLastModified(const QString &urlStr) {
  qDebug() << "Getting last modified for:" << urlStr;

  QNetworkAccessManager localNam;
  QNetworkRequest req;
  req.setUrl(QUrl(urlStr));
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply *localReply = localNam.head(req);
  QEventLoop loop;
  QObject::connect(localReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();
  QDateTime remoteMod;
  if (localReply->error() == QNetworkReply::NoError) {
    QVariant lastModVar = localReply->header(QNetworkRequest::LastModifiedHeader);
    if (lastModVar.isValid()) {
      remoteMod = lastModVar.toDateTime();
    }
  } else {
    qWarning() << "Failed to get last modified:" << localReply->errorString();
  }
  qDebug() << "Remote Last-Modified:" << remoteMod;
  localReply->deleteLater();
  return remoteMod;
}

bool FileHandler::isZipFileValid(const QString &filePath) {
  QFile file(filePath);
  if (!file.exists()) {
    qDebug() << "ZIP file doesn't exist:" << filePath;
    return false;
  }

  qint64 fileSize = file.size();
  if (fileSize == 0) {
    qDebug() << "ZIP file is empty:" << filePath;
    return false;
  }

  if (fileSize < 22) {
    qDebug() << "ZIP file is too small to be valid:" << filePath << "size:" << fileSize;
    return false;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    qDebug() << "Failed to open ZIP file for reading:" << filePath;
    return false;
  }

  char header[4];
  if (file.read(header, 4) != 4) {
    file.close();
    return false;
  }

  if (static_cast<unsigned char>(header[0]) != 0x50 || static_cast<unsigned char>(header[1]) != 0x4B || static_cast<unsigned char>(header[2]) != 0x03 ||
      static_cast<unsigned char>(header[3]) != 0x04) {
    qDebug() << "Invalid ZIP header signature for:" << filePath;
    file.close();
    return false;
  }

  bool eocdFound = false;
  qint64 searchStart = qMax<qint64>(0, fileSize - 65536);
  const qint64 maxEOCDSize = 65536 + 22;

  const qint64 readSize = qMin<qint64>(maxEOCDSize, fileSize);
  if (!file.seek(fileSize - readSize)) {
    file.close();
    return false;
  }

  QByteArray tail = file.read(readSize);
  file.close();

  if (tail.size() < 22) {
    qDebug() << "Cannot read enough data to find EOCD:" << filePath;
    return false;
  }

  for (int i = tail.size() - 22; i >= 0; i--) {
    if (static_cast<unsigned char>(tail[i]) == 0x50 && static_cast<unsigned char>(tail[i + 1]) == 0x4B && static_cast<unsigned char>(tail[i + 2]) == 0x05 &&
        static_cast<unsigned char>(tail[i + 3]) == 0x06) {

      eocdFound = true;

      if (i + 21 < tail.size()) {

        quint16 totalEntries = static_cast<unsigned char>(tail[i + 10]) | (static_cast<unsigned char>(tail[i + 11]) << 8);

        quint32 cdSize = static_cast<unsigned char>(tail[i + 12]) | (static_cast<unsigned char>(tail[i + 13]) << 8) | (static_cast<unsigned char>(tail[i + 14]) << 16) |
                         (static_cast<unsigned char>(tail[i + 15]) << 24);

        quint32 cdOffset = static_cast<unsigned char>(tail[i + 16]) | (static_cast<unsigned char>(tail[i + 17]) << 8) | (static_cast<unsigned char>(tail[i + 18]) << 16) |
                           (static_cast<unsigned char>(tail[i + 19]) << 24);

        if (cdOffset >= static_cast<quint32>(fileSize)) {
          qDebug() << "Central directory offset exceeds file size:" << filePath << "offset:" << cdOffset << "size:" << fileSize;
          return false;
        }

        if (cdSize > static_cast<quint32>(fileSize)) {
          qDebug() << "Central directory size exceeds file size:" << filePath << "cdSize:" << cdSize << "fileSize:" << fileSize;
          return false;
        }

        if (totalEntries == 0xFFFF || cdSize == 0xFFFFFFFF || cdOffset == 0xFFFFFFFF) {

          if (i >= 20) {
            if (static_cast<unsigned char>(tail[i - 20]) == 0x50 && static_cast<unsigned char>(tail[i - 19]) == 0x4B &&
                static_cast<unsigned char>(tail[i - 18]) == 0x06 && static_cast<unsigned char>(tail[i - 17]) == 0x07) {
              qDebug() << "Detected ZIP64 format for:" << filePath;
            } else {
              qDebug() << "ZIP64 indicators found but no ZIP64 locator:" << filePath;
              return false;
            }
          }
        }

        qDebug() << "Valid ZIP structure found for:" << filePath << "entries:" << totalEntries << "cdSize:" << cdSize << "cdOffset:" << cdOffset;
      }
      break;
    }
  }

  if (!eocdFound) {
    qDebug() << "End of Central Directory not found in:" << filePath;
    return false;
  }

  return true;
}
