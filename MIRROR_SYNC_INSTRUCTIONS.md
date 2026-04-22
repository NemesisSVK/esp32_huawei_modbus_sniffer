# Mirror Sync Instructions

Purpose: keep this GitHub mirror sanitized while preserving full local history in the source repo.

## Paths
- Source repo (full local history): `C:\Users\NeMeSiS\!Projects\esp32_modbus_sniffer`
- Mirror repo (GitHub-safe): `C:\Users\NeMeSiS\!Projects\esp32_modbus_sniffer_github_mirror`
- Remote: `https://github.com/NemesisSVK/esp32_huawei_modbus_sniffer.git`

## Regular Sync Flow
1. Run:
   ```powershell
   powershell -ExecutionPolicy Bypass -File C:\Users\NeMeSiS\!Projects\esp32_modbus_sniffer_github_mirror\sync_from_local.ps1
   ```
2. Review changes:
   ```powershell
   git -C C:\Users\NeMeSiS\!Projects\esp32_modbus_sniffer_github_mirror status --short
   ```
3. Commit and push:
   ```powershell
   git -C C:\Users\NeMeSiS\!Projects\esp32_modbus_sniffer_github_mirror add -A
   git -C C:\Users\NeMeSiS\!Projects\esp32_modbus_sniffer_github_mirror commit -m "Sync from local dev repo"
   git -C C:\Users\NeMeSiS\!Projects\esp32_modbus_sniffer_github_mirror push
   ```

## Sanitization Rules (must stay excluded)
- `config.json`
- `data/config.json`
- `ota.json`
- `data/ota.json`
- `compile_commands.json`
- `src/BuildConfig.h`
- `.pio/`
- `__pycache__/`
- `captures/`
- `_supporting-projects/`

## Notes for Future Agent Runs
- Do not run `git push` from the source repo.
- Always sync source -> mirror first, then push from mirror.
- This mirror keeps its own helper files:
  - `.gitignore` (mirror-specific sanitization policy)
  - `sync_from_local.ps1`
  - `MIRROR_SYNC_INSTRUCTIONS.md`
- If sync behavior is changed, keep this document and `sync_from_local.ps1` aligned.
