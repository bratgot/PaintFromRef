# PaintFromRef plugin menu registration (lives in the plugin's own folder;
# the user's ~/.nuke/menu.py adds this folder via nuke.pluginAddPath).
import nuke

nuke.menu("Nodes").addCommand(
    "Draw/PaintFromRef", "nuke.createNode('PaintFromRef')", icon="RotoPaint.png"
)
