#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TapFishingComponent.generated.h"

class AFishingSpotActor;
class UDataTable;
struct FWaterActivityRow;
struct FItemRow;
class UAudioComponent;
class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnFishCatchPopup, const FText& /*Popup*/);

UENUM(BlueprintType)
enum class ETapFishingState : uint8
{
	Idle,
	/** 던졌다 — 입질을 기다린다. 움직이면 취소. */
	WaitingBite,
	/** 입질! 서버 `RollFish` 응답을 기다린다 (! 표시). */
	Rolling,
	/** 연타 중. */
	Tapping,
	/** 다 끌어왔다 — 서버 `CatchFish` 응답을 기다린다. */
	Catching
};

// DESIGN 8.13 직접 낚시 — 던지기 → 입질 대기 → ! → 연타 → 잡힘.
//   · 입질 대기는 **호스트가 굴린다**(6~12초 ÷ 낚싯대×모자 배율, 지렁이면 ×0.9). 손님은 `Server_RequestBite` 로 부탁만.
//   · 입질 순간 서버 `RollFish(spotId, bait)` 가 어종·일회용 표를 준다. 표가 없거나 어종이 그 자리 서식지가 아니면
//     **`CatchFish` 를 부르지 않고** 끝낸다(서버 표와 클라 표가 어긋났다는 뜻이다).
//   · 연타가 목표에 닿으면 `CatchFish(token)` — 지급은 서버 응답 뒤 스냅샷 새로고침으로만 화면에 뜬다 (CLAUDE.md 8).
//   · 연타 성공 여부는 재료 단이라 호스트 말을 믿는다(8.13 "어종 결정" 근거 칸).
// 숫자는 전부 `DT_WaterActivityTable`, 어종 낚시 시간·서식지는 `DT_FishingTable`.
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UTapFishingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTapFishingComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 낚싯대를 들었고, 400cm 안에 잡을 수 있는 포인트가 있고, 쉬는 중일 때. */
	bool CanCast() const;

	/** 던질 수 있는 거리(cm) — 표 값 그대로, 표가 없으면 0. 낚싯대를 아직 안 든 HUD 도 이 거리로 포인트를 본다 (M15-08). */
	float GetCastRangeCm() const;

	/** 낚싯대를 들지 않았어도 던질 거리 안에 잡을 수 있는 포인트가 있는지 (M15-08 도구 자동 고르기). */
	bool HasSpotInCastRange() const;

	/** 던지기 버튼. */
	void CastAtNearest();

	/** 입질 대기를 시작한다. 호스트 굴림이 끝난 뒤 이 폰 주인 쪽에서 불린다. 2층 검사도 직접 부른다. */
	void BeginBiteWait(AFishingSpotActor* Spot, float WaitSeconds, bool bWormBait);

	/** 시간을 흘린다. Tick 과 2층 검사가 같은 길을 쓴다. */
	void AdvanceFishing(float DeltaSeconds);

	/** 감기 버튼 한 번. */
	void Tap();

	/** 그만두기 버튼 (연타 중) · 입질 전 취소. */
	void Quit();

	ETapFishingState GetState() const { return State; }
	/** 몸 클립용 — 주인은 자기 단계, 다른 기계는 복제된 단계. 보이는 것만이고 판정엔 안 쓴다. */
	ETapFishingState GetShownState() const { return ShownState; }
	float GetProgress() const { return Progress; }
	float GetGoal() const;
	int32 GetPressCount() const { return PressCount; }
	/** 몸 클립용 누름 수 — 주인은 자기 연타, 다른 기계는 복제된 값. 바뀔 때마다 감기가 잠깐 빨라진다. 판정엔 안 쓴다. */
	uint8 GetShownPressCount() const { return ShownPressCount; }
	int32 GetPressesNeeded() const { return PressesNeeded; }
	float GetBiteRemainingSeconds() const { return BiteRemainingSeconds; }
	/** ! 를 띄울 때 — 입질 순간부터 첫 연타 전까지. */
	bool ShowsBiteAlert() const { return State == ETapFishingState::Rolling || (State == ETapFishingState::Tapping && PressCount == 0); }
	FName GetLastCaughtItem() const { return LastCaughtItem; }
	/** 서버 `CatchFish` 확정 뒤 띄운 글 「+1 <물고기 이름>」 — 판마다 비운다. 검사·로그용. */
	const FText& GetLastCatchPopup() const { return LastCatchPopup; }

	/** 서버가 잡은 것을 확정한 뒤에만 한 번 — HUD 가 채집 획득 글과 같은 알림 줄에 띄운다 (M28-01). */
	FOnFishCatchPopup OnCatchPopup;
	const FString& GetLastMessage() const { return LastMessage; }
	/** 릴 감는 되풀이 소리가 지금 나고 있는가. 검사용. */
	bool IsReelLoopPlaying() const;
	/** 이번 낚시판에서 낚싯대 행 칸으로 낸 소리 이름을 순서대로 (던지기→입질→감기, M16-06). 검사·플레이 로그용. */
	const TArray<FName>& GetRodSoundLog() const { return RodSoundLog; }
	const FString& GetCurrentFishItemId() const { return FishItemId; }

	/** 호스트: 장비 배율(낚싯대 × 모자). 낚싯대가 없으면 0. */
	float ReadGearSpeedMul() const;

	UFUNCTION(Server, Reliable)
	void Server_RequestBite(AFishingSpotActor* Spot, bool bWormBait);

	UFUNCTION(Client, Reliable)
	void Client_StartBiteWait(AFishingSpotActor* Spot, float WaitSeconds, bool bWormBait);

	UFUNCTION(Server, Reliable)
	void Server_ReportSpotCatch(AFishingSpotActor* Spot);

	/** 손님 주인 → 호스트: 보이는 낚시 단계만 알린다(다른 사람 화면의 몸 클립). 보상·판정과 무관. */
	UFUNCTION(Server, Reliable)
	void Server_SetShownState(ETapFishingState NewState, FVector_NetQuantize10 CastTarget);

	/** 손님 주인 → 호스트: 보이는 누름 수만 알린다(남의 화면 감기 가속). 빠져도 한 박자 느려 보일 뿐이라 Unreliable. */
	UFUNCTION(Server, Unreliable)
	void Server_SetShownPressCount(uint8 NewCount);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 3층 검사 스위치 `-SnareFishTap=<0~1>` 전용 — 서버(`RollFish`) 없이 연타 화면을 만들어 붙들어 둔다.
	 * 발밑 진행바(M15-19)를 한 장에 담으려면 연타 중인 몸이 있어야 하는데, `-game` 한 판에는 백엔드가 없어
	 * 정상 흐름이 입질에서 끝난다. **보이는 것만이고 지급·판정은 타지 않는다**(`CatchFish` 를 안 부른다).
	 */
	void HoldTestTapping(float Fraction);

	/**
	 * 찌·낚싯줄을 보이는 단계(`ShownState`)대로 옮긴다 — 주인·남의 화면 같은 길. Tick 과 2층 검사가 부른다.
	 * 던지기 → 포인트 물 위 착수 → 둥실 → 감기 동안 물가 쪽으로 → 잡으면 발밑으로 튀어 들어오고 숨는다.
	 */
	void UpdateBobberAndLine(float DeltaSeconds);
	/** 검사·HUD(! 말풍선 자리)용. 컴포넌트는 BeginPlay 에서 만든다. */
	UStaticMeshComponent* GetBobberMesh() const { return BobberMesh; }
	/** 줄 토막들 — 대 끝에서 찌까지 차례로(처진 곡선). */
	const TArray<TObjectPtr<UStaticMeshComponent>>& GetLineMeshes() const { return LineMeshes; }
	/** 든 낚싯대 메시 끝(+Z 끝) 세계 위치 — 낚싯대가 없으면 머리 높이. */
	FVector GetRodTipLocation() const;
	/** 찌가 내려앉을 물 위 점 — 주인은 포인트 위치, 남의 화면은 복제된 값. */
	FVector GetShownCastTarget() const { return ShownCastTarget; }

	// ── 찌·줄 보이는 숫자 (DESIGN 8.13 「찌·낚싯줄」 행, 게임 규칙 숫자가 아니다) ──
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float BobberFlightSeconds = 0.3f;
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float BobberArcHeightCm = 80.f;
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float BobberDiameterCm = 14.f;
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float BobberFloatAmplitudeCm = 1.5f;
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float BobberFloatPeriodSeconds = 1.6f;
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float BobberBiteDipCm = 4.f;
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float BobberMaxPull = 0.15f;
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float BobberRemotePullSeconds = 6.f;
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float BobberHopSeconds = 0.25f;
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float LineDiameterCm = 2.5f;
	/** 줄 토막 수 — 처진 곡선을 원통 여러 개로 잇는다. */
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	int32 LineSegments = 8;
	/** 대기 중 줄 가운데가 처지는 깊이 = 줄 길이 × 이 값. 감기 중엔 `LineReelSagRatio`(팽팽). */
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float LineSagRatio = 0.12f;
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float LineReelSagRatio = 0.03f;
	/** 대기·입질·감기 동안 대 끝을 수평에서 이만큼 위로 세운다(°). 든 자세 클립은 대를 눕혀 줄이 땅에 끌렸다. */
	UPROPERTY(EditAnywhere, Category = "Fishing|Bobber")
	float RodAimPitchDegrees = 45.f;
	/** 입질 전 움직여도 되는 거리(cm) — 제자리 흔들림을 움직임으로 치지 않는 값. 게임 규칙 숫자가 아니다. */
	UPROPERTY(EditAnywhere, Category = "Fishing")
	float CancelMoveToleranceCm = 10.f;

protected:
	virtual void BeginPlay() override;

private:
	const FWaterActivityRow* GetRow() const;
	void RollBiteOnAuthority(AFishingSpotActor* Spot, bool bWormBait);
	void StartRolling();
	void StartCatching();
	void EndFishing(const FString& Why);
	bool HoldsLuckyRod() const;
	/** 확정된 어종을 「+1 이름」 으로 알린다 — 이름을 모르면 ID 그대로. */
	void AnnounceCatch(FName Caught);

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> WaterTable;

	/** 잡은 물고기 이름(`DisplayName`)을 찾는 아이템 표. */
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> ItemTable;

	const FWaterActivityRow* CachedRow = nullptr;

	/** 단계를 바꾸는 유일한 길 — 보이는 단계도 같이 맞추고 손님이면 호스트에 알린다. */
	void SetState(ETapFishingState NewState);

	ETapFishingState State = ETapFishingState::Idle;

	/** 다른 기계가 몸 클립을 고르는 단계. 주인은 스스로 정하므로 받지 않는다(COND_SkipOwner). */
	UPROPERTY(Replicated)
	ETapFishingState ShownState = ETapFishingState::Idle;

	/** 다른 기계가 감기 가속을 여는 누름 수 — 누를 때만 바뀌어 매 틱 복제되지 않는다. 연타 시작마다 0 (COND_SkipOwner). */
	UPROPERTY(Replicated)
	uint8 ShownPressCount = 0;

	/** 찌가 내려앉을 물 위 점 — 던질 때 주인이 정하고 남의 화면엔 이 값만 간다(COND_SkipOwner). */
	UPROPERTY(Replicated)
	FVector_NetQuantize10 ShownCastTarget = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BobberMesh;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> LineMeshes;
	void CreateBobberAndLine();
	/** 낚시 중엔 대 끝을 위로 세우고, 쉬면 쥐는 각으로 되돌린다. 손잡이(메시 원점 = 손목)를 축으로 돈다. */
	void AimRod(bool bAim);
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> AimedRod;
	FQuat RodRestRelative = FQuat::Identity;
	/** 찌가 마지막으로 그려진 단계·그 단계에서 흐른 시간·그 단계로 들어올 때 찌 자리. */
	ETapFishingState DrawnState = ETapFishingState::Idle;
	float DrawnStateSeconds = 0.f;
	FVector BobberFrom = FVector::ZeroVector;
	/** 잡은 뒤 발밑으로 튀어 드는 중(Catching 이 서버 응답으로 곧장 Idle 이 돼도 끝까지 보인다). */
	bool bHopping = false;

	UPROPERTY(Transient)
	TObjectPtr<AFishingSpotActor> CurrentSpot;

	FVector CastLocation = FVector::ZeroVector;
	bool bUsingWorm = false;
	float BiteRemainingSeconds = 0.f;
	float Progress = 0.f;
	float ProgressPerPress = 0.f;
	float SecondsAtZero = 0.f;
	int32 PressCount = 0;
	int32 PressesNeeded = 0;
	FString Token;
	/** 서버 `RollFish` 의 bait 인자 — "worm" · "berry" · 빈 칸(맨 바늘). */
	FString BaitArg;
	FString FishItemId;
	FName LastCaughtItem;
	FText LastCatchPopup;
	FString LastMessage;
	TArray<FName> RodSoundLog;

	/** 든 낚싯대 행의 소리 칸 하나를 튼다 (`Pick` 이 어느 칸인지 고른다). 도구 ID 를 가르지 않고 표만 읽는다. */
	void PlayRodCue(FName FItemRow::* Pick, const TCHAR* Phase, float Volume);

	/** 입질부터 끝날 때까지 릴 감는 소리(`ReelSoundCue`)를 되풀이한다 — 끝나면 `EndFishing` 이 끈다 (M16-06). */
	void StartReelLoop();
	void StopReelLoop();

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ReelLoop;
	/** 응답이 늦게 와도 지난 판에 끼어들지 않게 판마다 올린다. */
	int32 Attempt = 0;
	/** `-SnareFishTap=` 로 붙들어 둔 몫 (음수 = 안 씀). 검사 전용이라 복제하지 않는다. */
	float TestHoldFraction = -1.f;

	/** 3층 검사 스위치 `-SnareFishWait` — 순간이동 뒤 가까운 포인트에 던져 입질 대기에 붙들어 둔다(입질이 안 와 `RollFish` 를 안 부른다). */
	bool bTestHoldWait = false;
};
