#include "Backend/SnareBlockListSubsystem.h"

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "Ranch/CoopReportComponent.h"

const TArray<FName>& USnareBlockListSubsystem::GetReportReasons()
{
	static const TArray<FName> Reasons = { TEXT("name"), TEXT("harass"), TEXT("cheat"), TEXT("other") };
	return Reasons;
}

void USnareBlockListSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BindBackend(Collection.InitializeDependency<USnareBackendSubsystem>());
}

void USnareBlockListSubsystem::Deinitialize()
{
	BindBackend(nullptr);
	Super::Deinitialize();
}

void USnareBlockListSubsystem::BindBackend(USnareBackendSubsystem* InBackend)
{
	if (IsValid(Backend))
	{
		Backend->OnPlayerSnapshotChanged.RemoveDynamic(this, &USnareBlockListSubsystem::HandleSnapshotChanged);
	}
	Backend = InBackend;
	if (IsValid(Backend))
	{
		Backend->OnPlayerSnapshotChanged.AddUniqueDynamic(this, &USnareBlockListSubsystem::HandleSnapshotChanged);
	}
}

void USnareBlockListSubsystem::HandleSnapshotChanged()
{
	// 로그인 뒤 첫 보유 상태가 오면 그 계정의 차단 목록을 한 번 받는다.
	if (IsValid(Backend) && Backend->GetLoginState() == ESnareBackendLoginState::LoggedIn
		&& LoadedFor != Backend->GetPlayFabId())
	{
		RequestBlockList();
	}
}

bool USnareBlockListSubsystem::Call(const TCHAR* Function, const TSharedRef<FJsonObject>& Args, FDone OnDone)
{
	if (!IsValid(Backend) || bPending)
	{
		return false;
	}
	bPending = true;
	TWeakObjectPtr<USnareBlockListSubsystem> WeakThis(this);
	Backend->CallServerFunction(Function, Args,
		[WeakThis, OnDone = MoveTemp(OnDone)](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			USnareBlockListSubsystem* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (Failure.IsEmpty())
			{
				Self->ApplyBlocksReply(Response);
			}
			if (OnDone)
			{
				OnDone(Failure);
			}
		});
	return true;
}

bool USnareBlockListSubsystem::RequestBlockList()
{
	const FString For = IsValid(Backend) ? Backend->GetPlayFabId() : FString();
	TWeakObjectPtr<USnareBlockListSubsystem> WeakThis(this);
	return Call(TEXT("GetBlockList"), MakeShared<FJsonObject>(), [WeakThis, For](const FText& Failure)
	{
		if (Failure.IsEmpty() && WeakThis.IsValid())
		{
			WeakThis->LoadedFor = For;
		}
	});
}

TSharedRef<FJsonObject> USnareBlockListSubsystem::MakeReportArgs(const FString& TargetId, FName Reason)
{
	TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("targetPlayerId"), TargetId);
	Args->SetStringField(TEXT("reason"), Reason.ToString());
	return Args;
}

bool USnareBlockListSubsystem::RequestReport(const FString& TargetId, FName Reason, FDone OnDone)
{
	if (TargetId.IsEmpty() || !GetReportReasons().Contains(Reason))
	{
		return false;
	}
	return Call(TEXT("ReportPlayer"), MakeReportArgs(TargetId, Reason), MoveTemp(OnDone));
}

bool USnareBlockListSubsystem::RequestSetBlocked(const FString& TargetId, const FString& Name, bool bBlock, FDone OnDone)
{
	if (TargetId.IsEmpty())
	{
		return false;
	}
	TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("targetPlayerId"), TargetId);
	if (bBlock)
	{
		Args->SetStringField(TEXT("name"), Name);
	}
	return Call(bBlock ? TEXT("BlockPlayer") : TEXT("UnblockPlayer"), Args, MoveTemp(OnDone));
}

bool USnareBlockListSubsystem::ApplyBlocksReply(const TSharedPtr<FJsonObject>& Response)
{
	const TArray<TSharedPtr<FJsonValue>>* Blocks = nullptr;
	if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("blocks"), Blocks))
	{
		return false;
	}
	BlockedIds.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *Blocks)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		FString Id;
		if (Value.IsValid() && Value->TryGetObject(Entry) && (*Entry)->TryGetStringField(TEXT("id"), Id) && !Id.IsEmpty())
		{
			BlockedIds.Add(Id);
		}
	}
	return true;
}

bool USnareBlockListSubsystem::IsPawnBlocked(const APawn* Pawn)
{
	if (!IsValid(Pawn) || Pawn->IsLocallyControlled())
	{
		return false;
	}
	const UGameInstance* GameInstance = Pawn->GetGameInstance();
	const USnareBlockListSubsystem* Self = GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBlockListSubsystem>() : nullptr;
	const UCoopReportComponent* Report = Pawn->FindComponentByClass<UCoopReportComponent>();
	return Self != nullptr && Report != nullptr && Self->IsBlocked(Report->GetReportPlayFabId());
}
