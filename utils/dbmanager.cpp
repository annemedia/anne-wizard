#include "dbmanager.h"
#include "utils.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

DBManager::DBManager(QObject *parent) : QObject(parent) {}

QString DBManager::secureMariaDB(const QString &rootPass, bool useNativePass) {
  QString sql;
  qDebug() << "DBManager::secureMariaDB() rootPass" << rootPass;
  qDebug() << "DBManager::secureMariaDB() useNativePass" << useNativePass;
  
  if (useNativePass) {
    sql = QString("USE mysql; "
                  "DROP DATABASE IF EXISTS test; "
                  "DELETE FROM mysql.db WHERE Db='test' OR Db='test\\\\_%'; "
                  "DROP USER IF EXISTS ''@'localhost'; "
                  "DROP USER IF EXISTS ''@'%'; "
                  "DROP USER IF EXISTS 'root'@'%'; "
                  "FLUSH PRIVILEGES; "
                  "ALTER USER 'root'@'localhost' IDENTIFIED VIA "
                  "mysql_native_password "
                  "USING PASSWORD('%1'); "
                  "CREATE USER IF NOT EXISTS 'root'@'127.0.0.1' IDENTIFIED VIA "
                  "mysql_native_password USING PASSWORD('%1'); "
                  "CREATE USER IF NOT EXISTS 'root'@'::1' IDENTIFIED VIA "
                  "mysql_native_password USING PASSWORD('%1'); "
                  "FLUSH PRIVILEGES;")
              .arg(rootPass);
  } else {
    sql = QString("USE mysql; "
                  "DROP DATABASE IF EXISTS test; "
                  "DELETE FROM mysql.db WHERE Db='test' OR Db='test\\\\_%'; "
                  "DROP USER IF EXISTS ''@'localhost'; "
                  "DROP USER IF EXISTS ''@'%'; "
                  "DROP USER IF EXISTS 'root'@'%'; "
                  "FLUSH PRIVILEGES; "
                  "ALTER USER 'root'@'localhost' IDENTIFIED BY '%1'; "
                  "CREATE USER IF NOT EXISTS 'root'@'127.0.0.1' IDENTIFIED BY '%1'; "
                  "CREATE USER IF NOT EXISTS 'root'@'::1' IDENTIFIED BY '%1'; "
                  "FLUSH PRIVILEGES;")
              .arg(rootPass);
  }
  qDebug() << "DBManager::secureMariaDB() secure sql" << sql;
  return sql;
}

bool DBManager::validateRootPassword(const QString &rootPass) {
  QProcessResult result = Utils::executeProcess("mariadb", {"-u", "root", "-p" + rootPass, "-e", "SELECT 1;"}, 5000);
  return result.QPSuccess && result.exitCode == 0;
}

bool DBManager::databaseExists(const QString &rootPass, const QString &dbName) {
  QString sql = QString("SELECT SCHEMA_NAME FROM INFORMATION_SCHEMA.SCHEMATA WHERE SCHEMA_NAME = '%1';").arg(dbName);
  QProcessResult result = Utils::executeProcess("mariadb", {"-u", "root", "-p" + rootPass, "-e", sql}, 5000);

  return result.QPSuccess && result.exitCode == 0 && result.stdOut.contains(dbName);
}

QString DBManager::getBackupPath() {
  QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (appData.isEmpty()) {
    appData = QDir::homePath() + QDir::separator() + ".relons";
  }
  QDir dir(appData);
  if (!dir.exists()) {
    dir.mkpath(".");
  }
  return appData + QDir::separator() + "localrelons.sql";
}

bool DBManager::createDatabase(const QString &rootPass, const QString &dbName) {
  QString createSql = QString("CREATE DATABASE IF NOT EXISTS `%1` DEFAULT CHARACTER SET "
                              "utf8mb4 COLLATE utf8mb4_unicode_520_ci;")
                          .arg(dbName);

  QProcessResult result = Utils::executeProcess("mariadb", {"-u", "root", "-p" + rootPass, "-e", createSql}, -1);

  if (!result.QPSuccess || result.exitCode != 0) {
    qWarning() << "Failed to create database:" << result.stdErr;
    return false;
  }
  return true;
}

bool DBManager::dropAllTablesAndViews(const QString &rootPass, const QString &dbName) {

  QString backupFile = getBackupPath();

  QProcess dumpProc;
  dumpProc.setStandardOutputFile(backupFile, QIODevice::Truncate);
  QStringList dumpArgs = {"-u", "root", "-p" + rootPass, dbName, "relons_this_annode_only"};
  dumpProc.start("mysqldump", dumpArgs);

  if (!dumpProc.waitForFinished(-1) || dumpProc.exitCode() != 0) {
    qWarning() << "Failed to backup relons table:" << dumpProc.readAllStandardError();
  }

  QString viewSql = QString("SELECT CONCAT('SET FOREIGN_KEY_CHECKS=0; DROP VIEW IF EXISTS "
                            "`', table_name, '`;') FROM information_schema.views WHERE "
                            "table_schema = '%1';")
                        .arg(dbName);

  QProcessResult viewResult = Utils::executeProcess("mariadb", {"-u", "root", "-p" + rootPass, dbName, "-N", "-e", viewSql});

  if (!viewResult.QPSuccess || viewResult.exitCode != 0) {
    qWarning() << "Failed to generate view drops:" << viewResult.stdErr;
    return false;
  }

  QString viewOutput = viewResult.stdOut;
  QStringList viewDrops = viewOutput.split('\n', Qt::SkipEmptyParts);

  for (const QString &drop : viewDrops) {
    QString trimmed = drop.trimmed();
    if (trimmed.isEmpty())
      continue;

    QProcessResult dropResult = Utils::executeProcess("mariadb", {"-u", "root", "-p" + rootPass, dbName, "-e", trimmed});

    if (!dropResult.QPSuccess || dropResult.exitCode != 0) {
      qWarning() << "View drop failed:" << trimmed << dropResult.stdErr;
    }
  }

  QString tableSql = QString("SELECT CONCAT('SET FOREIGN_KEY_CHECKS=0; DROP TABLE IF EXISTS "
                             "`', table_name, '`;') FROM information_schema.tables WHERE "
                             "table_schema = '%1';")
                         .arg(dbName);

  QProcessResult tableResult = Utils::executeProcess("mariadb", {"-u", "root", "-p" + rootPass, dbName, "-N", "-e", tableSql});

  if (!tableResult.QPSuccess || tableResult.exitCode != 0) {
    qWarning() << "Failed to generate table drops:" << tableResult.stdErr;
    return false;
  }

  QString tableOutput = tableResult.stdOut;
  QStringList tableDrops = tableOutput.split('\n', Qt::SkipEmptyParts);

  for (const QString &drop : tableDrops) {
    QString trimmed = drop.trimmed();
    if (trimmed.isEmpty())
      continue;

    QProcessResult dropResult = Utils::executeProcess("mariadb", {"-u", "root", "-p" + rootPass, dbName, "-e", trimmed});

    if (!dropResult.QPSuccess || dropResult.exitCode != 0) {
      qWarning() << "Table drop failed:" << trimmed << dropResult.stdErr;
    }
  }

  return true;
}

QPair<bool, QString> DBManager::importSql(const QString &rootPass, const QString &dbName, const QString &sqlPath, std::atomic<bool> *cancelled) {
  return doImport(rootPass, dbName, sqlPath, cancelled);
}

QPair<bool, QString> DBManager::doImport(const QString &rootPass, const QString &dbName, const QString &sqlPath, std::atomic<bool> *cancelled) {
  try {
    bool dropSuccess = dropAllTablesAndViews(rootPass, dbName);

    if (!dropSuccess) {
      emit importStatus("Failed to drop existing tables and views.");
      return qMakePair(false, QString("Failed to drop existing tables and views."));
    }

    QFile sqlFile(sqlPath);
    if (!sqlFile.open(QIODevice::ReadOnly)) {
      emit importStatus("Failed to open SQL file.");
      return qMakePair(false, QString("Failed to open SQL file: %1").arg(sqlPath));
    }

    qint64 fileSize = sqlFile.size();
    qint64 totalRead = 0;
    const qint64 CHUNK_SIZE = 1024 * 1024;
    const qint64 PROGRESS_INTERVAL = 10 * 1024 * 1024;

    emit importStatus("Starting database import...");
    emit importProgress(0, fileSize);

    QProcess importProc;
    importProc.setProcessChannelMode(QProcess::MergedChannels);

    if (cancelled && cancelled->load()) {
      emit importStatus("Import cancelled before starting.");
      return qMakePair(false, QString("Import cancelled"));
    }

    QStringList args;
    args << "-u" << "root"
         << "-p" + rootPass << "--show-warnings"
         << "--force" << dbName;

    importProc.start("mysql", args);

    if (!importProc.waitForStarted(-1)) {
      emit importStatus("Failed to start MySQL process.");
      return qMakePair(false, QString("Failed to start MySQL import process"));
    }

    if (cancelled && cancelled->load()) {
      emit importStatus("Import cancelled after process started.");
      importProc.terminate();
      importProc.waitForFinished(1000);
      return qMakePair(false, QString("Import cancelled"));
    }

    emit importStatus("Importing SQL file...");

    QByteArray buffer;
    buffer.reserve(CHUNK_SIZE);

    while (!sqlFile.atEnd()) {
      if (cancelled && cancelled->load()) {
        emit importStatus("Import cancelled during file read.");
        sqlFile.close();
        importProc.terminate();
        importProc.waitForFinished(1000);
        return qMakePair(false, QString("Import cancelled"));
      }

      QByteArray chunk = sqlFile.read(CHUNK_SIZE);
      totalRead += chunk.size();

      qint64 bytesWritten = 0;
      while (bytesWritten < chunk.size()) {
        qint64 written = importProc.write(chunk.constData() + bytesWritten, chunk.size() - bytesWritten);
        if (written == -1) {
          emit importStatus("Write error to MySQL process.");
          sqlFile.close();
          importProc.terminate();
          return qMakePair(false, QString("Failed to write to MySQL process"));
        }
        bytesWritten += written;
      }

#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS) || defined(Q_OS_MAC)

      qint64 chunkStartBuffer = importProc.bytesToWrite();

#ifdef Q_OS_LINUX
      double consumptionThreshold = 0.98;
      const int maxWaitLoops = 600;
      const char *platformName = "Linux";
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
      double consumptionThreshold = 0.95;
      const int maxWaitLoops = 400;
      const char *platformName = "macOS";
#endif

      qint64 targetBuffer = chunkStartBuffer - (chunk.size() * consumptionThreshold);

      qDebug() << "[DEBUG]" << platformName << "- Waiting for chunk to be processed";
      qDebug() << "[DEBUG] Initial buffer:" << chunkStartBuffer << "Target buffer:" << targetBuffer;

      int waitLoops = 0;

      while (waitLoops < maxWaitLoops) {

        importProc.waitForBytesWritten(500);

        qint64 currentBuffer = importProc.bytesToWrite();
        qDebug() << "[DEBUG]" << platformName << "- Wait loop" << waitLoops << "- Buffer:" << currentBuffer;

        if (currentBuffer <= targetBuffer) {
          qDebug() << "[DEBUG]" << platformName << "- Chunk mostly processed, breaking wait";
          break;
        }

        if (importProc.bytesAvailable() > 0) {
          QByteArray output = importProc.readAll();
          if (!output.trimmed().isEmpty()) {
            qDebug() << "[DEBUG]" << platformName << "- MySQL is working, got output";
          }
        }

        waitLoops++;

        if (cancelled && cancelled->load()) {
          emit importStatus("Import cancelled during write wait.");
          sqlFile.close();
          importProc.terminate();
          importProc.waitForFinished(1000);
          return qMakePair(false, QString("Import cancelled"));
        }

        QCoreApplication::processEvents();
      }

      qDebug() << "[DEBUG]" << platformName << "- Finished waiting after" << waitLoops << "loops";

      if (importProc.bytesAvailable() > 0) {
        QByteArray output = importProc.readAll();
      }

#else

      importProc.waitForBytesWritten(-1);
#endif

      if (totalRead / PROGRESS_INTERVAL != (totalRead - chunk.size()) / PROGRESS_INTERVAL || totalRead == fileSize) {
        emit importProgress(totalRead, fileSize);

        qint64 mbRead = totalRead / (1024 * 1024);
        qint64 mbTotal = fileSize / (1024 * 1024);
        emit importStatus(QString("Processed %1 MB of %2 MB").arg(mbRead).arg(mbTotal));
      }

      QCoreApplication::processEvents();
    }

    sqlFile.close();
    importProc.closeWriteChannel();

    emit importStatus("Finalizing import...");

    QElapsedTimer timer;
    timer.start();

    bool wasCancelled = false;
    QByteArray allOutput;

    while (!importProc.waitForFinished(500)) {
      if (cancelled && cancelled->load()) {
        emit importStatus("Import cancelled during database processing.");
        wasCancelled = true;
        importProc.terminate();
        importProc.waitForFinished(1000);
        break;
      }

      if (importProc.bytesAvailable() > 0) {
        allOutput.append(importProc.readAll());
      }

      if (timer.elapsed() > 5000) {
        emit importStatus(QString("Finalizing... (%1 seconds elapsed)").arg(timer.elapsed() / 1000));
        timer.restart();
      }
    }

    if (wasCancelled) {
      return qMakePair(false, QString("Import cancelled by user"));
    }

    if (importProc.bytesAvailable() > 0) {
      allOutput.append(importProc.readAll());
    }

    int exitCode = importProc.exitCode();
    QString output = QString::fromUtf8(allOutput);

    emit importProgress(fileSize, fileSize);

    if (exitCode != 0) {
      emit importStatus("Import failed.");
      QString error = output.left(500);
      if (error.isEmpty()) {
        error = "MySQL process exited with code " + QString::number(exitCode);
      }
      return qMakePair(false, QString("SQL import failed: %1").arg(error));
    }

    emit importStatus("Import completed successfully!");
    return qMakePair(true, QString("Import completed successfully"));

  } catch (const std::exception &e) {
    emit importStatus(QString("Exception during import: %1").arg(e.what()));
    return qMakePair(false, QString("Internal error during import: %1").arg(e.what()));
  } catch (...) {
    emit importStatus("Unknown exception during import.");
    return qMakePair(false, QString("Unknown error during import"));
  }
}
