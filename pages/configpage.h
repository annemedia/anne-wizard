#ifndef CONFIGPAGE_H
#define CONFIGPAGE_H
#include "../wizard.h"
#include <QWizardPage>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>

namespace NodePeers {
  inline constexpr const char *BootstrapPeers = "spock.anne.media:9115; q.anne.media:9115; picard.anne.media:9772";
  inline constexpr const char *RebroadcastTo = "spock.anne.media:9115; q.anne.media:9115; picard.anne.media:9772";
}

class ConfigPage : public QWizardPage {
  Q_OBJECT

  Q_PROPERTY(QString accountId READ accountId WRITE setAccountId)
  Q_PROPERTY(QString nodeSecret READ nodeSecret WRITE setNodeSecret)

public:
  explicit ConfigPage(QWidget *parent = nullptr);

  QString accountId() const;
  QString nodeSecret() const;
  bool isComplete() const override;

protected:
  void initializePage() override;
  bool validatePage() override;

private slots:
  void generateKeys();
  void copyPublicKey();
  void copyAccountId();
  void copySeed();
  void updateCopyButtonStatus();

private:
  Wizard *m_wiz;
  void setAccountId(const QString &id);
  void setNodeSecret(const QString &secret);
  QString processCustomSeed(const QString &userInput);
  QLabel *statusLabel = nullptr;

  QGroupBox *dbPassGroup;
  QLabel *dbPassLabel;
  QLineEdit *dbPassEdit;

  QGroupBox *seedGroup;
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

  bool copiedAccountId;
  bool copiedPublicKey;
  bool copiedSeed;

  QString m_accountId;
  QString m_nodeSecret;
  QString generatedSeed;
  QString dbPass;
  bool skipDbPassReplace;
};

#endif