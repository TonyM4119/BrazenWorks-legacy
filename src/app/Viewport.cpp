#include "Viewport.h"

#include <AIS_MouseGesture.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_Grid.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Aspect_ScrollDelta.hxx>
#include <Aspect_VKeyFlags.hxx>
#include <NCollection_Vec2.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <OpenGl_View.hxx>
#include <OpenGl_Window.hxx>
#include <Prs3d_Drawer.hxx>
#include <Quantity_Color.hxx>
#include <V3d_TypeOfOrientation.hxx>

#include "Theme.h"

#include <QMouseEvent>
#include <QWindow>

#ifdef _WIN32
#include <windows.h>
#endif
#include <QSurfaceFormat>
#include <QWheelEvent>

// Approach follows Open CASCADE's official Qt sample (samples/qt/OcctQtViewer):
// OCCT renders into the framebuffer that QOpenGLWidget gives it.

namespace {

// Spelled out instead of Graphic3d_Vec2i: OCCT 8 no longer pulls that typedef
// in through the viewer headers. Same type in every OCCT version.
using Vec2i = NCollection_Vec2<int>;

// Wraps Qt's FBO so OCCT draws into it with sRGB handled correctly.
class QtFrameBuffer : public OpenGl_FrameBuffer {
  DEFINE_STANDARD_RTTI_INLINE(QtFrameBuffer, OpenGl_FrameBuffer)
 public:
  void BindBuffer(const Handle(OpenGl_Context)& ctx) override {
    OpenGl_FrameBuffer::BindBuffer(ctx);
    ctx->SetFrameBufferSRGB(true, false);
  }
  void BindDrawBuffer(const Handle(OpenGl_Context)& ctx) override {
    OpenGl_FrameBuffer::BindDrawBuffer(ctx);
    ctx->SetFrameBufferSRGB(true, false);
  }
  void BindReadBuffer(const Handle(OpenGl_Context)& ctx) override {
    OpenGl_FrameBuffer::BindReadBuffer(ctx);
  }
};

Handle(OpenGl_Context) glContextOf(const Handle(V3d_View)& view) {
  Handle(OpenGl_View) glView = Handle(OpenGl_View)::DownCast(view->View());
  return glView->GlWindow()->GetGlContext();
}

Aspect_VKeyMouse toVKeyMouse(Qt::MouseButtons buttons) {
  Aspect_VKeyMouse keys = Aspect_VKeyMouse_NONE;
  if (buttons & Qt::LeftButton) keys |= Aspect_VKeyMouse_LeftButton;
  if (buttons & Qt::MiddleButton) keys |= Aspect_VKeyMouse_MiddleButton;
  if (buttons & Qt::RightButton) keys |= Aspect_VKeyMouse_RightButton;
  return keys;
}

Aspect_VKeyFlags toVKeyFlags(Qt::KeyboardModifiers mods) {
  Aspect_VKeyFlags flags = Aspect_VKeyFlags_NONE;
  if (mods & Qt::ShiftModifier) flags |= Aspect_VKeyFlags_SHIFT;
  if (mods & Qt::ControlModifier) flags |= Aspect_VKeyFlags_CTRL;
  if (mods & Qt::AltModifier) flags |= Aspect_VKeyFlags_ALT;
  return flags;
}

Vec2i toPoint(const QPointF& p, qreal dpr) {
  return Vec2i(int(p.x() * dpr), int(p.y() * dpr));
}

}  // namespace

// The window handle OCCT records for the view. We must NOT call winId() on the
// viewport itself: that turns it into a native child window, and Qt can then
// no longer draw floating panels (browser, dialogs, nav bar) on top of it.
// Following OCCT's Qt sample: on Windows use the window of the current GL
// device context; elsewhere the top-level window's handle.
Aspect_Drawable Viewport::nativeHandle() const {
#ifdef _WIN32
  return (Aspect_Drawable)WindowFromDC(wglGetCurrentDC());
#else
  return (Aspect_Drawable)window()->winId();
#endif
}

Viewport::Viewport(QWidget* parent) : QOpenGLWidget(parent) {
  Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
  Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display, false);
  // Qt owns the window and swaps buffers; OCCT only draws.
  driver->ChangeOptions().buffersNoSwap = true;
  driver->ChangeOptions().buffersOpaqueAlpha = true;
  driver->ChangeOptions().useSystemBuffer = false;

  viewer_ = new V3d_Viewer(driver);
  const QColor bg = theme::current().viewport;
  viewer_->SetDefaultBackgroundColor(Quantity_Color(bg.redF(), bg.greenF(), bg.blueF(), Quantity_TOC_sRGB));
  viewer_->SetDefaultLights();
  viewer_->SetLightOn();
  viewer_->SetRectangularGridValues(0.0, 0.0, 10.0, 10.0, 0.0);      // 10 mm cells
  viewer_->SetRectangularGridGraphicValues(250.0, 250.0, 0.0);        // ±250 mm
  viewer_->ActivateGrid(Aspect_GT_Rectangular, Aspect_GDM_Lines);
  if (auto grid = viewer_->Grid(Aspect_GT_Rectangular); !grid.IsNull()) {
    // Subtle lines like the UI concept: minor slightly above the background,
    // every tenth line a little stronger.
    const QColor minor = theme::current().viewport.lighter(132);
    const QColor major = theme::current().viewportGrid;
    grid->SetColors(Quantity_Color(minor.redF(), minor.greenF(), minor.blueF(), Quantity_TOC_sRGB),
                    Quantity_Color(major.redF(), major.greenF(), major.blueF(), Quantity_TOC_sRGB));
  }

  context_ = new AIS_InteractiveContext(viewer_);
  context_->SetDisplayMode(AIS_Shaded, false);
  context_->DefaultDrawer()->SetFaceBoundaryDraw(true);

  viewCube_ = new AIS_ViewCube();
  viewCube_->SetViewAnimation(myViewAnimation);
  viewCube_->SetFixedAnimationLoop(false);
  viewCube_->SetAutoStartAnimation(true);
  viewCube_->SetSize(55);
  // Match the dark theme instead of OCCT's default white cube.
  {
    const theme::Palette& tp = theme::current();
    auto occt = [](const QColor& c) { return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB); };
    viewCube_->SetBoxColor(occt(tp.panelBorder));
    viewCube_->SetInnerColor(occt(tp.panel));
    viewCube_->SetTextColor(occt(tp.text));
    viewCube_->SetFontHeight(11);
    viewCube_->SetRoundRadius(0.1);
    viewCube_->SetDrawAxes(false);
  }
  viewCube_->SetTransformPersistence(new Graphic3d_TransformPers(
      Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER, Vec2i(90, 90)));

  view_ = viewer_->CreateView();
  view_->SetImmediateUpdate(false);

  setupMouseGestures();

  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

  QSurfaceFormat format;
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
  setFormat(format);
}

Viewport::~Viewport() {
  // Release OCCT GL resources while Qt's context is still current.
  makeCurrent();
  if (Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(view_->Window());
      !window.IsNull()) {
    window->SetNativeHandle(0);
  }
  context_->RemoveAll(false);
  context_.Nullify();
  view_->Remove();
  view_.Nullify();
  viewer_.Nullify();
  doneCurrent();
}

void Viewport::setupMouseGestures() {
  auto& map = ChangeMouseGestureMap();  // typedef AIS_MouseGestureMap is gone in OCCT 8
  map.Clear();
  map.Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_Pan);
  map.Bind(Aspect_VKeyMouse(Aspect_VKeyMouse_MiddleButton) | Aspect_VKeyFlags(Aspect_VKeyFlags_SHIFT),
           AIS_MouseGesture_RotateOrbit);
  switch (dragMode_) {
    case DragMode::Select: map.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_SelectRectangle); break;
    case DragMode::Orbit: map.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_RotateOrbit); break;
    case DragMode::Pan: map.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_Pan); break;
    case DragMode::Zoom: map.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_Zoom); break;
  }
}

void Viewport::setDragMode(DragMode mode) {
  dragMode_ = mode;
  setupMouseGestures();
  setCursor(mode == DragMode::Select ? Qt::ArrowCursor
            : mode == DragMode::Pan  ? Qt::OpenHandCursor
                                     : Qt::SizeAllCursor);
}

void Viewport::setDisplayMode(DisplayMode mode) {
  displayMode_ = mode;
  applyDisplayMode();
}

void Viewport::applyDisplayMode() {
  if (body_.IsNull()) return;
  body_->Attributes()->SetFaceBoundaryDraw(displayMode_ == DisplayMode::ShadedWithEdges);
  context_->SetDisplayMode(body_, displayMode_ == DisplayMode::Wireframe ? AIS_WireFrame : AIS_Shaded, false);
  context_->Redisplay(body_, false);
  view_->Invalidate();
  update();
}

void Viewport::setGridVisible(bool visible) {
  if (visible) {
    viewer_->ActivateGrid(Aspect_GT_Rectangular, Aspect_GDM_Lines);
  } else {
    viewer_->DeactivateGrid();
  }
  view_->Invalidate();
  update();
}

void Viewport::setBodyVisible(bool visible) {
  bodyVisible_ = visible;
  if (body_.IsNull()) return;
  if (visible) {
    context_->Display(body_, false);
    applyDisplayMode();
  } else {
    context_->Erase(body_, false);
  }
  view_->Invalidate();
  update();
}

void Viewport::homeView() {
  view_->SetProj(V3d_XposYnegZpos);
  fitAll();
}

void Viewport::initializeGL() {
  const qreal dpr = devicePixelRatioF();
  const Vec2i size(int(width() * dpr), int(height() * dpr));
  const Aspect_Drawable nativeWindow = nativeHandle();

  Handle(OpenGl_Context) glCtx = new OpenGl_Context();
  if (!glCtx->Init(false)) {
    qWarning("Viewport: Open CASCADE could not initialise the OpenGL context");
    return;
  }

  Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(view_->Window());
  const bool firstInit = window.IsNull();
  if (firstInit) {
    window = new Aspect_NeutralWindow();
    window->SetVirtual(true);
  }
  window->SetNativeHandle(nativeWindow);
  window->SetSize(size.x(), size.y());
  view_->SetWindow(window, glCtx->RenderingContext());

  if (firstInit) {
    context_->Display(viewCube_, 0, 0, false);
  }
  if (fitPending_) {
    fitPending_ = false;
    view_->MustBeResized();
    view_->FitAll(0.1, false);
  }
}

void Viewport::paintGL() {
  if (view_.IsNull() || view_->Window().IsNull()) return;

  const Aspect_Drawable nativeWindow = nativeHandle();
  if (view_->Window()->NativeHandle() != nativeWindow) {
    // The widget was re-parented (e.g. docked/undocked): rebind.
    initializeGL();
    return;
  }

  Handle(OpenGl_Context) glCtx = glContextOf(view_);
  Handle(OpenGl_FrameBuffer) fbo = glCtx->DefaultFrameBuffer();
  if (fbo.IsNull()) {
    fbo = new QtFrameBuffer();
    glCtx->SetDefaultFrameBuffer(fbo);
  }
  if (!fbo->InitWrapper(glCtx)) {
    fbo.Nullify();
    return;
  }

  Vec2i oldSize;
  Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(view_->Window());
  window->Size(oldSize.x(), oldSize.y());
  const Vec2i newSize = fbo->GetVPSize();
  if (newSize != oldSize) {
    window->SetSize(newSize.x(), newSize.y());
    view_->MustBeResized();
    view_->Invalidate();
  }

  view_->InvalidateImmediate();
  AIS_ViewController::FlushViewEvents(context_, view_, true);
}

void Viewport::showBody(const TopoDS_Shape& body) {
  if (!body_.IsNull()) {
    context_->Remove(body_, false);
    body_.Nullify();
  }
  if (!body.IsNull()) {
    body_ = new AIS_Shape(body);
    body_->SetColor(Quantity_Color(0.73, 0.75, 0.78, Quantity_TOC_sRGB));
    if (bodyVisible_) context_->Display(body_, AIS_Shaded, 0, false);
    applyDisplayMode();
  }
  view_->Invalidate();
  update();
}

void Viewport::fitAll() {
  if (view_->Window().IsNull()) {  // not shown yet: fit once GL is ready
    fitPending_ = true;
    return;
  }
  view_->FitAll(0.1, false);
  view_->Invalidate();
  update();
}

void Viewport::mousePressEvent(QMouseEvent* e) {
  QOpenGLWidget::mousePressEvent(e);
  if (!view_.IsNull() &&
      UpdateMouseButtons(toPoint(e->position(), devicePixelRatioF()), toVKeyMouse(e->buttons()),
                         toVKeyFlags(e->modifiers()), false)) {
    update();
  }
}

void Viewport::mouseReleaseEvent(QMouseEvent* e) {
  QOpenGLWidget::mouseReleaseEvent(e);
  if (!view_.IsNull() &&
      UpdateMouseButtons(toPoint(e->position(), devicePixelRatioF()), toVKeyMouse(e->buttons()),
                         toVKeyFlags(e->modifiers()), false)) {
    update();
  }
}

void Viewport::mouseMoveEvent(QMouseEvent* e) {
  QOpenGLWidget::mouseMoveEvent(e);
  if (!view_.IsNull() &&
      UpdateMousePosition(toPoint(e->position(), devicePixelRatioF()), toVKeyMouse(e->buttons()),
                          toVKeyFlags(e->modifiers()), false)) {
    update();
  }
}

void Viewport::wheelEvent(QWheelEvent* e) {
  QOpenGLWidget::wheelEvent(e);
  if (view_.IsNull()) return;
  const Vec2i pos = toPoint(e->position(), devicePixelRatioF());
  if (UpdateZoom(Aspect_ScrollDelta(pos, double(e->angleDelta().y()) / 8.0))) {
    update();
  }
}

void Viewport::handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                                const Handle(V3d_View)& view) {
  AIS_ViewController::handleViewRedraw(ctx, view);
  if (myToAskNextFrame) update();  // keep animating (e.g. view cube rotation)
}
