#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Building/SnareBuildRules.h"
#include "Data/ItemRow.h"
#include "SnarePlacedStructureActor.generated.h"

class APawn;
class UAudioComponent;
class UBoxComponent;
class UDataTable;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

// DESIGN 8.11: 거점 땅에 놓인 텐트·집·가구 하나.
//
// 호스트가 서버(`PlaceTent`/`PlaceStructure`) 성공 응답을 받은 **뒤에만** 복제로 세운다 — 게스트는 보기만 하고
// PIN 보관함만 연다 (DESIGN 8.11 "거점존 방은 호스트의 땅을 그린다"). 이 액터는 판정을 하지 않는다:
// 들고 있는 ID 들은 서버에 다시 물을 때 쓰는 **주소**다.
//
// 행에 메시(`PlacedMeshRef`)가 있으면 그 메시를 표 폭(`PlacedMeshWidthCm`)에 맞춘 한 배율로 세운다 (M13-37).
// 없으면 발자국 크기의 엔진 큐브를 색으로 칠해 세운다(자리표시).
// 미리보기(초록·파랑·빨강)도 같은 클래스를 복제 없이 로컬로 세워 쓴다.
UCLASS()
class SNAREHAVEN_API ASnarePlacedStructureActor : public AActor
{
	GENERATED_BODY()

public:
	ASnarePlacedStructureActor();

	/** 복제로 세우기 전 호스트가 채운다 (`SpawnActorDeferred` 사이). */
	void InitStructure(FName InItemID, const FString& InTentId, const FString& InStructureId,
		const FString& InOwnerPlayFabId, int32 InRotation, float InCellSizeCm);

	/**
	 * 텐트가 집이 됐다 — 서버 `UpgradeHouse` 응답의 `color` 그대로. 집이면 몸이 3×3 칸 오두막 몸통(`SM_Building_TentHouse_01`,
	 * 벽 안쪽 면 = 발자국 바깥 변)으로 바뀌고 덮개(앞벽·지붕, 지붕이 집 색)가 얹힌다 — 벽 장식이 걸릴 벽 (M108-02).
	 */
	void SetHouse(bool bInHouse, int32 InColor, int32 InFloors = 1);

	/** 가구를 칠했다 — 서버 `PaintStructure` 응답의 `color`(페인트 아이템 ID). None 이면 안 칠함. */
	void SetPaint(FName InPaintItemID);

	/** 미리보기 전용 — 복제하지 않는 로컬 액터로 만들고 판정 색을 입힌다. */
	void SetPreviewVerdict(ESnarePlacementVerdict Verdict);

	/**
	 * 고른 가구 테두리 (M13-24) — 켜면 몸 메시에 `M_Outline_Selected` 오버레이(뒤집은 껍질), 끄면 뺀다.
	 * 내 화면 표시일 뿐이라 복제하지 않는다. 색은 UI 팔레트 강조색, 두께는 DESIGN 8.12 표.
	 */
	void SetSelectedOutline(bool bOn);
	bool HasSelectedOutline() const;

	/**
	 * 전등 불빛 (DESIGN 8.11 「집 2층·전력」·3.3 「밤 불빛」 전구 줄) — `USnarePowerSubsystem` 이 밤이고 그 땅에 전력이
	 * 넉넉할 때만 켠다. 비전기 조명(`FlameLight`)도 같은 점광 — 색만 모닥불, 높이는 불꽃 자리. 복제하지 않는다: 각 기기가 같은 놓인 가구 목록과 같은 서버 시계로 스스로 센다.
	 */
	void SetLampLit(bool bLit, float Candela, float RadiusCm, const FLinearColor& Color = FLinearColor(FColor(255, 160, 80)));
	bool IsLampLit() const { return bLampLit; }
	bool IsLampLightVisible() const;

	/**
	 * 전기 가구 켜짐 모습 (M108-08b-2, DESIGN 8.11 「🟡 전기 가구」) — 켜지면 행 `PlacedOnMeshRef` 짝으로 바꿔 끼운다(섹션 1 — 드로콜 그대로,
	 * 자리·배율은 꺼진 메시로 재서 안 움직인다). 미리보기는 늘 꺼진 모습. 전력 셈(M108-08c)이 부른다 — 전등처럼 복제하지 않는다.
	 * 검사 스위치 `-SnareElectricOnDemo` 면 짝이 있는 가구는 늘 켜진 모습(내 화면만).
	 */
	void SetElectricOn(bool bOn);
	bool IsElectricOn() const { return bElectricOn; }
	/** 행 `PowerUse` — 전기 가구 소모(0 = 전기 아님). 전력 셈(`USnarePowerSubsystem`)이 땅 소모 합에 더한다 (M108-08c). */
	int32 GetPowerUse() const { return PowerUse; }
	/** 몸에 지금 켜짐 짝 메시가 끼워졌나 — 시험·검사 로그용. */
	bool IsShowingElectricOnMesh() const { return bShowingElectricOnMesh; }
	/**
	 * 도는 따로 메시 한 걸음 (M108-08d-1, 선풍기 날개 — 행 `PlacedSpin*`) — 켜짐 모습인 동안 초당 `PlacedSpinTurnsPerSecond` 바퀴로
	 * 자기 Y 축을 돌린다. 꺼지면 그 각에 멈춘다. 틱이 부른다 — 틱은 도는 동안만 켜진다. 시험 월드는 틱이 없어 직접 부른다.
	 */
	void TickElectricSpin(float DeltaSeconds);
	/** 도는 중인가 (도는 메시가 있고 켜짐 모습). */
	bool IsSpinning() const { return bSpinning; }
	/** 도는 메시의 지금 각(도, 0~360) — 검사용. */
	float GetSpinAngleDeg() const { return SpinAngleDeg; }
	const UStaticMeshComponent* GetSpinComponent() const { return Spinner; }
	/** 켜짐 고리를 틀어야 하는 상태인가(행 `PlacedOnLoopCue` 가 있고 켜짐 모습, M108-08d-2 라디오) — 소리 장치 없는 실행에서도 잰다. */
	bool IsElectricLoopOn() const { return bElectricLoopOn; }
	/** 켜짐 고리 오디오 컴포넌트가 실제로 재생 중인가. */
	bool IsElectricLoopPlaying() const;
	const UAudioComponent* GetElectricLoop() const { return ElectricLoop; }

	/**
	 * 연료 발전기 (M108-07c, DESIGN 8.11 「연료 발전기 숫자」) — 서버 `fuelUntilUtc` 까지 남은 초를 **게임 서버 시계** 끝 시각으로 옮겨 둔다.
	 * 기기 시계가 어긋나도 모든 기기가 같은 순간에 멈춘다. 연료 시각을 정하는 것은 서버 `FeedGenerator` 뿐 — 여기는 그 답의 사본이다.
	 * 복제한다: 게스트 화면의 전등도 호스트가 세운 발전기 끝 시각으로 켜고 끈다. 0 이하 = 연료 없음.
	 */
	void SetGeneratorFuelSecondsLeft(double SecondsLeft);
	/** 연료가 남아 도는 발전기인가 — 끝 시각 > 지금 서버 시계. 발전기가 아니면 false. */
	bool IsGeneratorRunning() const;
	/** 연료 남은 초 (지금 서버 시계 기준, 0 이상). */
	double GetGeneratorFuelSecondsLeft() const;
	/**
	 * 발전기 공회전 고리 (M108-07d, DESIGN 8.11 「연료 발전기 숫자」 소리 감쇠 300/1500 · 음량 0.5) — 도는 동안만 몸에 붙여 틀고,
	 * 멈추면 끊는다. 연료를 받을 때·복제로 끝 시각이 올 때 부르고, 도는 동안 1초마다 다시 봐 연료가 다 타면 1초 안에 멎는다.
	 * 각 기기가 같은 복제 끝 시각으로 스스로 정한다(소리는 복제하지 않음).
	 */
	void RefreshGeneratorSound();
	/** 소리를 틀어야 하는 상태인가(도는 발전기·미리보기 아님) — 소리 장치 없는 실행에서도 잰다. */
	bool IsGeneratorSoundOn() const { return bGeneratorSoundOn; }
	/** 공회전 오디오 컴포넌트가 실제로 재생 중인가. */
	bool IsGeneratorSoundPlaying() const;
	const UAudioComponent* GetGeneratorLoop() const { return GeneratorLoop; }
	/** 발전기 공회전 고리 소리 이름 — 되풀이 웨이브 허용 목록 시험(`Snarehaven.Audio.Waves.OneShotsDoNotLoop`)이 읽는다. */
	static FName GetGeneratorLoopCue();
	/**
	 * 발전기 몸 떨림 한 걸음 (M108-07d-2, DESIGN 8.11 「연료 발전기 숫자」 떨림) — 도는 동안 몸을 놓인 자리 둘레로 원을 그리며 떤다
	 * (전기톱 떨림 `UEquipmentComponent::TickComponent` 와 같은 원). 틱이 부른다 — 틱은 도는 발전기만 켜진다(`RefreshGeneratorSound`).
	 * 시험 월드는 틱이 없어 직접 부른다.
	 */
	void TickGeneratorShake(float DeltaSeconds);
	/** 틱이 켜졌나 = 몸이 떠는 중인가 (도는 발전기·미리보기 아님). */
	bool IsGeneratorShaking() const { return bGeneratorShaking; }
	/** 몸이 놓인 자리(`RefreshVisual` 이 둔 상대 자리)에서 벗어난 만큼 (cm) — 검사용. */
	FVector GetBodyOffsetFromRest() const;
	/** 게임 서버 시계 — 게스트도 GameState 가 맞춘 서버 시각. 게임스테이트 없는 검사 월드는 자기 시계. */
	static double ServerNowSeconds(const UWorld* World);

	FName GetItemID() const { return ItemID; }
	const FString& GetTentId() const { return TentId; }
	const FString& GetStructureId() const { return StructureId; }
	const FString& GetOwnerPlayFabId() const { return OwnerPlayFabId; }
	int32 GetRotation() const { return Rotation; }
	bool IsHouse() const { return bHouse; }
	int32 GetHouseColor() const { return HouseColor; }
	/** 집 층수 — 서버 텐트 줄 `floors`(2층 올리기 뒤 2). 텐트·1층 집은 1. */
	int32 GetHouseFloors() const { return HouseFloors; }
	/** 위층 몸을 세운 2층 집인가 (M108-06c) — 집 몸통·위층 메시가 다 있고 층수 2 이상. */
	bool IsTwoStoryHouse() const;
	FName GetPaintItemID() const { return PaintItemID; }

	/**
	 * 벽 장식이 걸린 칸·변·높이 (M108-02) — 서버 `wall {side, h}` 그대로. 바닥 가구는 변 -1.
	 * 복제하지 않는다: 호스트가 게스트 벽 장식을 받을 때 같은 벽 자리 겹침을 세는 데만 쓴다(자리는 액터 위치로 복제된다).
	 */
	void SetWallSlot(const FIntPoint& InCell, int32 InSide, int32 InHeightCm);
	bool IsWallHung() const { return WallSide >= 0; }
	FIntPoint GetWallCell() const { return WallCell; }
	int32 GetWallSide() const { return WallSide; }
	int32 GetWallHeightCm() const { return WallHeightCm; }

	/**
	 * 소품이 올라선 받침·슬롯 (M108-03) — 서버 `top {host, slot}` 그대로. 바닥 가구는 빈 글자·-1.
	 * 복제하지 않는다: 자리는 액터 위치로 복제되고, 이 값은 같은 받침 슬롯 겹침을 세는 데만 쓴다.
	 */
	void SetTopSlot(const FString& InHostId, int32 InSlot);
	bool IsOnTop() const { return !TopHostId.IsEmpty(); }
	const FString& GetTopHostId() const { return TopHostId; }
	int32 GetTopSlot() const { return TopSlot; }
	/**
	 * 놓인 층 (M108-06f) — 서버 `floor` 그대로(2층 바닥 가구·러그와 그 받침 위 소품은 2). 텐트·1층은 1.
	 * 복제한다: 게스트 화면의 집도 「1층에 서면 2층 가구 숨김」을 이 칸으로 가른다.
	 */
	void SetPlacedFloor(int32 InFloor) { PlacedFloor = FMath::Clamp(InFloor, 1, 2); }
	int32 GetPlacedFloor() const { return PlacedFloor; }
	/**
	 * 층 보기 숨김 (M108-06f) — 2층 집 1층에 선 이 기기 플레이어에게 2층 가구를 숨긴다(몸·불빛). 집의 `UpdateHouseRoof` 가 부른다.
	 * 복제하지 않는다(보는 사람마다 다르다) — 액터 숨김(`bHidden`)은 복제되므로 구성요소 보이기로만.
	 */
	void SetFloorViewHidden(bool bHide);
	bool IsFloorViewHidden() const { return bFloorViewHidden; }
	/** 받침 가구면 윗면 소품 슬롯 수 (행 `TopSlots`), 아니면 0. */
	int32 GetTopSlotCount() const { return TopSlotCount; }
	/**
	 * 받침 윗면 슬롯 자리 (M108-03) — 몸 메시 긴 변을 슬롯 수로 나눈 가운데(`SnareBuildRules::TopSlotAlong`), 높이 = 메시 꼭대기.
	 * 메시 공간에서 잰 뒤 몸 변환을 태우므로 15° 돌린 받침에서도 윗면 위다. 받침이 아니거나 슬롯 밖이면 false.
	 */
	bool GetTopSlotLocation(int32 Slot, FVector& OutLocation) const;
	/** 텐트(=작업대)인가. 텐트 행에는 `StructureId` 가 없고 `TentId` 만 있다. */
	bool IsTent() const { return PlaceableFunction == EPlaceableFunction::Tent; }
	EPlaceableFunction GetPlaceableFunction() const { return PlaceableFunction; }
	FIntPoint GetFootprint() const { return Footprint; }
	bool IsPreview() const { return bPreview; }
	float GetCellSizeCm() const { return CellSizeCm; }

	/** 칸(월드 격자)에 맞춰 세울 자리 — 발자국 가운데 바닥. 바닥은 아래로 쏴서 찾는다. */
	static FVector CellToWorld(UWorld* World, const FIntPoint& AnchorCell, const FIntPoint& RotatedSize, float InCellSizeCm,
		const FIntPoint& Half = FIntPoint::ZeroValue);

	/**
	 * 벽 장식 자리 (M108-02) — 집 벽 칸 변 가운데(`SnareBuildRules::WallSlotLocation`), 높이 = 바닥 `GroundZ` + `HeightCm`(장식 밑면).
	 * 바닥은 쏘지 않고 받는다 — 벽 칸 위엔 텐트 몸이 있어 아래로 쏘면 지붕에 맞는다(집 액터 높이를 넘긴다).
	 */
	static FVector WallToWorld(const FIntPoint& Cell, int32 Side, float InCellSizeCm, float GroundZ, int32 HeightCm);

	/** 미리보기 색·집 색 (월드 환경 시트 기준 🟡). 2층 검사가 판정 색을 본다. */
	static FLinearColor VerdictColor(ESnarePlacementVerdict Verdict);
	static FLinearColor HouseColorAt(int32 ColorIndex);

	FLinearColor GetTint() const { return Tint; }

	/** 집 몸통·덮개 (M108-02) — 시험이 메시·보이기를 본다. 덮개는 집일 때만 보이고, 이 기기 플레이어가 그 안에 서면 숨는다. */
	const UStaticMeshComponent* GetBodyComponent() const { return Body; }
	const UStaticMeshComponent* GetHouseRoofComponent() const { return HouseRoof; }
	/** 2층 집 (M108-06c) — 1층 앞벽·위층 벽(+275)·2층 바닥 판(계단 칸 비움). 2층이 아니면 숨김. 시험이 칸 수·충돌을 본다. */
	const UStaticMeshComponent* GetHouseFrontComponent() const { return HouseFront; }
	const UStaticMeshComponent* GetHouseUpperComponent() const { return HouseUpper; }
	const UInstancedStaticMeshComponent* GetUpperFloorComponent() const { return UpperFloor; }
	/**
	 * 계단 비탈 충돌 (M108-06d) — 행 기능 `Stairs` 일 때만 켜지는 매끈한 판 하나. 윗면이 낮은 끝(-X) 바닥에서 높은 끝(+X)
	 * 2층 바닥 높이(`UpperFloorZCm`)까지 오른다. 폰만 막는다 — 위층 판과 같은 이유(바닥 찾기·카메라는 지나감).
	 */
	const UBoxComponent* GetStairRampComponent() const { return StairRamp; }

	/**
	 * 2층 바닥 판을 다시 깐다 — 집 3×3 칸 중 놓인 계단(행 기능 `Stairs`) 발자국에 든 칸만 비운다(계단 구멍).
	 * 계단이 서거나 치워질 때 계단 쪽에서도 부른다(`NotifyHousesOfStairs`) — 집과 계단 중 무엇이 먼저 서도 맞게.
	 * `IgnoreStairs` = 지금 치워지는 계단(아직 월드에 있다).
	 */
	void RefreshUpperFloor(const AActor* IgnoreStairs = nullptr);

	/**
	 * 이 2층 집의 2층에 선 폰(발 = `HouseViewFeet`, 집 발자국 안·`IsOnUpperFloor`)을 문 앞 디딤 자리
	 * (`SnareBuildRules::HouseDoorStepX`)로 옮기고 옮긴 수를 돌려준다 (M108-06f-3). 계단이 치워질 때 호스트(클라 아닌 쪽)가
	 * 부른다 — 게스트 폰은 서버 위치가 복제로 따라간다. 2층 집이 아니면 0.
	 */
	int32 MoveUpperFloorPawnsToDoor();

	/**
	 * 덮개 숨김을 지금 다시 정한다 — `Feet` 가 덮개 밑이면 숨김 (`SnareHouseRoof::IsUnderRoof`, 거점 오두막과 같은 줄).
	 * 2층 집이면 발 높이로 층을 가른다(`SnareBuildRules::IsOnUpperFloor`): 1층 안 = 위층 덮개·1층 앞벽·위층 벽·2층 판 다 숨김,
	 * 2층 안 = 위층 덮개(지붕·위층 앞벽)만 숨김 (M108-06e). 1층 안이면 이 집 2층 가구(`PlacedFloor` 2)도 숨김 (M108-06f).
	 * 안에 서면 선 층 앞벽에 건 장식(행 기능 `WallDecor`, `IsOnFrontWall`)도 앞벽과 같이 숨김 (M108-09).
	 */
	void UpdateHouseRoof(const FVector& Feet);

	/** 이 집(텐트) 발자국의 앞(-X, 문 쪽) 벽 면 위 자리인가 — 벽 장식 액터 자리(벽 칸 변 가운데)로 가른다, 액터 돌림을 따름 (M108-09). */
	bool IsOnFrontWall(const FVector& WorldLocation) const;

	/** 덮개 숨김에 쓰는 발 위치 — 폰 캡슐 밑(액터 위치는 캡슐 가운데라 층 가름엔 반 키만큼 높다). */
	static FVector HouseViewFeet(const APawn& Pawn);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void Destroyed() override;

private:
	UFUNCTION()
	void OnRep_Structure();

	UFUNCTION()
	void OnRep_GeneratorFuel();

	/** 표를 읽어 발자국·기능을 맞추고 메시(없으면 큐브)·불빛·색을 다시 칠한다. */
	void RefreshVisual();
	/**
	 * 러그를 얼마나 올릴지(cm) — 루트는 발자국 가운데 바닥이라 비탈·울퉁불퉁한 모래에선 두께 2cm 러그가 통째로 묻힌다(루프#1389 캡처).
	 * 발자국 네 귀·가운데 바닥 중 가장 높은 곳까지 올린다(상한 `RugMaxLiftCm`, DESIGN 8.11 「러그 깔기」). 바닥을 못 찾으면 0.
	 */
	float RugGroundLiftCm() const;
	void ApplyTint(const FLinearColor& Color);

	/** 집 덮개 숨김 타이머 — 이 기기 플레이어 발 위치로 `UpdateHouseRoof`. 집일 때만 돈다(매 프레임 Tick 대신). */
	void PollHouseRoof();

	/** 계단이면 이 계단이 걸친 2층 집들의 바닥 판을 다시 깔게 한다. */
	void NotifyHousesOfStairs(const AActor* IgnoreStairs) const;

	/** 불빛 보이기 — 불 행이거나 켠 전등이고, 미리보기·층 보기 숨김이 아닐 때. */
	void RefreshFireLight();
	/** 모닥불·그릴·제련소 — 늘 불빛이 있는 행. */
	bool IsFireFunction() const;

	/** 계단이면 비탈 판을 발자국 긴 변·칸 크기에 맞춰 눕히고 켠다. 계단이 아니거나 미리보기면 끈다. */
	void RefreshStairRamp();

	/** 도는 메시를 행 칸대로 몸에 붙이거나(자리·젖힘) 숨기고, 켜짐 모습이면 돌기 시작한다 (M108-08d-1). */
	void RefreshSpinner(const FItemRow* Row, bool bBodyIsRowMesh);
	/** 켜짐 모습인 동안 행 `PlacedOnLoopCue` 고리를 몸에 붙여 틀고, 꺼지면 끊는다 (M108-08d-2 라디오 가락). */
	void RefreshElectricLoop(const FItemRow* Row);
	/** 틱 켜기 — 떠는 발전기나 도는 날개가 있을 때만. */
	void RefreshTickEnabled();

	UPROPERTY(VisibleAnywhere, Category = "Base")
	TObjectPtr<UStaticMeshComponent> Body;

	/** 집 덮개 — 앞벽·박공·지붕·문·창. 집이 아니면 숨김. */
	UPROPERTY(VisibleAnywhere, Category = "Base")
	TObjectPtr<UStaticMeshComponent> HouseRoof;

	/** 집 몸통·덮개 메시 (`make_house_meshes.py` 의 `TENT_HOUSE_DIMS`, 배율 1 — 발자국 가운데가 원점). */
	UPROPERTY()
	TObjectPtr<UStaticMesh> HouseMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> HouseRoofMesh;

	/** 2층 집 (M108-06c) — 1층 앞벽(덮개를 둘로 가른 아래)·위층 벽·위층 덮개(앞벽 창 하나·지붕)·2층 바닥 판 한 칸. */
	UPROPERTY(VisibleAnywhere, Category = "Base")
	TObjectPtr<UStaticMeshComponent> HouseFront;

	UPROPERTY(VisibleAnywhere, Category = "Base")
	TObjectPtr<UStaticMeshComponent> HouseUpper;

	UPROPERTY(VisibleAnywhere, Category = "Base")
	TObjectPtr<UInstancedStaticMeshComponent> UpperFloor;

	/** 계단 비탈 충돌 (M108-06d) — 보이지 않는다. 눈에 보이는 디딤 12단은 몸 메시. */
	UPROPERTY(VisibleAnywhere, Category = "Base")
	TObjectPtr<UBoxComponent> StairRamp;

	UPROPERTY()
	TObjectPtr<UStaticMesh> HouseFrontMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> HouseUpperMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> HouseUpperRoofMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> UpperFloorMesh;

	/** 덮개 지붕 슬롯 사본 — 집 색을 입힌다. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HouseRoofTint;

	/** 켜짐 모습일 때 도는 따로 메시 (M108-08d-1, 선풍기 날개) — 몸에 붙는다. 행에 없으면 숨김. */
	UPROPERTY(VisibleAnywhere, Category = "Base")
	TObjectPtr<UStaticMeshComponent> Spinner;

	/** 모닥불·화로·그릴의 불빛. 불이 아니면 꺼 둔다. */
	UPROPERTY(VisibleAnywhere, Category = "Base")
	TObjectPtr<UPointLightComponent> FireLight;

	/** 도는 발전기의 공회전 고리 — 멈추면 없앤다 (M108-07d). */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> GeneratorLoop;

	/** 켜진 전기 가구의 고리(라디오 가락) — 꺼지면 없앤다 (M108-08d-2). */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ElectricLoop;

	UPROPERTY()
	TObjectPtr<UDataTable> ItemTable;

	/** 행에 메시가 없을 때의 자리표시 큐브와 그 재질. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> CubeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TintMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> OutlineMaterial;

	/** 테두리 색·두께를 넣은 사본 — 처음 켤 때 만든다. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OutlineInstance;

	UPROPERTY(ReplicatedUsing = OnRep_Structure)
	FName ItemID;

	UPROPERTY(Replicated)
	FString TentId;

	UPROPERTY(Replicated)
	FString StructureId;

	/** 게스트가 남의 보관함을 열 때 서버에 넘길 주인 (`MoveToStorage` 의 `ownerId`). */
	UPROPERTY(Replicated)
	FString OwnerPlayFabId;

	UPROPERTY(ReplicatedUsing = OnRep_Structure)
	int32 Rotation = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Structure)
	bool bHouse = false;

	UPROPERTY(ReplicatedUsing = OnRep_Structure)
	int32 HouseColor = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Structure)
	int32 HouseFloors = 1;

	UPROPERTY(ReplicatedUsing = OnRep_Structure)
	FName PaintItemID;

	UPROPERTY(Replicated)
	int32 PlacedFloor = 1;

	/** 발전기 연료가 끝나는 게임 서버 시계(초). 0 = 연료 없음 (M108-07c). 오면 공회전 소리를 다시 본다(07d). */
	UPROPERTY(ReplicatedUsing = OnRep_GeneratorFuel)
	double GeneratorFuelEndServerSeconds = 0.0;

	/** `DT_BaseRuleTable.CellSizeCm` — 게스트는 표 대신 이 값으로 크기를 맞춘다. */
	UPROPERTY(ReplicatedUsing = OnRep_Structure)
	float CellSizeCm = -1.f;

	EPlaceableFunction PlaceableFunction = EPlaceableFunction::None;
	FIntPoint Footprint = FIntPoint(1, 1);
	FIntPoint WallCell = FIntPoint::ZeroValue;
	int32 WallSide = -1;
	int32 WallHeightCm = 0;
	FString TopHostId;
	int32 TopSlot = -1;
	int32 TopSlotCount = 0;
	int32 PowerUse = 0;
	FLinearColor Tint = FLinearColor::White;
	bool bPreview = false;
	bool bLampLit = false;
	bool bElectricOn = false;
	bool bShowingElectricOnMesh = false;
	bool bFloorViewHidden = false;
	FTimerHandle HouseRoofTimer;
	FTimerHandle GeneratorSoundTimer;
	bool bGeneratorSoundOn = false;
	bool bGeneratorShaking = false;
	float GeneratorShakePhase = 0.f;
	bool bSpinning = false;
	bool bSpinLoggedTurn = false;
	bool bElectricLoopOn = false;
	float SpinAngleDeg = 0.f;
	float SpinTurnsPerSecond = 0.f;
	/** 도는 메시를 몸에 붙인 젖힘 — 돌림은 이 안쪽(자기 Y 축)에 얹는다. */
	FQuat SpinTilt = FQuat::Identity;
	/** 몸이 놓인 상대 자리 — 떨림은 이 위에 얹는다(0 에 얹으면 메시 가운데 맞춤이 지워진다). */
	FVector BodyRestLocation = FVector::ZeroVector;
	ESnarePlacementVerdict PreviewVerdict = ESnarePlacementVerdict::Blue;
};
