#include "Viewport.h"


#include <AIS_AnimationCamera.hxx>
#include <AIS_MouseGesture.hxx>
#include <AIS_TextLabel.hxx>
#include <Font_NameOfFont.hxx>
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
#include <Graphic3d_Camera.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_TextAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <TCollection_ExtendedString.hxx>
#include <Quantity_Color.hxx>
#include <V3d_TypeOfOrientation.hxx>

#include "Settings.h"
#include "Theme.h"

#include <QKeyEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QMouseEvent>
#include <QWindow>

#ifdef _WIN32
void* currentGlWindowHandle();  // GlWindowHandle_win.cpp
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
  return (Aspect_Drawable)currentGlWindowHandle();
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
    const QColor minor = theme::current().light ? theme::current().viewport.darker(106)
                                                 : theme::current().viewport.lighter(132);
    const QColor major = theme::current().viewportGrid;
    grid->SetColors(Quantity_Color(minor.redF(), minor.greenF(), minor.blueF(), Quantity_TOC_sRGB),
                    Quantity_Color(major.redF(), major.greenF(), major.blueF(), Quantity_TOC_sRGB));
  }

  context_ = new AIS_InteractiveContext(viewer_);
  context_->SetDisplayMode(AIS_Shaded, false);
  context_->DefaultDrawer()->SetFaceBoundaryDraw(true);
  // Rounder curved surfaces: finer tessellation than OCCT's defaults
  // (chord deviation 0.1% of size, 20° angle).
  constexpr double kDegree = 3.14159265358979323846 / 180.0;
  const bool high = prefs::flag(prefs::kHighQuality);
  context_->DefaultDrawer()->SetDeviationCoefficient(high ? 0.0003 : 0.001);
  context_->DefaultDrawer()->SetDeviationAngle((high ? 4.0 : 12.0) * kDegree);

  viewCube_ = new AIS_ViewCube();
  viewCube_->SetViewAnimation(myViewAnimation);
  viewCube_->SetFixedAnimationLoop(false);
  viewCube_->SetAutoStartAnimation(true);
  viewCube_->SetDuration(0.4);
  viewCube_->SetSize(68);
  // Fusion-style cube: light faces, darker edges and corners, dark labels, and
  // coloured X/Y/Z axes running from its origin corner.
  {
    auto rgb = [](int r, int g, int b) { return Quantity_Color(r / 255.0, g / 255.0, b / 255.0, Quantity_TOC_sRGB); };
    viewCube_->SetBoxColor(rgb(236, 239, 242));
    viewCube_->BoxEdgeStyle()->SetColor(rgb(196, 202, 209));
    viewCube_->BoxCornerStyle()->SetColor(rgb(176, 183, 191));
    viewCube_->SetInnerColor(rgb(120, 126, 133));
    viewCube_->SetTextColor(rgb(70, 76, 84));
    viewCube_->SetFont(Font_NOF_SANS_SERIF);
    viewCube_->SetFontHeight(12);
    viewCube_->SetRoundRadius(0.08);
    viewCube_->SetBoxEdgeGap(0.0);
    viewCube_->SetDrawAxes(true);
    viewCube_->SetAxesLabels("X", "Y", "Z");
    viewCube_->SetAxesPadding(1.0);
    viewCube_->SetAxesRadius(1.1);
    viewCube_->SetAxesConeRadius(0.0);
    viewCube_->SetAxesSphereRadius(1.4);
    // The cube's own datum style; otherwise it falls back to the shared default.
    Handle(Prs3d_DatumAspect) datum = new Prs3d_DatumAspect();
    viewCube_->Attributes()->SetDatumAspect(datum);
    const std::pair<Prs3d_DatumParts, Quantity_Color> axes[] = {
        {Prs3d_DatumParts_XAxis, rgb(229, 72, 77)},
        {Prs3d_DatumParts_YAxis, rgb(61, 187, 79)},
        {Prs3d_DatumParts_ZAxis, rgb(62, 124, 240)},
    };
    for (const auto& [part, color] : axes) {
      datum->ShadingAspect(part)->SetColor(color);
      datum->LineAspect(part)->SetColor(color);
      datum->TextAspect(part)->SetColor(color);
      datum->TextAspect(part)->SetFont(Font_NOF_SANS_SERIF);
      datum->TextAspect(part)->SetHeight(14.0);
    }
  }
  viewCube_->SetTransformPersistence(new Graphic3d_TransformPers(
      Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER, Vec2i(105, 105)));

  view_ = viewer_->CreateView();
  view_->SetImmediateUpdate(false);
  // Smooth edges: 4x multisample anti-aliasing for shaded faces, edges and grid.
  const bool aa = prefs::flag(prefs::kAntialiasing);
  view_->ChangeRenderingParams().NbMsaaSamples = aa ? 4 : 0;
  view_->ChangeRenderingParams().IsAntialiasingEnabled = aa;

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
  // Default (Fusion): middle-drag pans, Shift+middle orbits. Preferences can swap them.
  map.Bind(Aspect_VKeyMouse_MiddleButton, orbitWithMiddle_ ? AIS_MouseGesture_RotateOrbit : AIS_MouseGesture_Pan);
  map.Bind(Aspect_VKeyMouse(Aspect_VKeyMouse_MiddleButton) | Aspect_VKeyFlags(Aspect_VKeyFlags_SHIFT),
           orbitWithMiddle_ ? AIS_MouseGesture_Pan : AIS_MouseGesture_RotateOrbit);
  switch (dragMode_) {
    case DragMode::Select: map.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_SelectRectangle); break;
    case DragMode::Orbit: map.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_RotateOrbit); break;
    case DragMode::Pan: map.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_Pan); break;
    case DragMode::Zoom: map.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_Zoom); break;
  }
}

void Viewport::setNavigation(bool orbitWithMiddle, bool reverseZoom, bool animateCamera) {
  orbitWithMiddle_ = orbitWithMiddle;
  reverseZoom_ = reverseZoom;
  animate_ = animateCamera;
  if (tool_) {
    setTool(tool_);  // rebinds, keeping left-drag for the tool
  } else {
    setupMouseGestures();
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

void Viewport::setGridSpacing(double stepMm, double extentMm) {
  viewer_->SetRectangularGridValues(0.0, 0.0, stepMm, stepMm, 0.0);
  viewer_->SetRectangularGridGraphicValues(extentMm, extentMm, 0.0);
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
  if (view_->Window().IsNull()) {
    view_->SetProj(V3d_XposYnegZpos);
    fitAll();
    return;
  }
  animateCamera([this] {
    view_->SetProj(V3d_XposYnegZpos);
    view_->FitAll(0.1, false);
  });
}

void Viewport::animateCamera(const std::function<void()>& applyTarget) {
  if (!animate_) {
    applyTarget();
    view_->Invalidate();
    update();
    return;
  }
  Handle(Graphic3d_Camera) start = new Graphic3d_Camera();
  start->Copy(view_->Camera());
  applyTarget();  // moves the camera to the goal without drawing
  Handle(Graphic3d_Camera) end = new Graphic3d_Camera();
  end->Copy(view_->Camera());
  view_->Camera()->Copy(start);

  // AIS_ViewController plays myViewAnimation frame by frame in handleViewRedraw.
  myViewAnimation->SetView(view_);
  myViewAnimation->SetCameraStart(start);
  myViewAnimation->SetCameraEnd(end);
  myViewAnimation->SetOwnDuration(0.35);
  myViewAnimation->StartTimer(0.0, 1.0, true, false);
  view_->Invalidate();
  update();
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

  {
    auto str = [](const GLubyte* v) { return v ? QString::fromLatin1(reinterpret_cast<const char*>(v)) : QString(); };
    QOpenGLFunctions* f = context()->functions();
    glInfo_ = QString("%1 — %2 (OpenGL %3)")
                  .arg(str(f->glGetString(GL_VENDOR)), str(f->glGetString(GL_RENDERER)),
                       str(f->glGetString(GL_VERSION)));
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
    if (bodyTransparency_ > 0.0) body_->SetTransparency(bodyTransparency_);
    if (bodyVisible_) context_->Display(body_, AIS_Shaded, 0, false);
    applyDisplayMode();
  }
  view_->Invalidate();
  update();
}

void Viewport::fitAll(bool animate) {
  if (view_->Window().IsNull()) {  // not shown yet: fit once GL is ready
    fitPending_ = true;
    return;
  }
  if (animate) {
    animateCamera([this] { view_->FitAll(0.1, false); });
    return;
  }
  view_->FitAll(0.1, false);
  view_->Invalidate();
  update();
}

void Viewport::mousePressEvent(QMouseEvent* e) {
  QOpenGLWidget::mousePressEvent(e);
  setFocus();
  if (tool_ && (e->button() == Qt::LeftButton || e->button() == Qt::RightButton) && tool_->mousePress(e)) return;
  if (!view_.IsNull() &&
      UpdateMouseButtons(toPoint(e->position(), devicePixelRatioF()), toVKeyMouse(e->buttons()),
                         toVKeyFlags(e->modifiers()), false)) {
    update();
  }
}

void Viewport::mouseReleaseEvent(QMouseEvent* e) {
  QOpenGLWidget::mouseReleaseEvent(e);
  if (tool_ && (e->button() == Qt::LeftButton || e->button() == Qt::RightButton) && tool_->mouseRelease(e)) return;
  if (!view_.IsNull() &&
      UpdateMouseButtons(toPoint(e->position(), devicePixelRatioF()), toVKeyMouse(e->buttons()),
                         toVKeyFlags(e->modifiers()), false)) {
    update();
  }
}

void Viewport::mouseMoveEvent(QMouseEvent* e) {
  QOpenGLWidget::mouseMoveEvent(e);
  if (tool_ && tool_->mouseMove(e) && !(e->buttons() & Qt::MiddleButton)) return;
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
  const double delta = double(e->angleDelta().y()) / 8.0 * (reverseZoom_ ? -1.0 : 1.0);
  if (UpdateZoom(Aspect_ScrollDelta(pos, delta))) {
    update();
  }
  if (tool_) tool_->viewChanged();
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* e) {
  if (tool_ && e->button() == Qt::LeftButton && tool_->mouseDoubleClick(e)) return;
  QOpenGLWidget::mouseDoubleClickEvent(e);
}

void Viewport::keyPressEvent(QKeyEvent* e) {
  if (tool_ && tool_->keyPress(e)) return;
  QOpenGLWidget::keyPressEvent(e);
}

void Viewport::setTool(ViewportTool* tool) {
  tool_ = tool;
  setCursor(tool ? Qt::CrossCursor : Qt::ArrowCursor);
  if (!tool) setupMouseGestures();
  else {
    // Left-drag belongs to the tool; keep middle-drag navigation.
    auto& map = ChangeMouseGestureMap();
    map.UnBind(Aspect_VKeyMouse_LeftButton);
  }
}

namespace {

Quantity_Color occtColor(const QColor& c) {
  return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

}  // namespace

void Viewport::clearOverlay(const QString& layer) {
  auto it = overlays_.find(layer);
  if (it == overlays_.end()) return;
  for (const auto& obj : *it) context_->Remove(obj, false);
  overlays_.erase(it);
  view_->Invalidate();
  update();
}

void Viewport::setOverlay(const QString& layer, const Overlay& o) {
  if (auto it = overlays_.find(layer); it != overlays_.end()) {
    for (const auto& obj : *it) context_->Remove(obj, false);
  }
  std::vector<Handle(AIS_InteractiveObject)>& objects = overlays_[layer];
  objects.clear();

  auto show = [&](const Handle(AIS_InteractiveObject)& obj, int mode, Graphic3d_ZLayerId z) {
    obj->SetZLayer(z);
    context_->Display(obj, mode, -1, false);  // -1: not selectable
    objects.push_back(obj);
  };

  for (const Overlay::Face& f : o.faces) {
    if (f.shape.IsNull()) continue;
    Handle(AIS_Shape) s = new AIS_Shape(f.shape);
    s->SetColor(occtColor(f.color));
    s->SetTransparency(f.transparency);
    s->Attributes()->SetFaceBoundaryDraw(false);
    show(s, AIS_Shaded, Graphic3d_ZLayerId_Topmost);
  }
  for (const Overlay::Curves& c : o.curves) {
    if (c.shape.IsNull()) continue;
    Handle(AIS_Shape) s = new AIS_Shape(c.shape);
    Handle(Prs3d_LineAspect) line =
        new Prs3d_LineAspect(occtColor(c.color), c.dashed ? Aspect_TOL_DASH : Aspect_TOL_SOLID, c.width);
    s->Attributes()->SetWireAspect(line);
    s->Attributes()->SetLineAspect(line);
    s->Attributes()->SetFreeBoundaryAspect(line);
    s->Attributes()->SetUnFreeBoundaryAspect(line);
    show(s, AIS_WireFrame, Graphic3d_ZLayerId_Topmost);
  }
  for (const Overlay::Points& p : o.points) {
    if (p.shape.IsNull()) continue;
    Handle(AIS_Shape) s = new AIS_Shape(p.shape);
    s->Attributes()->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_O_POINT, occtColor(p.color), p.size));
    show(s, AIS_WireFrame, Graphic3d_ZLayerId_Topmost);
  }
  for (const Overlay::Label& l : o.labels) {
    Handle(AIS_TextLabel) t = new AIS_TextLabel();
    t->SetText(TCollection_ExtendedString(l.text.toUtf8().constData(), Standard_True));
    t->SetPosition(l.position);
    t->SetColor(occtColor(l.color));
    t->SetHeight(l.height * devicePixelRatioF());
    t->SetFont(Font_NOF_SANS_SERIF);
    t->SetFontAspect(Font_FA_Bold);
    if (l.background.isValid()) {
      t->SetDisplayType(Aspect_TODT_SUBTITLE);
      t->SetColorSubTitle(occtColor(l.background));
    }
    t->SetHJustification(Graphic3d_HTA_CENTER);
    t->SetVJustification(Graphic3d_VTA_CENTER);
    show(t, 0, Graphic3d_ZLayerId_Topmost);
  }
  view_->Invalidate();
  update();
}

std::pair<gp_Pnt, gp_Dir> Viewport::rayAt(const QPointF& pos) const {
  const qreal dpr = devicePixelRatioF();
  double x, y, z, vx, vy, vz;
  view_->ConvertWithProj(int(pos.x() * dpr), int(pos.y() * dpr), x, y, z, vx, vy, vz);
  return {gp_Pnt(x, y, z), gp_Dir(vx, vy, vz)};
}

QPointF Viewport::project(const gp_Pnt& p) const {
  int xp = 0, yp = 0;
  view_->Convert(p.X(), p.Y(), p.Z(), xp, yp);
  const qreal dpr = devicePixelRatioF();
  return QPointF(xp / dpr, yp / dpr);
}

void Viewport::lookAlong(const gp_Dir& normal, const gp_Dir& up) {
  auto apply = [this, normal, up] {
    view_->SetProj(normal.X(), normal.Y(), normal.Z());
    view_->SetUp(up.X(), up.Y(), up.Z());
    view_->FitAll(0.25, false);
  };
  if (view_->Window().IsNull()) {
    apply();
    return;
  }
  animateCamera(apply);
}

void Viewport::setBodyTransparency(double transparency) {
  bodyTransparency_ = transparency;
  if (body_.IsNull()) return;
  body_->SetTransparency(transparency);
  context_->Redisplay(body_, false);
  view_->Invalidate();
  update();
}

void Viewport::handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                                const Handle(V3d_View)& view) {
  AIS_ViewController::handleViewRedraw(ctx, view);
  if (myToAskNextFrame) update();  // keep animating (e.g. view cube rotation)
}
