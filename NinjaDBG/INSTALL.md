# Installing NinjaDBG

NinjaDBG v1.1.4 is a CLI-only stealth debugger for Linux x86-64. The GUI
was removed in v1.1.4, so there are no Cairo, Pango, or X11 dependencies.

Licensed under Apache-2.0.

---

## 1. Required dependencies

Only two packages are required to build the core binary:

| Package          | Debian/Ubuntu                          | Fedora                                |
|------------------|----------------------------------------|---------------------------------------|
| build-essential  | `sudo apt-get install build-essential` | `sudo dnf install gcc gcc-c++ make`   |
| pkg-config       | `sudo apt-get install pkg-config`      | `sudo dnf install pkgconfig`          |

That is it. No graphics libraries, no GUI toolkits, no font dependencies.

---

## 2. Optional dependencies

These enable extra features. NinjaDBG runs fine without any of them.

| Optional dep                | Enables                                 | Install                                       |
|-----------------------------|-----------------------------------------|-----------------------------------------------|
| wine, wine64                | Windows PE cross-debugging              | `sudo apt-get install wine wine64`            |
| qemu-user                   | macOS Mach-O cross-debugging            | `sudo apt-get install qemu-user`              |
| python3 + angr              | angr decompilation backend              | `pip3 install angr`                           |
| retdec-dev                  | RetDec native decompilation backend     | `sudo apt-get install retdec-dev`             |
| lua5.4 + liblua5.4-dev      | Lua scripting backend                   | `sudo apt-get install lua5.4 liblua5.4-dev`   |
| linux-headers-$(uname -r)   | Kernel stealth LKM (ninja_stealth.ko)   | `sudo apt-get install linux-headers-$(uname -r)` |

---

## 3. Quick build

```bash
git clone https://github.com/ChapzoMods/NinjaDBG.git
cd NinjaDBG
make -j4
```

This produces:

- `build/ninjadb`              -- the CLI binary
- `build/target_test`          -- demo target program
- `build/libninjastealth.so`   -- preload stealth payload

Verify the build:

```bash
./build/ninjadb --help
./build/ninjadb --no-eula-check -c "help; quit"
```

---

## 4. Install system-wide

To install the binary, the stealth library, and the stealth source into
`/usr/local`:

```bash
sudo make install
```

This copies:

- `build/ninjadb`              -> `/usr/local/bin/ninjadb`
- `build/libninjastealth.so`   -> `/usr/local/lib/libninjastealth.so`
- `scripts/ninjastealth.c`     -> `/usr/local/share/ninjadb/ninjastealth.c`

After installation, run `ldconfig` once so the shared library is found:

```bash
sudo ldconfig
```

Then run from anywhere:

```bash
ninjadb --help
```

---

## 5. Uninstall

```bash
sudo make uninstall
```

This removes the three files listed above. It does not touch your home
directory or your `~/.config/ninjadb/` EULA acceptance file; remove that
manually if you want a clean slate:

```bash
rm -rf ~/.config/ninjadb
```

---

## 6. Debian / Ubuntu .deb package

The `deb` Makefile target builds a `.deb` package using `dpkg-deb`.

```bash
make deb
```

This produces `build/ninjadb_v1.1.4_amd64.deb`. Install it with:

```bash
sudo apt-get install ./build/ninjadb_v1.1.4_amd64.deb
# or, equivalently:
sudo dpkg -i build/ninjadb_v1.1.4_amd64.deb
```

Uninstall the `.deb` with:

```bash
sudo apt-get remove ninjadb
```

The `.deb` installs into the same `/usr/local` paths as `make install`,
so the two installation methods are interchangeable.

---

## 7. Arch Linux (AUR)

NinjaDBG is packaged for the Arch User Repository. Install with an AUR
helper such as `yay` or `paru`:

```bash
yay -S ninjadb
# or
paru -S ninjadb
```

Or build from the PKGBUILD manually:

```bash
git clone https://aur.archlinux.org/ninjadb.git
cd ninjadb
makepkg -si
```

The AUR package tracks the latest stable release on GitHub. If the AUR
package is out of date, please flag it on the AUR website or open an
issue.

---

## 8. Build from source (manual)

If your distribution is not Debian/Ubuntu/Arch, build from source:

```bash
git clone https://github.com/ChapzoMods/NinjaDBG.git
cd NinjaDBG
make -j4
sudo make install
sudo ldconfig
```

Requirements: a C++17 compiler (g++ >= 8 or clang++ >= 7), GNU make, and
`pkg-config`.

---

## 9. Verify the install

After installing by any method, verify:

```bash
ninjadb --help
ninjadb --no-eula-check -c "decomp list; pretty list; script list; quit"
```

The first command prints the usage banner. The second lists the
decompiler, pretty-printer, and scripting backends. All three should
print at least a header line; the per-backend status will say
`[AVAILABLE]` or `[NOT INSTALLED]` depending on which optional
dependencies you installed.

---

## 10. Troubleshooting

### `ninjadb: error while loading shared libraries: libninjastealth.so`

Run `sudo ldconfig` after `make install`. If that does not help, check
that `/usr/local/lib` is on the linker path:

```bash
echo /usr/local/lib | sudo tee /etc/ld.so.conf.d/local.conf
sudo ldconfig
```

### EULA prompt appears every run

The EULA acceptance is stored in `~/.config/ninjadb/eula_accepted`. If
your home directory is read-only or `$HOME` is unset, the file cannot be
written. Either set `HOME` to a writable directory or use
`--no-eula-check` for batch / scripted use.

### `make: g++: command not found`

Install `build-essential` (Debian/Ubuntu) or `gcc-c++` (Fedora).

### Kernel module fails to load

The kernel stealth LKM (`ninja_stealth.ko`) requires:

- `linux-headers-$(uname -r)` installed.
- Secure Boot disabled, or the module signed by a key trusted by your
  firmware.
- Root privileges.

Most users do not need the LKM -- the userland stealth layer
(`libninjastealth.so`) covers the common anti-debug checks.

---

## 11. License

NinjaDBG is licensed under the Apache License 2.0. See the `LICENSE`
file in the repository root for the full text.
