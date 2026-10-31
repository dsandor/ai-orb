// =============================================================================
//  ORB ARRAY: parametric enclosure for one or more GC9A01 1.28" round displays
// =============================================================================
//
//  Parts (pick with `part`):
//    shell  the front body. Print it face down: the front face is z = 0.
//    lid    the back panel. It snaps into the shell and its fingers clamp each
//           display. Print it outer face down.
//    stand  a desk foot with a tilted slot. Print it on its side; two hold a
//           wide box.
//    plate  every part laid out for printing.
//    assembly  the closed box with ghost displays (preview only).
//
//  Everything is authored as seen from the front: +x right, +y up. The shell
//  and lid are mirrored once at the end so that holds after printing face down.
//
//  Display (round 7-pin GC9A01 module): a 38 mm PCB disc plus a header tab that
//  makes the board about 45.5 mm long. The visible area is 32.4 mm across.
//  Each display sits in a keyhole pocket behind a bevelled window. A shallower
//  pocket holds the LCD glass, and the lid's fingers press the PCB forward.
//
//  Needs OpenSCAD 2021.01 or newer. The Manifold backend renders in seconds.
// =============================================================================

/* [Part] */
part = "assembly"; // [assembly, shell, lid, stand, plate]
// Explode the assembly preview (mm)
explode = 0; // [0:1:40]
// Include the two stand feet on the print plate
plate_stands = true;

/* [Layout] */
layout = "hex"; // [row, column, grid, hex, pyramid, diamond, arc, ring, flower, cross, zigzag, custom]
// Displays in a row, column, arc, ring or zigzag
count = 3; // [1:1:12]
// Rows for grid and hex
rows = 2; // [1:1:6]
// Columns for grid and hex
cols = 3; // [1:1:8]
// Levels for pyramid and diamond
levels = 3; // [2:1:5]
// Centre-to-centre distance between neighbours (mm)
spacing = 44; // [40:0.5:80]
// Hex: odd rows hold one display fewer, giving a symmetric honeycomb
hex_trim = true;
// Pyramid: point it down instead of up
pyramid_inverted = false;
// Arc: radius of the curve the displays follow (mm)
arc_radius = 95; // [40:1:300]
// Arc: rainbow bulges up, smile sags down
arc_style = "rainbow"; // [rainbow, smile]
// Ring: radius, 0 = as tight as spacing allows (mm)
ring_radius = 0; // [0:1:150]
// Ring: put one more display in the middle
ring_center = false;
// Zigzag: vertical offset between neighbours (mm)
zigzag_offset = 22; // [0:1:40]
// Custom: display centres as seen from the front [[x, y], ...] (mm)
custom_positions = [[0, 0], [46, 8], [92, 0]];
// Custom: header direction per display in degrees (0 right, 90 up, -90 down); empty = outward
custom_tab_angles = [];
// Which way each header tab points: auto picks a direction that clears its neighbours
tab_direction = "auto"; // [auto, outward, down, up, left, right]

/* [Body] */
body_shape = "hull"; // [hug, hull, box, capsule, circle]
// Material from a display pocket to the outer skin (mm)
margin = 5.5; // [4:0.5:20]
// Hug: radius that blends the gaps between displays (mm)
hug_smoothing = 9; // [0:0.5:30]
// Box: corner radius (mm)
corner_radius = 10; // [0:0.5:40]
// Total depth, front face to back panel (mm). Dupont plugs need about 26.
depth = 26; // [14:0.5:60]
// Side wall thickness (mm)
wall = 2.4; // [1.6:0.1:5]
// Front edge roundover (mm). Its first part is a 45 degree chamfer so it prints without supports.
front_round = 3; // [0:0.25:8]
// Back edge roundover, limited to leave a 0.8 mm rim (mm)
back_round = 1.2; // [0:0.1:3]

/* [Front face] */
// Front skin thickness in front of the glass (mm)
face_t = 1.6; // [1.2:0.1:3]
// Visible window; the active area is 32.4 mm (mm)
window_d = 33.2; // [30:0.1:36]
// 45 degree bevel around each window (mm)
window_bevel = 0.8; // [0:0.1:1.5]
// Engraved halo ring around each window
halo = true;
// Gap between the window bevel and the halo (mm)
halo_gap = 1.6; // [0.4:0.1:5]
// Halo width (mm)
halo_width = 0.8; // [0.4:0.1:3]
// Halo depth (mm)
halo_depth = 0.4; // [0.2:0.1:1]
// Text engraved under the displays; empty for none
label = "";
// Label cap height (mm)
label_size = 5; // [3:0.5:14]
// Label font
label_font = "Liberation Sans:style=Bold";
// Label engraving depth (mm)
label_depth = 0.6; // [0.2:0.1:1.5]

/* [Display module] */
// LCD glass outline (mm)
glass_d = 35.6;
// LCD glass and backlight thickness (mm)
glass_t = 2.0;
// PCB disc diameter (mm)
pcb_d = 38.2;
// PCB thickness (mm)
pcb_t = 1.6;
// Header tab width (mm)
tab_w = 23.15;
// Header tab reach from the display centre (mm)
tab_reach = 26.5;
// Fit allowance added to the glass, PCB and tab sizes (mm)
fit = 0.4; // [0:0.05:1]
// How far the pocket walls rise around the PCB edge (mm)
pcb_locate = 1.2; // [0.4:0.1:3]

/* [Back panel] */
// Back panel thickness (mm)
lid_t = 2; // [1.6:0.1:4]
// Height of the ledge the panel rests on (mm)
ledge_h = 6; // [4:0.5:10]
// Width of that ledge (mm)
ledge_w = 1.6; // [1.2:0.1:3]
// Gap between the panel's snap skirt and the ledge (mm)
lid_fit = 0.15; // [0:0.05:0.5]
// Snap skirt thickness (mm)
skirt_t = 1.2; // [0.8:0.1:2]
// Snap bead size; 0 = friction fit only (mm)
snap = 0.4; // [0:0.05:0.8]
// Skirt segment length between flex slits (mm)
skirt_segment = 16; // [8:1:40]
// Fingers that press each PCB into its pocket
press_fingers = 3; // [0:1:6]
// Width of each finger along the PCB edge (degrees)
finger_arc = 36; // [15:1:60]
// Extra length that preloads the fingers (mm)
press_preload = 0.3; // [0:0.05:1]
// Hexagonal vent pattern in the back panel
vents = true;
// Vent hole size across flats (mm)
vent_d = 3.2; // [2:0.1:8]
// Vent hole pitch (mm)
vent_pitch = 5; // [3:0.1:12]
// Keyhole slots for hanging on two wall screws
keyholes = false;
// Keyhole spacing (mm)
keyhole_spacing = 60; // [20:1:200]

/* [Controller and USB-C] */
// Wall the controller's USB-C port comes out of, flush with the outside; none = no controller
usb_side = "bottom"; // [bottom, top, left, right, none]
// Slide the port along that wall, measured from the wall's outermost point (mm)
usb_offset = 0; // [-150:0.5:150]
// Board length and width, USB-C on a short side; the default is an ESP32-S3 Super Mini (mm)
mcu_size = [24.1, 18.2];
// Board PCB thickness (mm)
mcu_pcb_t = 1.0;
// How far the USB-C receptacle sticks out past the board edge (mm)
usb_protrude = 2.2;
// USB-C receptacle width and height (mm)
usb_size = [8.94, 3.26];
// Gap around the receptacle in the wall opening (mm)
usb_clearance = 0.25; // [0.1:0.05:0.8]
// Port face against the outside: 0 flush, positive sticks out, negative sits in (mm)
usb_flush = 0; // [-1:0.1:1]
// Flat cut into a curved wall so the plug seats square; -1 = automatic (mm)
usb_flat = -1; // [-1:0.1:4]

/* [Stand] */
// Back tilt of the box (degrees from vertical)
stand_tilt = 15; // [0:1:35]
// Width of one stand foot (mm)
stand_width = 24; // [10:1:100]
// How deep the box sits in the slot (mm)
stand_slot = 9; // [4:0.5:20]

/* [Hidden] */
$fn = 128;
eps = 0.01;
edge_step = 0.2;      // slice height for the roundovers; matches a 0.2 mm layer
min_web = 1.2;        // thinnest acceptable material between two pockets

// =============================================================================
//  Derived sizes
// =============================================================================
pocket_r   = (pcb_d + fit) / 2;
glass_r    = (glass_d + fit) / 2;
window_r   = window_d / 2;
tab_w_f    = tab_w + fit;
tab_reach_f = tab_reach + fit / 2;
deck_t     = face_t + glass_t + pcb_locate;       // solid front deck holding the pockets
pcb_back_z = face_t + glass_t + pcb_t;            // the PCB's back, where the fingers press
z_top      = depth - lid_t;                       // inner face of the back panel
z_ledge    = z_top - ledge_h;                     // bottom of the ledge
z_snap     = z_top - (ledge_h - ledge_w) / 2;     // snap bead height
back_r     = min(back_round, wall - 0.8);
tab_clear  = tab_reach_f + pocket_r + min_web;    // pitch a header tab needs

hexp = spacing * sqrt(3) / 2;
grid_vertical_tabs = layout == "grid" && cols > 2 && rows > 2;
grid_pitch_y = grid_vertical_tabs ? max(spacing, tab_clear) : spacing;
zig_dx = max(sqrt(max(0, spacing * spacing - zigzag_offset * zigzag_offset)), spacing / 2);

// =============================================================================
//  Layouts
// =============================================================================
function vsum(v, i = 0) = i >= len(v) ? [0, 0] : v[i] + vsum(v, i + 1);
function centroid(p) = vsum(p) / len(p);
function center_pts(p) =
  let(xs = [for (q = p) q[0]], ys = [for (q = p) q[1]],
      c = [(min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2])
  [for (q = p) q - c];
function rot2(a, v) = [cos(a) * v[0] - sin(a) * v[1], sin(a) * v[0] + cos(a) * v[1]];

function ring_pts(n, r, start = 90) = [for (i = [0 : n - 1]) r * [cos(start - i * 360 / n), sin(start - i * 360 / n)]];
ring_r = ring_radius > 0 ? ring_radius
       : max(count > 1 ? spacing / (2 * sin(180 / count)) : 0, ring_center ? spacing : 0);
arc_step = 2 * asin(min(1, spacing / (2 * arc_radius)));
diamond_n = function(k) k < levels ? k + 1 : 2 * levels - 1 - k;

raw_positions =
    layout == "row"    ? [for (i = [0 : count - 1]) [i * spacing, 0]]
  : layout == "column" ? [for (i = [0 : count - 1]) [0, -i * spacing]]
  : layout == "grid"   ? [for (r = [0 : rows - 1], c = [0 : cols - 1]) [c * spacing, -r * grid_pitch_y]]
  : layout == "hex"    ? [for (r = [0 : rows - 1], c = [0 : cols - 1 - (hex_trim && r % 2 == 1 ? 1 : 0)])
                            [c * spacing + (r % 2) * spacing / 2, -r * hexp]]
  : layout == "pyramid" ? [for (k = [0 : levels - 1], i = [0 : k])
                            [(i - k / 2) * spacing, (pyramid_inverted ? 1 : -1) * k * hexp]]
  : layout == "diamond" ? [for (k = [0 : 2 * levels - 2], i = [0 : diamond_n(k) - 1])
                            [(i - (diamond_n(k) - 1) / 2) * spacing, -k * hexp]]
  : layout == "arc"    ? [for (i = [0 : count - 1])
                            let(a = 90 - (i - (count - 1) / 2) * arc_step)
                            [arc_radius * cos(a), (arc_style == "rainbow" ? 1 : -1) * arc_radius * sin(a)]]
  : layout == "ring"   ? concat(ring_center ? [[0, 0]] : [], count > 1 ? ring_pts(count, ring_r) : [[0, 0]])
  : layout == "flower" ? concat([[0, 0]], ring_pts(6, spacing))
  : layout == "cross"  ? [[0, 0], [0, spacing], [spacing, 0], [0, -spacing], [-spacing, 0]]
  : layout == "zigzag" ? [for (i = [0 : count - 1]) [i * zig_dx, (i % 2 == 0 ? 1 : -1) * zigzag_offset / 2]]
  : custom_positions;

P = center_pts(raw_positions);
N = len(P);

// Header tab directions: degrees, as seen from the front.
function quant90(a) = round(a / 90) * 90;
function outward(p, c) = let(d = p - c) norm(d) < 0.5 ? -90 : quant90(atan2(d[1], d[0]));
function named_tab(s) = s == "down" ? -90 : s == "up" ? 90 : s == "left" ? 180 : 0;
auto_tabs =
    layout == "row"    ? [for (p = P) -90]
  : layout == "column" ? [for (p = P) 0]
  : layout == "zigzag" ? [for (p = P) p[1] > 0 ? 90 : -90]
  : layout == "arc"    ? [for (p = P) arc_style == "rainbow" ? -90 : 90]
  : layout == "grid"   ? (cols <= 2 ? [for (p = P) p[0] < 0 ? 180 : 0]
                         : rows == 2 ? [for (p = P) p[1] > 0 ? 90 : -90]
                         : [for (p = P) -90])
  : layout == "custom" && len(custom_tab_angles) == N ? custom_tab_angles
  : [for (p = P) outward(p, centroid(P))];
A = tab_direction == "auto" ? auto_tabs
  : tab_direction == "outward" ? [for (p = P) outward(p, centroid(P))]
  : [for (p = P) named_tab(tab_direction)];

// =============================================================================
//  Footprint, outline and fit checks
// =============================================================================
tab_corners = [for (i = [0 : N - 1], s = [-1, 1]) P[i] + rot2(A[i], [tab_reach_f, s * tab_w_f / 2])];
fp_pts = concat([for (p = P, d = [[1, 0], [-1, 0], [0, 1], [0, -1]]) p + d * pocket_r], tab_corners);
fp_miny = min([for (q = fp_pts) q[1]]);

has_label = len(label) > 0;
label_h = label_size * 1.7;
label_w = label_size * 0.72 * len(label) + label_size * 1.5;
label_y = fp_miny - 1.5 - label_h / 2;
label_pts = has_label ? [for (sx = [-1, 1], sy = [-1, 1]) [sx * label_w / 2, label_y + sy * label_h / 2]] : [];

all_pts = concat(fp_pts, label_pts);
bb_min = [min([for (q = all_pts) q[0]]), min([for (q = all_pts) q[1]])] - [margin, margin];
bb_max = [max([for (q = all_pts) q[0]]), max([for (q = all_pts) q[1]])] + [margin, margin];
bb_size = bb_max - bb_min;
// Box and capsule bodies stay symmetric about the displays, so a header tab on one side
// doesn't push the displays off centre.
fp_lo = [min([for (q = fp_pts) q[0]]), fp_miny] - [margin, margin];
fp_hi = [max([for (q = fp_pts) q[0]]), max([for (q = fp_pts) q[1]])] + [margin, margin];
sym_half = [max(-fp_lo[0], fp_hi[0]), max(-fp_lo[1], fp_hi[1])];
box_lo = [min(-sym_half[0], bb_min[0]), min(-sym_half[1], bb_min[1])];
box_hi = [max(sym_half[0], bb_max[0]), max(sym_half[1], bb_max[1])];
circle_r = max(concat([for (p = P) norm(p) + pocket_r], [for (q = tab_corners) norm(q)],
                      [for (q = label_pts) norm(q)])) + margin;

module pocket_2d(a) {
  circle(r = pocket_r);
  rotate(a) translate([0, -tab_w_f / 2]) square([tab_reach_f, tab_w_f]);
}

module footprint() {
  for (i = [0 : N - 1]) translate(P[i]) pocket_2d(A[i]);
  if (has_label) translate([0, label_y]) square([label_w, label_h], center = true);
}

// Each display and its tab blended into one smooth lobe, for the hug body.
module lobes() {
  for (i = [0 : N - 1]) translate(P[i]) hull() pocket_2d(A[i]);
  if (has_label) translate([0, label_y]) square([label_w, label_h], center = true);
}

module rounded_rect(lo, hi, r) {
  rr = max(0.01, min(r, (hi[0] - lo[0]) / 2 - 0.01, (hi[1] - lo[1]) / 2 - 0.01));
  translate(lo + [rr, rr]) offset(r = rr) square(hi - lo - [2 * rr, 2 * rr]);
}

module outline_raw() {
  if (body_shape == "hull") hull() offset(r = margin) footprint();
  else if (body_shape == "hug") offset(r = -hug_smoothing) offset(r = hug_smoothing + margin) lobes();
  else if (body_shape == "circle") circle(r = circle_r, $fn = 256);
  else if (body_shape == "capsule") rounded_rect(box_lo, box_hi, min(box_hi - box_lo) / 2);
  else rounded_rect(box_lo, box_hi, corner_radius);
}

// ---- USB-C exit ---------------------------------------------------------------------
// Everything about the port is built in a local frame: rotate(usb_ang) turns the chosen wall
// into the bottom one, so outward is -y and the wall runs along x.
usb_on = usb_side != "none";
usb_ang = usb_side == "bottom" ? 0 : usb_side == "right" ? 90 : usb_side == "top" ? 180 : 270;
usb_d = rot2(usb_ang, [0, -1]);   // outward
usb_t = rot2(usb_ang, [1, 0]);    // along the wall
box_corners = [box_lo, box_hi, [box_lo[0], box_hi[1]], [box_hi[0], box_lo[1]]];
box_side_len = abs((box_hi - box_lo) * usb_t);
box_depth_len = abs((box_hi - box_lo) * usb_d);
// Candidates for the wall's outermost point: [distance out, position along the wall, curvature radius]
usb_cands =
    body_shape == "circle" ? [[circle_r, 0, circle_r]]
  : body_shape == "box" || body_shape == "capsule" ?
      [[max([for (c = box_corners) c * usb_d]), ((box_lo + box_hi) / 2) * usb_t,
        body_shape == "capsule" && box_side_len <= box_depth_len ? box_side_len / 2 : 0]]
  : concat([for (p = P) [p * usb_d + pocket_r + margin, p * usb_t, pocket_r + margin]],
           [for (q = concat(tab_corners, label_pts)) [q * usb_d + margin, q * usb_t, margin]]);
usb_smax = max([for (c = usb_cands) c[0]]);
usb_ties = [for (c = usb_cands) if (c[0] > usb_smax - 0.05) c];
// Several outermost points in a line mean a straight wall there.
usb_r = len(usb_ties) > 1 ? 0 : usb_ties[0][2];
usb_u = vsum([for (c = usb_ties) [c[1], 0]])[0] / len(usb_ties) + usb_offset;
usb_half = mcu_size[1] / 2 + 0.4;
usb_flat_auto = usb_r == 0 ? 0 : usb_half >= usb_r ? 3
              : min(3, max(0, usb_r - sqrt(usb_r * usb_r - usb_half * usb_half) - 0.7));
usb_flat_eff = usb_on ? (usb_flat >= 0 ? usb_flat : usb_flat_auto) : 0;
usb_wall = usb_smax - usb_flat_eff;                       // outer face of that wall, from the centre
usb_yo = -usb_wall;                                      // ... in the local frame
usb_ye = usb_yo - usb_flush + usb_protrude;              // board's USB-end edge
usb_pw = usb_size[0] + 2 * usb_clearance;                // port window width
usb_zlo = z_top - mcu_pcb_t - usb_size[1] - usb_clearance;
usb_zhi = z_top - mcu_pcb_t + usb_clearance;

module outline() {
  if (usb_on && usb_flat_eff > 0)
    intersection() {
      outline_raw();
      big = 4 * (norm(bb_size) + 50);
      rotate(usb_ang) translate([-big / 2, usb_yo]) square([big, big]);
    }
  else outline_raw();
}

// Distance from point q (relative to a display centre) to that display's header tab.
function tab_dist(q, a) =
  let(l = rot2(-a, q), cx = max(0, min(tab_reach_f, l[0])), cy = max(-tab_w_f / 2, min(tab_w_f / 2, l[1])))
  norm([l[0] - cx, l[1] - cy]);
overlaps = [for (i = [0 : N - 1], j = [0 : N - 1])
              if (i < j && norm(P[j] - P[i]) < 2 * pocket_r + min_web)
                str("displays ", i, " and ", j, " are too close: raise spacing")];
tab_hits = [for (i = [0 : N - 1], j = [0 : N - 1])
              if (i != j && tab_dist(P[j] - P[i], A[i]) < pocket_r + min_web)
                str("header tab of display ", i, " runs into display ", j,
                    ": raise spacing or change tab_direction")];
// Header tabs that collide, checked on a grid of points across each tab.
function in_tab(q, a, grow) =
  let(l = rot2(-a, q)) l[0] > -grow && l[0] < tab_reach_f + grow && abs(l[1]) < tab_w_f / 2 + grow;
tab_tab = [for (i = [0 : N - 1], j = [0 : N - 1])
             if (i < j && len([for (u = [0 : 0.25 : 1], v = [-0.5 : 0.25 : 0.5])
                   let(q = P[i] + rot2(A[i], [pocket_r + u * (tab_reach_f - pocket_r), v * tab_w_f]))
                   if (in_tab(q - P[j], A[j], min_web)) 1]) > 0)
               str("header tabs of displays ", i, " and ", j, " collide")];
warnings = concat(overlaps, tab_hits, tab_tab, margin < wall + 1 ? ["margin is thinner than wall + 1 mm"] : [],
                  usb_on && usb_zlo < pcb_back_z + 6 ? ["box too shallow: the controller would sit on the displays"] : []);

// =============================================================================
//  Shapes
// =============================================================================
// Inset for a printable roundover on the bed side: a 45 degree chamfer that
// turns into a quarter circle once the slope is steep enough.
function td_inset(z, r) =
  let(k = r * (1 - sqrt(0.5)))
  z < k ? 2 * k - z : r - sqrt(max(0, r * r - (r - z) * (r - z)));
function round_inset(z, r) = r - sqrt(max(0, r * r - (r - z) * (r - z)));

// Extrudes children(0) to height h with a printable front roundover and a true back roundover.
module rounded_slab(h, rf, rb) {
  nf = rf > 0 ? ceil(rf / edge_step) : 0;
  nb = rb > 0 ? ceil(rb / edge_step) : 0;
  for (k = [0 : 1 : nf - 1]) {
    z0 = k * rf / nf;
    translate([0, 0, z0]) linear_extrude(rf / nf + eps)
      offset(delta = -td_inset(z0 + rf / nf / 2, rf)) children(0);
  }
  translate([0, 0, rf]) linear_extrude(h - rf - rb) children(0);
  for (k = [0 : 1 : nb - 1]) {
    z0 = h - rb + k * rb / nb;
    translate([0, 0, z0 - eps]) linear_extrude(rb / nb + eps)
      offset(delta = -round_inset(rb - (k + 0.5) * rb / nb, rb)) children(0);
  }
}

// A band inside the outline: from `inner` mm in to `outer` mm in.
module inner_band(outer, inner) {
  difference() {
    offset(delta = -outer) outline();
    offset(delta = -inner) outline();
  }
}

// =============================================================================
//  Shell (front body)
// =============================================================================
module display_cut(a) {
  // Window with a bevel that opens toward the viewer
  translate([0, 0, -eps]) cylinder(h = window_bevel + eps, r1 = window_r + window_bevel + eps, r2 = window_r);
  translate([0, 0, -eps]) cylinder(h = face_t + 2 * eps, r = window_r);
  // LCD glass seat
  translate([0, 0, face_t]) cylinder(h = glass_t + eps, r = glass_r);
  // PCB seat; it opens into the cavity behind
  translate([0, 0, face_t + glass_t]) cylinder(h = deck_t, r = pocket_r);
  // Header tab and the LCD's flex cable, which folds around the glass edge
  translate([0, 0, face_t]) linear_extrude(deck_t)
    rotate(a) translate([0, -tab_w_f / 2]) square([tab_reach_f, tab_w_f]);
  if (halo) {
    hr = window_r + window_bevel + halo_gap;
    translate([0, 0, -eps]) linear_extrude(halo_depth + eps)
      difference() { circle(r = hr + halo_width); circle(r = hr); }
  }
}

// The back panel rests on this ledge. Its underside is a 45 degree chamfer, so it prints unsupported.
module ledge() {
  n = ceil(ledge_w / edge_step);
  for (k = [0 : n - 1])
    translate([0, 0, z_ledge + k * ledge_w / n]) linear_extrude(ledge_w / n + eps)
      inner_band(wall - eps, wall + ledge_w * (k + 1) / n);
  translate([0, 0, z_ledge + ledge_w]) linear_extrude(ledge_h - ledge_w)
    inner_band(wall - eps, wall + ledge_w);
}

// V groove in the ledge that the panel's snap bead clicks into.
module snap_groove() {
  if (snap > 0) {
    n = max(2, ceil(2 * snap / 0.1));
    for (k = [0 : n - 1]) {
      z0 = z_snap - snap + k * 2 * snap / n;
      w = snap - abs(z0 + snap / n - z_snap);
      translate([0, 0, z0]) linear_extrude(2 * snap / n + eps)
        inner_band(wall + ledge_w - w - 0.05, wall + ledge_w + eps);
    }
  }
}

// USB-C exit in the shell: a slot from the rim down to the port that the receptacle drops
// into as the back panel closes, and a shallow relief inside the wall for the board's edge.
// The panel's tongue fills the slot above the port.
module usb_shell_cut() {
  if (usb_on)
    rotate(usb_ang) {
      translate([usb_u - usb_pw / 2, usb_yo - 1, usb_zlo]) cube([usb_pw, wall + 1 + eps, depth]);
      translate([usb_u - usb_half, usb_ye - 0.3, usb_zlo])
        cube([2 * usb_half, usb_yo + wall + ledge_w + 0.6 - (usb_ye - 0.3), depth]);
    }
  // Ledge cleared across the cradle's full width, inside the wall only
  if (usb_on)
    intersection() {
      translate([0, 0, usb_zlo]) linear_extrude(depth) offset(delta = -wall + eps) outline();
      rotate(usb_ang) translate([usb_u - usb_half - 1.6, usb_ye - 0.3, usb_zlo - 1])
        cube([2 * usb_half + 3.2, usb_yo + wall + ledge_w + 6 - (usb_ye - 0.3), depth]);
    }
}

module shell_body() {
  difference() {
    union() {
      difference() {
        rounded_slab(depth, front_round, back_r) outline();
        translate([0, 0, deck_t]) linear_extrude(depth) offset(delta = -wall) outline();
      }
      ledge();
    }
    snap_groove();
    for (i = [0 : N - 1]) translate(P[i]) display_cut(A[i]);
    usb_shell_cut();
    if (has_label)
      translate([0, label_y, -eps]) linear_extrude(label_depth + eps)
        text(label, size = label_size, font = label_font, halign = "center", valign = "center");
  }
}

// =============================================================================
//  Lid (back panel)
// =============================================================================
lid_in = wall + lid_fit;                       // panel edge, inset from the outline
skirt_out = wall + ledge_w + lid_fit;          // skirt outer face, inset from the outline
skirt_h = ledge_h - 0.6;
finger_r_out = pcb_d / 2 - 0.5;
finger_r_in = finger_r_out - 2.0;

// True when a point (as seen from the front) lands on the controller board, grown by `grow`.
function on_board(q, grow) =
  let(l = rot2(-usb_ang, q))
  usb_on && abs(l[0] - usb_u) < mcu_size[1] / 2 + grow && l[1] > usb_ye - grow && l[1] < usb_ye + mcu_size[0] + grow;

module fingers() {
  h = z_top - pcb_back_z + press_preload;
  fr = (finger_r_in + finger_r_out) / 2;
  if (press_fingers > 0)
    for (i = [0 : N - 1]) translate([P[i][0], P[i][1], z_top - h]) {
      for (k = [0 : press_fingers - 1]) {
        c = A[i] + 180 + (k - (press_fingers - 1) / 2) * (300 / max(1, press_fingers));
        // A finger under the controller board is left out
        if (len([for (e = [-finger_arc / 2, 0, finger_arc / 2]) if (on_board(P[i] + rot2(c + e, [fr, 0]), 3)) 1]) == 0)
        rotate(c - finger_arc / 2)
          rotate_extrude(angle = finger_arc, $fn = 96)
            translate([finger_r_in, 0]) square([finger_r_out - finger_r_in, h + eps]);
      }
    }
}

module skirt() {
  difference() {
    union() {
      translate([0, 0, z_top - skirt_h]) linear_extrude(skirt_h + eps) inner_band(skirt_out, skirt_out + skirt_t);
      if (snap > 0) {
        n = max(2, ceil(2 * snap / 0.1));
        for (k = [0 : n - 1]) {
          z0 = z_snap - snap + k * 2 * snap / n;
          w = snap - abs(z0 + snap / n - z_snap);
          translate([0, 0, z0]) linear_extrude(2 * snap / n + eps)
            inner_band(skirt_out - w, skirt_out + eps);
        }
      }
    }
    // Flex slits so each skirt segment can spring in and click
    translate([0, 0, z_top - skirt_h - 1]) linear_extrude(skirt_h + 1 - 0.01)
      for (x = [bb_min[0] : skirt_segment : bb_max[0]], y = [bb_min[1] : skirt_segment : bb_max[1]]) {
        translate([x, bb_min[1] - 1]) square([1, bb_size[1] + 2]);
        translate([bb_min[0] - 1, y]) square([bb_size[0] + 2, 1]);
      }
  }
}

// Board outline in the local frame, grown by `grow`.
module board_2d(grow) {
  translate([usb_u - mcu_size[1] / 2 - grow, usb_ye - grow]) square([mcu_size[1] + 2 * grow, mcu_size[0] + 2 * grow]);
}

// Cradle on the panel's inside: the board lies flat against the panel, parts facing the
// displays, USB-C end toward the wall. Two lips on each long side click over the PCB.
module mcu_cradle() {
  fw = 1.2;          // frame wall
  fh = mcu_pcb_t + 2;
  zp = z_top - mcu_pcb_t - 0.1;   // where the lips hold the PCB
  if (usb_on)
    rotate(usb_ang) {
      translate([0, 0, z_top - fh]) linear_extrude(fh + eps)
        difference() {
          board_2d(0.2 + fw);
          board_2d(0.2);
          translate([usb_u - mcu_size[1], usb_ye - 10]) square([2 * mcu_size[1], 13]);   // open toward the wall
        }
      for (sx = [-1, 1], f = [0.35, 0.8])
        translate([usb_u + sx * (mcu_size[1] / 2 + 0.2), usb_ye + f * mcu_size[0], 0])
          hull() {
            translate([sx > 0 ? -0.5 : 0, -2, zp - 0.05]) cube([0.5, 4, 0.05]);
            translate([sx > 0 ? -0.01 : 0, -2, zp - 0.9]) cube([0.01, 4, 0.05]);
          }
    }
}

// Tongue on the panel that fills the shell's port slot above the receptacle, flush with the outside.
module usb_tongue() {
  if (usb_on)
    intersection() {
      rounded_slab(depth, front_round, back_r) outline();
      rotate(usb_ang) {
        // At panel height: fills the slot and the board relief
        translate([0, 0, z_top]) linear_extrude(lid_t + eps) offset(delta = -lid_fit) union() {
          translate([usb_u - usb_pw / 2, usb_yo - 1]) square([usb_pw, wall + 1.5]);
          translate([usb_u - usb_half, usb_ye - 0.3]) square([2 * usb_half, usb_yo + wall + 0.5 - (usb_ye - 0.3)]);
        }
        // Below it, beside the board's edge: closes the slot down to the top of the port
        translate([usb_u - usb_pw / 2 + lid_fit + 0.01, usb_yo - 1, usb_zhi]) cube([usb_pw - 2 * lid_fit - 0.02, usb_ye - 0.1 - (usb_yo - 1), z_top - usb_zhi + eps]);
      }
    }
}

keyhole_y = max([for (p = P) p[1]]);
module keyhole_2d(head) {
  for (s = [-1, 1]) translate([s * keyhole_spacing / 2, keyhole_y]) {
    circle(d = head ? 8.5 : 4.2, $fn = 48);
    translate([-(head ? 8.5 : 4.2) / 2, 0]) square([head ? 8.5 : 4.2, 7]);
    translate([0, 7]) circle(d = head ? 8.5 : 4.2, $fn = 48);
  }
}

module vent_2d() {
  r = vent_d / 2 / cos(30);
  intersection() {
    offset(delta = -(skirt_out + skirt_t + 2)) outline();
    for (iy = [floor(bb_min[1] / (vent_pitch * sqrt(3) / 2)) : ceil(bb_max[1] / (vent_pitch * sqrt(3) / 2))],
         ix = [floor(bb_min[0] / vent_pitch) - 1 : ceil(bb_max[0] / vent_pitch) + 1])
      translate([ix * vent_pitch + (iy % 2) * vent_pitch / 2, iy * vent_pitch * sqrt(3) / 2])
        rotate(30) circle(r = r, $fn = 6);
  }
}

module vent_keepout_2d() {
  for (i = [0 : N - 1]) translate(P[i]) {
    difference() { circle(r = finger_r_out + 1.5); circle(r = finger_r_in - 1.5); }
  }
  if (usb_on) rotate(usb_ang) board_2d(2.5);
  if (keyholes) offset(delta = 3) keyhole_2d(true);
}

module lid_body() {
  difference() {
    union() {
      // Panel with a small chamfer on its outer edge
      translate([0, 0, z_top]) linear_extrude(lid_t - 0.6) offset(delta = -lid_in) outline();
      translate([0, 0, depth - 0.6 - eps]) linear_extrude(0.6 + eps) offset(delta = -lid_in - 0.6) outline();
      translate([0, 0, depth - 0.6 - eps]) linear_extrude(0.3) offset(delta = -lid_in - 0.3) outline();
      difference() {
        skirt();
        // The board passes the skirt on its way to the wall
        if (usb_on) rotate(usb_ang) translate([0, 0, z_top - skirt_h - 1]) linear_extrude(skirt_h + 2) board_2d(2.2);
      }
      fingers();
      mcu_cradle();
      usb_tongue();
      if (keyholes)
        translate([0, 0, z_top - 3.5]) linear_extrude(3.5 + eps) offset(delta = 2) keyhole_2d(true);
    }
    if (vents)
      translate([0, 0, z_top - 1]) linear_extrude(lid_t + 2) difference() { vent_2d(); vent_keepout_2d(); }
    if (keyholes) {
      translate([0, 0, z_top - 2]) linear_extrude(lid_t + 3) keyhole_2d(false);
      translate([0, 0, z_top - 3.5 + 1.2]) linear_extrude(3.5 - 1.2 + eps + 0.6) keyhole_2d(true);
    }
  }
}

// =============================================================================
//  Stand
// =============================================================================
module stand_2d() {
  slot_w = depth + 0.6;
  cheek = 5;
  floor_t = 3;
  foot_t = 4;
  base_len = depth * 2.4 + 34;
  lean = 0.62;   // share of the foot on the side the box leans to
  block_w = slot_w + 2 * cheek;
  lift = (block_w / 2) * sin(stand_tilt);   // keeps the tilted block's low corner on the bed
  difference() {
    // Foot plus the tilted cradle on its pedestal, blended with concave fillets
    offset(r = -4) offset(r = 4) union() {
      translate([-base_len * lean, 0]) offset(r = 1.5) offset(delta = -1.5) square([base_len, foot_t]);
      hull() {
        translate([0, lift]) rotate(stand_tilt) translate([-block_w / 2, 0])
          offset(r = 2) offset(delta = -2) square([block_w, floor_t + stand_slot]);
        translate([-block_w / 2 - 2, 0]) square([block_w + 4, foot_t]);
      }
    }
    translate([0, lift]) rotate(stand_tilt) translate([-slot_w / 2, floor_t]) square([slot_w, stand_slot * 4]);
    translate([-500, -100]) square([1000, 100]);   // flat underside
  }
}

module stand() {
  linear_extrude(stand_width) offset(r = 1) offset(delta = -1) stand_2d();
}

// =============================================================================
//  Output
// =============================================================================
module shell() { mirror([1, 0, 0]) shell_body(); }
module lid_assembled() { mirror([1, 0, 0]) lid_body(); }
module lid_print() { translate([0, 0, depth]) rotate([180, 0, 0]) lid_assembled(); }

module ghost_displays() {
  mirror([1, 0, 0]) for (i = [0 : N - 1]) translate(P[i]) {
    color([0.02, 0.02, 0.03]) translate([0, 0, face_t + 0.05]) cylinder(h = glass_t - 0.1, r = glass_d / 2);
    color([0.08, 0.15, 0.55]) translate([0, 0, face_t + glass_t + 0.05])
      linear_extrude(pcb_t) { circle(d = pcb_d); rotate(A[i]) translate([0, -tab_w / 2]) square([tab_reach, tab_w]); }
    color([1.0, 0.55, 0.15]) translate([0, 0, face_t - 0.05]) linear_extrude(0.1)
      for (k = [0 : 7]) rotate(k * 45) translate([0, -0.5]) square([5, 1]);
  }
}

module ghost_board() {
  if (usb_on)
    mirror([1, 0, 0]) rotate(usb_ang) {
      color([0.05, 0.35, 0.2]) translate([usb_u - mcu_size[1] / 2, usb_ye, z_top - mcu_pcb_t])
        cube([mcu_size[1], mcu_size[0], mcu_pcb_t]);
      color([0.75, 0.75, 0.78]) translate([usb_u - usb_size[0] / 2, usb_ye - usb_protrude, z_top - mcu_pcb_t - usb_size[1]])
        cube([usb_size[0], usb_protrude + 5, usb_size[1]]);
    }
}

outer_size = body_shape == "circle" ? [2 * circle_r, 2 * circle_r]
           : body_shape == "box" || body_shape == "capsule" ? box_hi - box_lo : bb_size;
echo(str("ORB ARRAY: ", N, " display(s), layout ", layout, ", body ", body_shape,
         ", outer ", round(outer_size[0] * 10) / 10, " x ", round(outer_size[1] * 10) / 10, " x ", depth, " mm"));
for (i = [0 : N - 1])
  echo(str("  display ", i, " at [", round(P[i][0] * 10) / 10, ", ", round(P[i][1] * 10) / 10,
           "], header points ", A[i] == -90 ? "down" : A[i] == 90 ? "up" : A[i] == 0 ? "right" : A[i] == 180 || A[i] == -180 ? "left" : str(A[i], " deg")));
if (usb_on)
  echo(str("  USB-C exits the ", usb_side, " wall ", round(usb_u * 10) / 10, " mm along it",
           usb_flat_eff > 0 ? str(", on a ", round(usb_flat_eff * 10) / 10, " mm deep flat") : ""));
for (w = warnings) echo(str("WARNING: ", w));

if (part == "shell") shell();
else if (part == "lid") lid_print();
else if (part == "stand") stand();
else if (part == "plate") {
  shell();
  translate([bb_size[0] + 8, 0, 0]) lid_print();
  if (plate_stands)
    for (s = [0, 1]) translate([0, -bb_size[1] / 2 - 22 - s * 30, 0]) stand();
}
else {
  color([0.93, 0.93, 0.91]) shell();
  ghost_displays();
  translate([0, 0, explode]) ghost_board();
  color([0.80, 0.80, 0.78]) translate([0, 0, explode]) lid_assembled();
}
