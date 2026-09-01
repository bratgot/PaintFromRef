// Painterly.cpp
// See Painterly.h. ASCII only.

#include "Painterly.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

namespace pfr {
namespace {

const float kBigError = 1.0e9f;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9e3779b9u) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// --- separable box blur x3 as a gaussian approximation ------------------

void boxBlurPass(const std::vector<float>& src, std::vector<float>& dst,
                 int w, int h, int c, int radius, bool horizontal) {
    if (radius <= 0) {
        dst = src;
        return;
    }
    const float inv = 1.0f / (2 * radius + 1);
    if (horizontal) {
        for (int y = 0; y < h; ++y) {
            const float* row = &src[(size_t)y * w * c];
            float* out = &dst[(size_t)y * w * c];
            for (int ch = 0; ch < c; ++ch) {
                float acc = 0.0f;
                for (int i = -radius; i <= radius; ++i)
                    acc += row[clampi(i, 0, w - 1) * c + ch];
                for (int x = 0; x < w; ++x) {
                    out[x * c + ch] = acc * inv;
                    const int add = clampi(x + radius + 1, 0, w - 1);
                    const int sub = clampi(x - radius, 0, w - 1);
                    acc += row[add * c + ch] - row[sub * c + ch];
                }
            }
        }
    } else {
        for (int x = 0; x < w; ++x) {
            for (int ch = 0; ch < c; ++ch) {
                float acc = 0.0f;
                for (int i = -radius; i <= radius; ++i)
                    acc += src[(size_t)clampi(i, 0, h - 1) * w * c + x * c + ch];
                for (int y = 0; y < h; ++y) {
                    dst[(size_t)y * w * c + x * c + ch] = acc * inv;
                    const int add = clampi(y + radius + 1, 0, h - 1);
                    const int sub = clampi(y - radius, 0, h - 1);
                    acc += src[(size_t)add * w * c + x * c + ch] -
                           src[(size_t)sub * w * c + x * c + ch];
                }
            }
        }
    }
}

// Three box radii approximating a gaussian of the given sigma.
void boxesForGauss(float sigma, int radii[3]) {
    const int n = 3;
    float wIdeal = std::sqrt((12.0f * sigma * sigma / n) + 1.0f);
    int wl = (int)std::floor(wIdeal);
    if (wl % 2 == 0) wl--;
    if (wl < 1) wl = 1;
    const int wu = wl + 2;
    float mIdeal =
        (12.0f * sigma * sigma - n * wl * wl - 4.0f * n * wl - 3.0f * n) /
        (-4.0f * wl - 4.0f);
    int m = (int)std::round(mIdeal);
    m = clampi(m, 0, n);
    for (int i = 0; i < n; ++i) radii[i] = ((i < m ? wl : wu) - 1) / 2;
}

void gaussianBlur(const std::vector<float>& src, std::vector<float>& dst,
                  int w, int h, int c, float sigma) {
    if (sigma < 0.3f) {
        dst = src;
        return;
    }
    int radii[3];
    boxesForGauss(sigma, radii);
    std::vector<float> tmp((size_t)w * h * c);
    dst = src;
    for (int i = 0; i < 3; ++i) {
        boxBlurPass(dst, tmp, w, h, c, radii[i], true);
        boxBlurPass(tmp, dst, w, h, c, radii[i], false);
    }
}

// --- helpers over interleaved RGB buffers -------------------------------

inline const float* px(const std::vector<float>& buf, int w, int x, int y) {
    return &buf[((size_t)y * w + x) * 3];
}

inline float colorDist(const float* a, const float* b) {
    const float dr = a[0] - b[0];
    const float dg = a[1] - b[1];
    const float db = a[2] - b[2];
    return std::sqrt(dr * dr + dg * dg + db * db);
}

struct Canvas {
    std::vector<float> rgb;  // display color of what has been painted
    std::vector<float> cov;  // accumulated coverage 0..1
    int w = 0, h = 0;
    void init(int w_, int h_) {
        w = w_;
        h = h_;
        rgb.assign((size_t)w * h * 3, 0.0f);
        cov.assign((size_t)w * h, 0.0f);
    }
    // Error between the canvas and a reference pixel. Unpainted areas are
    // "infinitely wrong" so the first layer covers everything.
    float error(const std::vector<float>& ref, int x, int y) const {
        const size_t i = (size_t)y * w + x;
        if (cov[i] < 0.5f) return kBigError;
        return colorDist(&rgb[i * 3], px(ref, w, x, y));
    }
};

struct Candidate {
    float err = 0.0f;
    StrokeOut st;
};

// Build one curved stroke starting at (x0, y0). Follows normals to the
// luminance gradient of the blurred reference (Hertzmann sec. 3).
StrokeOut makeStroke(int x0, int y0, float radius,
                     const std::vector<float>& ref,
                     const std::vector<float>& lum, const Canvas& canvas,
                     int w, int h, const Params& p) {
    StrokeOut st;
    const float* c0 = px(ref, w, x0, y0);
    st.r = c0[0];
    st.g = c0[1];
    st.b = c0[2];
    st.diameter = radius * 2.0f;

    float x = (float)x0, y = (float)y0;
    float lastDx = 0.0f, lastDy = 0.0f;
    st.xy.push_back(x);
    st.xy.push_back(y);

    const float color[3] = {st.r, st.g, st.b};
    for (int i = 1; i < p.maxPoints; ++i) {
        const int xi = clampi((int)std::lround(x), 0, w - 1);
        const int yi = clampi((int)std::lround(y), 0, h - 1);

        // stop when the canvas already matches the reference better than
        // this stroke's color would
        if (i > p.minPoints) {
            const float refErr = canvas.error(ref, xi, yi);
            if (refErr < colorDist(px(ref, w, xi, yi), color)) break;
        }

        // luminance gradient (central differences on the blurred image)
        const int xm = clampi(xi - 1, 0, w - 1), xp = clampi(xi + 1, 0, w - 1);
        const int ym = clampi(yi - 1, 0, h - 1), yp = clampi(yi + 1, 0, h - 1);
        float gx = (lum[(size_t)yi * w + xp] - lum[(size_t)yi * w + xm]) * 0.5f;
        float gy = (lum[(size_t)yp * w + xi] - lum[(size_t)ym * w + xi]) * 0.5f;
        const float gmag = std::sqrt(gx * gx + gy * gy);

        float dx, dy;
        if (gmag < 1.0e-5f) {
            if (i == 1) break;  // flat area: keep the dab, no direction
            dx = lastDx;
            dy = lastDy;
        } else {
            dx = -gy / gmag;  // normal to the gradient = along the edge
            dy = gx / gmag;
            if (lastDx * dx + lastDy * dy < 0.0f) {
                dx = -dx;
                dy = -dy;
            }
            // curvature filter
            dx = p.curvature * dx + (1.0f - p.curvature) * lastDx;
            dy = p.curvature * dy + (1.0f - p.curvature) * lastDy;
            const float dmag = std::sqrt(dx * dx + dy * dy);
            if (dmag < 1.0e-5f) break;
            dx /= dmag;
            dy /= dmag;
        }

        x += radius * dx;
        y += radius * dy;
        if (x < -radius || y < -radius || x > w + radius || y > h + radius)
            break;
        lastDx = dx;
        lastDy = dy;
        st.xy.push_back(x);
        st.xy.push_back(y);
    }

    // RotoPaint needs at least 2 control points to draw anything; turn a
    // lone dab into a minimal two-point stroke.
    if (st.xy.size() == 2) {
        st.xy.push_back(st.xy[0] + radius * 0.5f);
        st.xy.push_back(st.xy[1]);
    }
    return st;
}

// Rasterize a stroke into the canvas simulation: coverage mask first (max
// over stamps, so opacity is applied once), then a single composite.
void renderStroke(const StrokeOut& st, Canvas& canvas, const Params& p,
                  std::vector<float>& scratch) {
    const float radius = st.diameter * 0.5f;
    const int w = canvas.w, h = canvas.h;

    float minX = 1.0e9f, minY = 1.0e9f, maxX = -1.0e9f, maxY = -1.0e9f;
    for (size_t i = 0; i < st.xy.size(); i += 2) {
        minX = std::min(minX, st.xy[i]);
        maxX = std::max(maxX, st.xy[i]);
        minY = std::min(minY, st.xy[i + 1]);
        maxY = std::max(maxY, st.xy[i + 1]);
    }
    const int bx = clampi((int)std::floor(minX - radius) - 1, 0, w - 1);
    const int by = clampi((int)std::floor(minY - radius) - 1, 0, h - 1);
    const int br = clampi((int)std::ceil(maxX + radius) + 1, 0, w - 1);
    const int bt = clampi((int)std::ceil(maxY + radius) + 1, 0, h - 1);
    const int bw = br - bx + 1, bh = bt - by + 1;
    if (bw <= 0 || bh <= 0) return;

    scratch.assign((size_t)bw * bh, 0.0f);
    const float hardR = radius * clampf(p.hardness, 0.0f, 1.0f);
    const float softW = std::max(radius - hardR, 0.5f);

    auto stamp = [&](float cx, float cy) {
        const int sx = clampi((int)std::floor(cx - radius), bx, br);
        const int ex = clampi((int)std::ceil(cx + radius), bx, br);
        const int sy = clampi((int)std::floor(cy - radius), by, bt);
        const int ey = clampi((int)std::ceil(cy + radius), by, bt);
        for (int yy = sy; yy <= ey; ++yy) {
            for (int xx = sx; xx <= ex; ++xx) {
                const float d = std::sqrt((xx - cx) * (xx - cx) +
                                          (yy - cy) * (yy - cy));
                float a;
                if (d <= hardR)
                    a = 1.0f;
                else if (d >= radius)
                    a = 0.0f;
                else
                    a = (radius - d) / softW;
                float& m = scratch[(size_t)(yy - by) * bw + (xx - bx)];
                if (a > m) m = a;
            }
        }
    };

    const float step = std::max(radius * 0.35f, 0.75f);
    stamp(st.xy[0], st.xy[1]);
    for (size_t i = 2; i < st.xy.size(); i += 2) {
        const float x0 = st.xy[i - 2], y0 = st.xy[i - 1];
        const float x1 = st.xy[i], y1 = st.xy[i + 1];
        const float len = std::sqrt((x1 - x0) * (x1 - x0) +
                                    (y1 - y0) * (y1 - y0));
        const int n = std::max(1, (int)std::ceil(len / step));
        for (int k = 1; k <= n; ++k) {
            const float t = (float)k / n;
            stamp(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
        }
    }

    for (int yy = by; yy <= bt; ++yy) {
        for (int xx = bx; xx <= br; ++xx) {
            const float a =
                scratch[(size_t)(yy - by) * bw + (xx - bx)] * p.opacity;
            if (a <= 0.0f) continue;
            const size_t i = (size_t)yy * w + xx;
            canvas.rgb[i * 3 + 0] += (st.r - canvas.rgb[i * 3 + 0]) * a;
            canvas.rgb[i * 3 + 1] += (st.g - canvas.rgb[i * 3 + 1]) * a;
            canvas.rgb[i * 3 + 2] += (st.b - canvas.rgb[i * 3 + 2]) * a;
            canvas.cov[i] += (1.0f - canvas.cov[i]) * a;
        }
    }
}

}  // namespace

std::vector<StrokeOut> paintImage(const float* rgb, int w, int h,
                                  const Params& p) {
    std::vector<StrokeOut> out;
    if (w <= 0 || h <= 0 || !rgb) return out;

    std::vector<float> src(rgb, rgb + (size_t)w * h * 3);

    // brush diameters, halving from max down to min (max 8 layers)
    std::vector<float> diameters;
    for (float d = std::max(p.maxBrush, 1.0f);
         d >= std::max(p.minBrush, 1.0f) * 0.999f && diameters.size() < 8;
         d *= 0.5f)
        diameters.push_back(d);
    if (diameters.empty()) diameters.push_back(std::max(p.maxBrush, 1.0f));

    Canvas canvas;
    canvas.init(w, h);
    Rng rng(p.seed);
    std::vector<float> scratch;
    std::vector<float> ref, lum((size_t)w * h);
    int budget = std::max(p.maxStrokes, 1);
    bool firstLayer = true;

    for (const float diameter : diameters) {
        if (budget <= 0) break;
        const float radius = diameter * 0.5f;

        gaussianBlur(src, ref, w, h, 3, p.blurFactor * diameter);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const float* c = px(ref, w, x, y);
                lum[(size_t)y * w + x] =
                    0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
            }

        const int step = std::max(1, (int)std::lround(p.gridFactor * radius));
        std::vector<Candidate> cands;

        for (int gy = 0; gy < h; gy += step) {
            const int cellT = std::min(gy + step, h);
            for (int gx = 0; gx < w; gx += step) {
                const int cellR = std::min(gx + step, w);
                float sum = 0.0f;
                float best = -1.0f;
                int bestX = gx, bestY = gy;
                for (int y = gy; y < cellT; ++y)
                    for (int x = gx; x < cellR; ++x) {
                        float e = canvas.error(ref, x, y);
                        if (e >= kBigError) e = 2.0f;  // unpainted: big, finite
                        sum += e;
                        if (e > best) {
                            best = e;
                            bestX = x;
                            bestY = y;
                        }
                    }
                const float areaErr =
                    sum / ((cellT - gy) * (cellR - gx));
                if (firstLayer || areaErr > p.threshold) {
                    Candidate c;
                    c.err = areaErr;
                    c.st = makeStroke(bestX, bestY, radius, ref, lum, canvas,
                                      w, h, p);
                    cands.push_back(std::move(c));
                }
            }
        }

        // keep the worst-error strokes if over budget, then shuffle so the
        // paint order has no directional bias
        if ((int)cands.size() > budget) {
            std::sort(cands.begin(), cands.end(),
                      [](const Candidate& a, const Candidate& b) {
                          return a.err > b.err;
                      });
            cands.resize(budget);
        }
        for (size_t i = cands.size(); i > 1; --i)
            std::swap(cands[i - 1], cands[rng.below((uint32_t)i)]);

        for (Candidate& c : cands) {
            renderStroke(c.st, canvas, p, scratch);
            out.push_back(std::move(c.st));
        }
        budget -= (int)cands.size();
        firstLayer = false;
    }
    return out;
}

}  // namespace pfr
