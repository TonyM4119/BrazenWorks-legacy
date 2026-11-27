#include "PreferencesDialog.h"

#include "Branding.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
  setObjectName("Preferences");
  setWindowTitle(tr("Preferences"));
  resize(860, 560);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(14, 14, 14, 14);
  root->setSpacing(12);

  auto* body = new QHBoxLayout;
  body->setSpacing(14);
  root->addLayout(body, 1);

  auto* left = new QVBoxLayout;
  left->setSpacing(8);
  search_ = new QLineEdit(this);
  search_->setPlaceholderText(tr("Search settings"));
  search_->setClearButtonEnabled(true);
  left->addWidget(search_);
  categories_ = new QListWidget(this);
  categories_->setObjectName("PrefsCategories");
  categories_->setFixedWidth(190);
  left->addWidget(categories_, 1);
  body->addLayout(left);

  pages_ = new QStackedWidget(this);
  body->addWidget(pages_, 1);

  const QString restart = tr("Takes effect when %1 starts again.").arg(branding::kDisplayName);

  // ---- General -----------------------------------------------------------------
  addPage(tr("General"));
  addChoice(prefs::kTheme, tr("Theme:"), {tr("Dark"), tr("Light")}, {"dark", "light"}, restart);
  addChoice(prefs::kDefaultUnit, tr("Default units for new designs:"),
            {tr("Inches (in)"), tr("Feet (ft)"), tr("Millimeters (mm)"), tr("Centimeters (cm)"), tr("Meters (m)")},
            {"in", "ft", "mm", "cm", "m"});
  addCheck(prefs::kShowVersion, tr("Show the version in the top bar"));
  addCheck(prefs::kShowUsedSketches, tr("Show sketches after a feature uses them"));

  // ---- Navigation ----------------------------------------------------------------
  addPage(tr("Navigation"));
  addChoice(prefs::kOrbitMiddle, tr("Middle mouse button:"),
            {tr("Drag pans, Shift + drag orbits (Fusion)"), tr("Drag orbits, Shift + drag pans")}, {false, true});
  addCheck(prefs::kReverseZoom, tr("Reverse the mouse wheel zoom"));
  addCheck(prefs::kAnimateCamera, tr("Animate camera moves (view cube, home, sketch)"));

  // ---- Display -------------------------------------------------------------------
  addPage(tr("Display"));
  addCheck(prefs::kAntialiasing, tr("Smooth edges (anti-aliasing)"), restart);
  addChoice(prefs::kHighQuality, tr("Curved surface quality:"), {tr("High (smoother)"), tr("Normal (faster)")},
            {true, false}, restart);
  addCheck(prefs::kShowGrid, tr("Show the grid"));

  // ---- Sketch --------------------------------------------------------------------
  addPage(tr("Sketch"));
  addCheck(prefs::kSnap, tr("Snap to points, the origin and curves"));
  addCheck(prefs::kInference, tr("Make nearly level or plumb lines horizontal/vertical"));
  addCheck(prefs::kConstraintTags, tr("Show constraint tags (H, V, Perp…)"));
  addNumber(prefs::kPickRadius, tr("Pick radius:"), 4, 24, tr(" px"));

  load([](const prefs::Key& k) { return prefs::get(k); });

  connect(categories_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
  categories_->setCurrentRow(0);
  connect(search_, &QLineEdit::textChanged, this, &PreferencesDialog::filter);

  // ---- Buttons -------------------------------------------------------------------
  auto* buttons = new QHBoxLayout;
  auto* restore = new QPushButton(tr("Restore Defaults"), this);
  auto* exportButton = new QPushButton(tr("Export Settings…"), this);
  auto* importButton = new QPushButton(tr("Import Settings…"), this);
  auto* ok = new QPushButton(tr("OK"), this);
  ok->setObjectName("PrimaryButton");
  ok->setDefault(true);
  auto* cancel = new QPushButton(tr("Cancel"), this);
  buttons->addWidget(restore);
  buttons->addWidget(exportButton);
  buttons->addWidget(importButton);
  buttons->addStretch(1);
  buttons->addWidget(ok);
  buttons->addWidget(cancel);
  root->addLayout(buttons);

  connect(restore, &QPushButton::clicked, this, [this] { load([](const prefs::Key& k) { return k.fallback; }); });
  connect(exportButton, &QPushButton::clicked, this, &PreferencesDialog::exportSettings);
  connect(importButton, &QPushButton::clicked, this, &PreferencesDialog::importSettings);
  connect(ok, &QPushButton::clicked, this, [this] {
    save();
    emit applied();
    accept();
  });
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

QFormLayout* PreferencesDialog::addPage(const QString& name) {
  auto* page = new QWidget(this);
  auto* outer = new QVBoxLayout(page);
  outer->setContentsMargins(0, 0, 0, 0);
  auto* box = new QGroupBox(name, page);
  box->setObjectName("PrefsGroup");
  form_ = new QFormLayout(box);
  form_->setContentsMargins(16, 20, 16, 16);
  form_->setHorizontalSpacing(18);
  form_->setVerticalSpacing(12);
  form_->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  outer->addWidget(box);
  outer->addStretch(1);
  pages_->addWidget(page);
  categories_->addItem(name);
  pageNames_ << name;
  return form_;
}

void PreferencesDialog::addNote(const QString& text) {
  auto* note = new QLabel(text, this);
  note->setObjectName("PrefsNote");
  note->setWordWrap(true);
  form_->addRow(QString(), note);
}

void PreferencesDialog::addChoice(const prefs::Key& key, const QString& label, const QStringList& texts,
                                  const QVariantList& values, const QString& note) {
  auto* combo = new QComboBox(this);
  combo->setObjectName("DialogCombo");
  for (int i = 0; i < texts.size(); ++i) combo->addItem(texts[i], values[i]);
  form_->addRow(label, combo);
  if (!note.isEmpty()) addNote(note);
  fields_.push_back({&key, [combo] { return combo->currentData(); },
                     [combo](const QVariant& v) {
                       for (int i = 0; i < combo->count(); ++i) {
                         if (combo->itemData(i).toString() == v.toString()) combo->setCurrentIndex(i);
                       }
                     },
                     label + " " + texts.join(' ') + " " + note, pages_->count() - 1});
}

void PreferencesDialog::addCheck(const prefs::Key& key, const QString& label, const QString& note) {
  auto* check = new QCheckBox(label, this);
  form_->addRow(check);
  if (!note.isEmpty()) addNote(note);
  fields_.push_back({&key, [check] { return check->isChecked(); },
                     [check](const QVariant& v) { check->setChecked(v.toBool()); }, label + " " + note,
                     pages_->count() - 1});
}

void PreferencesDialog::addNumber(const prefs::Key& key, const QString& label, int min, int max,
                                  const QString& suffix) {
  auto* spin = new QSpinBox(this);
  spin->setRange(min, max);
  spin->setSuffix(suffix);
  spin->setMaximumWidth(120);
  form_->addRow(label, spin);
  fields_.push_back({&key, [spin] { return spin->value(); }, [spin](const QVariant& v) { spin->setValue(v.toInt()); },
                     label, pages_->count() - 1});
}

void PreferencesDialog::load(const std::function<QVariant(const prefs::Key&)>& source) {
  for (Field& f : fields_) f.write(source(*f.key));
}

void PreferencesDialog::save() {
  for (Field& f : fields_) prefs::set(*f.key, f.read());
}

void PreferencesDialog::filter(const QString& text) {
  int first = -1;
  for (int page = 0; page < pageNames_.size(); ++page) {
    bool match = text.isEmpty() || pageNames_[page].contains(text, Qt::CaseInsensitive);
    for (const Field& f : fields_) {
      if (f.page == page && f.searchText.contains(text, Qt::CaseInsensitive)) match = true;
    }
    categories_->item(page)->setHidden(!match);
    if (match && first < 0) first = page;
  }
  if (first >= 0 && categories_->item(categories_->currentRow())->isHidden()) categories_->setCurrentRow(first);
}

void PreferencesDialog::exportSettings() {
  const QString path = QFileDialog::getSaveFileName(this, tr("Export Settings"),
                                                    QString("%1-settings.ini").arg(branding::kSlug),
                                                    tr("Settings (*.ini)"));
  if (path.isEmpty()) return;
  QSettings out(path, QSettings::IniFormat);
  out.clear();
  for (const Field& f : fields_) out.setValue(f.key->name, f.read());
  out.sync();
  if (out.status() != QSettings::NoError) QMessageBox::warning(this, windowTitle(), tr("Could not write %1.").arg(path));
}

void PreferencesDialog::importSettings() {
  const QString path = QFileDialog::getOpenFileName(this, tr("Import Settings"), {}, tr("Settings (*.ini)"));
  if (path.isEmpty()) return;
  QSettings in(path, QSettings::IniFormat);
  // Into the dialog only: nothing changes until OK.
  load([&in](const prefs::Key& k) { return in.value(k.name, prefs::get(k)); });
}
