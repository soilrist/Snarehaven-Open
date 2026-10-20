---
name: release
description: develop 을 main 에 합치고 버전을 올리자는 요청("main 에 올리자", "릴리즈해줘", "M1 마무리", "올려")을 받았을 때 사용. 루프를 멈추고 합치기 조건 확인부터 태그까지. 사람 승인 없이는 합치지 않는다.
---

1. `docs/conventions/BranchStrategy.md`, `docs/conventions/Versioning.md` 를 읽는다.
2. **루프 멈추기** — `loop/ctl.sh wrapup` → `loop/ctl.sh status` 로 "실행 중: 아니오" 가 될 때까지 기다린다 (최대 한 바퀴 시간). 안 멈추면 사람에게 알리고 중단.
3. 합치기 조건을 하나씩 확인하고 표로 보여 준다 — 못 확인한 건 "못 함"으로:
   컴파일 에러 0 / 테스트 통과 / `check_conventions.py` 오류 0 / INBOX 에 `결함`·`막힘` 없음 / 사람이 플레이해 봄(사람에게 물어봄)
4. 하나라도 실패·못 함이면 멈추고 알린다 (루프를 다시 켤지 물어봄).
5. 새 버전 번호를 규칙대로 제안하고 **승인받는다.**
6. `develop` 에서 `CHANGELOG.md` 의 `[Unreleased]` 를 새 버전·날짜로 바꾸고 빈 `[Unreleased]` 를 위에 만든다 → 보여 주고 승인 → `docs(changelog): v0.1.0 기록` 커밋.
7. `git checkout main` → `git merge --no-ff develop -m "merge: develop → main (v0.1.0)"` → `git tag v0.1.0`.
8. **반드시 `git checkout develop` 으로 돌아온다.** 확인: `git branch --show-current` = develop.
9. push 는 따로 요청받았을 때만. 루프 다시 켜기는 사람에게 묻고 `loop/ctl.sh on`.

이 파일과 규칙 문서 내용이 다르면 규칙 문서를 따른다.
