# Probe the nuke.rotopaint API in Nuke 14.1 terminal mode.
# Run:  & "C:\Program Files\Nuke14.1v8\Nuke14.1.exe" -t probe\probe_rotopaint_api.py
#
# Answers, empirically:
#   1. module surface of nuke.rotopaint
#   2. how to create a brush Stroke and append points
#   3. the real attribute names on a stroke (color, brush size, opacity, ...)
#   4. what the serialized curves knob text looks like
import sys
import traceback

import nuke
import nuke.rotopaint as rp

OUT = r"C:/dev/PaintFromRef/probe/probe_out.txt"
lines = []


def emit(s=""):
    lines.append(str(s))
    print(s)


def dump_dir(label, obj):
    emit("")
    emit("=== dir(%s) ===" % label)
    for name in dir(obj):
        if name.startswith("__"):
            continue
        try:
            attr = getattr(obj, name)
            doc = ""
            if callable(attr):
                doc = (getattr(attr, "__doc__", "") or "").strip().splitlines()
                doc = " :: " + doc[0] if doc else ""
            emit("  %s%s" % (name, doc))
        except Exception as e:
            emit("  %s  <error: %s>" % (name, e))


try:
    emit("nuke version: %s" % nuke.NUKE_VERSION_STRING)
    dump_dir("nuke.rotopaint", rp)

    n = nuke.createNode("RotoPaint", inpanel=False)
    curves = n["curves"]
    dump_dir("curvesKnob", curves)
    root = curves.rootLayer
    dump_dir("rootLayer", root)

    stroke = rp.Stroke(curves)
    dump_dir("Stroke", stroke)

    # StrokeType enumeration if present
    for cand in ("StrokeType",):
        if hasattr(rp, cand):
            dump_dir("rp.%s" % cand, getattr(rp, cand))

    # attributes
    attrs = stroke.getAttributes()
    dump_dir("AnimAttributes", attrs)
    emit("")
    emit("=== attribute enumeration ===")
    try:
        num = attrs.getNumAttributes()
        emit("numAttributes = %d" % num)
        for i in range(num):
            name = attrs.getAttributeName(i)
            try:
                val = attrs.getValue(0, name)
            except Exception as e:
                val = "<err %s>" % e
            emit("  [%02d] %-8s = %s" % (i, name, val))
    except Exception:
        emit(traceback.format_exc())

    # Append a few points, asymmetric so we can identify coordinate origin later.
    emit("")
    emit("=== building a test stroke ===")
    for x, y in ((100.0, 200.0), (300.0, 250.0), (500.0, 200.0)):
        try:
            stroke.append(rp.AnimControlPoint(x, y))
        except Exception:
            emit(traceback.format_exc())
            break
    emit("stroke built, len attempt:")
    try:
        emit("  len(stroke) API? getNumPoints/etc tried below")
    except Exception:
        pass

    # Try to set the obvious attributes; report what sticks.
    emit("")
    emit("=== setting attributes ===")
    for name, val in (
        ("r", 0.25),
        ("g", 0.5),
        ("b", 0.75),
        ("a", 1.0),
        ("bs", 42.0),      # brush size?
        ("opc", 0.8),      # opacity?
        ("bsh", 0.33),     # brush hardness?
        ("h", 0.33),
    ):
        try:
            attrs.set(name, val)
            emit("  set(%r, %s) OK" % (name, val))
        except Exception as e:
            emit("  set(%r, %s) FAILED: %s" % (name, val, e))

    root.append(stroke)

    emit("")
    emit("=== curves knob toScript ===")
    try:
        emit(curves.toScript())
    except Exception:
        emit(traceback.format_exc())

    emit("")
    emit("=== node writeKnobs ===")
    try:
        emit(n.writeKnobs(nuke.WRITE_ALL | nuke.WRITE_NON_DEFAULT_ONLY))
    except Exception:
        emit(traceback.format_exc())

except Exception:
    emit("TOP LEVEL FAILURE:")
    emit(traceback.format_exc())

with open(OUT, "w") as f:
    f.write("\n".join(lines))
print("wrote %s" % OUT)
