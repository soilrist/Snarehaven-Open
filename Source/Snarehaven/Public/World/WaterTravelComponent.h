#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "World/SnareSound.h"
#include "WaterTravelComponent.generated.h"

class ABoatActor;
class APawn;
class ASnarePlacedStructureActor;
class ASeaActor;
class UDataTable;
class UWaterActionWidget;
struct FWaterActivityRow;

// DESIGN 8.9 플레이어 쪽 물 — 수영(속도·스태미나·익사)과 구명보트(띄우기·타기·내리기·넣기).
//   · 물속 = 발이 바다 수면 아래이고 배에 타지 않았을 때. 매 틱 `UPlayerVitalsComponent::SetSwimState` 에 알린다
//     (스태미나 소모는 수영 전용 — 물 밖이면 아무 것도 안 뺀다, 3.1).
//   · 속도 배율은 캐릭터 `UpdateMoveSpeed` 가 읽는다: 운전 중 = 배 × 모터, 물속 = 수영, 그 밖 = 1.
//   · 배낭 차감·반환은 서버 `LaunchBoat`/`StowBoat` 가 판정한다. 성공 응답 뒤에만 호스트에게 배를 띄워 달라고 한다.
// 숫자는 전부 `DT_WaterActivityTable` 에서 온다.
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UWaterTravelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWaterTravelComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 물속 판정을 다시 하고 체력·스태미나 컴포넌트에 알린다. Tick 과 2층 검사가 같은 길을 쓴다. */
	void UpdateWaterState();

	bool IsInWater() const { return bInWater; }

	/** 월드의 바다 (없으면 nullptr). 캐릭터가 수영 몸 높이를 수면에 맞출 때 쓴다. */
	ASeaActor* GetSea() const;

	/** 걷기 속도에 곱할 배율. 운전 중 = 배 배율, 물속 = 수영 배율, 그 밖 = 1. */
	float GetMoveSpeedMultiplier() const;

	ABoatActor* GetRiddenBoat() const;
	bool IsDriving() const;

	// ── 배 (버튼이 부른다) ──
	bool CanLaunchBoat() const;
	void LaunchBoat();
	ABoatActor* FindBoardableBoat() const;
	bool CanBoard() const { return FindBoardableBoat() != nullptr; }
	void Board();
	bool CanLeave() const { return GetRiddenBoat() != nullptr; }
	void Leave();
	ABoatActor* FindStowableBoat() const;
	bool CanStow() const { return !bBusy && FindStowableBoat() != nullptr; }
	void Stow();

	/**
	 * 서버 `GetBaseState` 의 `boat.launched` (재시작 복원). 서버에 띄운 배가 남아 있으면 배낭에 구명보트가 없어도
	 * 띄우기를 열어 둔다 — 서버가 `already` 로 답하면 배낭 차감 없이 다시 세운다.
	 */
	void ApplyBackendBoat(bool bLaunched) { bServerBoatLaunched = bLaunched; }
	bool IsServerBoatLaunched() const { return bServerBoatLaunched; }

	/** 마지막 거절 사유 (서버 code 또는 한글 문장). 화면이 한 줄로 보여 준다. */
	const FString& GetLastMessage() const { return LastMessage; }

	/** 호스트: 이 폰 앞에 배를 띄우고 운전석에 앉힌다. 2층 검사가 직접 부른다. */
	ABoatActor* SpawnBoatOnAuthority(int32 MotorTier);

	/** 호스트: 가진 모터 → 단계. 전기 모터 2 · 모터 1 · 없음 0 (보유는 백엔드 스냅샷). */
	int32 ReadOwnedMotorTier() const;

	UFUNCTION(Server, Reliable)
	void Server_SpawnBoat(int32 MotorTier);

	UFUNCTION(Server, Reliable)
	void Server_Board(ABoatActor* Boat);

	UFUNCTION(Server, Reliable)
	void Server_Leave();

	UFUNCTION(Server, Reliable)
	void Server_DestroyBoat(ABoatActor* Boat);

	/**
	 * 서버 답 하나 → 배 소리 한 번 (DESIGN 8.9 「배 소리」, M51-03). 규칙은 `SnareSound::BoatCue` — 거절·오류 코드면 무음.
	 * 월드 소리(띄우기·넣기)는 호스트를 거쳐 `Where` 에서 모든 기기, 타기·내리기는 이 기기만.
	 */
	void PlayBoatCue(SnareSound::EBoatAction Action, bool bAccepted, const FString& ErrorCode, const FVector& Where);

	UFUNCTION(Server, Unreliable)
	void Server_PlayBoatCue(FName Cue, float Volume, FVector Where);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayBoatCue(FName Cue, float Volume, FVector Where);

	UFUNCTION(Client, Unreliable)
	void Client_PlayBoatCue(FName Cue, float Volume);

	/** 2층 시험이 읽는다 — 지금까지 낸 배 소리 수와 마지막 소리 이름. */
	int32 GetBoatCueCount() const { return BoatCueCount; }
	FName GetLastBoatCue() const { return LastBoatCue; }

protected:
	virtual void BeginPlay() override;

private:
	const FWaterActivityRow* GetRow() const;
	float GetInteractRangeCm() const;
	void BoardOnAuthority(ABoatActor* Boat);
	void LeaveOnAuthority();
	/** 호스트가 타기·내리기를 실제로 바꾼 뒤 그 사람 기기에 소리 한 번 (M51-04). */
	void PlayRideCueOnAuthority(SnareSound::EBoatAction Action);
	/** 이 기계가 조종하는 폰이면 물 버튼 위젯을 한 번 띄운다 — 손님은 컨트롤러가 늦게 붙어서 Tick 에서 본다. */
	void EnsureWidget();
	void ApplyTestTeleportOnce();
	/** 검사 스위치 `-SnareStandNear=<채집물 ID>` — 그 채집물이 맵에 설 때까지 기다렸다 가장 가까운 것 옆으로 한 번 옮긴다. */
	void ApplyTestStandNear(float DeltaTime);
	/** 검사 스위치 `-SnareHuntHerd` — 동물이 맵에 선 뒤 가까운 3~4마리를 폰 앞쪽으로 한 번 옮긴다 (`AAnimalCharacter::HuntHerdSpot`). */
	void ApplyTestHuntHerd(float DeltaTime);
	/** 같은 스위치의 `fishing.<spotId>` — 그 낚시 포인트를 던질 거리 안에 두는 가장 가까운 뭍에 포인트를 보고 선다. */
	void ApplyTestStandNearFishingSpot(APawn& Pawn, const FString& SpotId);
	/** 같은 스위치의 `animal.<동물 행 이름>` — 그 동물(빈 이름이면 아무 동물) 중 가장 가까운 것 곁에 그것을 보고 선다. */
	void ApplyTestStandNearAnimal(APawn& Pawn, const FString& RowName, float DeltaTime);
	/** 같은 스위치의 `ranch.<칸 번호>`·`vendor.<상점 종류>` — 목장 칸·상인(빈 값이면 아무 것) 중 가장 가까운 것 곁에 그것을 보고 선다. */
	void ApplyTestStandNearPlace(APawn& Pawn, const FString& WantedID, float DeltaTime);
	/** 같은 스위치의 `structure.<아이템 ID>` — 그 놓인 가구 곁에 그것을 보고 선다. 10초 안에 없으면 내 화면에만 하나 세운다. */
	void ApplyTestStandNearStructure(APawn& Pawn, FName ItemID, float DeltaTime);
	/** 목장 칸·상인·다른 가구에서 떨어진 땅에 복제 없는 가구 하나를 세운다 — 규칙 표가 없거나 땅이 없으면 nullptr. */
	ASnarePlacedStructureActor* SpawnLocalStructureForTest(APawn& Pawn, FName ItemID);
	/** 대상에서 지금 선 쪽으로 `GapCm` 떨어진 자리에 대상을 보고 선다 — 상황 버튼은 앞에 있는 것을 고른다. */
	void StandBesideForTest(APawn& Pawn, const AActor& Target, const FString& WantedID, float GapCm);

	/** 표를 붙잡아 둔다 — 행 포인터가 GC 로 날아가지 않게. */
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> WaterTable;

	UPROPERTY(Transient)
	TObjectPtr<ASeaActor> CachedSea;

	/** `WaterTable` 이 살아 있는 동안 유효. 표가 없거나 미정이면 nullptr — 수영·배가 꺼진다. */
	const FWaterActivityRow* CachedRow = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UWaterActionWidget> ActionWidget;

	bool bTestTeleportChecked = false;
	/** `-SnareStandNear=` 를 찾을 남은 초 — 음수면 아직 안 봤음, 0 이면 끝(옮겼거나 못 찾음). */
	float TestStandNearSecondsLeft = -1.f;
	/** `-SnareStandNearRepeat` — 지금 앞에 세운 채집물. 이게 베여 없어지면 다음 가까운 것 옆으로 다시 옮긴다. */
	TWeakObjectPtr<class AGatherableActor> TestStandNearTarget;
	/** `-SnareStandNearRepeat` — 근처에 채집물이 없던 연속 초와, 숲 안쪽으로 옮긴 횟수. */
	float TestStandNearNoneSeconds = 0.f;
	int32 TestStandNearHopCount = 0;
	/** `-SnareHuntHerd` 가 동물 모이기를 기다린 초 — 음수면 이미 끝(옮겼거나 포기). */
	float TestHuntHerdWaitedSeconds = 0.f;
	bool bInWater = false;
	/** 다음 헤엄 소리까지 남은 초 — 물속에서 움직이는 동안만 준다 (DESIGN 2.2 소리). */
	float SwimStrokeSecondsLeft = 0.f;
	bool bBusy = false;
	bool bServerBoatLaunched = false;
	FString LastMessage;
	int32 BoatCueCount = 0;
	FName LastBoatCue;
};
