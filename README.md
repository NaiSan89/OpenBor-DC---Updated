# OpenBOR Engine for Sega Dreamcast (Modern GCC 13+ Updated)

[OpenBOR](http://www.chronocrash.com/) is a royalty-free sprite-based side-scrolling gaming engine, based on the source code of the [Beats of Rage](https://en.wikipedia.org/wiki/Beats_of_Rage) game published by [Senile Team](http://www.senileteam.com) back in 2004. 

The `v3.0 Build 4111` release of the official source code was the **last source code revision supporting the Sega Dreamcast platform**. Starting from that release, Sega Dreamcast support was officially dropped. 

**This repository (NaiSan89's fork) breathes new life into the legacy Dreamcast port.** The goal is to fully modernize the engine to compile cleanly on today's strict compilers (GCC 13+) and the latest KallistiOS (KOS) environment, while also pushing for new performance and memory optimizations.

---

## 🚀 Features & Updates in this Fork

- **Modern Compiler Compatibility:** Fixed widespread `-fno-common` "multiple definition" linking errors (`paklist`, `anim_list`, `model_cache`, etc.) that prevented the legacy code from compiling on modern GCC toolchains.
- **KallistiOS Integration:** Fully builds against modern KOS v2.0.0+ along with updated Dreamcast ports of `SDL` and `libtremor` (integer-based OGG decoding for better SH4 CPU performance).

## 🗺️ Roadmap: The Future

- [x] **Toolchain Modernization:** Make the engine compile out-of-the-box on GCC 13+ and modern KOS.
- [ ] **Memory Management Overhaul:** The Sega Dreamcast is strictly limited to 16MB of main RAM. The primary upcoming goal for this fork is to profile memory usage, eliminate RAM leaks, and optimize asset loading to prevent out-of-memory crashes on larger `.pak` files.
- [ ] **Performance Tuning:** Leverage the SH4 FPU and modern GCC optimization flags to hit stable framerates even with heavy action on screen.

---

## 🛠️ Compiling

You will need a working [KallistiOS](http://gamedev.allusion.net/softprj/kos/) `2.0.0+` environment installed on your machine.

1. Open your terminal and clone this repository:
   ```bash
   git clone https://github.com/NaiSan89/OpenBor-DC---Updated.git
   cd OpenBor-DC---Updated/engine
   ```

2. Clean any legacy object files and load the KallistiOS environment:
   ```bash
   make clean
   find . -name "*.o" -delete
   . /opt/toolchains/dc/kos/environ.sh
   ```

3. Build the Dreamcast executable:
   ```bash
   make BUILD_DC=1
   ```

If successful, `OpenBOR.bin` will be generated in the `engine` directory.

---

## 🎮 Usage & Generating the Boot Disc

To run the engine on real hardware or an emulator (like Flycast or Demul), the `OpenBOR.bin` must be scrambled into a `1ST_READ.BIN` boot file, and bundled with a compatible `BOR.PAK` game data file.

1. **Scramble the binary:**
   ```bash
   scramble OpenBOR.bin 1ST_READ.BIN
   # (Note: Use $KOS_BASE/utils/scramble/scramble if it is not in your global PATH)
   ```

2. **Create the Disc Structure:**
   - Create a `cd_root` directory.
   - Place your newly scrambled `1ST_READ.BIN` inside `cd_root`.
   - Place your game's `Paks/BOR.PAK` inside `cd_root`.

3. **Generate the CDI image:**
   Use your preferred Dreamcast homebrew tools (like `mkisofs` and `cdi4dc`, or DreamSDK) to inject an `IP.BIN` and generate the final `.cdi` image for burning or emulation.

---

## 🙏 Credits

- **NaiSan89:** GCC 13+ modernization, KOS toolchain fixes, and memory management optimizations.
- The whole **OpenBOR Team** (Damon Caskey, Plombo, uTunnels, and many more) for building the ultimate 2D engine.
- **Senile Team** (Roel, Jeroen, Sander, Ben) for creating the original Beats of Rage.
- **Neill Corlett** for the original Dreamcast port.
- **SiZiOUS** for the previous iterations of Dreamcast compatibility fixes.
