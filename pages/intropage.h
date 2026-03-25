#ifndef INTROPAGE_H
#define INTROPAGE_H

#include "../wizard.h"
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QLabel>
#include <QPropertyAnimation>
#include <QVBoxLayout>
#include <QThread>
#include <QWizardPage>

class IntroPage : public QWizardPage {
  Q_OBJECT

public:
  explicit IntroPage(QWidget *parent = nullptr);

private:
  double getTotalRAM() const;
  QLabel *artLabel;
  QGraphicsOpacityEffect *opacityEffect;
  QPropertyAnimation *pulseAnimation;
};

#endif