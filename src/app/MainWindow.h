#pragma once

#include "GeometryBridge.h"
#include "model_core/src/ffi.rs.h"  // Rust timeline API (generated)

#include "sketch/SketchEditor.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>

#include <memory>
#include <vector>

class CommandDialog;
class DocumentBar;
class FloatingPanel;
class NavBar;
class PlanePicker;
class QTreeWidgetItem;
class ProfilePicker;
class QMenu;
class QTreeWidget;
class Ribbon;
class TimelineBar;
class Viewport;

// Fusion-style layout:
//   document bar ─ logo/menu · Projects · document tab ········ help · account
//   ribbon ─────── workspace │ tabs / tool groups (icons, group menus)
//   viewport ───── 3D view with floating Browser, command dialog, nav bar, view cube
//   timeline ───── ⏮ ◀ ▶ ⏭  [feature chips] ▌marker          status
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void buildAppMenu();
  void buildBrowser();
  void layoutOverlays(bool animate = false);

  void onTool(const QString& id);
  void openBoxDialog();
  void openFilletDialog();
  void showDialog(CommandDialog* dialog);
  void closeDialog();
  QPoint dialogPosition() const;

  void loadDemoDocument();
  void rebuildModel(bool fit = false);
  void refreshTimeline();
  void refreshBrowser();
  void setMarker(long marker);

  void setUnits(model::LengthUnitKind unit, bool remember = true);
  void fillUnitsMenu(QMenu* menu);
  QString unitName() const;

  void exportStep();
  void showAbout();
  void openPreferences();
  void applyPreferences();
  void buildHelpMenu();
  void showShortcuts();
  void showDiagnostics();

  // Sketching.
  void openCreateSketch();
  void createSketch(model::OriginPlaneKind plane);
  void enterSketch(std::uint64_t sketch);
  void finishSketch();
  void cancelSketch();
  void onSketchTool(const QString& id);
  void showSketches();
  void showOrigin();
  bool sketchVisible(std::uint64_t id) const;
  void toggleBrowserVisibility(QTreeWidgetItem* item);
  QVector<std::uint64_t> sketchIds(bool unconsumedOnly) const;

  // Extrude from sketch profiles.
  void openExtrudeDialog();
  void cancelPickers();

  // Undo / redo of the whole document.
  void pushUndo();
  void undo();
  void redo();
  void afterHistoryChange();

  rust::Box<model::Timeline> timeline_ = model::new_timeline();
  TopoDS_Shape body_;
  QVector<std::size_t> failed_;
  QString documentName_ = QStringLiteral("Untitled");

  DocumentBar* documentBar_ = nullptr;
  Ribbon* ribbon_ = nullptr;
  Viewport* viewport_ = nullptr;
  TimelineBar* timelineBar_ = nullptr;
  FloatingPanel* browserPanel_ = nullptr;
  QTreeWidget* browser_ = nullptr;
  NavBar* navBar_ = nullptr;
  QPointer<CommandDialog> dialog_;

  std::unique_ptr<SketchEditor> sketchEditor_;
  QPointer<FloatingPanel> sketchPanel_;
  std::unique_ptr<rust::Box<model::Timeline>> sketchEntry_;  // state to restore on Cancel Sketch
  std::size_t sketchUndoDepth_ = 0;
  std::unique_ptr<PlanePicker> planePicker_;
  std::unique_ptr<ProfilePicker> profilePicker_;
  long sketchReturnMarker_ = -1;  // marker to restore after editing an older sketch
  bool previewing_ = false;       // the view shows an extrude preview
  std::vector<rust::Box<model::Timeline>> undo_;
  std::vector<rust::Box<model::Timeline>> redo_;
  QString permanentStatus_;
  QHash<std::uint64_t, bool> sketchVisibility_;  // user overrides (default: shown until used)
  bool sketchesShown_ = true;                     // the Sketches folder's eye
  bool originShown_ = false;
};
