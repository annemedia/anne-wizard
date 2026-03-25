#ifndef NETCONFIGPAGE_H
#define NETCONFIGPAGE_H
#include "../wizard.h"
#include "../utils/netutils.h"
#include <QWizardPage>
#include <QTextEdit>
#include <QScrollArea>

class NetUtils;
class QLabel;
class QLineEdit;
class QGroupBox;

class NetConfigPage : public QWizardPage {
  Q_OBJECT

public:
  explicit NetConfigPage(QWidget *parent = nullptr);
  void initializePage() override;
  bool isComplete() const override;
  bool validatePage() override;

private slots:
  void validateAll();
  void updateNetworkInfoDisplay(const NetworkInfo &info);

private:
  NetUtils *netUtils;
  QLabel *statusLabel;
  QLabel *networkStatusLabel;
  QTextEdit *infoText;
  
  QGroupBox *networkGroup;
  QLabel *ipLabel;
  QLineEdit *ipEdit;
  
  QLineEdit *p2pPortEdit;
  QLabel *p2pPortStatus;
  QLineEdit *apiPortEdit;
  QLabel *apiPortStatus;
};

#endif
