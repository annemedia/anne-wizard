#include "netconfigpage.h"
#include "../utils/systemutils.h"
#include "../utils/utils.h"
#include <QFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

NetConfigPage::NetConfigPage(QWidget *parent) : QWizardPage(parent) {
  setTitle("Configuration - Network Setup");
  setSubTitle("Configure network settings and apply configuration.");

  netUtils = new NetUtils(this);

  auto *mainLayout = new QVBoxLayout(this);

  statusLabel = new QLabel("Ready");
  statusLabel->setStyleSheet("QLabel { font-weight: bold; }");

  networkGroup = new QGroupBox("Network Configuration");
  auto *networkLayout = new QVBoxLayout(networkGroup);

  ipLabel = new QLabel("WAN IP (auto-detected, editable):");
  ipEdit = new QLineEdit;
  ipEdit->setPlaceholderText("Detecting WAN IP...");
  networkLayout->addWidget(ipLabel);
  networkLayout->addWidget(ipEdit);

  auto *portLayout = new QHBoxLayout;

  portLayout->addWidget(new QLabel("P2P Port:"));
  p2pPortEdit = new QLineEdit("9115");
  p2pPortEdit->setFixedWidth(80);
  portLayout->addWidget(p2pPortEdit);

  p2pPortStatus = new QLabel("✓");
  p2pPortStatus->setStyleSheet("color: green; font-weight: bold;");
  p2pPortStatus->setFixedWidth(20);
  p2pPortStatus->setAlignment(Qt::AlignCenter);
  portLayout->addWidget(p2pPortStatus);

  portLayout->addStretch();

  portLayout->addWidget(new QLabel("API Port:"));
  apiPortEdit = new QLineEdit("9116");
  apiPortEdit->setFixedWidth(80);
  portLayout->addWidget(apiPortEdit);

  apiPortStatus = new QLabel("✓");
  apiPortStatus->setStyleSheet("color: green; font-weight: bold;");
  apiPortStatus->setFixedWidth(20);
  apiPortStatus->setAlignment(Qt::AlignCenter);
  portLayout->addWidget(apiPortStatus);

  networkLayout->addLayout(portLayout);

  networkStatusLabel = new QLabel("Analyzing network...");
  networkStatusLabel->setWordWrap(true);

  auto *portGroup = new QGroupBox("Important: P2P Port Forwarding (Required for Best Connectivity)");
  auto *portLayoutBox = new QVBoxLayout(portGroup);

  infoText = new QTextEdit;
  infoText->setReadOnly(true);
  infoText->setFrameStyle(QFrame::NoFrame);
  infoText->setStyleSheet("background-color: transparent; color: palette(text);");

  auto *scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFixedHeight(290);
  scroll->setWidget(infoText);

  portLayoutBox->addWidget(scroll);

  mainLayout->addWidget(statusLabel);
  mainLayout->addWidget(networkGroup);
  mainLayout->addWidget(networkStatusLabel);
  mainLayout->addWidget(portGroup);
  mainLayout->addStretch();

  registerField("p2pPort", p2pPortEdit);

  connect(ipEdit, &QLineEdit::textChanged, this, &NetConfigPage::validateAll);
  connect(p2pPortEdit, &QLineEdit::textChanged, this, &NetConfigPage::validateAll);
  connect(apiPortEdit, &QLineEdit::textChanged, this, &NetConfigPage::validateAll);

  connect(netUtils, &NetUtils::wanIpDetected, this, [this](const QString &ip) {
    if (ip.isEmpty()) {
      Utils::updateStatus(statusLabel, "Failed to detect WAN IP – enter manually", StatusType::Error);
      ipEdit->setPlaceholderText("Enter WAN IP or IPv6 address");
    } else {
      ipEdit->setText(ip);
      Utils::updateStatus(statusLabel, QString("WAN IP detected: %1").arg(ip), StatusType::Success);
    }

    if (!ip.isEmpty()) {
      NetworkInfo info;
      info.wanIp = ip;
      if (info.isTor) {
        QMessageBox::warning(this, "Tor Detected",
                             "Connectivity over Tor isn't supported for bidirectional P2P "
                             "networking.<br><br>"
                             "For annode to work, you can either connect over a VPN that "
                             "supports port forwarding or use your IP address.<br><br>"
                             "You may proceed regardless, but may need to reconfigure annode "
                             "IP settings manually.");
        updateNetworkInfoDisplay(info);
      }
    }

    validateAll();
  });
}

void NetConfigPage::initializePage() {
  Utils::updateStatus(statusLabel, "Detecting WAN IP...", StatusType::Progress);
  ipEdit->clear();
  ipEdit->setPlaceholderText("Detecting WAN IP...");

  NetworkInfo info;
  netUtils->detectWanIp(true, &info);

  QTimer::singleShot(100, this, [this, info]() { updateNetworkInfoDisplay(info); });

  validateAll();
}

void NetConfigPage::updateNetworkInfoDisplay(const NetworkInfo &info) {
  int port = field("p2pPort").toInt();
  if (port == 0)
    port = 9115;

  qDebug() << "=== NETWORK DETECTION DEBUG ===";
  qDebug() << "Local IP:       " << info.localIp;
  qDebug() << "WAN IP:         " << info.wanIp;
  qDebug() << "isTor:          " << info.isTor;
  qDebug() << "isVpn:          " << info.isVpn;
  qDebug() << "isLanBehindNat: " << info.isLanBehindNat;
  qDebug() << "isCgnat:        " << info.isCgnat;
  qDebug() << "vpnForwardingLikely:" << info.vpnForwardingLikely;
  qDebug() << "================================";

  QString status;
  QString details = "<p>Annode is a peer-to-peer application. For optimal network "
                    "performance and bidirectional connectivity with other nodes, "
                    "your P2P port <b>must be reachable from the internet</b>. Without "
                    "it, annode can still stay up to date with blocks and send outgoing "
                    "transactions, but some features will not work.</p>"
                    "<p>Note: Your local OS firewall rules will be automatically "
                    "configured on next action - double check in your firewall</p>";

  QString specific = QString();

  if (info.isTor) {
    status = "<font color='red'><b>Tor Detected</b></font>";
    specific = "<p><font color='red'>Connectivity over Tor isn't supported "
               "for bidirectional P2P.</font><br>"
               "Note: You may run annode over Tor but be aware of the aforesaid limitations. Alternatively use a "
               "VPN connection that supports port forwarding or your real WAN IP.</p>";
  } else if (info.isVpn) {
    status = QString("<font color='%1'><b>VPN Detected</b></font>").arg(info.vpnForwardingLikely ? "green" : "orange");
    specific = QString("<p><font color='%1'>The VPN provider should support port "
                       "forwarding for bidirectional connections.</font> If not "
                       "possible, connectivity will be limited.</p>")
                   .arg(info.vpnForwardingLikely ? "green" : "orange");
  } else if (info.isLanBehindNat) {
    status = "<font color='orange'><b>LAN Detected</b></font>";
    specific = "<p><font color='orange'>You are on a local network. "
               "Prioritize opening the port on your router for "
               "bidirectional connectivity.</font></p>";
  } else {
    status = "<font color='green'><b>Direct Public IP</b></font>";
    specific = "<p><font color='green'>Optimal setup - no extra configuration "
               "needed for bidirectional connectivity.</font></p>";
  }

  if (info.isCgnat) {
    status = "<font color='red'><b>CGNAT Detected</b></font>";
    specific += "<p><font color='red'>Opening port on router won't work due "
                "to ISP CGNAT. Prioritize using a VPN that offers dedicated "
                "port forwarding for inbound connections.</font></p>";
  }

  QString realGateway = info.gateway;
  if (info.isVpn && !info.gateway.isEmpty()) {
    realGateway = "192.168.1.1";
  }
  if (realGateway.isEmpty())
    realGateway = "192.168.1.1";

  QString routerOption = QString("<p><b>Option %1: Use your real IP - Port forwarding on router</b></p>"
                                 "<ul>"
                                 "<li>Log into your router as admin: %2 (usually at 192.168.1.1 or "
                                 "192.168.0.1, default login details may be found on the router's "
                                 "label or in its manual)</li>"
                                 "<li>Look for Port Forwarding / Virtual Server / NAT settings</li>"
                                 "<li>Create a new rule (note the definitions may vary by router type; "
                                 "if not sure, consult your router manual.<br>"
                                 " • External Port: <b>%3</b><br>"
                                 " • Internal IP: your computer's LAN IP (%4)<br>"
                                 " • Internal Port: <b>%3</b><br>"
                                 " • Protocol: TCP</li>"
                                 "</ul>")
                             .arg((info.isVpn || info.isCgnat || info.isTor) ? "2" : "1")
                             .arg(realGateway)
                             .arg(port)
                             .arg(info.localIp.isEmpty() ? "your PC IP" : info.localIp);

  QString vpnOption = QString("<p><b>Option %1: Use a VPN with port forwarding support</b></p>"
                              "<ul>"
                              "<li>Choose a VPN provider that offers dedicated forwarded ports</li>"
                              "<li>Connect to the VPN → get your assigned public port → change to "
                              "it in annode settings later</li>"
                              "</ul>")
                          .arg((info.isVpn || info.isCgnat || info.isTor) ? "1" : "2");

  if (info.isVpn || info.isCgnat || info.isTor) {
    details += specific + vpnOption + routerOption;
  } else {
    details += specific + routerOption + vpnOption;
  }

  networkStatusLabel->setText(status);
  infoText->setHtml(details);
}

void NetConfigPage::validateAll() {
  const QString ipText = ipEdit->text().trimmed();
  const bool ipOk = !ipText.isEmpty();

  int dummy;
  const bool p2pOk = p2pPortEdit->text().isEmpty() || netUtils->isValidPort(p2pPortEdit->text(), &dummy, 1, 65535);

  const bool apiOk = apiPortEdit->text().isEmpty() || netUtils->isValidPort(apiPortEdit->text(), &dummy, 1, 65535);

  p2pPortStatus->setText(p2pOk ? "✓" : "✗");
  p2pPortStatus->setStyleSheet(p2pOk ? "color: green; font-weight: bold;" : "color: red; font-weight: bold;");
  p2pPortStatus->setToolTip(p2pOk ? "" : "Port must be 1-65535");

  apiPortStatus->setText(apiOk ? "✓" : "✗");
  apiPortStatus->setStyleSheet(apiOk ? "color: green; font-weight: bold;" : "color: red; font-weight: bold;");
  apiPortStatus->setToolTip(apiOk ? "" : "Port must be 1-65535");

  emit completeChanged();
}

bool NetConfigPage::isComplete() const {
  if (ipEdit->text().trimmed().isEmpty())
    return false;
  if (!p2pPortEdit->text().isEmpty() && !netUtils->isValidPort(p2pPortEdit->text()))
    return false;
  if (!apiPortEdit->text().isEmpty() && !netUtils->isValidPort(apiPortEdit->text()))
    return false;
  return true;
}

bool NetConfigPage::validatePage() {
  auto *wiz = qobject_cast<Wizard *>(wizard());
  if (!wiz)
    return false;

  QString wanIp = ipEdit->text().trimmed();
  if (wanIp.isEmpty()) {
    QMessageBox::warning(this, "Error", "WAN IP is required.");
    ipEdit->setFocus();
    return false;
  }

  const QString p2pPortStr = netUtils->validatePortOrDefault(p2pPortEdit->text(), 9115);
  const QString apiPortStr = netUtils->validatePortOrDefault(apiPortEdit->text(), 9116);
  const quint16 p2pPort = p2pPortStr.toUShort();

  wiz->wanIp = wanIp;
  wiz->p2pPort = p2pPortStr;
  wiz->apiPort = apiPortStr;

  qDebug() << "Network config stored in wizard - IP:" << wanIp << ", P2P Port:" << p2pPortStr << ", API Port:" << apiPortStr;

  QString firewallMsg = netUtils->openFirewallPort(p2pPort, "tcp");
  Utils::updateStatus(statusLabel, QString("%1<br>%2").arg(statusLabel->text()).arg(firewallMsg), StatusType::Success);

  Utils::updateStatus(statusLabel, "Network configuration complete!", StatusType::Success);

  QMessageBox::information(this, "Success",
                           "<b>Network settings configured successfully!</b><br><br>"
                           "• WAN IP: " +
                               wanIp +
                               "<br>"
                               "• P2P Port: " +
                               p2pPortStr +
                               "<br>"
                               "• API Port: " +
                               apiPortStr + "<br><br>" + firewallMsg.simplified().remove(QRegularExpression("<[^>]*>")) +
                               "<br><br>"
                               "Go <b>Next</b> to configure account and finalize installation.");

  return true;
}