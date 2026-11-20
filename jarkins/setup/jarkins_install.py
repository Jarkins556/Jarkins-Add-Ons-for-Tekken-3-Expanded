"""The Jarkins add-on's installer: its window, the build and the game.

Setup Jarkins.cmd opens this (setup/jarkins_install.py) with the Expanded's own
Python, the way Setup TMST.cmd opens setup/tmst_gui.py. There is nothing to make
from the player's discs: addon.cmake has the Expanded's build link the add-on's
C sources into the game and stage its packages beside it. So installing it is
building the game with it in place - the Expanded's own update (Update & play,
launcher/setup_backend.py --update), with the add-on already in addons/jarkins -
run here in a window of its own, with its progress, then the game.

That update prepares every add-on before it builds; here it runs with the other
add-ons' own setup steps switched off (UPDATE_RUNNER). This window builds
Jarkins: it does not set TMST - or anyone else - up again.

Before the build it makes sure of the one thing the mods need from the game
itself: the HUD routine's entry hook (ensure_hud_hook). The plate colours and
the damage readouts both register on 0x8003D6B0, and the runtime only calls it
for the addresses game.toml's [recompiler] mod_function_entry_funcs names. An
Expanded set up before this add-on existed has no such line and never remakes
its generated game on a build, so the line is added once and the game generated
again. Both steps do nothing when they are already done.

    pythonw setup/jarkins_install.py

What the build prints goes to addons/jarkins/workspace/setup.log; the Expanded's
.setup/setup.log keeps the same run, step by step.
"""
from __future__ import annotations
import ctypes, hashlib, json, os, queue, re, signal, subprocess, sys, threading
from pathlib import Path

ADDON = Path(__file__).resolve().parents[1]
ROOT = ADDON.parents[1]                     # <Tekken 3 Expanded>/addons/jarkins/setup
LOG = ADDON / 'workspace' / 'setup.log'
WINDOWS = os.name == 'nt'
MAC = sys.platform == 'darwin'
PLAY = 'Tekken 3 Expanded.exe' if WINDOWS else 'Tekken 3 Expanded.app' if MAC else 'Tekken 3 Expanded.sh'
EXE = ROOT / 'build-release' / ('Tekken_3_Expanded.exe' if WINDOWS else 'Tekken_3_Expanded')
FONT = 'Segoe UI' if WINDOWS else 'Helvetica Neue' if MAC else 'DejaVu Sans'
INSTALLS = ('6 Costume Slots Mod (the name plate coloured by the costume the fighter came in)  ·  '
            'Jarkins Lab (Input Display, Damage Readouts)')
# The Expanded's update prepares every add-on in addons/ before it builds
# (launcher/setup_backend.py, prepare_addons): each one's own TEKKEN3_ADDON_SETUP
# step runs, and another add-on's can take minutes - TMST's re-reads its Tekken 2
# music from the disc. This window was opened to build Jarkins, not to set anyone
# else up, so the update runs with every other add-on's setup step switched off:
# they are still built, sources and packages, and still recorded in ready.json;
# only their prepare steps are skipped. An add-on is set up by its own window
# (Setup TMST.cmd) or by the Expanded's Update & play.
UPDATE_RUNNER = r'''
import sys
from pathlib import Path
root, mine = Path(sys.argv[1]), sys.argv[2]
sys.path.insert(0, str(root / 'launcher'))
import setup_backend as backend
step = backend.addon_setup
backend.addon_setup = lambda folder: step(folder) if folder.name == mine else None
backend.TEXT = True                     # --text: the plain lines this window reads
sys.path.insert(0, str(root / 'addons' / mine / 'setup'))
try:
    import jarkins_install as page
    page.ensure_hud_hook(root, backend)
    print(page.ensure_r1_bind(root), flush=True)
    backend.update_install()
except Exception as error:
    import traceback
    backend.log_line(traceback.format_exc())
    backend.emit('error', message=str(error) if isinstance(error, backend.SetupError)
                 else 'Setup stopped unexpectedly. See ' + str(backend.STATE / 'setup.log') + '.')
    sys.exit(1)
'''


HUD_HOOK = '0x8003D6B0'                     # the game's HUD routine: the health bars and plates


def hud_hook_in_toml(path: Path) -> bool:
    """game.toml's [recompiler] mod_function_entry_funcs lists the HUD routine."""
    try:
        text = path.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return False
    for line in text.splitlines():
        head = line.split('#', 1)[0]
        if head.lstrip().startswith('mod_function_entry_funcs') and HUD_HOOK.lower() in head.lower():
            return True
    return False


def write_hud_hook(path: Path) -> bool:
    """Put the HUD routine in [recompiler] mod_function_entry_funcs, adding the key
    or the list entry as needed. True only when the file had to change: the line,
    the comments and the line endings around it are left as they are."""
    try:
        text = path.read_text(encoding='utf-8')
    except OSError:
        return False
    if hud_hook_in_toml(path):
        return False
    eol = '\r\n' if '\r\n' in text else '\n'
    lines = text.split(eol)
    start = next((i for i, line in enumerate(lines) if line.strip() == '[recompiler]'), None)
    if start is None:
        return False
    end = next((i for i in range(start + 1, len(lines)) if lines[i].lstrip().startswith('[')), len(lines))
    key = next((i for i in range(start + 1, end)
                if lines[i].split('#', 1)[0].lstrip().startswith('mod_function_entry_funcs')), None)
    if key is None:
        lines[end:end] = ['# The add-on\'s readouts and plate colours need the HUD routine\'s entry:',
                          '# it is how they know the game is drawing a fight\'s HUD.',
                          f'mod_function_entry_funcs = ["{HUD_HOOK}"]']
    else:
        head, sep, comment = lines[key].partition('#')
        if ']' not in head:
            return False
        lines[key] = head.replace(']', f', "{HUD_HOOK}"]', 1) + (sep + comment if sep else '')
    path.write_text(eol.join(lines), encoding='utf-8')
    return True


def hud_hook_present(root: Path) -> bool:
    """The generated game calls the HUD routine's entry hook. A build alone never
    remakes the generated game (the Expanded caches it on its release), so the
    generated C is the only honest answer to whether the hook is in the game."""
    for path in (root / 'generated').glob('*.c'):
        try:
            if f'psx_mod_function_entry(cpu, {HUD_HOOK}u)' in path.read_text(encoding='utf-8', errors='replace'):
                return True
        except OSError:
            pass
    return False


def ensure_hud_hook(root: Path, backend) -> None:
    """The plate colours and the damage readouts both register a function-entry
    plugin on the game's HUD routine, and the runtime only calls it where
    game.toml's [recompiler] mod_function_entry_funcs names the address. An
    Expanded set up before this add-on existed has no such line, and its game is
    never generated again by a build, so the line is added once and the game
    generated again - the way the Expanded's own setup does it. Nothing happens
    when the line and the hook are already there."""
    toml = root / 'game.toml'
    if not toml.is_file():
        return
    print('Checking the HUD hook  The readouts and the plate colours need the game to call it.', flush=True)
    try:
        if write_hud_hook(toml):
            print('Added the HUD hook to game.toml  The game is generated again with it.', flush=True)
    except OSError as error:
        raise RuntimeError(f'game.toml could not be written ({error})') from error
    if hud_hook_present(root):
        return
    saved = backend.load_json(backend.STATE / 'last-inputs.json')
    disc = saved.get('disc')
    if not disc or not Path(disc).is_file():
        raise RuntimeError('the HUD hook is not in this game and its disc image is not recorded: '
                           'run the Expanded\'s "Setup Tekken 3.cmd" once, then open this again')
    try:
        disc = backend.single_bin(backend.cue_for(Path(disc)))
    except Exception as error:   # noqa: BLE001 - its message is the useful one
        raise RuntimeError(f'the recorded disc image could not be read ({error})') from error
    env = dict(os.environ, PYTHONUTF8='1', PYTHONNOUSERSITE='1')
    toolchain = backend.tools()
    if toolchain:
        env['PATH'] = str(toolchain / 'bin') + os.pathsep + env.get('PATH', '')
        env['PSXRECOMP_TOOLCHAIN_DIR'] = str(toolchain)
    print('Generating the game with the hook  This takes a minute or two.', flush=True)
    code = subprocess.run([sys.executable, str(root / 'psxrecomp/psxrecomp_cli.py'), 'generate',
                           '--project-root', str(root), '--config', str(toml),
                           '--disc', str(disc), '--no-toolchain-download'],
                          cwd=str(root), env=env).returncode
    if code or not hud_hook_present(root):
        raise RuntimeError('the game could not be generated with the HUD hook: see the setup log')
    print('The game calls the HUD hook now  The readouts and plate colours can work.', flush=True)


# --- the sixth costume slot: R1 -----------------------------------------------
#
# Of the three mods this one is the only one whose work is partly the game's own:
# which button picked a fighter, and the model he then comes out in, are recorded
# in the Expanded's src/tekken3_native_moves.c, and the plates read what it keeps
# (tekken3_native_pick_key). A copy of the Expanded made before that file knew R1
# has five keys and not six: there R1 still turns the third costume, so the sixth
# slot - Xiaoyu's P8, the model the random draw shows (variant 7) - cannot be
# reached and the plates' sixth colour never appears.
#
# So the window brings that one file to six keys: textually, by the edits this
# pack documents in COSTUME-BINDS.md, and nothing else in it - the release's own
# fixes stay as they are, B190 included. A file that already has them, or one
# this was not written against, is left untouched.
CORE_SOURCE = 'src/tekken3_native_moves.c'
R1_PATCHED = ('pick==PAD_R1?5:4', 'if(k==5 &&')
R1_STOCK = ('enum { THIRD_COSTUME_KEYS=PAD_R1 };', 'pick==PAD_CIRCLE?3:4)')
R1_EDITS = (
    (r'''static signed char pick_key[2][NATIVES];
/* F32 E6: cpu_variant[slot][fighter]''',
     r'''static signed char pick_key[2][NATIVES];
/* Whether a key has picked that fighter at all: default 0 is square's key, so a
 * flag is needed to tell "square picked him" from "nobody has yet". */
static unsigned char key_set[2][NATIVES];
/* F32 E6: cpu_variant[slot][fighter]'''),
    (r'''    uint16_t picks=edges&PICK;
    if(team_grid_open() || !(buttons&PAD_SELECT))picks&=(uint16_t)~PAD_START;
    if(!picks || ((uint16_t)~buttons&PAD_SQUARE && (uint16_t)~buttons&PAD_TRIANGLE))return;''',
     r'''    /* The six bindings are one per costume: square 1, triangle 2, cross 3,
     * circle 4, Start 5, R1 6. Select is not a binding any more - it held the
     * sixth slot and collided with R1. */
    uint16_t picks=(uint16_t)(edges&(PICK|PAD_R1));
    if(edges&PAD_START)picks&=(uint16_t)~PAD_R1;
    if(team_grid_open())picks&=(uint16_t)~PAD_START;
    if(!picks || ((uint16_t)~buttons&PAD_SQUARE && (uint16_t)~buttons&PAD_TRIANGLE))return;'''),
    (r'''    uint16_t pick=picks&-picks;
    pick_key[slot][under]=(signed char)(pick==PAD_SQUARE?0:pick==PAD_CROSS?1:pick==PAD_TRIANGLE?2:pick==PAD_CIRCLE?3:4);
}''',
     r'''    uint16_t pick=picks&-picks;
    /* R1 is the one game costume no other key reaches: Xiaoyu's sixth, the model
     * the random draw shows (P8, variant 7). 5 says "that one", and the drawing
     * code turns it into variant 7 for a fighter that has it (see
     * tekken3_native_model_variant). */
    /* A Start on a fighter a key already picked is the confirm, not a second pick:
     * R1 picks the sixth costume and the game still wants Start to begin, and that
     * confirm must not repaint the plate as Start's black. */
    if(pick==PAD_START && key_set[slot][under])return;
    pick_key[slot][under]=(signed char)(pick==PAD_SQUARE?0:pick==PAD_CROSS?1:pick==PAD_TRIANGLE?2:pick==PAD_CIRCLE?3:pick==PAD_R1?5:4);
    key_set[slot][under]=1;
}'''),
    (r'''enum { THIRD_COSTUME_KEYS=PAD_R1 };
extern int tekken3_ttt1_native_rp_model(unsigned id);''',
     r'''/* R1 used to turn the third costume, the same one Start reaches - that is the
 * share that made Start and R1 one binding. It now takes the sixth costume
 * instead (variant 7, Xiaoyu's secret model), and Start alone takes the fifth:
 * its press is the card's pick (record_key, key 5). */
extern int tekken3_ttt1_native_rp_model(unsigned id);'''),
    (r'''    uint16_t press=edges&(PAD_TRIANGLE|THIRD_COSTUME_KEYS)&(uint16_t)~taken[p];''',
     r'''    uint16_t press=edges&PAD_TRIANGLE&(uint16_t)~taken[p];'''),
    (r'''            if(pressed&PAD_SQUARE)press&=(uint16_t)~PAD_TRIANGLE;
            if(press&THIRD_COSTUME_KEYS) {
                pick_key[slot][under]=4;
                fprintf(stderr,"Native models: P%u R1, the third costume\n",slot+1);
            } else if(press&PAD_TRIANGLE)
                fprintf(stderr,"Native models: P%u triangle, the RP model\n",slot+1);
            taken[p]|=press;''',
     r'''            if(pressed&PAD_SQUARE)press&=(uint16_t)~PAD_TRIANGLE;
            if(press&PAD_TRIANGLE)
                fprintf(stderr,"Native models: P%u triangle, the RP model\n",slot+1);
            if(press)taken[p]|=press;'''),
    (r'''    if(taken[p]&PAD_TRIANGLE)out&=(uint16_t)~PAD_SQUARE;
    if(taken[p]&THIRD_COSTUME_KEYS)out&=(uint16_t)~PAD_TRIANGLE;
    return out;''',
     r'''    if(taken[p]&PAD_TRIANGLE)out&=(uint16_t)~PAD_SQUARE;
    return out;'''),
    (r'''static int challenged_variant(unsigned slot,unsigned id);
int tekken3_native_model_variant(unsigned slot,unsigned id,unsigned costume) {
    int k=slot<2 && id<NATIVES?pick_key[slot][id]:-1;
    if(k<0) {                                    /* F32 E6: the CPU's TTT draw */''',
     r'''static int challenged_variant(unsigned slot,unsigned id);
extern int tekken3_ttt1_native_variant_file(unsigned id,unsigned v);
/* The button that picked this native (0 square, 1 cross, 2 triangle, 3 circle, 4 Start, 5 R1),
 * -1 when the game drew it instead (CPU, a guest, a challenge). The name-plate families read
 * this: the plate's colour follows the binding that chose the fighter, not the costume, and
 * triangle and circle share the square's and the cross's costumes. */
int tekken3_native_pick_key(unsigned slot,unsigned id) {
    return slot<2 && id<NATIVES?pick_key[slot][id]:-1;
}
int tekken3_native_model_variant(unsigned slot,unsigned id,unsigned costume) {
    int k=slot<2 && id<NATIVES?pick_key[slot][id]:-1;
    /* Triangle is P3 of its own, not the RP model of whatever costume word the
     * game happens to hold. Eddy is why: his costume 2 is Tiger, so reading
     * triangle through the costume word handed him P5 - the model Start already
     * has - and the same fighter came out twice. P3 is triangle's model at
     * costume 0, which is the table this pack documents, so it is P3 whatever
     * the costume word says. */
    if(k==2)return 2;
    /* R1 is the sixth costume: variant 7, the model the random draw shows -
     * Xiaoyu's P8 has one, and she is the only fighter with a file for it. On
     * everyone else R1 still picks nothing, so the fighter keeps the model the
     * key he already had gives him. */
    if(k==5 && id<NATIVES && tekken3_ttt1_native_variant_file(id,7))return 7;
    if(k<0) {                                    /* F32 E6: the CPU's TTT draw */'''),
    (r'''    uint16_t picks=edges&PICK;
    unsigned under=cursor_character(slot);
    if(team_grid_open() || (under<NATIVES && !(psx_mod_read_word(0x80097ef4)>>(under&31)&1)))''',
     r'''    uint16_t picks=edges&PICK;
    unsigned under=cursor_character(slot);
    /* R1 is the sixth costume, and the game has no sixth costume button: a
     * plain R1 press is one the selector ignores, which is why R1 "did
     * nothing" at all. So the press is turned into the square's - costume 0,
     * the model the fighter would wear anyway - and the sixth model comes from
     * the recorded key (5), the way a triangle press is turned into the third
     * costume. The game only needs a pick it knows to accept the confirm. */
    if((edges&PAD_R1) && !confirmed(slot) && under<NATIVES &&
       tekken3_ttt1_native_variant_file(under,7)) {
        injecting[slot]=PAD_SQUARE; inject[slot]=INJECT_POLLS; hold[p]=CARD_KEYS;
        fprintf(stderr,"Native models: P%u R1, the sixth costume's model\n",slot+1);
        return 0xffff;
    }
    if(team_grid_open() || (under<NATIVES && !(psx_mod_read_word(0x80097ef4)>>(under&31)&1)))'''),
)


def r1_state(text: str) -> str:
    """'patched', 'stock' (a file this add-on knows how to bring to six keys) or 'unknown'."""
    if all(marker in text for marker in R1_PATCHED):
        return 'patched'
    if all(marker in text for marker in R1_STOCK):
        return 'stock'
    return 'unknown'


def r1_source(text: str) -> str:
    """A stock file brought to six keys. Every edit must be there exactly once: a file
    this was not written against raises instead of being half-changed."""
    for old, new in R1_EDITS:
        found = text.count(old)
        if found != 1:
            raise ValueError(f'an edit matches {found} times: {old.splitlines()[0].strip()[:60]}')
        text = text.replace(old, new, 1)
    return text


def ensure_r1_bind(root: Path) -> str:
    """The sixth costume slot in <Expanded>/src/tekken3_native_moves.c. Returns the
    line for the window and the log; the file is written only when it is a stock
    file every edit applies to."""
    path = root / CORE_SOURCE
    try:
        text = path.read_text(encoding='utf-8')
    except OSError:
        return 'Sixth costume  the game\'s own source is not in this folder: skipped.'
    state = r1_state(text)
    if state == 'patched':
        return 'Sixth costume  R1 is already in this game\'s source: nothing to do.'
    if state == 'unknown':
        return ('Sixth costume  this Expanded\'s ' + CORE_SOURCE + ' is not one this add-on knows: '
                'left alone, so R1 stays the third costume.')
    try:
        patched = r1_source(text)
    except ValueError as error:
        return f'Sixth costume  not added ({error}): R1 stays the third costume.'
    path.write_text(patched, encoding='utf-8')
    return 'Sixth costume  R1 added to the game\'s own source: the build compiles it in.'


def version() -> str:
    """addon.cmake's JARKINS_VERSION, for the window's title line."""
    try:
        text = (ADDON / 'addon.cmake').read_text(encoding='utf-8', errors='replace')
    except OSError:
        return ''
    m = re.search(r'set\(\s*JARKINS_VERSION\s+"([^"]+)"', text)
    return m[1] if m else ''


def digest(path: Path) -> str:
    """The Expanded's own digest of a file (launcher/setup_backend.py's): what
    .setup/ready.json keeps for each add-on's addon.cmake."""
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def expanded_backend():
    """The Expanded's setup module (internal to it, docs/COMPAT.md): None if it cannot be loaded."""
    try:
        import setup_backend
        return setup_backend
    except Exception:   # noqa: BLE001 - its absence is the problem() case
        return None


def problem() -> str | None:
    """Why the add-on cannot be installed yet, or None."""
    if not (ADDON / 'addon.cmake').is_file():
        return ('This jarkins folder has lost its addon.cmake. Extract the download again, '
                'drag the folder into the Expanded again, then open this once more.')
    if not (ROOT / 'game.toml').is_file() or not (ROOT / 'launcher/setup_backend.py').is_file():
        place = os.path.join('<Tekken 3 Expanded>', 'addons', 'jarkins')
        return (f'This folder is not inside Tekken 3 Expanded. It belongs at {place} - a folder '
                'of its own, not inside another add-on\'s folder and not in mods\\. Drag the '
                'whole jarkins folder into the Expanded\'s own addons folder and open this again.')
    if not (ROOT / '.setup/ready.json').is_file() or not (ROOT / 'build-release/CMakeCache.txt').is_file():
        return (f'Tekken 3 Expanded has not been set up yet. Run "Setup Tekken 3.cmd" in {ROOT} '
                'first and let it finish (the game starts), close the game, then open this again.')
    return None


def build_state() -> tuple[str, str]:
    """What the window's button does, and the line under the status:
    ('play', why nothing needs building) when the game already has this copy of
    the add-on, else ('build', why it is built again)."""
    try:
        state = json.loads((ROOT / '.setup/ready.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return 'build', 'The Expanded\'s setup state cannot be read: the game is built again.'
    left_out = state.get('addons_left_out') or []
    if ADDON.name in left_out:
        return 'build', ('The last build left this add-on out - .setup\\setup.log in the Expanded '
                         'folder says why. Install builds again.')
    listed = {}
    for entry in state.get('addons') or []:
        if isinstance(entry, list) and len(entry) == 2:
            listed[entry[0]] = entry[1]
    if listed.get(ADDON.name) != digest(ADDON / 'addon.cmake') or not EXE.is_file():
        return 'build', 'This copy of the add-on is not in the game yet: Install & play builds it in.'
    return 'play', 'The game already has this copy of the add-on: nothing needs building.'


def game_running() -> bool:
    """build-release's game is running: a running game cannot be built again."""
    try:
        if WINDOWS:
            # tasklist prints in the console's (OEM) code page, in the system's language.
            out = subprocess.run(['tasklist', '/fi', 'imagename eq ' + EXE.name, '/fo', 'csv', '/nh'],
                                 capture_output=True, text=True, encoding='oem', errors='replace',
                                 creationflags=subprocess.CREATE_NO_WINDOW).stdout or ''
            return EXE.name.lower() in out.lower()
        target = EXE.resolve()
        out = subprocess.run(['ps', '-A', '-o', 'comm='], capture_output=True, text=True,
                             errors='replace').stdout or ''
        for line in out.splitlines():
            line = line.strip()
            if line and (line == str(target) or (line.endswith('/' + EXE.name) and Path(line).resolve() == target)):
                return True
    except OSError:
        pass
    return False


def open_file(path: Path) -> None:
    if WINDOWS:
        os.startfile(str(path))
    else:
        subprocess.Popen(['open' if MAC else 'xdg-open', str(path)])


def popen_options() -> dict:
    """The build's process: no console window on Windows; elsewhere a session of
    its own, so that stop() reaches the compilers it starts."""
    if WINDOWS:
        return {'creationflags': subprocess.CREATE_NO_WINDOW}
    return {'start_new_session': True}


def stop(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    if not WINDOWS:
        try:
            os.killpg(process.pid, signal.SIGTERM)
            return
        except OSError:
            pass
    process.terminate()


class Job:
    """Windows owns the build's worker tree, so Cancel also stops the compilers:
    the Expanded's own ProcessJob when it has one."""
    def __init__(self):
        self.job = None
        if not WINDOWS:
            return
        try:
            from easy_launcher import ProcessJob
            self.job = ProcessJob()
        except Exception:   # noqa: BLE001 - without it Cancel stops the build's own process only
            self.job = None

    def assign(self, process) -> None:
        if self.job:
            try:
                self.job.assign(process)
            except OSError:
                pass

    def close(self) -> None:
        if self.job:
            self.job.close()
            self.job = None


def gui(test_hook=None) -> None:
    import tkinter as tk
    from tkinter import ttk

    class App:
        def __init__(self):
            self.window = tk.Tk()
            self.window.title('Tekken 3 Expanded — Jarkins add-on')
            self.window.configure(bg='#11131b')
            self.window.resizable(False, False)
            self.events = queue.Queue()
            self.process = None
            self.job = None
            self.busy = False
            self.epoch = 0
            self.error = ''
            s = ttk.Style(self.window)
            s.theme_use('clam')
            s.configure('.', font=(FONT, 10), background='#11131b', foreground='#ececf2')
            s.configure('TFrame', background='#11131b')
            s.configure('TLabel', background='#11131b', foreground='#ececf2')
            s.configure('Muted.TLabel', foreground='#a9adbd')
            s.configure('Title.TLabel', font=(FONT, 25, 'bold'))
            s.configure('TButton', background='#2d3242', foreground='#f4f4f7', padding=(14, 8), borderwidth=0)
            s.map('TButton', background=[('active', '#404758'), ('disabled', '#20232d')])
            s.configure('Play.TButton', background='#744bd7', font=(FONT, 12, 'bold'), padding=(20, 12))
            s.map('Play.TButton', background=[('active', '#8862e8'), ('disabled', '#393247')])
            s.configure('TProgressbar', background='#9775ea', troughcolor='#232735', borderwidth=0)
            root = ttk.Frame(self.window, padding=28)
            root.pack(fill='both', expand=True)
            ttk.Label(root, text='Jarkins for Tekken 3 Expanded', style='Title.TLabel').pack(anchor='w')
            ttk.Label(root, text='ALPHA ' + version() + ' · built into the game by its own build',
                      style='Muted.TLabel').pack(anchor='w', pady=(4, 22))
            ttk.Label(root, text='Installs: ' + INSTALLS, wraplength=590, justify='left').pack(anchor='w', pady=(0, 14))
            ttk.Label(root, text='Uses the Expanded’s own Python and compilers; the first build takes a few minutes.\n'
                      'Your mod settings and game data stay. Nothing is uploaded.\n'
                      'Switch the mods on in the game’s Mods tab, Jarkins group.',
                      style='Muted.TLabel', justify='left').pack(anchor='w', pady=(0, 20))
            self.problem = problem()
            what, why = ('build', '') if self.problem else build_state()
            self.action = what
            self.status = tk.StringVar(value='Tekken 3 Expanded is not ready' if self.problem
                                       else 'Ready to play' if what == 'play' else 'Ready to install')
            self.detail = tk.StringVar(value=self.problem or why or f'Tekken 3 Expanded: {ROOT}')
            ttk.Label(root, textvariable=self.status, font=(FONT, 11, 'bold'), wraplength=585).pack(anchor='w')
            ttk.Label(root, textvariable=self.detail, style='Muted.TLabel', wraplength=585,
                      justify='left').pack(anchor='w', pady=(5, 10))
            self.progress = ttk.Progressbar(root, mode='determinate', maximum=1000, length=590)
            self.progress.pack(fill='x', pady=(0, 16))
            if what == 'play' and not self.problem:
                self.progress.configure(value=1000)
            actions = ttk.Frame(root)
            actions.pack(fill='x')
            text = 'Play' if what == 'play' and not self.problem else 'Install & play'
            self.primary = ttk.Button(actions, text=text, style='Play.TButton',
                                      command=self.play if text == 'Play' else self.start)
            self.primary.pack(side='right')
            self.cancel_button = ttk.Button(actions, text='Cancel', command=self.cancel, state='disabled')
            self.cancel_button.pack(side='right', padx=(0, 9))
            ttk.Button(actions, text='Open setup log', command=self.open_log).pack(side='left')
            if self.problem:
                self.primary.configure(state='disabled')
            self.window.protocol('WM_DELETE_WINDOW', self.close)
            self.window.after(100, self.poll)
            self.window.update_idletasks()
            width, height = self.window.winfo_reqwidth(), self.window.winfo_reqheight()
            x = max(0, (self.window.winfo_screenwidth() - width) // 2)
            y = max(0, (self.window.winfo_screenheight() - height) // 2)
            self.window.geometry(f'+{x}+{y}')

        def set_busy(self, value: bool) -> None:
            self.busy = value
            self.primary.configure(state='disabled' if value else 'normal')
            self.cancel_button.configure(state='normal' if value else 'disabled')

        def set_progress(self, fraction: float) -> None:
            value = int(max(0.0, min(1.0, fraction)) * 1000)
            if value > self.progress['value']:      # forward only: the build runs more than once
                self.progress.configure(value=value)

        def start(self) -> None:
            if game_running():
                self.status.set('Close the game first')
                self.detail.set('Tekken 3 Expanded is running: a running game cannot be built again.')
                return
            self.epoch += 1
            self.error = ''
            self.set_busy(True)
            self.progress.configure(value=0)
            self.status.set('Starting the build')
            self.detail.set('The Expanded’s setup rebuilds the game with the add-on in place.')
            threading.Thread(target=self.work, args=(self.epoch,), daemon=True).start()

        def work(self, epoch: int) -> None:
            """The Expanded's own update, in a process of its own; its plain
            progress lines (setup_backend.py --text) go to the log and the window."""
            env = dict(os.environ, PYTHONUTF8='1', PYTHONIOENCODING='utf-8')
            python = Path(sys.executable).with_name('python.exe') if WINDOWS else Path(sys.executable)
            command = [str(python), '-u', '-c', UPDATE_RUNNER, str(ROOT), ADDON.name]
            shown = [str(python), '-u', str(ROOT / 'launcher/setup_backend.py'), '--update', '--text']
            LOG.parent.mkdir(parents=True, exist_ok=True)
            with LOG.open('w', encoding='utf-8') as log:
                log.write('> ' + ' '.join(shown) + '\n')
                try:
                    process = subprocess.Popen(command, cwd=str(ROOT), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                               stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace',
                                               env=env, **popen_options())
                except OSError as error:
                    self.events.put((epoch, ('error', f'Could not start the Expanded’s setup: {error}')))
                    return
                job = Job()
                job.assign(process)
                self.process, self.job = process, job
                try:
                    for line in process.stdout:
                        log.write(line)
                        log.flush()
                        self.events.put((epoch, ('line', line.rstrip())))
                    code = process.wait()
                finally:
                    job.close()
            if code:
                self.events.put((epoch, ('failed', code)))
            else:
                self.events.put((epoch, ('done',)))

        def on_line(self, line: str) -> None:
            if not line or line.startswith('> '):
                return
            m = re.match(r'(.+?):\s*(\d+)\s*/\s*(\d+)\s*$', line)      # the build's step counter
            if m:
                self.status.set('Building the game with the add-on')
                self.set_progress(int(m[2]) / max(1, int(m[3])))
                self.detail.set(f'Step {m[2]} of {m[3]}. Your mod settings and game data stay.')
                return
            if line.startswith('Error: '):
                self.error = line[len('Error: '):]
                return
            head, _, tail = line.partition('  ')
            head, tail = head.strip(), tail.strip()
            if head:
                self.status.set(head)
            if tail:
                self.detail.set(tail)
            if head.startswith('Ready to play'):
                self.set_progress(1.0)

        def poll(self) -> None:
            try:
                while True:
                    epoch, event = self.events.get_nowait()
                    if epoch != self.epoch:
                        continue
                    kind = event[0]
                    if kind == 'line':
                        self.on_line(event[1])
                    elif kind in ('failed', 'error'):
                        self.process = None
                        self.set_busy(False)
                        self.status.set('The build needs attention')
                        self.detail.set(self.error or
                                        'Open the setup log for details, then click Try again.')
                        self.primary.configure(text='Try again', command=self.start, state='normal')
                    elif kind == 'done':
                        self.process = None
                        self.set_busy(False)
                        self.set_progress(1.0)
                        self.status.set('Jarkins is built into the game')
                        self.detail.set('Play opens the launcher: tick what you want in the Mods tab, '
                                        'under Jarkins, then start the game.')
                        self.primary.configure(text='Play', command=self.play)
            except queue.Empty:
                pass
            self.window.after(100, self.poll)

        def play(self) -> None:
            try:
                backend = expanded_backend()
                if backend is not None:
                    backend.launch_game(settings=True)      # its launcher: the Mods tab is there
                else:
                    subprocess.Popen([str(ROOT / PLAY)], cwd=str(ROOT))
                self.window.destroy()
            except Exception as error:   # noqa: BLE001 - pythonw has no console: say it in the window
                self.status.set('Could not launch the game')
                self.detail.set(f'{error}. Start {PLAY} yourself.')

        def cancel(self) -> None:
            self.epoch += 1
            if self.job:
                self.job.close()
                self.job = None
            if self.process:
                stop(self.process)
            self.process = None
            self.set_busy(False)
            self.status.set('Build cancelled')
            self.detail.set('What is done is kept. Open this again whenever you are ready.')
            self.primary.configure(text='Try again', command=self.start, state='normal')

        def close(self) -> None:
            if self.busy:
                self.cancel()
            self.window.destroy()

        def open_log(self) -> None:
            if LOG.is_file():
                open_file(LOG)
            else:
                self.detail.set('The log appears when the build starts.')

    app = App()
    if test_hook:
        test_hook(app)
    app.window.mainloop()


def main() -> None:
    if WINDOWS:
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except (AttributeError, OSError):
            pass
    try:
        gui()
    except Exception as error:   # noqa: BLE001 - pythonw has no console: show it
        import tkinter as tk
        from tkinter import messagebox
        w = tk.Tk()
        w.withdraw()
        messagebox.showerror('Jarkins add-on', str(error))


if __name__ == '__main__':
    main()
