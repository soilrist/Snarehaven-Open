#include "World/ZoneTravelComponent.h"
#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareEntryReject.h"
#include "Combat/MeleeAttackComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "World/ZonePortalActor.h"

UZoneTravelComponent::UZoneTravelComponent()
{
	// 대상 찾기는 타이머가 돌린다 — 매 프레임 액터를 전수 조사하지 않는다
	// (engines/unreal.md 코드 규칙 2).
	PrimaryComponentTick.bCanEverTick = false;

	// 컴포넌트가 복제 대상이어야 `Server_` RPC 가 호스트로 간다 (값은 하나도 복제하지 않는다 —
	// `UCaptureComponent` 와 같다).
	SetIsReplicatedByDefault(true);
}

void UZoneTravelComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 하이라이트 갱신 주기도 새로 만들지 않고 근접 공격 간격을 빌린다
	// (DESIGN 3.3 "하이라이트 대상 갱신 주기는 공격 간격과 같은 값" — 감독 확정).
	const UMeleeAttackComponent* const Melee = FindMeleeAttack();
	const float ScanIntervalSeconds = IsValid(Melee) ? Melee->GetAttackIntervalSeconds() : -1.f;
	if (ScanIntervalSeconds <= 0.f)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("포탈 대상 갱신 주기를 빌릴 곳이 없다 — 버튼을 누를 때만 포탈을 찾는다"));
		return;
	}

	World->GetTimerManager().SetTimer(
		TargetScanTimerHandle, this, &UZoneTravelComponent::HandleTargetScan, ScanIntervalSeconds, true);
}

void UZoneTravelComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 타이머를 남겨 두면 파괴된 컴포넌트를 다시 부른다 (engines/unreal.md 코드 규칙 7).
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TargetScanTimerHandle);
	}

	// 하이라이트를 켠 채로 사라지면 포탈이 계속 빛난다.
	SetTarget(nullptr);

	Super::EndPlay(EndPlayReason);
}

void UZoneTravelComponent::HandleTargetScan()
{
	RefreshTarget();
}

UMeleeAttackComponent* UZoneTravelComponent::FindMeleeAttack() const
{
	const AActor* const Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return nullptr;
	}

	return Owner->FindComponentByClass<UMeleeAttackComponent>();
}

float UZoneTravelComponent::GetTravelRangeCm() const
{
	const UMeleeAttackComponent* const Melee = FindMeleeAttack();
	if (!IsValid(Melee))
	{
		// 사거리를 여기서 지어내지 않는다 — 자동 상호작용 거리는 DESIGN 3.1 확정값 하나뿐이고,
		// 그걸 들고 있는 컴포넌트가 없으면 미정으로 둔다 (DataDriven.md 3 · Verification.md 7).
		UE_LOG(LogSnarehaven, Warning, TEXT("근접 공격 컴포넌트가 없어 포탈 사거리를 알 수 없다"));
		return -1.f;
	}

	return Melee->GetAttackRangeCm();
}

bool UZoneTravelComponent::HasTravelAuthority() const
{
	const AActor* const Owner = GetOwner();
	return Owner == nullptr || Owner->HasAuthority();
}

AZonePortalActor* UZoneTravelComponent::RefreshTarget()
{
	AZonePortalActor* const NewTarget = FindNearestPortalInRange();
	SetTarget(NewTarget);
	return NewTarget;
}

bool UZoneTravelComponent::CanTravel() const
{
	return IsValid(CurrentTarget) && CurrentTarget->CanTravel();
}

bool UZoneTravelComponent::TryTravelToNearest()
{
	if (!HasTravelAuthority())
	{
		// 게스트는 부탁만 한다. 맵이 바뀌는 것은 호스트의 `ServerTravel` 을 엔진이 따라 옮겨 줄 때다
		// (M3-05 가 쓰는 길). 여기서 `OpenLevel` 을 부르면 게스트 혼자 다른 맵에 서서 방이 갈라진다.
		Server_TryTravelToNearest();
		return false;
	}

	return TryTravelOnAuthority();
}

FString UZoneTravelComponent::GetFerryDestKey(FName ZoneID)
{
	// 서버 `FERRY_FARES` 의 키 셋. 🟡 위험구역 존은 아직 표·맵이 없어 ID 를 `zone.danger` 로 가정한다.
	if (ZoneID == FName(TEXT("zone.hunt")))
	{
		return TEXT("hunt");
	}
	if (ZoneID == FName(TEXT("zone.ranch")))
	{
		return TEXT("base");
	}
	if (ZoneID == FName(TEXT("zone.danger")))
	{
		return TEXT("danger");
	}
	// M12-01: 숲(가을섬 성격) 칸 입장 = 원본 "섬 사이 = 선착장 배" 요금 (`UZoneEffectComponent` 가 부른다).
	if (ZoneID == FName(TEXT("zone.forest")))
	{
		return TEXT("island");
	}
	return FString();
}

USnareBackendSubsystem* UZoneTravelComponent::FindFerryBackend() const
{
	if (IsValid(FerryBackend))
	{
		return FerryBackend;
	}

	const UWorld* const World = GetWorld();
	UGameInstance* const GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
}

bool UZoneTravelComponent::TryFerryToNearest()
{
	if (bFerryPending)
	{
		return false;
	}

	const AZonePortalActor* const Target = RefreshTarget();
	if (IsValid(Target) && Target->IsIslandFerry())
	{
		// 섬 사이 배는 뱃삯이 없다 — 가을섬 입장료는 도착한 칸이 받는다 (DESIGN 8.14 섬 사이 배).
		TryTravelToNearest();
		return true;
	}
	const FString Dest = IsValid(Target) ? GetFerryDestKey(Target->GetTargetZoneID()) : FString();
	USnareBackendSubsystem* const Backend = FindFerryBackend();
	if (Dest.IsEmpty() || !IsValid(Backend))
	{
		return false;
	}

	bFerryPending = true;
	FerryMessage = FText::GetEmpty();

	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("dest"), Dest);

	// 답이 오기 전에 폰이 사라질 수 있다(다른 사람이 먼저 건넜다) — 약한 포인터로 쥔다.
	const TWeakObjectPtr<UZoneTravelComponent> WeakThis(this);
	const TWeakObjectPtr<USnareBackendSubsystem> WeakBackend(Backend);
	Backend->CallServerFunction(TEXT("PayFerry"), Args,
		[WeakThis, WeakBackend](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UZoneTravelComponent* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bFerryPending = false;
			// 사유 문구는 데이터 표(`DT_EntryRejectTable`)에서 온다 — 판정은 서버가 이미 했다.
			Self->FerryMessage = SnareEntryReject::DescribeReply(SnareEntryReject::LoadTable(), bOk, Response);
			if (!Self->FerryMessage.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Ferry] 거절 코드 %s — %s"),
					*SnareEntryReject::ReadCode(bOk, Response), *Error);
				// 요금을 못 냈다 — 떠나지 않는다 (DESIGN 8.14). 사유는 버튼 글자가 보여 준다.
				UE_LOG(LogSnarehaven, Log, TEXT("[Ferry] 건너지 않는다 — %s"), *Self->FerryMessage.ToString());
				// 버튼을 듣는 화면들이 글자를 다시 그리게 같은 알림을 한 번 더 쏜다 (대상은 그대로다).
				Self->OnZonePortalTargetChanged.Broadcast(Self->CurrentTarget);
				return;
			}

			// 잔액은 서버가 깎았다 — 캐시를 고치지 않고 다시 받는다 (CLAUDE.md 8).
			if (USnareBackendSubsystem* const Owner = WeakBackend.Get())
			{
				Owner->FetchPlayerState();
			}
			Self->TryTravelToNearest();
		});
	return true;
}

void UZoneTravelComponent::Server_TryTravelToNearest_Implementation()
{
	TryTravelOnAuthority();
}

bool UZoneTravelComponent::TryTravelOnAuthority()
{
	// 호스트가 **자기 쪽에서** 포탈을 다시 고른다 — 게스트가 보낸 대상을 믿지 않는다
	// (`UCaptureComponent::TryCaptureTargetOnAuthority` 와 같은 규칙).
	AZonePortalActor* const Target = RefreshTarget();
	if (!IsValid(Target))
	{
		return false;
	}

	return Target->IsIslandFerry() ? Target->FerryTraveller(GetOwner()) : Target->TravelToTargetZone();
}

AZonePortalActor* UZoneTravelComponent::FindNearestPortalInRange() const
{
	const AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	const float RangeCm = GetTravelRangeCm();
	if (!IsValid(Owner) || World == nullptr || RangeCm < 0.f)
	{
		return nullptr;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const float RangeSquared = RangeCm * RangeCm;

	// 포탈은 맵에 몇 개뿐이지만 BeginPlay 캐싱을 하지 않는 이유는 채집과 같다 — 갱신 주기 타이머에서만
	// 돌고, 나중에 포탈을 스폰해도 그대로 잡힌다.
	AZonePortalActor* Nearest = nullptr;
	float NearestDistanceSquared = 0.f;
	for (TActorIterator<AZonePortalActor> It(World); It; ++It)
	{
		AZonePortalActor* const Portal = *It;
		// 갈 곳이 확정되지 않은 포탈은 하이라이트도 하지 않는다 — 버튼이 떠도 눌러서 될 일이 없다
		// (ROADMAP M6-02b "버튼이 `CanTravel()` 인 포탈에만 뜬다").
		if (!IsValid(Portal) || !Portal->CanTravel())
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(OwnerLocation, Portal->GetActorLocation());
		if (DistanceSquared > RangeSquared)
		{
			continue;
		}

		if (Nearest == nullptr || DistanceSquared < NearestDistanceSquared)
		{
			Nearest = Portal;
			NearestDistanceSquared = DistanceSquared;
		}
	}

	return Nearest;
}

void UZoneTravelComponent::SetTarget(AZonePortalActor* NewTarget)
{
	if (CurrentTarget == NewTarget)
	{
		return;
	}

	if (IsValid(CurrentTarget))
	{
		CurrentTarget->SetHighlighted(false);
	}

	CurrentTarget = NewTarget;
	// 다른 포탈 앞에 서면 지난 거절 사유는 그 포탈 얘기라 지운다.
	FerryMessage = FText::GetEmpty();

	if (IsValid(CurrentTarget))
	{
		CurrentTarget->SetHighlighted(true);
	}

	OnZonePortalTargetChanged.Broadcast(CurrentTarget);
}
