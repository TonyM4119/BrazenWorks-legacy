#pragma once

#include "SketchEditor.h"

#include <QHash>
#include <QWidget>

class QCheckBox;
class QLabel;
class QToolButton;

// The panel on the right while a sketch is open (as in MitCAD and Fusion):
// degrees of freedom, display settings, the create and constraint tools, the
// running tool's hint, and Finish / Cancel.
class SketchPalette : public QWidget {
  Q_OBJECT
 public:
  SketchPalette(SketchEditor* editor, QWidget* parent = nullptr);

 signals:
  void toolRequested(const QString& id);  // same ids as the SKETCH ribbon tab
  void lookAtRequested();
  void finishRequested();
  void cancelRequested();

 private:
  void updateSolve(int dof, bool conflicting);
  void updateMode();
  QToolButton* toolButton(const QString& id, const QString& icon, const QString& tip);

  SketchEditor* editor_;
  QLabel* dof_ = nullptr;
  QLabel* toolTitle_ = nullptr;
  QLabel* toolHint_ = nullptr;
  QCheckBox* construction_ = nullptr;
  QHash<QString, QToolButton*> tools_;
};
