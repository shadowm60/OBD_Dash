# OBD_Dash back cover for the Waveshare ESP32-S3-Touch-LCD-5 (FreeCAD 1.x macro)
#
# Builds OBD_Dash_Cover.FCStd: a parametric back cover with a "chin" below the
# glass for the car cable, plus "keep-out" reference bodies of the display
# module (not printed). Every size and position is a cell of the spreadsheet
# "Params"; the features read them through expressions, so editing a cell and
# pressing Recompute (Ctrl+R) updates the model.
#
#   GUI:      Macro > Macros... > obd_dash_cover.py > Execute
#   headless: freecad.cmd CAD/obd_dash_cover.py      (also exports STL / STEP)
#
# Coordinates, seen from BEHIND (as the Waveshare back-view drawing):
#   origin = bottom-left corner of the glass, on the glass back face
#   X right, Y up, Z out of the back (away from the glass)
#   The chin hangs below the glass (Y < 0) and reaches forward to the glass
#   front plane (Z = -glass_t).
#
# Same structure as FP_Dash/CAD/fp_dash_cover.py (7" board). Differences:
# the 5" board takes 6-36 V itself (no converter cradle), its USB-C / microSD /
# BOOT / RESET / battery switch sit on the PCB edges (openings in the side
# walls instead of a hatch), and the PCB is held by 4 corner M2.5 screws,
# which are replaced by longer ones through the cover posts.

import os

import FreeCAD as App
import Part

BUILD_BATTERY_BAY = False   # LiPo frame; off: no room above the parts, and heat (README)
BUILD_VENTS = True          # slots in the back
BUILD_AMPS = True           # AMPS mount (RAM-style ball / phone holders)

DOC_NAME = "OBD_Dash_Cover"

try:
    HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:
    HERE = os.getcwd()

# ----------------------------------------------------------------- parameters
# (alias, value, unit, comment). "=" values are formulas (derived, don't edit).
SECTIONS = [
    ("Display module - Waveshare drawing (docs.waveshare.com), CHECK WITH CALIPERS", [
        ("glass_w", 127.70, "mm", "glass width"),
        ("glass_h", 87.45, "mm", "glass height"),
        ("glass_r", 4.0, "mm", "glass corner radius"),
        ("glass_t", 1.1, "mm", "glass thickness (MEASURE)"),
        ("front_to_lcd", 4.80, "mm", "glass front face -> LCD back"),
        ("front_to_pcb", 10.00, "mm", "glass front face -> PCB component side"),
        ("front_to_parts", 14.00, "mm", "glass front face -> top of the PCB parts"),
        ("front_to_term", 17.0, "mm", "glass front face -> top of the green terminal plug (MEASURE)"),
        ("pcb_w", 120.50, "mm", "PCB width"),
        ("pcb_h", 75.50, "mm", "PCB height"),
        ("pcb_x", 3.60, "mm", "glass left edge -> PCB left edge, seen from behind"),
        ("pcb_top", 6.20, "mm", "glass top edge -> PCB top edge"),
        ("hole_edge_x", 4.75, "mm", "PCB left / right edge -> M2.5 hole centre"),
        ("hole_dx", 111.0, "mm", "M2.5 hole spacing X"),
        ("hole_bottom", 6.0, "mm", "PCB bottom edge -> lower M2.5 hole row"),
        ("hole_dy", 63.0, "mm", "M2.5 hole spacing Y"),
        ("orig_screw_l", 5.0, "mm", "thread length of an original M2.5 PCB screw (MEASURE)"),
        ("corner_free", 10.0, "mm", "square around each hole without tall parts (photo: microSD starts ~10.3)"),
        ("term_x0", 35.5, "mm", "terminal plug left end, glass coordinates (estimated)"),
        ("term_w", 58.0, "mm", "terminal plug length (16 x 3.5 mm pitch, estimated)"),
        ("term_h", 14.0, "mm", "terminal plug depth on the PCB (estimated)"),
        ("term_over", 1.5, "mm", "terminal plug overhang beyond the PCB bottom edge (estimated)"),
    ]),
    ("Edge parts - centres measured from the lower M2.5 hole row (drawing)", [
        ("usb_dy", 31.5, "mm", "left edge: USB-C"),
        ("btn_dy", 11.6, "mm", "left edge: lower button"),
        ("btn_pitch", 8.6, "mm", "left edge: button spacing (BOOT / RESET)"),
        ("sd_dy", 51.0, "mm", "left edge: microSD slot (estimated from the photo)"),
        ("sw_dy", 17.2, "mm", "right edge: battery switch"),
    ]),
    ("Cover", [
        ("depth_in", 10.0, "mm", "inside depth: PCB component side -> inner back face"),
        ("wall", 2.4, "mm", "side wall thickness (6 lines of 0.4 nozzle)"),
        ("back_t", 2.5, "mm", "back thickness"),
        ("rim_gap", 0.5, "mm", "gap rim <-> glass, filled by ~1 mm foam tape"),
        ("post_d", 6.0, "mm", "screw post diameter (lands on the PCB around the hole)"),
        ("screw_d", 2.9, "mm", "M2.5 clearance hole"),
        ("chin_h", 16.0, "mm", "chin height below the glass (GX12 connector: ~26)"),
        ("front_t", 2.0, "mm", "chin front lip thickness"),
    ]),
    ("Openings in the side walls (size Y x Z, centre height above the PCB component side)", [
        ("usb_w", 13.0, "mm", "USB-C opening Y (fits the plug shell + overmould edge)"),
        ("usb_h", 7.5, "mm", "USB-C opening Z"),
        ("usb_z", 1.6, "mm", "USB-C centre above the PCB"),
        ("sd_w", 16.0, "mm", "microSD opening Y (room for a fingernail)"),
        ("sd_h", 5.0, "mm", "microSD opening Z"),
        ("sd_z", 0.9, "mm", "microSD centre above the PCB"),
        ("btn_d", 3.5, "mm", "BOOT / RESET pin hole diameter"),
        ("btn_z", 1.2, "mm", "button centre above the PCB"),
        ("sw_w", 10.0, "mm", "battery switch opening Y"),
        ("sw_h", 5.5, "mm", "battery switch opening Z"),
        ("sw_z", 1.5, "mm", "switch centre above the PCB"),
    ]),
    ("Cable entry in the chin (bottom face): M12 cable gland or GX12 4-pin", [
        ("con_d", 12.2, "mm", "panel hole: M12 gland / GX12 = 12.2, GX16 = 16.2"),
        ("con_x", 60.0, "mm", "centre X (near the CAN / VIN pins of the terminal)"),
        ("con_body_d", 15.0, "mm", "nut / body diameter inside (keep-out check)"),
        ("con_body_l", 10.0, "mm", "length inside: gland ~10, GX12 with solder cups ~22"),
        ("head_d", 5.0, "mm", "M2.5 screw head diameter (keep-out check)"),
    ]),
    ("Mount: AMPS pattern on the back (RAM / phone mounts)", [
        ("amps_cx", 63.85, "mm", "pattern centre X (glass coordinates)"),
        ("amps_cy", 52.0, "mm", "pattern centre Y"),
        ("amps_sx", 38.0, "mm", "hole spacing X"),
        ("amps_sy", 30.0, "mm", "hole spacing Y"),
        ("amps_d", 4.3, "mm", "M4 clearance hole"),
        ("amps_boss_d", 11.0, "mm", "boss diameter (inside)"),
        ("amps_boss_h", 4.0, "mm", "boss height (inside)"),
        ("nut_af", 7.0, "mm", "M4 nut across flats"),
        ("nut_tol", 0.3, "mm", "nut trap clearance"),
        ("nut_depth", 3.4, "mm", "nut trap depth"),
    ]),
    ("Battery bay (1-cell LiPo pouch) - only with BUILD_BATTERY_BAY = True", [
        ("bat_l", 50.0, "mm", "length (X)"),
        ("bat_w", 34.0, "mm", "width (Y)"),
        ("bat_t", 6.0, "mm", "thickness incl. swelling margin"),
        ("bat_cx", 98.0, "mm", "centre X (glass coordinates)"),
        ("bat_cy", 52.0, "mm", "centre Y"),
        ("bat_frame_h", 3.0, "mm", "frame height"),
        ("bat_gap", 10.0, "mm", "opening in the frame for the wires"),
        ("fit", 0.4, "mm", "clearance battery <-> frame, each side"),
        ("rail_t", 1.6, "mm", "frame wall thickness"),
    ]),
    ("Vents", [
        ("vent_w", 2.5, "mm", "slot width"),
        ("vent_l", 10.0, "mm", "slot length"),
        ("vent_pitch", 8.0, "mm", "slot spacing"),
        ("vent_cx", 63.85, "mm", "row centre X"),
        ("vent_cy", 78.0, "mm", "row centre Y"),
    ]),
    ("Derived - formulas, do not edit", [
        ("z_lcd", "=front_to_lcd - glass_t", "", "Z of the LCD back"),
        ("z_pcb", "=front_to_pcb - glass_t", "", "Z of the PCB component side"),
        ("z_parts", "=front_to_parts - glass_t", "", "Z of the top of the parts"),
        ("z_term", "=front_to_term - glass_t", "", "Z of the top of the terminal plug"),
        ("z_back_in", "=z_pcb + depth_in", "", "Z of the inner back face"),
        ("z_top", "=z_back_in + back_t", "", "Z of the outer back face"),
        ("thickness", "=z_top + glass_t", "", "overall: glass front -> cover back"),
        ("pcb_y", "=glass_h - pcb_top - pcb_h", "", "Y of the PCB bottom edge"),
        ("hx1", "=pcb_x + hole_edge_x", "", "hole column 1 X"),
        ("hx2", "=hx1 + hole_dx", "", "hole column 2 X"),
        ("hy1", "=pcb_y + hole_bottom", "", "lower hole row Y"),
        ("hy2", "=hy1 + hole_dy", "", "upper hole row Y"),
        ("post_len", "=depth_in", "", "screw post length (PCB -> inner back face)"),
        ("screw_len", "=orig_screw_l + back_t + post_len", "", "new M2.5 screw length (round DOWN)"),
        ("z_chin_in", "=front_t - glass_t", "", "Z of the chin's inner front face"),
        ("con_z", "=(z_chin_in + z_back_in) / 2", "", "cable entry axis Z"),
        ("clear_left", "=pcb_x - wall", "", "gap wall <-> PCB, left (> 0.5)"),
        ("clear_right", "=glass_w - pcb_x - pcb_w - wall", "", "gap wall <-> PCB, right"),
        ("clear_top", "=pcb_top - wall", "", "gap wall <-> PCB, top"),
        ("clear_parts", "=z_back_in - z_parts", "", "gap PCB parts <-> back (> 2)"),
        ("clear_term", "=z_back_in - z_term", "", "gap terminal plug <-> back (> 2)"),
        ("clear_amps", "=z_back_in - amps_boss_h - z_parts", "", "gap PCB parts <-> AMPS bosses"),
        ("clear_wires", "=chin_h - wall - con_body_l", "", "chin: room for the wire bend above the cable entry"),
    ]),
]


def make_params(doc):
    ss = doc.addObject("Spreadsheet::Sheet", "Params")
    ss.set("A1", "Parameter")
    ss.set("B1", "Value")
    ss.set("C1", "Meaning")
    row = 2
    for title, rows in SECTIONS:
        row += 1
        ss.set(f"A{row}", title)
        row += 1
        for alias, value, unit, comment in rows:
            ss.set(f"A{row}", alias)
            if isinstance(value, str):
                ss.set(f"B{row}", value)
            else:
                ss.set(f"B{row}", f"{value} {unit}".strip())
            ss.setAlias(f"B{row}", alias)
            ss.set(f"C{row}", comment)
            row += 1
    ss.setColumnWidth("A", 160)
    ss.setColumnWidth("B", 90)
    ss.setColumnWidth("C", 480)
    doc.recompute()
    return ss


# ----------------------------------------------------------------- helpers
# Sizes / positions are expression strings; "p." is replaced by "Params.".

def q(v):
    """Plain numbers are millimetres (FreeCAD won't add 0 to a length)."""
    return f"{v} mm" if isinstance(v, (int, float)) else v


def ex(e):
    return str(q(e)).replace("p.", "Params.")


def set_pos(obj, x, y, z):
    obj.setExpression("Placement.Base.x", ex(x))
    obj.setExpression("Placement.Base.y", ex(y))
    obj.setExpression("Placement.Base.z", ex(z))


def box(doc, name, L, W, H, x, y, z):
    o = doc.addObject("Part::Box", name)
    o.setExpression("Length", ex(L))
    o.setExpression("Width", ex(W))
    o.setExpression("Height", ex(H))
    set_pos(o, x, y, z)
    return o


def cyl(doc, name, d, H, x, y, z, axis="z"):
    """Cylinder of diameter d, height H; base centre at (x, y, z).
    axis "z" (default), "y" (horizontal, pointing +Y) or "x" (pointing +X)."""
    o = doc.addObject("Part::Cylinder", name)
    o.setExpression("Radius", ex(f"({d}) / 2"))
    o.setExpression("Height", ex(H))
    if axis == "y":
        o.Placement.Rotation = App.Rotation(App.Vector(1, 0, 0), -90)
    elif axis == "x":
        o.Placement.Rotation = App.Rotation(App.Vector(0, 1, 0), 90)
    set_pos(o, x, y, z)
    return o


def hexagon(doc, name, af, H, x, y, z):
    """Hexagonal prism (nut trap), across flats af."""
    o = doc.addObject("Part::Prism", name)
    o.Polygon = 6
    o.setExpression("Circumradius", ex(f"({af}) / sqrt(3)"))
    o.setExpression("Height", ex(H))
    set_pos(o, x, y, z)
    return o


def fuse(doc, name, shapes, refine=True):
    o = doc.addObject("Part::MultiFuse", name)
    o.Shapes = shapes
    o.Refine = refine
    return o


def cut(doc, name, base, tool, refine=True):
    o = doc.addObject("Part::Cut", name)
    o.Base = base
    o.Tool = tool
    o.Refine = refine
    return o


def rbox(doc, name, x, y, z, w, h, t, r):
    """Box with rounded vertical edges = 2 boxes + 4 cylinders."""
    x, y, z, w, h, t, r = (q(v) for v in (x, y, z, w, h, t, r))
    parts = [
        box(doc, f"{name}_bx", f"({w}) - 2 * ({r})", h, t, f"({x}) + ({r})", y, z),
        box(doc, f"{name}_by", w, f"({h}) - 2 * ({r})", t, x, f"({y}) + ({r})", z),
    ]
    for i, (cx, cy) in enumerate([
            (f"({x}) + ({r})", f"({y}) + ({r})"),
            (f"({x}) + ({w}) - ({r})", f"({y}) + ({r})"),
            (f"({x}) + ({r})", f"({y}) + ({h}) - ({r})"),
            (f"({x}) + ({w}) - ({r})", f"({y}) + ({h}) - ({r})")]):
        parts.append(cyl(doc, f"{name}_c{i}", f"2 * ({r})", t, cx, cy, z))
    return fuse(doc, name, parts)


def group(doc, name, objs):
    g = doc.addObject("App::DocumentObjectGroup", name)
    for o in objs:
        g.addObject(o)
    return g


# ----------------------------------------------------------------- model

def build():
    if App.listDocuments().get(DOC_NAME):
        App.closeDocument(DOC_NAME)
    doc = App.newDocument(DOC_NAME)
    make_params(doc)

    holes_xy = [("p.hx1", "p.hy1"), ("p.hx2", "p.hy1"), ("p.hx1", "p.hy2"), ("p.hx2", "p.hy2")]

    # --- reference: the display module (keep-out volumes, not printed)
    cf = "p.corner_free"
    ref = [
        box(doc, "Ref_Glass", "p.glass_w", "p.glass_h", "p.glass_t", 0, 0, "-p.glass_t"),
        # LCD module behind the glass: assumed the size of the PCB + 0.3 mm
        box(doc, "Ref_LCD", "p.pcb_w + 0.6mm", "p.pcb_h + 0.6mm", "p.z_lcd",
            "p.pcb_x - 0.3mm", "p.pcb_y - 0.3mm", 0),
        # PCB incl. the standoffs under it
        box(doc, "Ref_PCB", "p.pcb_w", "p.pcb_h", "p.z_pcb - p.z_lcd", "p.pcb_x", "p.pcb_y", "p.z_lcd"),
        # parts on the PCB, except the corners around the screw holes (the posts land there)
        fuse(doc, "Ref_PCB_parts", [
            box(doc, "Ref_parts_x", f"p.pcb_w - 2 * {cf}", "p.pcb_h", "p.z_parts - p.z_pcb",
                f"p.pcb_x + {cf}", "p.pcb_y", "p.z_pcb"),
            box(doc, "Ref_parts_y", "p.pcb_w", f"p.pcb_h - 2 * {cf}", "p.z_parts - p.z_pcb",
                "p.pcb_x", f"p.pcb_y + {cf}", "p.z_pcb"),
        ]),
        box(doc, "Ref_Terminal", "p.term_w", "p.term_h + p.term_over", "p.z_term - p.z_pcb",
            "p.term_x0", "p.pcb_y - p.term_over", "p.z_pcb"),
        box(doc, "Ref_Connector_body", "p.con_body_d", "p.con_body_l", "p.con_body_d",
            "p.con_x - p.con_body_d / 2", "-p.chin_h + p.wall", "p.con_z - p.con_body_d / 2"),
    ]
    if BUILD_BATTERY_BAY:
        ref.append(box(doc, "Ref_Battery", "p.bat_l", "p.bat_w", "p.bat_t",
                       "p.bat_cx - p.bat_l / 2", "p.bat_cy - p.bat_w / 2", "p.z_back_in - p.bat_t"))

    # --- cover: outer body = glass-sized shell + chin below the glass
    r = "p.glass_r"
    outer = [
        rbox(doc, "Outer_main", 0, 0, "p.rim_gap", "p.glass_w", "p.glass_h", "p.z_top - p.rim_gap", r),
        # squares the two lower corners so the chin joins flush
        box(doc, "Outer_bridge", "p.glass_w", f"{r} + 0.3mm", "p.z_top - p.rim_gap", 0, "-0.3mm", "p.rim_gap"),
        # chin: from the glass front plane to the back, rounded lower corners
        box(doc, "Chin_a", "p.glass_w", f"p.chin_h - {r} - 0.3mm", "p.z_top + p.glass_t",
            0, f"-p.chin_h + {r}", "-p.glass_t"),
        box(doc, "Chin_b", f"p.glass_w - 2 * {r}", r, "p.z_top + p.glass_t", r, "-p.chin_h", "-p.glass_t"),
        cyl(doc, "Chin_c0", f"2 * {r}", "p.z_top + p.glass_t", r, f"-p.chin_h + {r}", "-p.glass_t"),
        cyl(doc, "Chin_c1", f"2 * {r}", "p.z_top + p.glass_t", f"p.glass_w - {r}", f"-p.chin_h + {r}", "-p.glass_t"),
    ]
    outer_body = fuse(doc, "Outer", outer)
    inner = [
        rbox(doc, "Inner_main", "p.wall", "p.wall", "p.rim_gap - 1mm",
             "p.glass_w - 2 * p.wall", "p.glass_h - 2 * p.wall", "p.z_back_in - p.rim_gap + 1mm",
             "p.glass_r - p.wall"),
        # chin cavity, open towards the main cavity (wires from the terminal plug)
        box(doc, "Inner_chin", "p.glass_w - 2 * p.wall", "p.chin_h + 1mm", "p.z_back_in - p.z_chin_in",
            "p.wall", "-p.chin_h + p.wall", "p.z_chin_in"),
    ]
    shell = cut(doc, "Shell", outer_body, fuse(doc, "Inner", inner))

    adds = [shell]          # solids fused to the shell
    holes = []              # solids cut from the result

    # screw posts onto the PCB's corner holes (M2.5, original screws replaced)
    for i, (hx, hy) in enumerate(holes_xy):
        adds.append(cyl(doc, f"Post{i}", "p.post_d", "p.post_len", hx, hy, "p.z_pcb"))
        holes.append(cyl(doc, f"Post{i}_hole", "p.screw_d", "p.post_len + p.back_t + 2mm",
                         hx, hy, "p.z_pcb - 1mm"))

    # side wall openings: left = USB-C, microSD, BOOT / RESET; right = battery switch
    def wall_box(name, side, dy, w, h, z):
        x = "-1mm" if side == "L" else "p.glass_w - p.wall - 1mm"
        return box(doc, name, "p.wall + 2mm", w, h, x, f"p.hy1 + {dy} - {w} / 2", f"p.z_pcb + {z} - {h} / 2")

    holes.append(wall_box("Open_USB", "L", "p.usb_dy", "p.usb_w", "p.usb_h", "p.usb_z"))
    holes.append(wall_box("Open_SD", "L", "p.sd_dy", "p.sd_w", "p.sd_h", "p.sd_z"))
    holes.append(wall_box("Open_switch", "R", "p.sw_dy", "p.sw_w", "p.sw_h", "p.sw_z"))
    for i in range(2):
        holes.append(cyl(doc, f"Open_button{i}", "p.btn_d", "p.wall + 2mm", "-1mm",
                         f"p.hy1 + p.btn_dy + {i} * p.btn_pitch", "p.z_pcb + p.btn_z", axis="x"))

    # cable entry in the chin's bottom face
    holes.append(cyl(doc, "Cable_entry", "p.con_d", "p.wall + 2mm",
                     "p.con_x", "-p.chin_h - 1mm", "p.con_z", axis="y"))

    # AMPS mount: holes, bosses, M4 nut traps (nut inside, screw from the mount)
    if BUILD_AMPS:
        for i, (dx, dy) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
            ax = f"p.amps_cx + {dx} * p.amps_sx / 2"
            ay = f"p.amps_cy + {dy} * p.amps_sy / 2"
            adds.append(cyl(doc, f"AMPS_boss{i}", "p.amps_boss_d", "p.amps_boss_h",
                            ax, ay, "p.z_back_in - p.amps_boss_h"))
            holes.append(cyl(doc, f"AMPS_hole{i}", "p.amps_d", "p.amps_boss_h + p.back_t + 2mm",
                             ax, ay, "p.z_back_in - p.amps_boss_h - 1mm"))
            holes.append(hexagon(doc, f"AMPS_nut{i}", "p.nut_af + p.nut_tol", "p.nut_depth + 1mm",
                                 ax, ay, "p.z_back_in - p.amps_boss_h - 1mm"))

    # battery bay: frame with an opening for the wires on the left side
    if BUILD_BATTERY_BAY:
        iw = "(p.bat_l + 2 * p.fit)"
        ih = "(p.bat_w + 2 * p.fit)"
        x0 = f"p.bat_cx - {iw} / 2 - p.rail_t"
        y0 = f"p.bat_cy - {ih} / 2 - p.rail_t"
        z0 = "p.z_back_in - p.bat_frame_h"
        ow = f"({iw} + 2 * p.rail_t)"
        oh = f"({ih} + 2 * p.rail_t)"
        seg = f"(({ih} - p.bat_gap) / 2)"
        adds += [
            box(doc, "Bat_frame_B", ow, "p.rail_t", "p.bat_frame_h", x0, y0, z0),
            box(doc, "Bat_frame_T", ow, "p.rail_t", "p.bat_frame_h", x0, f"{y0} + {oh} - p.rail_t", z0),
            box(doc, "Bat_frame_R", "p.rail_t", oh, "p.bat_frame_h", f"{x0} + {ow} - p.rail_t", y0, z0),
            box(doc, "Bat_frame_L1", "p.rail_t", f"{seg} + p.rail_t", "p.bat_frame_h", x0, y0, z0),
            box(doc, "Bat_frame_L2", "p.rail_t", f"{seg} + p.rail_t", "p.bat_frame_h",
                x0, f"{y0} + {oh} - p.rail_t - {seg}", z0),
        ]

    # vent slots in the back
    if BUILD_VENTS:
        n = 8                               # number of slots
        for i in range(n):
            holes.append(box(doc, f"Vent{i}", "p.vent_w", "p.vent_l", "p.back_t + 2mm",
                             f"p.vent_cx + ({i} - ({n} - 1) / 2) * p.vent_pitch - p.vent_w / 2",
                             "p.vent_cy - p.vent_l / 2", "p.z_back_in - 1mm"))

    body = fuse(doc, "Cover_body", adds)
    cutters = fuse(doc, "Cover_holes", holes, refine=False)
    cover = cut(doc, "Cover", body, cutters)

    # cover screw heads on the outside: must not land on the AMPS mount area
    heads = [cyl(doc, f"Ref_Screw_head{i}", "p.head_d", "2.5mm", hx, hy, "p.z_top")
             for i, (hx, hy) in enumerate(holes_xy)]
    ref += heads

    doc.recompute()
    group(doc, "Reference_display", ref)
    if App.GuiUp:
        for o in ref:
            o.ViewObject.Transparency = 70
            o.ViewObject.ShapeColor = (0.3, 0.3, 0.8)
        cover.ViewObject.ShapeColor = (0.25, 0.25, 0.25)
    return doc, cover, ref


# ----------------------------------------------------------------- checks

def check(doc, cover, ref):
    ss = doc.getObject("Params")
    ok = True
    limits = {"clear_parts": 2.0, "clear_term": 2.0, "clear_amps": 1.0, "clear_wires": 3.0}
    print("--- clearances (mm)")
    for a in ("clear_left", "clear_right", "clear_top", "clear_parts", "clear_term", "clear_amps",
              "clear_wires", "post_len", "screw_len", "thickness"):
        v = float(ss.get(a))
        bad = a.startswith("clear") and v < limits.get(a, 0.5)
        if a == "clear_amps" and not BUILD_AMPS:
            bad = False
        ok &= not bad
        print(f"  {a:14s} {v:7.2f}{'   <-- too small' if bad else ''}")
    print("--- interference (must be 0): cover <-> display module and parts inside")
    for r in ref:
        v = cover.Shape.common(r.Shape).Volume
        print(f"  {r.Name:20s} {v:8.3f} mm3{'   <-- collision' if v > 1e-3 else ''}")
        ok &= v < 1e-3
    print("--- parts inside <-> each other (must be 0)")
    names = ["Ref_Connector_body", "Ref_Battery"]
    others = ["Ref_Terminal", "Ref_PCB_parts", "Ref_PCB", "Ref_LCD", "Ref_Glass"]
    for a in names:
        oa = doc.getObject(a)
        if oa is None:
            continue
        for b in others:
            v = oa.Shape.common(doc.getObject(b).Shape).Volume
            if v > 1e-3:
                print(f"  {a} <-> {b}: {v:.1f} mm3   <-- collision")
                ok = False
    print("--- cover volume %.1f cm3, valid %s" % (cover.Shape.Volume / 1000, cover.Shape.isValid()))
    ok &= cover.Shape.isValid()
    print("CHECK", "OK" if ok else "FAILED")
    return ok


def export(doc, cover):
    import MeshPart
    out = os.path.join(HERE, "export")
    os.makedirs(out, exist_ok=True)
    m = MeshPart.meshFromShape(Shape=cover.Shape, LinearDeflection=0.05, AngularDeflection=0.2)
    m.write(os.path.join(out, "obd_dash_cover.stl"))
    cover.Shape.exportStep(os.path.join(out, "obd_dash_cover.step"))
    print("exported to", out)


doc, cover, ref = build()
check(doc, cover, ref)
doc.saveAs(os.path.join(HERE, DOC_NAME + ".FCStd"))
print("saved", os.path.join(HERE, DOC_NAME + ".FCStd"))
if not App.GuiUp:
    export(doc, cover)
