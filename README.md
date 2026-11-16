# pc98hdd — move files between a PC-98 machine and a Mac

*日本語: [README.ja.md](README.ja.md)*

A single shell command that carries files between a real NEC PC-98 architecture
machine and a Mac, by walking the BlueSCSI microSD card between them. Put the card
in the Mac and you can push files onto the 98's hard disk or suck its contents back
out. Data on 5.25" and 3.5" floppies travels too, with the 98 acting as the relay.

Everything is done through one command, `98hdd`.

```
[5.25" / 3.5" FD] ⇄ [PC-98 machine] ⇄ [BlueSCSI] ⇄ [microSD]
                                           │  ← power off, pull the card, put it in the Mac
                                           ▼
                                      [Mac /Volumes/x]
                                      98hdd send / pull
```

It works by driving `mtools` against the FAT16 filesystem inside the BlueSCSI `.hda`
image, and handles the two things that make this annoying by hand: 8.3 filename
mangling and Shift-JIS ⇄ UTF-8 / CRLF ⇄ LF conversion.

Internals are documented in [SPEC.md](SPEC.md) (Japanese). This file is usage only.

---

## 1. What you need

### Hardware

| Item          | Notes                                            |
| ------------- | ------------------------------------------------ |
| A PC-98 machine | see supported machines below                   |
| BlueSCSI v2   | run as SCSI ID 1 / LUN 0 / 512 bytes-per-sector  |
| microSD card  | must mount on the Mac as volume name `x`         |
| Mac           | runs `98hdd`. macOS only — developed on Apple Silicon |

#### Supported machines

The Mac side only ever looks at the FAT16 filesystem inside the disk image, so it does
not depend on the 98 model. These are the ones actually confirmed on real hardware:

| Machine         | Status    |
| --------------- | --------- |
| NEC PC-9801RS   | confirmed |
| NEC PC-9821Ra7  | confirmed |
| EPSON PC-486GR  | confirmed |

Any other PC-98 architecture machine meeting the same conditions (BlueSCSI + Japanese
MS-DOS + FAT16) should work, but is **untested**.

The disk image on the card is `HD10_512 200MB.hda` (200 MB, a single FAT16 partition).
The `HD10_512` part of the name is BlueSCSI's naming convention — **rename it and the
98 stops seeing the disk.**

### Commands on the Mac

`98hdd` checks for all of these at startup and exits without doing anything if even one
is missing.

- `mdir` `mcopy` `mmd` — mtools
- `nkf` — character encoding conversion
- `iconv` `file` — shipped with macOS

Install mtools and nkf if you do not have them (`brew install mtools nkf`).

### About Windows

**This is a macOS tool. Windows is unverified.**

For what it is worth, hardly anything in the script is macOS-specific: the image path is
a default you override in `~/.98hddrc`, and every command it calls is POSIX. Under WSL2
with `mtools` and `nkf` installed it would most likely run, once you point `HDD_IMAGE`
and the `file=` line in `~/.mtoolsrc` at the card (`/mnt/e/...`) and mount it with
`mount -t drvfs`. Two things would trip you up first:

- **Line endings.** Git for Windows defaults to `core.autocrlf=true`, which would check
  the shell scripts out as CRLF and make bash die with `$'\r': command not found`.
  `.gitattributes` pins the whole tree to LF to prevent exactly that.
- **`98hdd` is a symlink.** Without `core.symlinks=true` Git writes a 9-byte text file
  in its place and `./98hdd` will not run. Use `bash 98hdd.sh`.

Even so, on Windows you probably want something else. **DiskExplorer** reads and writes
PC-98 disk images directly with a GUI, and ImDisk or OSFMount will mount the image at
`offset=360448` as an ordinary drive letter.

---

## 2. First-time setup

### 2-1. Create the directories and the config file

`98hdd.sh` has `~/9801` hardcoded as its default working directory, so cloning there is
the path of least resistance.

```sh
git clone https://github.com/JK-ondo/pc98hdd.git ~/9801
cd ~/9801
./98hdd init
```

This creates `send/`, `hdd/` and the config file `~/.98hddrc`. It does nothing if they
already exist.

You can clone anywhere you like — just run `98hdd init` and then point `BASE_DIR` in
`~/.98hddrc` at that directory.

### 2-2. Write `~/.mtoolsrc`

**Nothing works without this.** It is what tells mtools where the FAT16 filesystem
starts inside the image file. Create `~/.mtoolsrc` with:

```
drive p:
    file="/Volumes/x/HD10_512 200MB.hda"
    offset=360448
    fat_bits=16
    cylinders=820
    heads=8
    sectors=17
    filter
```

> **Note:** `~/.mtoolsrc` is the only place where `offset` actually takes effect.
> `~/.98hddrc` has an `HDD_OFFSET` entry too, but it is currently unused. When you
> switch to a different disk image you have to **edit both files**.

### 2-3. Check that it works

With the card inserted in the Mac:

```sh
./98hdd ls
```

If you get a listing of the root of the 98's hard disk, you are set.

---

## 3. Everyday use

### First: the card-swapping ritual

Every Mac-side operation assumes **the card is mounted on the Mac**.

1. **Power off** the 98
2. Pull the microSD out of the BlueSCSI
3. Insert it in the Mac and confirm it mounted at `/Volumes/x`
4. Run the command
5. Eject (unmount) the card in Finder and put it back in the BlueSCSI
6. Power the 98 back on

> ⚠️ **Never touch the card from the Mac while the 98 is running.** The card cannot
> physically be in both machines at once, and that is the only safety interlock there
> is. The tool itself does no locking.

### 3-1. Mac → 98

Put what you want to send in `send/`, then:

```sh
./98hdd send
```

The directory structure under `send/` is reproduced as-is at the root of the hard disk.

You can also name a source and a destination:

```sh
./98hdd send pc98/DOS62 /FD/DOS62     # contents of pc98/DOS62/ → A:\FD\DOS62\
```

A send either **fully succeeds or writes nothing**. The moment a filename collision or
an out-of-space condition is found, it stops without having written a single byte.

### 3-2. 98 → Mac

```sh
./98hdd pull
```

Every file on the hard disk is extracted under `hdd/`. At the end you get counts of
new / updated / unchanged.

- Files whose contents match last time are left alone
- Only files whose contents changed are replaced, and the old copy is first moved aside
  as `<name>.bak_20260907_153515`
- Files that fail to extract are warned about and skipped. **Nothing already in `hdd/`
  gets damaged**

> `pull` only ever adds. Files you deleted on the 98 side stay in `hdd/` forever.

### 3-3. Look inside the hard disk

```sh
./98hdd ls          # root
./98hdd ls /QC      # contents of A:\QC
```

### 3-4. Unpack a floppy image (FDI)

Extracts the files out of an Anex86-format floppy image in `pc98/`. This has nothing to
do with the card or the hard disk, and needs nothing mounted.

```sh
./98hdd fd pc98/DOS62.FDI              # → unpacked into pc98/DOS62/
./98hdd fd pc98/DOS62.FDI /tmp/out     # choose the output directory
```

Images that are not FAT (games with their own disk format, for instance) stop with an
error. **The image file itself is never touched.**

### 3-5. Exchanging data with floppies

Floppies and the Mac have no direct connection. The 98 is the relay.

- **FD → Mac**: on the 98, `COPY B:\*.* A:\SOMEDIR` → card to the Mac → `98hdd pull`
- **Mac → FD**: `98hdd send` to the hard disk → card to the 98 → `COPY A:\... B:\`

---

## 4. Behavior you need to know about

### Filenames get crushed into 8.3 (on send)

On send, each filename is transformed by these rules:

1. Upper-case it
2. **Discard** every character outside `A-Z 0-9 _ -`
3. Truncate to 8 characters of base name and 3 of extension

```
myLongName.c  →  MYLONGNA.C
```

⚠️ **A filename made only of Japanese characters becomes empty, and the send stops with
a collision error.** Rename it to an ASCII name first. (The other direction, `pull`,
does restore Japanese filenames from the 98 into UTF-8.)

### Text files are transcoded automatically

| Direction | Conversion                     |
| --------- | ------------------------------ |
| `send`    | UTF-8 + LF → Shift-JIS + CR+LF |
| `pull`    | Shift-JIS + CR+LF → UTF-8 + LF |

Whether a file counts as text is decided **by its extension alone**:

```
bat txt doc c h asm inc ini def bas err log md
```

Everything else is carried through byte-for-byte.

This is deliberate. Content sniffing with `file` used to decide it, but the verdict was
unstable and it corrupted an executable once, so the extension list replaced it.

**Two consequences follow:**

- Text with an extension not on that list stays Shift-JIS in `hdd/`. The bytes are
  intact, so read it through `nkf -w <file>`
- Files with an extension on that list are **not guaranteed to come back as the same
  bytes** after a send-then-pull round trip. Files off the list always are

### `hdd/` is not a bootable backup

`mdir` does not list files with the hidden attribute, so `IO.SYS` / `MSDOS.SYS` and
friends never get pulled. `hdd/` is **a mirror of the visible files** — rebuild a disk
from it and the 98 will not boot.

---

## 5. Troubleshooting

Messages are in Japanese; they are quoted here as they appear.

| Symptom | Cause and fix |
| ------- | ------------- |
| `コマンドが見つかりません: mdir` (command not found) | mtools is not installed. Same for `nkf` |
| `HDDイメージが見つかりません` (image not found) | The card is not mounted at `/Volumes/x`. Re-seat it, or check the path in `~/.98hddrc` |
| `ls` prints nothing, or mojibake | `~/.mtoolsrc` is missing or its `offset` is wrong. See 2-2 |
| `ファイル名の衝突` (filename collision) | Two files crush to the same 8.3 name. Rename one and re-run — nothing has been sent yet |
| `容量不足` (out of space) | Send less, or delete something on the 98. Nothing has been sent yet |
| `FATとして読めません` (not readable as FAT) | That image uses a game-specific format. Out of scope for `98hdd fd` |
| `コピー失敗: ...（スキップ）` (copy failed, skipped) | Just that one file was skipped. The rest are fine, and existing files in `hdd/` are untouched |

---

## 6. What is in the directory

A fresh clone contains only this:

```
~/9801/
├── 98hdd            → symlink to 98hdd.sh (this is what you run)
├── 98hdd.sh         the tool
├── README.md        this file
├── README.ja.md     Japanese version
├── SPEC.md          detailed specification (Japanese)
├── LICENSE          MIT
├── mise.toml        check tasks (mise run verify)
└── nx93rank/        extra: a DOS program that periodically resets the NyaHaX'93 high score table
    └── docs/        its contract, decision log and test design (Japanese)
```

The rest accumulates as you use the tool and is not in the repository (it is in
`.gitignore`):

```
├── send/            outbox for Mac → 98. Put files here, then 98hdd send
├── hdd/             destination for 98 → Mac. 98hdd pull unpacks here
├── pc98/            where floppy images (FDI) and software live
└── tools/           tools that run on the 98 itself (whole-disk copiers and the like)
```

`98hdd init` creates `send/` and `hdd/`. Create `pc98/` and `tools/` yourself.

There are two config files, `~/.98hddrc` (tool settings) and `~/.mtoolsrc` (how to read
the image). Both live outside the repository.

---

## 7. The 98 side, as the author runs it

The tool does not require any of this. It is just context on what is at the other end.

| Item          | Value                                                             |
| ------------- | ----------------------------------------------------------------- |
| OS            | Japanese MS-DOS Ver 5.0, base system set (NEC)                    |
| Boot drive    | `A:` (the BlueSCSI disk image)                                    |
| After boot    | `AUTOEXEC.BAT` loads MOUSE, then starts DOSSHELL                  |
| `A:\DOS`      | the MS-DOS utilities                                              |
| `A:\QC`       | Microsoft QuickC: `QC.EXE` (IDE) / `QCL.EXE` (command line)       |

There is no assembler (MASM) installed.

---

## 8. What is not included

None of the content this tool carries is in this repository.

- MS-DOS itself and its utilities, and Microsoft QuickC — copyright NEC and Microsoft
- Floppy images and disk contents of commercial and doujin games — copyright their
  respective owners
- Third-party tools that run on the 98 — get them from wherever they are distributed
- The real-hardware mirror that accumulates in `hdd/` — the author's own data

What is published is `98hdd.sh` and its documentation, `nx93rank/`, and the check tasks.
The BlueSCSI disk image is not included either; bring your own.

---

## 9. Contributing

Patches are welcome, including from people whose machine is nothing like the author's.

**Branch from `main`, push the branch, open a pull request against `main`.** Please do not
commit to `main` directly. There is no long-lived `dev` branch; a branch per change, deleted
once merged, keeps `main` the only thing anyone has to trust.

**`main` is meant to stay green**, so a fresh clone can run the checks and see them pass:

```sh
mise run verify      # everything
mise run format      # fixes formatting
```

`nx93rank/` is a DOS program, so two of its checks need more than a clone:

- `mise run build:nx93rank` cross-compiles `RANK.EXE` and **pulls a Docker image**. You only
  need it if you change `RANK.C` — the checked-in `RANK.EXE` must match, and
  `verify:nx93rank-binary` compares their hashes.
- The test suites themselves need only `clang` and `nm`. No network, nothing to install.

**If you change how `RANK.C` behaves, change [`nx93rank/docs/SPEC.md`](nx93rank/docs/SPEC.md)
first.** The tests are derived from that document rather than from the code, which is the only
reason they verify anything; fitting an expected value to new behavior quietly removes the
check. [`nx93rank/docs/DECISIONS.md`](nx93rank/docs/DECISIONS.md) says why each rule has the
shape it does — several of the plainer-looking alternatives were tried and rejected.

**Say what you could not test.** Much of this only fails on real hardware: 16-bit `int`, the
DOS file API, QuickC, and whichever PC-98 model you do or do not own. An untested claim that
is labelled as such is useful; an untested claim that is not is a trap for the next person.
The machines this has actually run on are listed in section 1.

Please do not add copyrighted material or anyone's personal data to the repository — see
section 8. Once it is in the history it cannot be taken back out.

---

## 10. License

MIT. See [LICENSE](LICENSE).

No warranty. `98hdd` writes directly into your disk image. **Back up any image you care
about before pointing this at it.**
