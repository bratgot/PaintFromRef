# Probe 3: is a default API-created stroke visible at all frames?
import nuke
import nuke.rotopaint as rp

n = nuke.createNode("RotoPaint", inpanel=False)
nuke.addFormat("640 480 0 0 640 480 1 pfr_probe")
n["format"].setValue("pfr_probe")

curves = n["curves"]
s = rp.Stroke(curves)
s.append(rp.AnimControlPoint(100, 100))
s.append(rp.AnimControlPoint(300, 100))
a = s.getAttributes()
a.set("r", 1.0)
a.set("a", 1.0)
a.set("bs", 40.0)
curves.rootLayer.append(s)

out = []
for frame in (0, 1, 25, 100):
    nuke.frame(frame)
    r = n.sample("red", 200, 100)
    al = n.sample("alpha", 200, 100)
    out.append("frame %3d: r=%.4f a=%.4f" % (frame, r, al))

txt = "\n".join(out)
print(txt)
with open(r"C:/dev/PaintFromRef/probe/probe_lifetime_out.txt", "w") as f:
    f.write(txt)
