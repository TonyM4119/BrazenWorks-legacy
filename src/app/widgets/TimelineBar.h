#pragma once

#include <QFrame>
#include <QTimer>
#include <QVector>

class QHBoxLayout;
class QLabel;
class QToolButton;

// The parametric history along the bottom: playback buttons, one chip per
// feature, and the brass rollback marker after the last active feature.
class TimelineBar : public QFrame {
  Q_OBJECT
 public:
  struct Chip {
    QString name;
    QString type;   // "Extrude", "Fillet", "Sketch"…
    QString icon;   // icon name in resources/icons
    bool failed = false;
  };

  explicit TimelineBar(QWidget* parent = nullptr);

  void setFeatures(const QVector<Chip>& chips, int marker);
  void showStatus(const QString& text, int timeoutMs = 8000);
  void setPermanentStatus(const QString& text);
  // The document-unit button at the right end ("in ▾"); MainWindow fills its menu.
  QToolButton* unitsButton() const { return units_; }

 signals:
  void markerRequested(int marker);   // features [0, marker) active
  void stepRequested(int delta);
  void featureDoubleClicked(int index);  // e.g. edit a sketch


 private:
  QHBoxLayout* chips_ = nullptr;
  QLabel* status_ = nullptr;
  QToolButton* units_ = nullptr;
  QString permanentStatus_;
  QString tempStatus_;
  int count_ = 0;
  QTimer pendingClick_;
  int pendingChip_ = -1;
};
