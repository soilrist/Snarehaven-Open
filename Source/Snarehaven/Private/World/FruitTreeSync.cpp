#include "World/FruitTreeSync.h"

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "World/FruitTreeActor.h"
#include "World/SnareFruitTree.h"

namespace
{
	/** 서버에 가는 호출은 한 번에 하나 — 눌러 둔 요청이 순서 뒤집혀 오는 걸 막는다. */
	bool bCallInFlight = false;

	USnareBackendSubsystem* FindBackend(UWorld* World)
	{
		UGameInstance* const GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		return GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	}
}

FString FruitTreeSync::FunctionName(const EAction Action)
{
	switch (Action)
	{
	case EAction::Plant: return TEXT("PlantFruit");
	case EAction::Harvest: return TEXT("HarvestTree");
	default: return TEXT("ChopTree");
	}
}

TSharedRef<FJsonObject> FruitTreeSync::MakeArgs(const int32 Slot, const FName FruitItemID)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetNumberField(TEXT("slot"), Slot);
	if (!FruitItemID.IsNone())
	{
		Args->SetStringField(TEXT("itemId"), FruitItemID.ToString());
	}
	return Args;
}

bool FruitTreeSync::ApplyReply(UWorld* World, const bool bOk, const TSharedPtr<FJsonObject>& Response)
{
	FString Code;
	FString Message;
	TArray<SnareFruitTree::FTreeView> BySlot;
	if (World == nullptr || !SnareServerReply::IsAccepted(bOk, Response, FString(), Code, Message)
		|| !SnareFruitTree::ParseTrees(Response, BySlot))
	{
		return false;
	}
	for (TActorIterator<AFruitTreeActor> It(World); It; ++It)
	{
		if (BySlot.IsValidIndex(It->SlotIndex))
		{
			It->ApplyView(BySlot[It->SlotIndex]);
		}
	}
	return true;
}

bool FruitTreeSync::Refresh(UWorld* World)
{
	USnareBackendSubsystem* const Backend = FindBackend(World);
	if (Backend == nullptr || bCallInFlight)
	{
		return false;
	}
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	// 목장 BeginPlay 는 로그인 답보다 먼저다(M107-05 새 계정 첫 나무 2그루가 안 보임) — 로그인 중이면 2초 뒤 다시 묻는다.
	// ponytail: 오프라인(NotStarted 그대로)이면 2초 타이머가 계속 돈다 — 값싼 확인뿐, 거슬리면 오프라인 스위치로 거른다.
	const ESnareBackendLoginState LoginState = Backend->GetLoginState();
	if (LoginState == ESnareBackendLoginState::NotStarted || LoginState == ESnareBackendLoginState::InProgress)
	{
		FTimerHandle RetryHandle;
		World->GetTimerManager().SetTimer(RetryHandle, FTimerDelegate::CreateLambda([WeakWorld]()
			{
				Refresh(WeakWorld.Get());
			}), 2.f, false);
		return true;
	}
	bCallInFlight = true;
	Backend->CallServerFunction(TEXT("GetFruitTrees"), MakeShared<FJsonObject>(),
		[WeakWorld](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString&)
		{
			bCallInFlight = false;
			ApplyReply(WeakWorld.Get(), bOk, Response);
		});
	return true;
}

bool FruitTreeSync::Request(UWorld* World, const EAction Action, const int32 Slot, const FName FruitItemID)
{
	USnareBackendSubsystem* const Backend = FindBackend(World);
	if (Backend == nullptr || bCallInFlight || Slot < 0 || Slot >= SnareFruitTree::SlotCount)
	{
		return false;
	}
	bCallInFlight = true;
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	const TWeakObjectPtr<USnareBackendSubsystem> WeakBackend(Backend);
	Backend->CallServerFunction(FunctionName(Action), MakeArgs(Slot, FruitItemID),
		[WeakWorld, WeakBackend](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			bCallInFlight = false;
			FString Code;
			FString Message;
			if (SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message))
			{
				// 과일·나무 재료는 서버가 바꿨다 — 보유 상태와 나무를 다시 받는다 (CLAUDE.md 8).
				if (USnareBackendSubsystem* const Backend = WeakBackend.Get())
				{
					Backend->FetchPlayerState();
				}
				Refresh(WeakWorld.Get());
			}
		});
	return true;
}

FString FruitTreeSync::PickFriendFunctionName()
{
	return TEXT("PickFriendFruit");
}

bool FruitTreeSync::PickFriend(UWorld* World, const FString& HostPlayFabId)
{
	USnareBackendSubsystem* const Backend = FindBackend(World);
	if (Backend == nullptr || bCallInFlight || HostPlayFabId.IsEmpty())
	{
		return false;
	}
	bCallInFlight = true;
	const TWeakObjectPtr<USnareBackendSubsystem> WeakBackend(Backend);
	Backend->CallServerFunction(PickFriendFunctionName(), SnareFruitTree::MakePickArgs(HostPlayFabId),
		[WeakBackend](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			bCallInFlight = false;
			FString Code;
			FString Message;
			if (SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message))
			{
				// 받은 과일은 서버가 가방에 넣었다 — 보유 상태를 다시 받는다 (CLAUDE.md 8).
				if (USnareBackendSubsystem* const Backend = WeakBackend.Get())
				{
					Backend->FetchPlayerState();
				}
			}
		});
	return true;
}
