#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "AnimalRow.generated.h"

// ROADMAP M1-01: 동물 한 종을 설명하는 데이터 한 줄 (DT_AnimalTable 의 행 구조체).
// 숫자는 전부 DESIGN 3.1 "확정 수치" 표에서 온다 — 코드 기본값은 "아직 안 채움"(-1) 이고
// 진짜 값은 데이터 테이블에 있다 (DataDriven.md 3 · engines/unreal.md 코드 규칙 3).
// ROADMAP M5-07d: 동물이 사람을 어떻게 대하는가 (DESIGN 8.7 "행동" 열 4갈래).
// **코드 `if` 대신 행 칸으로 둔다** — 갈래를 종 이름으로 가르면 종이 늘 때마다 코드가 는다 (DataDriven.md 1).
// 갈래마다 무슨 일이 일어나는지는 M5-07d2·d3 가 채운다. 이 조각은 **자리와 값만** 세운다.
UENUM(BlueprintType)
enum class EAnimalBehaviorKind : uint8
{
	/** 도주만 한다 — 맞으면 달아나고 끝까지 반격하지 않는다 (새 4종). */
	Flee,

	/** 도주하다 **궁지**(도주 이탈 한계 500cm)에 몰리면 돌아서서 친다 (여우·순록). */
	CorneredCounter,

	/** 첫 타를 맞으면 도주 대신 그 자리에서 싸운다 (염소·엘크·스라소니). */
	RetaliateWhenHit,

	/** **선공** — 감지 반경 안에 사람이 들어오면 먼저 다가와 친다 (늑대·울버린·곰). */
	Aggressive
};

USTRUCT(BlueprintType)
struct FAnimalRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 세이브·백엔드가 쓰는 고정 ID. 행 이름 `Animal_Pig` ↔ `animal.pig` (DataDriven.md 4 — 한번 정하면 안 바꾼다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	FName AnimalID;

	/** 화면에 보이는 이름. 로컬라이제이션을 위해 FString 이 아니라 FText. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	FText DisplayName;

	/** 최대 체력. DESIGN 3.1 「동물 체력」 종별 80~200 (M22). 음수면 미정 취급 (Verification.md 7). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	float MaxHP = -1.f;

	/** 포획 가능 임계값(최대 체력의 %). DESIGN 3.1 = 25. 음수면 미정 취급. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	float CaptureThresholdPct = -1.f;

	/**
	 * 맞고 도망갈 때의 이동 속도(cm/s). DESIGN 3.1 = 250 (전 종 공통, MVP).
	 * 음수면 미정 취급 — 그 종은 아예 안 움직인다 (Verification.md 7).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	float MoveSpeed = -1.f;

	/** 죽었을 때 나오는 재료 아이템 ID 들 (DT_ItemTable 의 `ItemID`). 재화는 절대 넣지 않는다 (DESIGN 3.1·4.8). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	TArray<FName> DeathDropItems;

	/** 사망 드롭 개수 범위의 최솟값. DESIGN 3.1 "재료 아이템 1~3개" = 1. 음수면 미정 취급. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	int32 DeathDropCountMin = -1;

	/** 사망 드롭 개수 범위의 최댓값. DESIGN 3.1 "재료 아이템 1~3개" = 3. 음수면 미정 취급. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	int32 DeathDropCountMax = -1;

	/**
	 * 사망 드롭 **한 개마다** 최상급(`item.pristine_` 로 시작하는 ID)이 나올 확률 0~1. DESIGN 3.1 = 0.05 (M22).
	 * 나머지는 최상급이 아닌 항목에서 균등하게 뽑는다. 목록에 최상급이 없는 종(늑대)은 이 칸을 안 쓴다.
	 * 음수면 미정 취급 — 최상급을 아예 굴리지 않는다 (Verification.md 7).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	float PristineDropChance = -1.f;

	/**
	 * 죽거나 포획된 뒤 액터가 사라지기까지 초. DESIGN 3.1 = 8 (M22 — 시체가 영원히 남던 결함).
	 * 호스트가 이 시간 뒤 `Destroy` 하고 복제로 모두에게서 사라진다. 0 이하면 미정 — 치우지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	float CorpseLifeSeconds = -1.f;

	/**
	 * 포획했을 때 되는 펫 아이템 ID (DT_ItemTable 의 `ItemID`, `EItemType::Pet` 행).
	 * 종마다 다른 펫이 되므로 코드가 아니라 행에 둔다 (DataDriven.md 1 — 동물이 늘어도 코드는 안 는다).
	 * 비어 있으면(NAME_None) 그 동물은 아직 펫이 될 수 없다 — 포획해도 지급 요청을 만들지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	FName PetItemID;

	/**
	 * 색 변종을 포획했을 때 되는 펫 아이템 ID (M21-07, `item.pet_<종>_variant`). DESIGN 10.6 「변종·우두머리 확률 고지」.
	 * 비어 있으면 그 종은 변종이 서지 않는다 — 자리표시 돼지·소·양이 그렇다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Variant")
	FName VariantPetItemID;

	/**
	 * 동물이 설 때마다 변종이 될 확률 0~1. DESIGN 10.6 = 0.03 (전 종 같은 값 — 생성기 상수 하나에서 온다).
	 * 음수면 미정 — 굴리지 않는다 (Verification.md 7). 0 은 실제 값(변종 없음)이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Variant")
	float VariantChance = -1.f;

	/** 스켈레탈 메시(M1-07 `SK_Animal_Pig`). 비어 있으면 액터가 자리표시 실린더를 그대로 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	TSoftObjectPtr<USkeletalMesh> MeshRef;

	/**
	 * 가만히 있을 때 돌릴 애니메이션(M1-07 `A_Animal_Pig_Idle`).
	 * 종마다 다른 클립이라 코드가 아니라 행에 둔다 (DataDriven.md 1).
	 * 비어 있으면 메시는 레퍼런스 포즈로 선다 — 그래도 보이기는 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	TSoftObjectPtr<UAnimSequence> IdleAnimRef;

	/**
	 * **자리표시 종인가** (M5-07 — 돼지·소·양). 참이면 기본 플레이에서 한 마리도 서지 않는다:
	 * 분산 스포너의 종 목록에서 빠지고(`AAnimalSpreadActor::MakeSpawnPool`), 레벨에 미리 놓인
	 * 개체는 게임모드가 치운다(`ASnarehavenGameMode::RemovePlaceholderAnimals`).
	 * 검사 스위치 `-SnareStageAnimals` 를 주면 그대로 선다 — 이 셋을 쓰는 검사가 아직 여럿이다.
	 *
	 * **행을 지우지 않고 칸으로 표시하는 이유**: 행을 지우면 그 행을 가리키는 검사·도감이 통째로
	 * 깨진다. 참조가 0 이 되는 날 행째 빠진다 (DESIGN 8.7 머리말 · 사용자 지시 2026-09-23 16:55 ②).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	bool bPlaceholderSpecies = false;

	/**
	 * **이야기 종인가** (메인 스토리 안개뿔 — `docs/specs/story-fogbuck.md` 2a). 참이면 이야기 쪽 코드가 따로 세우는
	 * 한 마리라 무작위 스포너 풀(`AAnimalSpreadActor::MakeSpawnPool`)과 도감 칸·변종 칸·변종 확률 집계에서 빠진다.
	 * 이름이 아니라 칸으로 가른다 — 기본 플레이 종 12 단정이 새 행 때문에 깨지지 않게.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	bool bStorySpecies = false;

	/**
	 * **이 종이 사는 존** (M5-07e). `DT_ZoneTable` 의 `ZoneID` 여야 한다(예: `zone.hunt`) — 없는 존을
	 * 적으면 2층 `Snarehaven.AnimalSpread.Pool.HabitatZonesExist` 가 운다.
	 * 분산 스포너는 자기 존(`FAnimalSpreadRow::ZoneID`)과 같은 행만 세운다(`MakeSpawnPool`).
	 * 목표 존(설원·숲·평원)은 DESIGN 8.7 "서식 존" 열에 있고, 그 존이 표에 생기기 전(M12-00)에는
	 * 전 종이 지금 맵(`zone.hunt`)에 선다 — 배정 근거는 DESIGN 8.7 "서식 존 배정" 표.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	FName HabitatZoneID;

	/**
	 * **메시를 이 높이(cm)에 맞춰 굽는가.** 0 이면 안 굽는다 — 메시를 **배율 1.0** 그대로 붙이고
	 * 발만 캡슐 바닥에 맞춘다. 바르코 12종이 그렇다: 가져올 때 이미 DESIGN 8.7 "크기" 표의 배율로
	 * 구워져 나오므로(`tools/unreal/import_animal_species.py`) 게임에서 또 굽으면 그 표가 무의미해진다.
	 * 양수면 메시 바운드 높이가 그 값이 되게 균일 배율로 굽는다 — 크기 표가 없는 자리표시 종
	 * (Quaternius 돼지·소·양)이 원본 단위 그대로 들어와 화면을 덮는 것을 막던 길이다.
	 *
	 * **캡슐 높이를 쓰지 않는 이유**: 캡슐(90cm)은 조준·사거리 판정용이라 **전 종 공통**이다
	 * (DESIGN 8.7 캡슐 줄). 거기에 몸을 맞추면 곰도 뇌조도 엘크도 전부 90cm 가 된다 —
	 * 감독 실측 결함 2026-09-23 18:55("전 종 몸 높이가 똑같이 90.0cm").
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	float MeshFitHeightCm = 0.f;

	/**
	 * 목 굵기(메시 단위 cm, `MeshFitHeightCm` 배율 전) — 목장 펫 목걸이 고리를 이만큼 벌린다(DESIGN 10.9, 스킨 조각 n).
	 * 숫자를 짓지 않고 메시에서 잰다: 데이터 표 스크립트가 `ARanchPetActor::MeasureNeckWidthCm` 을 불러 채운다.
	 * 0 이면(목 뼈가 없는 몸) 목걸이도 펫 키 × 행 비율로 맞춘다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	float PetNeckWidthCm = 0.f;

	/**
	 * 목 단면 가운데 — 목 뼈에서 잰 거리(메시 단위 cm, 컴포넌트 축). 목걸이 고리를 뼈가 아니라 여기에 둔다(뼈는 목 뒤쪽·가슴 속).
	 * `ARanchPetActor::MeasureNeckCenterCm` 으로 스크립트가 채운다(M40-02 감독 보정).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	FVector PetNeckCenterCm = FVector::ZeroVector;

	/**
	 * 죽었을 때 한 번 재생할 애니메이션(M1-09 `A_Animal_Pig_Death`). 반복하지 않고 끝 프레임에서 멈춘다.
	 * 종마다 다른 클립이라 코드가 아니라 행에 둔다 (DataDriven.md 1).
	 * 비어 있으면 죽어도 Idle 을 계속 돈다 — 보기엔 이상해도 흐름은 안 끊긴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	TSoftObjectPtr<UAnimSequence> DeathAnimRef;

	/**
	 * **걷는 동안 돌릴 클립**(`A_Animal_<종>_Walk`). 도주가 아니라 **다가오는** 걸음이다 —
	 * 선공·반격 종이 사람에게 접근할 때(M5-07d3, 접근 속도 45cm/s) 이 클립이 돈다.
	 * 비어 있으면 대기 클립으로 떨어진다 — 새 4종은 날갯짓 도주 하나로 걷기를 겸해 칸이 빈다 (DESIGN 8.7).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Clips")
	TSoftObjectPtr<UAnimSequence> WalkAnimRef;

	/**
	 * **도망가는 동안 돌릴 클립**(`A_Animal_<종>_Run`). 맞고 달아나는 동안(EAnimalState::Fight) 돈다.
	 * 비어 있으면 걷기 → 대기 순으로 떨어진다. 이 칸이 없던 동안 12종이 **대기 클립인 채로 미끄러지며**
	 * 도망가 교체가 망가져 보였다 (사용자 지시 2026-09-23 16:55 ③).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Clips")
	TSoftObjectPtr<UAnimSequence> FleeAnimRef;

	/**
	 * **때릴 때 한 번 돌릴 클립**(`A_Animal_<종>_Attack`). 트는 것은 M5-07d2·d3 몫이고 이 조각은 자리만 연다.
	 * 새 4종은 공격 클립이 아예 없어 칸이 빈다 (DESIGN 8.7 "포유류 8종 전부 공격 클립을 가진다").
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Clips")
	TSoftObjectPtr<UAnimSequence> AttackAnimRef;

	/**
	 * **맞았을 때 한 번 돌릴 클립**(`A_Animal_<종>_Hit`). 트는 것은 동물이 맞는 티를 내는 조각 몫 —
	 * 지금은 맞으면 곧바로 도주 클립으로 넘어간다. 자리를 먼저 여는 이유는 플레이어 쪽과 같다(8.8 ④).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Clips")
	TSoftObjectPtr<UAnimSequence> HitAnimRef;

	/**
	 * 이 동물이 사람을 어떻게 대하는가. DESIGN 8.7 "행동" 열에서 온다.
	 * 기본값이 `Flee` 인 것은 미정이 아니라 **가장 순한 쪽**이다 — 칸을 안 채운 종이 사람을 먼저 치는
	 * 일은 없어야 한다 (모르는 값이 사람을 때리는 쪽으로 기울면 안 된다).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Attack")
	EAnimalBehaviorKind BehaviorKind = EAnimalBehaviorKind::Flee;

	/**
	 * 동물 1타가 깎는 플레이어 체력. DESIGN 3.1 "동물 공격" 표 (여우 6 … 곰 20 · 새 4종 0).
	 * **0 은 실제 값이다** — 새 4종은 도주만 하므로 한 번도 안 친다. 음수면 미정 취급 (Verification.md 7).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Attack")
	float AttackDamage = -1.f;

	/** 동물 공격 간격(초). DESIGN 3.1 = 1.5 (전 종 공통). 음수면 미정 취급. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Attack")
	float AttackIntervalSec = -1.f;

	/** 동물 공격 사거리(cm). DESIGN 3.1 = 150 (플레이어 근접 사거리를 빌린 값). 음수면 미정 취급. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Attack")
	float AttackRangeCm = -1.f;

	/**
	 * 선공 감지 반경(cm). DESIGN 3.1 = 500 (도주 이탈 한계를 빌린 값) — **선공 종만** 그 값이고
	 * 나머지는 0 이다. **0 은 실제 값이다**(먼저 달려들지 않는다). 음수면 미정 취급.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Attack")
	float AggroRadiusCm = -1.f;

	/**
	 * 맞았을 때 우는 소리 이름 (`SFX_<이름>_01..` 의 이름, 예 `AnimalBirdHit`, M43-02). 비면 무음.
	 * 종마다 따로가 아니라 **묶음 4**(작은 새·사슴류·사냥꾼·가축) 중 하나를 고른다 — DESIGN 8.7 「동물 소리」.
	 * 도구 행 `UseSoundCue` 와 같은 짜임이다(이름 하나 → `USnareSoundSubsystem` 이 변형 중 하나를 고름).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Sound")
	FName HitSoundCue;

	/** 죽을 때 한 번 나는 소리 이름 (`SFX_<이름>`, M43-02). 비면 무음. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Sound")
	FName DeathSoundCue;

	/**
	 * 덤빌 때(선공 대상 잡음·공격 동작 시작) 나는 소리 이름 (`SFX_<이름>`, M43-02).
	 * `BehaviorKind` 가 `Flee` 인 종은 덤비지 않으니 **비어 있어야 정상**이다 — 행동과 소리가 어긋나지 않게.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Sound")
	FName AggroSoundCue;

	/** 이 종의 울음 높낮이 배율. 묶음 소리를 몸 크기에 맞게 낮춘다(곰). 표가 채운다 — DESIGN 8.7 「동물 소리」 높낮이 줄. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal|Sound")
	float SoundPitch = 1.f;
};
