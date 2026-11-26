#pragma once

#include "model_core/src/ffi.rs.h"

#include <QLineEdit>

// A length input. Shows its value in the document unit ("1.25 in") and reads
// whatever the user types: "2", "1 1/4", "3/8\"", "1' 6\"", "25mm", "2in + 5mm".
// Bare numbers are in the document unit. The value is kept in millimetres.
class LengthField : public QLineEdit {
  Q_OBJECT
 public:
  explicit LengthField(double valueMm, model::LengthUnitKind unit, QWidget* parent = nullptr);

  double valueMm() const { return mm_; }
  void setValueMm(double mm);

  void setUnit(model::LengthUnitKind unit);
  void setMinimumMm(double mm) { min_ = mm; }

  // Reads the current text. Returns false (and marks the field) if it can't.
  bool commit();
  bool isValid() const { return valid_; }

 signals:
  void valueChanged(double mm);

 private:
  void showValue();
  void markInvalid(const QString& reason);

  double mm_ = 0.0;
  double min_ = -1e12;
  model::LengthUnitKind unit_;
  bool valid_ = true;
};
