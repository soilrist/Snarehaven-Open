#pragma once

#include "CoreMinimal.h"
#include "Data/AnimalSpreadRow.h"
#include "GameFramework/Actor.h"
#include "AnimalSpreadActor.generated.h"

class AAnimalCharacter;
class UPrimitiveComponent;
class UDataTable;
struct FZoneRow;

// INBOX 지시(감독 2026-09-23 10:45) ★4 — 동물을 **맵 전역에 흩어** 세우는 스포너.
// 맵에 하나 놓고 `DT_AnimalSpreadTable` 의 행 이름만 적으면 영역·마리수·간격·금지 반경·재등장
// 주기는 전부 그 행이 정한다 (DataDriven.md 1). 숫자 원본은 DESIGN 3.1 이다.
//
// **자리 고르는 법 = 격자 칸 무작위.** 영역 안에 `MinSpacingCm` 간격의 격자를 깔고, 플레이어
// 시작점 둘레 `PlayerStartClearCm` 안의 칸을 버린 뒤, 남은 칸 중에서 **무작위로** 고른다.
// 왜 순수 무작위 좌표가 아닌가: 5000cm 정사각형에서 1200cm 간격을 지키며 좌표를 그냥 뽑으면
// (RSA 포화) 9마리 언저리에서 더 못 꽂는다 — 16마리가 물리적으로 안 들어간다. 격자로 깔면
// 같은 맵에서 후보가 20칸 나와 16마리가 들어가고, 재등장 때 **남은 빈 칸**을 다시 무작위로 고르므로
// "잡히면 다른 지점에서 나온다" 도 성립한다. 맵이 넓어지면(M12-02) 후보 칸이 늘어 저절로 흩어진다.
//
// **스폰은 서버만 한다** (CLAUDE.md 8 · DESIGN 4.1 권위 2단 표) — `ADangerZoneActor` 와 같은 규칙이다.
// 게스트에게는 복제된 동물만 간다.
UCLASS()
class SNAREHAVEN_API AAnimalSpreadActor : public AActor
{
	GENERATED_BODY()

public:
	AAnimalSpreadActor();

	/** `SpreadTable` 에서 `SpreadRowName` 행을 읽어 수치를 확정한다. BeginPlay 가 부른다. */
	UFUNCTION(BlueprintCallable, Category = "AnimalSpread")
	bool InitializeFromTable();

	/** 볼 표와 행 이름을 바꾼다 (맵에 놓을 때 배치 스크립트가 행 이름을 적는다). */
	UFUNCTION(BlueprintCallable, Category = "AnimalSpread")
	void SetSpreadData(UDataTable* Table, FName RowName);

	/** 표를 거치지 않고 수치를 직접 먹인다 — **검사 전용 길** (Verification.md 6). */
	UFUNCTION(BlueprintCallable, Category = "AnimalSpread")
	bool SetSpreadRow(const FAnimalSpreadRow& Row);

	/** 수치가 성해서 켜졌는지. 미정(음수)이 하나라도 있으면 false — 한 마리도 안 세운다. */
	UFUNCTION(BlueprintPure, Category = "AnimalSpread")
	bool IsSpreadActive() const { return bSpreadActive; }

	/** 확정된 수치 (행 사본). */
	UFUNCTION(BlueprintPure, Category = "AnimalSpread")
	const FAnimalSpreadRow& GetSpreadRow() const { return SpreadRow; }
	FName GetSpreadRowName() const { return SpreadRowName; }

	/** 지금 서 있는(안 죽고 안 잡힌) 동물 수. 죽은 동물은 액터가 남아도 빈자리로 센다. */
	UFUNCTION(BlueprintPure, Category = "AnimalSpread")
	int32 GetLiveAnimalCount() const;

	/**
	 * 빈자리만큼 동물을 **새 무작위 칸**에 세운다. 서버에서만 동작한다.
	 * @return 이번에 세운 마리 수
	 */
	UFUNCTION(BlueprintCallable, Category = "AnimalSpread")
	int32 RefillArea();

	/**
	 * 이야기 종(`bStorySpecies`) 한 마리를 이 존에 세운다 (docs/specs/story-fogbuck.md 2c). 서버에서만 동작한다.
	 * 이미 서 있으면 안 세운다. 언제 서야 하는지는 `bWanted` — 호출하는 쪽이 이야기 단계로 정한다.
	 * @return 이번에 세웠으면 true
	 */
	bool RefillStoryAnimal(bool bWanted);

	/** 이 존에 사는 이야기 종 행 이름 (없으면 NAME_None). 월드가 없어도 도는 순수 함수 — 2층이 직접 잰다. */
	static FName FindStorySpecies(const UDataTable* AnimalTable, FName ZoneID);

	/** 지금 서 있는 이야기 동물이 있는가. */
	bool HasStoryAnimalStanding() const;

	/** 지금 서 있는 시작점 순한 동물 수 (M21-01). 이 스포너가 시작점을 맡지 않으면 0. */
	UFUNCTION(BlueprintPure, Category = "AnimalSpread")
	int32 GetLiveStarterCount() const;

	/**
	 * 시작점 순한 동물의 빈자리를 **같은 자리에** 다시 세운다 (M21-01). 서버에서만 동작한다.
	 * @return 이번에 세운 마리 수
	 */
	UFUNCTION(BlueprintCallable, Category = "AnimalSpread")
	int32 RefillStarters();

	/**
	 * 동물이 설 땅을 위에서 아래로 찾는다 (INBOX 결함 2026-10-07 14:40 「사냥터 공작이 오두막 지붕 위를 걷는다」).
	 * `Top`→`Bottom` 으로 그은 **첫 바닥**이 걸을 수 있는 면이고 `IsAnimalGroundFloor` 여야 한다 — 첫 바닥이
	 * 지붕·울타리·바위 윗면이면 그 아래 땅을 찾지 않고 false(지붕 밑 = 집 안이라 거기 세우면 안 된다).
	 * @param OutImpact 바닥 닿은 점 (캡슐 높이는 안 더함)
	 */
	static bool FindAnimalGround(const UWorld* World, const FVector& Top, const FVector& Bottom, const AActor* Ignore,
		FVector& OutImpact);

	/**
	 * 이 바닥 부품이 동물이 설 땅인가 — 가로·세로 둘 다 배회 지름(`AAnimalCharacter::GetWanderRadiusCm` × 2)
	 * 이상. 배회 원이 안 들어가는 면(오두막 지붕·울타리·바위)은 땅이 아니다. 이름·태그가 아니라 크기로 가른다.
	 */
	static bool IsAnimalGroundFloor(const UPrimitiveComponent* Floor);

	/** 쉬는(Idle) 채로 땅 아닌 바닥(지붕 등)을 딛고 서 있는가. 싸움·도주·포획 중이거나 공중이면 false. */
	static bool IsStrandedOffGround(const AAnimalCharacter* Animal);

	/**
	 * 땅 아닌 바닥(지붕 등)에 서서 쉬는(Idle) 동물이면 `GroundLocation` 으로 옮기고 배회 집을 다시 잡는다.
	 * 싸움·도주·포획 중이거나 공중이면 안 건드린다. @return 옮겼으면 true
	 */
	static bool ReturnToGroundIfStranded(AAnimalCharacter* Animal, const FVector& GroundLocation);

	/** 이 스포너가 세운 동물 중 땅 아닌 바닥에 올라간 것을 땅으로 내린다 (재등장 주기마다). @return 내린 마리 수 */
	int32 ReturnStrandedAnimals();

	/**
	 * 시작점 순한 동물 자리 (월드 XY). 시작점에서 `RingCm` 떨어진 12방향을 **앞쪽부터** (0, +30, −30, +60 … 180°)
	 * 훑어 `IsUsable` 인 자리를 고르고, 이미 고른 자리와 `MinSpacingCm` 이상 떨어진 것만 남긴다.
	 * 월드가 없어도 도는 순수 함수라 2층이 거리·간격·바다 피하기를 이걸로 직접 잰다.
	 */
	static TArray<FVector2D> MakeStarterSpots(const FVector2D& Start, float StartYawDeg, int32 Count,
		float RingCm, float MinSpacingCm, TFunctionRef<bool(const FVector2D&)> IsUsable);

	/**
	 * 시작점에 세울 종 — `MakeSpawnPool` 중 **도주만 하는 종**(`EAnimalBehaviorKind::Flee`)만, 이름순.
	 * 처음 만난 동물에게 맞을 일이 없게 (DESIGN 3.1 「시작점 순한 동물 수」).
	 */
	static TArray<FName> MakeStarterPool(const UDataTable* AnimalTable, FName ZoneID);

	/**
	 * 영역 안에서 동물을 세울 수 있는 칸 목록 (영역 중심 기준 상대 좌표, cm).
	 * **월드가 없어도 도는 순수 함수**라 2층이 간격·금지 반경을 이걸로 직접 잰다.
	 * @param AreaHalfExtentCm    영역 반변 (행 값)
	 * @param MinSpacingCm        칸 간격 = 동물끼리 최소 간격 (행 값)
	 * @param PlayerStartClearCm  시작점 금지 반경 (행 값)
	 * @param ClearCenters        금지 반경의 중심들 (영역 중심 기준 상대 좌표 — 맵의 `APlayerStart` 들)
	 */
	static TArray<FVector2D> MakeSpreadCells(float AreaHalfExtentCm, float MinSpacingCm,
		float PlayerStartClearCm, const TArray<FVector2D>& ClearCenters);

	/**
	 * 이 스포너가 세울 종 목록 (동물 표 행 이름, **이름순**).
	 * 월드가 없어도 도는 순수 함수라 2층이 "자리표시가 섞였나" 를 이걸로 직접 센다.
	 * @param AnimalTable          볼 표 (`DT_AnimalTable`)
	 * @param ZoneID               이 스포너의 존(`FAnimalSpreadRow::ZoneID`) — `FAnimalRow::HabitatZoneID` 가 같은 행만 남는다 (M5-07e)
	 * @param bIncludePlaceholder  자리표시 종(`FAnimalRow::bPlaceholderSpecies` = 돼지·소·양)까지 넣을지
	 *                             — 검사 스위치 `-SnareStageAnimals` 를 줬을 때만 참 (사용자 지시 2026-09-23 16:55 ②)
	 */
	static TArray<FName> MakeSpawnPool(const UDataTable* AnimalTable, FName ZoneID, bool bIncludePlaceholder);

	/**
	 * 존이 맵 안 칸(`FZoneRow::RegionHalfExtentCm` > 0)이면 흩는 중심을 칸 가운데로, 반변을 칸 반변 이하로 맞춘다 (M12-00).
	 * 월드가 없어도 도는 순수 함수라 2층이 "격자가 존 경계 안인가" 를 이걸로 직접 잰다.
	 * @return 칸이 있는 존이라 맞췄으면 true (그때만 OutCenter 를 쓴다)
	 */
	static bool FitToZone(const FZoneRow& Zone, FAnimalSpreadRow& InOutRow, FVector2D& OutCenter);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimalSpread")
	TObjectPtr<UDataTable> SpreadTable;

	/** 볼 행 이름. 기본값을 두지 않는다 — 존이 늘면 맵에서 골라 적는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimalSpread")
	FName SpreadRowName;

private:
	/** 재등장 타이머가 부르는 자리. 하는 일은 `RefillArea()` 하나다. */
	void HandleRespawnTick();

	/** 맵의 `APlayerStart` 들을 영역 중심 기준 상대 좌표로. 금지 반경의 중심이 된다. */
	TArray<FVector2D> CollectPlayerStartCenters() const;

	/** 지금 서 있는 동물에게서 `MinSpacingCm` 이상 떨어진 칸만 남긴다. */
	TArray<FVector2D> CollectFreeCells() const;

	/** 칸 하나를 바닥에 내려 찍는다. 걸을 수 있는 바닥이 없으면 false — 그 칸은 버린다. */
	bool ProjectCellToGround(const FVector2D& Cell, FVector& OutLocation) const;

	/** 그 자리에 한 마리 세운다. 종은 동물 표의 행을 차례로 돌려 쓴다. */
	AAnimalCharacter* SpawnOneAnimal(int32 SlotIndex, const FVector& Location);

	/** 그 자리에 `AnimalRowName` 종 한 마리를 세운다 (흩는 동물·시작점 동물 공통). */
	AAnimalCharacter* SpawnAnimalOfKind(FName AnimalRowName, const FVector& Location);

	/**
	 * 이 스포너가 맡을 시작점을 고르고 자리를 정한다 (BeginPlay 한 번). 시작점이 제 존 안이고, 같은 일을 할
	 * 스포너(시작점 동물 수 > 0) 중 **시작점에 가장 가까운** 것만 맡는다 — 같은 존 스포너가 둘이라 둘 다 세우면 4마리다.
	 */
	void ChooseStarterSpots();


	UPROPERTY()
	FAnimalSpreadRow SpreadRow;

	bool bSpreadActive = false;

	// 이 스포너가 세운 동물들. 죽은 것도 남는다 — 빈자리 판정은 상태로 한다.
	UPROPERTY()
	TArray<TObjectPtr<AAnimalCharacter>> SpawnedAnimals;

	// 시작점 순한 동물 (M21-01) — 자리는 BeginPlay 에 한 번 정하고, 다시 설 때도 같은 자리다.
	TArray<FVector> StarterLocations;

	UPROPERTY()
	TArray<TObjectPtr<AAnimalCharacter>> StarterAnimals;

	UPROPERTY()
	TObjectPtr<AAnimalCharacter> StoryAnimal;

	/** 이야기 단계(또는 검사 스위치 `-SnareStageFogbuck`)가 이야기 동물을 부르는가. */
	bool IsStoryAnimalWanted() const;

	FTimerHandle RespawnTimerHandle;
};
