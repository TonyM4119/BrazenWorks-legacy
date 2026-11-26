#pragma once

#include <QFrame>
#include <QVector>

class QHBoxLayout;
class QLabel;

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

 signals:
  void markerRequested(int marker);   // features [0, marker) active
  void stepRequested(int delta);

 private:
  QHBoxLayout* chips_ = nullptr;
  QLabel* status_ = nullptr;
  QString permanentStatus_;
  int count_ = 0;
};
