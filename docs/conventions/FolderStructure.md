# 폴더 구조 (공통 원칙)

> 엔진 프로젝트 안쪽 폴더 트리·파일 다루기·어셈블리 구성은 엔진마다 다르다 → **`docs/engines/<엔진>.md` "폴더 구조" 절이 진짜 표.**
> 여기엔 저장소 최상위와 세 엔진 다 통하는 원칙만 적는다.

## 한 줄 요약
**우리가 만든 것과 남이 만든 것(에셋스토어·패키지·플러그인)이 절대 안 섞이게, 우리 것은 프로젝트 안 정해진 한 폴더 아래에 전부 모은다.**
(Unity `Assets/_Project/` · Unreal `Content/Game/` · Godot `game/` — 이름은 달라도 역할은 같다.)

```
저장소/
├─ CLAUDE.md                ← AI가 먼저 읽는 한 장
├─ CHANGELOG.md             ← 버전별 바뀐 것
├─ docs/
│  ├─ DESIGN.md             ← 기획서(GDD)
│  ├─ STATUS.md             ← 바퀴 기록 (최근 것만, 오래된 건 archive/)
│  ├─ ROADMAP.md            ← 마일스톤 순서
│  ├─ feedback/INBOX.md     ← 사람 → 루프 지시
│  ├─ conventions/          ← 공통 규칙 책장 (필요할 때만 연다)
│  ├─ engines/              ← 엔진별 구체 규칙 (unity.md / unreal.md / godot.md)
│  ├─ checks/               ← 엔진별 검사 방법
│  ├─ templates/            ← 양식
│  ├─ specs/                ← 기능 명세 (templates/FeatureSpec 복사본)
│  ├─ decisions/            ← 결정 기록 (templates/Decision 복사본)
│  ├─ postmortems/          ← 사고 기록
│  └─ archive/
├─ loop/                    ← 일꾼 루프 파일 (PROMPT.md, DIRECTOR.md, 스크립트)
├─ tools/                   ← 검사기·가공 스크립트
├─ captures/                ← 검사용 캡처 (tmp/ 는 깃 제외)
├─ _Source/                 ← 에셋 원본 (psd, 생성 원본 등 — 엔진이 안 읽음)
│  ├─ raw/                  ← 깃 제외
│  └─ picked/
└─ (엔진 프로젝트 폴더)      ← 엔진별 정확한 트리는 docs/engines/<엔진>.md
```

## 규칙
1. **폴더는 "종류 → 대상" 순서.** `Art/Sprites/Enemies/Goblin/` O, `Goblin/Sprites/` X. 왜: 루프가 "스프라이트 어디 둬?"를 매번 고민하지 않게.
   - 예외: 한 기능이 크게 독립적이면(미니게임 등) 우리 것 폴더 아래에 `Features/이름/` 을 만들고 같은 하위 구조를 반복해도 된다. 만들 때 decisions/ 에 기록.
2. **빈 폴더 금지.** 왜: 메타·임포트 사이드카 파일만 남아서 깃이 지저분해진다.
3. **폴더 이름에 공백·한글 금지.** 케이스는 엔진 컨벤션(Unity/Unreal PascalCase, Godot snake_case)을 따른다.
4. 우리가 안 만든 것(에셋스토어·플러그인·애드온)은 **고치지 않는다.** 업데이트 때 덮어써져서 고친 게 사라진다.
5. 새 최상위 폴더(우리 것 폴더 바로 아래)가 필요하면 **루프는 만들지 않고 INBOX `질문`으로 제안**한다. 사람 승인 후 감독이 이 문서·`docs/engines/<엔진>.md` 를 고치고 나서 쓴다. (하위 폴더는 규칙 1 을 따르면 🟢)

## 엔진 파일 다루기 · 어셈블리/모듈 구성
전부 엔진마다 다르다 (Unity `.meta` 짝맞춤·asmdef, Unreal Content Browser·리다이렉터·C++ 모듈, Godot `.import` 사이드카) → **`docs/engines/<엔진>.md`** 의 "파일 다루기" 절을 본다.
