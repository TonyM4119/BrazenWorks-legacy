#pragma once

#include "Kernel.h"
#include "Viewport.h"
#include "model_core/src/ffi.rs.h"  // Rust timeline API (generated)

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <TopoDS_Shape.hxx>

#include <array>
#include <cstdint>
#include <memory>
#include <functional>
#include <vector>

class CommandDialog;
class QLabel;
class QLineEdit;

// Edits one sketch of the timeline in the 3D view: drawing tools, snapping,
// constraints, dimensions, selection and dragging. Every change is a
// transaction: the host gets a snapshot for undo, or the snapshot back if the
// change failed half-way.
class SketchEditor : public QObject, public ViewportTool {
  Q_OBJECT
 public:
  enum class Mode {
    Select, Line, Rectangle, Circle, Arc, Point, Dimension, Constrain,
    // More create tools.
    Rectangle3, RectangleCentre, Circle2, Circle3, CentreArc, TangentArc, Polygon, Ellipse, Slot, Spline,
    // Modify tools.
    Trim, Extend, Break, Fillet, Chamfer, Offset, Mirror, Move, Rotate, Scale, PatternRect, PatternCirc, Project,
    // Variants (Fusion's submenus).
    MidLine, PolygonCirc, PolygonEdge, SlotOverall, SlotCentre,
  };
  static bool isDrawing(Mode m);  // places points with snapping
  static bool picksCurves(Mode m);  // Mirror, Move…: pick curves first, Enter, then the rest

  struct Host {
    std::function<model::Timeline&()> timeline;
    std::function<void(rust::Box<model::Timeline>)> pushUndo;  // state before a change
    std::function<void(rust::Box<model::Timeline>)> restore;   // undo a failed change
    std::function<TopoDS_Shape()> body;                         // for Project (may be null)
  };

  SketchEditor(Viewport* view, Host host, std::uint64_t sketch, QObject* parent = nullptr);
  ~SketchEditor() override;

  std::uint64_t sketch() const { return sketch_; }
  Mode mode() const { return mode_; }
  model::ConstraintKind constraintKind() const { return constraintKind_; }

  void setMode(Mode mode);
  // autoHV: Fusion's Horizontal/Vertical, which picks by the geometry's direction.
  void setConstraint(model::ConstraintKind kind, bool autoHV = false);
  bool autoHV() const { return autoHV_; }
  // Rounded rectangles: the next rectangles get their corners filleted.
  void setRoundCorners(bool on) { roundCorners_ = on; }
  bool roundCorners() const { return roundCorners_; }  // enters Constrain mode
  void toggleConstruction();
  void deleteSelection();
  // Re-reads the sketch (after undo/redo or a unit change) and redraws.
  void reload();
  void clearDisplay();
  bool fullyConstrained() const { return dof_ == 0; }

  // Sketch Palette.
  enum class Display { Dimensions, Constraints, Points, Profiles, Snap };
  void setDisplay(Display what, bool on);
  bool display(Display what) const;
  void setConstructionMode(bool on);
  bool constructionMode() const { return constructionMode_; }
  void selectUnconstrained();
  // Adds the constraints the geometry already shows (coincident ends,
  // horizontal/vertical lines…).
  void autoConstrain();
  // Clipboard (shared between sketches): Ctrl+C, Ctrl+X, Ctrl+V.
  void copySelection();
  void cutSelection();
  void paste();

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
  void commandDialog(CommandDialog* dialog);  // the host shows it like other commands

 private:
  struct Hit {
    enum Kind { None, Point, Curve, Origin, Label } kind = None;
    std::uint64_t id = 0;  // entity, or constraint for labels
    double u = 0, v = 0;   // picked position (snapped for points)
    bool mid = false;      // snapped to the curve's midpoint
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

  // Sizes typed while drawing (value[i] in mm when set[i]).
  struct Typed {
    double value[2] = {0, 0};
    bool set[2] = {false, false};
  };

  // Tools.
  void clickDraw(const Hit& h, const Typed& typed);
  void clickDraw(const Hit& h) { clickDraw(h, Typed()); }
  void markNew(const std::vector<std::uint64_t>& curves);
  int sizeBoxCount() const;
  bool typedValue(int i, double& mm);
  void showSizeBoxes(const double live[2], const QPointF& anchor);
  void hideSizeBoxes();
  void resetSizeBoxes();
  void focusSizeBox(int i);
  Hit typedTarget(const Hit& first, Hit cursor, Typed& typed);
  void commitSizes();
  void clickDimension(const Hit& h, const QPointF& pos);
  void clickConstrain(const Hit& h);
  void resetDraft();
  void hintForMode();

  // More create tools (SketchShapes.cpp).
  void clickShape(const Hit& h, const Typed& typed);
  void finishSpline();
  void previewShape(const Hit& a, double cu, double cv, std::vector<geom::Seg2>& segs, double live[2]);
  std::uint64_t cornerAt(const std::vector<std::uint64_t>& lines, double u, double v) const;
  void attach(const Hit& h, std::uint64_t point);  // join a new shape's point to what was clicked

  // Modify tools (SketchModify.cpp).
  void clickModify(const Hit& h, const QPointF& pos, Qt::KeyboardModifiers mods);
  void finishPicking();
  void previewModify(Overlay& o, double cu, double cv);
  std::vector<std::uint64_t> chainOf(std::uint64_t curve) const;
  std::vector<std::uint64_t> cornerLines(std::uint64_t point) const;
  int edgeAt(const QPointF& pos) const;
  void projectEdge(int index);
  void openPatternDialog(bool circular, std::uint64_t editGroup = 0);
  void showCurveMenu(std::uint64_t curve, const QPointF& pos);
  std::uint64_t editMirror_ = 0;  // Mirror tool re-picking the axis of this group
  void showDimensionMenu(std::uint64_t constraint, const QPointF& pos);
  bool applyResult(const model::ConstraintResult& r, model::ConstraintKind kind, std::uint64_t a, std::uint64_t b,
                   std::uint64_t c);

  // Dimensions.
  struct PendingDimension {
    std::uint64_t existing = 0;  // edit this constraint; else create:
    model::ConstraintKind kind = model::ConstraintKind::Length;
    Hit a, b;
    bool angle = false;
    bool placed = false;  // the label goes at (lu, lv)
    double lu = 0, lv = 0;
  };
  bool placing_ = false;          // a new dimension follows the cursor until a click places it
  std::uint64_t labelDrag_ = 0;   // dimension whose label is being dragged
  bool labelMoved_ = false;
  void dragLabel(const QPointF& pos);
  void storeLabel(std::uint64_t constraint, double lu, double lv);
  void openValueEditor(const PendingDimension& d, const QPointF& pos, double currentValue);
  // A value box that runs a tool step (fillet radius, rotate angle…) instead
  // of setting a dimension.
  enum class ValueKind { Length, Angle, Number };
  void askValue(const QString& prompt, ValueKind kind, double initial, const QPointF& pos,
                std::function<bool(double)> op);
  void placeValueBox(QLineEdit* edit, const QPointF& pos);
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

  QPointer<QLineEdit> sizeEdit_[2];
  bool sizeTyped_[2] = {false, false};
  bool constructionMode_ = false;
  bool showDims_ = true;
  bool showTags_ = true;
  bool showPoints_ = true;
  bool showProfiles_ = true;
  bool snap_ = true;

  // Snapping guides found by snap() (u0, v0, u1, v1), drawn by the preview.
  mutable std::vector<std::array<double, 4>> guides_;
  // Modify tools: the curves picked, which step comes next, the anchor.
  std::vector<std::uint64_t> opCurves_;
  int opStage_ = 0;
  Hit opAnchor_;
  std::function<bool(double)> valueOp_;
  ValueKind valueKind_ = ValueKind::Length;
  std::vector<std::uint64_t> looseEnds_;
  std::vector<std::uint64_t> degenerate_;
  std::vector<TopoDS_Shape> bodyEdges_;
  int hoverEdge_ = -1;
  int polygonSides_ = 6;
  bool autoHV_ = false;
  bool roundCorners_ = false;
  bool sizeAt_[2] = {false, false};  // size boxes sit on these screen spots (live dimensions)
  QPointF sizeSpot_[2];
  void roundRectangle(const std::vector<std::uint64_t>& lines, double w, double h);

  // Fusion-style cursor (arrow, crosshair, the tool's icon) and the prompt
  // that follows it ("Place first corner").
  void updateCursor();
  QString stepPrompt() const;
  void movePrompt();
  QLabel* prompt_ = nullptr;

  // Box selection: left to right takes what is inside, right to left what
  // the box touches (as Fusion does).
  bool boxing_ = false;
  QPointF boxStart_;
  bool boxAdd_ = false;
  std::vector<QPointF> screenPolyline(const model::SketchEntityInfo& e) const;
  void drawBox();
  void finishBox(const QPointF& end);
};
