#pragma once

#include "GeometryBridge.h"
#include "model_core/src/ffi.rs.h"  // Rust timeline API (generated)

#include <QMainWindow>
#include <QPointer>

class CommandDialog;
class DocumentBar;
class FloatingPanel;
class NavBar;
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

  void exportStep();
  void showAbout();
  void openPreferences();
  void applyPreferences();
  void buildHelpMenu();
  void showShortcuts();
  void showDiagnostics();

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
};
