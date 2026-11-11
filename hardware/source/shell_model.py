"""
Knob shell: rounded-rectangle desk base for the 1.28" ESP32-S3 round knob display.
Replaces the geared puck in puck_model.py (kept for history). Needs: numpy, manifold3d.

Parts (mm), written to stl/:
  shell.stl        main body, printed upside down (top face on the bed); knob pocket on top, open underneath
  bottom_plate.stl screws into the 4 bosses; recesses for rubber feet
  shell_assembly.stl  shell + plate + a stand-in knob (viewing only)

Cordless version: the knob's 18 mm insertable base drops into a 46 mm, 18 mm deep pocket.
Its power plug ("USB IN (5V)") sits at the rim, and the cord drops straight through a notch at the back of the pocket.
The top face leans toward the user (front, -y) by TILT_DEG, and the pocket is square to it. Inside: a battery bay framed by ribs on the bottom plate (sized for a
~50 x 34 x 10 mm 1S LiPo), room behind it for a USB-C charge + 5 V boost board at the back wall, a USB-C slot,
a charge-LED hole and a slide-switch slot on the right end.

VERIFY against the real unit: notch size vs the USB IN plug and cord, and the charge board size.
"""
import os
import numpy as np
import manifold3d as mf

# ---------------- parameters ----------------
L, W, H = 90.0, 64.0, 38.5          # outline (2026-10-31: 37 -> 38.5 so the charge board can ride 1.2 mm higher without thinning the knob pocket floor)
CORNER_R = 12.0                     # plan-view corner radius
TOP_FILLET = 3.0                    # rounded top edge
BOT_CHAMFER = 0.8                   # small chamfer on the bed edge (printed upside down: this is the top in print)
WALL, ROOF = 2.4, 2.4               # side wall and top skin thickness
KNOB_D = 48.0                       # 1.28" knob display: 48 x 48 x 33 mm
KNOB_H = 33.0
KNOB_X = 0.0                        # knob centre offset along the length (0 = centred)
POCKET_D = 46.2                     # press fit chosen from fit-test plate (2 dots = 46.2 was perfect, 2026-10-03)
RIB_N, RIB_W, RIB_PROUD, RIB_LEAD = 0, 1.2, 0.35, 3.0   # crush ribs on the pocket wall: 46.3 mm across the ribs, tapered over the top 3 mm
KNOB_INSERT = 18.0                  # height of the knob's insertable portion
POCKET_DEPTH = KNOB_INSERT         # knob sits directly on the pocket floor (risers dropped 2026-10-01)
POCKET_FLOOR = 2.5                  # ledge ring thickness under the knob
SEAT_RING = 0                       # 0 = solid pocket floor; >0 = knob rests on a ring this wide, middle open
TILT_DEG = 7.5                      # top face (and knob) leans toward the user (front, -y); ~10% slope
CORD_CUT = 20.0                     # D-shaped cord opening: the back segment of the pocket floor, this far in from the rim (~47 mm chord, 3.5 mm short of centre),
CORD_NOTCH_ANGLE = 90               # under the knob's 'USB IN (5V)' plug, which sits at the edge with the cord exiting sideways
FIT = 0.2                           # clearance for the bottom plate
PLATE_T = 2.0
BOSS_D, BOSS_HOLE_D = 6.0, 2.2      # M2.5 self-tapping
BOSS_INSET = 7.0                    # boss centre from the outer outline edge (along both axes of the corner)
USBC_W, USBC_H, USBC_Z = 9.6, 3.8, 6.8   # panel slot on the back wall (+y), centre height from the bed
USBC_RECESS = (12.5, 7.0, 1.4)      # outside recess around the slot so the plug overmold reaches (leaves 1.0 mm wall)
LED_D, LED_X = 1.8, 16.0              # charge-LED hole on the back wall, beside the USB-C slot
SWITCH_W, SWITCH_H, SWITCH_Z = 7.0, 3.5, 8.0   # slide-switch slot on the right end wall (+x)
BATT = (52.0, 36.0)                 # battery bay inside size (cell ~50 x 34 x 10 + slack)
BATT_Y = -10.0                      # bay centre: toward the front, leaving the back for the charge board
BATT_X = -6.8                       # bay centre along x: pushed left until its rib meets the front-left screw column (2026-10-26)
RIB_T, RIB_H = 1.2, 4.0
FOOT_D, FOOT_DEPTH = 10.0, 1.0
SEG = 128
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stl")

mf.set_circular_segments(SEG) if hasattr(mf, "set_circular_segments") else None

# ---------------- helpers ----------------
def rrect(l, w, r, n=24):
    """Rounded-rectangle polygon (CCW), centred on the origin."""
    r = max(min(r, l/2 - 1e-3, w/2 - 1e-3), 0.01)
    pts = []
    for cx, cy, a0 in ((l/2-r, w/2-r, 0), (-l/2+r, w/2-r, 90), (-l/2+r, -w/2+r, 180), (l/2-r, -w/2+r, 270)):
        for a in np.radians(np.linspace(a0, a0+90, n)):
            pts.append((cx + r*np.cos(a), cy + r*np.sin(a)))
    return pts

def rrect_slab(l, w, r, z0, z1):
    cs = mf.CrossSection([rrect(l, w, r)])
    return mf.Manifold.extrude(cs, z1 - z0).translate((0, 0, z0))

def cyl(d, z0, z1, x=0.0, y=0.0):
    return mf.Manifold.cylinder(z1 - z0, d/2, d/2, SEG).translate((x, y, z0))

def outer_body():
    """Convex hull of rounded-rect slices: bottom chamfer, straight sides, quarter-round top edge."""
    slices = [(BOT_CHAMFER, 0.0), (0.0, BOT_CHAMFER), (0.0, H - TOP_FILLET)]
    for a in np.radians(np.linspace(0, 90, 12)[1:]):
        inset = TOP_FILLET * (1 - np.cos(a))
        slices.append((inset, H - TOP_FILLET + TOP_FILLET*np.sin(a)))
    pts = []
    for inset, z in slices:
        pts += [(x, y, z) for x, y in rrect(L - 2*inset, W - 2*inset, CORNER_R - inset)]
    return mf.Manifold.hull_points(pts)

K = np.tan(np.radians(TILT_DEG))

def shear_top(m):
    """Tilt the top: z' = z + K*y*z/H. The bed face stays flat; the top becomes a plane through (0,0,H),
    lower at the front (-y), higher at the back (+y)."""
    def f(v):
        v = np.array(v, copy=True)
        v[:, 2] = v[:, 2] + K * v[:, 1] * v[:, 2] / H
        return v
    return m.warp_batch(f)

def seat(m):
    """Place a tool/part defined with z=0 at the top-surface centre onto the tilted top, square to it."""
    return m.rotate((TILT_DEG, 0, 0)).translate((KNOB_X, 0, H + K*0))

def boss_xy():
    dx, dy = L/2 - BOSS_INSET, W/2 - BOSS_INSET
    return [(sx*dx, sy*dy) for sx in (-1, 1) for sy in (-1, 1)]

# ---------------- parts ----------------
def make_shell():
    body = outer_body()
    pocket_r = POCKET_D/2
    # hollow underneath, keeping a solid collar around the pocket walls; the seat ring is added after the tilt
    cavity = rrect_slab(L - 2*WALL, W - 2*WALL, CORNER_R - WALL, -1, H - ROOF)
    collar = cyl(2*(pocket_r + WALL), H - POCKET_DEPTH, H + 1, x=KNOB_X)
    cavity = cavity - collar
    for x, y in boss_xy():
        cavity = cavity - cyl(BOSS_D, PLATE_T, H, x, y)
    shell = shear_top(body - cavity)
    # the shear nudges the boss bottoms; re-clear the plate seat so the plate sits flat
    shell = shell - rrect_slab(L - 2*WALL, W - 2*WALL, CORNER_R - WALL, -1, PLATE_T)
    for x, y in boss_xy():
        shell = shell - cyl(BOSS_HOLE_D, -1, H - ROOF - 3.0, x, y)
    # knob pocket and cord notch, square to the tilted top
    shell = shell + seat(cyl(2*(pocket_r + WALL), -POCKET_DEPTH - POCKET_FLOOR, -POCKET_DEPTH + 4))   # seat ring, square to the top
    shell = shell - seat(cyl(2*pocket_r, -POCKET_DEPTH, 5))
    for i in range(RIB_N):                                              # crush ribs, at 0/60/120... deg (none at the back, 90)
        r0 = POCKET_D/2 - RIB_PROUD
        rib = mf.Manifold.hull_points([(x, y, z) for z, d in ((-POCKET_DEPTH, RIB_PROUD), (-RIB_LEAD, RIB_PROUD), (0.0, 0.01))
                                       for x in (r0 + RIB_PROUD - d, r0 + RIB_PROUD + 0.5) for y in (-RIB_W/2, RIB_W/2)])
        shell = shell + seat(rib.rotate((0, 0, 60*i)))
    if SEAT_RING > 0:
        shell = shell - seat(cyl(2*(pocket_r - SEAT_RING), -60, 0))   # open centre inside the seat ring
    # D-shaped cord opening: the back segment of the pocket floor
    seg = cyl(2*pocket_r, -POCKET_DEPTH - 20, 0) ^ mf.Manifold.cube((POCKET_D + 2, CORD_CUT + 1, 40)).translate((-POCKET_D/2 - 1, pocket_r - CORD_CUT, -POCKET_DEPTH - 30))
    shell = shell - seat(seg.rotate((0, 0, CORD_NOTCH_ANGLE - 90)))
    # ports stay level (cut after the tilt)
    usbc = mf.Manifold.cube((USBC_W, WALL*3, USBC_H), True).translate((0, W/2 - WALL/2, USBC_Z))
    shell = shell - usbc
    rw, rh, rd = USBC_RECESS
    shell = shell - mf.Manifold.cube((rw, rd*2, rh), True).translate((0, W/2, USBC_Z))
    led = mf.Manifold.cylinder(WALL*3, LED_D/2, LED_D/2, 32).rotate((-90, 0, 0)).translate((LED_X, W/2 - WALL*1.5, USBC_Z))
    shell = shell - led
    sw = mf.Manifold.cube((WALL*3, SWITCH_W, SWITCH_H), True).translate((L/2 - WALL/2, 0, SWITCH_Z))
    shell = shell - sw
    return max(shell.decompose(), key=lambda p: p.volume())          # drop zero-volume slivers where a rib meets the floor

def knob_seat_z():
    """Height of the knob's bottom face at its centre (it sits on the tilted pocket floor)."""
    return H - POCKET_DEPTH

def make_plate():
    plate = rrect_slab(L - 2*WALL - 2*FIT, W - 2*WALL - 2*FIT, CORNER_R - WALL - FIT, 0, PLATE_T)
    for x, y in boss_xy():
        plate = plate - cyl(2.8, -1, PLATE_T + 1, x, y)                 # M2.5 clearance
        plate = plate - cyl(5.0, -1, 1.2, x, y)                         # countersink-ish head recess
    fx, fy = L/2 - WALL - 13, W/2 - WALL - 8
    for sx in (-1, 1):
        for sy in (-1, 1):
            plate = plate - cyl(FOOT_D, -1, FOOT_DEPTH, sx*fx, sy*fy)
    bx, by = BATT                                                       # battery bay ribs
    frame = rrect_slab(bx + 2*RIB_T, by + 2*RIB_T, 2, PLATE_T - 0.01, PLATE_T + RIB_H) - rrect_slab(bx, by, 1, PLATE_T - 1, PLATE_T + RIB_H + 1)
    return plate + frame.translate((KNOB_X, BATT_Y, 0))

def make_knob_standin():
    return seat(cyl(POCKET_D, -POCKET_DEPTH, 0) + cyl(KNOB_D, 0, KNOB_H - KNOB_INSERT))

# ---------------- output ----------------
def to_np(m):
    mesh = m.to_mesh()
    return np.asarray(mesh.vert_properties)[:, :3].astype(float), np.asarray(mesh.tri_verts).astype(int)

def write_stl(path, m):
    import struct
    V, F = to_np(m)
    tri = V[F]
    n = np.cross(tri[:, 1]-tri[:, 0], tri[:, 2]-tri[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    with open(path, 'wb') as f:
        f.write(b'Knob shell'.ljust(80, b' ')); f.write(struct.pack('<I', len(F)))
        data = np.zeros(len(F), dtype=[('n', '<f4', 3), ('v', '<f4', 9), ('a', '<u2')])
        data['n'] = n; data['v'] = tri.reshape(-1, 9)
        f.write(data.tobytes())

if __name__ == "__main__":
    os.makedirs(f"{OUT}/old_one_piece", exist_ok=True)                     # superseded by puck_tub + puck_lid below
    shell, plate, knob = make_shell(), make_plate(), make_knob_standin()
    write_stl(f"{OUT}/old_one_piece/shell.stl", shell.mirror((0, 0, 1)).translate((0, 0, H)))   # print top-down
    write_stl(f"{OUT}/old_one_piece/bottom_plate.stl", plate)
    write_stl(f"{OUT}/old_one_piece/shell_assembly.stl", shell + plate + knob)
    for n, p in (("shell", shell), ("plate", plate)):
        print(n, p.num_tri(), "tris, vol %.1f cm3, genus %d" % (p.volume()/1000, p.genus()))

# ---------------- tub + lid split ----------------
# The shell is split on a level plane just under the tilted pocket floor. The lid is solid below the pocket, so it
# prints split-face-down with no bridges or supports; the tub has its own floor (no bottom plate).
SPLIT = 13.0                        # joint height (tub depth); the knob pocket's lowest point is ~15.8; the battery pokes into a shallow pocket under the lid
TUB_FLOOR = 2.0
SCREW_CLEAR, SCREW_CBORE, CBORE_DEPTH = 3.4, 6.2, 5.0  # M3 x 16 (head sunk 5 so 8 mm bites the lid; was 6 mm bites the lid) socket head from underneath
PILOT_D, PILOT_DEPTH = 2.6, 8.0                          # self-tap into the lid
CB_W, CB_T = 20.4, 2.0              # charge board width and edge thickness
CB_FIT = 0.3                        # slot clearance on width and thickness
CB_EXTRA_T = 0.4                    # extra groove height
CB_RAIL_L, CB_LIP = 16.0, 1.0       # rail length from the back wall; how far the rails overlap the board edges
CB_Z0 = USBC_Z - 1.6 - CB_T         # board bottom: its top-mounted USB-C (3.2 tall) centres on the slot; 3.2 = 1.2 mm over the floor for the solder under it
CB_SINK = 0.0                       # floor pocket under the charge board
DECK_Z = USBC_Z - 1.6 + 0.5         # battery rib tops: 0.5 mm over the charge board's top (the battery only overhangs the board's bare front edge)
BAT_T = 10.0                        # battery thickness
LID_BAT_CLEAR = 0.4                 # gap over the battery in the lid's underside pocket
CB_L_MAX = 26.6                     # longest charge board (from the back wall) the deck support stays clear of; measured >= 26.1
RK_LIP = (20.76, 12.60)             # rocker's front lip: along the wall, height; 13.94 deep overall, ~12 behind the lip
RK_CUT = (19.0, 10.8)               # cutout, smaller than the lip so the rocker snaps in from outside on its side tabs (body size estimated; confirm)
RK_TOP = SPLIT                      # the cutout is a notch in the top of the tub wall; the lid's wall closes its top edge
RK_Z = RK_TOP - RK_CUT[1]/2         # cutout centre, on y = 0
RK_PANEL = 1.6                      # wall thinned to this on the inside around the cutout so the side tabs catch (they want a 1-2 mm panel)
RK_THIN_W = 25.0                    # width of the thinned patch along the wall
RK_LID_NOTCH = 2.4                   # the rocker stands ~2 mm above the split + 0.4 clearance: matching notch in the lid's rim, lip-wide
RK_DEPTH = 12.0                     # body + terminals behind the lip

def pocket_tools():
    pocket_r = POCKET_D/2
    tools = seat(cyl(2*pocket_r, -POCKET_DEPTH, 5))
    seg = cyl(2*pocket_r, -POCKET_DEPTH - 40, 0) ^ mf.Manifold.cube((POCKET_D + 2, CORD_CUT + 1, 80)).translate((-POCKET_D/2 - 1, pocket_r - CORD_CUT, -POCKET_DEPTH - 60))
    return tools + seat(seg.rotate((0, 0, CORD_NOTCH_ANGLE - 90)))

def make_lid():
    body = shear_top(outer_body()) ^ rrect_slab(L + 2, W + 2, CORNER_R, SPLIT, H + 20)
    lid = body - pocket_tools()
    # shallow pocket in the lid's underside for the top of the battery (2026-10-26), so the case keeps its 37 mm height;
    # it runs 1.5 mm past the battery into the cord opening so no thin sliver is left between them
    # 2026-10-31: the recess now covers the whole inside of the case (switch area included), keeping solid
    # columns around the screw pilots
    rec = rrect_slab(L - 2*WALL, W - 2*WALL, CORNER_R - WALL, SPLIT - 1, DECK_Z + BAT_T + LID_BAT_CLEAR)
    for x, y in boss_xy():
        rec = rec - cyl(BOSS_D + 2, SPLIT - 2, SPLIT + 10, x, y)
    lid = lid - rec
    nw = RK_LIP[0] + 0.3                                                # notch in the lid's right end rim for the top of the rocker
    lid = lid - mf.Manifold.cube((WALL + 3, nw, RK_LID_NOTCH + 1)).translate((L/2 - WALL - 1, -nw/2, SPLIT - 1))
    for x, y in boss_xy():
        lid = lid - cyl(PILOT_D, SPLIT - 1, SPLIT + PILOT_DEPTH, x, y)
    return max(lid.decompose(), key=lambda p: p.volume())

def make_tub():
    body = shear_top(outer_body()) ^ rrect_slab(L + 2, W + 2, CORNER_R, -1, SPLIT)
    cavity = rrect_slab(L - 2*WALL, W - 2*WALL, CORNER_R - WALL, TUB_FLOOR, SPLIT + 1)
    for x, y in boss_xy():
        cavity = cavity - cyl(BOSS_D + 2, 0, SPLIT + 2, x, y)
    tub = body - cavity
    bx, by = BATT                                                       # battery bay ribs on the floor
    yi = W/2 - WALL
    # battery ribs (2026-10-26): open strips the battery lies on, pushed against the front wall and left until they meet the
    # front-left screw column. The two strips in line with the charge board stop well short so the board can be dropped in
    # and slid back into its rails; the battery's back edge overhangs the board's bare front edge.
    yf = -(W/2 - WALL)
    ys = yi - CB_L_MAX - 0.5                                            # rib ends, just short of the longest board
    y_ins = yi - CB_RAIL_L - CB_L_MAX - 0.5                             # in line with the board: room to drop it in and slide it back
    yb = yf + by - 1.4                                                  # battery back stop (34.6 mm bay for the 34 mm cell)
    x0, x1 = BATT_X - bx/2, BATT_X + bx/2
    z0 = TUB_FLOOR - 0.01
    clear = lambda xa, xb: xb < -CB_W/2 - 0.4 or xa > CB_W/2 + 0.4      # outside the board's width
    xs = list(np.linspace(x0, x1 - 1.6, 6)[1:-1])
    deck = mf.Manifold()
    for x in xs:
        deck = deck + mf.Manifold.cube((1.6, (ys if clear(x, x + 1.6) else y_ins) - yf + 0.5, DECK_Z - z0)).translate((x, yf - 0.5, z0))
    for xa, xb in ((x0 - RIB_T, x0), (x1, x1 + RIB_T)):                 # side strips: battery rests on their inner ledge, 3 mm lips
        deck = deck + mf.Manifold.cube((xb - xa, yb + RIB_T - yf + 0.5, DECK_Z + 3.0 - z0)).translate((xa, yf - 0.5, z0))
        deck = deck + mf.Manifold.cube((1.6, yb - yf + 0.5, DECK_Z - z0)).translate(((xb if xa < BATT_X else xa - 1.6), yf - 0.5, z0))
    for xa, xb in ((x0 - RIB_T, -CB_W/2 - 1.5), (CB_W/2 + 1.5, x1 + RIB_T)):   # back stops either side of the board
        deck = deck + mf.Manifold.cube((xb - xa, RIB_T, DECK_Z + 3.0 - z0)).translate((xa, yb, z0))
    tub = tub + deck
    # charge-board tray: two grooved rails on the back wall; the board slides in toward the wall, USB-C first
    yi = W/2 - WALL
    gw, gt = CB_W/2 + CB_FIT/2, CB_T + CB_FIT + CB_EXTRA_T
    if CB_SINK > 0:
        tub = tub - mf.Manifold.cube((2*gw + 0.4, CB_L_MAX + 1.0, CB_SINK + 0.01)).translate((-gw - 0.2, yi - CB_L_MAX - 1.0, TUB_FLOOR - CB_SINK))   # board pocket in the floor
    for sx in (-1, 1):
        rail = mf.Manifold.cube((gw - CB_LIP + 2.5 - (gw - CB_LIP), CB_RAIL_L, CB_Z0 + gt + 1.5)).translate((gw - CB_LIP, yi - CB_RAIL_L, 0.5))
        rail = mf.Manifold.cube((3.5, CB_RAIL_L + 0.5, CB_Z0 + gt + 1.5 - 0.5)).translate((gw - CB_LIP, yi - CB_RAIL_L, 0.5))
        groove = mf.Manifold.cube((CB_LIP + 0.01, CB_RAIL_L + 2, gt)).translate((gw - CB_LIP - 0.01, yi - CB_RAIL_L - 1, CB_Z0))
        rail = rail - groove
        tub = tub + (rail.mirror((1, 0, 0)) if sx < 0 else rail)
    for x, y in boss_xy():
        tub = tub - cyl(SCREW_CLEAR, -1, SPLIT + 1, x, y) - cyl(SCREW_CBORE, -1, CBORE_DEPTH, x, y)
    fx, fy = L/2 - 15, W/2 - 10
    for sx in (-1, 1):
        for sy in (-1, 1):
            tub = tub - cyl(FOOT_D, -1, 0.8, sx*fx, sy*fy)
    usbc = mf.Manifold.cube((USBC_W, WALL*3, USBC_H), True).translate((0, W/2 - WALL/2, USBC_Z))
    rw, rh, rd = USBC_RECESS
    tub = tub - usbc - mf.Manifold.cube((rw, rd*2, rh), True).translate((0, W/2, USBC_Z))
    tub = tub - mf.Manifold.cylinder(WALL*3, LED_D/2, LED_D/2, 32).rotate((-90, 0, 0)).translate((LED_X, W/2 - WALL*1.5, USBC_Z))
    # snap-in rocker on the right end wall (2026-10-31): a notch in the top of the wall, its top edge closed by the lid, so the
    # case keeps its height. The rocker snaps in from outside before the lid goes on; the wall is thinned around it for the tabs.
    cw, ch = RK_CUT
    tub = tub - mf.Manifold.cube((WALL*3, cw, ch + 1.0)).translate((L/2 - WALL*2, -cw/2, RK_TOP - ch))
    tub = tub - mf.Manifold.cube((WALL - RK_PANEL + 3, RK_THIN_W, RK_TOP - TUB_FLOOR + 1.0)).translate((L/2 - RK_PANEL - (WALL - RK_PANEL + 3), -RK_THIN_W/2, TUB_FLOOR))
    return max(tub.decompose(), key=lambda p: p.volume())

if __name__ == "__main__":
    tub, lid = make_tub(), make_lid()
    write_stl(f"{OUT}/puck_tub.stl", tub)                                         # print floor-down
    write_stl(f"{OUT}/puck_lid.stl", lid.translate((0, 0, -SPLIT)))               # print split-face-down (as exported)
    write_stl(f"{OUT}/puck_tub_lid_assembly.stl", tub + lid + make_knob_standin())
    for n, p in (("tub", tub), ("lid", lid)):
        print(n, p.num_tri(), "tris, vol %.1f cm3, genus %d, pieces %d" % (p.volume()/1000, p.genus(), len(p.decompose())))
