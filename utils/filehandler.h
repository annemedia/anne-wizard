#ifndef FILEHANDLER_H
#define FILEHANDLER_H

#include <QByteArray>
#include <QDateTime>
#include <QString>

class FileHandler {
public:
  FileHandler() = default;

  bool downloadZip(const QString &url, QByteArray &zipData);
  bool extractZip(const QByteArray &zipData, const QString &targetDir,
                  bool overwrite = true);
  bool extractZipFromFile(const QString &zipPath, const QString &targetDir,
                          bool overwrite = true);
  static bool isZipFileValid(const QString &filePath);
  static QDateTime getRemoteLastModified(const QString &urlStr);
  QString lastError() const { return m_lastError; }

private:
  QString m_lastError;
  
};

#endif

