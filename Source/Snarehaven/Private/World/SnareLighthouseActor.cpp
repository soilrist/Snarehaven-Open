#include "World/SnareLighthouseActor.h"

#include "Backend/SnareStory.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Snarehaven.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SnareDayNight.h"

namespace
{
	// 반입 스크립트 `tools/unreal/import_lighthouse_mesh.py` 가 만드는 에셋.
	const TCHAR* TowerMeshPath = TEXT("/Game/Game/Environment/Props/Story/SM_Prop_Lighthouse_01.SM_Prop_Lighthouse_01");
	// 켜진 창 — 언릿 발광 한 겹(`tools/unreal/create_hit_flash_material.py`). 불투명도 1 이면 창 색이 그대로 빛난다.
	const TCHAR* GlowMaterialPath = TEXT("/Game/Game/VFX/Hit/M_HitFlash.M_HitFlash");
	// 메시 원본의 창 슬롯 이름과 꺼진 창 색(`SLOT_COLORS["Window"]`), 켜진 창 색(랜턴 점광과 같은 따뜻한 빛).
	const FName WindowSlotName(TEXT("Window"));
	const FLinearColor WindowDarkColor(0.02f, 0.035f, 0.055f);
	const FLinearColor WindowLitColor(1.f, 0.72f, 0.3f);
	// 메시 원본(`make_lighthouse_mesh.py`)의 랜턴 방 높이(cm).
	constexpr float LanternHeightCm = 1105.f;
	// 빛 세기 최대값과 퍼짐 — 밤 불빛(가로등)과 같은 단위, 그림자 끔(모바일 동적 광원 예산).
	constexpr float BeamMaxIntensity = 2000000.f;
	constexpr float LanternMaxIntensity = 100000.f;
	// 아래로 숙임 — 수평에 가까우면(-3도) 빛이 땅에 닿기 전에 닿는 거리(5000)가 끝나 아무 데도 안 비친다(#1283 캡처). -10도 + 바깥 원뿔 16도 → 탑에서 3400~5000cm 고리에 닿는다.
	constexpr float BeamPitchDegrees = -10.f;
	// 낮에도 보이는 빛줄기 메시 (INBOX 감독 10-20 16:20 보정) — 엔진 원뿔(바닥 반지름 50 · 높이 100 · 꼭짓점 +Z, 루프#1423 실측)을
	// 늘려 랜턴에서 빛줄기 쪽으로 눕힌다. 길이 = 탑 키 1337cm 의 2배 넘게, 끝 반지름 = 퍼짐 약 7.6도. 재질은 가산·언릿·끝으로 옅어짐.
	const TCHAR* BeamConeMeshPath = TEXT("/Engine/BasicShapes/Cone.Cone");
	const TCHAR* BeamConeMaterialPath = TEXT("/Game/Game/VFX/Lighthouse/M_LighthouseBeam.M_LighthouseBeam");
	constexpr float BeamMeshLengthCm = 3000.f;
	constexpr float BeamMeshEndRadiusCm = 400.f;
	constexpr float BeamMeshMaxStrength = 1.f;
	constexpr float ConeMeshRadiusCm = 50.f;
	constexpr float ConeMeshHeightCm = 100.f;
}

ASnareLighthouseActor::ASnareLighthouseActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);

	USceneComponent* const Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Tower = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tower"));
	Tower->SetupAttachment(Root);
	Tower->SetMobility(EComponentMobility::Movable);
	const ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TowerMeshPath);
	if (Mesh.Succeeded())
	{
		Tower->SetStaticMesh(Mesh.Object);
	}

	const ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(GlowMaterialPath);
	if (Glow.Succeeded())
	{
		GlowMaterial = Glow.Object;
	}

	Beam = CreateDefaultSubobject<USpotLightComponent>(TEXT("Beam"));
	Beam->SetupAttachment(Root);
	Beam->SetRelativeLocation(FVector(0.f, 0.f, LanternHeightCm));
	Beam->SetAttenuationRadius(SnareStory::LighthouseReachCm);
	Beam->SetInnerConeAngle(18.f);
	Beam->SetOuterConeAngle(36.f);
	Beam->SetLightColor(FLinearColor(1.f, 0.78f, 0.45f));
	Beam->SetCastShadows(false);
	Beam->SetIntensity(0.f);

	// 스포트 빛에 붙여 같이 돈다. 원뿔 +Z(꼭짓점)를 빛 뒤(-X)로 눕히면(피치 90) 바닥이 빛 앞(+X)에 온다 — 가운데를 길이 반만큼 앞으로.
	BeamMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeamMesh"));
	BeamMesh->SetupAttachment(Beam);
	BeamMesh->SetMobility(EComponentMobility::Movable);
	BeamMesh->SetRelativeLocation(FVector(BeamMeshLengthCm * 0.5f, 0.f, 0.f));
	BeamMesh->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	BeamMesh->SetRelativeScale3D(FVector(BeamMeshEndRadiusCm / ConeMeshRadiusCm, BeamMeshEndRadiusCm / ConeMeshRadiusCm, BeamMeshLengthCm / ConeMeshHeightCm));
	BeamMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeamMesh->SetCastShadow(false);
	BeamMesh->SetVisibility(false);
	const ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(BeamConeMeshPath);
	if (Cone.Succeeded())
	{
		BeamMesh->SetStaticMesh(Cone.Object);
	}
	const ConstructorHelpers::FObjectFinder<UMaterialInterface> BeamMaterial(BeamConeMaterialPath);
	if (BeamMaterial.Succeeded())
	{
		BeamMeshMaterial = BeamMaterial.Object;
	}

	Lantern = CreateDefaultSubobject<UPointLightComponent>(TEXT("Lantern"));
	Lantern->SetupAttachment(Root);
	Lantern->SetRelativeLocation(FVector(0.f, 0.f, LanternHeightCm));
	Lantern->SetAttenuationRadius(2000.f);
	Lantern->SetLightColor(FLinearColor(1.f, 0.72f, 0.3f));
	Lantern->SetCastShadows(false);
	Lantern->SetIntensity(0.f);
}

ASnareLighthouseActor* ASnareLighthouseActor::SpawnAtSpot(UWorld* World)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	if (TActorIterator<ASnareLighthouseActor> Existing(World); Existing)
	{
		return *Existing;
	}

	FVector Spot(SnareStory::LighthouseSpotX, SnareStory::LighthouseSpotY, 0.f);
	FHitResult Hit;
	const FVector Top(Spot.X, Spot.Y, 5000.0);
	const FVector Bottom(Spot.X, Spot.Y, -5000.0);
	if (World->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_WorldStatic))
	{
		Spot.Z = Hit.ImpactPoint.Z;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnareLighthouseActor* const Lighthouse = World->SpawnActor<ASnareLighthouseActor>(Spot, FRotator::ZeroRotator, Params);
	UE_LOG(LogSnarehaven, Log, TEXT("[Lighthouse] 탑 세움 — (%.0f, %.0f, %.0f)"), Spot.X, Spot.Y, Spot.Z);
	return Lighthouse;
}

void ASnareLighthouseActor::ApplyLight(float Brightness, float BeamYawDegrees)
{
	Beam->SetIntensity(BeamMaxIntensity * Brightness);
	Lantern->SetIntensity(LanternMaxIntensity * Brightness);
	Beam->SetRelativeRotation(FRotator(BeamPitchDegrees, BeamYawDegrees, 0.f));
	UpdateWindow(Brightness);
	UpdateBeamMesh(Brightness);
}

void ASnareLighthouseActor::UpdateBeamMesh(float Brightness)
{
	if (Brightness <= 0.f || BeamMeshMaterial == nullptr || BeamMesh->GetStaticMesh() == nullptr)
	{
		BeamMesh->SetVisibility(false);
		BeamMeshStrength = 0.f;
		return;
	}
	if (BeamMeshGlowMaterial == nullptr)
	{
		BeamMeshGlowMaterial = UMaterialInstanceDynamic::Create(BeamMeshMaterial, this);
		BeamMesh->SetMaterial(0, BeamMeshGlowMaterial);
	}
	const float Strength = BeamMeshMaxStrength * FMath::Min(Brightness, 1.f);
	if (!FMath::IsNearlyEqual(Strength, BeamMeshStrength))
	{
		BeamMeshGlowMaterial->SetScalarParameterValue(TEXT("BeamStrength"), Strength);
		BeamMeshStrength = Strength;
	}
	BeamMesh->SetVisibility(true);
}

const UStaticMeshComponent* ASnareLighthouseActor::GetBeamMesh() const
{
	return BeamMesh;
}

float ASnareLighthouseActor::GetBeamMeshStrength() const
{
	return BeamMesh->IsVisible() ? BeamMeshStrength : 0.f;
}

void ASnareLighthouseActor::UpdateWindow(float Brightness)
{
	if (WindowSlot == INDEX_NONE)
	{
		WindowSlot = Tower->GetMaterialIndex(WindowSlotName);
		if (WindowSlot == INDEX_NONE)
		{
			return;
		}
		WindowDarkMaterial = Tower->GetMaterial(WindowSlot);
	}
	if (Brightness <= 0.f)
	{
		if (WindowGlow > 0.f)
		{
			Tower->SetMaterial(WindowSlot, WindowDarkMaterial);
			WindowGlow = 0.f;
		}
		return;
	}
	if (WindowGlowMaterial == nullptr)
	{
		if (GlowMaterial == nullptr)
		{
			return;
		}
		WindowGlowMaterial = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		WindowGlowMaterial->SetScalarParameterValue(TEXT("FlashOpacity"), 1.f);
	}
	if (FMath::IsNearlyEqual(Brightness, WindowGlow) && Tower->GetMaterial(WindowSlot) == WindowGlowMaterial)
	{
		return;
	}
	WindowGlowMaterial->SetVectorParameterValue(TEXT("FlashColor"), FMath::Lerp(WindowDarkColor, WindowLitColor, FMath::Min(Brightness, 1.f)));
	Tower->SetMaterial(WindowSlot, WindowGlowMaterial);
	WindowGlow = FMath::Min(Brightness, 1.f);
}

float ASnareLighthouseActor::GetWindowGlow() const
{
	return WindowSlot != INDEX_NONE && Tower->GetMaterial(WindowSlot) == WindowGlowMaterial ? WindowGlow : 0.f;
}

float ASnareLighthouseActor::GetBeamIntensity() const
{
	return Beam->Intensity;
}

float ASnareLighthouseActor::GetBeamYaw() const
{
	return Beam->GetRelativeRotation().Yaw;
}

void ASnareLighthouseActor::StartIgnition(double NowSeconds)
{
	IgnitionStartSeconds = NowSeconds;
	UE_LOG(LogSnarehaven, Log, TEXT("[Lighthouse] 점화 연출 시작 — %.1f초 동안 불이 붙고 빛줄기가 한 바퀴 돈다"), SnareStory::LighthouseIgnitionSeconds());
}

bool ASnareLighthouseActor::IsIgniting(double NowSeconds) const
{
	return IgnitionStartSeconds >= 0.0 && NowSeconds >= IgnitionStartSeconds
		&& NowSeconds - IgnitionStartSeconds < SnareStory::LighthouseIgnitionSeconds();
}

void ASnareLighthouseActor::UpdateLight(bool bDone, float Night, double BeamSeconds, double NowSeconds)
{
	float Brightness = SnareStory::LighthouseBrightness(bDone, Night);
	float Yaw = SnareStory::LighthouseBeamYaw(BeamSeconds);
	if (IsIgniting(NowSeconds))
	{
		const float Since = static_cast<float>(NowSeconds - IgnitionStartSeconds);
		Brightness = FMath::Max(Brightness, SnareStory::LighthouseIgnitionBrightness(Since));
		Yaw = FMath::Fmod(Yaw + SnareStory::LighthouseIgnitionExtraYaw(Since), 360.f);
	}
	else if (IgnitionStartSeconds >= 0.0 && NowSeconds >= IgnitionStartSeconds)
	{
		IgnitionStartSeconds = -1.0;
		UE_LOG(LogSnarehaven, Log, TEXT("[Lighthouse] 점화 연출 끝 — 이제 밤마다 켜진다"));
	}
	ApplyLight(Brightness, Yaw);
	if (!FMath::IsNearlyEqual(Brightness, LoggedBrightness) && (Brightness <= 0.f || Brightness >= 1.f || LoggedBrightness <= 0.f))
	{
		LoggedBrightness = Brightness;
		UE_LOG(LogSnarehaven, Log, TEXT("[Lighthouse] 밝기 %.2f (끝낸 계정 %s · 밤 %.2f · 점화 %s) — 빛줄기 세기 %.0f"),
			Brightness, bDone ? TEXT("예") : TEXT("아니오"), Night, IsIgniting(NowSeconds) ? TEXT("중") : TEXT("아님"), GetBeamIntensity());
	}
}

float ASnareLighthouseActor::GetLanternIntensity() const
{
	return Lantern->Intensity;
}

void ASnareLighthouseActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* const World = GetWorld();
	const AGameStateBase* const GameState = World ? World->GetGameState() : nullptr;
	if (GameState == nullptr)
	{
		return;
	}
	float Night = 0.f;
	if (const USnareDayNightSubsystem* const DayNight = World->GetSubsystem<USnareDayNightSubsystem>())
	{
		Night = DayNight->GetNightAmount();
	}
	UpdateLight(SnareStory::GetKnownDone(), Night, GameState->GetServerWorldTimeSeconds(), World->GetTimeSeconds());
}
