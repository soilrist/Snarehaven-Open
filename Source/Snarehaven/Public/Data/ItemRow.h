#pragma once

#include "CoreMinimal.h"
#include "Data/ItemAction.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "ItemRow.generated.h"

class UAnimSequence;

// 아이템 종류. 실제로 나오는 것만 둔다 — 제작·요리(DESIGN 8.10)·건축(8.11)이 들어와 배치형·음식이 붙었다.
// **새 값은 맨 끝에만 붙인다** — 표 에셋은 이름으로 저장되지만 복제·세이브 쪽이 순서값을 쓸 수 있다.
UENUM(BlueprintType)
enum class EItemType : uint8
{
	/** 사냥 드롭 재료(고기·가죽류). 쌓이기만 하고 MVP 에서는 쓰지 않는다 (DESIGN 3.3). */
	Material,
	/** 포획한 동물. 목장 슬롯에 배치하는 대상 (DESIGN 3.2). */
	Pet,
	/** 들거나 입는 장비 (DESIGN 3.5). 맨손 공격에 종별 차등이 없어 **스탯이 없는 순수 시각 효과**다. */
	Equipment,
	/** 땅에 놓는 가구·불·텐트·배 (DESIGN 8.11·8.9). 놓기·치우기는 백엔드 판정 (`PlaceStructure`·`LaunchBoat`). */
	Placeable,
	/** 먹으면 체력이 차는 요리 (DESIGN 8.10). 먹기는 CloudScript `ConsumeItem` 응답 뒤 호스트가 회복한다. */
	Food,
	/** 목장 동물에게 씌우는 꾸미기 (DESIGN 10.9). 능력치 없음 — 씌우기는 CloudScript `SetPetCosmetic` 이 판정한다. */
	PetCosmetic,
	/**
	 * 사람이 입는 꾸미기 — **겉모습 덮개** (`docs/specs/skin-parts.md` 5장 1). 능력치 없음. 같은 부위의 장비(효과)는
	 * 그대로 두고 보이는 메시만 덮는다 — 꾸미기를 사서 효과를 잃으면 아무도 안 산다(DESIGN 10.3).
	 */
	Cosmetic,
	/**
	 * 가챠 목장 데코 — 목장 겉모습 스킨 (DESIGN 10.9b · `docs/specs/gacha-deco.md`). 능력치·충돌 없음, 소비 0.
	 * 적용·해제는 CloudScript `SetRanchDeco` 가 판정하고, 종류(`RanchDecoKind`) 태그 액터의 메시·재질·빛만 바꾼다.
	 */
	RanchDeco
};

// 레시피 재료 칸이 아이템 ID 대신 받을 수 있는 묶음 이름 (DESIGN 8.10 "재료 태그").
// "고기 1" 처럼 종을 가리지 않는 재료가 있어서다 — 곰고기든 새고기든 같은 한 칸을 채운다.
/** 쏠 때 손에 주는 반동 진동의 종류 (DESIGN 2.2 「총 반동 진동」). 총 행이 데이터로 고른다 — 아이템 이름으로 가르지 않는다. */
UENUM(BlueprintType)
enum class EShotHapticKind : uint8
{
	/** 진동 없음. 총(`MagazineSize > 0`) 행에 이 값이면 시험이 잡는다. */
	None,
	Pistol,
	Rifle,
};

UENUM(BlueprintType)
enum class ECraftTag : uint8
{
	/** 어느 묶음에도 안 든다. */
	None,
	Wood,
	Cedar,
	Berry,
	Meat,
	/** 가죽·털. 가챠 희귀 소재(`gacha.mat_*hide*`)도 여기로 들어온다 (DESIGN 8.10 마지막 줄). */
	Hide,
	Feather,
	Fish,
	// DESIGN 13.6 「재료 태그 추가」 — 요리 재료 칸. 값은 끝에만 더한다(표 에셋이 숫자로 저장한다).
	Fruit,
	Jam,
	Oil,
	/** 소금·흑후추·딜·고추. */
	Spice,
	Vegetable,
	/** 식용 버섯 6종 — 숲 3 · 늪 3 (원본에 없는 태그 🟡). */
	Mushroom
};

// 등급 4단 (DESIGN 8.12 "등급 색" — 원본 물고기 등급과 같은 넷). 배낭 정렬·칸 테두리 색에 쓴다.
UENUM(BlueprintType)
enum class EItemGrade : uint8
{
	Common,
	Rare,
	Epic,
	Legendary
};

// 배치형이 놓인 뒤 하는 일 (DESIGN 8.11 "가챠 목장 데코" 줄 + 8.9 배). `None` 은 앉거나 보기만 하는 가구.
UENUM(BlueprintType)
enum class EPlaceableFunction : uint8
{
	None,
	/** 제작 자리 (DESIGN 8.10 "작업대"). */
	Workbench,
	/** 요리 + 불 옆 회복 (모닥불·정원 화로). */
	Campfire,
	/** 요리만, 가장 빠르다. */
	Grill,
	/** 보관 칸. 칸 수는 `BonusSlots`. */
	Storage,
	/** 땅을 차지한다 (DESIGN 8.11). */
	Tent,
	/** 물 위에 띄운다 (DESIGN 8.9). */
	Boat,
	/** 덩어리를 주괴로 녹인다 — 걸리는 시간은 `CookSeconds`, 요리 타이머와 같은 길 (DESIGN 8.16). */
	Smelter,
	/** 집 벽 칸 변에 건다 — 바닥 발자국 대신 서버 `wall {side, h}` (DESIGN 8.11 「건축가 꾸미기」 벽 걸기, M108-02). */
	WallDecor,
	/** 받침 가구(`TopSlots` > 0) 윗면 슬롯에 올린다 — 바닥 발자국 대신 서버 `top {host, slot}` (DESIGN 8.11 위에 올리기, M108-03). */
	TopDecor,
	/** 바닥 가구 밑에 깐다 — 바닥 가구와 같은 칸을 나눠 쓰고 러그끼리만 겹침을 본다 (DESIGN 8.11 「러그 깔기」, M108-05b). */
	Rug,
	/** 비전기 불빛 — 밤이면 전력 없이 불꽃 점광이 켜진다, 땅 하나 불빛 상한 안에서 (DESIGN 8.11 「비전기 조명」, M108-05e). */
	FlameLight,
	/** 1층↔2층 계단 — 2층 집 안 한 줄(3×1)에만, 서버 `stairPlaceFail` 이 판정 (DESIGN 8.11 「계단·위층」, M108-06). */
	Stairs,
	/** 연료 발전기 — 서버 `FeedGenerator` 가 넣은 연료가 남은 동안 그 땅 공급에 `GeneratorPower` (DESIGN 8.11 「연료 발전기 숫자」, M108-07). */
	Generator
};

// 장비를 몸 어디에 붙이는지. 부위별 모듈형 레이어라 처음부터 부위 단위로 나눈다 (결정 006 · DESIGN 3.5) —
// 나중에 상의만 갈아입히려면 슬롯이 부위로 갈려 있어야 한다. 얼굴 장식·신발·장갑은 꾸미기 칸(`skin-parts.md`).
// **새 값은 맨 끝에만 붙인다** — 표에 저장된 순번이 밀리지 않게.
UENUM(BlueprintType)
enum class EEquipSlot : uint8
{
	/** 장비가 아닌 행. `EItemType::Equipment` 가 아닌 행은 전부 이 값이다. */
	None,
	/** 오른손에 드는 무기 (몽둥이). */
	Weapon,
	/** 채집 도구 (손도끼·바구니). */
	Tool,
	/** 상의. */
	OutfitTop,
	/** 하의. */
	OutfitBottom,
	/** 등에 메는 배낭 — 장착하면 배낭 칸이 `BonusSlots` 만큼 는다 (DESIGN 8.12). */
	Backpack,
	/** 머리 (어부 모자, DESIGN 8.13). */
	Head,
	/** 얼굴 장식 (고글·안경 — 꾸미기 칸, `skin-parts.md`). */
	FaceAccessory,
	/** 신발 (꾸미기 칸). */
	Shoes,
	/** 장갑 (꾸미기 칸). */
	Gloves
};

// 먹으면 걸리는 버프 종류 (DESIGN 13.6 「버프 규칙」). 값의 뜻은 종류마다 다르다 — 전부 비율(0.3 = 30%).
// 같은 종류는 새 것이 덮어쓰고, 다른 종류는 같이 간다. 새 값은 끝에만 붙인다(표에 저장된 순번이 밀리지 않게).
UENUM(BlueprintType)
enum class EBuffKind : uint8
{
	/** 버프 없는 행. */
	None,
	/** 걷기·달리기 속도 × (1 + 값). 탈것·배·수영은 빼고 (에너지 드링크). */
	Speed,
	/** 존 체력 감소(설원 추위) × (1 − 값) (커피 병·차 병·핫팩·따뜻한 탄산). */
	ColdResist,
	/** 수영 스태미나 소모 × (1 − 값) (블루베리잼). */
	SwimSave,
	/** 자연 체력 회복 × (1 + 값) (링곤베리잼). */
	Regen
};

// M18-11 손에 쥐는 자리 — 손 소켓 원점(손목)에 놓을 **메시 로컬 점**(cm, 배율 전)과 그때 메시가 도는 각(소켓 공간).
// 도구 메시 원점은 경계 상자 가운데라 0 이면 손목에 메시 한가운데가 붙는다(곡괭이 머리가 머리 옆에 묻혔다).
// 값은 DESIGN 3.1 「쥐는 자리」 표(메시 꼭짓점 실측) — 둘 다 0 인 행은 예전처럼 소켓 원점에 원점을 붙인다.
USTRUCT(BlueprintType)
struct FEquipGrip
{
	GENERATED_BODY()

	/** 손목에 올 메시 로컬 점(cm). 손잡이 끝에서 메시 길이 0.1 안쪽. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	FVector Location = FVector::ZeroVector;

	/** 소켓 공간에서 메시를 돌리는 각 — 긴 축을 손가락 쪽(손 뼈 +Z)에 둔다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	FRotator Rotation = FRotator::ZeroRotator;

	bool IsSet() const { return !Location.IsZero() || !Rotation.IsZero(); }
};

// ROADMAP M1-01: 아이템 한 종을 설명하는 데이터 한 줄 (DT_ItemTable 의 행 구조체).
// 스택 상한 같은 인벤토리 숫자는 여기 만들지 않는다 — 설계서에 없고, 필요한 조각(M1-04)에서 사람이 정한다
// (DataDriven.md 3 — 추천 숫자도 쓰지 않는다).
USTRUCT(BlueprintType)
struct FItemRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 세이브·백엔드가 쓰는 고정 ID. 행 이름 `Item_PigMeat` ↔ `item.pig_meat` (DataDriven.md 4). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FName ItemID;

	/** 화면에 보이는 이름. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	EItemType ItemType = EItemType::Material;

	/** 인벤토리 아이콘. 아트가 들어올 때까지 비어 있다 (비면 데이터 무결성 검사가 경고로 센다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TSoftObjectPtr<UTexture2D> IconRef;

	// --- 장비 칸 (ROADMAP M5-06a · DESIGN 3.5). 장비를 새 테이블로 빼지 않고 이 표에 열을 늘린다 —
	// 사냥·목장·채집이 이미 이 표 하나로 돈다. 장비가 아닌 행은 아래 셋이 전부 빈 값이다.

	/** 붙일 부위. `ItemType` 이 `Equipment`·`Cosmetic` 인 행만 `None` 이 아니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	EEquipSlot EquipSlot = EEquipSlot::None;

	/** **손에 드는** 부위(`Weapon`·`Tool`)가 소켓에 매다는 스태틱 메시. 옷 부위는 아래 칸을 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	TSoftObjectPtr<UStaticMesh> EquipMeshRef;

	/**
	 * **입는** 부위(`OutfitTop`·`OutfitBottom`·`Shoes`·`Gloves`)가 몸에 겹치는 스켈레탈 메시 (ROADMAP M9-02 · 결정 006).
	 * 옷은 소켓에 매다는 물건이 아니라 몸 위에 한 겹 더 덮는 것이라 종류부터 다르다 — 그래서
	 * `EquipMeshRef` 를 같이 쓰지 않고 칸을 갈랐다(한 칸으로 합치면 표에서 잘못된 종류를 골라도
	 * 저장되고, 붙일 때가 되어서야 캐스트가 실패한다). 손에 드는 행은 이 칸이 비어 있다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	TSoftObjectPtr<USkeletalMesh> EquipOutfitMeshRef;

	/** 붙일 스켈레톤 소켓 이름 (`Socket_Weapon`). 소켓 자체는 M5-06b 에서 스켈레톤에 만든다 — 지금은 이름만 적힌다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	FName EquipSocketName;

	/**
	 * 머리 부위(`Head`) 모자의 **월드 폭** cm — 메시 바닥 긴 변(X·Y 큰 쪽)을 이 값에 맞추고 높이는 메시 비율대로
	 * (M13-35 · DESIGN 3.5 「모자별 폭」). 0 이면 `UEquipmentComponent` 기본 폭(32). 머리가 아닌 행은 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float HatWidthCm = 0.f;

	/**
	 * 머리 부위 행이 **머리를 덮지 않는 장식**(왕관처럼 윗면이 열린 고리)인가 (DESIGN 3.5 「모자 쓸 때 머리카락」).
	 * 참이면 써도 머리카락을 그대로 둔다 — 숨기면 고리 안으로 맨살 정수리가 보인다(INBOX 결함 10-20 ①).
	 * 거짓(기본)이면 머리를 덮는 모자라 머리카락을 숨긴다(껍질이 모자 안쪽 틈보다 두꺼워 뚫린다). 머리가 아닌 행은 거짓.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	bool bHatKeepsHair = false;

	/**
	 * 목장 꾸미기(`PetCosmetic`) 메시 폭 = 펫 키 × 이 값 (DESIGN 10.9 「붙는 자리」). 동물마다 키가 달라 월드 cm
	 * (`HatWidthCm`) 하나로는 못 맞춘다. 붙는 뼈는 `EquipSocketName`(`head`·`neck`), 메시는 `EquipMeshRef`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float PetCosmeticWidthRatio = 0.f;

	/**
	 * 목 뼈(`neck`)에 거는 꾸미기의 바깥 폭 = 펫 목 굵기 × 이 값 (DESIGN 10.9 「가챠 펫 의상」). 0 이면 목걸이 기본(1.5).
	 * 털 목도리처럼 바깥으로 부푼 메시는 안쪽 지름 / 바깥 폭이 작아 더 크게 벌려야 목에 안 박힌다(황금 갈기 1.8).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float PetCollarWidthPerNeck = 0.f;

	/**
	 * 소켓에 매단 메시가 **몸 배율을 따르는가** (M13-43 · DESIGN 3.5 「도구 크기」 낚싯대·배낭 행).
	 * 참이면 메시 크기가 몸 리그(키 약 100cm) 기준 값이라 몸이 캡슐에 맞춰 커지는 만큼(`BodyMeshScale`) 같이 커진다.
	 * 거짓(기본)이면 월드 배율 1 — 손도끼·바구니·13.5 도구처럼 표 값이 곧 월드 cm 인 행.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	bool bEquipFollowsBodyScale = false;

	/** 손에 쥐는 자리(M18-11 · DESIGN 3.1 「쥐는 자리」). 오른손 `Socket_Weapon` 행 기준 각이다. 비면 메시 원점을 소켓에. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	FEquipGrip GripOffset;

	// --- 사거리·데미지 감쇠 칸 (ROADMAP M11-04 · DESIGN 8.6-b). 장비를 새 테이블로 빼지 않고 이 표에 열을
	// 늘리는 것은 위 장비 칸과 같은 이유다. **셋 다 0 이면 컴포넌트 기본값(8.6-b 의 근접 행)을 쓴다** —
	// 장비가 아닌 행은 물론이고, 근접 무기처럼 기본 곡선과 같은 행도 0 으로 두면 된다.
	// 8.6-b 가 정한 곡선은 **하나뿐**이라 무기마다 다른 수치를 적으라고 만든 칸이 아니다 —
	// 총기(MVP 제외, 8절 항목7)가 들어올 때 사거리를 늘리며 감쇠를 맞바꾸는 자리다.

	/** 이 무기를 들었을 때의 사거리(cm). 0 이면 컴포넌트 기본값. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackRangeCm = 0.f;

	/** 감쇠가 시작되는 사거리 비율 `s` (0~1). 1 이면 끝거리까지 데미지가 온전하다. 0 이면 컴포넌트 기본값. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float FalloffStartRatio = 0.f;

	/** 끝거리에서 남는 데미지 비율 `m` (0~1). 0 이면 컴포넌트 기본값. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float MinDamageRatio = 0.f;

	// --- 사냥 총 칸 (ROADMAP M39-01 · DESIGN 8.6-c). **`MagazineSize` 가 0 이면 총이 아니다** — 나머지 칸도
	// 전부 0/빈 값이 기본이라 총이 아닌 행은 안 적어도 된다. 탄약은 인벤토리라 재장전 때 CloudScript `LoadAmmo` 가 뺀다.

	/** 한 번 맞힐 때의 피해(감쇠 전). 0 이면 근접 기본 피해(3.1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackDamage = 0.f;

	/** 탄창에 드는 발 수. 0 이면 총이 아니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 MagazineSize = 0;

	/** 재장전에 걸리는 초 (그동안 쏘기 누름 무시). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float ReloadSeconds = 0.f;

	/** 재장전 동안 한 번 트는 몸 동작 (M39-06b2) — `ReloadSeconds` 한 바퀴에 맞춰 돌린다. 비면 든 자세 그대로. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TSoftObjectPtr<UAnimSequence> ReloadAnim;

	/** 한 발과 다음 발 사이 최소 초. 0 이면 근접 공격 간격(3.1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float FireIntervalSeconds = 0.f;

	/** 총알이 나는 속도(cm/s). 피해는 거리 ÷ 이 값 뒤에 들어간다 — 날아가는 총알이 눈에 보이게. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float ProjectileSpeedCmPerSecond = 0.f;

	/** 한 발에 나가는 펠릿(알) 수. 1 이면 한 알(권총·소총). 알마다 피해는 `AttackDamage / PelletCount` (DESIGN 8.6-c). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 PelletCount = 1;

	/** 펠릿이 흩어지는 반각(도) — 알들이 조준선 좌우 ±이 각 안에 고르게 퍼진다. 0 이면 한 줄. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float PelletSpreadHalfDegrees = 0.f;

	/** 폭발 반경(cm). 0 이면 폭발 없음. 총알이 박힌 자리를 중심으로 이 안의 사냥 동물이 맞는다 (DESIGN 8.6-c 폭발 피해 줄). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float SplashRadiusCm = 0.f;

	/** 폭발 반경 가장자리에서 남는 피해 비율(0~1). 중심은 1.0, 가장자리까지 선형으로 준다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float SplashEdgeRatio = 0.f;

	/** 재장전 때 가방에서 빼는 탄약 아이템 ID (`item.ammo`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	FName AmmoItemID;

	/** 쏠 때 손 진동 종류. 총 행은 `None` 이 아니어야 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	EShotHapticKind ShotHapticKind = EShotHapticKind::None;

	// --- 제작·상점·배치·음식 칸 (DESIGN 8.10~8.14). 위 장비 칸과 같은 이유로 새 표를 만들지 않는다.
	// **기본값은 전부 "그 기능이 없다"** 라서 해당 없는 행은 안 적어도 된다 (-1 미정 값을 쓰지 않는다).

	/** 레시피 재료 칸이 이 아이템을 묶음으로 받을 때의 이름 (DESIGN 8.10). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting")
	ECraftTag CraftTag = ECraftTag::None;

	/** 등급 (DESIGN 8.12). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	EItemGrade Grade = EItemGrade::Common;

	/** 잡화점에 팔 때 받는 코인 (DESIGN 8.14). **0 이면 못 판다.** 실제 지급은 CloudScript `SellItem` 이 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	int32 SellPrice = 0;

	/** 배치형이 차지하는 칸 수(1칸 100cm, DESIGN 8.11). 배치형이 아니거나 격자에 안 놓이는 배는 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	int32 FootprintX = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	int32 FootprintY = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	EPlaceableFunction PlaceableFunction = EPlaceableFunction::None;

	/** 놓였을 때 보이는 메시 (M13-37 · DESIGN 8.11 「가구·상자 진짜 메시 크기」). 비면 칸 크기 큐브 자리표시. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	TSoftObjectPtr<UStaticMesh> PlacedMeshRef;

	/**
	 * 놓인 메시의 **월드 폭** cm — 메시 수평 긴 변(X·Y 큰 쪽)을 이 값에 맞추는 한 배율, 높이는 메시 비율대로.
	 * 긴 변은 `FootprintX` 방향으로 눕힌다. 칸(100cm × 칸 수) 안의 값이다 — 겹침 판정은 계속 칸으로 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	float PlacedMeshWidthCm = 0.f;

	/**
	 * 받침 가구 윗면의 소품 슬롯 수 (DESIGN 8.11 「위에 올리기 받침·소품」 — 탁자 3 · 보관함 2). 서버 `TOP_HOSTS` 와 같은 값.
	 * 슬롯은 놓인 메시 긴 변을 같은 폭으로 나눈 가운데들이다. 받침이 아니면 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	int32 TopSlots = 0;

	/**
	 * 전기 가구가 켜져 있는 동안 쓰는 전력 (DESIGN 8.11 「🟡 전기 가구」 — 냉장고 40 · 네온 간판 25 · 선풍기 20 · 라디오 15). 전기 아니면 0.
	 * 행 기능과 따로다 — 라디오는 `TopDecor`, 간판은 `WallDecor` 이면서 전기. 전등은 규칙 칸 `LampItemID`·`LampPowerUse` 그대로.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	int32 PowerUse = 0;

	/** 켜졌을 때 바꿔 끼우는 메시 (M108-08b-2) — `PlacedMeshRef` 와 같은 꼴·크기에 불 칸만 밝은 팔레트 칸. 비면 켜져도 같은 메시. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	TSoftObjectPtr<UStaticMesh> PlacedOnMeshRef;

	/**
	 * 켜짐 모습일 때만 도는 따로 메시 (M108-08d-1, 선풍기 날개) — 몸에 붙여 `PlacedSpinPivotCm` 에 두고 자기 Y 축으로 돈다.
	 * 꺼지면 그 각에 멈춰 그대로 보인다. 비면 없음.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	TSoftObjectPtr<UStaticMesh> PlacedSpinMeshRef;

	/** 도는 메시를 둘 자리 — 꺼진 몸 메시 좌표 cm(배율 전, 몸과 같이 커지고 돈다). 도는 축은 이 점을 지난다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	FVector PlacedSpinPivotCm = FVector::ZeroVector;

	/** 도는 메시를 몸 X 축 둘레로 젖힌 각(도) — 양수면 앞(+Y)이 위를 본다. 선풍기 머리 젖힘과 같은 값(메시 원본 `FAN_TILT`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	float PlacedSpinTiltDeg = 0.f;

	/** 켜진 동안 초당 바퀴 (DESIGN 8.11 「🟡 전기 가구」 선풍기 날개 2). 0 이면 안 돈다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	float PlacedSpinTurnsPerSecond = 0.f;

	/**
	 * 켜짐 모습인 동안 몸에 붙여 트는 되풀이 소리 (M108-08d-2, 라디오 가락 `Radio_Loop` → `SFX_Radio_Loop_01`). 꺼지면 끊는다. 비면 없음.
	 * 감쇠·음량은 아래 세 칸 (DESIGN 8.11 「🟡 전기 가구」 라디오 200 / 900cm · 0.35).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	FName PlacedOnLoopCue;

	/** 켜짐 고리 음량 (곱하기, 효과음 크기 위에). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	float PlacedOnLoopVolume = 0.f;

	/** 켜짐 고리 감쇠 — 이 반지름 안은 크기 그대로(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	float PlacedOnLoopInnerCm = 0.f;

	/** 켜짐 고리 감쇠 — 이 반지름에서 0(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	float PlacedOnLoopOuterCm = 0.f;

	/**
	 * 불 하나가 요리 1개를 굽는 데 걸리는 초 (DESIGN 8.10 "요리 시간" — 모닥불 20 · 화로 12 · 그릴 6).
	 * 레시피가 아니라 **불**에 둔다 — 같은 구운 고기도 어느 불에 올리느냐로 시간이 갈린다. 불이 아니면 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	int32 CookSeconds = 0;

	/** 먹었을 때 차는 체력 (DESIGN 8.10 · 13.3 날로 먹기). 못 먹으면 0, 음수는 독(메제레움 −40). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Food")
	int32 HealAmount = 0;

	/** 물가에서 채우면 바뀌는 아이템 — 빈 병 → 물 병 (DESIGN 13.6). 못 채우면 None. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Food")
	FName FilledItemID;

	/** 먹으면 걸리는 버프 (DESIGN 13.6). None 이면 없다. 호스트 몸 상태라 서버가 아니라 `UPlayerVitalsComponent` 가 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Food")
	EBuffKind BuffKind = EBuffKind::None;

	/** 버프 크기 — 비율(0.3 = 30%). 뜻은 `EBuffKind` 주석. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Food")
	float BuffValue = 0.f;

	/** 버프 지속 초 (13.6 — 전부 180). 0 이면 버프 없음. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Food")
	float BuffSeconds = 0.f;

	/** 장착·설치하면 생기는 칸 수 — 하이킹 배낭 +8(8.12) · 보관함 20(8.12) · 보트 컨테이너 8(8.9). 없으면 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	int32 BonusSlots = 0;

	/** 입질 대기를 나누는 낚시 속도 배율 (DESIGN 8.13 — 낚싯대·어부 모자). 0 이면 낚시와 상관없다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	float FishingSpeedMul = 0.f;

	/** 달리기 속도 배율 (DESIGN 13.5 달리기 모자). 0 이면 상관없다. 호스트 몸 상태라 서버가 아니라 캐릭터가 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float RunSpeedMul = 0.f;

	/** 막타 때 최상급(`pristine_`) 드롭 확률에 곱하는 배율 (DESIGN 13.5 사냥 모자 ×1.3). 0 이면 상관없다. 호스트가 굴린다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float PristineChanceMul = 0.f;

	/** 쓰고 있는 동안 초당 체력 회복 (DESIGN 13.5 딸기 모자 +1 / 10초 = 0.1). 0 이면 없다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float HealthRegenPerSecond = 0.f;

	/** 쓰고 있는 동안 머리 앞에 켜는 불빛 밝기 cd (DESIGN 8.16 채굴 모자). 0 이면 불빛이 없다. 화면 일이라 기계마다 스스로 켠다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float HeadLightIntensityCd = 0.f;

	/** 머리 불빛이 닿는 반경 cm (DESIGN 8.16 채굴 모자). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float HeadLightRadiusCm = 0.f;

	/** 입고 있는 동안 수영 스태미나 소모 배율 (DESIGN 13.8 구명조끼 ×0.5). 0 이면 상관없다. 수영에만 건다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float SwimStaminaMul = 0.f;

	/** 도구 칸에 들면 카메라 거리 배율 (DESIGN 13.5 망원경 ×1.3). 0 이면 상관없다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float CameraDistanceMul = 0.f;

	/** 도구 칸에 들면 미니맵 아래에 내 좌표가 뜬다 (DESIGN 13.5 위성 통신기). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	bool bShowsMapCoordinates = false;

	/** 도구를 **쓸 때** 트는 몸 동작 (M16-01 · DESIGN 8.8). `None` 이면 쓰는 동작이 없다(쌍안경 등). 코드는 도구 이름을 가르지 않고 이 칸만 읽는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	EItemAction UseAction = EItemAction::None;

	/** 이 무기·도구를 **들고 있는 동안** 되풀이하는 몸 자세 (M18-11). 걸을 땐 상체에만 얹힌다. 비면 맨손 대기·이동 그대로(바구니). 쓰기 클립 없이 이것만 있는 도구(낚싯대)는 무기 든 자세를 덮는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	TSoftObjectPtr<UAnimSequence> HoldPoseAnim;

	/** 이 무기·도구로 **휘두르거나 쓸 때** 한 번 트는 몸 동작 (M18-11). 타격 장이 길이의 1/3 이 되게 자른 클립(DESIGN 3.1). 비면 공용 공격·도구 동작. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	TSoftObjectPtr<UAnimSequence> UseAnim;

	/**
	 * 켜면 한 번 누름에 `UseAnim` 을 재생 속도 1 로 끝까지 틀고, 다음 누름까지의 간격도 그 클립 길이다
	 * (전기톱·고급/전기 곡괭이 — 내밀어 떨다 돌아오기, 사용자 2026-10-04 「한 번 클릭 + 딜레이」·레퍼런스 시트 약 1.5초).
	 * 꺼져 있으면 클립을 공격 간격 한 바퀴에 맞춰 돌린다. 길이 숫자는 클립에서 읽는다(DataDriven.md 3).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	bool bUseAnimFixedLength = false;

	/**
	 * 쓰기 버튼을 **누르고 있는 동안** 되풀이할 `UseAnim` 구간(초) — 끝에 닿으면 시작으로 감고, 떼면 끝에서 이어 틀어
	 * 든 자세로 돌아온다(전기톱 — 내민 채 떨며 버팀, 감독 보정 2026-10-06 02:15). 끝 ≤ 시작이면 되풀이 없음(한 번 끝까지).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float UseHoldStartSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	float UseHoldEndSeconds = 0.f;

	/** 도구를 쓸 때 나는 소리 이름 (`SFX_<이름>` 의 이름, 예 `Chop`). 비면 소리 없음 (M16-01). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	FName UseSoundCue;

	/** 낚싯대 입질 소리 이름 (`SFX_<이름>`, M16-06). 비면 소리 없음. 낚싯대 종류마다 다른 값을 준다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	FName BiteSoundCue;

	/** 낚싯대 릴 감는(연타) 소리 이름 (`SFX_<이름>`, M16-06). 비면 소리 없음. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	FName ReelSoundCue;

	/** 도구를 **든 동안** 되풀이하는 소리 이름 (전기톱 공회전 등, M16-03 부터 쓴다). 비면 없음 (M16-01). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	FName HoldLoopSoundCue;

	/** 켜면 장착하지 않고 배낭에 **가지고만 있어도** 이 행의 효과(좌표 등)가 켜진다 (DESIGN 13.5 위성 통신기, 레퍼런스 방식). 배낭 화면은 `장착` 대신 `소지 중` 을 보인다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	bool bPassiveWhileCarried = false;

	/** 페인트면 칠한 가구 색 `RRGGBB` (DESIGN 13.8). 비면 페인트가 아니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeable")
	FString PaintColorHex;

	/**
	 * 목장 데코 종류 칸 (DESIGN 10.9b) — `fence`·`path`·`barn`·`sky`. 같은 종류는 한 칸이다(서버 `RANCH_DECO_KINDS`).
	 * 바꿀 액터는 `L_Ranch` 의 `RanchDeco.<종류>` 태그로 찾는다(`setup_ranch_buildings.py` 가 붙인다). 데코가 아니면 비어 있다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RanchDeco")
	FName RanchDecoKind;

	/** 데코를 적용하면 태그 액터에 씌울 메시. 비면 메시는 그대로 두고 재질만 바꾼다(흙길·돌담). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RanchDeco")
	TSoftObjectPtr<UStaticMesh> RanchDecoMeshRef;

	/** 데코를 적용하면 태그 액터 슬롯에 씌울 재질. 비면 재질은 그대로(메시 원래 슬롯 재질). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RanchDeco")
	TSoftObjectPtr<UMaterialInterface> RanchDecoMaterialRef;

	/** `RanchDecoMaterialRef` 를 씌울 슬롯 이름(헛간 지붕 `RoofBlack`). 비면 모든 슬롯. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RanchDeco")
	FName RanchDecoMaterialSlot;

	/** `sky` 데코 — 해(`RanchSun`) 세기 배수. 0 이면 빛을 안 바꾼다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RanchDeco")
	float RanchDecoSunScale = 0.f;

	/** `sky` 데코 — 하늘빛(`RanchSky`) 세기 배수. 0 이면 안 바꾼다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RanchDeco")
	float RanchDecoSkyScale = 0.f;

	/** `sky` 데코 — 해 빛 색. `RanchDecoSunScale` 가 0 이면 쓰지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RanchDeco")
	FLinearColor RanchDecoSunColor = FLinearColor::White;
};
