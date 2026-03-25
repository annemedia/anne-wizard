#ifndef MINERCONFIGPAGE_H
#define MINERCONFIGPAGE_H
#include "../wizard.h"
#include <QWizardPage>
#include <QGroupBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QString>

class MinerConfigPage : public QWizardPage
{
    Q_OBJECT

public:
    MinerConfigPage(QWidget *parent = nullptr);
    void initializePage() override;
    bool validatePage() override;
    bool isComplete() const override;

private slots:
    void onNonceDirectorySelect();
    void generateMinerKeys();
    void copyAccountId();
    void copyPublicKey();
    void copySeed();
    void updateCopyButtonStatus();
    void onMiningTypeChanged(bool checked); 
private:
    Wizard *m_wiz;
    bool updateConfigYamlFile();
    bool updateNodeProperties();
    QString getFirstSeedFromProperties(bool forSoloMining);
    QString getSecretPhraseSuffix() const;
    QString getApiPort() const;
    

    QGroupBox *miningTypeGroup;
    QRadioButton *soloMiningRadio;
    QRadioButton *shareMiningRadio;
    QLabel *miningTypeInfo;
    

    QGroupBox *keyGroup;
    QLineEdit *seedEdit;
    QPushButton *generateKeysBtn;
    QLabel *accountIdLabel;
    QLineEdit *accountIdEdit;
    QPushButton *accountIdCopyBtn;
    QLabel *publicKeyLabel;
    QLineEdit *publicKeyEdit;
    QPushButton *publicKeyCopyBtn;
    QLabel *seedDisplayLabel;
    QLineEdit *seedDisplayEdit;
    QPushButton *seedCopyBtn;
    

    QGroupBox *nonceGroup;
    QListWidget *nonceDirList;
    QPushButton *addNonceDirBtn;
    QPushButton *removeNonceDirBtn;
    
    QString minerInstallDir;
    QString minerAccountId;
    QString minerPublicKey;
    QString minerSeed;
    

    bool accountIdCopied = false;
    bool publicKeyCopied = false;
    bool seedCopied = false;
};

#endif