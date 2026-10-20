#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "Building/SnareBuildRules.h"
#include "Crafting/SnareCraftRules.h"
#include "SnareBaseSubsystem.generated.h"

class UDataTable;
class USnareBackendSubsystem;
struct FBaseRuleRow;
struct FItemRow;

/** 땅 위에 놓인 가구 하나 — 서버 `base.tents[].items[]` 의 사본 (이번 실행에서 받은 응답으로만 채운다). */
USTRUCT(BlueprintType)
struct FSnareStructureMirror
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FString StructureId;

	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FName ItemID;

	/** 앵커 칸 (서버 `x`,`y`). */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FIntPoint Cell = FIntPoint::ZeroValue;

	/** 세밀 격자 반 칸 오프셋 (서버 `hx`,`hy`, 0 또는 1 — 50cm). 옛 가구는 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FIntPoint Half = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "Base")
	int32 Rotation = 0;

	/** 벽 장식이면 걸린 집 변 (서버 `wall.side` 0=+Y 1=+X 2=-Y 3=-X), 바닥 가구면 -1. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	int32 WallSide = -1;

	/** 벽 장식 높이 cm (서버 `wall.h`). 바닥 가구면 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	int32 WallHeightCm = 0;

	/** 소품이면 올라선 받침 가구의 `StructureId` (서버 `top.host`), 아니면 빈 글자. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FString TopHostId;

	/** 소품이 올라선 받침 슬롯 (서버 `top.slot`), 아니면 -1. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	int32 TopSlot = -1;

	/** 놓인 층 (서버 `floor`, 없으면 1) — 2층 집 2층 바닥 가구·러그와 그 받침 위 소품은 2 (M108-06f). */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	int32 Floor = 1;

	/** 보관함 속 — 서버 `MoveToStorage`/`MoveFromStorage` 응답의 `storage` 를 그대로 옮긴다. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	TMap<FName, int32> Storage;

	/** 칠한 페인트 아이템 ID — 서버 가구 줄 `color` (DESIGN 13.8). None 이면 안 칠함. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FName Color;

	/** 발전기 연료가 끝나는 시각 — 서버 가구 줄 `fuelUntilUtc` 글자 그대로 (M108-07). 발전기가 아니거나 넣은 적 없으면 빈 글자. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FString FuelUntilUtc;
};

/** 텐트(또는 집) 하나 — 서버 `base.tents[]` 의 사본. 시각 글자는 **서버가 준 글자 그대로** 둔다 (DESIGN 8.11 판정). */
USTRUCT(BlueprintType)
struct FSnareTentMirror
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FString TentId;

	/** 텐트가 선 존 — 서버 `zone` 글자 그대로 (`SnareBaseZones`). 칸 좌표는 존마다 다른 월드라 겹침·세우기를 존끼리만 본다. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FName Zone = FName(TEXT("ranch"));

	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FIntPoint Cell = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FString PlacedAtUtc;

	/** 이 시각까지 집으로 못 올리면 서버가 텐트를 지운다. `PlaceTent` 응답의 `upgradeDeadlineUtc`. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FString UpgradeDeadlineUtc;

	UPROPERTY(BlueprintReadOnly, Category = "Base")
	bool bUpgraded = false;

	UPROPERTY(BlueprintReadOnly, Category = "Base")
	int32 Color = 0;

	/** 집 유지가 끝나는 시각. `UpgradeHouse`/`FeedUpkeep` 응답의 `upkeepUntilUtc`. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FString UpkeepUntilUtc;

	/** 집 층수 — 서버 `floors`(없으면 1). `UpgradeHouse {level: 2}` 응답 뒤에만 2가 된다 (DESIGN 8.11 「집 2층·전력」). */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	int32 Floors = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Base")
	TArray<FSnareStructureMirror> Structures;

	/** 한도에 드는 가구 몫 — 벽 장식(`WallSide`)·소품(`TopHostId`)은 0.5 (서버 `furnitureLoad`). */
	float GetFurnitureLoad() const
	{
		const int32 HungOrTop = Structures.FilterByPredicate([](const FSnareStructureMirror& Structure)
		{
			return Structure.WallSide >= 0 || !Structure.TopHostId.IsEmpty();
		}).Num();
		return SnareBuildRules::FurnitureLoad(Structures.Num() - HungOrTop, HungOrTop);
	}
};

/** 불 하나 위에서 도는 요리 (DESIGN 8.10 "한 번에 1개, 불 옆을 떠나도 계속"). */
USTRUCT(BlueprintType)
struct FSnareCookJob
{
	GENERATED_BODY()

	/** 어느 불인가 — 놓인 불의 `StructureId`. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FString FireKey;

	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FName FireItemID;

	UPROPERTY(BlueprintReadOnly, Category = "Base")
	FName ResultItemID;

	/** 불 행의 `CookSeconds` 그대로. */
	UPROPERTY(BlueprintReadOnly, Category = "Base")
	float DurationSeconds = 0.f;

	/** 게임 인스턴스 기준 시작 시각 (`FPlatformTime::Seconds`). 남은 시간 표시용이다. */
	double StartedAtSeconds = 0.0;

	FTimerHandle TimerHandle;
};

DECLARE_MULTICAST_DELEGATE(FOnSnareBaseChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSnareBaseMessage, const FText& /*Message*/, bool /*bError*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSnareStructurePlaced, const FSnareTentMirror& /*Tent*/, const FSnareStructureMirror& /*Structure*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSnareStructureRemoved, const FString& /*StructureId*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSnareTentChanged, const FSnareTentMirror& /*Tent*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSnareCraftConfirmed, FName /*ResultItemID*/, ESnareCraftPlace /*Place*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSnareBuildConfirmed, const FString& /*TentId*/, const FString& /*StructureId*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnSnareStructureEditConfirmed, const FString& /*TentId*/, const FString& /*StructureId*/, ESnareStructureEdit /*Edit*/);

// DESIGN 8.10 제작·요리 · 8.11 건축·거점의 **서버 왕복 창구**.
//
// 판정은 전부 CloudScript 다 (CLAUDE.md 8). 여기는 ① `USnareBackendSubsystem::CallServerFunction` 으로 묻고
// ② 성공이면 `FetchPlayerState()` 로 가방을 다시 받고 ③ 응답에 실린 텐트·가구·보관함 값을 **글자 그대로** 사본에 적는다.
// 응답 전에는 가방도 사본도 안 건드린다 — 2층 `Snarehaven.Base.Server.NothingBeforeResponse` 가 잰다.
//
// 게임 인스턴스에 두는 이유: 사본이 맵을 건너(사냥존 ↔ 거점존) 살아 있어야 거점에 돌아왔을 때 다시 세운다.
// 새 실행(재시작)에서는 서버 `GetBaseState` 의 `tents` 를 `ApplyBackendTents` 로 받아 사본을 다시 채운다.
// 서버 `PlaceTent` 의 `zone` 값 (`TENT_ZONES`). 레퍼런스처럼 사냥 섬(hunt) 빈 땅 어디든 캠프를 친다 (DESIGN 8.11).
namespace SnareBaseZones
{
	inline const FName Ranch(TEXT("ranch"));
	inline const FName Hunt(TEXT("hunt"));
}

UCLASS()
class SNAREHAVEN_API USnareBaseSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	USnareBaseSubsystem();

	virtual void Deinitialize() override;

	// ── 서버에 묻기 (전부 비동기, 응답 전엔 아무것도 안 바뀐다). 이미 하나가 날아가 있으면 false 이고 ──
	// ── 기다리는 중이라는 안내 한 줄이 뜬다 (`MessageForBusy`). ──

	/**
	 * `CraftItem {recipeId, times}` — 서버 `RECIPES` 는 결과 아이템 ID 로 열린다.
	 * @param bTellWhenBusy 앞 요청을 기다리는 중이면 「만드는 중입니다」를 띄울지. 사람이 누른 길만 true —
	 *        요리 타이머의 자동 재시도(`CompleteCooking`)까지 말하면 누르지도 않은 안내가 뜬다.
	 * @param Place 어디서 만들었나 — 서버가 확정하면 `OnCraftConfirmed` 에 실려 그 자리 소리를 고른다 (M28-02).
	 */
	bool RequestCraft(FName ResultItemID, int32 Times = 1, bool bTellWhenBusy = true,
		ESnareCraftPlace Place = ESnareCraftPlace::Backpack);
	/** @param Zone 텐트를 칠 존 (`SnareBaseZones`). */
	bool RequestPlaceTent(const FIntPoint& Cell, FName Zone = SnareBaseZones::Ranch);
	/**
	 * @param Pin 보관함만, 5자리. 비면 안 보낸다 (주인만 연다).
	 * @param WallSide 0~3 이면 벽 장식으로 `wall {side, h}` 를 보낸다 (`WallHeightCm` 높이). -1 이면 바닥 가구.
	 * @param TopHostId 비지 않으면 소품으로 `top {host, slot}` 을 보낸다 (`TopSlot` 슬롯). 자리·돌림은 서버가 받침 것으로 저장한다.
	 * @param Floor 2 면 2층 바닥으로 `floor: 2` 를 보낸다 (M108-06f). 소품은 받침 층을 따르므로 안 보낸다.
	 */
	bool RequestPlaceStructure(const FString& TentId, FName ItemID, const FIntPoint& Cell, int32 Rotation, const FString& Pin,
		const FIntPoint& Half = FIntPoint::ZeroValue, int32 WallSide = -1, int32 WallHeightCm = 0,
		const FString& TopHostId = FString(), int32 TopSlot = -1, int32 Floor = 1);
	bool RequestRemoveStructure(const FString& TentId, const FString& StructureId);
	/**
	 * `UpgradeHouse {tentId, color}` — 텐트 → 집 (1층). `Level` 2 면 `{tentId, level: 2}` — 집 → 2층 (DESIGN 8.11 「집 2층·전력」),
	 * 색은 안 보낸다. 사본의 `bUpgraded`·`Floors` 는 응답 뒤에만 바뀐다.
	 */
	bool RequestUpgradeHouse(const FString& TentId, int32 Color, int32 Level = 1);
	/** `PaintStructure` — 페인트 1통으로 내 가구 하나를 칠한다 (DESIGN 13.8). 응답 뒤 `OnStructurePlaced` 로 다시 알린다. */
	bool RequestPaintStructure(const FString& TentId, const FString& StructureId, FName PaintItemID);
	bool RequestFeedUpkeep(const FString& TentId, int32 Count);
	/**
	 * `FeedGenerator {tentId, structureId, fuel, count}` — 내 발전기에 나무·석탄 덩어리를 넣는다 (M108-07c-2). 차감·끝 시각은 서버 몫,
	 * 응답 `fuelUntilUtc`·`nowUtc` 뒤에만 사본을 고치고 `OnStructurePlaced` 로 다시 알린다 — 놓인 액터 연료가 그 값으로 바뀌고
	 * 게스트면 호스트에게 다시 공유된다 (CLAUDE.md 8).
	 */
	bool RequestFeedGenerator(const FString& TentId, const FString& StructureId, FName FuelItemID, int32 Count);
	/** @param OwnerId 남의 보관함이면 주인 PlayFabId (비면 내 것). */
	bool RequestMoveToStorage(const FString& TentId, const FString& StructureId, FName ItemID, int32 Count,
		const FString& OwnerId, const FString& Pin);
	bool RequestMoveFromStorage(const FString& TentId, const FString& StructureId, FName ItemID, int32 Count,
		const FString& OwnerId, const FString& Pin);

	// ── 배 보관함 (DESIGN 8.9·8.12 보트 컨테이너 — 계정당 하나, 주인만) ──

	/** `MoveToStorage {boat: true, itemId, count}`. 사본은 응답 `storage` 뒤에만 바뀐다 (CLAUDE.md 8). */
	bool RequestMoveToBoatStorage(FName ItemID, int32 Count);
	/** `MoveFromStorage {boat: true, itemId, count}`. */
	bool RequestMoveFromBoatStorage(FName ItemID, int32 Count);
	/** 배 보관함 속 — 서버 응답·`GetBaseState.boatStorage` 그대로. */
	const TMap<FName, int32>& GetBoatStorage() const { return BoatStorage; }
	/** 서버에서 배 보관함 속을 한 번이라도 받았나 (못 받았으면 화면이 「넣거나 꺼내면 보인다」 고 쓴다). */
	bool HasBoatStorageMirror() const { return bBoatStorageKnown; }
	/** `GetBaseState.boatStorage` 객체(아이템 → 개수)로 사본을 갈아 끼운다. null 이면 그대로. */
	void ApplyBackendBoatStorage(const TSharedPtr<FJsonObject>& StorageObject);
	/** 배 보관함 칸 수 — 표 `item.boat_container` 의 `BonusSlots` (서버 `BOAT_CONTAINER_SLOTS`). 표가 없으면 0. */
	int32 GetBoatStorageSlots() const;
	/** 배 보관함을 여는 아이템 ID (서버 `BOAT_CONTAINER_ITEM`). */
	static FName GetBoatContainerItemID();

	bool IsBusy() const { return bRequestInFlight; }

	// ── 요리 (DESIGN 8.10) ──

	/** 불 아이템의 요리 시간 — `DT_ItemTable.CookSeconds`. 불이 아니거나 표가 없으면 -1. */
	float GetCookSeconds(FName FireItemID) const;

	/**
	 * 불 위에 요리를 올린다. 타이머는 재화가 아니라 클라이언트가 돌리고, 끝나면 `CraftItem` 을 부른다 —
	 * 재료 차감·지급은 그때 서버가 한다 (DESIGN 8.10 판정 줄).
	 * @return 올렸으면 true (이미 그 불이 요리 중이거나 시간이 미정이면 false)
	 */
	bool StartCooking(const FString& FireKey, FName FireItemID, FName ResultItemID);

	const FSnareCookJob* FindCookJob(const FString& FireKey) const;

	/** 남은 초 (요리 중이 아니면 -1). */
	float GetCookRemainingSeconds(const FString& FireKey) const;

	/** 타이머가 끝났을 때 — 서버에 굽기를 묻는다. 2층 검사가 시간을 기다리지 않고 직접 부른다. */
	void CompleteCooking(const FString& FireKey);

	// ── 사본 읽기 ──

	const TArray<FSnareTentMirror>& GetTents() const { return Tents; }
	const FSnareTentMirror* FindTent(const FString& TentId) const;
	/** 이 존의 이 칸이 내 어느 텐트 땅 안인가 (없으면 nullptr). */
	const FSnareTentMirror* FindTentOwningCell(const FIntPoint& Cell, FName Zone = SnareBaseZones::Ranch) const;
	const FSnareStructureMirror* FindStructure(const FString& StructureId, FString* OutTentId = nullptr) const;
	/**
	 * 내 땅의 가구인가 — 서버가 준 내 텐트(시각 있음)에 그 가구 줄이 있다. 칠할 대상 고르기(DESIGN 13.8)가 쓴다.
	 * 남의 가구·남의 보관함 사본(시각 없음)·텐트 몸(가구 ID 없음)은 false — 서버도 `not_found` 로 막는다.
	 */
	bool IsOwnFurniture(const FString& TentId, const FString& StructureId) const;

	/**
	 * 미리보기 판정 (초록·파랑·빨강). 텐트면 내 다른 텐트 땅과, 가구면 그 칸을 품은 텐트의 땅·겹침·40개 한도로 본다.
	 * 겹침은 같은 층 가구끼리 (서버 「층이 다르면 안 겹침」). `Floor` 2 면 서버 `floor2PlaceFail` 과 같은 줄 — 2층 집·계단 있음·
	 * 발자국 전부 집 안 3×3·계단 구멍 칸 아님이 아니면 파랑, 텐트 몸은 1층이라 안 센다 (M108-06f).
	 * 판정은 여전히 서버다 — 이 색은 누르기 전 안내다.
	 */
	ESnarePlacementVerdict EvaluatePlacement(FName ItemID, const FIntPoint& Cell, int32 Rotation, bool bInBaseZone,
		FName Zone = SnareBaseZones::Ranch, const FIntPoint& Half = FIntPoint::ZeroValue, int32 Floor = 1) const;

	/**
	 * 벽 장식 미리보기 판정 (M108-02) — 그 텐트가 집이고, 벽 칸 변이고, 같은 자리 높이가 비었나 (`SnareBuildRules::EvaluateWallHang`).
	 * 텐트가 없거나 가구 한도면 파랑. 판정은 여전히 서버(`PlaceStructure` 의 `wall`)다.
	 */
	ESnarePlacementVerdict EvaluateWallPlacement(const FString& TentId, const FIntPoint& Cell, int32 Side, int32 HeightCm,
		int32 HouseCells) const;

	/** 2층 검사 문 — 서버 응답 없이 사본에 텐트를 넣는다 (판정 검사용, 가방은 안 건드린다). */
	/**
	 * 소품 미리보기 판정 (M108-03) — 그 텐트에 받침 `HostId` 가 있고 슬롯이 비었나 (`SnareBuildRules::EvaluateTopPlace`).
	 * 텐트·받침이 없거나 가구 한도면 파랑. 판정은 여전히 서버(`PlaceStructure` 의 `top`)다.
	 */
	ESnarePlacementVerdict EvaluateTopPlacement(const FString& TentId, const FString& HostId, int32 Slot, int32 SlotCount) const;

	void AddTentMirrorForTest(const FSnareTentMirror& Tent) { Tents.Add(Tent); }

	/**
	 * 서버 `GetBaseState` 의 `tents` 배열로 사본을 통째로 갈아 끼운다 (재시작 복원 — 서버가 원본).
	 * 텐트·가구마다 `OnTentChanged`·`OnStructurePlaced` 를 울려 거점 맵이 다시 세우게 한다.
	 */
	void ApplyBackendTents(const TArray<TSharedPtr<FJsonValue>>& TentValues);

	/**
	 * 서버 시계 (M108-07c) — `GetBaseState`·`FeedGenerator` 응답의 `nowUtc` 를 받은 순간과 함께 둔다. 연료 남은 시간을
	 * 기기 시계가 아니라 서버 시계로 재려는 것(기기 시계가 몇 분 틀려도 같은 답). 빈 글자·못 읽는 글자면 기기 UTC 로 대신한다.
	 */
	void ApplyBackendNow(const FString& NowUtc);

	/** 서버 `fuelUntilUtc` 까지 지금 남은 초 (서버 시계 기준, 0 이상). 빈 글자·못 읽는 글자면 0. */
	double FuelSecondsLeft(const FString& FuelUntilUtc) const;

	/** 규칙 행 (`DT_BaseRuleTable` 의 `Base`). 표가 없으면 nullptr — 쓰는 쪽은 아무것도 켜지 않는다. */
	const FBaseRuleRow* GetRules() const;
	const FItemRow* FindItemRow(FName ItemID) const;
	UDataTable* GetItemTable() const { return ItemTable; }
	UDataTable* GetRecipeTable() const { return RecipeTable; }

	/** 마지막 안내 글 (토스트). 2층 검사가 오류 매핑을 볼 때 읽는다. */
	const FText& GetLastMessage() const { return LastMessage; }

	/** 서버 오류 코드 → 화면 글. 서버 글에 글꼴 밖 글자(`·`)가 있어 코드로 고른다. */
	static FText MessageForError(const FString& FunctionName, const TSharedPtr<FJsonObject>& Response);

	/**
	 * 앞 요청을 기다리는 중에 또 눌렀을 때의 안내 글 (INBOX 2026-09-25 ④).
	 * 말이 없으면 "안 눌렸나" 하고 계속 누르게 된다 — 누른 사람에게 무엇을 기다리는지 알린다.
	 */
	static FText MessageForBusy(const FString& FunctionName);

	// ── 2층 검사 문 ──

	void SetBackendForTest(USnareBackendSubsystem* Backend) { BackendOverride = Backend; }
	void SetTablesForTest(UDataTable* InItemTable, UDataTable* InRecipeTable, UDataTable* InBaseRuleTable);
	/** 보관함·유지 확정 소리를 낸 횟수와 마지막 소리 이름 (M45-03) — 「성공 뒤 1 · 거절 0」 을 세는 문. */
	int32 GetStorageSoundCount() const { return StorageSoundCount; }
	FName GetLastStorageSoundCue() const { return LastStorageSoundCue; }

	FOnSnareBaseChanged OnBaseChanged;
	FOnSnareBaseMessage OnBaseMessage;
	FOnSnareStructurePlaced OnStructurePlaced;
	FOnSnareStructureRemoved OnStructureRemoved;
	FOnSnareTentChanged OnTentChanged;
	/** 서버가 `CraftItem` 을 확정한 뒤에만 한 번 (요리·제련 타이머 끝 포함). 거절·왕복 실패면 안 나간다. */
	FOnSnareCraftConfirmed OnCraftConfirmed;
	/**
	 * 서버가 텐트·가구 놓기(`PlaceTent`·`PlaceStructure`)·집 올리기(`UpgradeHouse`)를 확정한 뒤에만 한 번 (M28-03).
	 * 스냅샷 되살리기·칠하기·유지 넣기로 다시 세울 때는 안 나간다. 텐트·집이면 `StructureId` 가 비어 있다.
	 */
	FOnSnareBuildConfirmed OnBuildConfirmed;
	/**
	 * 서버가 철거(`RemoveStructure`)·칠하기(`PaintStructure`)를 확정한 뒤에만 한 번 (M45-02). 거절·왕복 실패면 안 나간다.
	 * 철거는 사본에서 지우고 `OnStructureRemoved` 를 보내기 **전에** 나간다 — 듣는 쪽이 아직 선 물건 자리를 찾게.
	 */
	FOnSnareStructureEditConfirmed OnStructureEditConfirmed;

private:
	/** 마지막으로 받은 서버 시각과 그때 이 기기의 단조 시계(`FPlatformTime::Seconds`). 시각이 없으면 기기 UTC. */
	FDateTime BackendNowUtc;
	double BackendNowReceivedAt = -1.0;

	using FSuccessHandler = TFunction<void(const TSharedPtr<FJsonObject>& Response)>;

	/**
	 * 한 번의 왕복. 성공이면 `OnSuccess` → `FetchPlayerState` → 알림, 실패면 안내 글만.
	 * @param bTellWhenBusy 앞 요청이 아직 날아가 있을 때 안내 글을 띄울지 (사람이 누른 길만 true).
	 */
	bool Call(const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, FSuccessHandler OnSuccess,
		bool bTellWhenBusy = true, const FString& MessageName = FString());
	bool CallBoatStorage(const FString& FunctionName, FName ItemID, int32 Count);

	USnareBackendSubsystem* GetBackend() const;
	FSnareTentMirror* FindTentMutable(const FString& TentId);
	void PostMessage(const FText& Message, bool bError);
	void ApplyStorageResponse(const FString& TentId, const FString& StructureId, const TSharedPtr<FJsonObject>& Response);
	/** 보관함 넣기·빼기(집·배)·유지 나무 넣기를 서버가 받아 준 뒤 내 귀에만 한 번 — 성공 람다에서만 부른다 (M45-03). */
	void PlayStorageConfirmSound();

	UPROPERTY(Transient)
	TObjectPtr<USnareBackendSubsystem> BackendOverride;

	UPROPERTY()
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY()
	TObjectPtr<UDataTable> RecipeTable;

	UPROPERTY()
	TObjectPtr<UDataTable> BaseRuleTable;

	UPROPERTY(Transient)
	TArray<FSnareTentMirror> Tents;

	UPROPERTY(Transient)
	TArray<FSnareCookJob> CookJobs;

	UPROPERTY(Transient)
	TMap<FName, int32> BoatStorage;

	bool bBoatStorageKnown = false;

	FText LastMessage;

	bool bRequestInFlight = false;

	int32 StorageSoundCount = 0;
	FName LastStorageSoundCue;
};
