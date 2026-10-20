# 엔진 모듈: Unreal

> 공통 원칙은 `docs/conventions/AssetNaming.md` · `CodingStyle.md` · `FolderStructure.md` · `DataDriven.md`.
> 여기엔 **Unreal 에서 실제로 어떻게** 만 적는다. 검사(테스트·캡처) 방법은 `docs/checks/unreal.md`.

## 에셋 이름 접두어
출처: Epic [Gameplay Ability System / Content 규칙](https://dev.epicgames.com/documentation/en-us/unreal-engine/recommended-asset-naming-conventions-in-unreal-engine-projects) 표.

| 종류 | 접두어 | 예시 |
|---|---|---|
| 블루프린트 | `BP_` | `BP_Enemy_Goblin` |
| 위젯 블루프린트(UMG) | `WBP_` | `WBP_HUD_Health` |
| 정적 메시 | `SM_` | `SM_Barrel` |
| 스켈레탈 메시 | `SK_` | `SK_Player_Knight` |
| 머티리얼 | `M_` | `M_Water` |
| 머티리얼 인스턴스 | `MI_` | `MI_Water_Dark` |
| 머티리얼 함수 | `MF_` | `MF_Dissolve` |
| 텍스처 | `T_` | `T_Rock_D` |
| 애니메이션 시퀀스 | `A_` | `A_Player_Hero_Attack_01` |
| 애님 블루프린트 | `ABP_` | `ABP_Player_Hero` |
| 애님 몽타주 | `AM_` | `AM_Player_Hero_Attack` |
| 블렌드 스페이스 | `BS_` | `BS_Player_Locomotion` |
| 나이아가라 시스템 | `NS_` | `NS_Slash_Fire` |
| 나이아가라 이미터 | `NE_` | `NE_Spark` |
| 파티클 시스템(구 Cascade) | `PS_` | `PS_Impact_Dust` |
| 사운드(웨이브/큐) | `SFX_` / `BGM_` | `SFX_Hit_Metal_02` (Unity 표와 동일하게 통일 — Epic 공식 접두어 없음) |
| 데이터 테이블 | `DT_` | `DT_Enemy` |
| 데이터 애셋 | `DA_` | `DA_Enemy_Goblin` |
| 입력 액션(Enhanced Input) | `IA_` | `IA_Move` |
| 입력 매핑 컨텍스트 | `IMC_` | `IMC_Default` |
| 커브 애셋 | `Curve_` | `Curve_Falloff` |
| 레벨(맵) | `L_` | `L_Town_Hanyang`, `L_Dungeon_01` |

### 접미어 (텍스처) — Unity 표와 동일
`_D`(기본색) `_N`(노멀) `_M`(마스크: ORM 묶음) `_E`(발광) `_R`(거칠기) `_A`(알파)

### 접두어 없음
- **C++ 클래스**: 타입 접두어(`A`/`U`/`F`/`I`/`E`)가 이미 이름에 들어간다 — 아래 "코드" 참고. 파일 이름 = 클래스 이름(접두어 포함, `AEnemyGoblin.h`).
- **폴더**: PascalCase.

## 코드 — 이름 (Epic Coding Standard)
| 대상 | 규칙 | 예 |
|---|---|---|
| `AActor` 파생 클래스 | `A` + PascalCase | `AEnemyCharacter` |
| `UObject` 파생 클래스(액터 아님) | `U` + PascalCase | `UHealthComponent` |
| `USTRUCT` | `F` + PascalCase | `FDamageInfo` |
| 인터페이스 | `I` + PascalCase | `IDamageable` |
| enum | `E` + PascalCase | `EDamageType` |
| **bool 변수** | **`b` + PascalCase** (Unity `m_Is…`와 대응) | `bool bIsDead;` |
| 멤버 변수(그 외) | PascalCase, 별도 접두어 없음 (Unity `m_`와 다름) | `float MoveSpeed;` |
| 함수·메서드 | PascalCase, 동사로 시작 | `TakeDamage()`, `IsGameOver()` |
| 델리게이트/이벤트 | `On` + PascalCase (멀티캐스트는 `F...Signature`) | `OnHealthChanged`, `FOnHealthChangedSignature` |
| 매개변수·지역변수 | PascalCase (Unity 의 camelCase 지역변수와 다름 — Epic 표준) | `float DamageAmount` |

**Epic 표준은 `m_`/`k_`/`s_` 접두어를 쓰지 않는다** — 대신 타입 접두어(`b`, `F`, `U`, `A`, `E`)로 구분. 이 프로젝트에서 Unity 쪽 `m_` 관례를 그대로 가져오지 않는다 (엔진 표준을 우선).

## 코드 — Unreal 규칙 (실제 버그를 막는 부분)
1. **Blueprint-노출 변수는 `UPROPERTY(EditAnywhere, BlueprintReadOnly)`** 로 캡슐화 의도를 명시. 아무 이유 없이 `EditDefaultsOnly`/`VisibleAnywhere` 안 섞기.
2. **`Tick()` 에서 `FindComponentByClass`, `GetAllActorsOfClass` 반복 호출 금지** → `BeginPlay` 에서 한 번 캐싱.
3. **밸런스 숫자를 코드에 박지 않는다** → 데이터(DataTable/DataAsset)로 (DataDriven.md).
4. **`GetObjectsOfClass`/문자열 애셋 경로로 찾기 지양** → `TSoftObjectPtr`/직접 참조.
5. **델리게이트 구독은 `BeginPlay`/생성자에서 `AddDynamic`, `EndPlay`에서 `RemoveDynamic`.**
6. **포인터는 `IsValid()`로 검사**, raw `nullptr` 비교만으로 끝내지 않는다 (파괴 예약된 액터는 포인터가 non-null 이어도 무효할 수 있다).
7. 타이머·비동기 작업은 액터 파괴 시 `GetWorldTimerManager().ClearTimer` 로 정리.
8. `UE_LOG` 는 커스텀 로그 카테고리로: `UE_LOG(LogCombat, Log, TEXT("..."))`. 커밋 전 임시 로그 삭제.
9. 에디터 전용 코드는 별도 `Editor` 모듈에만. `#if WITH_EDITOR` 는 최후 수단.

## 코드 — 예시
```cpp
// Health.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Health.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChangedSignature, float, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDiedSignature);

UCLASS(BlueprintType)
class MYGAME_API UHealthComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UHealthComponent();

    UFUNCTION(BlueprintCallable, Category = "Health")
    void TakeDamage(float Amount);

    UFUNCTION(BlueprintPure, Category = "Health")
    bool IsDead() const { return CurrentHealth <= 0.f; }

    UPROPERTY(BlueprintAssignable)
    FOnHealthChangedSignature OnHealthChanged;

    UPROPERTY(BlueprintAssignable)
    FOnDiedSignature OnDied;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(EditDefaultsOnly, Category = "Health")
    class UHealthConfig* Config;   // 최대 체력·무적 시간은 데이터(DA_Player_Hero_Health)에서

    float CurrentHealth = 0.f;
    float InvincibleUntil = 0.f;
    bool bIsDead = false;
};
```
> `UHealthConfig` **클래스**는 접두어 없음, 그걸로 만든 **데이터 애셋 파일**만 `DA_` (`DA_Player_Hero_Health`).

## 폴더 구조
```
Content/                    ← Unreal 프로젝트가 하위 폴더면 unreal/Content/
├─ Game/                    ← ★ 우리 것 전부 (Unity 의 _Project 와 대응)
│  ├─ Characters/  Enemies/  Environment/  UI/  VFX/  Audio/
│  ├─ Data/                 ← DataTable/DataAsset
│  │  ├─ Items/  Enemies/  Skills/  Quests/  Dialogue/  Balance/
│  ├─ Blueprints/
│  ├─ Maps/                 (L_*.umap)
│  ├─ Input/                (IA_*, IMC_*)
│  └─ UI/
├─ Sandbox/                 ← 실험용. 합격하면 Game/ 으로 옮김
└─ (ThirdParty 플러그인 콘텐츠는 Plugins/<이름>/Content/ 에 — Content/ 안에 안 둠)

Source/                     ← C++ 모듈
├─ MyGame/
│  ├─ MyGame.Build.cs
│  ├─ Public/  Private/
└─ MyGameEditor/            ← 에디터 전용 모듈 (선택)
```

## 언리얼 파일 다루기 (연결이 끊기는 사고 방지)
`.uasset`/`.umap` 은 **바이너리**다. 텍스트 도구로 못 고치고, git diff/merge 가 안 된다.
1. **파일 이동·이름 변경은 Content Browser 안에서만** (탐색기·Finder·`mv`·`git mv` 금지) — 그래야 리다이렉터가 자동 생성돼 참조가 안 끊긴다.
2. 이동·이름변경 후 **"Fix Up Redirectors in Folder"** 를 실행하고 그 결과(리다이렉터 삭제)까지 커밋.
3. **같은 애셋을 여러 사람이 동시에 고치지 않는다.** 바이너리라 병합이 안 되므로 — 나중 커밋이 먼저 커밋을 덮어써도 git 이 충돌로 못 잡아낼 수 있다. `docs/conventions/BranchStrategy.md` "합치기 충돌" 참고.
4. `.uasset` 은 원본 그대로 커밋 (Unity 의 `.meta` 같은 별도 사이드카 파일 없음).
5. Git LFS 로 바이너리 애셋을 관리하는 게 강력 권장 (`.gitattributes` 에 `*.uasset filter=lfs`, `*.umap filter=lfs`).

## 브랜치 합치기 충돌
**바이너리 애셋(`.uasset`/`.umap`)은 git 이 병합할 수 없다.** 충돌이 나면 자동 병합 시도 자체가 무의미 — 두 버전 중 하나를 고르거나(`--ours`/`--theirs`), 애초에 같은 맵/애셋을 병렬로 안 고치는 규칙(BranchStrategy.md 1)을 지킨다. C++ 소스(`.h`/`.cpp`)는 일반 텍스트 병합 그대로.

## 버전
`Project Settings > Description > Version` 을 `CHANGELOG.md`/태그와 같은 값으로. 모바일 빌드 번호(`Project Settings > Platforms > Android/iOS`)는 태그할 때마다 +1.

## 에디터 연결 (MCP — `ModelContextProtocol` 플러그인)
- **조회는 자유:** 아웃풋 로그, 액터·컴포넌트 값, 월드 계층 읽기.
- **변경은 조건부:** 애셋 생성·이동, 컴포넌트 부착, 레벨 저장은 — 감독/수동 작업이면 승인 후, 루프면 이번 조각 범위 안에서만.
- **금지:** 사람이 PIE(Play In Editor) 중이거나 INBOX 에 `보류:` 가 있을 때 에디터 변경. 프로젝트 설정·플러그인 변경은 INBOX 지시가 있을 때만.
- **설정(2026-09-18, 사람 지시로 켬):** `Snarehaven.uproject` 에 `ModelContextProtocol`(`TargetAllowList: Editor`). 엔진 내장(`Engine/Plugins/Experimental/ModelContextProtocol`, Mac 바이너리 있음 — 빌드 불필요). 자동 시작은 사용자별 `Saved/Config/MacEditor/EditorPerProjectUserSettings.ini` 의 `bAutoStartServer=True` (커밋 안 됨). 클라이언트 설정 `.mcp.json` 은 uproject 옆, `http://127.0.0.1:8000/mcp` (gitignore).
- **`AllToolsets` 는 2026-09-21 껐다(사람 승인):** 이 엔진(5.8.2)에서 `unreal.ToolsetDefinition`·`unreal.AgentSkill`·`unreal.PythonTestRunner` 등 API 자체가 없어 Python 서브모듈 임포트가 전부 실패 — 부팅마다 `LogPython: Error:` 트레이스백만 남기고 실제 기능은 없었다(로그 잡음 원인, Jev 1차 필터 점검 중 발견). 엔진 버전이 맞아 다시 쓸 일 생기면 `Snarehaven.uproject` 의 `AllToolsets` 항목에 `"Enabled": true`·`"TargetAllowList": ["Editor"]` 를 되돌리면 된다.
- **수동 시작:** 에디터 하단 `Cmd` 콘솔에 `ModelContextProtocol.StartServer` (로그 `Starting MCP server on port 8000` 로 확인). 플러그인이 없던 에디터가 종료하면서 `EditorPerProjectUserSettings.ini` 를 덮어써 `bAutoStartServer` 블록이 사라질 수 있다 — 자동 시작이 안 되면 블록부터 확인(감독 실증 2026-09-18).
- **주의:** `bAutoStartServer` 는 커맨드릿에도 적용돼 헤드리스 실행마다 8000 포트를 잡으려 한다. 에디터가 이미 잡고 있으면 커맨드릿 쪽 리스너만 실패하고 스크립트는 그대로 돈다(감독 실증 2026-09-18, `docs/checks/unreal.md` 함정 참고).
