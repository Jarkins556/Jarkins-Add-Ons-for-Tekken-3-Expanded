# Releasing Jarkins

How a release of this add-on is cut, numbered and published. The rule underneath all of
it: **nothing is published until Jarkins approves that update.** A release is a copy of
something already agreed, not a way of finding out whether it works.

---

## 1. The three stages

| | **Alpha** (now) | **Beta** | **Release** |
| --- | --- | --- | --- |
| Version | `0.2.x` – `0.8.x` | `0.9.x` | `1.0.0`, then `1.0.x` |
| Feature set | Incomplete, openly: only finished mods ship, the rest stay out of the download | Complete — everything on the roadmap is in | Same, frozen |
| What changes | Anything: mods added, cut, reworked; `mods\state.toml` keys may move | Fixes only; no new mods, no renames | Fixes only |
| Breakage | Expected; a broken fresh install may have to wait for the next build | Not acceptable | Not acceptable, and no regressions |
| GitHub | Release ticked **prerelease**, title `Jarkins alpha x.y.z` | Prerelease, title `Jarkins beta x.y.z` | Not prerelease — this is what `latest` points at |

**Alpha → beta** when every roadmap mod is in the download and working (training tools,
dummy settings, combo trials, frame data, replays, the remaining toggle mods and modes
packages), a fresh install builds clean on each platform claimed, the setup window's two
Expanded-side steps need no hand-editing, no known crash exists, and the README's
"NOT IN THIS ALPHA" section is gone.

**Beta → release** when two consecutive betas land with no install-blocking report, upgrade
(delete `addons\jarkins`, drop the new folder in, Update & play) and uninstall are both
verified on a real install, the docs match the Mods page exactly, and the formats this
add-on owns — `mods\state.toml`, the package manifests — are declared frozen.

---

## 3. Versioning

Two kinds of change, two kinds of bump:

| Change | Bump | Example |
| --- | --- | --- |
| New mod(s) added — Jarkins says the batch is ready | **minor** | `0.2.0` → `0.3.0` (skins) → `0.4.0` |
| Skin mod content updates between those | **patch** | `0.3.0` → `0.3.1` |
| Bug fixes to existing content | **patch** | `0.3.1` → `0.3.2` |

Several mods landing together are **one** minor bump, not one each. The first skins
release is `0.3.0` (a new mod); only skin *content* refreshes after it are `0.3.x`.

Every one of those — minor or patch — gets its own tag and its own release. A patch is
not a smaller process: it runs the same seven steps below.

---

## 2. One version, four places

They must agree, or the download name, the Mods page and the tag disagree with each other:

1. `addon.cmake` → `set(JARKINS_VERSION "x.y.z")` (what the game reports at build time),
2. the dist folder `dist\Jarkins-Alpha-x.y.z\`,
3. the zip `dist\Jarkins-Alpha-x.y.z.zip`,
4. the git tag `vx.y.z` and the release title `Jarkins alpha x.y.z`.

`sync_release.py` has the version in its paths too (`DIST`, `ZIP`); bump it there as well.

---

## 4. What a release contains

- Exactly the files listed in `addons/jarkins/tools/sync_release.py`'s `FILES` — no more.
  Anything not in that list does not ship, whatever the dev tree contains.
- `dist\...\jarkins\` and the repo's `jarkins\` must be byte-identical (`diff -rq`).
- No unfinished work: the boot banner, the jukebox extras, the staged `data` folder and —
  until it is approved — `src/jarkins_skins.c`/skin packages stay out. `sync_release.py`
  drops the first three; the skins source is **still in `FILES`**, so a plain sync brings
  it back. Check for it every time.
- No game data, no disc-derived assets, no build output, no secrets, no tools scripts.
- `README.txt` and `README.md` must make the same claims about behaviour.

---

## 5. The procedure

1. **Sync.** `<Expanded>\.setup\venv\Scripts\python.exe addons\jarkins\tools\sync_release.py`
   — writes `dist`, the zip and both mirrors (`Downloads\jarkins`, the Tekken3Hub folder).
2. **Bump** per §3, in the four places of §2.
3. **Verify** — entries count, fingerprint, no stray sources, mirrors identical, repo copy
   identical:
   - `zipfile` count matches `len(FILES)`;
   - the `# jarkins-release-fingerprint:` line is the same in dist, both mirrors and the repo;
   - `grep -c jarkins_skins dist\...\jarkins\src\*` is `0` unless skins is this release;
   - `README.txt` still CRLF (no bare LF) — editors love to rewrite it.
4. **Commit and push** `main` (the repo holds `README.md`, `.gitignore`, `jarkins\`).
5. **Create the release**: tag `vx.y.z`, title `Jarkins alpha x.y.z`, prerelease ticked
   while in alpha/beta, notes listing what changed **since the previous tag** — not a
   restatement of the whole add-on.
6. **Attach** `dist\Jarkins-Alpha-x.y.z.zip`.
7. **Download it back and check it**: entry count, fingerprint, README inside, no skins
   unless intended. Only then is the release "done".

---

## 6. Invariants

- **Release assets are immutable once approved.** A wording fix does not get uploaded over
  a live asset — it rides in the next version. Rewriting history on a download link is
  worse than a slightly stale sentence.
- **A broken release is fixed forward**, in the next version, with notes saying so.
- **Only approved work ships.** The dev tree is allowed to be ahead of the release line;
  copying dev over release is how unfinished code escapes.
- **Two Expanded-side changes exist** and are made only by the add-on's own setup window,
  once each: the HUD hook line in `game.toml` and R1 as the sixth costume in the game's
  source. Any release that changes what they do must say so in its notes.
- **No `sudo`, no credentials, no platform tokens** in the repo or in a zip.

---

## 7. Traps worth remembering

- Adding the `game.toml` line makes the Expanded regenerate once (`game_current()`); a
  build never does it again on its own.
- The fingerprint hashes `src/` and `mods/`. A `.c` **or manifest wording** change forces
  every install to rebuild once; a `README.txt`, `Setup Jarkins.cmd` or setup-window text
  change must not touch it.
- `sync_release.py` re-adds `src/jarkins_skins.c` on every run while it is in `FILES`.
- Windows' own extractor and editors that "fix" line endings both break the shipped
  `README.txt` and `.cmd` (CRLF), and a partial extraction breaks the setup window itself.
