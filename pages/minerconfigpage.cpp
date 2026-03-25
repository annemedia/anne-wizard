#include "minerconfigpage.h"
#include "../utils/jsutils.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

MinerConfigPage::MinerConfigPage(QWidget *parent) : QWizardPage(parent) {
  setTitle("ANNE Miner Configuration");
  setSubTitle("Generate mining account keys and configure nonce directories.");

  miningTypeGroup = new QGroupBox("Mining Type");
  soloMiningRadio = new QRadioButton("Solo Mining");
  shareMiningRadio = new QRadioButton("Share Mining");
  soloMiningRadio->setChecked(true);

  miningTypeInfo = new QLabel();
  miningTypeInfo->setWordWrap(true);
  miningTypeInfo->setText(
      "There is a SOLO-MINING reward and there are SHARE-MINING rewards. Half of the MINING REWARD goes to the SOLO block winner. The other half is divided up into 7 "
      "MINING "
      "SHARES with 7 SHARE MINING winners. This allows for smaller share miners that do not have as strong of HDD capacity to compete for a mining reward.");
  miningTypeInfo->setStyleSheet("QLabel { padding: 5px; background-color: #171717; border-radius: 3px; font-size: 11.5px; }");

  QHBoxLayout *typeLayout = new QHBoxLayout;
  typeLayout->addWidget(soloMiningRadio);
  typeLayout->addWidget(shareMiningRadio);
  typeLayout->addStretch();

  QVBoxLayout *typeGroupLayout = new QVBoxLayout;
  typeGroupLayout->addLayout(typeLayout);
  typeGroupLayout->addWidget(miningTypeInfo);
  miningTypeGroup->setLayout(typeGroupLayout);

  keyGroup = new QGroupBox("Generate Mining Account Keys");

  seedEdit = new QLineEdit();
  seedEdit->setPlaceholderText(QString("Enter custom seed (min %1 chars) or leave empty for auto-generate").arg(JSUtils::MIN_CUSTOM_SEED_LENGTH));

  generateKeysBtn = new QPushButton("Generate Mining Keys");
  generateKeysBtn->setEnabled(true);

  accountIdLabel = new QLabel("Account ID:");
  accountIdEdit = new QLineEdit();
  accountIdEdit->setReadOnly(true);
  accountIdCopyBtn = new QPushButton("Copy");
  accountIdCopyBtn->setEnabled(false);

  publicKeyLabel = new QLabel("Public Key:");
  publicKeyEdit = new QLineEdit();
  publicKeyEdit->setReadOnly(true);
  publicKeyCopyBtn = new QPushButton("Copy");
  publicKeyCopyBtn->setEnabled(false);

  seedDisplayLabel = new QLabel("Seed:");
  seedDisplayEdit = new QLineEdit();
  seedDisplayEdit->setReadOnly(true);
  seedCopyBtn = new QPushButton("Copy");
  seedCopyBtn->setEnabled(false);

  QFormLayout *keyForm = new QFormLayout;
  keyForm->addRow("Seed (optional):", seedEdit);
  keyForm->addRow("", generateKeysBtn);

  QHBoxLayout *accountIdLayout = new QHBoxLayout;
  accountIdLayout->addWidget(accountIdEdit);
  accountIdLayout->addWidget(accountIdCopyBtn);

  QHBoxLayout *publicKeyLayout = new QHBoxLayout;
  publicKeyLayout->addWidget(publicKeyEdit);
  publicKeyLayout->addWidget(publicKeyCopyBtn);

  QHBoxLayout *seedLayout = new QHBoxLayout;
  seedLayout->addWidget(seedDisplayEdit);
  seedLayout->addWidget(seedCopyBtn);

  keyForm->addRow(accountIdLabel, accountIdLayout);
  keyForm->addRow(publicKeyLabel, publicKeyLayout);
  keyForm->addRow(seedDisplayLabel, seedLayout);

  QVBoxLayout *keyLayout = new QVBoxLayout;
  keyLayout->addLayout(keyForm);
  keyGroup->setLayout(keyLayout);

  connect(generateKeysBtn, &QPushButton::clicked, this, &MinerConfigPage::generateMinerKeys);
  connect(accountIdCopyBtn, &QPushButton::clicked, this, &MinerConfigPage::copyAccountId);
  connect(publicKeyCopyBtn, &QPushButton::clicked, this, &MinerConfigPage::copyPublicKey);
  connect(seedCopyBtn, &QPushButton::clicked, this, &MinerConfigPage::copySeed);
  connect(soloMiningRadio, &QRadioButton::toggled, this, &MinerConfigPage::onMiningTypeChanged);
  connect(shareMiningRadio, &QRadioButton::toggled, this, &MinerConfigPage::onMiningTypeChanged);

  nonceGroup = new QGroupBox("Nonce Directories (Optional)");

  QLabel *nonceDesc = new QLabel("Select directories for your mining nonces, or pre-create empty ones. You can also leave this empty and edit config.yaml later.");
  nonceDesc->setWordWrap(true);
  nonceDesc->setStyleSheet("QLabel { font-size: 11.5px; }");

  nonceDirList = new QListWidget();

  addNonceDirBtn = new QPushButton("Add Directory");
  removeNonceDirBtn = new QPushButton("Remove Selected");
  removeNonceDirBtn->setEnabled(false);

  QHBoxLayout *nonceButtonLayout = new QHBoxLayout;
  nonceButtonLayout->addWidget(addNonceDirBtn);
  nonceButtonLayout->addWidget(removeNonceDirBtn);
  nonceButtonLayout->addStretch();

  QVBoxLayout *nonceLayout = new QVBoxLayout;
  nonceLayout->addWidget(nonceDesc);
  nonceLayout->addWidget(nonceDirList);
  nonceLayout->addLayout(nonceButtonLayout);
  nonceGroup->setLayout(nonceLayout);

  connect(addNonceDirBtn, &QPushButton::clicked, this, &MinerConfigPage::onNonceDirectorySelect);
  connect(removeNonceDirBtn, &QPushButton::clicked, this, [this]() {
    for (QListWidgetItem *item : nonceDirList->selectedItems()) {
      delete item;
    }
  });
  connect(nonceDirList, &QListWidget::itemSelectionChanged, this, [this]() { removeNonceDirBtn->setEnabled(!nonceDirList->selectedItems().isEmpty()); });

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->addWidget(miningTypeGroup);
  mainLayout->addWidget(keyGroup);
  mainLayout->addWidget(nonceGroup);

  mainLayout->addStretch();

  connect(accountIdCopyBtn, &QPushButton::clicked, this, &MinerConfigPage::updateCopyButtonStatus);
  connect(publicKeyCopyBtn, &QPushButton::clicked, this, &MinerConfigPage::updateCopyButtonStatus);
  connect(seedCopyBtn, &QPushButton::clicked, this, &MinerConfigPage::updateCopyButtonStatus);

  registerField("minerAccountId", accountIdEdit);
  registerField("minerPublicKey", publicKeyEdit);
  registerField("minerSeed", seedDisplayEdit);
}

void MinerConfigPage::initializePage() {
  qDebug() << "MinerConfigPage::initializePage()";

  m_wiz = qobject_cast<Wizard *>(wizard());
  if (!m_wiz) {
    return;
  }

  minerInstallDir = m_wiz->installDir + QDir::separator() + "anneminer";
  qDebug() << "Miner install dir:" << minerInstallDir;

  QString configPath = minerInstallDir + QDir::separator() + "config.yaml";
  if (!QFile::exists(configPath)) {
    QMessageBox::critical(this, "Error", "config.yaml not found at:\n" + configPath + "\n\nThis file is required for mining configuration.");
    return;
  };

  accountIdCopied = false;
  publicKeyCopied = false;
  seedCopied = false;

  bool isSolo = soloMiningRadio->isChecked();
  QString existingSeed = getFirstSeedFromProperties(isSolo);

  if (!existingSeed.isEmpty()) {

    seedEdit->setText(existingSeed);

    QTimer::singleShot(500, this, [this]() {
      if (!seedEdit->text().isEmpty()) {
        generateKeysBtn->click();
      }
    });

    qDebug() << "Found existing" << (isSolo ? "SOLO" : "SHARE") << "seed in node.properties, auto-populating keys";
  }

  emit completeChanged();
}

void MinerConfigPage::onNonceDirectorySelect() {
  QString dir = QFileDialog::getExistingDirectory(this, "Select Nonce Directory", QDir::homePath(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if (!dir.isEmpty()) {

    bool alreadyExists = false;
    for (int i = 0; i < nonceDirList->count(); ++i) {
      if (nonceDirList->item(i)->text() == dir) {
        alreadyExists = true;
        break;
      }
    }

    if (!alreadyExists) {
      nonceDirList->addItem(dir);
    }
  }
}

void MinerConfigPage::generateMinerKeys() {
  QString seedInput = seedEdit->text().trimmed();
  bool isCustom = !seedInput.isEmpty();

  if (isCustom && seedInput.length() < JSUtils::MIN_CUSTOM_SEED_LENGTH) {
    QMessageBox::warning(this, "Invalid Seed",
                         QString("Custom seed must be at least %1 characters (%2 bits).").arg(JSUtils::MIN_CUSTOM_SEED_LENGTH).arg(JSUtils::MIN_CUSTOM_SEED_LENGTH * 4));
    return;
  }

  QVariantMap resultMap = JSUtils::generateANNEKeys(seedInput);

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

  JSUtils::secureClear(minerAccountId);
  JSUtils::secureClear(minerPublicKey);
  JSUtils::secureClear(minerSeed);

  minerAccountId = resultMap["nid"].toString();
  minerPublicKey = resultMap["publicKey"].toString();
  minerSeed = resultMap["seed"].toString();

  for (auto &key : resultMap.keys()) {
    if (resultMap[key].canConvert<QString>()) {
      QString str = resultMap[key].toString();
      JSUtils::secureClear(str);
    }
  }
  resultMap.clear();

  if (minerAccountId.isEmpty() || minerSeed.isEmpty() || minerPublicKey.isEmpty()) {
    QMessageBox::warning(this, "Incomplete Keys", "Error: Incomplete keys generated.");
    return;
  }

  accountIdEdit->setText(minerAccountId);
  publicKeyEdit->setText(minerPublicKey);
  seedDisplayEdit->setText(minerSeed);

  accountIdCopyBtn->setEnabled(true);
  publicKeyCopyBtn->setEnabled(true);
  seedCopyBtn->setEnabled(true);

  accountIdCopied = false;
  publicKeyCopied = false;
  seedCopied = false;

  emit completeChanged();
}

void MinerConfigPage::copyAccountId() {
  QClipboard *clipboard = QApplication::clipboard();
  clipboard->setText(accountIdEdit->text());
  accountIdCopied = true;
  accountIdCopyBtn->setText("✓ Copied");
  accountIdCopyBtn->setEnabled(false);
  emit completeChanged();

  QTimer::singleShot(5000, this, [this]() {
    accountIdCopyBtn->setText("Copy");
    accountIdCopyBtn->setEnabled(true);
  });
}

void MinerConfigPage::copyPublicKey() {
  QClipboard *clipboard = QApplication::clipboard();
  clipboard->setText(publicKeyEdit->text());
  publicKeyCopied = true;
  publicKeyCopyBtn->setText("✓ Copied");
  publicKeyCopyBtn->setEnabled(false);
  emit completeChanged();

  QTimer::singleShot(5000, this, [this]() {
    publicKeyCopyBtn->setText("Copy");
    publicKeyCopyBtn->setEnabled(true);
  });
}

void MinerConfigPage::copySeed() {
  QClipboard *clipboard = QApplication::clipboard();
  clipboard->setText(seedDisplayEdit->text());
  seedCopied = true;
  seedCopyBtn->setText("✓ Copied");
  seedCopyBtn->setEnabled(false);
  emit completeChanged();

  QTimer::singleShot(5000, this, [this]() {
    seedCopyBtn->setText("Copy");
    seedCopyBtn->setEnabled(true);
  });
}

void MinerConfigPage::updateCopyButtonStatus() { emit completeChanged(); }

bool MinerConfigPage::isComplete() const {

  if (minerAccountId.isEmpty() || minerPublicKey.isEmpty() || minerSeed.isEmpty()) {
    return false;
  }

  if (!accountIdCopied || !publicKeyCopied || !seedCopied) {
    return false;
  }

  return true;
}

QString MinerConfigPage::getSecretPhraseSuffix() const { return soloMiningRadio->isChecked() ? "-SOLO" : "-SHARE"; }

QString MinerConfigPage::getApiPort() const {
  if (!m_wiz->apiPort.isEmpty()) {
    return m_wiz->apiPort;
  }
  return "9116";
}

bool MinerConfigPage::updateNodeProperties() {
  QString nodePropsPath = m_wiz->installDir + QDir::separator() + "annode" + QDir::separator() + "conf" + QDir::separator() + "node.properties";
  if (!QFile::exists(nodePropsPath)) {
    QMessageBox::critical(this, "Error", "node.properties not found at:\n" + nodePropsPath + "\n\nThis file is required for mining configuration.");
    return false;
  }

  QFile nodePropsFile(nodePropsPath);
  if (!nodePropsFile.open(QIODevice::ReadWrite | QIODevice::Text)) {
    QMessageBox::critical(this, "Error", "Cannot open miner node.properties for reading/writing.");
    return false;
  }

  QString content = QString::fromUtf8(nodePropsFile.readAll());
  nodePropsFile.close();

  bool isSolo = soloMiningRadio->isChecked();
  QString allowProperty = isSolo ? "AllowOtherSoloMiners" : "AllowOtherShareMiners";
  QString passphraseProperty = isSolo ? "SoloMiningPassphrases" : "ShareMiningPassphrases";

  QStringList lines = content.split('\n');
  bool foundAllow = false;
  bool foundPassphrase = false;

  for (int i = 0; i < lines.size(); ++i) {
    QString line = lines[i].trimmed();

    if (line.startsWith(allowProperty)) {
      QRegularExpression allowRegex("^" + QRegularExpression::escape(allowProperty) + "\\s*=.*$");
      if (allowRegex.match(line).hasMatch()) {
        lines[i] = allowProperty + " = true";
        foundAllow = true;
      }
    }

    else if (line.startsWith(passphraseProperty) && !minerSeed.isEmpty()) {
      QRegularExpression passRegex("^" + QRegularExpression::escape(passphraseProperty) + "\\s*=\\s*(.+)$");
      QRegularExpressionMatch match = passRegex.match(line);

      if (match.hasMatch()) {
        QString existingPassphrases = match.captured(1).trimmed();

        QStringList seeds = existingPassphrases.split(';', Qt::SkipEmptyParts);

        for (int j = 0; j < seeds.size(); ++j) {
          seeds[j] = seeds[j].trimmed();
        }

        bool seedExists = seeds.contains(minerSeed);

        if (seedExists) {

          if (seeds.size() == 1) {

            foundPassphrase = true;
            qDebug() << "Seed already exists as the only seed, keeping as-is";
          } else {

            foundPassphrase = true;
            qDebug() << "Seed exists among multiple seeds, keeping existing";
          }
        } else {

          if (!existingPassphrases.isEmpty() && !existingPassphrases.endsWith(';')) {
            existingPassphrases += ";";
          }
          existingPassphrases += minerSeed;
          lines[i] = passphraseProperty + " = " + existingPassphrases;
          foundPassphrase = true;
          qDebug() << "Added new seed to existing passphrases";
        }
      }
    }
  }

  if (!foundAllow) {
    lines.append(allowProperty + " = true");
  }

  if (!foundPassphrase && !minerSeed.isEmpty()) {
    lines.append(passphraseProperty + " = " + minerSeed);
  }

  QMap<QString, int> propertyLastSeen;
  for (int i = lines.size() - 1; i >= 0; i--) {
    QString line = lines[i].trimmed();

    int equalsPos = line.indexOf('=');
    if (equalsPos > 0) {
      QString propertyName = line.left(equalsPos).trimmed();

      if (propertyName == allowProperty || propertyName == passphraseProperty) {
        if (propertyLastSeen.contains(propertyName)) {

          lines.removeAt(i);
        } else {
          propertyLastSeen[propertyName] = i;
        }
      }
    }
  }

  if (!nodePropsFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    QMessageBox::critical(this, "Error", "Cannot write miner node.properties");
    return false;
  }

  QTextStream out(&nodePropsFile);
  out << lines.join('\n');
  nodePropsFile.close();

  qDebug() << "Updated miner node.properties for" << (isSolo ? "SOLO" : "SHARE") << "mining";

  return true;
}

bool MinerConfigPage::updateConfigYamlFile() {
  if (minerAccountId.isEmpty()) {
    QMessageBox::warning(this, "Missing Account ID", "Please generate mining keys first.");
    generateKeysBtn->setFocus();
    return false;
  }

  QString configPath = minerInstallDir + QDir::separator() + "config.yaml";
  QFile configFile(configPath);

  if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::critical(this, "Error", "Cannot read config.yaml");
    return false;
  }

  QString content = QString::fromUtf8(configFile.readAll());
  configFile.close();

  QString secretPhraseEntry = minerAccountId + ": '" + getSecretPhraseSuffix() + "'";

  QRegularExpression accountSectionRegex("account_id_to_secret_phrase:(.*?)(?=\\n\\w|$)");
  accountSectionRegex.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);

  QString miningTypeStr = soloMiningRadio->isChecked() ? "solo" : "share";

  if (content.contains(accountSectionRegex)) {
    content.replace(accountSectionRegex,
                    QString("account_id_to_secret_phrase: # define accounts and passphrases for %1 mining\n %2").arg(miningTypeStr).arg(secretPhraseEntry));
  } else {
    content =
        content.trimmed() + "\n\n# define accounts and passphrases for " + miningTypeStr + " mining\n" + "account_id_to_secret_phrase:\n " + secretPhraseEntry + "\n";
  }

  QStringList nonceEntries;
  for (int i = 0; i < nonceDirList->count(); ++i) {
    QString dir = nonceDirList->item(i)->text();

#ifdef Q_OS_WIN
    dir.replace("\\", "\\\\");
    nonceEntries.append("  - '" + dir + "'");
#else
    nonceEntries.append("  - '" + dir + "'");
#endif
  }

  if (!nonceEntries.isEmpty()) {
    QString nonceDirsSection = "plot_dirs:\n" + nonceEntries.join("\n");
    QRegularExpression nonceSectionRegex("plot_dirs:(.*?)(?=\\n\\w|$)");
    nonceSectionRegex.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);

    if (content.contains(nonceSectionRegex)) {
      content.replace(nonceSectionRegex, nonceDirsSection);
    } else {
      content = content.trimmed() + "\n\n" + nonceDirsSection + "\n";
    }
  }

  QString url = "http://localhost:" + getApiPort();
  QRegularExpression urlRegex("url:\\s*['\"].*?['\"]");

  if (content.contains(urlRegex)) {
    content.replace(urlRegex, "url: '" + url + "'");
  } else {
    content = content.trimmed() + "\n\nurl: '" + url + "'";
  }

  if (!configFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    QMessageBox::critical(this, "Error", "Cannot write config.yaml");
    return false;
  }

  QTextStream out(&configFile);
  out << content;
  configFile.close();

  qDebug() << "Updated miner config.yaml with mining account ID:" << minerAccountId;

  return updateNodeProperties();
}

bool MinerConfigPage::validatePage() {
  qDebug() << "MinerConfigPage::validatePage()";

  if (minerAccountId.isEmpty()) {
    QMessageBox::warning(this, "Missing Keys", "Please generate mining keys before proceeding.");
    generateKeysBtn->setFocus();
    return false;
  }

  if (!accountIdCopied || !publicKeyCopied || !seedCopied) {
    QMessageBox::warning(this, "Copy Required", "Please copy all keys to clipboard before proceeding. This ensures you have securely saved your mining keys.");
    return false;
  }

  if (!updateConfigYamlFile()) {
    QMessageBox::warning(this, "Configuration Error",
                         "Failed to update miner configuration file.\n"
                         "You can edit config.yaml manually in the miner directory.");
    return false;
  }

  QString miningTypeDisplay = soloMiningRadio->isChecked() ? "SOLO" : "SHARE";
  QMessageBox::information(this, "Success",
                           "<b>Miner configuration saved successfully!</b><br><br>"
                           "• Mining keys generated and copied<br>"
                           "• config.yaml updated<br>"
                           "• node.properties updated with mining seed<br>"
                           "• Mining type: " +
                               miningTypeDisplay +
                               "<br><br>"
                               "<font color='green'>✓ All keys have been copied to clipboard</font>");

  return true;
}

QString MinerConfigPage::getFirstSeedFromProperties(bool forSoloMining) {

  QString nodePropsPath = m_wiz->installDir + QDir::separator() + "annode" + QDir::separator() + "conf" + QDir::separator() + "node.properties";
  if (!QFile::exists(nodePropsPath)) {
    return QString();
  }

  QFile nodePropsFile(nodePropsPath);
  if (!nodePropsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }

  QString content = QString::fromUtf8(nodePropsFile.readAll());
  nodePropsFile.close();

  QString passphraseProperty = forSoloMining ? "SoloMiningPassphrases" : "ShareMiningPassphrases";
  QStringList lines = content.split('\n');

  for (const QString &line : lines) {
    QString trimmedLine = line.trimmed();

    QRegularExpression propRegex("^" + QRegularExpression::escape(passphraseProperty) + "\\s*=\\s*(.+)$");
    QRegularExpressionMatch match = propRegex.match(trimmedLine);

    if (match.hasMatch()) {
      QString passphrases = match.captured(1).trimmed();
      if (!passphrases.isEmpty()) {

        QStringList seeds = passphrases.split(';', Qt::SkipEmptyParts);
        if (!seeds.isEmpty()) {
          return seeds.first().trimmed();
        }
      }
    }
  }

  return QString();
}

void MinerConfigPage::onMiningTypeChanged(bool checked) {
  if (!checked) {
    return;
  }

  QRadioButton *changedRadio = qobject_cast<QRadioButton *>(sender());
  if (!changedRadio || !changedRadio->isChecked()) {
    return;
  }

  bool isSolo = (changedRadio == soloMiningRadio);
  QString existingSeed = getFirstSeedFromProperties(isSolo);

  qDebug() << "Mining type changed to" << (isSolo ? "SOLO" : "SHARE") << ", existing seed:" << (existingSeed.isEmpty() ? "none" : "found");

  if (!existingSeed.isEmpty()) {

    if (seedEdit->text() != existingSeed) {
      accountIdEdit->clear();
      publicKeyEdit->clear();
      seedDisplayEdit->clear();

      accountIdCopied = false;
      publicKeyCopied = false;
      seedCopied = false;

      seedEdit->setText(existingSeed);

      QTimer::singleShot(100, this, [this]() {
        if (!seedEdit->text().isEmpty()) {
          qDebug() << "Auto-generating keys with seed from node.properties";
          generateKeysBtn->click();
        }
      });
    }
  } else {

    if (!seedEdit->text().isEmpty()) {

      seedEdit->clear();
      accountIdEdit->clear();
      publicKeyEdit->clear();
      seedDisplayEdit->clear();

      accountIdCopied = false;
      publicKeyCopied = false;
      seedCopied = false;

      emit completeChanged();
    }
  }
}
