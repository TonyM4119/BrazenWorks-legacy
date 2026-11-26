#pragma once

#include "Viewport.h"
#include "model_core/src/ffi.rs.h"  // Rust timeline API (generated)

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <functional>
#include <vector>

class QLineEdit;

// Edits one sketch of the timeline in the 3D view: drawing tools, snapping,
// constraints, dimensions, selection and dragging. Every change is a
// transaction: the host gets a snapshot for undo, or the snapshot back if the
// change failed half-way.
class SketchEditor : public QObject, public ViewportTool {
  Q_OBJECT
 public:
  enum class Mode { Select, Line, Rectangle, Circle, Arc, Point, Dimension, Constrain };

  struct Host {
    std::function<model::Timeline&()> timeline;
    std::function<void(rust::Box<model::Timeline>)> pushUndo;  // state before a change
    std::function<void(rust::Box<model::Timeline>)> restore;   // undo a failed change
  };

  SketchEditor(Viewport* view, Host host, std::uint64_t sketch, QObject* parent = nullptr);
  ~SketchEditor() override;

  std::uint64_t sketch() const { return sketch_; }
  Mode mode() const { return mode_; }
  model::ConstraintKind constraintKind() const { return constraintKind_; }

  void setMode(Mode mode);
  void setConstraint(model::ConstraintKind kind);  // enters Constrain mode
  void toggleConstruction();
  void deleteSelection();
  // Re-reads the sketch (after undo/redo or a unit change) and redraws.
  void reload();
  void clearDisplay();
  bool fullyConstrained() const { return dof_ == 0; }

  // ViewportTool
  bool mousePress(QMouseEvent* e) override;
  bool mouseMove(QMouseEvent* e) override;
  bool mouseRelease(QMouseEvent* e) override;
  bool mouseDoubleClick(QMouseEvent* e) override;
  bool keyPress(QKeyEvent* e) override;
  void viewChanged() override;

 signals:
  void edited();
  void message(const QString& text);
  void solveStateChanged(int dof, bool conflicting);
  void modeChanged(SketchEditor::Mode mode);
  void finishRequested();

 private:
  struct Hit {
    enum Kind { None, Point, Curve, Origin, Label } kind = None;
    std::uint64_t id = 0;  // entity, or constraint for labels
    double u = 0, v = 0;   // picked position (snapped for points)
  };
  struct LabelSpot {
    QPointF screen;
    std::uint64_t constraint;
    bool dimension;
  };

  model::Timeline& tl() { return host_.timeline(); }
  model::LengthUnitKind unit();

  // Geometry helpers (plane coordinates).
  bool toPlane(const QPointF& pos, double& u, double& v) const;
  gp_Pnt world(double u, double v) const;
  double unitsPerPixel() const;
  const model::SketchEntityInfo* entity(std::uint64_t id) const;
  bool isCurve(std::uint64_t id) const;
  bool isPoint(std::uint64_t id) const;
  std::uint64_t originPointId() const;

  Hit hitTest(const QPointF& pos, bool pointsFirst = true, bool labels = false) const;
  Hit snap(const QPointF& pos) const;

  // Transactions.
  bool edit(const std::function<bool()>& change);
  std::uint64_t resolve(const Hit& h);  // inside edit(): an existing or new point id
  bool addConstraint(model::ConstraintKind kind, std::uint64_t a, std::uint64_t b = 0, std::uint64_t c = 0,
                     double value = 0.0, bool quiet = false);

  // Tools.
  void clickDraw(const Hit& h);
  void clickDimension(const Hit& h, const QPointF& pos);
  void clickConstrain(const Hit& h);
  void resetDraft();
  void hintForMode();

  // Dimensions.
  struct PendingDimension {
    std::uint64_t existing = 0;  // edit this constraint; else create:
    model::ConstraintKind kind = model::ConstraintKind::Length;
    Hit a, b;
    bool angle = false;
  };
  void openValueEditor(const PendingDimension& d, const QPointF& pos, double currentValue);
  void commitValue();
  void closeValueEditor();
  double measure(model::ConstraintKind kind, const Hit& a, const Hit& b) const;

  // Display.
  void redraw();
  void drawHover();
  void drawPreview();
  QString dimensionText(const model::SketchConstraintInfo& c);

  Viewport* view_;
  Host host_;
  std::uint64_t sketch_;
  model::PlaneInfo plane_{};

  std::vector<model::SketchEntityInfo> entities_;
  std::vector<model::SketchConstraintInfo> constraints_;
  std::vector<model::ProfileInfo> profiles_;
  int dof_ = -1;
  bool conflicting_ = false;

  Mode mode_ = Mode::Select;
  model::ConstraintKind constraintKind_ = model::ConstraintKind::Coincident;

  QSet<std::uint64_t> selected_;     // entities
  QSet<std::uint64_t> selectedCon_;  // constraints / dimensions
  Hit hover_;
  QPointF cursor_;
  bool hasCursor_ = false;

  // Drawing state.
  std::vector<Hit> clicks_;
  std::uint64_t chainStart_ = 0;
  std::vector<Hit> picks_;  // dimension / constraint picks

  // Dragging.
  bool pressed_ = false;
  bool dragging_ = false;
  Hit dragHit_;
  double dragU_ = 0, dragV_ = 0;
  QPointF pressPos_;
  std::unique_ptr<rust::Box<model::Timeline>> dragBefore_;

  std::vector<LabelSpot> labels_;
  QPointer<QLineEdit> valueEdit_;
  PendingDimension pending_;
  QTimer redrawTimer_;
};
