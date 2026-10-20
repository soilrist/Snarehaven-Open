#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "SnareBlockListSubsystem.generated.h"

class APawn;
class FJsonObject;
class USnareBackendSubsystem;

// 신고·차단 (DESIGN 4.8 「신고 하루 상한」·「차단 목록 상한」, 앱스토어 1.2 — 사람 결정 10-16).
//
// 차단 목록은 서버 `blocks` 가 원본이다. 클라는 로그인 뒤 첫 보유 상태가 오면 `GetBlockList` 로 한 번 받아 두고,
// 차단·해제도 서버 답(`blocks`)을 그대로 옮긴다 — 목록을 클라가 혼자 더하거나 빼지 않는다.
// 차단은 **내 화면에서만** 그 사람 이름을 「차단한 플레이어」로 가리고 이모트 말풍선·소리를 숨긴다.
UCLASS()
class SNAREHAVEN_API USnareBlockListSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 신고 이유 — 서버 `REPORT_REASONS` 와 같은 순서. */
	static const TArray<FName>& GetReportReasons();

	/** 실패면 사유 문장, 성공이면 빈 글. */
	using FDone = TFunction<void(const FText& Failure)>;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 서버를 물을 백엔드. `Initialize` 가 같은 게임 인스턴스 것을 묶는다 — 2층 검사는 가짜를 직접 묶는다. */
	void BindBackend(USnareBackendSubsystem* InBackend);

	/** `GetBlockList` 를 묻는다. 이미 묻는 중이면 아무것도 안 한다. */
	bool RequestBlockList();

	bool RequestReport(const FString& TargetId, FName Reason, FDone OnDone);

	/** 차단(`BlockPlayer`) 또는 해제(`UnblockPlayer`). */
	bool RequestSetBlocked(const FString& TargetId, const FString& Name, bool bBlock, FDone OnDone);

	bool IsBlocked(const FString& PlayFabId) const { return !PlayFabId.IsEmpty() && BlockedIds.Contains(PlayFabId); }
	int32 NumBlocked() const { return BlockedIds.Num(); }
	bool IsPending() const { return bPending; }

	/** 서버 답의 `blocks`(`[{id, name, atUtc}]`)를 목록으로 그대로 옮긴다. @return 배열이 있었으면 true */
	bool ApplyBlocksReply(const TSharedPtr<FJsonObject>& Response);

	/** 그 몸(다른 사람)을 내가 차단했나 — 몸의 `UCoopReportComponent` PlayFabId 로 본다. 내 몸·모르는 몸은 false. */
	static bool IsPawnBlocked(const APawn* Pawn);

	static TSharedRef<FJsonObject> MakeReportArgs(const FString& TargetId, FName Reason);

private:
	UFUNCTION()
	void HandleSnapshotChanged();

	bool Call(const TCHAR* Function, const TSharedRef<FJsonObject>& Args, FDone OnDone);

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> Backend;

	TSet<FString> BlockedIds;
	/** 목록을 받아 둔 계정 — 다른 계정으로 다시 로그인하면 다시 받는다. */
	FString LoadedFor;
	bool bPending = false;
};
