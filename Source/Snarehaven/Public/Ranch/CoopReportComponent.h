#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CoopReportComponent.generated.h"

class FJsonObject;

/** 서버 `ReportCoop` 답이 바뀌었을 때 (같이 하기가 켜졌거나 꺼졌을 때). 목장 HUD 가 듣는다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCoopStateChangedSignature);

/** 한 번의 보고에 실을 것. `bSend` 가 거짓이면 보내지 않는다. */
struct FSnareCoopReport
{
	bool bSend = false;
	FString HostId;
	TArray<FString> Peers;
};

/** 방 안 몸 하나가 복제로 알려 준 것 — `BuildReport` 의 입력. */
struct FSnareCoopMember
{
	FString PlayFabId;
	bool bHost = false;
};

// M21-06b2 같이 하기 보고 (DESIGN 3.2 「같이 하기 생산」·「같이 하기 보고 유효」).
//   · 내 몸이면 로그인한 PlayFabId 를 Server RPC 로 올린다 → 복제(`ReportPlayFabId`). 호스트 몸인지는 서버가 정한다.
//   · 보고 주기마다 월드의 다른 몸 id 를 모아 서버 `ReportCoop {hostId, peers}` 를 부른다.
//     혼자·로그인 안 함·호스트 id 모름이면 안 보낸다.
//   · 생산을 당기는 것도, 몇 명인지 세는 것도 서버다 — 여기 남는 것은 서버 답 `coop` 하나뿐(CLAUDE.md 8).
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UCoopReportComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCoopReportComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 보고 간격(초) — DESIGN 3.2 「같이 하기 보고 유효」 120초의 절반. */
	static float GetReportIntervalSeconds();

	/** 지금 보낼 때인가. 한 번도 안 보냈으면(`LastReportSeconds` < 0) 바로. */
	static bool ShouldReportNow(double NowSeconds, double InLastReportSeconds);

	/**
	 * 방 안 몸들로 보고 하나를 짓는다. 내 것·빈 id·같은 id 두 번은 peers 에서 뺀다.
	 * @param MyId    로그인한 내 PlayFabId (비면 안 보낸다)
	 * @param Members 월드의 몸들(내 몸 포함 가능)
	 */
	static FSnareCoopReport BuildReport(const FString& MyId, const TArray<FSnareCoopMember>& Members);

	/** 보고 하나를 서버 `ReportCoop` 인자 `{hostId, peers}` 로 옮긴다. */
	static TSharedRef<FJsonObject> MakeArgs(const FSnareCoopReport& Report);

	/** 서버 답에서 `coop` 을 읽는다. 실패·형식 틀림이면 거짓. */
	static bool ReadCoopReply(bool bOk, const TSharedPtr<FJsonObject>& Response);

	/** 복제된 이 몸의 PlayFabId. 아직 안 올렸으면 빈 문자열. */
	const FString& GetReportPlayFabId() const { return ReportPlayFabId; }

	/** 이 몸이 방 호스트(리슨 서버)의 몸인가 — 서버가 정해 복제. */
	bool IsHostPawn() const { return bHostPawn; }

	/** 마지막 서버 답이 「같이 하기」였나. */
	bool IsCoopActive() const { return bCoopActive; }

	/**
	 * 한 번 돈다 — 내 몸이 아니면 아무것도 안 한다. id 를 아직 안 올렸으면 올리고, 주기가 됐으면 보고한다.
	 * 타이머와 2층 검사가 같이 쓴다.
	 * @param NowSeconds 월드 시각(초)
	 */
	void Poll(double NowSeconds);

	/**
	 * 서버 답 하나를 반영한다 — 실제 왕복도 검사 주입도 이 자리로 들어온다.
	 */
	void ApplyReply(bool bOk, const TSharedPtr<FJsonObject>& Response);

	UPROPERTY(BlueprintAssignable, Category = "Coop")
	FOnCoopStateChangedSignature OnCoopStateChanged;

private:
	UFUNCTION(Server, Reliable)
	void Server_SetPlayFabId(const FString& InPlayFabId);

	/** 이 몸이 이 기계에서 조종하는 몸인가. */
	bool IsMine() const;

	/** 월드의 몸들에서 복제된 id·호스트 표시를 모은다. */
	TArray<FSnareCoopMember> GatherMembers() const;

	void SetCoopActive(bool bActive);

	void HandlePollTimer();

	/** 손님 폰이면 방장 거점 나무(과일나무 8.18 친구 나무)를 세워 둔다. */
	void SyncFriendTree();

	UPROPERTY(Replicated)
	FString ReportPlayFabId;

	UPROPERTY(Replicated)
	bool bHostPawn = false;

	/** 마지막으로 Server RPC 로 올린 id — 같은 id 를 또 올리지 않는다. */
	FString SentPlayFabId;

	double LastReportSeconds = -1.0;
	bool bCoopActive = false;
	bool bReportInFlight = false;

	FTimerHandle PollTimer;
};
