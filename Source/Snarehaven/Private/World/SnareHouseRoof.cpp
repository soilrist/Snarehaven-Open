#include "World/SnareHouseRoof.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/PlayerController.h"
#include "SnarehavenCharacter.h"

namespace SnareHouseRoof
{
	bool IsUnderRoof(const FBox& RoofBounds, const FVector& Feet)
	{
		return RoofBounds.IsValid
			&& Feet.X > RoofBounds.Min.X + EaveInsetCm && Feet.X < RoofBounds.Max.X - EaveInsetCm
			&& Feet.Y > RoofBounds.Min.Y + EaveInsetCm && Feet.Y < RoofBounds.Max.Y - EaveInsetCm
			&& Feet.Z < RoofBounds.Max.Z;
	}

	bool KeepsVisibleInInterior(const FBox& RoofBounds, const FVector& ActorOrigin, const FVector& ActorExtent)
	{
		const FVector RoofExtent = RoofBounds.GetExtent();
		return RoofBounds.IsValid && RoofBounds.IsInside(ActorOrigin)
			&& ActorExtent.X <= RoofExtent.X * 1.5f && ActorExtent.Y <= RoofExtent.Y * 1.5f;
	}
}

const FName USnareHouseRoofSubsystem::RoofTag(TEXT("SnareHouseRoof"));

bool USnareHouseRoofSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USnareHouseRoofSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USnareHouseRoofSubsystem, STATGROUP_Tickables);
}

void USnareHouseRoofSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* const World = GetWorld();
	if (!bSearched)
	{
		bSearched = true;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(RoofTag))
			{
				Roofs.Add(*It);
			}
		}
	}
	if (Roofs.IsEmpty())
	{
		return;
	}
	// 지붕은 이 기기에서 보는 사람 기준 — 로컬 플레이어 하나만 본다(남이 들어가도 내 화면 지붕은 그대로).
	const APlayerController* const Controller = World->GetFirstPlayerController();
	const APawn* const Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		return;
	}
	const FVector Feet = Pawn->GetActorLocation();
	AActor* InsideNow = nullptr;
	for (const TWeakObjectPtr<AActor>& Roof : Roofs)
	{
		if (Roof.IsValid())
		{
			const bool bInside = SnareHouseRoof::IsUnderRoof(Roof->GetComponentsBoundingBox(), Feet);
			if (Roof->IsHidden() != bInside)
			{
				Roof->SetActorHiddenInGame(bInside);
			}
			if (bInside)
			{
				InsideNow = Roof.Get();
			}
		}
	}
	// 레퍼런스는 집에 들어가면 화면이 검게 바뀌고 실내만 남는다 — 안팎이 바뀐 순간에만 한 번 바꾼다.
	if (InsideNow != InsideRoof.Get())
	{
		LeaveInterior();
		if (InsideNow != nullptr)
		{
			EnterInterior(World, InsideNow, Pawn);
		}
		InsideRoof = InsideNow;
		// 실내는 화면 가운데 작은 상자라 카메라를 당긴다(M18-02) — 다가가고 돌아가는 건 캐릭터가 부드럽게.
		if (ASnarehavenCharacter* const Character = Cast<ASnarehavenCharacter>(Controller->GetPawn()))
		{
			Character->SetInteriorCamera(InsideNow != nullptr);
		}
		if (Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StartCameraFade(1.f, 0.f, SnareHouseRoof::InteriorFadeSeconds, FLinearColor::Black);
		}
	}
}

void USnareHouseRoofSubsystem::EnterInterior(UWorld* World, const AActor* Roof, const APawn* Pawn)
{
	const FBox RoofBounds = Roof->GetComponentsBoundingBox();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* const Actor = *It;
		if (Actor->IsHidden() || Actor == Pawn || Actor->GetOwner() == Pawn || Actor->IsAttachedTo(Pawn))
		{
			continue;
		}
		FVector Origin, Extent;
		Actor->GetActorBounds(true, Origin, Extent);
		if (SnareHouseRoof::KeepsVisibleInInterior(RoofBounds, Actor->GetActorLocation(), Extent))
		{
			continue;
		}
		TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
		for (UPrimitiveComponent* const Part : Parts)
		{
			if (Part->IsVisible() && !Part->bHiddenInGame)
			{
				Part->SetHiddenInGame(true);
				HiddenOutside.Add(Part);
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[HouseRoof] 집 안 — 바깥 구성요소 %d개를 이 화면에서 껐다"), HiddenOutside.Num());
}

void USnareHouseRoofSubsystem::LeaveInterior()
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& Part : HiddenOutside)
	{
		if (Part.IsValid())
		{
			Part->SetHiddenInGame(false);
		}
	}
	HiddenOutside.Reset();
}
