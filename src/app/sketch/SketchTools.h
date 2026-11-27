#pragma once

#include "SketchEditor.h"

#include <QString>

#include <vector>

// Every sketch tool that is a mode of the editor, in one table: the ribbon,
// the Sketch Palette's Feature Options and the editor's hints all read it.
struct SketchToolDef {
  const char* id;  // ribbon id, e.g. "sketch.trim"
  SketchEditor::Mode mode;
  const char* icon;
  const char* title;
  const char* summary;   // what it does (first line of the hover help)
  const char* hint;      // how to use it
  const char* shortcut;  // "" for none
  const char* family;    // tools that are variants of one another share it ("Rectangle"); "" for none
};

const std::vector<SketchToolDef>& sketchTools();
const SketchToolDef* sketchToolFor(SketchEditor::Mode mode);
const SketchToolDef* sketchToolById(const QString& id);
// The variants that share a tool's family, in menu order.
std::vector<const SketchToolDef*> sketchFamily(const QString& family);
