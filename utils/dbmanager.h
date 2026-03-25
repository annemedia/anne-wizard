#ifndef DBMANAGER_H
#define DBMANAGER_H

#include <QObject>
#include <QPair>
#include <QString>
#include <atomic>
#include <functional>

class DBManager : public QObject {
    Q_OBJECT
    
public:
    explicit DBManager(QObject *parent = nullptr);
    
    static QString secureMariaDB(const QString &rootPass, bool useNativePass = false);
    static bool createDatabase(const QString &rootPass, const QString &dbName);
    static bool validateRootPassword(const QString &rootPass);
    
    QPair<bool, QString> importSql(const QString &rootPass, const QString &dbName, const QString &sqlPath, std::atomic<bool> *cancelled = nullptr);

signals:
    void importProgress(qint64 bytesProcessed, qint64 totalBytes);
    void importStatus(const QString &message);

private:
    static QString getBackupPath();
    static bool databaseExists(const QString &rootPass, const QString &dbName);
    static bool dropAllTablesAndViews(const QString &rootPass, const QString &dbName);
    QPair<bool, QString> doImport(const QString &rootPass, const QString &dbName, const QString &sqlPath, std::atomic<bool> *cancelled);
};

#endif