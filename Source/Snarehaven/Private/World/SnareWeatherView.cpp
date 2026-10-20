#include "World/SnareWeatherView.h"
#include "World/SnareWeather.h"
#include "SnareCheckSwitch.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Snarehaven.h"

namespace SnareWeatherView
{
	float Hash01(int32 Index, int32 Channel)
	{
		uint32 H = static_cast<uint32>(Index) * 747796405u + static_cast<uint32>(Channel) * 2891336453u + 12345u;
		H = ((H >> ((H >> 28u) + 4u)) ^ H) * 277803737u;
		H = (H >> 22u) ^ H;
		return static_cast<float>(H & 0xFFFFFFu) / 16777216.f;
	}

	int32 ActiveCount(float Amount, int32 Max)
	{
		return FMath::Clamp(FMath::CeilToInt(FMath::Clamp(Amount, 0.f, 1.f) * Max), 0, Max);
	}
}

bool USnareWeatherSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USnareWeatherSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USnareWeatherSubsystem, STATGROUP_Tickables);
}

void USnareWeatherSubsystem::Build()
{
	bBuilt = true;
	// 캡처·점검용: `-SnareWeatherAt=<초>` 로 서버 시각에 더한다 (슬롯 번호 × 600 + 60 = 그 칸의 섞기가 끝난 뒤)
	SnareCheckSwitch::Value(TEXT("SnareWeatherAt="), OffsetSeconds);
	UWorld* const World = GetWorld();
	Holder = World->SpawnActor<AActor>();
	USceneComponent* const Root = NewObject<USceneComponent>(Holder, TEXT("Root"));
	Holder->SetRootComponent(Root);
	Root->RegisterComponent();

	UStaticMesh* const Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* const Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	// 총알 튀김(`SnareGunTracerActor`)처럼 ISM 에 쓸 수 있는 발광 재질 — `M_HitFlash` 는 ISM 용 사용 플래그가 없어 기본 회색이 된다.
	UMaterialInterface* const Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Game/VFX/Hit/M_ImpactPuff.M_ImpactPuff"));

	auto MakeComp = [&](const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color, float Opacity) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* const Comp = NewObject<UInstancedStaticMeshComponent>(Holder, Name);
		Comp->SetStaticMesh(Mesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetCanEverAffectNavigation(false);
		Comp->bUseAsOccluder = false;
		Comp->SetCullDistance(0.f);
		if (Glow != nullptr)
		{
			UMaterialInstanceDynamic* const Mid = UMaterialInstanceDynamic::Create(Glow, Holder);
			Mid->SetVectorParameterValue(TEXT("FlashColor"), Color);
			Mid->SetScalarParameterValue(TEXT("FlashOpacity"), Opacity);
			Mid->SetScalarParameterValue(TEXT("EdgeFalloff"), 0.f);   // 가장자리까지 고르게 (총알 튀김과 같은 쓰임)
			Comp->SetMaterial(0, Mid);
		}
		Comp->SetupAttachment(Holder->GetRootComponent());
		Comp->RegisterComponent();
		return Comp;
	};
	RainComp = MakeComp(TEXT("Rain"), Cube, FLinearColor(0.75f, 0.85f, 1.f), 0.55f);
	SnowComp = MakeComp(TEXT("Snow"), Sphere, FLinearColor(1.f, 1.f, 1.f), 0.9f);
	for (int32 I = 0; I < SnareWeatherView::MaxRain; ++I) RainComp->AddInstance(FTransform(FVector(0, 0, -100000)), true);
	for (int32 I = 0; I < SnareWeatherView::MaxSnow; ++I) SnowComp->AddInstance(FTransform(FVector(0, 0, -100000)), true);
	UE_LOG(LogSnarehaven, Log, TEXT("날씨 화면 — 비 %d · 눈 %d 입자 준비 · 시작 %.0f초"),
		SnareWeatherView::MaxRain, SnareWeatherView::MaxSnow, OffsetSeconds);
}

void USnareWeatherSubsystem::UpdateParticles(UInstancedStaticMeshComponent* Comp, int32 Max, float Amount,
	const FVector& Center, double Seconds, bool bSnow)
{
	using namespace SnareWeatherView;
	const int32 Active = bUnderRoof ? 0 : ActiveCount(Amount, Max);
	const float Fall = bSnow ? SnowFallCmPerSec : RainFallCmPerSec;
	TArray<FTransform> Out;
	Out.SetNum(Max);
	// 비: 가늘고 긴 막대(6×6×70cm, 게임 카메라에서 약 3×36px) · 눈: 22cm 공(약 10px)
	const FVector Scale = bSnow ? FVector(SnowSizeCm / 100.f) : FVector(0.06f, 0.06f, 0.70f);
	for (int32 I = 0; I < Max; ++I)
	{
		if (I >= Active)
		{
			Out[I] = FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector::ZeroVector);
			continue;
		}
		const float X = (Hash01(I, 0) * 2.f - 1.f) * BoxHalfCm;
		const float Y = (Hash01(I, 1) * 2.f - 1.f) * BoxHalfCm;
		const float Phase = Hash01(I, 2) * BoxHeightCm;
		const float Z = BoxHeightCm - FMath::Fmod(Phase + static_cast<float>(Seconds) * Fall, BoxHeightCm);
		float Sway = 0.f;
		if (bSnow)
		{
			Sway = FMath::Sin(static_cast<float>(Seconds) * 1.3f + Hash01(I, 3) * 6.28f) * SnowSwayCm;
		}
		// 상자를 캐릭터를 따라 굴린다 — 캐릭터가 걸어도 입자가 같은 자리에 뜬 채 지나가게 월드 격자에 붙인다.
		const float Wrap = BoxHalfCm * 2.f;
		const float Wx = FMath::Fmod(X - Center.X + BoxHalfCm + Wrap * 1000.f, Wrap) - BoxHalfCm;
		const float Wy = FMath::Fmod(Y - Center.Y + BoxHalfCm + Wrap * 1000.f, Wrap) - BoxHalfCm;
		Out[I] = FTransform(FRotator::ZeroRotator, FVector(Center.X + Wx + Sway, Center.Y + Wy, Center.Z - 100.f + Z), Scale);
	}
	Comp->BatchUpdateInstancesTransforms(0, Out, true, true, true);
}

void USnareWeatherSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* const World = GetWorld();
	const AGameStateBase* const GameState = World ? World->GetGameState() : nullptr;
	if (GameState == nullptr)
	{
		return;
	}
	if (!bBuilt)
	{
		Build();
	}
	const double Seconds = GameState->GetServerWorldTimeSeconds() + OffsetSeconds;
	SnareWeather::Amounts(Seconds, Rain, Snow);

	const APlayerController* const PC = World->GetFirstPlayerController();
	const APawn* const Pawn = PC ? PC->GetPawn() : nullptr;
	if (Pawn == nullptr || RainComp == nullptr || SnowComp == nullptr)
	{
		return;
	}
	// 지붕 아래면 입자를 끈다 (표 「실내·지붕」) — 머리 위 위로 쏜 선이 무언가에 닿으면 지붕 아래.
	RoofTimer -= DeltaTime;
	if (RoofTimer <= 0.f)
	{
		RoofTimer = SnareWeatherView::RoofCheckSeconds;
		FHitResult Hit;
		const FVector From = Pawn->GetActorLocation();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SnareWeatherRoof), false, Pawn);
		bUnderRoof = World->LineTraceSingleByChannel(Hit, From, From + FVector(0, 0, 900.f), ECC_Visibility, Params);
	}
	const FVector Center = Pawn->GetActorLocation();
	UpdateParticles(RainComp, SnareWeatherView::MaxRain, Rain, Center, Seconds, false);
	UpdateParticles(SnowComp, SnareWeatherView::MaxSnow, Snow, Center, Seconds, true);
}
