#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ZoneEffectComponent.generated.h"

class UDataTable;
struct FZoneRow;

// DESIGN 1.3 존 특성 (M12-01) — 플레이어가 선 맵 안 칸(`FZoneRow::RegionHalfExtentCm` > 0)의 특성을 몸에 건다.
//   · 설원: 매 틱 `UPlayerVitalsComponent::SetZoneHealthDrain(HealthDrainPerSecond)` — 칸 밖이면 0.
//     `UWaterTravelComponent` 처럼 모든 기계가 돈다(체력 컴포넌트가 기계마다 따로 흐른다).
//   · 섬(`bChargesEntryFee`): 이 기계가 조종하는 폰이 칸에 처음 들어서면 서버 `PayFerry` 를 묻는다.
//     **요금 숫자·차감은 서버**다 (CLAUDE.md 8). 거절이면 마지막 칸 밖 자리로 되돌린다.
//   · 늪(`bBuildBlocked`)은 여기서 안 본다 — `USnareBaseBuildComponent::IsBuildAllowedAt` 이 설치 자리로 본다.
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UZoneEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UZoneEffectComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 지금 자리의 칸을 다시 보고 특성을 건다. Tick 과 2층 검사가 같은 길을 쓴다. */
	void UpdateZoneEffects();

	/** 마지막으로 본 칸 존 ID. 칸 밖이면 None. */
	FName GetCurrentRegionID() const { return CurrentRegionID; }

	/** 마지막 입장료 거절 문구 (서버 code → `DT_EntryRejectTable`). 받아들여졌으면 빈 글자. */
	const FText& GetLastMessage() const { return LastMessage; }

	/** 2층 검사 전용 — `DT_ZoneTable` 대신 검사 전용 칸 표를 쓴다 (BeginPlay 뒤에 부른다). */
	void SetZoneTableForTest(UDataTable* Table) { ZoneTable = Table; }

	/** 거절당한 손님이 호스트에게 "칸 밖으로 되돌려 달라" 고 한다 — 자기 몸만 옮긴다. */
	UFUNCTION(Server, Reliable)
	void Server_PushOut(FVector To);

protected:
	virtual void BeginPlay() override;

private:
	void RequestEntryFee(const FZoneRow& Zone);
	void PushOut();

	/** 표를 붙잡아 둔다 — 행 포인터가 GC 로 날아가지 않게. */
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> ZoneTable;

	FName CurrentRegionID;

	// ponytail: 입장료는 "이 맵에 머무는 동안 칸마다 한 번" — 맵을 옮기면 컴포넌트가 새로 생겨 다시 낸다.
	// 원본처럼 배를 탈 때마다 받아야 하면 칸을 나갈 때 이 집합에서 뺀다.
	TSet<FName> PaidRegionIDs;
	bool bFeePending = false;
	float RoofCheckTimer = 0.f;
	bool bColdUnderRoof = false;
	FVector LastFreeLocation = FVector::ZeroVector;
	bool bHasFreeLocation = false;
	FText LastMessage;
};
