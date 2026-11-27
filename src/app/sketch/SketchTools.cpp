#include "SketchTools.h"

#include <QObject>

using M = SketchEditor::Mode;

const std::vector<SketchToolDef>& sketchTools() {
  static const std::vector<SketchToolDef> tools = {
      // Create.
      {"sketch.line", M::Line, "line", "Line",
       "Click points; type a length (or Tab) for the next segment. Double-click or Esc stops."},
      {"sketch.rectangle", M::Rectangle, "rectangle", "2-Point Rectangle",
       "Click two corners, or type the width, Tab, the height, Enter."},
      {"sketch.rectangle3", M::Rectangle3, "rectangle3", "3-Point Rectangle",
       "Click both ends of one side, then a point for the height. Any angle."},
      {"sketch.rectanglec", M::RectangleCentre, "rectanglec", "Center Rectangle",
       "Click the centre, then a corner. Type the width, Tab, the height."},
      {"sketch.circle", M::Circle, "circle", "Center Diameter Circle",
       "Click the centre, then type the diameter and press Enter."},
      {"sketch.circle2", M::Circle2, "circle2", "2-Point Circle", "Click both ends of a diameter."},
      {"sketch.circle3", M::Circle3, "circle3", "3-Point Circle", "Click three points on the circle."},
      {"sketch.arc", M::Arc, "arc", "3-Point Arc", "Click the start, the end, then a point on the arc."},
      {"sketch.arcc", M::CentreArc, "arcc", "Center Point Arc",
       "Click the centre, the start, then the end (counter-clockwise)."},
      {"sketch.arct", M::TangentArc, "arct", "Tangent Arc",
       "Click the end of a line or arc, then where the arc ends. It leaves tangent."},
      {"sketch.polygon", M::Polygon, "polygon", "Polygon",
       "Click the centre, then a corner. Type the radius, Tab, the number of sides."},
      {"sketch.ellipse", M::Ellipse, "ellipse", "Ellipse",
       "Click the centre, the end of the major axis, then a point for the minor radius."},
      {"sketch.slot", M::Slot, "sketch-slot", "Center to Center Slot",
       "Click both arc centres, then the width. Type the width and press Enter."},
      {"sketch.spline", M::Spline, "spline", "Fit Point Spline",
       "Click points the curve passes through. Double-click or Enter ends it; click the first point to close it."},
      {"sketch.point", M::Point, "point", "Point", "Click to place points."},
      // Modify.
      {"sketch.fillet", M::Fillet, "sketch-fillet", "Fillet",
       "Click the corner where two lines meet, type the radius, Enter."},
      {"sketch.chamfer", M::Chamfer, "sketch-chamfer", "Chamfer",
       "Click the corner where two lines meet, type the distance, Enter."},
      {"sketch.trim", M::Trim, "trim", "Trim",
       "Click the piece of a curve to remove, up to where other curves cross it."},
      {"sketch.extend", M::Extend, "extend", "Extend", "Click near the end of a line or arc to extend it to the next curve."},
      {"sketch.break", M::Break, "break", "Break", "Click a curve to split it where other curves cross it."},
      {"sketch.offset", M::Offset, "offset", "Offset",
       "Click a curve (its connected chain comes along) or select first, click the side, type the distance."},
      {"sketch.mirror", M::Mirror, "mirror", "Mirror",
       "Click the curves to mirror (or select first), Enter, then click the mirror line."},
      {"sketch.move", M::Move, "move", "Move/Copy",
       "Click the curves (or select first), Enter, click a base point, then the new spot. Hold Ctrl to copy."},
      {"sketch.rotate", M::Rotate, "rotate", "Rotate",
       "Click the curves (or select first), Enter, click the centre, type the angle. Hold Ctrl to copy."},
      {"sketch.scale", M::Scale, "scale", "Sketch Scale",
       "Click the curves (or select first), Enter, click the centre, type the scale factor."},
      {"sketch.patternr", M::PatternRect, "patternr", "Rectangular Pattern",
       "Click the curves (or select first), Enter, then set the rows, columns and spacing."},
      {"sketch.patternc", M::PatternCirc, "patternc", "Circular Pattern",
       "Click the curves (or select first), Enter, click the centre, then set the count and angle."},
      {"sketch.project", M::Project, "project", "Project",
       "Click edges of the body to copy them into the sketch as fixed geometry."},
      // Other.
      {"sketch.dimension", M::Dimension, "dimension", "Sketch Dimension",
       "Pick a curve, or two points/curves; click where the dimension goes, type a value, Enter."},
      {"sketch.select", M::Select, "select", "Select",
       "Click to select, drag points or lines to move them, Delete removes. Double-click a dimension to "
       "change it; right-click one for more."},
  };
  return tools;
}

const SketchToolDef* sketchToolFor(SketchEditor::Mode mode) {
  for (const SketchToolDef& t : sketchTools())
    if (t.mode == mode) return &t;
  return nullptr;
}

const SketchToolDef* sketchToolById(const QString& id) {
  for (const SketchToolDef& t : sketchTools())
    if (id == QLatin1String(t.id)) return &t;
  return nullptr;
}
