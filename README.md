# Snarehaven

동물을 사냥하거나 체력을 깎아 **올가미로 포획**하고, 포획한 동물을 **목장**에 두면 시간이 지나 생산물을 주는 모바일 협동 생존 + 목장 방치형 게임.

A mobile co-op survival + idle-ranch game: hunt animals for materials or weaken and **snare** them, then keep them on your **ranch** to produce goods over time.

- Engine: Unreal Engine 5.8 (Mobile renderer) · iOS / Android
- Backend: PlayFab CloudScript (`tools/playfab/cloudscript_main.js`) — the server decides currency, inventory and ranch timers
- Multiplayer: EOS P2P listen server, 4–8 players PvE
- Built by one person directing AI agents: a director session plus an autonomous build loop (`loop/`, `CLAUDE.md`, `docs/conventions/`)

## Run
1. Open `Snarehaven.uproject` with UE 5.8 (Git LFS required: `git lfs pull`).
2. Online features need your own keys. Create `Config/PlayFab.ini`, `Config/EOS.ini` and `Config/UserEngine.ini` (they are git-ignored and not included).

## Docs
- Design: `docs/DESIGN.md` · Progress: `docs/STATUS.md` · Roadmap: `docs/ROADMAP.md`
- Asset licenses: `docs/ASSET_LICENSES.md`

License: MIT for code — see `LICENSE` for the scope and third-party asset exceptions.
