#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_Drawable.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include <QOpenGLWidget>

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
  void fitAll();
  void homeView();  // isometric, fitted

  // What a plain left-drag does (middle-drag pan / Shift+middle orbit always work).
  void setDragMode(DragMode mode);
  void setDisplayMode(DisplayMode mode);
  void setGridVisible(bool visible);

  QSize minimumSizeHint() const override { return {200, 200}; }

 protected:
  void initializeGL() override;
  void paintGL() override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

  // AIS_ViewController
  void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                        const Handle(V3d_View)& view) override;

 private:
  void setupMouseGestures();
  Aspect_Drawable nativeHandle() const;

  Handle(V3d_Viewer) viewer_;
  Handle(V3d_View) view_;
  Handle(AIS_InteractiveContext) context_;
  Handle(AIS_ViewCube) viewCube_;
  Handle(AIS_Shape) body_;
  bool fitPending_ = false;
  DragMode dragMode_ = DragMode::Select;
  DisplayMode displayMode_ = DisplayMode::ShadedWithEdges;
  bool bodyVisible_ = true;
  void applyDisplayMode();  // fit requested before the GL window existed
};
