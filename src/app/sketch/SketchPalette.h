#pragma once

#include "SketchEditor.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QToolButton;
class QVBoxLayout;

// The panel on the right while a sketch is open, laid out as Fusion's:
// Status (is the sketch fully constrained), Feature Options (the running
// tool and how to use it), Options (label on the left, control on the
// right), and Finish Sketch. The tools themselves live in the toolbar.
class SketchPalette : public QWidget {
  Q_OBJECT
 public:
  SketchPalette(SketchEditor* editor, QWidget* parent = nullptr);

 signals:
  void toolRequested(const QString& id);
  void lookAtRequested();
  void finishRequested();
  void cancelRequested();
  void gridToggled(bool on);
  void contentChanged();  // a section opened or closed: the panel refits

 private:
  void updateSolve(int dof, bool conflicting);
  void updateMode();
  // A heading with a triangle that folds its section away (remembered).
  QVBoxLayout* section(QVBoxLayout* col, const QString& key, QLabel** title = nullptr);
  void row(QVBoxLayout* box, const QString& label, QWidget* control, const QString& tip = {});

  SketchEditor* editor_;
  QLabel* status_ = nullptr;
  QLabel* toolTitle_ = nullptr;
  QLabel* toolHint_ = nullptr;
  QWidget* featureSection_ = nullptr;
  QWidget* featureHead_ = nullptr;
  QToolButton* normalLine_ = nullptr;
  QToolButton* constructionLine_ = nullptr;
};
