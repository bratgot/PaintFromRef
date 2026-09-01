// Painterly.h
// Stroke synthesis engine for PaintFromRef: converts an RGB image into an
// ordered list of curved paint strokes (Hertzmann, "Painterly Rendering with
// Curved Brush Strokes of Multiple Sizes", SIGGRAPH 1998).
//
// Pure CPU code, no Nuke dependencies. ASCII only.

#ifndef PFR_PAINTERLY_H
#define PFR_PAINTERLY_H

#include <cstdint>
#include <vector>

namespace pfr {

// One output stroke. Coordinates are pixels, origin bottom-left (matches
// Nuke / RotoPaint). "diameter" maps to the RotoPaint "bs" attribute.
struct StrokeOut {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    float diameter = 1.0f;
    std::vector<float> xy;  // x0,y0,x1,y1,...
};

struct Params {
    float maxBrush = 64.0f;    // largest brush diameter, px
    float minBrush = 8.0f;     // smallest brush diameter, px
    float threshold = 0.10f;   // per-cell error threshold (linear RGB distance)
    float gridFactor = 1.0f;   // grid step = gridFactor * brush radius
    float blurFactor = 0.5f;   // reference blur sigma = blurFactor * diameter
    int maxPoints = 12;        // max control points per stroke
    int minPoints = 2;         // min control points before early-out allowed
    float curvature = 1.0f;    // 1 = follow image gradients, 0 = straight
    float opacity = 1.0f;      // stroke opacity (also used in canvas sim)
    float hardness = 0.8f;     // brush hardness 0..1 (canvas sim falloff)
    int maxStrokes = 20000;    // total stroke budget across all layers
    uint32_t seed = 1;
};

// rgb: interleaved float RGB, row-major, row 0 = BOTTOM of image.
// Returns strokes in paint order: index 0 = painted first = bottom of the
// stack (coarsest). The RotoPaint builder must append them in reverse,
// because earlier entries in a RotoPaint layer render ON TOP.
std::vector<StrokeOut> paintImage(const float* rgb, int w, int h,
                                  const Params& p);

}  // namespace pfr

#endif
