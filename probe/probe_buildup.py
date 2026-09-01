# Probe 5: does bu (buildup) 0 make opc behave like real opacity?
import struct
import nuke

def hf(v):
    v = float(v)
    if v == int(v) and abs(v) < 1e9:
        return str(int(v))
    return "x%08x" % struct.unpack("<I", struct.pack("<f", v))[0]

def stroke(name, bu, opc):
    return (
        "{cubiccurve %s 512 catmullrom {cc {f 2080} {p {%s %s 1} {%s %s 1}}} "
        "{t 0} {a r 1 g 0 b 0 opc %s bs 40 h 1 bu %d str 1 bsh 1}}"
        % (name, hf(100.0), hf(100.0), hf(300.0), hf(100.0), hf(opc), bu)
    )

nuke.addFormat("512 512 0 0 512 512 1 pfr_probe")
out = []
for bu, opc, y in ((1, 0.5, 100), (0, 0.5, 100), (0, 1.0, 100)):
    n = nuke.nodes.RotoPaint()
    n["format"].setValue("pfr_probe")
    txt = ("{{v x3f99999a} {f 0} {n {layer Root {f 0} {t 256 256} {a} %s}}}"
           % stroke("Brush1", bu, opc))
    n["curves"].fromScript(txt)
    out.append("bu=%d opc=%.1f -> r=%.3f a=%.3f"
               % (bu, opc, n.sample("red", 200, y), n.sample("alpha", 200, y)))
txt = "\n".join(out)
print(txt)
with open(r"C:/dev/PaintFromRef/probe/probe_buildup_out.txt", "w") as f:
    f.write(txt)
