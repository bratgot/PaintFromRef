# PaintFromRef init.py - runs in every Nuke mode (GUI, -t, -ti, farm).
# The compiled plugin is version-specific, so binaries live in per-version
# subfolders (14.1/, 15.2/, ...). Add the one matching the running Nuke.
import os

import nuke

_dir = os.path.dirname(os.path.abspath(__file__))
_mm = "%d.%d" % (nuke.NUKE_VERSION_MAJOR, nuke.NUKE_VERSION_MINOR)
_bin = os.path.join(_dir, _mm)
if os.path.isdir(_bin):
    nuke.pluginAddPath(_bin.replace("\\", "/"), addToSysPath=False)
else:
    nuke.tprint(
        "PaintFromRef: no binary for Nuke %s (looked in %s)" % (_mm, _bin)
    )
