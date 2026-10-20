#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GatherComponent.generated.h"

class AGatherableActor;
class UMeleeAttackComponent;
struct FItemRow;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGatherTargetChangedSignature, AGatherableActor*, NewTarget);

/**
 * 호스트가 게스트의 채집 부탁을 물린 까닭 (M15-15). 멀티에서 복제 나무를 베는 판정은 호스트 자리에서 나는데,
 * 거기서 조용히 돌아서면 게스트 화면에는 **아무 일도 안 난 것**처럼 보인다 (감독 조작 조사 #9).
 */
UENUM(BlueprintType)
enum class EGatherRejectReason : uint8
{
	/** 호스트 자리에서 재 보니 사거리 밖 — 게스트 화면이 먼저 움직였거나 지연으로 어긋났다. */
	TooFar,
	/** 요구 도구를 안 들었다 (호스트가 보는 게스트 장비 기준). */
	NeedTool,
	/** 먼저 누른 사람이 가져갔다 — 또는 다시 자라는 중이다. */
	AlreadyTaken,
};

// 듣는 쪽(캐릭터)에 넘기는 것은 **이미 사람 말로 바뀐 한 줄**이다 — 까닭마다 다른 문구를 화면 쪽에서
// 다시 고르면 같은 상황에 두 가지 말이 생긴다 (문구는 `GetRejectMessage` 한곳).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGatherRejectedSignature, const FText&, Message);
// ToolUses — 이 채집이 보너스 도구(DESIGN 13.5 전기톱·마체테)로 한 번이면 1. 서버가 가방을 보고 +1 을 정한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnGatheredSignature, FName, GatherableID, FName, ItemID, int32, Count, int32, ToolUses);

// ROADMAP M1-06: 플레이어 쪽 채집. DESIGN 3.3 "오브젝트 터치 → 즉시 획득" 그대로 —
// 조준이 없고, 사거리 안 **가장 가까운 채집물 하나**를 스스로 골라 하이라이트하고 집는다 (자동 상호작용).
//
// **사거리 숫자를 새로 만들지 않는다** — 자동 상호작용 하이라이트 거리는 이미 `UMeleeAttackComponent` 가
// 들고 있는 DESIGN 3.1 확정값(150cm)이고, 같은 버튼으로 동물과 채집물을 고르는 이상 거리가 두 군데로
// 갈리면 안 된다. 그래서 형제 컴포넌트에서 빌려 쓴다 (UCaptureComponent 가 대상을 빌려 쓰는 것과 같은 이유).
//
// 권위: 채집은 재화가 아니라 재료지만, **보유 수량은 여기서도 안 건드린다** (CLAUDE.md 8 · DESIGN 4.5).
// `OnGathered` 로 알리기만 하고 지급 요청을 쌓는 건 그걸 듣는 쪽(ASnarehavenCharacter)이다.
// 멀티 복제(Replicated)는 M3 조각이라 아직 붙이지 않았다.
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UGatherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGatherComponent();

	/**
	 * 사거리 안 가장 가까운 채집물을 다시 찾아 하이라이트를 옮긴다.
	 * @return 새 대상 (없으면 nullptr)
	 */
	UFUNCTION(BlueprintCallable, Category = "Gather")
	AGatherableActor* RefreshTarget();

	/**
	 * 채집 버튼을 누른 순간. 대상이 있으면 즉시 획득하고(진행바 없음 — DESIGN 3.3) 알린다.
	 * 2층 검사도 이 함수를 직접 부른다 (입력 없이 같은 길을 타려고).
	 * @return 실제로 채집했으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Gather")
	bool TryGatherNearest();

	/** 지금 하이라이트된 채집물. 사냥 HUD(M1-08)의 버튼 표시가 이걸 본다. */
	UFUNCTION(BlueprintPure, Category = "Gather")
	AGatherableActor* GetGatherTarget() const { return CurrentTarget; }

	/** 채집 버튼을 켜도 되는지. 사거리 안에 아직 안 집은 채집물이 있을 때만 참이다. */
	UFUNCTION(BlueprintPure, Category = "Gather")
	bool CanGather() const;

	/**
	 * 지금 쓰는 사거리(cm). 근접 공격 컴포넌트에서 빌려 온 값이라 여기 기본값이 없다.
	 * @return 사거리 (빌릴 곳이 없으면 -1 — 미정 취급이라 아무것도 안 집는다)
	 */
	UFUNCTION(BlueprintPure, Category = "Gather")
	float GetGatherRangeCm() const;

	/** 하이라이트 대상이 바뀔 때. 사냥 HUD(M1-08)가 이걸 듣는다. */
	UPROPERTY(BlueprintAssignable, Category = "Gather")
	FOnGatherTargetChangedSignature OnGatherTargetChanged;

	/**
	 * 도구를 안 따지고 사거리 안 가장 가까운 채집물 — 사냥 HUD 가 "지금 어떤 도구를 줘야 하나" 를 묻는 길 (M15-08).
	 * 하이라이트 대상(`GetGatherTarget`)은 **이미 맞는 도구를 든 뒤에야** 서므로 그것만으로는 도구를 고를 수 없다.
	 */
	UFUNCTION(BlueprintPure, Category = "Gather")
	AGatherableActor* FindNearestNeedingTool() const;

	/** 도구 칸에 든 아이템 ID (맨손이면 None). 손도끼 나무(DESIGN 8.10)를 거르는 데 쓴다. */
	FName GetEquippedToolItemID() const;

	/** 채집에 성공했을 때. 지급 요청을 쌓는 건 이걸 듣는 쪽이다 (ASnarehavenCharacter). */
	UPROPERTY(BlueprintAssignable, Category = "Gather")
	FOnGatheredSignature OnGathered;

	/** 호스트가 내 채집 부탁을 물렸을 때 (M15-15). 화면에 한 줄 띄우는 건 이걸 듣는 쪽이다 (ASnarehavenCharacter). */
	UPROPERTY(BlueprintAssignable, Category = "Gather")
	FOnGatherRejectedSignature OnGatherRejected;

	/**
	 * 거절 까닭을 사람 말 한 줄로. 호스트가 물린 것과 로컬 "도구가 없다" 안내가 **같은 문구**를 쓰도록 여기 모았다 —
	 * 같은 상황에 화면마다 다른 말이 뜨면 사람이 같은 일로 안 본다.
	 * @param RequiredToolItemID 그 채집물이 요구하는 도구 (`NeedTool` 일 때만 본다)
	 */
	static FText GetRejectMessage(EGatherRejectReason Reason, FName RequiredToolItemID);

	/**
	 * 맞는 도구 없이 나무·광맥 앞에 섰을 때 공격 버튼 위에 **누르기 전부터** 띄우는 안내 한 줄 (INBOX 18:10 ②, DESIGN 3.3).
	 * 없으면 어디서 사는지(「손도끼가 필요 — 잡화점」), 가졌는데 안 들었으면 어떻게 드는지(「손도끼를 들어요 — 도구 버튼」).
	 * 손도끼·곡괭이 가르기는 `GetRejectMessage` 와 같은 기준(그 도구 행의 `UseAction`)이다.
	 */
	static FText GetNeedToolHint(FName RequiredToolItemID, bool bOwnsFittingTool);

	/** 그 도구가 캐는 도구(곡괭이, 행 `UseAction` Mine)인가 — 거절 문구·안내 줄·상황 버튼 「…필요」 글자가 같은 기준으로 가른다. */
	static bool IsMiningTool(FName RequiredToolItemID);

	/**
	 * 호스트가 마지막으로 물린 까닭 한 줄 (한 번도 안 물렸으면 빈 글).
	 * 2층 검사가 "게스트 화면에 무엇이 도착했나" 를 읽는 자리다 — HUD 없이도 실제로 온 문구를 본다.
	 */
	UFUNCTION(BlueprintPure, Category = "Gather")
	FText GetLastRejectMessage() const { return LastRejectMessage; }

	/**
	 * 게스트가 복제 나무를 베어 달라고 호스트에게 부탁한다. 호스트가 거리·도구를 다시 보고 베면
	 * `OnGathered` → 캐릭터의 `Client_QueueGatherGrant` 로 지급 요청이 돌아가고, 못 베면
	 * `Client_GatherRejected` 로 까닭 한 줄이 돌아간다 (M15-15).
	 * 2층 검사가 게스트 부탁을 직접 태우므로 공개다 (일반 길은 `TryGatherNearest` 가 부른다).
	 */
	UFUNCTION(Server, Reliable)
	void Server_GatherTarget(AGatherableActor* Target);

	/** 방금 `OnGathered` 로 알린 채집에 쓴 도구(`GetActingToolItemID`, 맨손이면 None) — 캐릭터가 베기/캐기/줍기 동작을 고른다 (DESIGN 8.8). */
	FName LastGatherToolItemID() const { return LastRequiredToolItemID; }

	/**
	 * 이 채집에 도구를 **썼나** — 필요한 도구(손도끼 나무·곡괭이 광맥)면 그것, 필요 없는 채집물이라도 든 도구가
	 * 그 채집물의 덤 도구(마체테 덤불, DESIGN 13.5)면 든 도구, 아니면 None(맨손 줍기).
	 * None 이 아니면 소리·몸 동작이 든 도구 행(`UseSoundCue`·`UseAction`)을 튼다 (DESIGN 3.3 도구 채집, M28-09a).
	 */
	static FName GetActingToolItemID(FName RequiredToolItemID, FName BonusToolItemID, FName HeldToolItemID);

	/** 지금 든 도구의 표 한 줄 (없으면 nullptr). 포인터는 그 자리에서만 읽는다. 채집 동작·소리가 이 행의 칸(`UseAction`·`UseSoundCue`)을 읽는다 (M16-01). */
	const FItemRow* FindEquippedToolRow() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/**
	 * 같은 액터에 붙은 근접 공격 컴포넌트 (사거리·갱신 주기를 거기서 빌려 쓴다).
	 * 캐싱하지 않고 쓸 때마다 찾는 이유는 `UCaptureComponent::FindMeleeAttack` 과 같다 —
	 * 컴포넌트 등록 순서에 기대지 않고, 런타임에 떼었다 붙여도 낡은 포인터를 보지 않기 위해서다.
	 */
	UMeleeAttackComponent* FindMeleeAttack() const;

	/**
	 * 사거리 안 가장 가까운 채집물.
	 * @param bRequireTool 참이면 지금 든 도구로 캘 수 있는 것만 (하이라이트용 — 못 캐는 것에 불이 들어오면
	 *                     눌러도 아무 일이 없는 버튼이 된다). 거짓이면 도구를 안 따진다.
	 */
	AGatherableActor* FindNearestGatherableInRange(bool bRequireTool = true) const;

	FName LastRequiredToolItemID;

	/** 호스트가 마지막으로 돌려준 거절 문구 (M15-15). */
	FText LastRejectMessage;

	/** 채집물 하나를 실제로 베고 알린다. 로컬 채집과 호스트가 받은 게스트 부탁이 같이 쓴다. */
	bool GatherTarget(AGatherableActor* Target);

	/** 호스트가 물린 까닭을 부탁한 사람 화면에만 돌려준다 (M15-15). */
	UFUNCTION(Client, Reliable)
	void Client_GatherRejected(EGatherRejectReason Reason, FName RequiredToolItemID);
	void SetTarget(AGatherableActor* NewTarget);
	void HandleTargetScan();

	// 액터를 원시 포인터로 들고 있으면 파괴돼도 눈치채지 못한다 — GC 가 아는 포인터로 든다.
	UPROPERTY(Transient)
	TObjectPtr<AGatherableActor> CurrentTarget;

	FTimerHandle TargetScanTimerHandle;
};
