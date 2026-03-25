#ifndef WIZARD_H
#define WIZARD_H
#include "utils/osinfo.h"
#include "utils/javautils.h"
#include <QWizard>
#include <QProgressDialog>
#include <functional>
#include <QMap>
#include <QString>
#include <QFutureWatcher> 
#include <QCoreApplication>      
#include <QProcessEnvironment>   
#include <QCloseEvent>
#ifdef Q_OS_UNIX
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

class Wizard : public QWizard
{
    Q_OBJECT

public:
    explicit Wizard(QWidget *parent = nullptr);
    ~Wizard() override = default;


    enum PageId {
        IntroPageId,
        WelcomePageId,
        JavaPageId,
        MariaDBPageId,
        DBSetupPageId,
        DownloadPageId,
        NetConfigPageId,
        ConfigPageId,
        MinerConfigPageId,
        FinalPageId
    };

    QString wanIp;
    QString p2pPort;
    QString apiPort;
    QString installDir;
    QString rootPass;
    QString dbName;
    QString dbUser;
    QString dbUserPass;
    QMap<QString, QString> configReplacements;
    bool isFreshInstall = false;
    
    bool isNodeConfigEncrypted() const;
    bool isRunningElevated() const;

    int nextId() const override;
    void resetSkippedPagesIfNeeded();
    void showProgress(const QString &title,
                      std::function<void(QProgressDialog *)> task);
    void debugWindowPosition();
    const OSInfo& osInfo() const { return m_osInfo; }
    JavaCheckResult javaCheckResult() const { return m_javaCheckResult; }
    bool javaDetected() const { return m_javaDetected; }
    QString javaVersion() const { return m_javaVersion; }
    QList<JavaPackageInfo> availableJavaPackages() const { return m_availablePackages; }
    bool isJavaDetectionComplete() const { return m_javaDetectionComplete; }
    bool confirmExit();
    void startJavaDetectionAsync();
public slots:
    void onCurrentIdChanged(int id);
    
signals:
    void javaDetectionComplete();
protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
private slots:
    void handleCustomButtonClicked(int id);
    void onJavaDetectionFinished();  
private:
    QProgressDialog *progressDialog = nullptr;
    OSInfo m_osInfo;   
    bool areExtrasInstalled() const;
    
    JavaCheckResult m_javaCheckResult; 
    bool m_javaDetected = false;
    QString m_javaVersion;
    QList<JavaPackageInfo> m_availablePackages;
    bool m_javaDetectionComplete = false;
    QFutureWatcher<void> m_javaDetectionWatcher; 
    mutable bool nodeConfigIsEncrypted = false;
};

#endif
