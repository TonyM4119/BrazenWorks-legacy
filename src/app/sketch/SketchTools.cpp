#include "SketchTools.h"

using M = SketchEditor::Mode;

const std::vector<SketchToolDef>& sketchTools() {
  static const std::vector<SketchToolDef> tools = {
      // Create.
      {"sketch.line", M::Line, "line", "Line", "Creates lines.",
       "Click a start point and an end point for each segment; type a length (Tab for the angle). Double-click or "
       "Esc to stop.",
       "L", ""},
      {"sketch.midline", M::MidLine, "midline", "Midpoint Line", "Creates a line from its midpoint.",
       "Click the midpoint, then one end; the other end mirrors it. Type the length and press Enter.", "", ""},
      {"sketch.rectangle", M::Rectangle, "rectangle", "2-Point Rectangle",
       "Creates a rectangle from two opposite corners.",
       "Click one corner, then the opposite corner. Type the width, Tab, the height, Enter.", "R", "Rectangle"},
      {"sketch.rectangle3", M::Rectangle3, "rectangle3", "3-Point Rectangle",
       "Creates a rectangle at any angle from one side and its height.",
       "Click both ends of one side, then a point that sets the height.", "", "Rectangle"},
      {"sketch.rectanglec", M::RectangleCentre, "rectanglec", "Center Rectangle",
       "Creates a rectangle from its centre and a corner.",
       "Click the centre, then a corner. Type the width, Tab, the height, Enter.", "", "Rectangle"},
      {"sketch.circle", M::Circle, "circle", "Center Diameter Circle", "Creates a circle from its centre and diameter.",
       "Click the centre, then a point on the circle, or type the diameter and press Enter.", "C", "Circle"},
      {"sketch.circle2", M::Circle2, "circle2", "2-Point Circle", "Creates a circle through two points on a diameter.",
       "Click both ends of a diameter.", "", "Circle"},
      {"sketch.circle3", M::Circle3, "circle3", "3-Point Circle", "Creates a circle through three points.",
       "Click three points on the circle.", "", "Circle"},
      {"sketch.arc", M::Arc, "arc", "3-Point Arc", "Creates an arc through three points.",
       "Click the start, the end, then a point the arc passes through.", "", "Arc"},
      {"sketch.arcc", M::CentreArc, "arcc", "Center Point Arc", "Creates an arc from its centre, start and end.",
       "Click the centre, the start, then the end (counter-clockwise).", "", "Arc"},
      {"sketch.arct", M::TangentArc, "arct", "Tangent Arc", "Creates an arc tangent to the end of a line or arc.",
       "Click the end of a line or arc, then where the arc ends.", "", "Arc"},
      {"sketch.polygonc", M::PolygonCirc, "polygonc", "Circumscribed Polygon",
       "Creates a regular polygon around a circle: you set the distance to the middle of a side.",
       "Click the centre, then the middle of a side. Type the distance, Tab, the number of sides.", "", "Polygon"},
      {"sketch.polygon", M::Polygon, "polygon", "Inscribed Polygon",
       "Creates a regular polygon inside a circle: you set the distance to a corner.",
       "Click the centre, then a corner. Type the radius, Tab, the number of sides.", "", "Polygon"},
      {"sketch.polygone", M::PolygonEdge, "polygone", "Edge Polygon", "Creates a regular polygon from one of its sides.",
       "Click both ends of one side, then the side the polygon goes on. Tab sets the number of sides.", "", "Polygon"},
      {"sketch.ellipse", M::Ellipse, "ellipse", "Ellipse", "Creates an ellipse.",
       "Click the centre, the end of the major axis, then a point that sets the minor radius.", "", ""},
      {"sketch.slot", M::Slot, "sketch-slot", "Center to Center Slot",
       "Creates a slot from the centres of its two end arcs.",
       "Click both arc centres, then the width (or type it and press Enter).", "", "Slot"},
      {"sketch.sloto", M::SlotOverall, "sloto", "Overall Slot", "Creates a slot from its overall length.",
       "Click both ends of the slot, then the width (or type it and press Enter).", "", "Slot"},
      {"sketch.slotc", M::SlotCentre, "slotc", "Center Point Slot", "Creates a slot from its centre.",
       "Click the slot's centre, then one arc centre, then the width.", "", "Slot"},
      {"sketch.spline", M::Spline, "spline", "Fit Point Spline", "Creates a smooth curve through points you pick.",
       "Click the points the curve passes through. Double-click or Enter ends it; click the first point to close it.",
       "", "Spline"},
      {"sketch.point", M::Point, "point", "Point", "Creates sketch points.", "Click to place points.", "", ""},
      {"sketch.mirror", M::Mirror, "mirror", "Mirror", "Mirrors curves across a line, keeping them symmetric.",
       "Pick the curves (or select them first), press Enter, then click the mirror line.", "", ""},
      {"sketch.patternc", M::PatternCirc, "patternc", "Circular Pattern", "Copies curves around a centre point.",
       "Pick the curves (or select them first), press Enter, click the centre, then set the quantity and angle.", "",
       ""},
      {"sketch.patternr", M::PatternRect, "patternr", "Rectangular Pattern", "Copies curves in rows and columns.",
       "Pick the curves (or select them first), press Enter, then set the rows, columns and spacing.", "", ""},
      {"sketch.project", M::Project, "project", "Project", "Copies edges of bodies into the sketch.",
       "Click edges of the body. The copies are fixed in place.", "P", ""},
      {"sketch.dimension", M::Dimension, "dimension", "Sketch Dimension",
       "Sizes and positions sketch geometry with dimensions.",
       "Pick a curve, or two points or curves, click where the dimension goes, type a value and press Enter.", "D",
       ""},
      // Modify.
      {"sketch.fillet", M::Fillet, "sketch-fillet", "Fillet", "Rounds a corner where two lines meet.",
       "Click the corner, type the radius and press Enter.", "", ""},
      {"sketch.chamfer", M::Chamfer, "sketch-chamfer", "Equal Distance Chamfer",
       "Cuts a corner where two lines meet, the same distance along each.",
       "Click the corner, type the distance and press Enter.", "", ""},
      {"sketch.offset", M::Offset, "offset", "Offset", "Creates a copy of curves at a set distance.",
       "Click a curve (its connected chain comes along) or select first, click the side, type the distance.", "O",
       ""},
      {"sketch.trim", M::Trim, "trim", "Trim", "Removes the piece of a curve between the curves that cross it.",
       "Click the piece to remove.", "T", ""},
      {"sketch.extend", M::Extend, "extend", "Extend", "Lengthens a line or arc to the next curve.",
       "Click near the end to extend.", "", ""},
      {"sketch.break", M::Break, "break", "Break", "Splits a curve where other curves cross it.",
       "Click the curve to split.", "", ""},
      {"sketch.scale", M::Scale, "scale", "Sketch Scale", "Scales curves about a point.",
       "Pick the curves (or select them first), press Enter, click the point to scale about, type the factor.", "",
       ""},
      {"sketch.move", M::Move, "move", "Move/Copy", "Moves or copies curves.",
       "Pick the curves (or select them first), press Enter, click a base point, then the new spot. Hold Ctrl to "
       "copy.",
       "M", "Move/Copy"},
      {"sketch.rotate", M::Rotate, "rotate", "Rotate", "Turns curves about a point.",
       "Pick the curves (or select them first), press Enter, click the centre, type the angle. Hold Ctrl to copy.", "",
       "Move/Copy"},
      // Other.
      {"sketch.select", M::Select, "select", "Select", "Selects sketch geometry.",
       "Click to select, drag in empty space for a box (left to right: inside; right to left: touching). Drag "
       "points or lines to move them; Delete removes. Double-click a dimension to change it; right-click for more.",
       "", ""},
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

std::vector<const SketchToolDef*> sketchFamily(const QString& family) {
  std::vector<const SketchToolDef*> out;
  if (family.isEmpty()) return out;
  for (const SketchToolDef& t : sketchTools())
    if (family == QLatin1String(t.family)) out.push_back(&t);
  return out;
}
