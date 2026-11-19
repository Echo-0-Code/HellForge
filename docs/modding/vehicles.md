# New vehicles

<!-- diataxis: how-to -->

This page is for code mod authors who want a vehicle of their own: an FRV, a tank or an exosuit that a Pelican drops.
You should know how to add an entity and a stratagem: see [new entities](content.md#new-entities).

A new vehicle is a copy of a vanilla one with records of its own, the way the game makes its own variants. It can
change any record, and it can have a model of its own as long as the model keeps its base's joints. A vehicle built
from nothing (its own skeleton, seats, drivetrain and camera) is not possible yet.

## Contents

- [A copy with stats of its own](#a-copy-with-stats-of-its-own)
- [A model of its own](#a-model-of-its-own)
- [Calling it in](#calling-it-in)
- [What stays the base's](#what-stays-the-bases)

## A copy with stats of its own

Register an entity with the vanilla vehicle as `base` and `own_all = 1`, so every record is the copy's own and your
changes leave the vanilla vehicle alone. Change records with `set`:

```c
static const HfSet rallySet[] = {{HF_FIELD(VehicleMotion, steering_response_speed), HF_F32(6.2f)}, {0}};
static const HfEntityDef rally = {.size = sizeof(HfEntityDef), .name = "yourname/your_mod/rally_frv",
    .base = 0xCC21C7FFD3EBEFB9ull, .own_all = 1, .set = rallySet,
    .designation = "M-102R", .title = "Rally FRV", .description = "An M-102 with a quicker steering rack."};
```

Take a weapon off with `{HF_FIELD(Mount, slot0_entity), HF_U64(0)}`: the slot then carries nothing.
Each seat is an interact zone of `Interactable`. Close one by setting its type and its radius to 0, as unused zones
have them: `{HF_FIELD(Interactable, zone2_type), HF_U32(0)}` and `{HF_FIELD(Interactable, zone2_radius), HF_F32(0)}`
close the FRV's roof seat. With the type alone, the seat can't be entered but still shows its prompt. On the M-102, zones 0 to 4 are its five
seats (type 19), and zone 2 is the roof seat, which the M-103 turns into its supply rack (type 9).

An exosuit's arms are mounts: give it arms of your own with `mount`, as the `vehicle-variants` example does. A ground
vehicle ignores `model_from`, because its seats, wheels, tracks and damage zones name its own model's parts.

## A model of its own

Give the copy a unit of your own: a copy of the vehicle's unit with your mesh in parts (`"parts": true` on a unit
copy's `mesh`, [data mods](data.md#copy)), named in the entity's `unit`:

```json
{ "package": "yourname/your_mod/buggy", "load": "session", "resources": [
  { "type": "unit", "copy": "0xCC21C7FFD3EBEFB9", "name": "yourname/your_mod/buggy",
    "mesh": { "obj": "models/buggy.obj", "parts": true } } ] }
```

```c
static const HfEntityDef buggy = {.size = sizeof(HfEntityDef), .name = "yourname/your_mod/buggy",
    .base = 0xCC21C7FFD3EBEFB9ull, .own_all = 1, .unit = "yourname/your_mod/buggy"};
```

The unit keeps every joint of the vehicle's, so its seats, wheels, gun mount and damage zones still fit. Your OBJ is in
the unit's own space at rest: metres, +Y forward, +Z up, +X to the right (with `yUp`, the default, Blender's export of a
model sitting that way). Each object in the file rides the joint it is named after, and moves with it:

| Object name | What it does |
|---|---|
| none (faces before the first `o`) | rides the main bone, the vehicle's body |
| a joint's name, such as `l_front_rotation` | turns and moves with that joint |
| a joint's name with Blender's number, `l_front_rotation.001` | the same joint |

Blender can also write the OBJ Z-up (export with forward Y, up Z) and the mesh then takes `"yUp": false`: Blender's axes are the unit's own.

Put a part where its joint is. `HellForge.Cli unit-joints --unit <unit>` lists a unit's joints with their positions.
The M-102's are:

| Joint | Part | Position (x, y, z) |
|---|---|---|
| `boss` | the body (the main bone) | 0, 0.241, 0.760 |
| `l_front_rotation` | front left wheel | -0.903, 1.704, 0.096 |
| `r_front_rotation` | front right wheel | 0.848, 1.704, 0.096 |
| `l_rear_rotation` | rear left wheel | -0.907, -1.647, 0.094 |
| `r_rear_rotation` | rear right wheel | 0.904, -1.647, 0.094 |
| `steering_wheel` | the steering wheel | -0.361, 0.797, 1.038 |

Other joints of the M-102 a part can ride:

| Joint | Position (x, y, z) |
|---|---|
| `hood` | 0, 1.23, 1.25 |
| `engine` | 0, 1.57, 0.95 |
| `door_fwd_l`, `door_fwd_r` | ∓0.77, 1.04, 1.01 |
| `door_bck_l`, `door_bck_r` | ∓0.74, 0.09, 1.03 |
| `l_top_lid`, `r_top_lid` (roof lids) | -0.37, 0.27, 1.59 / 0.35, 0.27, 1.59 |
| `gun_mount` | 0, -1.20, 1.65 |

Each corner's suspension has joints too, such as `l_f_upper_wishbone`, `l_f_lower_wishbone` and `l_front_steering`.
The M-103 and M-104 have units of their own with the same joints.

An exosuit's legs are joints too, so a body in parts walks with them. The EXO-45's unit (the EXO-49, 51 and 55 share
it) has, among others:

| Joint | Part | Position (x, y, z) |
|---|---|---|
| `boss` | the hips (the main bone) | 0, -0.54, 2.06 |
| `thigh_l`, `thigh_r` | upper legs | ∓0.87, -0.62, 2.13 |
| `knee_upper_l`, `knee_upper_r` | knees | -0.87, -0.03, 1.34 / 0.85, 0.02, 1.37 |
| `knee_lower_l`, `knee_lower_r` | lower legs | -0.87, -0.42, 1.09 / 0.86, -0.37, 1.11 |
| `foot_l`, `foot_r` | feet | ∓0.87, 0, 0.22 |
| `cockpit` | the cockpit, which turns with the aim | 0, -0.49, 2.93 |
| `attach_turret_l`, `attach_turret_r` | where the arms mount | ∓1.05, -0.43, 3.28 |

A vehicle's meshes are split into groups by material. A part goes in the first group whose bones reach its joint and
draws with that group's material: the M-102's wheels draw with its tyre material and the body with its body material.
Where a level of detail or a shadow does not reach a joint, that part rides the nearest joint above it there.

The `vehicle-model` example is an M-102 under a body of boxes, with its wheels on the four wheel joints.

## Calling it in

A Pelican drop stratagem carries the vehicle. Add a stratagem whose base is the vanilla vehicle's, with your vehicle
and the transport Pelican as its payload:

```c
static uint64_t payload[2];
payload[0] = hf->register_entity(mod, &buggy);
payload[1] = HF_ENTITY_PELICAN_TRANSPORT;
HfStratagem s = {sizeof s, .base = HF_STRATAGEM_M102_GUNNER_FRV, .name = "yourname/your_mod/buggy",
    .code = "RRDLLUDD", .payload = payload, .payload_count = 2};
hf->add_stratagem(mod, &s);
```

The drop loads the package of its first payload entity, your vehicle's.

## Moving seats and joints

A unit copy's `joints` moves joints of the copy to new rest positions, in the unit's own space (metres, +X right, +Y
forward, +Z up). Name a joint by its name, or by `#` and its 8-digit name hash for one the game names only by hash.
Every joint below a moved one moves with it, and so do physics actors and interact zones riding it.

```json
{ "type": "unit", "copy": "0xCC21C7FFD3EBEFB9", "name": "yourname/your_mod/quad",
  "joints": { "attach_driver": [0, 0.1, 0.75], "interact_driver": [-0.9, 0.1, 0.3], "steering_wheel": [0, 0.5, 1.0] } }
```

A seat is two joints: where the player sits (`attach_driver` and its passengers) and where they get in (the interact
zone's joint, `interact_driver` and its passengers). The M-102's are:

| Seat | Sits at | Gets in at | Zone |
|---|---|---|---|
| driver | `attach_driver` (-0.36, 0.60, 0.60) | `interact_driver` (-1.76, 0.60, 0.23) | 3 |
| front passenger | `#b8d4b9d0` (0.36, 0.60, 0.60) | `#2b131d4f` (1.76, 0.60) | 4 |
| rear left | `#cf12eb4f` (-0.36, -0.21, 0.60) | `#b10b8ce4` (-1.76, -0.21) | 0 |
| rear right | `#46540ee8` (0.36, -0.21, 0.60) | `#02e8c349` (1.76, -0.21) | 1 |
| roof gunner | the gun mount | `#1bec92c7` (0, -2.75, 0.39) | 2 |

`Interactable`'s `zoneN_joint` and `zoneN_offset_x/y/z` also set where a zone sits, per entity. The animations a
seated player plays stay the base's, so a seat moved far from where it was may look off.

## Physics of its own

A unit copy gets its base's physics (collision, mass and wheels) under its own name. Its `physics` changes that, in
one of two ways:

- `{"file": "physics/quad.physics"}`: a whole physics resource from the mod folder. It must have been made from the
  copy's base (the unit at +0x08 is the base's), and HellForge renames it to the copy.
- `{"bytes": [{"at": "0x58", "expect": "00000000", "set": "0000803F"}]}`: byte edits on the copy of the base's
  physics, each checked against what the base holds there, as a `replace` is.

The physics resource's layout (actors, shapes, Havok shape data and the wheel table) is in the project's vehicle
findings. A mod that brings its own physics is marked as unchecked for mod sync.

`Vehicle`'s `wheelN_joint_a`, `wheelN_steering_joint`, `wheelN_rotation_joint` and `wheelN_joint_d` name the joints that
physics wheel N turns. The FRV's wheels are, in order, left rear, right rear, left front, right front. A vehicle with
fewer wheels in its physics (a motorbike's two) names the joints of its own.

## What stays the base's

- The drivetrain, unless the copy brings physics of its own.
- Animations, the camera, and the HUD. A seat can be moved or closed, not added.
- Damage zones and where they are.
- Room: the game's vehicle tables have space for about 3 new tracked vehicles and 13 ground vehicles in all.

Models HellForge cannot rewrite yet are refused with the reason, such as the tanks' bone weights.
