#include "World/MineDoorActor.h"
#include "SnareCheckSwitch.h"

#include "Snarehaven.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

AMineDoorActor::AMineDoorActor()
{
	// 밟는 순간만 본다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	PrimaryActorTick.bCanEverTick = false;
	// 돌문 소리를 모든 기기에 보내려면 복제돼야 한다 (`Multicast_PlayCrossingCue`, DESIGN 8.10).
	bReplicates = true;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	RootComponent = Trigger;
	Trigger->SetBoxExtent(FVector(50.f));
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
}

void AMineDoorActor::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMineDoorActor::HandleOverlap);
#if !UE_BUILD_SHIPPING
		// `-SnareCrossSoundAt=2/3/4` 의 셋째 초 = 광산 문 돌문 소리 (M53-05, 포털 쪽 스위치와 한 짝).
		FString CrossAt;
		TArray<FString> Seconds;
		if (SnareCheckSwitch::Value(TEXT("SnareCrossSoundAt="), CrossAt) && CrossAt.ParseIntoArray(Seconds, TEXT("/")) > 2)
		{
			FTimerHandle Unused;
			GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				PlayCrossingCue(true, GetActorLocation());
			}), FCString::Atof(*Seconds[2]), false);
		}
#endif
	}
}

void AMineDoorActor::HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	SendThrough(Cast<APawn>(OtherActor));
}

bool AMineDoorActor::SendThrough(APawn* Pawn)
{
	// 조종자가 PlayerController 인지로 본다 — `IsPlayerControlled` 는 PlayerState 까지 요구해 게임모드 없는 검사 월드에서 늘 거짓이다.
	const AController* const Controller = IsValid(Pawn) ? Pawn->GetController() : nullptr;
	if (!HasAuthority() || !Controller || !Controller->IsPlayerController() || ExitLocation.IsZero())
	{
		return false;
	}
	// 발이 바닥에 닿게 캡슐 반높이만큼 올린다. 이동 복제가 클라이언트까지 옮긴다(8.14 수영 밀어내기와 같은 길).
	const FVector To = ExitLocation + FVector(0.f, 0.f, Pawn->GetSimpleCollisionHalfHeight());
	if (!Pawn->TeleportTo(To, FRotator(0.f, ExitYawDegrees, 0.f)))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Mine] %s 문 %s → %s 자리가 막혀 못 옮겼다"),
			*Pawn->GetName(), *GetName(), *To.ToCompactString());
		PlayCrossingCue(false, To);
		return false;
	}
	PlayCrossingCue(true, To);
	UE_LOG(LogSnarehaven, Log, TEXT("[Mine] %s 문 %s 로 지나 %s"), *Pawn->GetName(), *GetName(), *To.ToCompactString());
	return true;
}

void AMineDoorActor::PlayCrossingCue(bool bConfirmed, const FVector& Where)
{
	const SnareSound::FChangeCue Cue = SnareSound::ZoneCue(SnareSound::EZoneCrossing::MineDoor, bConfirmed);
	if (Cue.Cue.IsNone())
	{
		return;
	}
	++CrossingCueCount;
	++CrossingMulticastCount;
	LastCrossingCue = Cue.Cue;
	UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 건너기 %s — 크기 %.2f · 모든 기기"), *Cue.Cue.ToString(), Cue.Volume);
	Multicast_PlayCrossingCue(Cue.Cue, Cue.Volume, Where);
}

void AMineDoorActor::Multicast_PlayCrossingCue_Implementation(FName Cue, float Volume, FVector Where)
{
	USnareSoundSubsystem::PlayAt(this, Cue, Where, Volume);
}
