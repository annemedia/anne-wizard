
#include "intropage.h"
#include <QDebug>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpacerItem>
#include <QStyle>
#include <QVBoxLayout>
#include <QtGlobal>
#include <cmath>
#if defined(Q_OS_WIN)
  #include <windows.h>
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  #include <sys/types.h>
  #include <sys/sysctl.h>
#elif defined(Q_OS_LINUX)
  #include <sys/sysinfo.h>
#else
  #include <unistd.h>
#endif
#include <cmath> 

IntroPage::IntroPage(QWidget *parent) : QWizardPage(parent) {
  setTitle("Welcome to the A.N.N.E. Installation Wizard!");
  setSubTitle("Checking your system compatibility...");

  QLabel *welcomeLabel = new QLabel("<p>"
                                    "<p>You're about to install annode on your system. Depending on your hardware and internet speed, the installation will take "
                                    "anywhere from <b>10 to 60 minutes</b>.<br><br>"
                                    "Now’s the perfect time to wiz a cuppa! ☕</p>");
  welcomeLabel->setWordWrap(true);
  welcomeLabel->setAlignment(Qt::AlignLeft);
  welcomeLabel->setStyleSheet("font-size: 13px; margin: 0px;");

  const QString asciiArt = QStringLiteral("\n"
                                          "      __/\\__      \n"
                                          ". _   \\\\''//      \n"
                                          "-( )--/_||_\\      \n"
                                          " .'.  \\_()_/      \n"
                                          "  |    | . \\      \n"
                                          "   |anne| .  \\    \n"
                                          "    .'.  ,\\_____'. \n");

  artLabel = new QLabel(asciiArt);
  artLabel->setAlignment(Qt::AlignCenter);
  artLabel->setFont(QFont("Courier New", 15, QFont::Normal));
  artLabel->setStyleSheet("color: #fefe17; padding: 2px;");

  opacityEffect = new QGraphicsOpacityEffect(this);
  opacityEffect->setOpacity(1.0);
  artLabel->setGraphicsEffect(opacityEffect);

  pulseAnimation = new QPropertyAnimation(opacityEffect, "opacity", this);
  pulseAnimation->setDuration(1500);
  pulseAnimation->setStartValue(0.7);
  pulseAnimation->setEndValue(1.0);
  pulseAnimation->setLoopCount(-1);
  pulseAnimation->start();

  int cpuCores = QThread::idealThreadCount();
  double totalRAM = getTotalRAM();

  QGroupBox *reqGroup = new QGroupBox("Minimum System Requirements");
  reqGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid gray; border-radius: 5px; margin-top: 1ex; padding-top: 10px; } "
                          "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");

  QVBoxLayout *reqLayout = new QVBoxLayout(reqGroup);

  QLabel *minCPULabel = new QLabel("Minimum: 4 CPU cores");
  minCPULabel->setStyleSheet("font-weight: bold;");

  QLabel *detectedCPULabel = new QLabel(QString("Detected: %1 CPU cores").arg(cpuCores));

  QLabel *minRAMLabel = new QLabel("Minimum: 4 GB RAM");
  minRAMLabel->setStyleSheet("font-weight: bold;");

  QLabel *detectedRAMLabel = new QLabel(QString("Detected: %1 GB RAM").arg(totalRAM));

  bool meetsRequirements = (cpuCores >= 4) && (totalRAM >= 4.0);

  QLabel *statusLabel = new QLabel();
  if (meetsRequirements) {
    statusLabel->setText("✓ Your system meets the minimum requirements. You're good to go!");
    statusLabel->setStyleSheet("color: #4CAF50; font-weight: bold;");
  } else {
    statusLabel->setText("⚠ Warning: Your system does not meet recommended minimum requirements. "
                         "You may proceed, but performance may be slow.");
    statusLabel->setStyleSheet("color: #FF9800; font-weight: bold;");
  }
  statusLabel->setWordWrap(true);

  reqLayout->addWidget(minCPULabel);
  reqLayout->addWidget(detectedCPULabel);
  reqLayout->addSpacing(10);
  reqLayout->addWidget(minRAMLabel);
  reqLayout->addWidget(detectedRAMLabel);
  reqLayout->addSpacing(10);
  reqLayout->addWidget(statusLabel);
  reqLayout->addStretch();

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->addWidget(welcomeLabel);
  mainLayout->addWidget(artLabel);
  mainLayout->addWidget(reqGroup);
  mainLayout->addStretch();
}



double IntroPage::getTotalRAM() const {
  const double ONE_GB = 1000000000.0;

#ifdef Q_OS_WIN
  MEMORYSTATUSEX memInfo{};
  memInfo.dwLength = sizeof(MEMORYSTATUSEX);

  if (GlobalMemoryStatusEx(&memInfo)) {
    double gb = static_cast<double>(memInfo.ullTotalPhys) / ONE_GB;
    return std::round(gb * 10.0) / 10.0;
  }

#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  uint64_t mem = 0;
  size_t len = sizeof(mem);
  int mib[2] = {CTL_HW, HW_MEMSIZE};
  
  if (sysctl(mib, 2, &mem, &len, NULL, 0) == 0) {
    double gb = static_cast<double>(mem) / ONE_GB;
    return std::round(gb * 10.0) / 10.0;
  }

#elif defined(Q_OS_LINUX)
  struct sysinfo info;
  if (sysinfo(&info) == 0) {
    double bytes = static_cast<double>(info.totalram) * info.mem_unit;
    double gb = bytes / ONE_GB;
    return std::round(gb * 10.0) / 10.0;
  }

#else
  #ifdef _SC_PHYS_PAGES
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    
    if (pages > 0 && page_size > 0) {
      double bytes = static_cast<double>(pages) * page_size;
      double gb = bytes / ONE_GB;
      return std::round(gb * 10.0) / 10.0;
    }
  #endif

#endif

  return 0.0;
}