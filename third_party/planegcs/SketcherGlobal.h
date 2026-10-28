// SPDX-License-Identifier: LGPL-2.1-or-later
// Khan: replaces FreeCAD's src/Mod/Sketcher/SketcherGlobal.h. PlaneGCS's headers
// include it as "../../SketcherGlobal.h"; it only defines the export macro.
#pragma once

#if defined(_WIN32)
#  if defined(KH_PLANEGCS_BUILD)
#    define SketcherExport __declspec(dllexport)
#  else
#    define SketcherExport __declspec(dllimport)
#  endif
#else
#  define SketcherExport __attribute__((visibility("default")))
#endif
