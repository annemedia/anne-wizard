#ifndef ANTORUTILS_H
#define ANTORUTILS_H

#include <QFile>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

enum class DownloadState { Idle, Requesting, Downloading, WaitingForFileReady, Completed, Error, Stalled };

class AntorUtils : public QObject {
  Q_OBJECT

public:
  enum class Platform { LinuxMac, Windows, Unknown };

  explicit AntorUtils(QObject *parent = nullptr);
  ~AntorUtils();

  void downloadFileByNid(const QString &nid, const QString &downloadPath, std::function<void(bool, const QString &)> callback);

  void getPlatformNid(std::function<void(bool, const QString &)> callback, bool useMariah = false);

  static Platform detectPlatform();

  void forceRestartDownload();
  void cleanup();
  void cancelCurrentDownload();
  void retryDownload(const QString &nid);
  void setBaseUrl(const QString &url) { m_baseUrl = url; }
  QString baseUrl() const { return m_baseUrl; }

signals:
  void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
  void antorProgress(const QString &nid, int chunksCompleted, int chunksTotal);
  void antorFileReady(const QString &nid);
  void statusMessage(const QString &message);
  void errorOccurred(const QString &error);
  void downloadStalled();

private slots:

  void handleDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
  void handleDownloadError(QNetworkReply::NetworkError error);
  void handleDownloadCompletion();
  void checkForStall();
  void forceFileSync();

private:
  enum class DownloadType { Fileserve, Mirror };
  DownloadType m_currentDownloadType;
  void startDownloadOrResume(const QString &nid, const QString &downloadPath, std::function<void(bool, const QString &)> callback);
  DownloadState m_currentState;
  QString m_currentNid;
  QString m_currentDownloadPath;
  QString m_cachedNid;
  QDateTime m_cacheTimestamp;
  qint64 m_originalFileSize;
  bool m_cacheValid;
  std::function<void(bool, const QString &)> m_currentCallback;

  QNetworkAccessManager *m_nam;
  QNetworkReply *m_currentDownloadReply;

  QFile *m_currentFile;
  qint64 m_resumeOffset;

  QTimer *m_stallTimer;
  int m_retryCount;
  static const int MAX_RETRIES = 10;

  QString m_baseUrl = "https://spock.anne.media:9116";
  QString m_snapshotUrl = "https://anne.media/mirror/anne-node/annedb-latest.sql.zip";
  QString m_linuxUrl = "https://anne.media/mirror/anne-node/anne-node.zip";
  QString m_windowsUrl = "https://anne.media/mirror/anne-node/anne-node-win_x64.zip";

  int m_lastChunksCompleted;
  int m_lastChunksTotal;

  void startDirectDownload(QNetworkReply *reply);
  void startResumedDownload(QNetworkReply *reply);
  void handleUnexpectedStatus(int statusCode);
  void finishWithError(const QString &error);
  void fallbackToMirrorDownload();

  bool openDownloadFile(QIODevice::OpenMode mode);
  void startStallDetection();
  void stopStallDetection();
  void resetStallDetection();

  QString getPlatformNidFromReleases(const QJsonObject &releases, bool useMariah = false);

  void configureForTorEnvironment();
  bool m_isStalled;

  bool isTransientError(QNetworkReply::NetworkError error);

  qint64 m_lastBytesReceived;
  QDateTime m_lastProgressTime;
};

#endif
