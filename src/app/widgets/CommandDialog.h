#pragma once

#include "FloatingPanel.h"
#include "model_core/src/ffi.rs.h"

#include <QVector>

class LengthField;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;

// The floating dialog a command opens (EXTRUDE, FILLET, BOX…): labelled
// inputs in a grid and OK / Cancel. Enter accepts, Esc cancels.
class CommandDialog : public FloatingPanel {
  Q_OBJECT
 public:
  explicit CommandDialog(const QString& title, QWidget* parent = nullptr);

  QDoubleSpinBox* addNumber(const QString& label, double value, const QString& suffix = QStringLiteral(" mm"),
                            double min = -1e6, double max = 1e6, int decimals = 2);
  // A read-only line ("Profiles: 2 selected").
  QLabel* addInfo(const QString& label, const QString& text);
  QComboBox* addChoice(const QString& label, const QStringList& options, int current = 0);
  // A length shown in the document unit; `minMm` rejects smaller values.
  LengthField* addLength(const QString& label, double valueMm, model::LengthUnitKind unit, double minMm = -1e12);

  // Shows every length field in a new unit (when the document unit changes).
  void setUnit(model::LengthUnitKind unit);

  void focusFirstField();

 signals:
  void accepted();
  void rejected();

 protected:
  void keyPressEvent(QKeyEvent* event) override;

 private:
  void tryAccept();

  QGridLayout* grid_ = nullptr;
  QVector<LengthField*> lengths_;
  QWidget* firstField_ = nullptr;
  int rows_ = 0;
};
