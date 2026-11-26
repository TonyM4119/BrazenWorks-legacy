// Windows only. Kept in its own file on purpose: Open CASCADE's
// Standard_Macro.hxx defines NOGDI and NOUSER on Windows, which strips
// wglGetCurrentDC and WindowFromDC out of <windows.h>. This file includes no
// OCCT header, so it sees the full API.
#include <windows.h>

// The window of the OpenGL context Qt made current for the viewport.
void* currentGlWindowHandle() { return WindowFromDC(wglGetCurrentDC()); }
