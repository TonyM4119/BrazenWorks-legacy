#include "CommandDialog.h"

#include "LengthField.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

CommandDialog::CommandDialog(const QString& title, QWidget* parent) : FloatingPanel(title.toUpper(), parent) {
  setAccentDot(true);
  setFixedWidth(290);

  auto* content = new QWidget(this);
  auto* v = new QVBoxLayout(content);
  v->setContentsMargins(6, 6, 6, 4);
  v->setSpacing(10);

  grid_ = new QGridLayout;
  grid_->setHorizontalSpacing(10);
  grid_->setVerticalSpacing(8);
  grid_->setColumnStretch(1, 1);
  v->addLayout(grid_);

  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  auto* cancel = new QPushButton(tr("Cancel"), content);
  auto* ok = new QPushButton(tr("OK"), content);
  ok->setObjectName("PrimaryButton");
  ok->setDefault(true);
  connect(cancel, &QPushButton::clicked, this, &CommandDialog::rejected);
  connect(ok, &QPushButton::clicked, this, &CommandDialog::tryAccept);
  buttons->addWidget(cancel);
  buttons->addWidget(ok);
  v->addLayout(buttons);

  setContent(content);
}

QDoubleSpinBox* CommandDialog::addNumber(const QString& label, double value, const QString& suffix,
                                         double min, double max, int decimals) {
  auto* name = new QLabel(label, this);
  name->setObjectName("DialogLabel");
  auto* field = new QDoubleSpinBox(this);
  field->setRange(min, max);
  field->setDecimals(decimals);
  field->setValue(value);
  field->setSuffix(suffix);
  field->setButtonSymbols(QAbstractSpinBox::NoButtons);
  field->setKeyboardTracking(false);
  name->setBuddy(field);
  grid_->addWidget(name, rows_, 0);
  grid_->addWidget(field, rows_, 1);
  ++rows_;
  if (!firstField_) firstField_ = field;
  return field;
}

QComboBox* CommandDialog::addChoice(const QString& label, const QStringList& options, int current) {
  auto* name = new QLabel(label, this);
  name->setObjectName("DialogLabel");
  auto* field = new QComboBox(this);
  field->setObjectName("DialogCombo");
  field->addItems(options);
  field->setCurrentIndex(current);
  name->setBuddy(field);
  grid_->addWidget(name, rows_, 0);
  grid_->addWidget(field, rows_, 1);
  ++rows_;
  if (!firstField_) firstField_ = field;
  return field;
}

LengthField* CommandDialog::addLength(const QString& label, double valueMm, model::LengthUnitKind unit,
                                     double minMm) {
  auto* name = new QLabel(label, this);
  name->setObjectName("DialogLabel");
  auto* field = new LengthField(valueMm, unit, this);
  field->setMinimumMm(minMm);
  name->setBuddy(field);
  grid_->addWidget(name, rows_, 0);
  grid_->addWidget(field, rows_, 1);
  ++rows_;
  lengths_.push_back(field);
  if (!firstField_) firstField_ = field;
  return field;
}

void CommandDialog::setUnit(model::LengthUnitKind unit) {
  for (LengthField* f : lengths_) f->setUnit(unit);
}

void CommandDialog::tryAccept() {
  // Read every length (including the one being typed); stay open on errors.
  LengthField* firstBad = nullptr;
  for (LengthField* f : lengths_) {
    if (!f->commit() && !firstBad) firstBad = f;
  }
  if (firstBad) {
    firstBad->setFocus();
    firstBad->selectAll();
    return;
  }
  emit accepted();
}

void CommandDialog::focusFirstField() {
  if (!firstField_) return;
  firstField_->setFocus();
  if (auto* spin = qobject_cast<QDoubleSpinBox*>(firstField_)) spin->selectAll();
  if (auto* edit = qobject_cast<QLineEdit*>(firstField_)) edit->selectAll();
}

void CommandDialog::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    emit rejected();
    return;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    tryAccept();
    return;
  }
  FloatingPanel::keyPressEvent(event);
}
