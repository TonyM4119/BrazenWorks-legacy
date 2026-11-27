#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_Drawable.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include <QColor>
#include <QHash>
#include <QOpenGLWidget>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace geom {
struct Plane;
}

class QKeyEvent;

// Something that takes over left-clicks in the viewport (sketch editor,
// profile picker…). Return true to consume the event; middle-drag and the
// wheel always navigate.
class ViewportTool {
 public:
  virtual ~ViewportTool() = default;
  virtual bool mousePress(QMouseEvent*) { return false; }
  virtual bool mouseMove(QMouseEvent*) { return false; }
  virtual bool mouseRelease(QMouseEvent*) { return false; }
  virtual bool mouseDoubleClick(QMouseEvent*) { return false; }
  virtual bool keyPress(QKeyEvent*) { return false; }
  virtual void viewChanged() {}
};

// Non-selectable display drawn over the model (sketch curves, labels, previews).
struct Overlay {
  struct Curves {
    TopoDS_Shape shape;
    QColor color;
    double width = 2.0;
    bool dashed = false;
  };
  struct Face {
    TopoDS_Shape shape;
    QColor color;
    double transparency = 0.6;  // 0 opaque … 1 invisible
  };
  struct Points {
    TopoDS_Shape shape;  // compound of vertices
    QColor color;
    double size = 2.0;
  };
  struct Label {
    gp_Pnt position;
    QString text;
    QColor color;
    double height = 13.0;
    QColor background;  // invalid = no tag behind the text
  };
  std::vector<Face> faces;
  std::vector<Curves> curves;
  std::vector<Points> points;
  std::vector<Label> labels;
};

// The 3D canvas: an Open CASCADE V3d view rendered into a Qt OpenGL widget.
// Mouse mapping follows the common CAD convention (Fusion-style):
//   middle-drag = pan, Shift+middle-drag = orbit, wheel = zoom, left = select.
class Viewport : public QOpenGLWidget, public AIS_ViewController {
  Q_OBJECT
 public:
  explicit Viewport(QWidget* parent = nullptr);
  ~Viewport() override;

  enum class DragMode { Select, Orbit, Pan, Zoom };
  enum class DisplayMode { ShadedWithEdges, Shaded, Wireframe };

  void showBody(const TopoDS_Shape& body);
  void setBodyVisible(bool visible);
  bool bodyVisible() const { return bodyVisible_; }
  void fitAll(bool animate = false);
  void homeView();  // isometric, fitted, animated

  // What a plain left-drag does (middle-drag pan / Shift+middle orbit always work).
  void setDragMode(DragMode mode);
  void setDisplayMode(DisplayMode mode);
  void setGridVisible(bool visible);
  // Grid line spacing and half-size, in mm (every tenth line is stronger).
  void setGridSpacing(double stepMm, double extentMm);
  // The plane the grid lies on (the XY plane by default).
  void setGridPlane(const geom::Plane& plane);

  // Overlay layers, replaced as a whole ("sketch", "preview", "profiles"…).
  void setOverlay(const QString& layer, const Overlay& overlay);
  void clearOverlay(const QString& layer);

  // The tool receiving left-clicks, or nullptr for normal selection.
  void setTool(ViewportTool* tool);
  ViewportTool* tool() const { return tool_; }

  // Widget pixel → ray in world space (origin, direction).
  std::pair<gp_Pnt, gp_Dir> rayAt(const QPointF& pos) const;
  // World point → widget pixel.
  QPointF project(const gp_Pnt& p) const;
  // Turn the camera to look along -normal with `up` pointing up, fitted.
  void lookAlong(const gp_Dir& normal, const gp_Dir& up);
  void setBodyTransparency(double transparency);
  // Preferences → Navigation.
  void setNavigation(bool orbitWithMiddle, bool reverseZoom, bool animateCamera);
  // Preferences → Diagnostics: the OpenGL renderer and version in use.
  QString glInfo() const { return glInfo_; }

  QSize minimumSizeHint() const override { return {200, 200}; }

 protected:
  void initializeGL() override;
  void paintGL() override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

  // AIS_ViewController
  void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                        const Handle(V3d_View)& view) override;

 private:
  void setupMouseGestures();
  // Glides the camera from where it is to where `applyTarget` puts it.
  void animateCamera(const std::function<void()>& applyTarget);
  Aspect_Drawable nativeHandle() const;

  Handle(V3d_Viewer) viewer_;
  Handle(V3d_View) view_;
  Handle(AIS_InteractiveContext) context_;
  Handle(AIS_ViewCube) viewCube_;
  Handle(AIS_Shape) body_;
  bool fitPending_ = false;
  DragMode dragMode_ = DragMode::Select;
  bool orbitWithMiddle_ = false;
  bool reverseZoom_ = false;
  bool animate_ = true;
  QString glInfo_;
  DisplayMode displayMode_ = DisplayMode::ShadedWithEdges;
  bool bodyVisible_ = true;
  double bodyTransparency_ = 0.0;
  ViewportTool* tool_ = nullptr;
  QHash<QString, std::vector<Handle(AIS_InteractiveObject)>> overlays_;
  void applyDisplayMode();  // fit requested before the GL window existed
};
