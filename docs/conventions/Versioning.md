# 버전 & 변경기록

> 출처: [Semantic Versioning 2.0](https://semver.org/) + [Keep a Changelog 1.1](https://keepachangelog.com/)

## 한 줄 요약
**버전 = `큰.중간.작은`** (예 `0.3.2`) · **바뀐 건 `CHANGELOG.md` 맨 위 `[Unreleased]` 에 바로바로 적는다.**

## 버전 숫자 올리는 법 (게임용으로 풀어 씀)
| 자리 | 언제 올리나 | 예 |
|---|---|---|
| **큰** (1.x.x) | 출시 / 세이브 호환이 깨짐 | 정식 출시 = `1.0.0` |
| **중간** (x.1.x) | 마일스톤 완료 (M1, M2…) | M1 끝 → `0.1.0`, M2 끝 → `0.2.0` |
| **작은** (x.x.1) | 버그 수정·밸런스만 | `0.2.1` |

- 출시 전은 `0.x.y`. 마일스톤 번호와 중간 자리를 맞추면 헷갈리지 않는다 (`M3` = `0.3.x`).
- 태그는 **`main` 에 합칠 때만**: `v0.3.0`
- 엔진 프로젝트 안 버전 필드도 같은 값으로 맞춘다 — 정확한 위치는 `docs/engines/<엔진>.md` "버전" 절 (Unity `Player Settings > Version`, Unreal `Project Settings > Description > Version`, Godot `project.godot` `config/version`). 모바일 빌드 번호는 태그할 때마다 +1.

## CHANGELOG 규칙
1. 루프는 **사용자에게 보이는 변화가 있는 커밋**마다 `[Unreleased]` 아래 해당 칸에 한 줄 추가.
   (리팩터·문서·루프 설정은 안 적는다 — 그건 git log 에 있다)
2. 칸은 이 6개만: `Added` 추가 / `Changed` 변경 / `Fixed` 수정 / `Removed` 삭제 / `Balance` 밸런스(우리 추가) / `Known Issues` 알려진 문제(우리 추가)
3. `main` 합칠 때 감독이 `[Unreleased]` → `[0.3.0] - 2026-09-20` 로 이름 바꾸고 새 빈 `[Unreleased]` 를 위에 만든다.
4. **플레이어가 읽는다고 생각하고** 쓴다: "EnemyAI.cs 리팩터" X → "고블린이 벽에 끼는 문제 수정" O

형식은 저장소 루트 `CHANGELOG.md` 참고.
