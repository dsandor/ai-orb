# Orb Array enclosure

`orb_array.scad` builds a box for one or more round 1.28" GC9A01 displays. Open it in OpenSCAD and use the Customizer panel, or export from the command line:

```sh
openscad --backend=manifold -D 'part="shell"' -D 'layout="arc"' -D count=5 -o shell.stl orb_array.scad
```

Set `part` to `shell`, `lid` or `stand` to export each piece, `plate` for all of them laid out together, or `assembly` for a preview with ghost displays. The console lists each display's position and header direction, and prints a `WARNING` when two displays or headers would collide.

## Layouts
| `layout` | Uses | Notes |
|---|---|---|
| `row`, `column` | `count` | headers point down (row) or right (column) |
| `grid` | `rows`, `cols` | headers point outward when they can; a grid of 3 x 3 or more stretches the row pitch to fit them |
| `hex` | `rows`, `cols`, `hex_trim` | honeycomb |
| `pyramid` | `levels`, `pyramid_inverted` | triangle stack |
| `diamond` | `levels` | 1, 2 ... n ... 2, 1 |
| `arc` | `count`, `arc_radius`, `arc_style` | gauge-cluster curve, `rainbow` or `smile` |
| `ring` | `count`, `ring_radius`, `ring_center` | displays on a circle |
| `flower` | `spacing` | one display in the middle and six around it; needs `spacing` of about 48 or more |
| `cross` | `spacing` | plus sign of five |
| `zigzag` | `count`, `zigzag_offset` | alternating up and down |
| `custom` | `custom_positions`, `custom_tab_angles` | your own coordinates, as seen from the front |

Body shapes (`body_shape`): `hull` (smooth pebble around everything), `hug` (follows each display, with `hug_smoothing` blending the gaps), `box`, `capsule` and `circle`.

## How it goes together
- **Display mounting:** each display drops into a keyhole pocket from the back. The glass sits in its own seat behind a bevelled window, and the PCB and header tab sit in the larger pocket behind that.
- **Back panel:** it snaps onto a ledge inside the shell. Slits split its skirt into springy segments, and a bead on each one clicks into a groove. Fingers on the panel press each PCB forward, leaving its header side free for the wires.
- **Controller and USB-C:** the ESP32-S3 Super Mini clips into a cradle on the inside of the back panel, with its USB-C end toward the wall named by `usb_side` (`usb_offset` slides it along that wall). The receptacle drops into a slot in the shell as the panel closes, and its face ends up flush with the outside. A tongue on the panel closes the slot above the port, so from outside you see only a port-sized opening. On a curved wall the outline gets a small flat at the port so the plug seats square (`usb_flat`, automatic by default).
  - Board size: `mcu_size` = 24.1 x 18.2 mm, `usb_protrude` = 2.2 mm, `mcu_pcb_t` = 1.0 mm. Measure the PCB thickness on your board: it sets the port's height in the wall.
  - Assembly: clip the board into the panel, plug the display wires in, then press the panel home.
- **Stands:** print two `stand` feet to set the box on a desk at `stand_tilt`.

## Printing
- **Shell:** print it front face down. The front edge roundover starts as a 45-degree chamfer, so nothing needs supports.
- **Lid:** print it outer face down.
- **Stand:** print it on its side.
- **Settings:** PLA or PETG, 0.2 mm layers, 3 walls, 15% gyroid infill, no supports.

Check the display dimensions against your own module before a big print. They are in the `Display module` section, and the defaults come from the common 7-pin board: a 38.2 mm disc, a 23.15 mm wide header tab that brings the board to 45.5 mm long, and a 35.6 mm glass. Printing a single display (`layout="row"`, `count=1`) is a cheap fit test.
