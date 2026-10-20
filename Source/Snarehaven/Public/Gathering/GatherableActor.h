#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/GatherableRow.h"
#include "GatherableActor.generated.h"

class UDataTable;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;

// ROADMAP M1-06: 맵에 깔아 두는 채집 오브젝트(베리 덤불·마른 나무). 종류별 차이는 코드가 아니라
// `DT_GatherableTable` 의 행 하나로 정해진다 (DataDriven.md 1 — 채집물이 늘어도 코드는 안 는다).
//
// 베리 덤불·마른 나무는 한 번 채집하면 사라지고 **리스폰하지 않는다** (DESIGN 3.3 감독 확정).
// 나무·향나무(DESIGN 8.10)는 행의 `RespawnSeconds` 만큼 뒤에 다시 자라고, 손도끼(`RequiredToolItemID`)가
// 있어야 벨 수 있으며, 개수는 호스트가 [GrantCount, GrantCountMax] 에서 굴린다.
// 나무는 호스트가 맵의 장식 나무 자리에 복제(Replicated)로 세운다 — 누가 베면 모두에게 사라진다.
//
// 권위: 이 액터는 "무엇을 몇 개 주기로 되어 있는지" 만 알고, **보유 수량은 건드리지 않는다.**
// 실제 지급은 백엔드가 확정한다 (DESIGN 4.5 · CLAUDE.md 8) — 요청을 쌓는 건 `UGatherComponent` 를 통해
// `ASnarehavenCharacter` 가 한다. 맵에 손으로 깐 채집물도 복제한다(M13-02 존 채집 더미) — 누가 집으면 모두에게
// 사라지고, 게스트는 호스트에게 채집을 부탁한다(`UGatherComponent::Server_GatherTarget`).
UCLASS()
class SNAREHAVEN_API AGatherableActor : public AActor
{
	GENERATED_BODY()

public:
	AGatherableActor();

	/**
	 * `GatherableTable` 의 `GatherableRowName` 행을 읽어 줄 아이템·개수를 채운다.
	 * BeginPlay 가 부르고, 2층 검사는 테이블·행을 직접 넣고 부른다.
	 * @return 행을 찾아 초기화까지 성공했으면 true (개수가 미정(-1)이면 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "Gatherable")
	bool InitializeFromTable();

	/**
	 * 볼 데이터를 바꾼다. 맵에 깔 때 종류를 지정하거나 검사에서 바꿔 끼울 때 쓴다.
	 * @param Table   채집물 데이터 테이블 (FGatherableRow 행 구조체)
	 * @param RowName 그 안의 행 이름 (예: `Gatherable_BerryBush`)
	 */
	UFUNCTION(BlueprintCallable, Category = "Gatherable")
	void SetGatherableData(UDataTable* Table, FName RowName);

	/**
	 * 행 대신 값으로 바로 채운다. 2층 검사가 테스트 전용 값으로 부르는 자리다 (Verification.md 6).
	 * @param Row 채집물 한 줄
	 * @return 켜졌으면 true (개수가 미정(-1)이면 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "Gatherable")
	bool InitializeFromRow(const FGatherableRow& Row);

	/** 아직 채집할 수 있는지. 이미 채집했거나 줄 것이 미정이면 false (Verification.md 7). */
	UFUNCTION(BlueprintPure, Category = "Gatherable")
	bool CanGather() const;

	/**
	 * 이 도구를 든 사람이 지금 채집할 수 있는지. 도구가 필요 없는 행은 `CanGather` 와 같다 (DESIGN 8.10 손도끼).
	 * @param ToolItemID 도구 칸에 든 아이템 ID (맨손이면 None)
	 */
	UFUNCTION(BlueprintPure, Category = "Gatherable")
	bool CanGatherWithTool(FName ToolItemID) const;

	/** 이 채집물에 필요한 도구 (없으면 None). */
	UFUNCTION(BlueprintPure, Category = "Gatherable")
	FName GetRequiredToolItemID() const { return RequiredToolItemID; }

	/**
	 * 한 번 친다 (DESIGN 8.10 — 나무는 여러 번 쳐야 쓰러진다). 이번 번에 쓰러질 차례면 true 를 돌려주고
	 * 부른 쪽이 이어서 `Gather` 한다. 아직이면 친 쪽 반대로 살짝 흔들리기만 하고 false — 지급 요청도 없다.
	 * `HitsToFell` 이 1 이하인 행은 늘 true 다. 복제 나무면 호스트만 부른다(타격 수가 게스트에게 내려간다).
	 * @param HitterLocation 친 사람 자리 — 흔들림·넘어가는 쪽을 정한다
	 * @param bFellAtOnce 보너스 도구(전동 톱·전기 곡괭이)로 쳤으면 true — 남은 타격 수와 상관없이 이번 번에 쓰러진다
	 */
	bool TakeHit(const FVector& HitterLocation, bool bFellAtOnce = false);

	/** 쓰러지기까지 쳐야 하는 번 수 (1 이하면 한 번에) · 지금까지 친 번 수. */
	int32 GetHitsToFell() const { return HitsToFell; }
	int32 GetHitsTaken() const { return HitsTaken; }

	/** 넘어가는 연출이 도는 중인지 — 끝나면 몸이 숨는다. */
	bool IsFalling() const { return TiltMode == ETiltMode::Fall; }

	/** 들고 캐면 서버가 +1 해 주는 도구 (DESIGN 13.5 전기톱·마체테). 없으면 None. */
	FName GetBonusToolItemID() const { return BonusToolItemID; }

	/**
	 * 가중치 후보에서 하나를 고른다 (DESIGN 13.4). 굴림을 밖에서 넣어 2층 검사가 결과를 정할 수 있게 뺐다.
	 * @param Options 후보 (가중치 0 이하는 건너뛴다)
	 * @param Roll    [0, 가중치 합) 안의 수
	 * @return 고른 아이템 ID (후보가 없으면 None)
	 */
	static FName PickWeightedItem(const TArray<FGatherGrantOption>& Options, int32 Roll);

	/** 벤 뒤 다시 자라기까지 초 (0 이면 리스폰 없음). */
	UFUNCTION(BlueprintPure, Category = "Gatherable")
	float GetRespawnSeconds() const { return RespawnSeconds; }

	/** 개수 굴림 범위 (최소 = 최대면 고정). */
	int32 GetGrantCountMin() const { return GrantCountMin; }
	int32 GetGrantCountMax() const { return GrantCountMax; }

	/** 장식 나무를 몸으로 삼았는지 (못 찾으면 자리표시 큐브가 보인다). */
	AActor* GetAdoptedDecor() const { return AdoptedDecor.Get(); }

	/** 숨긴 줄기 충돌 원기둥 (행에 `TrunkCollisionCm` 이 없으면 늘 꺼져 있다). */
	UStaticMeshComponent* GetTrunk() const { return Trunk; }

	/** 보이는 몸 — 열매 덤불이 거둔 뒤에도 서 있는지 2층이 본다. */
	UStaticMeshComponent* GetBody() const { return Body; }

	/** 리스폰 타이머를 기다리지 않고 바로 다시 자라게 한다. 타이머 끝과 2층 검사가 부른다. */
	void Respawn();

	/**
	 * 호스트가 맵에 이미 깔린 장식 나무(행의 `DecorMeshPrefix` 메시를 쓰는 StaticMeshActor) 자리마다
	 * 복제되는 채집물을 세운다. 맵(.umap)을 고치지 않고 있는 나무를 벨 수 있게 하는 길이다 (DESIGN 8.10).
	 * @return 세운 개수
	 */
	static int32 SpawnAtDecorMeshes(UWorld* World);

	/**
	 * 숲 나무(폴리지·ISM 인스턴스 — 행의 `DecorMeshPrefix` 메시)는 수천 그루라 액터를 다 세우지 않는다.
	 * 호스트가 사람 곁(`GatherRangeCm` 의 몇 배 안)에 있는 인스턴스에만 복제 채집물을 세우고,
	 * 아무도 곁에 없고 안 베인 것은 스스로 사라진다. 채집물은 그 인스턴스를 몸으로 삼는다(벤 동안 배율 0).
	 * @param Center        사람 자리
	 * @param GatherRangeCm 채집 사거리 — 세우는 반경은 이것의 몇 배다(사거리 안에 들기 전에 게스트에게 내려가게)
	 * @return 이번에 새로 세운 개수
	 */
	static int32 SpawnAtInstancedTreesNear(UWorld* World, const FVector& Center, float GatherRangeCm);

	/** 몸으로 삼은 숲 나무 인스턴스 (없으면 nullptr / INDEX_NONE). */
	UInstancedStaticMeshComponent* GetAdoptedInstances() const { return AdoptedInstances.Get(); }
	int32 GetAdoptedInstanceIndex() const { return AdoptedInstanceIndex; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 채집한다. DESIGN 3.3 대로 진행바 없이 즉시이고, 성공하면 오브젝트는 사라진다
	 * (리스폰 행은 숨었다가 `RespawnSeconds` 뒤 다시 선다. 열매 덤불은 몸이 남고 열매만 없어진다).
	 * **아이템을 주지 않는다** — 무엇을 몇 개 주기로 되어 있는지만 확정하고, 요청은 부른 쪽이 쌓는다.
	 * @return 실제로 채집했으면 true (이미 채집했거나 줄 것이 미정이면 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "Gatherable")
	bool Gather();

	/** 외곽선 하이라이트(자동 상호작용 — DESIGN 3.3). 실제 외곽선은 아트 단계의 포스트프로세스 몫이다. */
	UFUNCTION(BlueprintCallable, Category = "Gatherable")
	void SetHighlighted(bool bHighlighted);

	UFUNCTION(BlueprintPure, Category = "Gatherable")
	FName GetGatherableID() const { return GatherableID; }

	/** 이 오브젝트가 주기로 되어 있는 아이템 ID. 지급은 백엔드 몫이다 (CLAUDE.md 8). */
	UFUNCTION(BlueprintPure, Category = "Gatherable")
	FName GetGrantItemID() const { return GrantItemID; }

	UFUNCTION(BlueprintPure, Category = "Gatherable")
	int32 GetGrantCount() const { return GrantCount; }

	/** 덤 아이템(나무의 수지)과 이번 번 굴린 개수 — 0 이면 이번엔 덤 없음 (DESIGN 8.10). */
	FName GetExtraItemID() const { return ExtraItemID; }
	int32 GetExtraCount() const { return ExtraCount; }

	UFUNCTION(BlueprintPure, Category = "Gatherable")
	bool IsGathered() const { return bGathered; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 어떤 채집물인지는 맵에 깔린 인스턴스마다 다르다 (engines/unreal.md 코드 규칙 1).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	TObjectPtr<UDataTable> GatherableTable;

	/** 볼 행 이름. 기본값을 두지 않는 이유: 두 종이 대등해서 한쪽을 기본으로 삼을 근거가 없다. */
	// 복제 나무는 게스트가 이 이름으로 같은 표 행을 읽어 도구·장식을 스스로 맞춘다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Gatherable")
	FName GatherableRowName;

private:
	// 아트가 들어오기 전까지 화면에 보이는 몸 역할을 하는 엔진 기본 큐브
	// (ASnarehavenCharacter 의 자리표시 실린더와 같은 이유 — 없으면 캡처에 아무것도 안 찍힌다).
	// 행의 `MeshRef` 가 채워지면 그 메시로 갈아 끼운다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gatherable", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Body;

	// 줄기만 막는 숨은 원기둥 — 가지·잎까지 막으면 동선이 묶인다. 벤 동안은 끈다 (M13-15).
	UPROPERTY(VisibleAnywhere, Category = "Gatherable", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Trunk;

	bool bHasTrunk = false;

	// 행에서 읽어 둔 값. 채집 순간에 테이블을 다시 뒤지지 않으려고 들고 있는다.
	FName GatherableID;
	FName GrantItemID;
	TArray<FGatherGrantOption> GrantOptions;
	int32 GrantCount = -1;
	int32 GrantCountMin = -1;
	int32 GrantCountMax = -1;
	FName ExtraItemID;
	int32 ExtraCountMax = 0;
	int32 ExtraCount = 0;
	float RespawnSeconds = 0.f;
	bool bKeepBodyWhenGathered = false;
	float GatheredVisualDelaySeconds = 0.f;
	FName RequiredToolItemID;
	FName BonusToolItemID;
	TArray<FName> AlsoToolItemIDs;
	FString DecorMeshPrefix;

	UFUNCTION()
	void OnRep_Gathered();

	UFUNCTION()
	void OnRep_HitsTaken();

	/** 흔들림(아직 남은 번)·넘어감(마지막 번) 연출. 몸이 무엇이든(숲 인스턴스·장식 나무·자기 몸) 뿌리를 축으로 기운다. */
	enum class ETiltMode : uint8 { None, Shake, Fall };
	void StartTilt(ETiltMode Mode);
	void TickTilt();
	void StopTilt();
	/** 뿌리를 축으로 `HitYawDeg` 쪽(친 사람 반대)으로 `AngleDeg` 만큼 기울인다. 0 이면 원래 자세. */
	void SetBodyTilt(float AngleDeg);

	/** 벤 상태를 눈에 반영한다 — 장식 나무가 있으면 그걸, 없으면 자리표시 몸을 숨긴다. */
	void ApplyGatheredVisual();
	/** 거둔 겉모습을 `GatheredVisualDelaySeconds` 뒤에 입힌다 — 손이 닿는 때에 열매가 없어지게. */
	void ScheduleGatheredVisual();
	/** 열매 칸(재질 1번 이후)을 잎 재질로 덮거나(true) 메시 원래 재질로 돌린다(false). */
	void SetFruitStripped(bool bStrip);
	void RollGrantCount();
	void AdoptDecorIfAny();

	/** 몸으로 삼은 인스턴스를 숨기거나(배율 0 — 인덱스가 밀리지 않게 지우지 않는다) 원래대로 되돌린다. */
	void SetInstanceHidden(bool bHide);

	/** 호스트: 안 베였고 곁에 사람이 없으면 사라진다 — 숲을 걸어 다녀도 액터가 쌓이지 않게. */
	void ReleaseIfUnwatched();

	UPROPERTY(ReplicatedUsing = OnRep_Gathered)
	bool bGathered = false;

	int32 HitsToFell = 0;

	// 호스트가 세고 게스트에게 내려준다 — 게스트도 칠 때마다 흔들리는 걸 본다. 다시 자라면 0.
	UPROPERTY(ReplicatedUsing = OnRep_HitsTaken)
	int32 HitsTaken = 0;

	// 마지막으로 친 사람 → 나무 쪽 수평 방향(도). 흔들림·넘어감이 이쪽으로 간다.
	UPROPERTY(Replicated)
	float HitYawDeg = 0.f;

	ETiltMode TiltMode = ETiltMode::None;
	float TiltElapsedSeconds = 0.f;
	float CurrentTiltDeg = 0.f;
	FTimerHandle TiltTimerHandle;
	// 장식 나무 원래 자세 — 기울였다가 되돌릴 때 쓴다 (숲 인스턴스는 `AdoptedInstanceTransform`).
	FQuat DecorRestRotation = FQuat::Identity;
	FQuat BodyRestRotation = FQuat::Identity;

	TWeakObjectPtr<AActor> AdoptedDecor;
	FTimerHandle RespawnTimerHandle;
	FTimerHandle GatheredVisualTimerHandle;

	// 숲 나무 인스턴스 몸 — 맵의 같은 인스턴스를 호스트·게스트가 각자 자리로 찾는다(인스턴스는 복제되지 않는다).
	TWeakObjectPtr<UInstancedStaticMeshComponent> AdoptedInstances;
	int32 AdoptedInstanceIndex = INDEX_NONE;
	FTransform AdoptedInstanceTransform;
	bool bInstanceHidden = false;

	// 호스트가 세운 인스턴스 채집물만 0 보다 크다 — 이 거리 안에 사람이 없으면 사라진다.
	float ReleaseRadiusCm = 0.f;
	FTimerHandle ReleaseTimerHandle;
};
