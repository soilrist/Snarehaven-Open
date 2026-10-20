#include "World/SnareMineView.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Snarehaven.h"

namespace SnareMineView
{
	bool IsInMine(const FVector& Feet)
	{
		return Feet.X > MineMinX && Feet.X < MineMaxX && FMath::Abs(Feet.Y) < MineHalfY;
	}

	bool OccludesView(const FBox& RockBounds, const FVector& CameraLocation, const FVector& Target)
	{
		if (!RockBounds.IsValid)
		{
			return false;
		}
		// 플레이어가 바운드 안이어도 가린 것으로 친다 — 벽 바위는 충돌이 없어 바운드 상자가 플레이어와 겹치는 일이 흔하다.
		return FMath::LineBoxIntersection(RockBounds.ExpandBy(OccluderMarginCm), CameraLocation, Target, Target - CameraLocation);
	}
}

namespace
{
	// `setup_mine.py` 가 굴 벽 바위에만 입히는 재질 — 이름으로 벽 바위를 가려낸다(레벨 파일을 건드리지 않으려고).
	const TCHAR* const MineRockMaterial = TEXT("MI_Nature_Leafs_MineRock");
}

bool USnareMineViewSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USnareMineViewSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USnareMineViewSubsystem, STATGROUP_Tickables);
}

void USnareMineViewSubsystem::CollectRocks()
{
	bCollected = true;
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		if (!SnareMineView::IsInMine(It->GetActorLocation()))
		{
			continue;
		}
		UStaticMeshComponent* const Mesh = It->GetStaticMeshComponent();
		const UMaterialInterface* const Material = Mesh ? Mesh->GetMaterial(0) : nullptr;
		if (Material && Material->GetName() == MineRockMaterial)
		{
			Rocks.Add(Mesh);
		}
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[MineView] 굴 벽 바위 %d개를 찾았다"), Rocks.Num());
}

void USnareMineViewSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* const World = GetWorld();
	const APlayerController* const Controller = World->GetFirstPlayerController();
	APawn* const Pawn = Controller ? Controller->GetPawn() : nullptr;
	const bool bInMine = Pawn && SnareMineView::IsInMine(Pawn->GetActorLocation());
	if (!bInMine)
	{
		if (!HiddenRocks.IsEmpty())
		{
			ShowAllRocks();
		}
		if (Glow.IsValid() && Glow->IsVisible())
		{
			SetGlow(Pawn, false);
		}
		return;
	}
	if (!bCollected)
	{
		CollectRocks();
	}
	if (!Glow.IsValid() || !Glow->IsVisible())
	{
		SetGlow(Pawn, true);
	}
	if (Controller->PlayerCameraManager == nullptr)
	{
		return;
	}
	const FVector CameraLocation = Controller->PlayerCameraManager->GetCameraLocation();
	const FVector Target = Pawn->GetActorLocation();
	const int32 HiddenBefore = HiddenRocks.Num();
	for (const TWeakObjectPtr<UPrimitiveComponent>& Rock : Rocks)
	{
		UPrimitiveComponent* const Part = Rock.Get();
		if (Part == nullptr)
		{
			continue;
		}
		const FBox Bounds = Part->Bounds.GetBox();
		const bool bNear = FVector::DistSquared2D(Bounds.GetCenter(), Target) < FMath::Square(SnareMineView::OccluderRangeCm);
		const bool bBlock = bNear && SnareMineView::OccludesView(Bounds, CameraLocation, Target);
		if (bBlock != Part->bHiddenInGame)
		{
			Part->SetHiddenInGame(bBlock);
			if (bBlock)
			{
				HiddenRocks.AddUnique(Part);
			}
			else
			{
				HiddenRocks.Remove(Part);
			}
		}
	}
	if (HiddenRocks.Num() != HiddenBefore)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[MineView] 카메라 %s → 플레이어 %s 사이 바위 %d개를 껐다"), *CameraLocation.ToCompactString(), *Target.ToCompactString(), HiddenRocks.Num());
	}
}

void USnareMineViewSubsystem::ShowAllRocks()
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& Rock : HiddenRocks)
	{
		if (Rock.IsValid())
		{
			Rock->SetHiddenInGame(false);
		}
	}
	HiddenRocks.Reset();
}

void USnareMineViewSubsystem::SetGlow(APawn* Pawn, bool bOn)
{
	if (!bOn)
	{
		if (Glow.IsValid())
		{
			Glow->SetVisibility(false);
		}
		return;
	}
	if (!Glow.IsValid() || Glow->GetOwner() != Pawn)
	{
		UPointLightComponent* const Light = NewObject<UPointLightComponent>(Pawn);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetCastShadows(false);
		Light->SetLightColor(FLinearColor(1.f, 0.88f, 0.7f));
		Light->SetIntensity(SnareMineView::GlowIntensityCd);
		Light->SetAttenuationRadius(SnareMineView::GlowRadiusCm);
		Light->SetupAttachment(Pawn->GetRootComponent());
		Light->SetRelativeLocation(FVector(0.f, 0.f, SnareMineView::GlowHeightCm));
		Light->RegisterComponent();
		Glow = Light;
	}
	Glow->SetVisibility(true);
}
