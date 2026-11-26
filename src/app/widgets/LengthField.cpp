#include "LengthField.h"

#include <QStyle>

namespace {

QString toQString(const rust::String& s) { return QString::fromUtf8(s.data(), int(s.size())); }

}  // namespace

LengthField::LengthField(double valueMm, model::LengthUnitKind unit, QWidget* parent)
    : QLineEdit(parent), mm_(valueMm), unit_(unit) {
  setObjectName("LengthField");
  showValue();
  connect(this, &QLineEdit::editingFinished, this, [this] { commit(); });
  connect(this, &QLineEdit::textEdited, this, [this] {
    if (!valid_) {  // clear the error as soon as the user starts fixing it
      valid_ = true;
      setProperty("invalid", false);
      setToolTip(QString());
      style()->unpolish(this);
      style()->polish(this);
    }
  });
}

void LengthField::setValueMm(double mm) {
  mm_ = mm;
  valid_ = true;
  showValue();
}

void LengthField::setUnit(model::LengthUnitKind unit) {
  if (!valid_ || isModified()) commit();
  unit_ = unit;
  if (valid_) showValue();
}

bool LengthField::commit() {
  const std::string text = this->text().toStdString();
  const model::LengthParse parsed = model::length_parse(text, unit_);
  if (!parsed.ok) {
    markInvalid(toQString(parsed.error));
    return false;
  }
  if (parsed.mm < min_) {
    markInvalid(tr("Must be at least %1").arg(toQString(model::length_format(min_, unit_))));
    return false;
  }
  const bool changed = parsed.mm != mm_;
  mm_ = parsed.mm;
  valid_ = true;
  showValue();
  if (changed) emit valueChanged(mm_);
  return true;
}

void LengthField::showValue() {
  setText(toQString(model::length_format(mm_, unit_)));
  setModified(false);
  setProperty("invalid", false);
  setToolTip(tr("Type a length: 2, 1 1/4, 3/8\", 1' 6\", 25mm, 2in + 5mm"));
  style()->unpolish(this);
  style()->polish(this);
}

void LengthField::markInvalid(const QString& reason) {
  valid_ = false;
  setProperty("invalid", true);
  setToolTip(reason);
  style()->unpolish(this);
  style()->polish(this);
}
