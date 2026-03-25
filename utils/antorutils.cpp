#include "antorutils.h"
#include "netutils.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUrl>
#include <sys/stat.h>
#include <unistd.h>
#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MAC)
#include <TargetConditionals.h>
#elif defined(Q_OS_LINUX)
#include <sys/utsname.h>
#endif

AntorUtils::AntorUtils(QObject *parent)
    : QObject(parent), m_currentState(DownloadState::Idle), m_nam(new QNetworkAccessManager(this)), m_currentDownloadReply(nullptr), m_currentFile(nullptr),
      m_resumeOffset(0), m_retryCount(0), m_lastChunksCompleted(0), m_lastChunksTotal(0), m_isStalled(false), m_lastBytesReceived(0),
      m_lastProgressTime(QDateTime::currentDateTime()), m_cacheValid(false), m_originalFileSize(0), m_currentDownloadType(DownloadType::Fileserve) {

  m_stallTimer = new QTimer(this);
  m_stallTimer->setInterval(10000);
  connect(m_stallTimer, &QTimer::timeout, this, &AntorUtils::checkForStall);
  configureForTorEnvironment();
  qDebug() << "[ANTOR] Initialized with baseUrl:" << m_baseUrl;
}

AntorUtils::~AntorUtils() { cleanup(); }

void AntorUtils::downloadFileByNid(const QString &nid, const QString &downloadPath, std::function<void(bool, const QString &)> callback) {
  qDebug() << "[ANTOR] downloadFileByNid - NID:" << nid << "path:" << downloadPath;

  cleanup();
  m_currentNid = nid;
  m_currentDownloadPath = downloadPath;
  m_currentCallback = callback;
  m_currentState = DownloadState::Downloading;
  m_originalFileSize = 0;
  emit statusMessage("Checking local files...");

  QFileInfo fi(downloadPath);
  if (fi.exists() && fi.size() > 0) {
    m_resumeOffset = fi.size();
    qDebug() << "[ANTOR] Found existing file, size:" << m_resumeOffset;
  } else if (fi.exists() && fi.size() == 0) {
    QFile::remove(downloadPath);
    qDebug() << "[ANTOR] Removed empty file";
    m_resumeOffset = 0;
  } else {
    m_resumeOffset = 0;
  }

  qDebug() << "[ANTOR] File check complete, resume offset:" << m_resumeOffset;
  startDownloadOrResume(nid, downloadPath, callback);
}

void AntorUtils::startDownloadOrResume(const QString &nid, const QString &downloadPath, std::function<void(bool, const QString &)> callback) {
  qDebug() << "[ANTOR] startDownloadWithResume - NID:" << nid << "resume offset:" << m_resumeOffset
           << "type:" << (m_currentDownloadType == DownloadType::Fileserve ? "fileserve" : "mirror");

  QUrl url;
  // bypassing fileserve in favor of mirror
  m_currentDownloadType = DownloadType::Mirror;
  if (m_currentDownloadType == DownloadType::Fileserve) {
    url = QUrl(QString("%1/fileserve/nid/%2").arg(m_baseUrl, nid));
    m_resumeOffset = 0;
  } else {
    if (m_currentDownloadPath.endsWith(".sql.zip", Qt::CaseInsensitive)) {
      url = QUrl(m_snapshotUrl);
    } else {
      Platform platform = detectPlatform();
      url = QUrl(platform == Platform::Windows ? m_windowsUrl : m_linuxUrl);
    }
  }
  QNetworkRequest request = NetUtils::createSslConfiguredRequest(url, m_nam);

  if (m_currentDownloadType == DownloadType::Mirror && m_resumeOffset > 0) {
    request.setRawHeader("Range", QString("bytes=%1-").arg(m_resumeOffset).toUtf8());
    qDebug() << "[ANTOR] Setting Range header for resume:" << m_resumeOffset;
    emit statusMessage(QString("Resuming download from %1 MB...").arg(m_resumeOffset / (1024.0 * 1024.0), 0, 'f', 1));
  } else {
    emit statusMessage("Starting download...");
  }

  m_currentCallback = callback;
  qDebug() << "[ANTOR] Sending request to URL:" << url.toString();
  QNetworkReply *reply = m_nam->get(request);
  m_currentDownloadReply = reply;

  connect(reply, &QNetworkReply::downloadProgress, this, &AntorUtils::handleDownloadProgress);
  connect(reply, &QNetworkReply::errorOccurred, this, &AntorUtils::handleDownloadError);

  QSharedPointer<bool> fileOpened(new bool(false));

  connect(reply, &QIODevice::readyRead, this, [this, reply, fileOpened]() {
    if (!(*fileOpened)) {
      qDebug() << "[ANTOR] First data received, opening file...";
      QIODevice::OpenMode mode;
      if (m_currentDownloadType == DownloadType::Mirror && m_resumeOffset > 0) {
        mode = QIODevice::ReadWrite | QIODevice::Append;
        qDebug() << "[ANTOR] Opening for resume, offset:" << m_resumeOffset;
        if (!openDownloadFile(mode)) {
          qDebug() << "[ANTOR] Cannot open file for resume";
          handleDownloadError(QNetworkReply::UnknownNetworkError);
          return;
        }

        qint64 actualSize = m_currentFile->size();
        if (actualSize != m_resumeOffset) {
          qDebug() << "[ANTOR] File size" << actualSize << "!= offset" << m_resumeOffset;
          if (actualSize > m_resumeOffset) {
            m_currentFile->resize(m_resumeOffset);
            qDebug() << "[ANTOR] Truncated file to resume offset";
          } else if (actualSize < m_resumeOffset) {
            qDebug() << "[ANTOR] Warning: File smaller than expected resume offset";
          }
        }
      } else {
        mode = QIODevice::WriteOnly;
        qDebug() << "[ANTOR] Opening fresh file";
        if (!openDownloadFile(mode)) {
          qDebug() << "[ANTOR] Cannot open file for writing";
          handleDownloadError(QNetworkReply::UnknownNetworkError);
          return;
        }
      }
      *fileOpened = true;
      qDebug() << "[ANTOR] File opened successfully";
    }

    if (!m_currentFile)
      return;
    static int chunkCounter = 0;
    const int SYNC_EVERY_N_CHUNKS = 50;

    static int logCounter = 0;
    const int LOG_EVERY_N_CHUNKS = 1000;
    while (reply->bytesAvailable() > 0) {
      QByteArray data = reply->read(8192);
      qint64 written = m_currentFile->write(data);
      if (written != data.size()) {
        qDebug() << "[ANTOR] Write error";
        handleDownloadError(QNetworkReply::UnknownNetworkError);
        return;
      }
      chunkCounter++;
      logCounter++;
      if (chunkCounter % SYNC_EVERY_N_CHUNKS == 0) {
        forceFileSync();
        chunkCounter = 0;
      }

      if (logCounter >= LOG_EVERY_N_CHUNKS && m_currentFile) {
        logCounter = 0;
        qDebug() << "[ANTOR] Downloaded" << m_currentFile->size() << "bytes so far";
      }
    }
  });

  connect(reply, &QNetworkReply::finished, [this, reply, fileOpened]() {
    if (reply->error() != QNetworkReply::NoError) {

      return;
    }
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug() << "[ANTOR] Finished - HTTP Status:" << statusCode;
    if (statusCode == 200 || statusCode == 206) {

      if (m_currentFile && reply->bytesAvailable() > 0) {
        QByteArray data = reply->readAll();
        qint64 written = m_currentFile->write(data);
        if (written != data.size()) {
          qDebug() << "[ANTOR] Final write error";
          handleDownloadError(QNetworkReply::UnknownNetworkError);
          return;
        }
      }

      if (m_currentFile) {
        forceFileSync();
        m_currentFile->close();
        delete m_currentFile;
        m_currentFile = nullptr;
      }
      m_currentState = DownloadState::Completed;
      m_stallTimer->stop();
      qint64 fileSize = QFileInfo(m_currentDownloadPath).size();
      qDebug() << "[ANTOR] Download completed:" << fileSize << "bytes";
      emit statusMessage("Download completed successfully");
      emit downloadProgress(fileSize, fileSize);
      if (m_currentCallback) {
        m_currentCallback(true, m_currentDownloadPath);
      }
    } else if (statusCode == 202) {
      qDebug() << "[ANTOR] 202 - File not ready, falling back to mirror";
      emit statusMessage("File not ready, using mirror download...");
      if (m_currentFile) {
        m_currentFile->close();
        qint64 partialSize = m_currentFile->size();
        delete m_currentFile;
        m_currentFile = nullptr;
        if (partialSize > 0) {
          QFile::remove(m_currentDownloadPath);
          qDebug() << "[ANTOR] Discarded partial from 202 response";
        }
      }

      m_resumeOffset = 0;
      m_currentState = DownloadState::Requesting;

      fallbackToMirrorDownload();
    } else {
      handleUnexpectedStatus(statusCode);
    }
    reply->deleteLater();
    m_currentDownloadReply = nullptr;
  });

  connect(reply, &QNetworkReply::metaDataChanged, this, [reply]() {
    qDebug() << "[ANTOR] Response headers:";
    auto headers = reply->rawHeaderList();
    for (const auto &header : headers) {
      qDebug() << "[ANTOR]   " << header << ":" << reply->rawHeader(header);
    }

    QByteArray contentRange = reply->rawHeader("Content-Range");
    if (!contentRange.isEmpty()) {
      qDebug() << "[ANTOR] Content-Range header:" << contentRange;
    }
  });

  m_stallTimer->start();
  m_lastProgressTime = QDateTime::currentDateTime();

  if (m_resumeOffset > 0) {
    emit statusMessage(QString("Resuming from %1 MB...").arg(m_resumeOffset / (1024.0 * 1024.0), 0, 'f', 1));
  } else {
    emit statusMessage("Connecting to server...");
  }
}

void AntorUtils::fallbackToMirrorDownload() {
  qDebug() << "[ANTOR] Starting fallback to mirror download";
  m_currentState = DownloadState::Requesting;
  m_currentDownloadType = DownloadType::Mirror;

  QString mirrorUrl;
  if (m_currentDownloadPath.endsWith(".sql.zip", Qt::CaseInsensitive)) {
    mirrorUrl = m_snapshotUrl;
    qDebug() << "[ANTOR] Using snapshot mirror URL:" << mirrorUrl;
  } else {
    Platform platform = detectPlatform();
    if (platform == Platform::Windows) {
      mirrorUrl = m_windowsUrl;
    } else {
      mirrorUrl = m_linuxUrl;
    }
    qDebug() << "[ANTOR] Using binary mirror URL:" << mirrorUrl;
  }

  QUrl url(mirrorUrl);
  QNetworkRequest request = NetUtils::createSslConfiguredRequest(url, m_nam);
  qDebug() << "[ANTOR] Starting mirror download from:" << mirrorUrl;
  m_currentState = DownloadState::Downloading;

  QFileInfo fi(m_currentDownloadPath);
  m_resumeOffset = fi.exists() ? fi.size() : 0;

  QString tempMirrorPath = m_currentDownloadPath + ".mirror";
  QString finalPath = m_currentDownloadPath;

  if (QFile::exists(tempMirrorPath)) {
    QFile::remove(tempMirrorPath);
  }

  if (m_resumeOffset == 0 && QFile::exists(finalPath)) {
    QFile::remove(finalPath);
    qDebug() << "[ANTOR] Removed existing file for fresh mirror download";
  }

  if (m_resumeOffset > 0) {
    request.setRawHeader("Range", QString("bytes=%1-").arg(m_resumeOffset).toUtf8());
    qDebug() << "[ANTOR] Mirror download resuming from:" << m_resumeOffset << "to temp file:" << tempMirrorPath;
    emit statusMessage(QString("Resuming mirror download from %1 MB...").arg(m_resumeOffset / (1024.0 * 1024.0), 0, 'f', 1));
  } else {
    emit statusMessage("Starting mirror download...");
  }

  QString originalDownloadPath = m_currentDownloadPath;
  m_currentDownloadPath = tempMirrorPath;

  if (!openDownloadFile(QIODevice::WriteOnly)) {
    qDebug() << "[ANTOR] Cannot open temp file for mirror download";
    m_currentState = DownloadState::Error;
    m_currentDownloadPath = originalDownloadPath;
    if (m_currentCallback) {
      m_currentCallback(false, "Cannot open temp file for writing");
    }
    return;
  }

  QNetworkReply *reply = m_nam->get(request);
  m_currentDownloadReply = reply;
  connect(reply, &QNetworkReply::downloadProgress, this, &AntorUtils::handleDownloadProgress);
  connect(reply, &QNetworkReply::errorOccurred, this, &AntorUtils::handleDownloadError);

  connect(reply, &QIODevice::readyRead, this, [this, reply]() {
    if (!m_currentFile)
      return;
    static int chunkCounter = 0;
    const int SYNC_EVERY_N_CHUNKS = 50;
    while (reply->bytesAvailable() > 0) {
      QByteArray data = reply->read(8192);
      qint64 written = m_currentFile->write(data);
      if (written != data.size()) {
        qDebug() << "[ANTOR] Mirror write error";
        handleDownloadError(QNetworkReply::UnknownNetworkError);
        return;
      }
      chunkCounter++;
      if (chunkCounter % SYNC_EVERY_N_CHUNKS == 0) {
        forceFileSync();
        chunkCounter = 0;
      }
    }
  });

  connect(reply, &QNetworkReply::finished, [this, reply, originalDownloadPath, tempMirrorPath, finalPath]() {
    if (reply->error() != QNetworkReply::NoError) {
      m_currentDownloadPath = originalDownloadPath;
      return;
    }
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug() << "[ANTOR] Mirror download finished - HTTP Status:" << statusCode;
    if (statusCode == 200 || statusCode == 206) {
      if (m_currentFile && reply->bytesAvailable() > 0) {
        QByteArray data = reply->readAll();
        m_currentFile->write(data);
      }
      if (m_currentFile) {
        forceFileSync();
        m_currentFile->close();
        delete m_currentFile;
        m_currentFile = nullptr;
      }

      if (m_resumeOffset > 0) {
        QFile originalFile(finalPath);
        QFile tempFile(tempMirrorPath);

        if (originalFile.open(QIODevice::Append) && tempFile.open(QIODevice::ReadOnly)) {
          QByteArray data = tempFile.readAll();
          originalFile.write(data);
          originalFile.close();
          tempFile.close();
          QFile::remove(tempMirrorPath);
          qDebug() << "[ANTOR] Appended" << data.size() << "bytes from temp to original file";
        } else {
          qDebug() << "[ANTOR] Failed to append temp to original";
        }
      } else {
        if (QFile::rename(tempMirrorPath, finalPath)) {
          qDebug() << "[ANTOR] Renamed mirror download to expected name:" << finalPath;
        } else {
          qDebug() << "[ANTOR] Failed to rename mirror download file";
        }
      }

      m_currentDownloadPath = finalPath;
      m_currentState = DownloadState::Completed;
      m_stallTimer->stop();
      qint64 fileSize = QFileInfo(finalPath).size();
      qDebug() << "[ANTOR] Mirror download completed:" << fileSize << "bytes";
      emit statusMessage("Mirror download completed successfully");
      emit downloadProgress(fileSize, fileSize);
      if (m_currentCallback) {
        m_currentCallback(true, finalPath);
      }
    } else {
      m_currentDownloadPath = originalDownloadPath;
      handleUnexpectedStatus(statusCode);
    }
    reply->deleteLater();
    m_currentDownloadReply = nullptr;
  });

  m_stallTimer->start();
  m_lastProgressTime = QDateTime::currentDateTime();
  emit statusMessage("Starting mirror download...");
}

void AntorUtils::startDirectDownload(QNetworkReply *reply) {
  qDebug() << "[ANTOR] Starting direct download";
  emit statusMessage("Starting download...");
  m_currentDownloadReply = reply;
  if (!openDownloadFile(QIODevice::WriteOnly)) {
    finishWithError("Cannot open file for writing");
    return;
  }
  connect(reply, &QIODevice::readyRead, this, [this, reply]() {
    if (!m_currentFile)
      return;
    QByteArray data = reply->readAll();
    qint64 written = m_currentFile->write(data);
    m_currentFile->flush();
#ifdef Q_OS_UNIX
    if (m_currentFile->handle() >= 0) {
      fsync(m_currentFile->handle());
    }
#endif
    if (written != data.size()) {
      qDebug() << "[ANTOR] Write error";
      handleDownloadError(QNetworkReply::UnknownNetworkError);
    }
  });
  connect(reply, &QNetworkReply::finished, this, &AntorUtils::handleDownloadCompletion);
}

void AntorUtils::startResumedDownload(QNetworkReply *reply) {
  qDebug() << "[ANTOR] Starting resumed download";
  emit statusMessage("Resuming download...");
  m_currentDownloadReply = reply;
  if (!openDownloadFile(QIODevice::Append)) {
    finishWithError("Cannot open file for appending");
    return;
  }
  connect(reply, &QIODevice::readyRead, this, [this, reply]() {
    if (!m_currentFile)
      return;
    static int chunkCounter = 0;
    const int SYNC_EVERY_N_CHUNKS = 50;
    while (reply->bytesAvailable() > 0) {
      QByteArray data = reply->read(8192);
      qint64 written = m_currentFile->write(data);
      if (written != data.size()) {
        qDebug() << "[ANTOR] Write error";
        handleDownloadError(QNetworkReply::UnknownNetworkError);
        return;
      }
      chunkCounter++;
      if (chunkCounter >= SYNC_EVERY_N_CHUNKS) {
        forceFileSync();
        chunkCounter = 0;
      }
    }
    m_currentFile->flush();
  });
  connect(reply, &QNetworkReply::finished, this, &AntorUtils::handleDownloadCompletion);
}

void AntorUtils::forceFileSync() {
  if (m_currentFile && m_currentFile->isOpen()) {
    m_currentFile->flush();
#ifdef Q_OS_UNIX
    if (m_currentFile->handle() >= 0) {
      fsync(m_currentFile->handle());
    }
#elif defined(Q_OS_WIN)
    FlushFileBuffers((HANDLE)_get_osfhandle(m_currentFile->handle()));
#endif
  }
}

void AntorUtils::handleDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
  m_lastProgressTime = QDateTime::currentDateTime();
  if (m_resumeOffset == 0 && bytesTotal > 0 && m_originalFileSize == 0) {
    m_originalFileSize = bytesTotal;
    qDebug() << "[ANTOR] Stored original file size:" << m_originalFileSize;
  }
  qint64 totalReceived = m_resumeOffset + bytesReceived;
  qint64 totalSize;
  if (bytesTotal <= 0) {
    totalSize = 0;
  } else if (m_resumeOffset > 0) {
    if (m_originalFileSize > 0) {
      totalSize = m_originalFileSize;
      qDebug() << "[ANTOR] Using stored original size for resume:" << totalSize;
    } else {
      qint64 remainingSize = bytesTotal - m_resumeOffset;
      if (remainingSize > 0) {
        totalSize = bytesTotal;
      } else {
        totalSize = m_resumeOffset + bytesTotal;
        qDebug() << "[ANTOR] Correct server - calculated total:" << totalSize;
      }
    }
  } else {
    totalSize = bytesTotal;
  }
  if (totalSize > 0 && totalReceived > totalSize) {
    qDebug() << "[ANTOR] WARNING: Received" << totalReceived << "exceeds total" << totalSize << "- clamping";
    totalReceived = totalSize;
  }
  emit downloadProgress(totalReceived, totalSize);
  if (totalSize > 0) {
    double receivedMB = totalReceived / (1024.0 * 1024.0);
    double totalMB = totalSize / (1024.0 * 1024.0);
    int percent = 0;

    if (totalSize > 0) {
      percent = static_cast<int>((totalReceived * 100) / totalSize);
      if (percent > 100)
        percent = 100;
      if (percent < 0)
        percent = 0;
    }
    emit statusMessage(QString("Downloading... %1 MB / %2 MB (%3%)").arg(receivedMB, 0, 'f', 1).arg(totalMB, 0, 'f', 1).arg(percent));
  } else {
    double receivedMB = totalReceived / (1024.0 * 1024.0);
    emit statusMessage(QString("Downloading... %1 MB").arg(receivedMB, 0, 'f', 1));
  }
}

void AntorUtils::handleDownloadCompletion() {
  QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
  if (!reply)
    return;
  if (reply->error() == QNetworkReply::NoError) {
    if (m_currentFile && reply->bytesAvailable() > 0) {
      QByteArray data = reply->readAll();
      m_currentFile->write(data);
      m_currentFile->flush();
    }
    if (m_currentFile) {
      m_currentFile->close();
      delete m_currentFile;
      m_currentFile = nullptr;
    }
    m_currentState = DownloadState::Completed;
    m_stallTimer->stop();
    qint64 fileSize = QFileInfo(m_currentDownloadPath).size();
    qDebug() << "[ANTOR] Download completed:" << fileSize << "bytes";
    emit statusMessage("Download completed successfully");
    emit downloadProgress(fileSize, fileSize);
    if (m_currentCallback) {
      m_currentCallback(true, m_currentDownloadPath);
    }
  }
  reply->deleteLater();
  m_currentDownloadReply = nullptr;
}

void AntorUtils::handleDownloadError(QNetworkReply::NetworkError error) {
  QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
  QString errorMsg = reply ? reply->errorString() : "Unknown error";
  qDebug() << "[ANTOR] Download error:" << error << "-" << errorMsg;
  if (reply) {
    disconnect(reply, nullptr, this, nullptr);
  }
  m_currentState = DownloadState::Error;
  m_stallTimer->stop();
  QString userMsg = QString("Download failed: %1").arg(errorMsg);
  emit errorOccurred(userMsg);
  if (m_currentCallback) {
    m_currentCallback(false, userMsg);
  }
}

void AntorUtils::retryDownload(const QString &nid) {
  if (!nid.isEmpty()) {
    m_currentNid = nid;
  }
  qDebug() << "[ANTOR] retryDownload called, current state:" << static_cast<int>(m_currentState);
  if (m_currentNid.isEmpty() || m_currentDownloadPath.isEmpty()) {
    qDebug() << "[ANTOR] Cannot retry - no active m_currentNid" << m_currentNid;
    qDebug() << "[ANTOR] Cannot retry - no active m_currentDownloadPath" << m_currentDownloadPath;
    emit errorOccurred("No download to retry");
    return;
  }
  if (m_currentState != DownloadState::Error && m_currentState != DownloadState::Stalled && m_currentState != DownloadState::Downloading) {
    qDebug() << "[ANTOR] Cannot retry from state:" << static_cast<int>(m_currentState);
    return;
  }
  qDebug() << "[ANTOR] User requested retry";
  emit statusMessage("Retrying download...");
  m_stallTimer->stop();
  if (m_currentDownloadReply) {
    disconnect(m_currentDownloadReply, nullptr, this, nullptr);
    m_currentDownloadReply->abort();
    m_currentDownloadReply->deleteLater();
    m_currentDownloadReply = nullptr;
  }
  if (m_currentFile) {
    m_currentFile->close();
    delete m_currentFile;
    m_currentFile = nullptr;
  }
  m_currentState = DownloadState::Requesting;
  m_isStalled = false;
  m_retryCount++;

  QTimer::singleShot(100, this, [this]() {
    bool canResume = false;

    QFileInfo fi(m_currentDownloadPath);
    if (fi.exists() && fi.size() > 0 && m_currentDownloadType == DownloadType::Mirror) {
      m_resumeOffset = fi.size();
      qDebug() << "[ANTOR] Can resume from offset:" << m_resumeOffset;
      canResume = true;
    } else {
      m_resumeOffset = 0;
      qDebug() << "[ANTOR] Cannot resume, starting fresh";
      canResume = false;
    }

    if (canResume) {
      qDebug() << "[ANTOR] Manual retry initiated successfully (resume)";
      m_currentState = DownloadState::Downloading;
      m_lastProgressTime = QDateTime::currentDateTime();
      m_stallTimer->start();
      emit statusMessage("Download resumed...");
      startDownloadOrResume(m_currentNid, m_currentDownloadPath, m_currentCallback);
    } else {
      qDebug() << "[ANTOR] Manual retry failed or cannot resume";
      if (fi.exists()) {
        if (fi.size() > 0) {
          qDebug() << "[ANTOR] Keeping partial file of size:" << fi.size();
        } else {
          QFile::remove(m_currentDownloadPath);
        }
      }
      m_resumeOffset = 0;
      QTimer::singleShot(1000, this, [this]() { downloadFileByNid(m_currentNid, m_currentDownloadPath, m_currentCallback); });
    }
  });
}

void AntorUtils::cleanup() {
  qDebug() << "[ANTOR] Cleanup started, current state:" << static_cast<int>(m_currentState);
  m_stallTimer->stop();
  if (m_currentDownloadReply) {
    disconnect(m_currentDownloadReply, nullptr, this, nullptr);
    if (m_currentDownloadReply->isRunning()) {
      m_currentDownloadReply->abort();
    }
    m_currentDownloadReply->deleteLater();
    m_currentDownloadReply = nullptr;
  }
  if (m_currentFile) {
    m_currentFile->close();
    delete m_currentFile;
    m_currentFile = nullptr;
  }
  m_retryCount = 0;
  m_isStalled = false;
  qDebug() << "[ANTOR] Cleanup completed";
}

void AntorUtils::forceRestartDownload() {
  qDebug() << "[ANTOR] FORCE RESTARTING download (last resort)";
  QString savedPath = m_currentDownloadPath;
  QString savedNid = m_currentNid;
  auto savedCallback = m_currentCallback;
  cleanup();
  if (QFile::exists(savedPath)) {
    QFileInfo info(savedPath);
    if (info.size() > 0) {
      qDebug() << "[ANTOR] Keeping partial file for potential resume:" << info.size() << "bytes";
    } else {
      QFile::remove(savedPath);
      qDebug() << "[ANTOR] Removed empty file";
    }
  }
  m_retryCount = 0;
  m_resumeOffset = 0;
  QTimer::singleShot(100, this, [this, savedNid, savedPath, savedCallback]() { downloadFileByNid(savedNid, savedPath, savedCallback); });
}

void AntorUtils::checkForStall() {
  if (m_currentState != DownloadState::Downloading) {
    return;
  }
  qint64 secondsSinceProgress = m_lastProgressTime.secsTo(QDateTime::currentDateTime());
  if (secondsSinceProgress > 120 && m_lastBytesReceived == m_currentFile->size()) {
    qDebug() << "[ANTOR] Download appears stalled for" << secondsSinceProgress << "seconds";
    emit downloadStalled();
  }
}

AntorUtils::Platform AntorUtils::detectPlatform() {
#ifdef Q_OS_WIN
  return Platform::Windows;
#elif defined(Q_OS_LINUX) || defined(Q_OS_MAC)
  return Platform::LinuxMac;
#else
  return Platform::Unknown;
#endif
}

void AntorUtils::getPlatformNid(std::function<void(bool, const QString &)> callback, bool useMariah) {
  qDebug() << "[ANTOR] getPlatformNid - fetching latest releases" << (useMariah ? "(using Mariah NID)" : "");
  if (m_cacheValid && m_cacheTimestamp.isValid() && m_cacheTimestamp.secsTo(QDateTime::currentDateTime()) < 3600) {
    qDebug() << "[ANTOR] Using cached NID:" << m_cachedNid;
    callback(true, m_cachedNid);
    return;
  }
  QUrl url(QString("%1/anne?requestType=latestReleases").arg(m_baseUrl));

  QNetworkRequest request = NetUtils::createSslConfiguredRequest(url, m_nam, 30000);
  QNetworkReply *reply = m_nam->get(request);
  QTimer *timeoutTimer = new QTimer();
  timeoutTimer->setSingleShot(true);
  timeoutTimer->start(60000);
  connect(timeoutTimer, &QTimer::timeout, [this, reply, callback, timeoutTimer]() {
    qDebug() << "[ANTOR] Timeout fetching latest releases after 15 seconds";
    if (reply && reply->isRunning()) {
      reply->abort();
    }
    if (m_cacheValid) {
      qDebug() << "[ANTOR] Using cached NID due to timeout";
      callback(true, m_cachedNid);
    } else {
      qDebug() << "[ANTOR] No cached NID available after timeout";
      emit errorOccurred("Network timeout fetching release information");
      callback(false, "");
    }
    timeoutTimer->deleteLater();
  });
  connect(reply, &QNetworkReply::finished, [this, reply, callback, useMariah, timeoutTimer]() {
    timeoutTimer->stop();
    timeoutTimer->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      qDebug() << "[ANTOR] Network error getting releases:" << reply->errorString();
      if (m_cacheValid) {
        qDebug() << "[ANTOR] Using cached NID due to network error:" << m_cachedNid;
        callback(true, m_cachedNid);
      } else {
        emit errorOccurred(
            QString("Network error: Check your internet connection"));
        callback(false, "");
      }
      reply->deleteLater();
      return;
    }
    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (doc.isNull() || !doc.isObject()) {
      qDebug() << "[ANTOR] Invalid JSON response for releases";
      if (m_cacheValid) {
        qDebug() << "[ANTOR] Using cached NID due to invalid JSON";
        callback(true, m_cachedNid);
      } else {
        emit errorOccurred("Invalid JSON response from server");
        callback(false, "");
      }
      reply->deleteLater();
      return;
    }
    QJsonObject releases = doc.object();
    qDebug() << "[ANTOR] Latest releases response:" << releases;
    QString nid = getPlatformNidFromReleases(releases, useMariah);
    if (nid.isEmpty()) {
      qDebug() << "[ANTOR] Could not determine platform NID from releases";
      if (m_cacheValid) {
        qDebug() << "[ANTOR] Using cached NID due to empty response";
        callback(true, m_cachedNid);
      } else {
        emit errorOccurred("Could not determine platform NID");
        callback(false, "");
      }
    } else {
      m_cachedNid = nid;
      m_cacheTimestamp = QDateTime::currentDateTime();
      m_cacheValid = true;
      qDebug() << "[ANTOR] Platform NID determined and cached:" << nid;
      callback(true, nid);
    }
    reply->deleteLater();
  });
}

QString AntorUtils::getPlatformNidFromReleases(const QJsonObject &releases, bool useMariah) {
  if (useMariah) {
    QString nid = releases.value("mariah_snap_nid").toString();
    qDebug() << "[ANTOR] Using Mariah NID:" << nid;
    return nid;
  }
  Platform platform = detectPlatform();
  QString nid;
  switch (platform) {
  case Platform::Windows:
    nid = releases.value("annode_win_x64_zip_nid").toString();
    if (nid.isEmpty()) {
      nid = releases.value("annode_zip_nid").toString();
    }
    qDebug() << "[ANTOR] Windows platform, using win_x64 NID (fallback to standard):" << nid;
    break;
  case Platform::LinuxMac:
    nid = releases.value("annode_zip_nid").toString();
    qDebug() << "[ANTOR] Linux/Mac platform, using standard NID:" << nid;
    break;
  default:
    nid = releases.value("annode_zip_nid").toString();
    qDebug() << "[ANTOR] Unknown platform, defaulting to standard NID:" << nid;
    break;
  }
  return nid;
}

bool AntorUtils::openDownloadFile(QIODevice::OpenMode mode) {
  QFileInfo fileInfo(m_currentDownloadPath);
  QDir dir;
  QString absolutePath = fileInfo.absolutePath();
  if (!dir.exists(absolutePath)) {
    if (!dir.mkpath(absolutePath)) {
      qDebug() << "[ANTOR] Failed to create directory path:" << absolutePath;
      return false;
    }
  }
  m_currentFile = new QFile(m_currentDownloadPath);
  return m_currentFile->open(mode);
}

void AntorUtils::startStallDetection() { m_stallTimer->start(); }

void AntorUtils::stopStallDetection() { m_stallTimer->stop(); }

void AntorUtils::resetStallDetection() {
  m_lastProgressTime = QDateTime::currentDateTime();
  m_isStalled = false;
}

void AntorUtils::handleUnexpectedStatus(int statusCode) {
  qDebug() << "[ANTOR] Unexpected HTTP status:" << statusCode;
  QString error = QString("Unexpected HTTP status: %1").arg(statusCode);
  emit errorOccurred(error);
  if (m_currentCallback) {
    m_currentCallback(false, error);
  }
  cleanup();
}

void AntorUtils::finishWithError(const QString &error) {
  m_currentState = DownloadState::Error;
  emit errorOccurred(error);
  if (m_currentCallback) {
    m_currentCallback(false, error);
  }
  cleanup();
}

bool AntorUtils::isTransientError(QNetworkReply::NetworkError error) {
  return (error == QNetworkReply::RemoteHostClosedError || error == QNetworkReply::TimeoutError || error == QNetworkReply::ConnectionRefusedError ||
          error == QNetworkReply::TemporaryNetworkFailureError || error == QNetworkReply::HostNotFoundError);
}

void AntorUtils::configureForTorEnvironment() {
  qDebug() << "[ANTOR] Configuring for Tor environment";

  QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
  sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);

  sslConfig.setSslOption(QSsl::SslOptionDisableSessionTickets, true);
  sslConfig.setSslOption(QSsl::SslOptionDisableSessionPersistence, true);
  sslConfig.setSslOption(QSsl::SslOptionDisableLegacyRenegotiation, true);

  sslConfig.setProtocol(QSsl::TlsV1_2);
  QSslConfiguration::setDefaultConfiguration(sslConfig);
  qDebug() << "[ANTOR] SSL verification disabled completely";
}

void AntorUtils::cancelCurrentDownload() {
  qDebug() << "[ANTOR] Cancelling current download";

  if (m_currentDownloadReply && m_currentDownloadReply->isRunning()) {
    m_currentDownloadReply->abort();
  }

  if (m_currentFile) {
    m_currentFile->close();
    delete m_currentFile;
    m_currentFile = nullptr;
  }

  m_stallTimer->stop();

  m_currentState = DownloadState::Idle;

  qDebug() << "[ANTOR] Download cancelled";
}