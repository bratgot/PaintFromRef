# Probe 4: can we author strokes by generating serialized curves-knob text
# and loading it with one fromScript() call? (The per-stroke Python API path
# measured 71.6s for 1122 strokes -- unusable.)
#
# Verifies: round-trip fromScript(toScript()), synthetic text parses, strokes
# render at the right place/color, z-order (first element in text = on top),
# and timing for a few thousand strokes.
import struct
import time
import traceback

import nuke
import nuke.rotopaint as rp

OUT = r"C:/dev/PaintFromRef/probe/probe_fromscript_out.txt"
lines = []


def emit(s=""):
    lines.append(str(s))
    print(s)


def hf(v):
    """Serialize a float the way Nuke does: plain int if integral, else
    x<ieee754 hex>."""
    if float(v) == int(v) and abs(v) < 1e9:
        return str(int(v))
    return "x%08x" % struct.unpack("<I", struct.pack("<f", float(v)))[0]


def stroke_text(name, pts, r, g, b, bs, opc=1.0, h=1.0):
    p = "\n       ".join("{%s %s 1}" % (hf(x), hf(y)) for x, y in pts)
    return (
        "{cubiccurve %s 512 catmullrom\n"
        "     {cc\n"
        "      {f 2080}\n"
        "      {p\n       %s}}\n"
        "     {t 0}\n"
        "     {a r %s g %s b %s opc %s bs %s h %s bu 1 str 1 bsh %s}}"
        % (name, p, hf(r), hf(g), hf(b), hf(opc), hf(bs), hf(h), hf(h))
    )


def curves_text(strokes, w, h):
    body = "\n    ".join(strokes)
    return (
        "{{v x3f99999a}\n"
        "  {f 0}\n"
        "  {n\n"
        "   {layer Root\n"
        "    {f 0}\n"
        "    {t %s %s}\n"
        "    {a}\n"
        "    %s}}}" % (hf(w / 2.0), hf(h / 2.0), body)
    )


try:
    nuke.addFormat("512 512 0 0 512 512 1 pfr_probe")

    # --- round trip sanity: API-built node -> toScript -> fromScript ------
    n1 = nuke.nodes.RotoPaint()
    n1["format"].setValue("pfr_probe")
    s = rp.Stroke(n1["curves"])
    s.append(rp.AnimControlPoint(100, 100))
    s.append(rp.AnimControlPoint(300, 100))
    a = s.getAttributes()
    a.set("r", 1.0)
    a.set("bs", 40.0)
    n1["curves"].rootLayer.append(s)
    script = n1["curves"].toScript()
    n2 = nuke.nodes.RotoPaint()
    n2["format"].setValue("pfr_probe")
    n2["curves"].fromScript(script)
    emit("round trip sample (should be r=1 a=1): r=%.3f a=%.3f"
         % (n2.sample("red", 200, 100), n2.sample("alpha", 200, 100)))

    # --- synthetic text ---------------------------------------------------
    n3 = nuke.nodes.RotoPaint()
    n3["format"].setValue("pfr_probe")
    st = [
        # green appended FIRST in text -> should render ON TOP at overlap
        stroke_text("Brush1", [(100, 300), (200, 300)], 0.0, 1.0, 0.0, 40.0),
        stroke_text("Brush2", [(100, 300), (200, 300)], 0.0, 0.0, 1.0, 40.0),
        stroke_text("Brush3", [(100.5, 100.25), (300.75, 100.25)], 0.25, 0.5,
                    0.75, 30.0, opc=0.5, h=0.6),
    ]
    txt = curves_text(st, 512, 512)
    n3["curves"].fromScript(txt)
    emit("synthetic z-order (want green 1,0): r=%.3f g=%.3f b=%.3f"
         % (n3.sample("red", 150, 300), n3.sample("green", 150, 300),
            n3.sample("blue", 150, 300)))
    emit("synthetic colored stroke (want ~0.125 0.25 0.375 from opc 0.5): "
         "r=%.3f g=%.3f b=%.3f a=%.3f"
         % (n3.sample("red", 200, 100), n3.sample("green", 200, 100),
            n3.sample("blue", 200, 100), n3.sample("alpha", 200, 100)))
    emit("empty corner (want 0): a=%.3f" % n3.sample("alpha", 480, 480))

    # --- timing at scale --------------------------------------------------
    import random

    random.seed(1)
    big = []
    for i in range(8000):
        x = random.uniform(20, 490)
        y = random.uniform(20, 490)
        big.append(
            stroke_text("Brush%d" % (i + 10), [(x, y), (x + 15, y + 5)],
                        random.random(), random.random(), random.random(),
                        8.0)
        )
    big_txt = curves_text(big, 512, 512)
    emit("text size for 8000 strokes: %.1f MB" % (len(big_txt) / 1e6))
    n4 = nuke.nodes.RotoPaint()
    n4["format"].setValue("pfr_probe")
    t0 = time.time()
    n4["curves"].fromScript(big_txt)
    t1 = time.time()
    emit("fromScript(8000 strokes): %.2fs" % (t1 - t0))
    t0 = time.time()
    v = n4.sample("alpha", 256, 256)
    t1 = time.time()
    emit("first sample after load: %.2fs (a=%.3f)" % (t1 - t0, v))

except Exception:
    emit("TOP LEVEL FAILURE:")
    emit(traceback.format_exc())

with open(OUT, "w") as fh:
    fh.write("\n".join(lines))
print("wrote %s" % OUT)
