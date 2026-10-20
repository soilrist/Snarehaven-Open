---
name: new-content
description: 몬스터·아이템·스킬·퀘스트·대사 같은 게임 콘텐츠를 추가해 달라는 요청("고블린 추가해줘", "포션 아이템 만들어줘")을 받았을 때 사용. 코드가 아니라 데이터 에셋으로 추가한다.
---

1. `docs/conventions/DataDriven.md`, `docs/conventions/AssetNaming.md` 를 읽는다.
2. `docs/DESIGN.md` 에서 해당 콘텐츠 항목을 grep 으로 찾는다 (통째로 읽지 않음).
3. 이미 있는 데이터 틀을 쓴다 (엔진별 코드 위치·형식은 `docs/engines/<엔진>.md`). 틀이 없을 때만 new-script 절차로 틀을 만든다.
4. 데이터 에셋을 엔진별 이름·폴더 규칙대로 만든다 (에디터/엔진 MCP 로, 구체 규칙·예시는 `docs/engines/<엔진>.md`).
5. 숫자는 DESIGN §6 에서 가져온다. 없으면 §6 표에 근거와 함께 먼저 적고 데이터에 넣는다 (`DataDriven.md` 3 — 정하는 법·`-1` 금지는 거기). 그림이 딸린 콘텐츠(캐릭터·무기·아이콘)는 `docs/reference/<대상>.md` 시트부터 (`Reference.md` §2).
6. ID 는 `DataDriven.md` 의 ID 규칙대로 에셋 이름에서 기계적으로 만든다. 기존 ID 와 겹치지 않는지 grep 으로 확인.
7. `python3 tools/check_conventions.py --changed` 로 이름·자리표시 경고를 확인하고 보고한다.

이 파일과 규칙 문서 내용이 다르면 규칙 문서를 따른다.
