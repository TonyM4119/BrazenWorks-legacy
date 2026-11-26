#pragma once

#include "FloatingPanel.h"

class QComboBox;
class QDoubleSpinBox;
class QGridLayout;

// The floating dialog a command opens (EXTRUDE, FILLET, BOX…): labelled
// inputs in a grid and OK / Cancel. Enter accepts, Esc cancels.
class CommandDialog : public FloatingPanel {
  Q_OBJECT
 public:
  explicit CommandDialog(const QString& title, QWidget* parent = nullptr);

  QDoubleSpinBox* addNumber(const QString& label, double value, const QString& suffix = QStringLiteral(" mm"),
                            double min = -1e6, double max = 1e6, int decimals = 2);
  QComboBox* addChoice(const QString& label, const QStringList& options, int current = 0);

  void focusFirstField();

 signals:
  void accepted();
  void rejected();

 protected:
  void keyPressEvent(QKeyEvent* event) override;

 private:
  QGridLayout* grid_ = nullptr;
  QWidget* firstField_ = nullptr;
  int rows_ = 0;
};
