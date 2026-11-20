JARKINS for Tekken 3 Expanded - ALPHA 0.2.0
==========================================

An add-on for Tekken 3 Expanded: the 6 Costume Slots Mod - each fighter's name
plate coloured by the key that picked him - and Jarkins Lab, the training readouts
(Input Display and Damage Readouts). That is what this alpha ships: the rest of
the roadmap is not in it. It installs the same way TMST does - drag its folder
into the Expanded's own addons folder - and sits beside TMST (or any other
add-on) without touching it.

ALPHA: an early build for people who want to try it and say what breaks. No game
data is included: no disc, no BIOS, no artwork from the game.

INSTALL (three steps)
---------------------
  1. Extract this download: you get one folder,  jarkins , and this file is
     inside it.

  2. Drag the whole  jarkins  folder into TEKKEN 3 EXPANDED'S OWN "addons"
     folder, so it lands at  addons\jarkins . Every add-on lives in that folder
     side by side, one folder each, read from its own addon.cmake. TMST is not
     needed: this add-on builds on its own, and if TMST is installed it just
     sits in its own folder next to this one and is left alone.

         <Tekken 3 Expanded>\addons\jarkins\     <- the add-on goes here

     Not into another add-on's folder (addons\tmst, say), not into the
     Expanded's root, not into mods\. Do not rename the folder.

  3. Open the  jarkins  folder and double-click  "Setup Jarkins.cmd".
     It checks it is in the right place, then opens the add-on's own setup
     window - the same kind TMST's setup has. Click  Install & play : it builds
     the game with the add-on in (a few minutes the first time; your mod settings
     and game data stay), then offers  Play , which opens the launcher.
     Then: Launcher -> Mods -> Jarkins, tick what you want, restart the game
     after toggling (the mods read their switches at launch).

  The Expanded must be set up once before this: without its .setup folder the
  file says so and asks you to run the Expanded's own "Setup Tekken 3.cmd"
  first. That file's  Update & play  builds whatever is in addons\ too, so it
  is an alternative to this one; "Setup Jarkins.cmd" copies nothing into the
  game - it is the same build, in the add-on's own window. It does set up two
  things in the Expanded the first time, because the mods need them (WHAT YOU
  NEED, below): one line in game.toml, and R1 as the sixth costume in the game's
  own source.

WHAT IS IN IT (only what is finished)
-------------------------------------
  6 Costume Slots Mod  one colour per key on the select screen: square red,
                       cross blue, triangle green, circle yellow, Start
                       orange, R1 purple. The plate follows the button that
                       picked the fighter, not the costume he ends up in -
                       triangle and circle put him in the game's costumes 1
                       and 3, yet read green and yellow - and a fighter the
                       game drew itself (the CPU, a guest, a challenge) has
                       no key, so it reads by the costume it shows: 1 red,
                       3 blue, 5 orange. R1 is the sixth slot, the model only
                       Xiaoyu has (P8): on everyone else it changes nothing,
                       and the fighter keeps the model his key gave him.
                       Names, models and portraits are left alone: Eddy's
                       third costume is the game's own TIGER, the rest stay
                       EDDY.
  Jarkins Lab          Input Display (the last few inputs, in Practice) and
                       Damage Readouts (combo counter, damage, exact health).

NOT IN THIS ALPHA
-----------------
  Unfinished work is kept out of the download, so nothing half-wired shows up
  in the Mods page: the training tools (dummy, combo trials, frame data),
  replays, the extra toggle mods and the modes packages all come later. What
  is in the download is the whole add-on: the two mods above, the C sources
  they run, and the window that builds them into the game.

WHAT YOU NEED
-------------
  - Tekken 3 Expanded 1.5.0 or newer (the add-on hook is API 1) - download it
    from  https://github.com/omarma/tekken3-expanded/releases/latest  - already
    set up once: its own "Setup Tekken 3.cmd" run to the end, so .setup\ is
    there and the game has started at least once.
  - TMST is optional and untouched: install it before or after this, in its own
    addons\tmst folder. Nothing here changes TMST, and no mod in this alpha
    needs it.
  - One line in game.toml, which "Setup Jarkins.cmd" adds for you the first
    time:  mod_function_entry_funcs = ["0x8003D6B0"]  under  [recompiler] .
    0x8003D6B0 is the game's HUD routine; the damage readouts and the plate
    colours draw at its entry, and neither can without it. Adding it makes the
    game generate again once (a minute or two). An Expanded that already has
    the line - or already had it added - is left exactly as it is.
  - R1, the sixth costume, which "Setup Jarkins.cmd" adds to the game's own
    source the first time (src\tekken3_native_moves.c). It is the one change to
    the game itself, and it is only made to a version it knows: everything else
    in that file - the release's own fixes included - is left alone, and a copy
    it does not recognise is not touched at all, with a line in the window
    saying so. Only Xiaoyu has the model the sixth slot draws on; on everyone
    else R1 changes nothing, as before.

UPDATING / UNINSTALLING
-----------------------
  Update: delete  addons\jarkins , drag the new  jarkins  folder in, Update &
  play again. The Mods page keeps your switches (mods\state.toml).
  Uninstall: close the game, delete  addons\jarkins , click Update & play. The
  game is back to what it was; your other add-ons' switches stay. The one line
  in game.toml and the six-key change in the game's source stay too and do
  nothing without the add-on: delete the line for a byte-identical game.toml.

IF SOMETHING GOES WRONG
-----------------------
  - "This folder is not inside Tekken 3 Expanded yet": the jarkins folder is
    not at <Expanded>\addons\jarkins - drag it into the Expanded's own addons
    folder (beside tmst, not inside it) and run the file again.
  - "This copy is inside another add-on's folder": it was dropped into
    addons\tmst (or another add-on) by mistake. Move the jarkins folder up one
    level, to <Expanded>\addons\jarkins.
  - The add-on is left out of a build when the Expanded's setup cannot set it up
    or build it: the game still runs, and .setup\setup.log in the Expanded folder
    says why. Attach that log to a report.
  - Jarkins missing from the Mods page: the build did not include the add-on. In
    the Expanded's setup, clear "Add-ons skipped", then Update & play.

WHAT IS IN THE FOLDER
---------------------
  README.txt            this file.
  Setup Jarkins.cmd     the file you open after dragging the folder in.
  setup\                the installer window it opens (jarkins_install.py):
                        Install & play builds the game with the add-on in.
  addon.cmake           read by the Expanded's build: links the C sources into
                        the game and stages the packages beside it.
  src\                  the C sources (overlay and plate colours, input display,
                        damage).
  mods\                 the package manifests the launcher's Mods page lists
                        (jarkins.lab and jarkins.plates - the finished ones).

Tekken 3 and all its characters are trademarks of Bandai Namco Entertainment
Inc. This is a fan-made, non-commercial add-on, not affiliated with or endorsed
by them. No game data is included or distributed by it.
