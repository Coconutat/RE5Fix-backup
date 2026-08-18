# RE5Fix Domain Glossary

- **RE5Fix** — A DLL hook/fix for Resident Evil 5 that adjusts FOV, FPS cap,
  shadow quality, colour filter, resolution limits, borderless windowed mode,
  and ultrawide-related UI/cutscene/movie behaviour.
- **re5dx9.exe** — The 32-bit DirectX 9 game executable of Resident Evil 5
  (Steam/Gold Edition).
- **v1.0.0.129** — The game executable version this repository is currently
  being adapted to; PE timestamp 2023-02-13.
- **Proxy DLL** — A DLL that forwards exported functions to the real system DLL
  with the same name so the game loads the mod without noticing.
- **Pattern scan** — Searching the unpacked game memory for a byte signature to
  locate code/data to patch instead of relying on fixed addresses.
- **Inline hook** — Replacing the first bytes of a target instruction with a
  jump to a mod-provided assembly stub, then jumping back.
- **Memory patch** — Directly overwriting bytes in the game's memory (used for
  simpler fixes such as UI scaling and resolution limits).
- **Enigma Protector** — The packer/protector used by `re5dx9.exe`; the original
  game code is decrypted only at runtime, so signatures must be checked in
  process memory.
- **DXVK** — A Direct3D 9 → Vulkan translation layer; can coexist with RE5Fix.
- **Ultrawide fixes** — The set of RE5Fix features that correct UI scaling,
  cutscene crashes, and movie stretching at aspect ratios wider than 16:9.
- **MovieFix** — The RE5Fix feature that prevents pre-rendered movies from being
  stretched; its v1.0.0.129 signature is still unresolved (TODO).
