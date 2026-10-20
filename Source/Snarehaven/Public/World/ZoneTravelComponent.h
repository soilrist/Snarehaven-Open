#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ZoneTravelComponent.generated.h"

class AZonePortalActor;
class UMeleeAttackComponent;
class USnareBackendSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnZonePortalTargetChangedSignature, AZonePortalActor*, NewTarget);

// ROADMAP M6-02b: 플레이어 쪽 포탈 상호작용. `UGatherComponent` 와 **같은 짜임**이다 — 사거리 안 가장
// 가까운 포탈 하나를 타이머로 골라 하이라이트하고, 화면 버튼이 그것을 누른다. 새 상호작용 틀을 만들지
// 않는다(ROADMAP M6-02b 문구). 사거리·갱신 주기도 새로 만들지 않고 `UMeleeAttackComponent` 에서
// 빌린다 — 자동 상호작용 거리는 DESIGN 3.1 확정값 하나뿐이라 두 군데로 갈리면 안 된다.
//
// **꺼진 포탈은 고르지 않는다.** 갈 곳이 확정된 포탈(`AZonePortalActor::CanTravel()`)만 대상이 되므로
// 버튼도 그때만 뜬다 (ROADMAP M6-02b 합격 기준).
//
// 권위: **존 이동은 서버가 한다.** 게스트가 스스로 맵을 열면 방이 갈라지므로(`docs/WORLD_ZONES.md` 표 B)
// 게스트는 `Server_TryTravelToNearest` 로 부탁만 하고, 호스트가 **자기 쪽에서 포탈을 다시 골라** 옮긴다
// (`UCaptureComponent::Server_TryCaptureTarget` 과 같은 규칙 — 인자가 없는 것이 의도다).
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UZoneTravelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UZoneTravelComponent();

	/**
	 * 사거리 안 가장 가까운 포탈을 다시 찾아 하이라이트를 옮긴다. 꺼진 포탈은 후보가 아니다.
	 * @return 새 대상 (없으면 nullptr)
	 */
	UFUNCTION(BlueprintCallable, Category = "Zone")
	AZonePortalActor* RefreshTarget();

	/** 지금 하이라이트된 포탈. 사냥 HUD 의 포탈 버튼 표시가 이걸 본다. */
	UFUNCTION(BlueprintPure, Category = "Zone")
	AZonePortalActor* GetPortalTarget() const { return CurrentTarget; }

	/** 포탈 버튼을 띄워도 되는지. 사거리 안에 **갈 곳이 확정된** 포탈이 있을 때만 참이다. */
	UFUNCTION(BlueprintPure, Category = "Zone")
	bool CanTravel() const;

	/**
	 * 포탈 버튼을 누른 순간. 호스트면 그 자리에서 방을 옮기고, 게스트면 호스트에게 부탁만 한다.
	 * 2층 검사도 이 함수를 직접 부른다 (입력 없이 같은 길을 타려고).
	 * @return 호스트에서 실제로 떠났으면 true. **게스트에서는 언제나 false** (요청만 보낸다)
	 */
	UFUNCTION(BlueprintCallable, Category = "Zone")
	bool TryTravelToNearest();

	/** 이 폰의 존 이동을 여기서 확정해도 되는지 (호스트인지). 액터에 안 붙었으면 권위로 본다. */
	UFUNCTION(BlueprintPure, Category = "Zone")
	bool HasTravelAuthority() const;

	/** 게스트 → 호스트 이동 요청. 포탈은 호스트가 자기 쪽 하이라이트로 다시 고른다 (인자 없음이 의도다). */
	UFUNCTION(Server, Reliable)
	void Server_TryTravelToNearest();

	/**
	 * 지금 쓰는 사거리(cm). 근접 공격 컴포넌트에서 빌려 온 값이라 여기 기본값이 없다.
	 * @return 사거리 (빌릴 곳이 없으면 -1 — 미정 취급이라 아무 포탈도 고르지 않는다)
	 */
	UFUNCTION(BlueprintPure, Category = "Zone")
	float GetTravelRangeCm() const;

	// ── DESIGN 8.14 선착장: 요금을 내고 건넌다 ────────────────────────────────────────
	// 화면 버튼은 `TryTravelToNearest` 가 아니라 이것을 부른다. **요금은 서버(`PayFerry`)가 받는다** —
	// 클라이언트는 요금 숫자를 모르고, 성공 답이 온 뒤에만 기존 이동 길(`TryTravelToNearest`)을 탄다.
	// 🟡 누른 사람 한 명이 내고 방 전원이 건넌다 (존 이동이 방 단위라서 — 나눠 내는 규칙은 설계서에 없다).

	/**
	 * 포탈 버튼을 누른 순간. 갈 곳을 `PayFerry` 의 `dest` 로 바꿔 묻고, 답이 성공일 때만 떠난다.
	 * 답을 기다리는 동안 다시 눌러도 두 번 묻지 않는다.
	 * @return 요청을 띄웠으면 true (떠났는지가 아니다 — 떠나는 것은 답이 온 뒤다)
	 */
	UFUNCTION(BlueprintCallable, Category = "Zone")
	bool TryFerryToNearest();

	/** 요금 답을 기다리는 중인가. 버튼 글자가 이걸 본다. */
	bool IsFerryPending() const { return bFerryPending; }

	/** 마지막 요금 답의 실패 사유 (`코인이 모자란다 (5 필요, 2 있음)`). 성공·대상 변경 뒤에는 빈 값. */
	const FText& GetFerryMessage() const { return FerryMessage; }

	/**
	 * 요금을 물을 백엔드를 정한다. 평소에는 비워 두면 게임 인스턴스에서 찾고,
	 * 2층 검사는 게임 인스턴스 없는 월드라 직접 넣는다 (`UCodexScreenWidget::BindToBackend` 와 같은 문).
	 */
	void SetFerryBackend(USnareBackendSubsystem* Backend) { FerryBackend = Backend; }

	/**
	 * 존 ID → `PayFerry` 의 `dest` 글자. **요금표가 아니라 서버 규약의 이름표**다 (`cloudscript_main.js`
	 * `FERRY_FARES` 의 키). 모르는 존이면 빈 문자열 — 그때는 묻지도 떠나지도 않는다.
	 */
	static FString GetFerryDestKey(FName ZoneID);

	/** 하이라이트된 포탈이 바뀔 때. 사냥 HUD 가 이걸 듣고 버튼을 폈다 접는다. */
	UPROPERTY(BlueprintAssignable, Category = "Zone")
	FOnZonePortalTargetChangedSignature OnZonePortalTargetChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 호스트에서만 도는 실제 이동. `TryTravelToNearest` 와 `Server_TryTravelToNearest` 가 여기로 모인다. */
	bool TryTravelOnAuthority();

	/**
	 * 같은 액터에 붙은 근접 공격 컴포넌트 (사거리·갱신 주기를 거기서 빌려 쓴다).
	 * 캐싱하지 않고 쓸 때마다 찾는 이유는 `UGatherComponent::FindMeleeAttack` 과 같다 —
	 * 컴포넌트 등록 순서에 기대지 않고, 런타임에 떼었다 붙여도 낡은 포인터를 보지 않기 위해서다.
	 */
	UMeleeAttackComponent* FindMeleeAttack() const;

	AZonePortalActor* FindNearestPortalInRange() const;
	void SetTarget(AZonePortalActor* NewTarget);
	void HandleTargetScan();

	// 액터를 원시 포인터로 들고 있으면 파괴돼도 눈치채지 못한다 — GC 가 아는 포인터로 든다.
	UPROPERTY(Transient)
	TObjectPtr<AZonePortalActor> CurrentTarget;

	FTimerHandle TargetScanTimerHandle;

	/** 요금을 물을 백엔드. 비었으면 게임 인스턴스 서브시스템을 쓴다. */
	USnareBackendSubsystem* FindFerryBackend() const;

	UPROPERTY(Transient)
	TObjectPtr<USnareBackendSubsystem> FerryBackend;

	bool bFerryPending = false;
	FText FerryMessage;
};
