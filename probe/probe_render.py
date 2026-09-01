# Probe 2: rendering behavior of generated RotoPaint strokes (Nuke 14.1 -ti).
# Answers:
#   1. does a disconnected RotoPaint have a 'format' knob (standalone resolution)?
#   2. coordinate origin of AnimControlPoint (bottom-left vs top-left)
#   3. is stroke color linear (set r=0.25 -> sample 0.25)?
#   4. z-order: does a later-appended stroke render on top?
#   5. lifetime: is a default stroke visible at all frames?
#   6. what does hardness falloff look like at the stroke edge?
import traceback

import nuke
import nuke.rotopaint as rp

OUT = r"C:/dev/PaintFromRef/probe/probe_render_out.txt"
lines = []


def emit(s=""):
    lines.append(str(s))
    print(s)


def add_stroke(curves, pts, r, g, b, size, opc=1.0, hardness=1.0):
    s = rp.Stroke(curves)
    for x, y in pts:
        s.append(rp.AnimControlPoint(x, y))
    a = s.getAttributes()
    a.set("r", r)
    a.set("g", g)
    a.set("b", b)
    a.set("a", 1.0)
    a.set("bs", size)
    a.set("opc", opc)
    a.set("h", hardness)
    a.set("bsh", hardness)
    curves.rootLayer.append(s)
    return s


try:
    emit("nuke version: %s" % nuke.NUKE_VERSION_STRING)

    n = nuke.createNode("RotoPaint", inpanel=False)

    # 1. format knob?
    emit("has 'format' knob: %s" % ("format" in n.knobs()))
    fmt_knob_names = [k for k in n.knobs() if "format" in k.lower()]
    emit("knobs containing 'format': %s" % fmt_knob_names)

    # try setting a custom format
    try:
        f = nuke.addFormat("640 480 0 0 640 480 1 pfr_probe")
        n["format"].setValue("pfr_probe")
        emit("set format OK -> %s" % n.format().name())
        emit("node width x height: %d x %d" % (n.width(), n.height()))
    except Exception:
        emit("format set FAILED:\n" + traceback.format_exc())

    curves = n["curves"]

    # 2/3. red horizontal stroke low in the image: y=100 if origin is bottom-left
    add_stroke(curves, [(100, 100), (300, 100)], 0.25, 0.0, 0.0, 40.0)

    # 4. z-order test at x~500: blue first, then green on same spot
    add_stroke(curves, [(450, 300), (550, 300)], 0.0, 0.0, 1.0, 40.0)
    add_stroke(curves, [(450, 300), (550, 300)], 0.0, 1.0, 0.0, 40.0)

    def sample(x, y):
        return (
            n.sample("red", x, y),
            n.sample("green", x, y),
            n.sample("blue", x, y),
            n.sample("alpha", x, y),
        )

    emit("")
    emit("=== samples at frame %s ===" % nuke.frame())
    emit("(200,100) on red stroke if origin bottom-left: %s %s %s %s" % sample(200, 100))
    emit("(200,380) on red stroke if origin top-left:    %s %s %s %s" % sample(200, 380))
    emit("(500,300) z-order test (green wins if later=top): %s %s %s %s" % sample(500, 300))
    emit("(600,50) empty area: %s %s %s %s" % sample(600, 50))

    # 6. hardness falloff: sample across the red stroke edge (bs=40 -> radius 20)
    emit("")
    emit("=== edge falloff (red stroke centered y=100, hardness=1.0) ===")
    for dy in (0, 5, 10, 15, 18, 20, 22, 25):
        r, g, b, a = sample(200, 100 + dy)
        emit("  dy=%2d  r=%.4f a=%.4f" % (dy, r, a))

    # 5. lifetime: sample at other frames
    emit("")
    emit("=== lifetime across frames ===")
    for frame in (1, 25, 100):
        try:
            nuke.frame(frame)
            r, g, b, a = sample(200, 100)
            emit("  frame %3d: r=%.4f a=%.4f" % (frame, r))
        except Exception:
            emit("  frame %d sample FAILED:\n%s" % (frame, traceback.format_exc()))

    # what lifetime-ish attributes exist on the stroke by default?
    emit("")
    emit("=== serialise of one stroke (default lifetime encoding) ===")
    st = add_stroke(curves, [(50, 400), (80, 400)], 1.0, 1.0, 1.0, 10.0)
    try:
        emit(st.serialise())
    except Exception:
        emit(traceback.format_exc())

except Exception:
    emit("TOP LEVEL FAILURE:")
    emit(traceback.format_exc())

with open(OUT, "w") as fh:
    fh.write("\n".join(lines))
print("wrote %s" % OUT)
