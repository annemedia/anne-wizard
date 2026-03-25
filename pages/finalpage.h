#ifndef FINALPAGE_H
#define FINALPAGE_H
#include "../wizard.h"
#include <QWizardPage>

class QCheckBox;
class QLabel;
class QGroupBox;

class FinalPage : public QWizardPage {
    Q_OBJECT
    
public:
    FinalPage(QWidget *parent = nullptr);
    void initializePage() override;
    bool validatePage() override;
    bool isComplete() const override;
    
private:
    Wizard *m_wiz;
    QCheckBox *cbDesktopShortcut;
    QCheckBox *cbMenuEntry;
    QCheckBox *cbLaunchOnBoot;
    QCheckBox *cbHeadlessMode;
    QCheckBox *cbLaunchNow;
    QLabel *finalInfo;
};

#endif