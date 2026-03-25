#include "netutils.h"
#include "utils.h"
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QTimer>
#include <QTemporaryFile>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QMessageBox>

#ifdef Q_OS_WIN
#include <netfw.h>
#include <windows.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

NetUtils::NetUtils(QObject *parent)
    : QObject(parent), networkManager(new QNetworkAccessManager(this)),
      ipServices({"https://checkip.amazonaws.com", "https://api.ipify.org", "https://myip.dnsomatic.com", "https://ifconfig.me/ip", "https://icanhazip.com"}) {
  qDebug() << "NetUtils: Instance created";
}

NetUtils::~NetUtils() = default;

QString NetUtils::getLocalPublicIp(bool detailed) const {
  qDebug() << "NetUtils: Starting local IP detection (detailed =" << detailed << ")";

  QString vpnIp;
  QString lanIp;
  QString lanGateway;
  bool isBehindNat = false;
  bool isCgnat = false;
  bool isVpn = false;

  for (const QHostAddress &addr : QNetworkInterface::allAddresses()) {
    if (addr.protocol() != QAbstractSocket::IPv4Protocol || addr.isLoopback())
      continue;

    const QString ipStr = addr.toString();
    const quint32 ip = addr.toIPv4Address();
    if ((ip >> 16) == 0xA9FE)
      continue;

    const bool isPublic = !((ip >> 24) == 10 || (ip >> 16) == 0xC0A8 || ((ip >> 16) == 0xAC10) || ((ip >> 22) == 0x190 && (ip & 0xFFC00000) == 0x64400000));

    if (isPublic) {
      lanIp = ipStr;
      isBehindNat = false;
      break;
    }

    if (ipStr.startsWith("10.") && vpnIp.isEmpty()) {
      vpnIp = ipStr;
    } else if (lanIp.isEmpty() && (ipStr.startsWith("192.168.") || ipStr.startsWith("172."))) {
      lanIp = ipStr;
    }
  }

#ifdef Q_OS_LINUX
  QProcessResult routeResult = Utils::executeProcess("ip", {"-4", "route", "show", "default"});

  if (routeResult.QPSuccess && routeResult.exitCode == 0) {
    const QString out = routeResult.stdOut;
    QRegularExpression re(R"(default via (\S+) dev (\S+))");
    QRegularExpressionMatch m = re.match(out);
    if (m.hasMatch()) {
      lanGateway = m.captured(1);
      const QString dev = m.captured(2);

      QProcessResult addrResult = Utils::executeProcess("ip", {"-4", "addr", "show", "dev", dev, "scope", "global"});
      if (addrResult.QPSuccess && addrResult.exitCode == 0) {
        QRegularExpression ipRe(R"(\binet\s+(\d+\.\d+\.\d+\.\d+))");
        QRegularExpressionMatch ipm = ipRe.match(addrResult.stdOut);
        if (ipm.hasMatch()) {
          const QString realIp = ipm.captured(1);
          if (realIp.startsWith("10.") && !realIp.startsWith("10.152.152.")) {
            vpnIp = realIp;
          } else {
            lanIp = realIp;
          }
        }
      }
    }
  }
#endif

  if (vpnIp.startsWith("10.") && !vpnIp.startsWith("10.152.152.")) {
    isVpn = true;
    isBehindNat = true;
  } else if (!lanIp.isEmpty()) {
    isBehindNat = true;
    isCgnat = lanIp.startsWith("100.64.");
  }

  const QString displayIp = lanIp.isEmpty() ? vpnIp : lanIp;
  const QString finalGateway = lanGateway.isEmpty() ? "your router" : lanGateway;

  if (!detailed) {
    return displayIp;
  }

  QJsonObject json;
  json["localIp"] = displayIp;
  json["gateway"] = finalGateway;
  json["isLanBehindNat"] = isBehindNat;
  json["isCgnat"] = isCgnat;
  json["isVpn"] = isVpn;

  qDebug() << "NetUtils: FINAL → DisplayIP:" << displayIp << "RealLAN:" << lanIp << "VPN_Tunnel:" << vpnIp << "Gateway:" << finalGateway << "VPN:" << (isVpn ? "YES" : "NO");

  return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

void NetUtils::detectWanIp(bool detailed, NetworkInfo *out) {
  qDebug() << "NetUtils: Starting WAN IP detection...";

  QString localDiagnostics = getLocalPublicIp(detailed);

  if (detailed && out) {
    QJsonDocument doc = QJsonDocument::fromJson(localDiagnostics.toUtf8());
    if (!doc.isNull()) {
      QJsonObject json = doc.object();
      out->localIp = json["localIp"].toString();
      out->gateway = json["gateway"].toString();
      out->isLanBehindNat = json["isLanBehindNat"].toBool();
      out->isCgnat = json["isCgnat"].toBool();
      out->isVpn = json["isVpn"].toBool();
    }
    out->isTor = isUsingTor();
    if (out->isTor) {
      qDebug() << "NetUtils: Tor detected!";
    } else {
      qDebug() << "NetUtils: Tor NOT detected!";
    }
  } else {
    qDebug() << "NetUtils: WTF!";
  }

  QString candidateLocalIp;
  if (!localDiagnostics.isEmpty()) {
    candidateLocalIp = getLocalPublicIp(false);
    if (candidateLocalIp.isEmpty()) {
      QJsonDocument doc = QJsonDocument::fromJson(localDiagnostics.toUtf8());
      if (!doc.isNull() && doc.isObject()) {
        candidateLocalIp = doc.object()["localIp"].toString();
      }
    }
  }

  bool localIpIsActuallyPublic = false;
  if (!candidateLocalIp.isEmpty()) {
    QHostAddress addr(candidateLocalIp);

    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
      const quint32 ip = addr.toIPv4Address();

      // Standard private ranges
      bool isStandardPrivate = (ip >> 24) == 10 ||     // 10.0.0.0/8
                               (ip >> 16) == 0xC0A8 || // 192.168.0.0/16
                               ((ip >> 12) == 0xAC1);  // 172.16.0.0/12 (172.16.0.0 - 172.31.255.255)

      // CGNAT range (100.64.0.0/10)
      bool isCgnat = ((ip >> 10) == 0x2B3F && (ip >> 24) == 100);

      // Link-local (169.254.0.0/16)
      bool isLinkLocal = (ip >> 16) == 0xA9FE;

      // Localhost (127.0.0.0/8)
      bool isLocalhost = (ip >> 24) == 127;

      // Documentation ranges
      bool isDocumentation = (ip >> 24) == 192 && (ip >> 16) == 0x5802 || // 192.0.2.0/24
                             (ip >> 24) == 198 && (ip >> 16) == 0x5120 || // 198.51.100.0/24
                             (ip >> 24) == 203 && (ip >> 16) == 0x8850;   // 203.0.113.0/24

      // Benchmarking (198.18.0.0/15)
      bool isBenchmarking = (ip >> 17) == 0x6309; // 198.18.0.0/15

      localIpIsActuallyPublic = !isStandardPrivate && !isCgnat && !isLinkLocal && !isLocalhost && !isDocumentation && !isBenchmarking;

      if ((isStandardPrivate || isCgnat || isLinkLocal || isLocalhost) && localIpIsActuallyPublic) {
        qDebug() << "NetUtils: WARNING - IP" << candidateLocalIp << "is in private/special range but marked as public!";
      }
    } else {
      // is IPv6 global?
      localIpIsActuallyPublic = addr.isGlobal();
    }
  }

  if (localIpIsActuallyPublic) {
    qDebug() << "NetUtils: Local interface has genuine public IP ->" << candidateLocalIp;
    emit wanIpDetected(candidateLocalIp);
    if (detailed && out)
      out->wanIp = candidateLocalIp;
    return;
  }

  qDebug() << "NetUtils: Local IP is not reliably public (" << candidateLocalIp << "), falling back to remote services...";

  tryRemoteIpCheck(0);

  if (detailed && out) {
    QEventLoop loop;
    QTimer::singleShot(12000, &loop, &QEventLoop::quit);
    connect(this, &NetUtils::wanIpDetected, [&loop, out](const QString &wan) {
      out->wanIp = wan;
      loop.quit();
    });
    loop.exec();

    bool wanPublic = !out->wanIp.isEmpty() && QHostAddress(out->wanIp).isGlobal();
    out->vpnForwardingLikely = out->isVpn && wanPublic;

    if (!out->isCgnat && out->localIp.startsWith("100.64.")) {
      out->isCgnat = out->isLanBehindNat;
    }
  }
}

void NetUtils::tryRemoteIpCheck(int index) {
  if (index >= ipServices.size()) {
    qDebug() << "NetUtils: All remote services exhausted, no valid public IP found";
    emit wanIpDetected(QString());
    return;
  }

  const QString url = ipServices.at(index);
  qDebug() << QString("NetUtils: Trying remote service [%1/%2]: %3").arg(index + 1).arg(ipServices.size()).arg(url);

  QNetworkRequest req{QUrl(url)};

  req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
                                 "like Gecko) Chrome/131.0.0.0 Safari/537.36");
  req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/"
                             "xml;q=0.9,image/avif,image/webp,*/*;q=0.8");
  req.setRawHeader("Accept-Language", "en-US,en;q=0.5");
  req.setRawHeader("Accept-Encoding", "gzip, deflate, br");
  req.setRawHeader("Connection", "close");
  req.setRawHeader("Upgrade-Insecure-Requests", "1");

  req.setTransferTimeout(20000);

  qDebug() << "NetUtils: >>> Sending request to" << url;
  for (const QByteArray &headerName : req.rawHeaderList()) {
    qDebug() << "    " << headerName.constData() << ":" << req.rawHeader(headerName).constData();
  }
  qDebug() << "";

  QNetworkReply *reply = networkManager->get(req);

  QTimer::singleShot(10500, reply, [reply]() {
    if (!reply->isFinished()) {
      qWarning() << "NetUtils: Reply timed out (manual abort)" << reply->url().toString();
      reply->abort();
    }
  });

  connect(reply, &QNetworkReply::finished, this, [this, reply, index, url]() {
    qDebug() << "NetUtils: Reply finished for" << url;
    onReplyFinished(reply, index);
  });
}

void NetUtils::onReplyFinished(QNetworkReply *reply, int index) {
  const QUrl url = reply->url();
  const QString serviceName = url.toString();
  const QNetworkReply::NetworkError err = reply->error();

  if (err != QNetworkReply::NoError) {
    qWarning() << QString("NetUtils: Service failed [%1] - Error: %2 (%3)").arg(serviceName).arg(err).arg(reply->errorString());

    reply->deleteLater();
    tryRemoteIpCheck(index + 1);
    return;
  }

  const QByteArray dataRaw = reply->readAll();
  const QString candidateRaw = QString::fromUtf8(dataRaw).trimmed();
  const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

  qDebug() << QString("NetUtils: Received from %1 | HTTP %2 | %3 bytes").arg(serviceName).arg(httpStatus).arg(dataRaw.size());

  if (dataRaw.size() < 80 && (dataRaw.contains(char(0x00)) || (dataRaw.size() >= 2 && dataRaw.startsWith(QByteArrayLiteral("\x1f\x8b"))))) {
    qWarning() << "NetUtils: Received compressed/binary garbage instead of "
                  "plain IP, treating as failure";
    reply->deleteLater();
    tryRemoteIpCheck(index + 1);
    return;
  }

  const QString candidate = candidateRaw;

  if (candidate.contains("<") || candidate.size() > 80) {
    qWarning() << "NetUtils: Response looks like HTML or garbage:" << candidate.left(200);
    reply->deleteLater();
    tryRemoteIpCheck(index + 1);
    return;
  }

  QHostAddress addr(candidate);
  if (candidate.isEmpty() || addr.isNull()) {
    qWarning() << "NetUtils: Invalid response:" << candidate;
    reply->deleteLater();
    tryRemoteIpCheck(index + 1);
    return;
  }

  if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
    qDebug() << "NetUtils: Valid public IPv4 received ->" << candidate;
    reply->deleteLater();
    emit wanIpDetected(candidate);
    return;
  }

  if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
    if (index == 0 && ipServices.size() > 1) {
      qDebug() << "NetUtils: Got IPv6 from first service, continuing search "
                  "for IPv4...";
    } else {
      qDebug() << "NetUtils: No IPv4 found, accepting IPv6 fallback ->" << candidate;
      reply->deleteLater();
      emit wanIpDetected(candidate);
      return;
    }
  }

  reply->deleteLater();
  tryRemoteIpCheck(index + 1);
}

bool NetUtils::isValidPort(const QString &text, int *outPort, int min, int max) {
  bool ok;
  int port = text.toInt(&ok);
  if (!ok || port < min || port > max) {
    qDebug() << "NetUtils: Invalid port value:" << text;
    return false;
  }
  if (outPort)
    *outPort = port;
  return true;
}

QString NetUtils::validatePortOrDefault(const QString &text, int defaultPort) {
  int port;
  if (isValidPort(text, &port)) {
    return QString::number(port);
  } else {
    qDebug() << "NetUtils: Using default port" << defaultPort << "instead of" << text;
    return QString::number(defaultPort);
  }
}

QString NetUtils::openFirewallPort(quint16 port, const QString &protocol) {
  QString proto = protocol.toLower();
  if (proto != "tcp" && proto != "udp")
    proto = "tcp";

  const QString portStr = QString::number(port);

  bool success = false;

#ifdef Q_OS_WIN
  QProcessResult procResult = Utils::executeProcess("netsh", {"advfirewall", "firewall", "add", "rule", "name=AnneNode P2P Port " + portStr, "dir=in", "action=allow",
                                                              "protocol=" + proto.toUpper(), "localport=" + portStr, "profile=any", "enable=yes"});

  if (procResult.QPSuccess && procResult.exitCode == 0) {
    success = true;
  } else {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    INetFwPolicy2 *pPolicy = nullptr;
    INetFwRules *pRules = nullptr;
    INetFwRule *pRule = nullptr;

    HRESULT hr = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwPolicy2), (void **)&pPolicy);

    if (SUCCEEDED(hr)) {
      hr = pPolicy->get_Rules(&pRules);
    }

    if (SUCCEEDED(hr)) {
      hr = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwRule), (void **)&pRule);
    }

    if (SUCCEEDED(hr)) {
      BSTR bstrName = SysAllocString(L"AnneNode P2P Port");
      BSTR bstrDesc = SysAllocString(L"Allow incoming AnneNode P2P traffic");
      BSTR bstrPort = SysAllocString(reinterpret_cast<const wchar_t *>(portStr.utf16()));

      pRule->put_Name(bstrName);
      pRule->put_Description(bstrDesc);
      pRule->put_Protocol(proto == "tcp" ? NET_FW_IP_PROTOCOL_TCP : NET_FW_IP_PROTOCOL_UDP);
      pRule->put_LocalPorts(bstrPort);
      pRule->put_Direction(NET_FW_RULE_DIR_IN);
      pRule->put_Action(NET_FW_ACTION_ALLOW);
      pRule->put_Enabled(VARIANT_TRUE);
      pRule->put_Profiles(NET_FW_PROFILE2_ALL);

      hr = pRules->Add(pRule);

      success = SUCCEEDED(hr);

      SysFreeString(bstrName);
      SysFreeString(bstrDesc);
      SysFreeString(bstrPort);
    }

    if (pRule)
      pRule->Release();
    if (pRules)
      pRules->Release();
    if (pPolicy)
      pPolicy->Release();

    CoUninitialize();
  }

// Python helper script, not a bright idea (working, but python is not installed by default by on macs).
//   #elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)

//     const QString anchorName = "com.apple/250.AnneNode";

//     QProcess checkFirewall;
//     checkFirewall.start("/usr/libexec/ApplicationFirewall/socketfilterfw", {"--getglobalstate"});
//     checkFirewall.waitForFinished(5000);
//     QByteArray firewallOutput = checkFirewall.readAllStandardOutput();
//     bool firewallEnabled = firewallOutput.contains("enabled") || firewallOutput.contains("State = 1");

//     if (!firewallEnabled) {
//         success = true;
//     } else {
//         QTemporaryFile tempPythonFile;
//         if (tempPythonFile.open()) {
//             QString pythonScript = QString(R"PYTHON(
// #!/usr/bin/env python3
// import subprocess
// import sys
// import tempfile
// import os

// PORT = %1
// PROTO = "%2"
// ANCHOR_NAME = "%3"
// ANCHOR_FILE = "/etc/pf.anchors/com.apple.250.AnneNode"

// def main():
//     # Create ONE SIMPLE shell script
//     shell_script = f'''#!/bin/bash
// PORT={PORT}
// PROTO="{PROTO}"
// ANCHOR_NAME="{ANCHOR_NAME}"
// ANCHOR_FILE="{ANCHOR_FILE}"

// # ===== CRITICAL: ALLOW JAVA THROUGH APPLICATION FIREWALL =====
// # This is REQUIRED for WAN connectivity
// JAVA_PATH="/usr/bin/java"
// if [ -e "$JAVA_PATH" ]; then
//     echo "Configuring Application Firewall for Java runtime..."
//     /usr/libexec/ApplicationFirewall/socketfilterfw --add "$JAVA_PATH" --setblockall off 2>/dev/null || true
//     echo "✓ Java runtime added to Application Firewall"
// else
//     echo "⚠ Warning: Java not found at $JAVA_PATH"
// fi

// # Function to add anchor if not exists
// add_anchor_if_needed() {{
//     if ! grep -q "anchor.*$ANCHOR_NAME" /etc/pf.conf; then
//         # Append anchor to end of pf.conf (simplest approach)
//         echo '' >> /etc/pf.conf
//         echo 'anchor "'"$ANCHOR_NAME"'"' >> /etc/pf.conf
//         echo 'load anchor "'"$ANCHOR_NAME"'" from "'"$ANCHOR_FILE"'"' >> /etc/pf.conf
        
//         # Reload pf.conf
//         pfctl -f /etc/pf.conf 2>/dev/null || true
//     fi
// }}

// # Create anchor directory and file
// mkdir -p /etc/pf.anchors 2>/dev/null
// echo "# Anchor for P2P" > "$ANCHOR_FILE"

// # Add anchor to pf.conf
// add_anchor_if_needed

// # Add the port rule
// RULE="pass in quick proto $PROTO from any to any port $PORT keep state"
// echo "$RULE" | pfctl -a "$ANCHOR_NAME" -f - 2>&1

// if [ $? -eq 0 ]; then
//     echo "SUCCESS: Port $PORT/$PROTO opened"
//     exit 0
// else
//     echo "ERROR: Failed to add rule"
//     exit 1
// fi
// '''

//     # Write and execute
//     with tempfile.NamedTemporaryFile(mode='w', suffix='.sh', delete=False) as f:
//         f.write(shell_script)
//         temp_path = f.name
    
//     os.chmod(temp_path, 0o755)
    
//     # Execute with one authentication
//     escaped_path = temp_path.replace('"', r'\"')
//     applescript = f'do shell script "{escaped_path}" with administrator privileges'
    
//     result = subprocess.run(['osascript', '-e', applescript], 
//                           capture_output=True, text=True)
    
//     os.unlink(temp_path)
    
//     # Return result
//     if result.returncode == 0:
//         print("SUCCESS: Firewall configured")
//         sys.exit(0)
//     else:
//         print(f"ERROR: {result.stderr}")
//         sys.exit(1)

// if __name__ == "__main__":
//     main()
// )PYTHON").arg(portStr, proto, anchorName);
            
//             tempPythonFile.write(pythonScript.toUtf8());
//             tempPythonFile.close();
            
//             QFile::setPermissions(tempPythonFile.fileName(),
//                 QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            
//             QMessageBox infoBox;
//             infoBox.setWindowTitle(tr("Firewall Configuration Required"));
//             infoBox.setText(tr("<html><b>Firewall Configuration Required</b><br><br>"
//                              "The macOS firewall is currently active.<br>"
//                              "To allow your application to receive incoming connections on port <b>%1</b>,<br>"
//                              "administrator privileges are needed to modify firewall rules.<br><br>"
//                              "Click OK to proceed. You will be prompted for your password<br>"
//                              "in the standard macOS authentication dialog.</html>").arg(portStr));
//             infoBox.setIcon(QMessageBox::Information);
//             infoBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            
//             if (infoBox.exec() != QMessageBox::Ok) {
//                 return QString("<font color='orange'>✗ Configuration cancelled by user.</font>");
//             }
            
//             QProcess scriptProcess;
//             scriptProcess.start("python3", QStringList() << tempPythonFile.fileName());
//             scriptProcess.waitForFinished(30000);
            
//             success = (scriptProcess.exitCode() == 0);
            
//             if (!success) {
//                 QString errorOutput = QString::fromUtf8(scriptProcess.readAllStandardError());
//                 QString stdOutput = QString::fromUtf8(scriptProcess.readAllStandardOutput());
//                 qDebug() << "Python script output:" << stdOutput;
//                 qDebug() << "Python script error:" << errorOutput;
//             }
//         }
//     }
    
//     if (success) {
//         return QString("<font color='green'>✓ P2P port %1 opened in firewall</font>").arg(port);
//     }

#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)

    const QString anchorName = "com.apple.250.AnneNode";
    const QString anchorFile = "/etc/pf.anchors/" + anchorName;
    
    QMessageBox infoBox;
    infoBox.setWindowTitle(tr("Firewall Configuration Required"));
    infoBox.setText(tr("<html><b>Firewall Configuration Required</b><br><br>"
                     "To allow incoming connections on port <b>%1</b>,<br>"
                     "administrator privileges are needed.<br><br>"
                     "Click OK to proceed. You will be prompted for your password.</html>").arg(portStr));
    infoBox.setIcon(QMessageBox::Information);
    infoBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    
    if (infoBox.exec() != QMessageBox::Ok) {
        return QString("<font color='orange'>✗ Configuration cancelled by user.</font>");
    }
    
    QString scriptPath = "/tmp/annewizard_firewall.sh";
    QString scriptContent = QString(
        "#!/bin/bash\n"
        "set -e\n"
        "\n"
        "# Add Java to Application Firewall\n"
        "/usr/libexec/ApplicationFirewall/socketfilterfw --add /usr/bin/java --setblockall off\n"
        "\n"
        "# Create anchor file\n"
        "mkdir -p /etc/pf.anchors\n"
        "echo 'pass in quick proto %2 from any to any port %1 keep state' > %4\n"
        "\n"
        "# Load rule into running PF\n"
        "pfctl -a %3 -f %4\n"
        "\n"
        "# Add to pf.conf if not already there\n"
        "if ! grep -q 'anchor \"%3\"' /etc/pf.conf; then\n"
        "    echo 'anchor \"%3\"' >> /etc/pf.conf\n"
        "    echo 'load anchor \"%3\" from \"%4\"' >> /etc/pf.conf\n"
        "fi\n"
        "\n"
        "# Reload PF completely\n"
        "pfctl -d\n"
        "pfctl -e -f /etc/pf.conf\n"
        "\n"
        "echo 'DONE'\n"
    ).arg(portStr, proto, anchorName, anchorFile);
    
    QFile scriptFile(scriptPath);
    if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        scriptFile.write(scriptContent.toUtf8());
        scriptFile.close();
        
        QFile::setPermissions(scriptPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        
        QString appleScript = QString("do shell script \"%1\" with administrator privileges").arg(scriptPath);
        QProcess p;
        p.start("osascript", QStringList() << "-e" << appleScript);
        p.waitForFinished(60000);
        
        QString output = QString::fromUtf8(p.readAllStandardOutput());
        QString error = QString::fromUtf8(p.readAllStandardError());
        
        qDebug() << "Exit code:" << p.exitCode();
        qDebug() << "Output:" << output;
        qDebug() << "Error:" << error;
        
        QFile::remove(scriptPath);
        
        if (p.exitCode() == 0) {
            return QString("<font color='green'>✓ P2P port %1 opened in firewall</font>").arg(port);
        } else {
            return QString("<font color='orange'>⚠ Failed: %1</font>").arg(error);
        }
    } else {
        return QString("<font color='orange'>⚠ Cannot create script</font>");
    }

#elif defined(Q_OS_LINUX)
  if (QFile::exists("/etc/csf/csf.conf") || QFile::exists("/usr/sbin/csf")) {
    QProcessResult csfResult = Utils::executeProcess("csf", {"-a", QString("tcp|in|d=%1").arg(port)});

    QFile allow("/etc/csf/csf.allow");
    if (allow.open(QIODevice::ReadWrite | QIODevice::Text)) {
      QString content = QString::fromUtf8(allow.readAll());
      const QString rule = QString("tcp|in|d=%1|c=AnneNode P2P Port\n").arg(port);
      if (!content.contains(rule)) {
        allow.seek(allow.size());
        allow.write(rule.toUtf8());
      }
      allow.close();
    }

    QProcessResult reloadResult = Utils::executeProcess("csf", {"-r"});
    success = reloadResult.QPSuccess && reloadResult.exitCode == 0;
  }
  if (!success) {
    QProcessResult ufwResult = Utils::executeProcess("ufw", {"allow", portStr + "/" + proto, "comment", "AnneNode P2P"});
    success = ufwResult.QPSuccess && ufwResult.exitCode == 0;
  }

  if (!success) {
    QProcessResult firewallCmdResult = Utils::executeProcess("firewall-cmd", {"--permanent", "--add-port=" + portStr + "/" + proto});

    if (firewallCmdResult.QPSuccess && firewallCmdResult.exitCode == 0) {
      QProcessResult reloadResult = Utils::executeProcess("firewall-cmd", {"--reload"});
      success = reloadResult.QPSuccess && reloadResult.exitCode == 0;
    }
  }

  if (!success) {
    QProcessResult iptablesResult = Utils::executeProcess("iptables", {"-I", "INPUT", "1", "-p", proto, "--dport", portStr, "-j", "ACCEPT"});

    if (iptablesResult.QPSuccess && iptablesResult.exitCode == 0) {
      QProcess saveProc;
      saveProc.start("iptables-save");
      QProcess writeProc;
      writeProc.start("sh", {"-c", "iptables-save > /etc/iptables/rules.v4"});
      writeProc.waitForFinished(5000);
      success = writeProc.exitCode() == 0;
    }
  }
  if (!success) {
    QProcessResult nftResult = Utils::executeProcess("nft", {"add", "rule", "inet", "filter", "input", proto, "dport", portStr, "accept"});
    success = nftResult.QPSuccess && nftResult.exitCode == 0;
  }
#endif

  if (success) {
    return QString("<font color='green'>✓ P2P port %1 opened in firewall</font>").arg(port);
  }

  bool anyRealFirewallInstalled = false;

#ifdef Q_OS_WIN
  anyRealFirewallInstalled = true;
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  anyRealFirewallInstalled = true;
#else
  const QStringList realFirewallPaths = {"/usr/sbin/csf", "/usr/sbin/ufw", "/usr/bin/firewall-cmd", "/usr/sbin/shorewall"};
  for (const QString &path : realFirewallPaths) {
    if (QFileInfo::exists(path) && QFileInfo(path).isExecutable()) {
      anyRealFirewallInstalled = true;
      break;
    }
  }
#endif

  if (!anyRealFirewallInstalled) {
    return QString("<font color='gray'>• No firewall software detected – "
                   "nothing to configure</font>");
  }

  return QString("<font color='orange'>⚠ Unable to open P2P port %1 in "
                 "detected firewall</font><br>"
                 "<font color='gray'>   (Open TCP port %1 manually if "
                 "needed)</font>")
      .arg(port);
}

bool NetUtils::isUsingTor() const {
  bool isTor = false;

  QNetworkRequest req(QUrl("https://check.torproject.org/api/ip"));
  req.setTransferTimeout(20000);

  QNetworkReply *reply = networkManager->get(req);
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  timeout.start(9000);

  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

  loop.exec();

  if (reply->error() == QNetworkReply::NoError) {
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.isObject()) {
      isTor = doc.object().value("IsTor").toBool(false);
    }
  }

  reply->deleteLater();
  return isTor;
}

QSslConfiguration NetUtils::getForcedSslConfiguration() {
  QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
  sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
  return sslConfig;
}

void NetUtils::applyForcedSslConfiguration(QNetworkRequest &request, QNetworkAccessManager *nam) {
  QSslConfiguration sslConfig = getForcedSslConfiguration();
  request.setSslConfiguration(sslConfig);

  request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
  request.setAttribute(QNetworkRequest::Http2DirectAttribute, false);
  request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36");
  request.setRawHeader("Accept", "*/*");
}

QNetworkRequest NetUtils::createSslConfiguredRequest(const QUrl &url, QNetworkAccessManager *nam, int timeout) {
  QNetworkRequest request(url);
  request.setTransferTimeout(timeout);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  applyForcedSslConfiguration(request, nam);
  return request;
}