#pragma once

#include "SketchEditor.h"

#include <QString>

#include <vector>

// Every sketch tool that is a mode of the editor, in one table: the ribbon,
// the Sketch Palette and the editor's hints all read it.
struct SketchToolDef {
  const char* id;  // ribbon / palette id, e.g. "sketch.trim"
  SketchEditor::Mode mode;
  const char* icon;
  const char* title;
  const char* hint;
};

const std::vector<SketchToolDef>& sketchTools();
const SketchToolDef* sketchToolFor(SketchEditor::Mode mode);
const SketchToolDef* sketchToolById(const QString& id);
