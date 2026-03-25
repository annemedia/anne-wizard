#ifndef DOWNLOADPAGE_H
#define DOWNLOADPAGE_H

#include "../wizard.h"
#include "../utils/antorutils.h"
#include <QWizardPage>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QFile>
#include <QCheckBox>
#include <QGroupBox>
#include <QDateTime>

class DownloadPage : public QWizardPage {
    Q_OBJECT

public:
    explicit DownloadPage(QWidget *parent = nullptr);
    ~DownloadPage();

protected:
    void initializePage() override;
    bool validatePage() override;
    bool isComplete() const override;

private slots:
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onAntorProgress(const QString &nid, int completed, int total);
    void onDownloadStalled();
    void onDirChanged();
    void onExtrasSelectionChanged();
    void onDownloadInstallClicked();
    
    void retryAnnodeDownload();

private:
    Wizard *m_wiz;
    enum class Phase {
        NotStarted,
        DownloadingAnnode,
        AnnodeComplete,
        DownloadingExtras,
        ExtrasComplete,
        Extracting,
        Complete,
        Error
    };
    QNetworkAccessManager* m_nam;
    QString HASHER_LINUX_URL = "https://anne.media/mirror/anne-hasher/anne-hasher-linux.zip";
    QString HASHER_MAC_URL = "https://anne.media/mirror/anne-hasher/anne-hasher-macos.zip";
    QString HASHER_WINDOWS_URL = "https://anne.media/mirror/anne-hasher/anne-hasher-windows.zip";

    QString MINER_LINUX_URL = "https://anne.media/mirror/anne-miner/anne-miner-linux.zip";
    QString MINER_MAC_URL = "https://anne.media/mirror/anne-miner/anne-miner-macos.zip";
    QString MINER_WINDOWS_URL = "https://anne.media/mirror/anne-miner/anne-miner-windows.zip";
    
    Phase m_currentPhase = Phase::NotStarted;
    
    QLineEdit *installDirEdit;
    QLabel *statusLabel;
    QProgressBar *progressBar;
    QPushButton *downloadInstallBtn;
    QLabel *dirLabel;
    

    QGroupBox *appsGroupBox;
    QGroupBox *annodeGroup;
    QGroupBox *extrasGroup;
    QCheckBox *annodeCheckbox;
    QCheckBox *extrasCheckbox;
    
    AntorUtils *antorUtils;
    QString currentNid;
    QString userHome;
    QString installDirStr;
    

    QString m_annodeZipPath;
    QString m_hasherZipPath;
    QString m_minerZipPath;
    
    QString getAction() const;
    

    void startDownloadProcess();
    void performAnnodeDownload();
    void onAnnodeDownloadComplete();
    void performExtrasDownload();
    void onExtrasDownloadComplete();
    void performExtraction();
    void cleanupPage() override;
    QDateTime getRemoteLastModified(const QString &url);
    QString getHasherUrl() const;
    QString getMinerUrl() const;
    void downloadExtrasFile(const QString &url, const QString &type, 
                           const QString &cacheDir, std::function<void()> onComplete);
    void handleDownloadError(const QString &error, const QString &component);
    void updateStatusForPhase();
};

#endif