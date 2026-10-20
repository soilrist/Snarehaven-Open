#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MountComponent.generated.h"

class UAnimSequence;
class UDataTable;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FAnimalRow;
struct FSnarePlayerSnapshot;
struct FWaterActivityRow;

// DESIGN 8.3 탈것 (M6-06 · M13-03) — 버튼 하나로 타고 내린다.
//   · 탈 수 있는지 = 스냅샷 `Mounts` 에 종이 하나라도 있나(결정 009 — 목줄을 서버 `ExchangeCollar` 로 바꾼 종).
//   · 탈 종 = 이번 판에 마지막으로 고른 종, 없으면 첫 종. 안장은 그 종에 서버가 달아 둔 것(`MountSaddles`), 없으면 맨등.
//   · 속도는 `DT_WaterActivityTable` 의 `MountSpeedMul` (걷기 × 배율). 캐릭터 `UpdateMoveSpeed` 가 읽는다.
//   · `bMounted` 는 호스트만 바꾸고 복제된다 — 모든 기계가 같은 값으로 `MaxWalkSpeed` 를 계산한다.
//   · 물속·배 위에선 못 타고, 타고 있다가 물에 들어가면 호스트가 내린다(8.9 수영이 이긴다).
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UMountComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMountComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	bool IsMounted() const { return bMounted; }

	/** 걷기 속도에 곱할 배율. 타 있으면 표 값, 아니면 1. */
	float GetMoveSpeedMultiplier() const;

	/** 못 타는 까닭 code. 빈 문자열 = 탈 수 있다. */
	FString GetMountBlockCode(const FSnarePlayerSnapshot& Snapshot) const;

	/** 버튼 한 개 — 타 있으면 내리고, 아니면 이 기계의 백엔드 스냅샷으로 판정해 탄다. */
	void ToggleMount();

	/** 스냅샷을 직접 받는 길. `ToggleMount` 와 2층 검사가 같이 쓴다. */
	void ToggleMountWith(const FSnarePlayerSnapshot& Snapshot);

	/** 탈 종 — 고른 종을 아직 가지고 있으면 그것, 아니면 첫 종. 하나도 없으면 None. */
	FName GetRideSpecies(const FSnarePlayerSnapshot& Snapshot) const;

	/** "탈것 바꾸기" 버튼 — 가진 종 안에서 다음 종으로 넘긴다(이번 판만 기억). */
	void CycleMountSpecies(const FSnarePlayerSnapshot& Snapshot);

	/** 종 이름(동물 표 `DisplayName`). 표에 없으면 ID 그대로. */
	FText GetSpeciesName(FName SpeciesId) const;

	/** 지금 탄 종(복제). 안 타 있으면 마지막으로 탔던 종. */
	FName GetMountSpecies() const { return MountSpeciesId; }

	/** 마지막 거절 code. 화면이 한 줄로 보여 준다. */
	const FString& GetLastMessage() const { return LastMessage; }

	UFUNCTION(Server, Reliable)
	void Server_SetMounted(bool bNewMounted, FName SpeciesId, FName SaddleId);

	/**
	 * 탄 모습을 지금 상태에 맞춘다 — 타 있으면 늑대 몸을 붙이고 탄 사람 몸을 그 등 높이로 올린다, 아니면 되돌린다.
	 * 크기는 탄 사람 몸에서 재므로 캐릭터가 몸을 새로 입힌 뒤(`ApplyPlayerBody`)에도 다시 부른다.
	 */
	void ApplyMountBody();

	/** 탈것 몸이 지금 트는 클립(대기·걷기·달리기). 안 타 있으면 nullptr. 2층 검사용. */
	UAnimSequence* GetMountBodyAnim() const { return MountBody ? MountAnim.Get() : nullptr; }

	/** 등에 얹힌 안장. 안 타 있으면 nullptr. */
	UStaticMeshComponent* GetSaddle() const { return Saddle; }

	/** 호스트가 낸 타기·내리기 소리 요청 수 (간격·같은 상태·무음 길을 거른 뒤). 검사용. */
	int32 GetMountCueCount() const { return MountCueCount; }

	/** 마지막으로 낸 소리 이름 (`MountOn`·`MountOff`). 검사용. */
	FName GetLastMountCue() const { return LastMountCue; }

protected:
	virtual void BeginPlay() override;

private:
	bool IsOnWater() const;
	const FAnimalRow* FindSpeciesRow(FName SpeciesId) const;
	/** `bQuiet` = 플레이어가 하지 않은 바꿈(`-SnareMountNow` 캡처) — 소리를 안 낸다 (M47-04). */
	void SetMountedOnAuthority(bool bNewMounted, bool bQuiet = false);
	void RequestMounted(bool bNewMounted, FName SpeciesId = NAME_None, FName SaddleId = NAME_None);

	/** 이동 속도로 탈것 몸 클립을 고른다 — 모든 기계가 복제된 속도로 같은 답을 낸다. */
	void UpdateMountBodyAnim();

	UFUNCTION()
	void OnRep_Mounted();

	/**
	 * 호스트가 탄/내린 소리를 정한다 (M47-04) — `SnareSound::MountCue`, 표의 간격(0.6초) 안 연타는 첫 한 번만.
	 * `OnRep_Mounted` 에서 내지 않는 까닭: 늦게 들어온 손님은 이미 탄 사람의 첫 복제에서도 OnRep 이 불려 「탐」 소리가 난다.
	 */
	void RequestMountCue();

	/** 그 플레이어 자리 월드 소리 — 장비 꺼내기(`Multicast_PlayChangeCue`)와 같은 꼴. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayMountCue(FName Cue, float Volume);

	double LastMountCueSeconds = -UE_BIG_NUMBER;
	int32 MountCueCount = 0;
	FName LastMountCue;

	UPROPERTY(ReplicatedUsing = OnRep_Mounted)
	bool bMounted = false;

	/** 탄 종·안장 — `bMounted` 보다 먼저 호스트가 적어 같은 묶음으로 복제된다. 몸·안장 모습만 바꾼다(효과 없음). */
	UPROPERTY(Replicated)
	FName MountSpeciesId;

	UPROPERTY(Replicated)
	FName MountSaddleId;

	/** 이 기계에서 고른 종. 서버에 안 적는다(🟡 결정 009). */
	FName ChosenSpecies;

	/** 표를 붙잡아 둔다 — 행 포인터가 GC 로 날아가지 않게. */
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> WaterTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> AnimalTable;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> MountBody;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Saddle;

	/** `SM_Mount_Saddle` — 생성자 하드 참조(쿠킹에 따라가게). */
	UPROPERTY()
	TObjectPtr<UStaticMesh> SaddleMesh;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> MountAnim;

	/** `AnimalTable` 이 살아 있는 동안 유효. 탈것 몸의 행(대기·걷기·도주 클립). */
	const FAnimalRow* MountRow = nullptr;

	/** 걷기 두 문턱 사이에서 지금 상태를 그대로 두려고 기억한다(캐릭터 `bBodyMoving` 과 같은 규칙). */
	bool bMountMoving = false;

	/** `WaterTable` 이 살아 있는 동안 유효. 표가 없거나 미정이면 nullptr — 탈것이 꺼진다. */
	const FWaterActivityRow* CachedRow = nullptr;

	FString LastMessage;
};
