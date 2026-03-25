#ifndef WELCOMEPAGE_H
#define WELCOMEPAGE_H
#include "../wizard.h"
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QWizardPage>

class LicensePage : public QWizardPage {
  Q_OBJECT

public:
  explicit LicensePage(QWidget *parent = nullptr);

private:
  QLabel *logoLabel;
  QGraphicsDropShadowEffect *glowEffect;
  QPropertyAnimation *pulseAnimation;
};

#endif
