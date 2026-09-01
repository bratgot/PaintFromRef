# pfr_builder.py
# Builds a standalone RotoPaint node from a strokes JSON file written by the
# PaintFromRef NDK op. Lives next to PaintFromRef.dll on the Nuke plugin
# path, so `import pfr_builder` works from the Script Editor and from the
# op's C++ code.
#
# Strokes are authored by generating the curves knob's serialized text and
# loading it with a single fromScript() call. The documented per-stroke
# nuke.rotopaint API was measured at 71.6s for 1122 strokes; fromScript
# loads 8000 strokes in ~1.2s (see probe/probe_fromscript.py).
#
# Facts verified against Nuke 14.1v8 (see probe/ in the source repo):
#   - a disconnected RotoPaint has a working 'format' knob
#   - stroke coordinates are pixels, origin bottom-left
#   - stroke colors are linear (no colorspace conversion applied)
#   - EARLIER elements in a layer render ON TOP, so strokes are emitted in
#     reverse paint order (fine detail first, coarse background last)
#   - serialized floats are written as x<ieee754-hex>, integral values plain

import json
import struct

import nuke


def _hf(v):
    """Serialize a number the way Nuke's curve serializer does."""
    v = float(v)
    if v == int(v) and abs(v) < 1e9:
        return str(int(v))
    return "x%08x" % struct.unpack("<I", struct.pack("<f", v))[0]


def _stroke_text(index, s):
    pts = s["p"]
    p = " ".join(
        "{%s %s 1}" % (_hf(pts[i]), _hf(pts[i + 1])) for i in range(0, len(pts), 2)
    )
    col = s["c"]
    hardness = s.get("h", 1.0)
    return (
        # bu 0 (no buildup): with bu 1, opc is overpowered by overlapping
        # stamps within a stroke (probe_buildup.py) and the opacity knob
        # would not do what it says
        "{cubiccurve Brush%d 512 catmullrom {cc {f 2080} {p %s}} {t 0} "
        "{a r %s g %s b %s opc %s bs %s h %s bu 0 str 1 bsh %s}}"
        % (
            index,
            p,
            _hf(col[0]),
            _hf(col[1]),
            _hf(col[2]),
            _hf(s.get("o", 1.0)),
            _hf(s["bs"]),
            _hf(hardness),
            _hf(hardness),
        )
    )


def _curves_text(stroke_texts, width, height):
    return (
        "{{v x3f99999a} {f 0} {n {layer Root {f 0} {t %s %s} {a} %s}}}"
        % (_hf(width / 2.0), _hf(height / 2.0), " ".join(stroke_texts))
    )


def _find_or_add_format(width, height, pixel_aspect=1.0):
    """Return the name of a root-anchored format matching w/h/pa, creating
    one if needed."""
    for f in nuke.formats():
        try:
            if (
                f.width() == width
                and f.height() == height
                and abs(f.pixelAspect() - pixel_aspect) < 1e-6
                and f.x() == 0
                and f.y() == 0
                and f.name()
            ):
                return f.name()
        except Exception:
            continue
    name = "PFR_%dx%d" % (width, height)
    nuke.addFormat(
        "%d %d 0 0 %d %d %g %s" % (width, height, width, height, pixel_aspect, name)
    )
    return name


def build(strokes_path, src_node_name=None):
    """Create a RotoPaint node from the strokes file. Returns the new node's
    name (which is also what the C++ op shows in its status knob)."""
    with open(strokes_path) as fh:
        data = json.load(fh)

    strokes = data["strokes"]
    node = nuke.nodes.RotoPaint()  # does not auto-connect to the selection
    node["format"].setValue(
        _find_or_add_format(
            int(data["width"]), int(data["height"]), float(data.get("pixel_aspect", 1.0))
        )
    )

    # earlier elements render on top: emit fine (later) strokes first
    texts = [
        _stroke_text(len(strokes) - i, s) for i, s in enumerate(reversed(strokes))
    ]
    node["curves"].fromScript(
        _curves_text(texts, int(data["width"]), int(data["height"]))
    )

    node["label"].setValue("%d strokes\nfrom %s" % (len(strokes), src_node_name or "?"))

    if src_node_name:
        src = nuke.toNode(src_node_name.split(".")[-1])
        if src is not None:
            node.setXYpos(src.xpos() + 120, src.ypos())

    return node.name()
