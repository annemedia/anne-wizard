#include "configpage.h"
#include "../utils/jsutils.h"
#include "../utils/systemutils.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

ConfigPage::ConfigPage(QWidget *parent) : QWizardPage(parent) {
  qDebug() << "=== ConfigPage CONSTRUCTOR START ===";

  setTitle("Configuration - Account Setup");
  setSubTitle("Configure database and generate annode account keys.");

  dbPassGroup = new QGroupBox("Database Configuration");
  QVBoxLayout *dbLayout = new QVBoxLayout(dbPassGroup);

  dbPassLabel = new QLabel("Enter MariaDB root password:");
  dbLayout->addWidget(dbPassLabel);
  dbPassEdit = new QLineEdit();
  dbPassEdit->setEchoMode(QLineEdit::Password);

  dbPassEdit->setPlaceholderText("Enter MariaDB root password");
  dbPassEdit->hide();
  dbLayout->addWidget(dbPassEdit);
  dbPassGroup->hide();

  seedGroup = new QGroupBox("Annode Account Keys");
  QFormLayout *seedForm = new QFormLayout(seedGroup);

  seedEdit = new QLineEdit();
  seedEdit->setPlaceholderText(QString("Enter custom seed (min %1 chars) or leave empty for auto-generate").arg(JSUtils::MIN_CUSTOM_SEED_LENGTH));

  generateKeysBtn = new QPushButton("Generate annode keys");
  generateKeysBtn->setEnabled(true);

  accountIdLabel = new QLabel("Account ID:");
  accountIdEdit = new QLineEdit();
  accountIdEdit->setReadOnly(true);
  accountIdCopyBtn = new QPushButton("Copy");
  accountIdCopyBtn->setEnabled(false);

  QHBoxLayout *accountIdLayout = new QHBoxLayout;
  accountIdLayout->addWidget(accountIdEdit);
  accountIdLayout->addWidget(accountIdCopyBtn);

  seedForm->addRow("Seed (optional):", seedEdit);
  seedForm->addRow("", generateKeysBtn);
  seedForm->addRow(accountIdLabel, accountIdLayout);

  publicKeyLabel = new QLabel("Public Key:");
  publicKeyEdit = new QLineEdit();
  publicKeyEdit->setReadOnly(true);
  publicKeyCopyBtn = new QPushButton("Copy");
  publicKeyCopyBtn->setEnabled(false);

  QHBoxLayout *publicKeyLayout = new QHBoxLayout;
  publicKeyLayout->addWidget(publicKeyEdit);
  publicKeyLayout->addWidget(publicKeyCopyBtn);

  seedForm->addRow(publicKeyLabel, publicKeyLayout);

  seedDisplayLabel = new QLabel("Seed:");
  seedDisplayEdit = new QLineEdit();
  seedDisplayEdit->setReadOnly(true);
  seedCopyBtn = new QPushButton("Copy");
  seedCopyBtn->setEnabled(false);

  QHBoxLayout *seedDispLayout = new QHBoxLayout;
  seedDispLayout->addWidget(seedDisplayEdit);
  seedDispLayout->addWidget(seedCopyBtn);

  seedForm->addRow(seedDisplayLabel, seedDispLayout);

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->addWidget(dbPassGroup);
  mainLayout->addWidget(seedGroup);

  QLabel *onboardingInfo = new QLabel();
  onboardingInfo->setWordWrap(true);
  onboardingInfo->setText("<p>To use ANNE, every account must be onboarded by receipt of a "
                          "transaction.</p><p>You can get some annecoins at ADF Swaps or ask "
                          "someone you know to send you a few annecoins. Doing a swap (for any "
                          "amount) will automatically register your NID and public key, and "
                          "deposit coins into your account.<p><a "
                          "href=\"https://www.anne.network:9116/aon.html?page=swaps\">https://"
                          "www.anne.network:9116/aon.html?page=swaps</a></p><p>If you downloaded "
                          "ANNE Miner and intend to mine, don't forget to also onboard the miner's "
                          "account you will configure on the next page.</p>");
  onboardingInfo->setTextFormat(Qt::RichText);
  onboardingInfo->setTextInteractionFlags(Qt::TextBrowserInteraction);
  onboardingInfo->setStyleSheet("QLabel { padding: 5px; margin-top:10px; background-color: #171717; "
                                "border-radius: 3px; }");
  mainLayout->addWidget(onboardingInfo);
  mainLayout->addStretch();

  connect(onboardingInfo, &QLabel::linkActivated, this, [this](const QString &link) {
#ifdef Q_OS_UNIX
    uid_t realUid = SystemUtils::getRealUserUid();
    uid_t effectiveUid = geteuid();
    if (realUid != 0 && realUid != effectiveUid) {
      struct passwd *pw = getpwuid(realUid);
      if (pw && pw->pw_name) {
        QStringList args;
        args << "-u" << QString(pw->pw_name) << "xdg-open" << link;

        QProcess::startDetached("sudo", args);
        return;
      }
    }
#endif

    QDesktopServices::openUrl(QUrl(link));
  });

  connect(generateKeysBtn, &QPushButton::clicked, this, &ConfigPage::generateKeys);

  connect(publicKeyCopyBtn, &QPushButton::clicked, this, &ConfigPage::copyPublicKey);
  connect(accountIdCopyBtn, &QPushButton::clicked, this, &ConfigPage::copyAccountId);
  connect(seedCopyBtn, &QPushButton::clicked, this, &ConfigPage::copySeed);

  connect(accountIdCopyBtn, &QPushButton::clicked, this, &ConfigPage::updateCopyButtonStatus);
  connect(publicKeyCopyBtn, &QPushButton::clicked, this, &ConfigPage::updateCopyButtonStatus);
  connect(seedCopyBtn, &QPushButton::clicked, this, &ConfigPage::updateCopyButtonStatus);

  copiedAccountId = false;
  copiedPublicKey = false;
  copiedSeed = false;

  qDebug() << "=== ConfigPage CONSTRUCTOR END ===";
}

void ConfigPage::initializePage() {
  qDebug() << "=== ConfigPage INITIALIZE PAGE CALLED (on enter) ===";

  registerField("accountId", this, "accountId");
  registerField("nodeSecret", this, "nodeSecret");
  registerField("dbPassword", this, "dbPassword");

  m_wiz = qobject_cast<Wizard *>(wizard());
  bool fromWizard = false;

  if (m_wiz && !m_wiz->dbUserPass.isEmpty()) {
    dbPass = m_wiz->dbUserPass;
    fromWizard = true;
    qDebug() << "DB Password retrieved from Wizard::dbUserPass (length:" << dbPass.length() << ")";
  }

  if (fromWizard) {
    dbPassGroup->hide();
    setField("dbPassword", dbPass);
  } else {
    dbPassGroup->show();
    dbPassEdit->show();
    dbPassEdit->setFocus();
    dbPassLabel->setText("Enter MariaDB root password:");
    qDebug() << "DB Password not available from prior steps or file - manual "
                "entry required";
  }

  copiedAccountId = false;
  copiedPublicKey = false;
  copiedSeed = false;

  emit completeChanged();
}

QString ConfigPage::accountId() const { return m_accountId; }

QString ConfigPage::nodeSecret() const { return m_nodeSecret; }

void ConfigPage::setAccountId(const QString &id) {
  if (m_accountId != id) {
    m_accountId = id;
    emit completeChanged();
  }
}

void ConfigPage::setNodeSecret(const QString &secret) {
  if (m_nodeSecret != secret) {

    JSUtils::secureClear(m_nodeSecret);

    m_nodeSecret = secret;

    emit completeChanged();
  }
}

bool ConfigPage::isComplete() const {

  if (m_accountId.isEmpty() || m_nodeSecret.isEmpty()) {
    return false;
  }

  if (!copiedAccountId || !copiedPublicKey || !copiedSeed) {
    return false;
  }

  return true;
}

void ConfigPage::updateCopyButtonStatus() { emit completeChanged(); }

void ConfigPage::generateKeys() {
  QString seedInput = seedEdit->text().trimmed();
  bool isCustom = !seedInput.isEmpty();
qDebug() << "=== UI DEBUG ===";
    qDebug() << "UI seedInput length:" << seedInput.length();
    qDebug() << "UI seedInput first 30:" << seedInput.left(30);
  if (isCustom && seedInput.length() < JSUtils::MIN_CUSTOM_SEED_LENGTH) {
    QMessageBox::warning(this, "Invalid Seed",
                         QString("Custom seed must be at least %1 characters (%2 bits).").arg(JSUtils::MIN_CUSTOM_SEED_LENGTH).arg(JSUtils::MIN_CUSTOM_SEED_LENGTH * 4));
    return;
  }

  QVariantMap resultMap = JSUtils::generateANNEKeys(seedInput);
qDebug() << "Result map keys:" << resultMap.keys();
    qDebug() << "Result 'seed' length:" << resultMap["seed"].toString().length();
    qDebug() << "Result 'seed' first 30:" << resultMap["seed"].toString().left(30);
    qDebug() << "=== END UI DEBUG ===";
  if (resultMap.contains("error")) {
    QString errorMsg = resultMap["error"].toString();

    for (auto &key : resultMap.keys()) {
      if (resultMap[key].canConvert<QString>()) {
        QString str = resultMap[key].toString();
        JSUtils::secureClear(str);
      }
    }
    resultMap.clear();

    QMessageBox::warning(this, "Key Generation Error", errorMsg);
    JSUtils::secureClear(errorMsg);
    return;
  }

  QString accountId = resultMap["nid"].toString();
  QString nodeSecretStr = resultMap["seed"].toString();
  QString publicKey = resultMap["publicKey"].toString();

  for (auto &key : resultMap.keys()) {
    if (resultMap[key].canConvert<QString>()) {
      QString str = resultMap[key].toString();
      JSUtils::secureClear(str);
    }
  }
  resultMap.clear();

  if (accountId.isEmpty() || nodeSecretStr.isEmpty() || publicKey.isEmpty()) {
    QMessageBox::warning(this, "Incomplete Keys", "Error: Incomplete keys generated.");
    JSUtils::secureClear(accountId);
    JSUtils::secureClear(nodeSecretStr);
    JSUtils::secureClear(publicKey);
    return;
  }

  accountIdEdit->setText(accountId);
  publicKeyEdit->setText(publicKey);
  seedDisplayEdit->setText(nodeSecretStr);

  accountIdCopyBtn->setEnabled(true);
  publicKeyCopyBtn->setEnabled(true);
  seedCopyBtn->setEnabled(true);

  copiedAccountId = false;
  copiedPublicKey = false;
  copiedSeed = false;

  setAccountId(accountId);
  setNodeSecret(nodeSecretStr);

  setField("accountId", m_accountId);
  setField("nodeSecret", m_nodeSecret);

  emit completeChanged();
}

void ConfigPage::copyPublicKey() {
  QClipboard *clipboard = QApplication::clipboard();
  clipboard->setText(publicKeyEdit->text());

  copiedPublicKey = true;
  publicKeyCopyBtn->setText("✓ Copied");
  publicKeyCopyBtn->setEnabled(false);

  QTimer::singleShot(5000, this, [this]() {
    publicKeyCopyBtn->setText("Copy");
    publicKeyCopyBtn->setEnabled(true);
  });
}

void ConfigPage::copyAccountId() {
  QClipboard *clipboard = QApplication::clipboard();
  clipboard->setText(accountIdEdit->text());

  copiedAccountId = true;
  accountIdCopyBtn->setText("✓ Copied");
  accountIdCopyBtn->setEnabled(false);

  QTimer::singleShot(5000, this, [this]() {
    accountIdCopyBtn->setText("Copy");
    accountIdCopyBtn->setEnabled(true);
  });
}

void ConfigPage::copySeed() {
  QClipboard *clipboard = QApplication::clipboard();
  clipboard->setText(seedDisplayEdit->text());

  copiedSeed = true;
  seedCopyBtn->setText("✓ Copied");
  seedCopyBtn->setEnabled(false);

  QTimer::singleShot(5000, this, [this]() {
    seedCopyBtn->setText("Copy");
    seedCopyBtn->setEnabled(true);
  });
}

bool ConfigPage::validatePage() {
  qDebug() << "=== ConfigPage validatePage CALLED (on Next) ===";

  if (dbPassGroup->isVisible()) {
    dbPass = dbPassEdit->text();
    if (dbPass.isEmpty()) {
      QMessageBox::warning(this, "Missing Data", "Database password is required.");
      dbPassEdit->setFocus();
      return false;
    }
    m_wiz->dbUserPass = dbPass;

    setField("dbPassword", dbPass);
  }

  if (accountId().isEmpty() || nodeSecret().isEmpty()) {
    QMessageBox::warning(this, "Missing Data", "Please generate keys first.");
    generateKeysBtn->setFocus();
    return false;
  }

  QString wanIp = m_wiz->wanIp;
  QString p2pPortStr = m_wiz->p2pPort.isEmpty() ? "9115" : m_wiz->p2pPort;
  QString apiPortStr = m_wiz->apiPort.isEmpty() ? "9116" : m_wiz->apiPort;
  QString nodeURI = "http://localhost:" + apiPortStr;
  if (wanIp.isEmpty()) {
    QMessageBox::warning(this, "Missing Network Config", "Network configuration is missing. Please go back to Network Setup.");
    return false;
  }

  const QString accountId = field("accountId").toString().trimmed();
  const QString nodeSecret = field("nodeSecret").toString().trimmed();
  const QString dbPass = field("dbPassword").toString();
  const bool skipDbPass = field("skipDbPassReplace").toBool();

  qDebug() << "Writing final configuration with:";
  qDebug() << "  IP:" << wanIp << "P2P:" << p2pPortStr << "API:" << apiPortStr;
  qDebug() << "  Account ID:" << accountId;
  qDebug() << "  nodeURI:" << nodeURI;
  qDebug() << "  DB Pass provided:" << !dbPass.isEmpty() << "Skip:" << skipDbPass;

  const QString annodeDir = m_wiz->installDir + "/annode";
  const QString defaultFile = annodeDir + "/conf/node-default.properties";
  const QString targetFile = annodeDir + "/conf/node.properties";

  qDebug() << "=== USING CORRECT PATHS ===";
  qDebug() << "Base install dir:" << m_wiz->installDir;
  qDebug() << "Annode dir:" << annodeDir;
  qDebug() << "Default file:" << defaultFile;
  qDebug() << "Target file:" << targetFile;

  if (!QDir(annodeDir).exists()) {
    QMessageBox::critical(this, "Error",
                          "Annode directory not found: " + annodeDir +
                              "\n\n"
                              "Please go back to Download page and ensure Annode is installed.");
    return false;
  }

  QFile target(targetFile);
  if (!target.exists()) {
    qDebug() << "Target file doesn't exist, creating from template...";

    if (!QFile::exists(defaultFile)) {
      QMessageBox::critical(this, "Error",
                            "Annode template not found!\n\n"
                            "Expected at: " +
                                defaultFile +
                                "\n\n"
                                "Please ensure Annode was extracted correctly.");
      return false;
    }

    QFile templateFile(defaultFile);
    if (!templateFile.copy(targetFile)) {
      QString error = templateFile.errorString();
      QMessageBox::critical(this, "Error",
                            "Failed to create node.properties from template.\n\n"
                            "Source: " +
                                defaultFile +
                                "\n"
                                "Destination: " +
                                targetFile +
                                "\n"
                                "Error: " +
                                error);
      return false;
    }
    qDebug() << "Created node.properties from template";
  }

#ifdef Q_OS_UNIX
  SystemUtils::chownUser(targetFile);
#endif

  if (!target.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::critical(this, "Error", "Cannot read node.properties");
    return false;
  }

  QString content = QString::fromUtf8(target.readAll());
  target.close();

  auto replaceKey = [&](const QString &key, const QString &value) {
    QRegularExpression re(QString("^\\s*%1\\s*=\\s*.*$").arg(QRegularExpression::escape(key)), QRegularExpression::MultilineOption);
    QString newLine = QString("%1 = %2").arg(key, value);
    if (content.contains(re)) {
      content.replace(re, newLine);
    } else {
      content += QString("\n%1 = %2\n").arg(key, value);
    }
  };

  replaceKey("DB.Username", "root");
  if (!skipDbPass)
    replaceKey("DB.Password", dbPass);
  replaceKey("anne.nodeAccountId", accountId);
  replaceKey("anne.nodeSecret", nodeSecret);
  replaceKey("anne.nodeURI", nodeURI);
  replaceKey("P2P.myAddress", wanIp + ":" + p2pPortStr);
  replaceKey("P2P.Port", p2pPortStr);
  replaceKey("API.Port", apiPortStr);
  replaceKey("P2P.NumBootstrapConnections", "25");

  const QString bootstrapPeers = QString::fromUtf8(NodePeers::BootstrapPeers);
  const QString rebroadcastTo = QString::fromUtf8(NodePeers::RebroadcastTo);
  replaceKey("P2P.BootstrapPeers", bootstrapPeers);
  replaceKey("P2P.rebroadcastTo", rebroadcastTo);

  if (!target.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    QMessageBox::critical(this, "Error", "Cannot write node.properties");
    return false;
  }

  target.write(content.toUtf8());
  target.close();

  setField("skipDbPassReplace", skipDbPass);

  qDebug() << "Configuration saved successfully to:" << targetFile;

  QMessageBox::information(this, "Success",
                           "<b>All settings saved successfully!</b><br><br>"
                           "• Account keys written<br>"
                           "• Network ports configured<br>"
                           "• Database configured<br>"
                           "• Bootstrap peers added<br><br>"
                           "Annode Configuration is now complete!");

  return true;
}
