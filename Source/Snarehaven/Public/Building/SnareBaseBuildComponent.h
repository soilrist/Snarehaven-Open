#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Building/SnareBuildRules.h"
#include "Crafting/SnareCraftRules.h"
#include "SnareBaseBuildComponent.generated.h"

class ASnarePlacedStructureActor;
enum class EPlaceableFunction : uint8;
class UBuildHudWidget;
class UCraftingScreenWidget;
class USnareBaseSubsystem;
struct FSnareStructureMirror;
struct FSnareTentMirror;

DECLARE_MULTICAST_DELEGATE(FOnSnarePlacementChanged);

// DESIGN 8.10·8.11 을 캐릭터에 붙이는 자리: ① 놓인 것 앞에서 상호작용 버튼 → 제작·요리·보관함 화면
// ② 놓기 모드(미리보기 색·90도 회전·확정) ③ 호스트가 서버 성공 응답대로 복제 액터를 세우고 치우기
// ④ 모닥불·화로 곁 체력 회복.
//
// 판정은 하지 않는다 — 요청은 전부 `USnareBaseSubsystem` 을 거쳐 서버로 간다 (CLAUDE.md 8).
// 놓기는 **내가 조종하는 몸 + 지을 수 있는 땅**에서 켜진다 — 게스트도 자기 계정으로 요청을 보내고,
// 서버가 확정한 것만 화면에 선다. 복제는 권위가 있는 쪽만 하므로 게스트 것은 자기 화면에만 보인다 (M15-14, DESIGN 8.11 멀티).
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API USnareBaseBuildComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USnareBaseBuildComponent();

	/**
	 * 상호작용 버튼 — 사거리 안 가장 가까운 놓인 것에 맞는 화면을 연다.
	 * @return 무언가를 열었으면 true (캐릭터가 공격으로 넘어가지 않는다)
	 */
	UFUNCTION(BlueprintCallable, Category = "Base")
	bool TryInteractNearest();

	/** `TryInteractNearest` 가 열 것이 있는지 — 조회만 한다 (사냥 HUD 버튼 글자 「열기」, INBOX 18:10 ①). */
	bool HasInteractTarget() const;

	/** `TryInteractNearest` 가 열 것의 기능 — 없으면 None (처음 다가가기 안내 키, M42-04). */
	EPlaceableFunction GetInteractTargetFunction() const;

	/**
	 * 화면 탭 (M13-20) — 탭 자리의 광선이 처음 지나는 가구가 내 가구이고 페인트를 가졌으면 칠하기 쪽을 연다.
	 * 남의 가구·빈 곳·놓기 모드면 아무것도 안 연다. @return 칠하기 쪽을 열었으면 true
	 */
	bool TryTapFurniture(const FVector2D& ScreenPos);

	/** 광선이 처음 지나는 가구 (미리보기·텐트 뺌). 몸은 충돌이 없어(거리 판정) 트레이스 대신 몸 상자와 맞댄다. */
	static ASnarePlacedStructureActor* FindFurnitureOnRay(UWorld* World, const FVector& Origin, const FVector& Direction);

	/**
	 * 3층 검사 전용 (`-SnarePaintDemo=<페인트 ID>`, M13-33) — `Origin` 앞 두 칸에 벤치 둘을 **로컬로** 세운다:
	 * [0] 은 `PaintItemID` 로 칠하고, [1] 은 고른 가구 테두리를 켠다. 서버·재화·인벤토리는 안 건드린다(그리기만).
	 */
	static TArray<ASnarePlacedStructureActor*> SpawnPaintDemo(UWorld* World, const FVector& Origin, const FVector& Forward,
		FName PaintItemID, float CellSizeCm);

	/** 가방 제작 탭(어디서나 레시피)을 연다. HUD 의 `제작` 버튼이 부른다. */
	void OpenBackpackCrafting();

	/** 배 보관함 쪽을 연다. 배 위 물 버튼 줄의 `배 보관함` 이 부른다 (DESIGN 8.9 보트 컨테이너). */
	void OpenBoatStorage();

	/** 띄운 제작 판 (없으면 nullptr) — 2층 시험이 배낭 제작 탭이 판을 열었는지 본다. */
	UCraftingScreenWidget* GetCraftingScreen() const { return Screen; }
	/** 2층 시험 전용 — 로컬 플레이어 없는 시험 월드에서 판을 붙인다(물 버튼 줄이 판이 열린 동안 숨는지). */
	void SetCraftingScreenForTest(UCraftingScreenWidget* InScreen) { Screen = InScreen; }

	// ── 놓기 모드 ──

	/** 이 아이템으로 놓기 모드를 켠다. 호스트·거점존이 아니면 안내만 하고 false. */
	bool BeginPlacing(FName ItemID);

	/**
	 * 지금 이 아이템을 놓을 수 있나 — 못 놓으면 까닭 한 줄, 놓을 수 있으면 빈 글 (INBOX 2026-09-25 ①).
	 * 폰에는 툴팁이 안 보여 배낭 카드가 이 글을 그대로 보여 준다. 판정은 여전히 서버다 — 이 글은 누르기 전 안내다.
	 */
	FText GetPlaceBlockReason(FName ItemID) const;
	/** 돌리기 — 벽 장식이면 돌리는 대신 높이 단(`SnareBuildRules::WallHeightStops`)을 넘긴다 (M108-02). */
	void RotatePlacement();
	/** 세밀 켜기/끄기 — 켜면 돌리기가 15° 씩, 끄면 90° 씩 (M108-01). 끌 때 가까운 90° 로 맞춘다. */
	void ToggleFinePlacement();
	bool IsFinePlacement() const { return bFinePlacement; }
	/** 서버에 놓기를 묻는다 (보관함이면 5자리 PIN). 초록이 아니면 묻지 않는다. */
	bool ConfirmPlacement(const FString& Pin);
	void CancelPlacement();

	bool IsPlacing() const { return !PlacingItemID.IsNone(); }
	FName GetPlacingItemID() const { return PlacingItemID; }
	FIntPoint GetPlacementCell() const { return PlacementCell; }
	FIntPoint GetPlacementHalf() const { return PlacementHalf; }
	/** 마지막 설치 확정 「쿵」·철거·칠하기 소리를 낸 자리 — 2층 검사용 (M28-03·M45-02). */
	FVector GetLastBuildSoundLocation() const { return LastBuildSoundLocation; }
	int32 GetPlacementRotation() const { return PlacementRotation; }
	/** 벽 장식을 놓는 중인가 (행 기능 `WallDecor`). 놓기 바가 「돌리기」 자리에 「높이」를 보인다. */
	bool IsPlacingWallDecor() const;
	/** 벽 장식 미리보기가 고른 집 변 (0=+Y 1=+X 2=-Y 3=-X), 걸 집이 없으면 -1. */
	int32 GetPlacementWallSide() const { return PlacementWallSide; }
	int32 GetPlacementWallHeight() const { return PlacementWallHeightCm; }
	/** 소품을 놓는 중인가 (행 기능 `TopDecor`). 놓기 바가 「돌리기」 자리에 「자리 n/N」을 보인다. */
	bool IsPlacingTopDecor() const;
	/** 소품 미리보기가 고른 받침(`StructureId`)·슬롯·그 받침 슬롯 수. 받침이 없으면 빈 글자·-1·0. */
	const FString& GetPlacementTopHostId() const { return PlacementTopHostId; }
	int32 GetPlacementTopSlot() const { return PlacementTopSlot; }
	int32 GetPlacementTopSlotCount() const { return PlacementTopSlotCount; }
	ESnarePlacementVerdict GetPlacementVerdict() const { return PlacementVerdict; }
	/** 바닥 가구 미리보기 층 — 2층 집 2층에 서 있으면 2(미리보기가 2층 판 높이에 뜨고 `floor: 2` 로 묻는다), 아니면 1 (M108-06f). */
	int32 GetPlacementFloor() const { return PlacementFloor; }

	/**
	 * 이 발 위치에서 바닥 가구를 놓을 층 — 그 존의 내 2층 집 안 3×3 에 서 있고 발 높이가 층 가름 문턱
	 * (`SnareBuildRules::IsOnUpperFloor`) 이상이면 2, 아니면 1. 2 면 `OutHouse` 에 그 집 사본 (M108-06f, 명세 `house-stairs.md` 2절).
	 */
	int32 PlacementFloorAt(const FVector& Feet, FName Zone, const FSnareTentMirror** OutHouse = nullptr) const;

	/**
	 * 지금 이 레벨의 텐트 존 (`SnareBaseZones`) — 거점존이면 ranch, 사냥 섬이면 hunt, 짓지 못하는 맵이면 None.
	 * 게임모드 클래스로 판단한다 — 게스트도 게임 상태로 안다.
	 */
	static FName GetTentZone(const UWorld* World);

	/**
	 * 이 발자국(월드 중심·반폭 cm) 안에 도구로 캐는 채집물(나무·바위·광맥)이 서 있는지 — 레퍼런스처럼 나무 위엔 못 짓는다.
	 * 꽃·베리처럼 맨손으로 줍는 것은 막지 않는다.
	 */
	static bool IsBlockedByGatherable(const UWorld* World, const FVector& Center, const FVector2D& HalfExtent);

	/**
	 * 이 발자국(월드 중심·반폭 cm)이 드러난 목장 칸(말뚝 + 칸에 붙은 발판·난간) 위에 걸치는지 — 동물을 매어 둔 칸 위엔 못 짓는다.
	 * 잠긴 칸(숨김)은 막지 않는다 — 안 보이는 땅이 빨강이면 까닭을 모른다. 맞닿기만 한 것은 걸침이 아니다.
	 */
	static bool IsBlockedByRanchSlot(const UWorld* World, const FVector& Center, const FVector2D& HalfExtent);

	/** 지금 이 레벨에 지을 수 있나 (`GetTentZone` 이 있다). 레퍼런스처럼 섬 빈 땅 어디든 캠프를 친다 (DESIGN 8.11). */
	static bool IsBaseZone(const UWorld* World);

	/**
	 * 이 자리에 지어도 되는 땅인가 — 지을 수 있는 레벨이고, 뭍이고(바다·광산 굴 밖, `ASeaActor::CoastDistance` < 0),
	 * 선 칸이 건축 불가 존(늪·설원, `FZoneRow::bBuildBlocked`, DESIGN 1.3)이 아니다.
	 */
	static bool IsBuildAllowedAt(const UWorld* World, const FVector& Location);

	/** 놓기 권한 — 내가 조종하는 몸이고(게스트도 된다, M15-14) 지금 선 자리가 `IsBuildAllowedAt`. */
	bool CanBuildHere() const;

	/**
	 * 호스트 판정 (M15-26) — 게스트가 서버(백엔드)에서 확정받은 설치를 호스트 화면에 복제 액터로 세운다.
	 * 발자국이 있는 아이템(놓을 수 있는 것)이고 발자국이 이미 선 것과 안 겹칠 때만 받는다(겹침은 호스트 기준 — 4~8인이 한 땅에 놓는다).
	 * 재화·인벤토리는 안 건드린다: 그건 이미 백엔드가 끝냈고, 여기는 보이는 것만 남에게 알린다.
	 * 벽 장식(`WallSide` 0~3, M108-02)은 바닥 발자국 대신 그 집 벽 칸 변·높이로 판정하고 그 벽에 세운다.
	 * 텐트는 집 여부·색(`bHouse`·`HouseColor`, 서버 `UpgradeHouse` 응답 그대로)도 받는다 — 이미 선 텐트면 그것만 고친다.
	 * 소품(`TopHostId`·`TopSlot`, M108-03)은 바닥 발자국 대신 이 화면에 선 그 받침의 윗면 슬롯으로 판정하고 그 위에 세운다.
	 * 2층 바닥 가구(`Floor` 2, M108-06f)는 같은 층 것끼리만 겹침을 세고 집 바닥 + 2층 높이에 세운다.
	 * 발전기는 연료 남은 초(`FuelSecondsLeft`, M108-07c — 게스트가 서버 `fuelUntilUtc` 로 잰 값)도 — 이미 서 있으면 그것만 고친다.
	 * @return 세웠으면(이미 서 있어도) true
	 */
	bool HostAcceptShared(FName ItemID, const FString& TentId, const FString& StructureId, const FIntPoint& Cell,
		int32 Rotation, const FString& OwnerPlayFabId, FName PaintItemID, const FIntPoint& Half = FIntPoint::ZeroValue,
		int32 WallSide = -1, int32 WallHeightCm = 0, bool bHouse = false, int32 HouseColor = 0,
		const FString& TopHostId = FString(), int32 TopSlot = -1, int32 HouseFloors = 1, int32 Floor = 1,
		float FuelSecondsLeft = 0.f);

	/** 게스트가 치운 것을 호스트 화면에서도 치운다 (주인 계정이 같은 것만). @return 치웠으면 true */
	bool HostRemoveShared(const FString& StructureId, const FString& OwnerPlayFabId);

	FOnSnarePlacementChanged OnPlacementChanged;

	/** 게스트 → 호스트: 내가 세운 것을 남에게도 보이게 (M15-26). 판정은 `HostAcceptShared`. */
	UFUNCTION(Server, Reliable)
	void Server_ShareStructure(FName ItemID, const FString& TentId, const FString& StructureId, FIntPoint Cell,
		int32 Rotation, const FString& OwnerPlayFabId, FName PaintItemID, FIntPoint Half, int32 WallSide, int32 WallHeightCm,
		bool bHouse, int32 HouseColor, const FString& TopHostId, int32 TopSlot, int32 HouseFloors, int32 Floor,
		float FuelSecondsLeft);

	UFUNCTION(Server, Reliable)
	void Server_UnshareStructure(const FString& StructureId, const FString& OwnerPlayFabId);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	USnareBaseSubsystem* GetBase() const;
	bool IsLocalOwner() const;

	/** 가장 가까운 놓인 것 (사거리 = 근접 공격 사거리 — 상호작용 버튼이 하나라 거리도 하나다). */
	ASnarePlacedStructureActor* FindNearestStructure() const;

	UCraftingScreenWidget* EnsureScreen();
	void EnsureHud();

	void HandlePreviewTick();
	/** 벽 장식 미리보기 — 몸 앞 점에서 가장 가까운 내 집(없으면 텐트)의 가장 가까운 벽 칸 변에 붙인다. */
	void UpdateWallPreview(USnareBaseSubsystem& Base, const FBaseRuleRow& Rules);
	/**
	 * 소품 미리보기 — 몸 앞 점에서 가장 가까운 내 받침(탁자·보관함, 이 화면에 선 것)의 윗면 슬롯에 올린다.
	 * 받침이 바뀌면 빈 슬롯 중 점에 가까운 것을 고르고, 같은 받침이면 「자리」로 고른 슬롯을 지킨다.
	 */
	void UpdateTopPreview(USnareBaseSubsystem& Base, const FBaseRuleRow& Rules);
	/** 텐트 행 발자국 = 집 한 변 칸 수 (서버 `HOUSE_CELLS`). 표가 없으면 1. */
	int32 HouseCellsOf(const USnareBaseSubsystem& Base) const;
	/** 이 텐트(집) 바닥 높이 — 세운 텐트 액터가 있으면 그 높이, 없으면 텐트 칸 바닥을 쏴서. */
	float HouseGroundZ(const FSnareTentMirror& Tent, float CellSizeCm) const;
	void HandleFireHealTick();

	// 호스트: 서버 성공 응답 → 복제 액터.
	void HandleTentChanged(const FSnareTentMirror& Tent);
	void HandleStructurePlaced(const FSnareTentMirror& Tent, const FSnareStructureMirror& Structure);
	void HandleStructureRemoved(const FString& StructureId);
	/** 서버가 제작을 확정했다 — 내 몸이면 그 자리 소리 한 번 + 줍기 몸짓 (M28-02). 판은 닫지 않는다. */
	void HandleCraftConfirmed(FName ResultItemID, ESnareCraftPlace Place);
	/** 서버가 설치·집 올리기를 확정했다 — 내 몸이 부탁한 것이면 선 자리에서 「쿵」 한 번, 같은 방 사람도 그 자리에서 듣는다 (M28-03). */
	void HandleBuildConfirmed(const FString& TentId, const FString& StructureId);
	/** 서버가 철거·칠하기를 확정했다 — 내 몸이 부탁한 것이면 그 물건 자리에서 한 번, 같은 방 사람도 그 자리에서 듣는다 (M45-02). */
	void HandleStructureEditConfirmed(const FString& TentId, const FString& StructureId, ESnareStructureEdit Edit);
	void SpawnWholeMirror();
	/**
	 * 서버가 확정한 것을 화면에 세운다. 복제는 권위가 있는 쪽(호스트)만 — 게스트 것은 자기 화면에만 선다.
	 * `Floor` 2 = 2층 바닥 가구 — 그 집 바닥 + `UpperFloorZCm` 에 선다(바닥을 쏘면 2층 판을 지나 땅에 맞는다). 소품은 받침 층을 따른다.
	 */
	ASnarePlacedStructureActor* SpawnStructure(FName ItemID, const FString& TentId, const FString& StructureId,
		const FIntPoint& Cell, int32 Rotation, bool bReplicated, const FString& OwnerPlayFabId = FString(),
		const FIntPoint& Half = FIntPoint::ZeroValue, int32 WallSide = -1, int32 WallHeightCm = 0,
		const FString& TopHostId = FString(), int32 TopSlot = -1, int32 Floor = 1);
	/** `HostAcceptShared` 의 벽 장식 판 — 이 화면에 선 그 집 액터의 벽 칸·같은 벽 간격(`EvaluateWallHang`)으로 받는다. */
	bool HostAcceptSharedWall(const USnareBaseSubsystem& Base, const FBaseRuleRow& Rules, FName ItemID, const FString& TentId,
		const FString& StructureId, const FIntPoint& Cell, const FString& OwnerPlayFabId, FName PaintItemID,
		int32 WallSide, int32 WallHeightCm);
	/** `HostAcceptShared` 의 소품 판 — 이 화면에 선 그 받침 액터의 슬롯 수·같은 받침에 올라간 슬롯(`EvaluateTopPlace`)으로 받는다. */
	bool HostAcceptSharedTop(FName ItemID, const FString& TentId, const FString& StructureId, const FIntPoint& Cell,
		const FString& OwnerPlayFabId, FName PaintItemID, const FString& TopHostId, int32 TopSlot);
	/** 클라이언트에서 세운 것을 호스트에게 알린다(네트워크 클라이언트일 때만 나간다). */
	void ShareWithHost(const ASnarePlacedStructureActor* Spawned, const FIntPoint& Cell, const FIntPoint& Half) const;
	ASnarePlacedStructureActor* FindSpawned(const FString& TentId, const FString& StructureId) const;
	/**
	 * 검사 스위치 `-SnareWalkStairs=<발 높이 cm>` (M108-06d) — 몸을 계단 낮은 끝 앞 땅에 세우고 사람 조이스틱과 같은
	 * `AddMovementInput` 으로 오르게 한다. 발 높이가 그 값에 닿으면 손을 떼고(비탈 위에 선 채), 2층 바닥 높이 이상이면
	 * 꼭대기에서 집 가운데 쪽으로 걸어 나가 2층 판에 선다. 위치를 옮겨 놓지 않는다 — 비탈 충돌이 몸을 올린다.
	 */
	void StartStairsWalkDemo(const ASnarePlacedStructureActor& Stairs, const FVector& HouseCenter, float StopFeetCm);
	/** 검사 스위치 `-SnarePlaceUpstairs` (M108-06f)·`-SnarePlaceGroundRoom` (M108-06f-4) — 집 안에 선 몸을 `Facing` 으로 돌리고
	 *  나무 의자 놓기 미리보기를 켠다(누르지 않음). `SwitchName` 은 로그에 적는 스위치 이름. */
	void BeginUpstairsPlaceDemo(const FVector& Facing, const TCHAR* SwitchName = TEXT("-SnarePlaceUpstairs"));

	UPROPERTY(Transient)
	TObjectPtr<UCraftingScreenWidget> Screen;

	UPROPERTY(Transient)
	TObjectPtr<UBuildHudWidget> Hud;

	UPROPERTY(Transient)
	TObjectPtr<ASnarePlacedStructureActor> PreviewActor;

	FName PlacingItemID;
	FIntPoint PlacementCell = FIntPoint::ZeroValue;
	/** 세밀 격자 반 칸 오프셋(0/1) — `PlacementCell` 에 더해 50cm 단위 자리가 된다. */
	FIntPoint PlacementHalf = FIntPoint::ZeroValue;
	int32 PlacementRotation = 0;
	bool bFinePlacement = false;
	/** 벽 장식 놓기 — 고른 집·변·높이 (서버 `PlaceStructure` 의 `tentId`·`wall {side, h}`). */
	FString PlacementTentId;
	int32 PlacementWallSide = -1;
	int32 PlacementWallHeightCm = SnareBuildRules::WallHeightDefault;
	/** 소품 놓기 — 고른 받침·슬롯 (서버 `PlaceStructure` 의 `top {host, slot}`), 받침 슬롯 수. 텐트는 `PlacementTentId`. */
	FString PlacementTopHostId;
	int32 PlacementTopSlot = -1;
	int32 PlacementTopSlotCount = 0;
	ESnarePlacementVerdict PlacementVerdict = ESnarePlacementVerdict::Blue;
	int32 PlacementFloor = 1;

	FTimerHandle PreviewTimerHandle;
	FTimerHandle FireHealTimerHandle;
	FDelegateHandle TentChangedHandle;
	FDelegateHandle StructurePlacedHandle;
	FDelegateHandle StructureRemovedHandle;
	FDelegateHandle CraftConfirmedHandle;
	FDelegateHandle BuildConfirmedHandle;
	FDelegateHandle StructureEditConfirmedHandle;
	FVector LastBuildSoundLocation = FVector::ZeroVector;
};
