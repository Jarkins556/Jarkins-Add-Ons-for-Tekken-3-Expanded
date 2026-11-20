# Jarkins — add-ons for Tekken 3 Expanded

**Alpha 0.2.0.** An add-on pack for **Tekken 3 Expanded**: six costume slots on the game's own select screen with a name-plate colour each, and a training overlay for Practice and real matches.

It installs the way every Expanded add-on does — one folder in `addons\`, one setup window — and it sits beside TMST (or any other add-on) without touching it.

| Mod | What it does |
| --- | --- |
| **6 Costume Slots Mod** | One colour per key on the select screen: square red, cross blue, triangle green, circle yellow, Start orange, R1 purple. The plate follows the key that picked the fighter, not the costume he ends up in — triangle and circle put him in the game's costumes 1 and 3, yet read green and yellow — and a fighter the game drew itself (CPU, guest, challenge) reads by the costume it shows. **R1 is the sixth slot**, the model only Xiaoyu has (P8); on everyone else R1 changes nothing. Names, models and portraits are untouched: Eddy's third costume stays the game's own TIGER. |
| **Jarkins Lab** | **Input Display** — the last few inputs, in Practice. **Damage Readouts** — combo counter, damage, exact health. |

Everything else on the roadmap (dummy settings, combo trials, frame data, replays, the remaining toggle mods) is not in this alpha.

## Requirements

- **Tekken 3 Expanded 1.5.0 or newer** (add-on hook API 1) — [download the latest release](https://github.com/omarma/tekken3-expanded/releases/latest) — set up once already: its own `Setup Tekken 3.cmd` run to the end, so `.setup\` exists and the game has started at least once.
- **Works alongside TMST's add-ons without requiring them.** Install TMST, both, or neither: nothing in this pack needs it, and neither folder touches the other. A mod that ever does need one will say so here, on that mod, once it is added.
- **No game data is included** — no disc image, no BIOS, no artwork. You bring your own Tekken 3 disc through the Expanded.

## Install

1. Download this repository (**Code → Download ZIP**) or take the release.
2. Drag the `jarkins` folder into the Expanded's own `addons` folder, so it lands at `<Tekken 3 Expanded>\addons\jarkins`. Not inside another add-on's folder, not in the game's root. Don't rename it.
3. Open `addons\jarkins` and run **`Setup Jarkins.cmd`** → **Install & play**. First build takes a few minutes; your mod switches and game data stay. Then **Play** opens the launcher.
4. Launcher → **Mods** → **Jarkins** → tick what you want, then restart the game (the mods read their switches at launch).

The setup window also sets up two things in the Expanded, once, only when they are missing:

- one line in `game.toml` — `mod_function_entry_funcs = ["0x8003D6B0"]` under `[recompiler]`, the HUD routine the plates and the damage readouts draw at (the game generates again once, a minute or two);
- **R1 as the sixth costume**, in the game's own `src\tekken3_native_moves.c`. Only a version of that file it recognises is touched, everything else in it (the release's own fixes included) is left as it was, and anything else is reported in the window and skipped.

## Update / uninstall

- **Update:** delete `addons\jarkins`, drag the new folder in, run **Update & play**. The Mods page keeps your switches (`mods\state.toml`).
- **Uninstall:** close the game, delete `addons\jarkins`, run **Update & play**. The game is back to what it was and your other add-ons' switches stay. The `game.toml` line and the six-key change in the game's source stay too and do nothing without the add-on; delete the line for a byte-identical `game.toml`.

## What is in the folder

| File | What it is |
| --- | --- |
| `README.txt` | the player guide, the full one — install, uninstall, troubleshooting |
| `Setup Jarkins.cmd` | what you double-click after dragging the folder in |
| `setup\jarkins_install.py` | the installer window it opens |
| `addon.cmake` | read by the Expanded's build: links the C sources, stages the packages |
| `src\` | the C sources — plate colours, overlay, input display, damage |
| `mods\` | the package manifests the Mods page lists (`jarkins.lab`, `jarkins.plates`) |

Reporting a problem: `addons\jarkins\workspace\setup.log` and, for the Expanded's own build, `.setup\setup.log` say why something was left out — attach them.

Tekken 3 and all its characters are trademarks of Bandai Namco Entertainment Inc. This is a fan-made, non-commercial add-on, not affiliated with or endorsed by them. No game data is included or distributed by it.
