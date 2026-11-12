# Renders PNG previews of OBD_Dash_Cover.FCStd (no GUI needed):
#   freecad.cmd CAD/render_preview.py
# Writes CAD/preview/*.png. Uses matplotlib from FreeCAD's Python.
# matplotlib sorts faces roughly, so hidden parts can show through in
# oblique views: use these for a quick look, FreeCAD itself for details.
import os

import FreeCAD as App
import MeshPart
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

try:
    HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:
    HERE = os.getcwd()
OUT = os.path.join(HERE, "preview")
os.makedirs(OUT, exist_ok=True)

doc = App.openDocument(os.path.join(HERE, "OBD_Dash_Cover.FCStd"))
doc.recompute()


def tris(obj, defl=0.3):
    m = MeshPart.meshFromShape(Shape=obj.Shape, LinearDeflection=defl, AngularDeflection=0.5)
    return [[tuple(p) for p in f.Points] for f in m.Facets]


def shade(t, color, alpha, light=(-0.4, -0.5, 0.75)):
    """Simple Lambert shading (two-sided): one RGBA per triangle."""
    import numpy as np
    base = np.array(matplotlib.colors.to_rgb(color))
    a = np.array(t)
    n = np.cross(a[:, 1] - a[:, 0], a[:, 2] - a[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    k = 0.35 + 0.65 * np.abs(n @ (np.array(light) / np.linalg.norm(light)))
    rgb = np.clip(base[None, :] * k[:, None], 0, 1)
    return np.hstack([rgb, np.full((len(rgb), 1), alpha)])


def view(name, objs, elev, azim, title, size=(11, 8)):
    fig = plt.figure(figsize=size, dpi=110)
    ax = fig.add_subplot(111, projection="3d")
    # one collection for all objects: matplotlib depth-sorts within a
    # collection only, so separate ones would be painted over each other
    import numpy as np
    all_t, all_c = [], []
    for o, color, alpha in objs:
        t = tris(o)
        all_t += t
        all_c.append(shade(t, color, alpha))
    pc = Poly3DCollection(all_t, edgecolor="none")
    pc.set_facecolor(np.vstack(all_c))
    ax.add_collection3d(pc)
    ax.set_xlim(-5, 133)
    ax.set_ylim(-20, 90)
    ax.set_zlim(-30, 50)
    ax.set_box_aspect((138, 110, 80))
    ax.view_init(elev=elev, azim=azim)
    ax.set_axis_off()
    ax.set_title(title)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, name), bbox_inches="tight")
    plt.close(fig)
    print("wrote", name)


cover = doc.getObject("Cover")
g = {n: doc.getObject(n) for n in ("Ref_Glass", "Ref_LCD", "Ref_PCB", "Ref_PCB_parts", "Ref_Terminal",
                                   "Ref_Connector_body")}
grey, black, blue, green, lime, orange = "#8a8a8a", "#202020", "#4060c0", "#30a050", "#60d060", "#e07020"

view("back.png", [(cover, grey, 1.0)], 89, -90,
     "Back (outside): AMPS mount, vents, screw holes; chin at the bottom")
view("front.png", [(cover, grey, 1.0), (g["Ref_Glass"], black, 1.0)], -89, 90,
     "Front: display glass (black) and the chin below it, cable entry underneath")
view("left_side.png", [(cover, grey, 1.0), (g["Ref_PCB"], green, 0.8)], 10, -175,
     "Left side: microSD, USB-C, BOOT / RESET openings")
view("inside.png", [(cover, grey, 1.0)], -60, -90,
     "Inside (seen from the display side, mirrored): posts, AMPS bosses, chin cavity")
view("inside_with_board.png",
     [(cover, grey, 0.30), (g["Ref_PCB"], green, 0.6), (g["Ref_PCB_parts"], green, 0.35),
      (g["Ref_Terminal"], lime, 0.8), (g["Ref_Connector_body"], orange, 0.8)], -35, -60,
     "Inside with the PCB (green), terminal plug (light green), cable entry body (orange)")
