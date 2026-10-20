#include "Ranch/CoopReportComponent.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "World/FruitTreeActor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	// DESIGN 3.2 「같이 하기 보고 유효」 — 서버는 120초 안 보고만 센다. 그 절반이라 한 번 빠져도 끊기지 않는다.
	constexpr float CoopReportIntervalSeconds = 60.f;

	// 주기를 재는 눈금. 보고 자체는 위 간격으로만 나가고, 이건 id 를 올리는 첫 순간을 당길 뿐이다.
	constexpr float CoopPollSeconds = 5.f;
}

UCoopReportComponent::UCoopReportComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCoopReportComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCoopReportComponent, ReportPlayFabId);
	DOREPLIFETIME(UCoopReportComponent, bHostPawn);
}

void UCoopReportComponent::BeginPlay()
{
	Super::BeginPlay();

	// 빙의는 BeginPlay 뒤에 올 수 있어 여기서 「내 몸인가」 를 가르지 않는다 — 매 눈금 `Poll` 이 본다.
	if (UWorld* const World = GetWorld(); World != nullptr && World->IsGameWorld())
	{
		World->GetTimerManager().SetTimer(PollTimer, this, &UCoopReportComponent::HandlePollTimer, CoopPollSeconds, true);
	}
}

void UCoopReportComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	Super::EndPlay(EndPlayReason);
}

float UCoopReportComponent::GetReportIntervalSeconds()
{
	return CoopReportIntervalSeconds;
}

bool UCoopReportComponent::ShouldReportNow(double NowSeconds, double InLastReportSeconds)
{
	return InLastReportSeconds < 0.0 || NowSeconds - InLastReportSeconds >= CoopReportIntervalSeconds;
}

FSnareCoopReport UCoopReportComponent::BuildReport(const FString& MyId, const TArray<FSnareCoopMember>& Members)
{
	FSnareCoopReport Report;
	if (MyId.IsEmpty())
	{
		return Report;
	}
	for (const FSnareCoopMember& Member : Members)
	{
		if (Member.PlayFabId.IsEmpty())
		{
			continue;
		}
		if (Member.bHost)
		{
			Report.HostId = Member.PlayFabId;
		}
		if (Member.PlayFabId != MyId)
		{
			Report.Peers.AddUnique(Member.PlayFabId);
		}
	}
	Report.bSend = !Report.HostId.IsEmpty() && Report.Peers.Num() > 0;
	return Report;
}

TSharedRef<FJsonObject> UCoopReportComponent::MakeArgs(const FSnareCoopReport& Report)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("hostId"), Report.HostId);
	TArray<TSharedPtr<FJsonValue>> PeerValues;
	for (const FString& Peer : Report.Peers)
	{
		PeerValues.Add(MakeShared<FJsonValueString>(Peer));
	}
	Args->SetArrayField(TEXT("peers"), PeerValues);
	return Args;
}

bool UCoopReportComponent::ReadCoopReply(bool bOk, const TSharedPtr<FJsonObject>& Response)
{
	bool bCoop = false;
	return bOk && Response.IsValid() && Response->TryGetBoolField(TEXT("coop"), bCoop) && bCoop;
}

bool UCoopReportComponent::IsMine() const
{
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	return Pawn != nullptr && Pawn->IsLocallyControlled();
}

TArray<FSnareCoopMember> UCoopReportComponent::GatherMembers() const
{
	TArray<FSnareCoopMember> Members;
	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		return Members;
	}
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		if (const UCoopReportComponent* const Other = It->FindComponentByClass<UCoopReportComponent>())
		{
			Members.Add({ Other->ReportPlayFabId, Other->bHostPawn });
		}
	}
	return Members;
}

void UCoopReportComponent::SyncFriendTree()
{
	UWorld* const World = GetWorld();
	if (World == nullptr || World->GetNetMode() != NM_Client || UGameplayStatics::GetCurrentLevelName(World) != TEXT("L_Ranch"))
	{
		return;
	}
	// 검사 전용 스위치(`-SnareCapture=` 와 같은 부류, 설계 숫자 아님): 로그인 없는 검사 폰은 방장 id 가 비어 친구 나무가
	// 안 서므로, 명령줄 id 로 세운다. 두 프로세스 손님 캡처(루프#946): 방장 `L_Ranch?listen`, 손님 `127.0.0.1:7777`.
	FString TestHostId;
	if (SnareCheckSwitch::Value(TEXT("SnareFriendTreeHost="), TestHostId) && !TestHostId.IsEmpty())
	{
		AFruitTreeActor* const TestTree = AFruitTreeActor::EnsureFriendTree(World, TestHostId, TEXT("민수"));
		// 같은 부류의 스위치: 친구 나무는 늘 다 자란 모양이라 묘목·빈 자리 그림은 이걸로만 찍을 수 있다(`Sapling`·`Empty`).
		FString TestStage;
		if (TestTree != nullptr && SnareCheckSwitch::Value(TEXT("SnareFriendTreeStage="), TestStage))
		{
			SnareFruitTree::FTreeView View;
			View.Fruit = SnareFruitTree::NativeFruitOf(TestHostId);
			View.Stage = TestStage == TEXT("Sapling") ? SnareFruitTree::EStage::Sapling : SnareFruitTree::EStage::Empty;
			TestTree->ApplyView(View);
		}
		return;
	}
	// 손님 화면에만 방장 거점 나무를 세운다(DESIGN 8.18). 방장 몸이 아직 id 를 안 올렸으면 빈 id 라 세우지 않는다.
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const UCoopReportComponent* const Other = It->FindComponentByClass<UCoopReportComponent>();
		if (Other != nullptr && Other->bHostPawn && !Other->ReportPlayFabId.IsEmpty())
		{
			const APlayerState* const State = It->GetPlayerState();
			AFruitTreeActor::EnsureFriendTree(World, Other->ReportPlayFabId, State != nullptr ? State->GetPlayerName() : FString());
			return;
		}
	}
}

void UCoopReportComponent::HandlePollTimer()
{
	if (const UWorld* const World = GetWorld())
	{
		Poll(World->GetTimeSeconds());
	}
}

void UCoopReportComponent::Poll(double NowSeconds)
{
	if (!IsMine())
	{
		return;
	}
	const UGameInstance* const GameInstance = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
	USnareBackendSubsystem* const Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	const FString MyId = Backend != nullptr ? Backend->GetPlayFabId() : FString();
	// 친구 나무는 내 id 와 무관하게(검사 스위치) 먼저 맞춘다 — 평소엔 방장 id 가 빈 손님에게 나무가 안 선다.
	SyncFriendTree();
	if (MyId.IsEmpty())
	{
		SetCoopActive(false);
		return;
	}

	if (SentPlayFabId != MyId)
	{
		SentPlayFabId = MyId;
		Server_SetPlayFabId(MyId);
	}

	if (bReportInFlight || !ShouldReportNow(NowSeconds, LastReportSeconds))
	{
		return;
	}

	const FSnareCoopReport Report = BuildReport(MyId, GatherMembers());
	if (!Report.bSend)
	{
		// 혼자면 서버에 묻지 않는다 — 서버도 혼자는 안 당긴다. 다른 사람이 오면 다음 눈금에 바로 보낸다.
		SetCoopActive(false);
		return;
	}

	LastReportSeconds = NowSeconds;
	bReportInFlight = true;
	UE_LOG(LogSnarehaven, Log, TEXT("[Coop] ReportCoop 보냄 — 다른 사람 %d명"), Report.Peers.Num());
	const TWeakObjectPtr<UCoopReportComponent> WeakThis(this);
	Backend->CallServerFunction(TEXT("ReportCoop"), MakeArgs(Report),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			if (UCoopReportComponent* const Self = WeakThis.Get())
			{
				if (!bOk)
				{
					UE_LOG(LogSnarehaven, Log, TEXT("[Coop] ReportCoop 실패 — %s"), *Error);
				}
				Self->ApplyReply(bOk, Response);
			}
		});
}

void UCoopReportComponent::ApplyReply(bool bOk, const TSharedPtr<FJsonObject>& Response)
{
	bReportInFlight = false;
	SetCoopActive(ReadCoopReply(bOk, Response));
}

void UCoopReportComponent::SetCoopActive(bool bActive)
{
	if (bCoopActive == bActive)
	{
		return;
	}
	bCoopActive = bActive;
	UE_LOG(LogSnarehaven, Log, TEXT("[Coop] 같이 하기 %s"), bActive ? TEXT("켜짐") : TEXT("꺼짐"));
	OnCoopStateChanged.Broadcast();
}

void UCoopReportComponent::Server_SetPlayFabId_Implementation(const FString& InPlayFabId)
{
	// 서버는 이 id 를 믿지 않는다 — PlayFab 쪽에서 각자 자기 계정 기록을 맞대 센다(DESIGN 3.2). 여기선 서로 알리기만.
	ReportPlayFabId = InPlayFabId.Left(64);
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	bHostPawn = Pawn != nullptr && Pawn->IsLocallyControlled();
}
