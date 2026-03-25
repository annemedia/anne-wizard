#include "finalpage.h"
#include "../utils/systemutils.h"
#include <QCheckBox>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QVBoxLayout>

FinalPage::FinalPage(QWidget *parent) : QWizardPage(parent) {
  setTitle(tr("Installation Complete"));
  setSubTitle(tr("Configure launcher and system integration"));

  auto *mainLayout = new QVBoxLayout(this);

  auto *integrationGroup = new QGroupBox(tr("Desktop Integration Options"));
  auto *intLayout = new QVBoxLayout(integrationGroup);

  cbDesktopShortcut = new QCheckBox(tr("Create &desktop shortcut"));
  cbMenuEntry = new QCheckBox(tr("Add to &applications menu / start menu"));
  cbLaunchOnBoot = new QCheckBox(tr("Launch annode &automatically at system startup"));
  cbHeadlessMode = new QCheckBox(tr("Run in &headless mode (no GUI, opens terminal with logs)"));

  cbHeadlessMode->setToolTip(tr("When enabled:\n"
                                "• A terminal window will open showing live annode logs\n"
                                "• Press Ctrl+C in the terminal to gracefully stop annode\n"
                                "• You can always view logs from the annode directory with:\n"
                                "    tail -f annode.log"));

  cbHeadlessMode->setToolTip(cbHeadlessMode->toolTip() + tr("\n\nDesktop shortcut and Applications menu entry will be "
                                                            "created automatically and cannot be disabled\n"
                                                            "(required to launch annode in headless mode)"));

  cbLaunchNow = new QCheckBox(tr("Start annode &immediately after finishing"));

  cbDesktopShortcut->setChecked(true);
  cbMenuEntry->setChecked(true);
  cbLaunchOnBoot->setChecked(false);
  cbHeadlessMode->setChecked(false);
  cbLaunchNow->setChecked(true);

  intLayout->addWidget(cbDesktopShortcut);
  intLayout->addWidget(cbMenuEntry);
  intLayout->addWidget(cbLaunchOnBoot);
  intLayout->addWidget(cbHeadlessMode);
  intLayout->addWidget(cbLaunchNow);

  const QString asciiArt = QStringLiteral("\n"
                                          "      __/\\__      \n"
                                          ". _   \\\\''//      \n"
                                          "-( )--/_||_\\      \n"
                                          " .'.  \\_()_/      \n"
                                          "  |    | . \\      \n"
                                          "   |anne| .  \\    \n"
                                          "    .'.  ,\\_____'. \n");

  QLabel *artLabel = new QLabel(asciiArt);
  artLabel->setAlignment(Qt::AlignCenter);
  artLabel->setFont(QFont("Courier New", 15, QFont::Normal));
  artLabel->setStyleSheet("color: #fefe17; padding: 2px;");

  QGraphicsOpacityEffect *opacityEffect = new QGraphicsOpacityEffect(this);
  opacityEffect->setOpacity(1.0);
  artLabel->setGraphicsEffect(opacityEffect);

  QPropertyAnimation *pulseAnimation = new QPropertyAnimation(opacityEffect, "opacity", this);
  pulseAnimation->setDuration(1500);
  pulseAnimation->setStartValue(0.7);
  pulseAnimation->setEndValue(1.0);
  pulseAnimation->setLoopCount(-1);
  pulseAnimation->start();

  finalInfo = new QLabel();
  finalInfo->setWordWrap(true);
  finalInfo->setTextFormat(Qt::RichText);
  finalInfo->setTextInteractionFlags(Qt::TextBrowserInteraction);
  finalInfo->setStyleSheet("QLabel { padding: 5px; margin-top:20px; background-color: #171717; border-radius: 3px; }");

  mainLayout->addWidget(integrationGroup);
  mainLayout->addWidget(artLabel);
  mainLayout->addWidget(finalInfo);
  mainLayout->addStretch();

  registerField("final.createDesktopShortcut", cbDesktopShortcut);
  registerField("final.createMenuEntry", cbMenuEntry);
  registerField("final.launchOnBoot", cbLaunchOnBoot);
  registerField("final.runHeadless", cbHeadlessMode);
  registerField("final.launchNow", cbLaunchNow);
}

void FinalPage::initializePage() {

  m_wiz = qobject_cast<Wizard *>(wizard());
  if (!m_wiz) {
    return;
  }
  QString apiPortStr = "9116";

  apiPortStr = m_wiz->apiPort.isEmpty() ? "9116" : m_wiz->apiPort;

  finalInfo->setText("<p>Congrats! You wized through the ANNE Wizard like a boss.</p>"
                     "<p>Once your annode is up and running, check out the goodies at</p>"
                     "<p><a href=\"http://localhost:" +
                     apiPortStr +
                     "/ANNE.html\">"
                     "http://localhost:" +
                     apiPortStr + "/ANNE.html</a></p>");

  emit completeChanged();
  cbDesktopShortcut->setChecked(field("final.createDesktopShortcut").toBool());
  cbMenuEntry->setChecked(field("final.createMenuEntry").toBool());
  cbLaunchOnBoot->setChecked(field("final.launchOnBoot").toBool());
  cbHeadlessMode->setChecked(field("final.runHeadless").toBool());
  cbLaunchNow->setChecked(field("final.launchNow").toBool());

  bool headless = cbHeadlessMode->isChecked();

  cbDesktopShortcut->setChecked(true);
  cbMenuEntry->setChecked(true);

  cbDesktopShortcut->setEnabled(!headless);
  cbMenuEntry->setEnabled(!headless);

  connect(cbHeadlessMode, &QCheckBox::toggled, this, [this](bool checked) {
    cbDesktopShortcut->setChecked(true);
    cbMenuEntry->setChecked(true);
    cbDesktopShortcut->setEnabled(!checked);
    cbMenuEntry->setEnabled(!checked);
  });
}

bool FinalPage::validatePage() {
  if (m_wiz->installDir.isEmpty())
    return false;

  QStringList details;
  QString summary;
  bool success = SystemUtils::createDesktopIntegration(m_wiz->installDir, field("final.createDesktopShortcut").toBool(), field("final.createMenuEntry").toBool(),
                                                       field("final.launchOnBoot").toBool(), field("final.runHeadless").toBool(), field("final.launchNow").toBool(),
                                                       details, summary);

  QMessageBox::information(this, success ? tr("Success") : tr("Partial Success"), summary);

  return true;
}

bool FinalPage::isComplete() const { return true; }