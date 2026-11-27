#pragma once

#include "Settings.h"

#include <QDialog>

#include <functional>
#include <vector>

class QLineEdit;
class QListWidget;
class QStackedWidget;
class QFormLayout;

// Preferences: categories on the left (with search), one page each on the
// right, Restore Defaults / Export / Import, OK / Cancel. Nothing is saved
// until OK.
class PreferencesDialog : public QDialog {
  Q_OBJECT
 public:
  explicit PreferencesDialog(QWidget* parent = nullptr);

 signals:
  void applied();  // settings were saved; re-read them

 private:
  struct Field {
    const prefs::Key* key;
    std::function<QVariant()> read;            // widget → value
    std::function<void(const QVariant&)> write;  // value → widget
    QString searchText;
    int page;
  };

  QFormLayout* addPage(const QString& name);
  void addChoice(const prefs::Key& key, const QString& label, const QStringList& texts, const QVariantList& values,
                 const QString& note = {});
  void addCheck(const prefs::Key& key, const QString& label, const QString& note = {});
  void addNumber(const prefs::Key& key, const QString& label, int min, int max, const QString& suffix);
  void addNote(const QString& text);

  void load(const std::function<QVariant(const prefs::Key&)>& source);
  void save();
  void filter(const QString& text);
  void exportSettings();
  void importSettings();

  QLineEdit* search_ = nullptr;
  QListWidget* categories_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  QStringList pageNames_;
  QFormLayout* form_ = nullptr;  // page being built
  std::vector<Field> fields_;
};
