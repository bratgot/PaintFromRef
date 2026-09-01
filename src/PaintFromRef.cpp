// PaintFromRef.cpp
// Nuke NDK op: analyses the reference image on input 0 and generates a
// standalone RotoPaint node whose paint strokes recreate that image, so the
// original source is no longer needed.
//
// The C++ side does the heavy lifting (stroke synthesis, see Painterly.cpp)
// and writes a strokes JSON file; a small Python builder (pfr_builder.py,
// shipped next to the DLL) turns that file into a RotoPaint node via the
// documented nuke.rotopaint API.
//
// ASCII only.

#include "Painterly.h"

#include "DDImage/Iop.h"
#include "DDImage/Knob.h"
#include "DDImage/Knobs.h"
#include "DDImage/Row.h"
#include "DDImage/Tile.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace DD::Image;

static const char* const CLASS = "PaintFromRef";
static const char* const HELP =
    "Recreates the input image as paint strokes in a new, standalone "
    "RotoPaint node. Connect a reference image, pick a quality, press "
    "Create RotoPaint. The generated node carries the image format and "
    "does not need the original image any more.";

namespace {

enum QualityPreset { Q_DRAFT = 0, Q_NORMAL, Q_FINE, Q_ULTRA, Q_CUSTOM };

static const char* const kQualityNames[] = {"draft", "normal", "fine",
                                            "ultra", "custom", nullptr};

struct Preset {
    float maxBrush, minBrush, threshold;
    int maxStrokes, maxPoints;
};

// diameters in px; thresholds are linear-RGB distances
static const Preset kPresets[4] = {
    /* draft  */ {96.0f, 16.0f, 0.20f, 2000, 8},
    /* normal */ {64.0f, 8.0f, 0.10f, 8000, 12},
    /* fine   */ {64.0f, 4.0f, 0.06f, 20000, 12},
    /* ultra  */ {48.0f, 2.0f, 0.04f, 50000, 16},
};

std::string ck_str(const char* s) { return s ? std::string(s) : std::string(); }

}  // namespace

class PaintFromRef : public Iop {
    // preset + custom parameters
    int quality_;
    float max_brush_;
    float min_brush_;
    float threshold_;
    int max_strokes_;
    int max_points_;
    // style parameters (always live)
    float grid_factor_;
    float curvature_;
    float opacity_;
    float hardness_;
    int seed_;
    // io / plumbing
    const char* output_json_;
    const char* status_;
    int run_trigger_;
    int last_run_trigger_;
    bool in_action_;

public:
    PaintFromRef(Node* node)
        : Iop(node)
        , quality_(Q_NORMAL)
        , max_brush_(64.0f)
        , min_brush_(8.0f)
        , threshold_(0.10f)
        , max_strokes_(8000)
        , max_points_(12)
        , grid_factor_(1.0f)
        , curvature_(1.0f)
        , opacity_(1.0f)
        , hardness_(0.8f)
        , seed_(1)
        , output_json_("")
        , status_("(idle)")
        , run_trigger_(0)
        , last_run_trigger_(0)
        , in_action_(false) {}

    int minimum_inputs() const override { return 1; }
    int maximum_inputs() const override { return 1; }

    const char* Class() const override { return CLASS; }
    const char* node_help() const override { return HELP; }

    void _validate(bool for_real) override {
        copy_info();
        // headless / scripted trigger path (knob_changed does not fire in
        // -t mode); see NDK_NOTES 27.1
        if (for_real && run_trigger_ != last_run_trigger_ && !in_action_) {
            last_run_trigger_ = run_trigger_;
            if (run_trigger_ != 0) {
                in_action_ = true;
                do_generate(false);
                in_action_ = false;
            }
        }
    }

    void _request(int x, int y, int r, int t, ChannelMask channels,
                  int count) override {
        input0().request(x, y, r, t, channels, count);
    }

    void engine(int y, int x, int r, ChannelMask channels, Row& row) override {
        row.get(input0(), y, x, r, channels);
    }

    void knobs(Knob_Callback f) override {
        Enumeration_knob(f, &quality_, kQualityNames, "quality", "quality");
        Tooltip(f,
                "Preset controlling stroke count and brush sizes. Pick "
                "'custom' to use the values on the Advanced tab.");
        Newline(f);
        Button(f, "create_rotopaint", "Create RotoPaint");
        Tooltip(f,
                "Analyse the input image and create a standalone RotoPaint "
                "node that recreates it in paint strokes.");
        String_knob(f, &status_, "status", "status");
        SetFlags(f, Knob::NO_ANIMATION | Knob::OUTPUT_ONLY);

        Tab_knob(f, "Advanced");
        Text_knob(f,
                  "Brush values below are used when quality = custom. Style "
                  "values are always used.");
        Newline(f);
        Float_knob(f, &max_brush_, IRange(4, 256), "max_brush",
                   "max brush size");
        Tooltip(f, "Largest brush diameter in pixels (coarsest layer).");
        Float_knob(f, &min_brush_, IRange(1, 64), "min_brush",
                   "min brush size");
        Tooltip(f, "Smallest brush diameter in pixels (detail layer).");
        Float_knob(f, &threshold_, IRange(0.01, 0.5), "threshold",
                   "error threshold");
        Tooltip(f,
                "How wrong a region must be before a detail stroke is "
                "painted over it. Lower = more strokes, closer match.");
        Int_knob(f, &max_strokes_, IRange(100, 200000), "max_strokes",
                 "max strokes");
        Tooltip(f,
                "Hard cap on total strokes. Keeps the generated RotoPaint "
                "responsive; detail layers are trimmed first.");
        Int_knob(f, &max_points_, IRange(2, 32), "stroke_length",
                 "max stroke points");
        Divider(f, "style");
        Float_knob(f, &grid_factor_, IRange(0.5, 4), "grid_factor",
                   "stroke density");
        Tooltip(f,
                "Grid spacing as a multiple of the brush radius. Lower = "
                "denser strokes.");
        Float_knob(f, &curvature_, IRange(0, 1), "curvature", "curvature");
        Tooltip(f, "1 = strokes bend along image edges, 0 = straight.");
        Float_knob(f, &opacity_, IRange(0.1, 1), "opacity", "stroke opacity");
        Float_knob(f, &hardness_, IRange(0, 1), "hardness", "brush hardness");
        Int_knob(f, &seed_, "seed", "seed");
        File_knob(f, &output_json_, "output_json", "strokes json");
        SetFlags(f, Knob::NO_ANIMATION);
        Tooltip(f,
                "Optional: also write the raw strokes file here (used by "
                "the test harness). Leave empty for normal use.");

        // hidden scripting trigger; bump it and cook the node to generate
        // headless (see NDK_NOTES 27.1)
        Int_knob(f, &run_trigger_, "run_trigger", "run trigger");
        SetFlags(f, Knob::INVISIBLE | Knob::DO_NOT_WRITE |
                        Knob::NO_ANIMATION);
    }

    int knob_changed(Knob* k) override {
        try {
            if (k->is("create_rotopaint")) {
                in_action_ = true;
                do_generate(true);
                in_action_ = false;
                return 1;
            }
            if (k->is("quality") && quality_ != Q_CUSTOM) {
                // reflect the preset into the custom knobs so the user can
                // see (and fork from) the values in use
                const Preset& ps = kPresets[quality_];
                if (Knob* kb = knob("max_brush")) kb->set_value(ps.maxBrush);
                if (Knob* kb = knob("min_brush")) kb->set_value(ps.minBrush);
                if (Knob* kb = knob("threshold")) kb->set_value(ps.threshold);
                if (Knob* kb = knob("max_strokes"))
                    kb->set_value(ps.maxStrokes);
                if (Knob* kb = knob("stroke_length"))
                    kb->set_value(ps.maxPoints);
                return 1;
            }
        } catch (const std::exception& e) {
            in_action_ = false;
            error("PaintFromRef: %s", e.what());
        } catch (...) {
            in_action_ = false;
            error("PaintFromRef: unknown exception");
        }
        return Iop::knob_changed(k);
    }

private:
    // status updates are only legal from the knob_changed (GUI) path; from
    // _validate we log to stderr instead (see NDK_NOTES 27.2)
    void set_status(bool gui, const char* msg) {
        if (!gui) {
            std::fprintf(stderr, "[PaintFromRef] %s\n", msg ? msg : "");
            std::fflush(stderr);
            return;
        }
        if (Knob* k = knob("status")) k->set_text(msg ? msg : "");
    }

    pfr::Params params_from_knobs() const {
        pfr::Params p;
        if (quality_ != Q_CUSTOM) {
            const Preset& ps = kPresets[quality_ < 0 ? 0 : quality_];
            p.maxBrush = ps.maxBrush;
            p.minBrush = ps.minBrush;
            p.threshold = ps.threshold;
            p.maxStrokes = ps.maxStrokes;
            p.maxPoints = ps.maxPoints;
        } else {
            p.maxBrush = max_brush_;
            p.minBrush = min_brush_;
            p.threshold = threshold_;
            p.maxStrokes = max_strokes_;
            p.maxPoints = max_points_;
        }
        p.gridFactor = grid_factor_;
        p.curvature = curvature_;
        p.opacity = opacity_;
        p.hardness = hardness_;
        p.seed = (uint32_t)(seed_ <= 0 ? 1 : seed_);
        return p;
    }

    std::string json_output_path() const {
        std::string user = ck_str(output_json_);
        // strip whitespace Nuke sometimes appends to File_knob values
        while (!user.empty() &&
               (user.back() == '\n' || user.back() == '\r' ||
                user.back() == ' '))
            user.pop_back();
        if (!user.empty()) return user;
        std::error_code ec;
        std::filesystem::path dir =
            std::filesystem::temp_directory_path(ec) / "PaintFromRef";
        std::filesystem::create_directories(dir, ec);
        const auto ticks = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        char name[64];
        std::snprintf(name, sizeof(name), "strokes_%lld.json",
                      (long long)ticks);
        return (dir / name).generic_string();
    }

    void do_generate(bool gui) {
        try {
            do_generate_impl(gui);
        } catch (const std::exception& e) {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "FAILED: %s", e.what());
            set_status(gui, buf);
        } catch (...) {
            set_status(gui, "FAILED: unknown exception");
        }
    }

    void do_generate_impl(bool gui) {
        if (!input(0)) {
            set_status(gui, "FAILED: no input image connected");
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        set_status(gui, "analysing...");

        Iop& in = input0();
        in.validate(true);
        const Format& fmt = in.format();
        const int x0 = fmt.x(), y0 = fmt.y();
        const int w = fmt.w(), h = fmt.h();
        if (w <= 0 || h <= 0) {
            set_status(gui, "FAILED: input has no format");
            return;
        }

        // The input's data window (bbox) may be smaller than the format
        // (e.g. a Crop with reformat off). Only the intersection is backed
        // by pixels; requesting or reading beyond it fails. Everything
        // outside the bbox is black, which the zero-filled buffer provides.
        Box fetch(x0, y0, fmt.r(), fmt.t());
        fetch.intersect(in.info());
        if (fetch.r() <= fetch.x() || fetch.t() <= fetch.y()) {
            set_status(gui,
                       "FAILED: input bbox does not overlap the format");
            return;
        }

        in.request(fetch.x(), fetch.y(), fetch.r(), fetch.t(), Mask_RGB, 1);
        Tile tile(in, fetch.x(), fetch.y(), fetch.r(), fetch.t(), Mask_RGB,
                  true);
        if (aborted()) {
            set_status(gui, "aborted");
            return;
        }

        std::vector<float> rgb((size_t)w * h * 3, 0.0f);
        static const Channel chans[3] = {Chan_Red, Chan_Green, Chan_Blue};
        for (int c = 0; c < 3; ++c) {
            for (int y = fetch.y(); y < fetch.t(); ++y) {
                Tile::RowPtr row = tile[chans[c]][y];
                if (!row) continue;  // channel missing on input: leave 0
                float* dst =
                    &rgb[((size_t)(y - y0) * w + (fetch.x() - x0)) * 3 + c];
                for (int x = fetch.x(); x < fetch.r(); ++x, dst += 3)
                    *dst = row[x];
            }
        }

        const pfr::Params params = params_from_knobs();
        const std::vector<pfr::StrokeOut> strokes =
            pfr::paintImage(rgb.data(), w, h, params);
        if (strokes.empty()) {
            set_status(gui, "FAILED: no strokes generated");
            return;
        }

        const std::string path = json_output_path();
        write_strokes_json(path, strokes, fmt, params);

        const auto t1 = std::chrono::steady_clock::now();
        const double secs =
            std::chrono::duration<double>(t1 - t0).count();

        if (gui) {
            // build the RotoPaint right away via the bundled python module
            std::string cmd = "__import__('pfr_builder').build('" + path +
                              "','" + node_name() + "')";
            const bool ok = script_command(cmd.c_str(), true, true);
            std::string result = ck_str(script_result(true));
            script_unlock();
            char buf[512];
            if (ok) {
                std::snprintf(buf, sizeof(buf),
                              "created %s with %d strokes in %.1fs",
                              result.c_str(), (int)strokes.size(), secs);
            } else {
                std::snprintf(buf, sizeof(buf), "builder FAILED: %s",
                              result.c_str());
            }
            set_status(gui, buf);
        } else {
            char buf[600];
            std::snprintf(buf, sizeof(buf),
                          "wrote %d strokes to %s in %.1fs",
                          (int)strokes.size(), path.c_str(), secs);
            set_status(gui, buf);
        }
    }

    void write_strokes_json(const std::string& path,
                            const std::vector<pfr::StrokeOut>& strokes,
                            const Format& fmt, const pfr::Params& params) {
        std::ofstream os(path, std::ios::binary);
        if (!os)
            throw std::runtime_error("cannot write " + path);
        char buf[128];
        os << "{\"width\":" << fmt.w() << ",\"height\":" << fmt.h()
           << ",\"pixel_aspect\":" << fmt.pixel_aspect()
           << ",\"strokes\":[";
        const float ox = (float)fmt.x(), oy = (float)fmt.y();
        bool firstStroke = true;
        for (const pfr::StrokeOut& s : strokes) {
            if (!firstStroke) os << ",";
            firstStroke = false;
            std::snprintf(buf, sizeof(buf),
                          "{\"c\":[%.5g,%.5g,%.5g],\"bs\":%.4g,\"o\":%.4g,"
                          "\"h\":%.4g,\"p\":[",
                          s.r, s.g, s.b, s.diameter, params.opacity,
                          params.hardness);
            os << buf;
            for (size_t i = 0; i < s.xy.size(); i += 2) {
                std::snprintf(buf, sizeof(buf), "%s%.2f,%.2f",
                              i ? "," : "", s.xy[i] + ox, s.xy[i + 1] + oy);
                os << buf;
            }
            os << "]}";
        }
        os << "]}\n";
        if (!os.good())
            throw std::runtime_error("write failed: " + path);
    }

    static const Iop::Description description;
};

static Iop* build(Node* node) { return new PaintFromRef(node); }
const Iop::Description PaintFromRef::description(CLASS, "Draw/PaintFromRef",
                                                 build);
