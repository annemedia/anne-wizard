#ifndef NETUTILS_H
#define NETUTILS_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>

class QNetworkReply;


struct NetworkInfo {
    QString wanIp;
    QString localIp;
    QString gateway;
    bool isVpn = false;
    bool vpnForwardingLikely = false;
    bool isLanBehindNat = false;
    bool isCgnat = false;
    bool isTor = false;
};

class NetUtils : public QObject {
  Q_OBJECT

public:
  explicit NetUtils(QObject *parent = nullptr);
  ~NetUtils() override;

  void detectWanIp(bool detailed, NetworkInfo *out);
  QString getLocalPublicIp(bool detailed = false) const;  
  bool isUsingTor() const;
  static bool isValidPort(const QString &text, int *outPort = nullptr,
                          int min = 1, int max = 65535);
  static QString validatePortOrDefault(const QString &text, int defaultPort);
  static QString openFirewallPort(quint16 port,
                                  const QString &protocol = "tcp");

  static QNetworkRequest createSslConfiguredRequest(const QUrl &url, QNetworkAccessManager *nam = nullptr, int timeout = 6000000);
    

signals:
  void wanIpDetected(const QString &ip);
  void detectionFailed(const QString &error);
  void torCheckResult(bool isTor);

private:
  void onReplyFinished(QNetworkReply *reply, int index);
  void tryRemoteIpCheck(int index);
    static void setupNetworkAccessManager(QNetworkAccessManager *nam);
    static QSslConfiguration getForcedSslConfiguration();
    static void applyForcedSslConfiguration(QNetworkRequest &request, QNetworkAccessManager *nam = nullptr);
    
    QNetworkAccessManager *networkManager = nullptr;

  const QStringList ipServices;
};

#endif
