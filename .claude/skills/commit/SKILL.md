---
name: commit
description: 변경 사항을 커밋해 달라는 요청("커밋해줘", "이거 커밋")을 받았을 때 사용. 커밋 규칙에 맞춰 조각별로 커밋한다.
disable-model-invocation: true
---

1. `docs/conventions/GitCommit.md` 와 `docs/conventions/BranchStrategy.md` 를 읽는다.
2. `git status` 와 `git diff --stat` 으로 바뀐 파일을 확인한다.
3. 현재 브랜치가 규칙에 맞는지 본다. `main` 이면 멈추고 알린다.
4. 올릴 파일을 `git add <파일들>` 한 뒤 `python3 tools/check_conventions.py --staged` 를 돌린다 (훅도 똑같이 검사함). 오류가 있으면 목록을 보여 준다.
5. 에셋과 `.meta` 가 짝으로 있는지 확인한다.
6. 여러 조각이 섞였으면 나눠서 커밋하자고 제안한다.
7. 조각마다 파일 목록과 메시지를 보여 주고 승인받은 뒤 `git commit`. (`git add -A` / `.` 금지, `--no-verify` 금지)
8. push 는 따로 요청받았을 때만.

이 파일과 규칙 문서 내용이 다르면 규칙 문서를 따른다.
