#include "Combat/SnareGunTracerActor.h"
#include "Snarehaven.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Gathering/GatherComponent.h"
#include "Gathering/GatherableActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SnareSound.h"

namespace
{
	// 총알 모양 (M39-05). **보이는 모양만 정하는 연출 튜닝값**이라 명세 2절엔 문장으로만 적는다.
	// 엔진 기본 공·상자는 100cm 라 배율 = cm ÷ 100.
	// 게임 카메라(4500cm · 세로 17.14°)에서 1280×720 한 화소 ≈ 1.9cm. 6cm 점·2cm 꼬리는 2px 로 풀밭 결과 안 갈렸다
	// (감독 #583 3층 불합격 — M39-07b 「두께 ≥4px · 꼬리 ≥60px」).
	constexpr float BulletSizeScale = 0.14f;      // 빛나는 점 14cm ≈ 7px — 꼬리보다 굵어 「머리 + 꼬리」 로 읽힌다
	constexpr float TailThicknessScale = 0.10f;   // 꼬리 굵기 10cm ≈ 5px
	constexpr float MuzzleFlashScale = 0.45f;     // 불꽃 45cm(앞으로 72cm) ≈ 24~38px — 22cm(12px)는 게임 카메라에서 안 보였다(감독 10-15 보정)
	constexpr float PelletLineLengthCm = 450.f;   // 산탄 갈래 줄 길이 ≈ 240px — 총구에서 부채꼴이 읽히는 만큼
	constexpr float PelletLineThicknessScale = 0.06f; // 6cm ≈ 3px
	constexpr float EngineShapeSizeCm = 100.f;
	// 발광색 — 노란 점, 살짝 주황 꼬리·불꽃. 언릿이라 값 그대로 화면에 나온다.
	const FLinearColor BulletColor(1.f, 0.9f, 0.4f);
	const FLinearColor TailColor(1.f, 0.75f, 0.25f);
	const FLinearColor SmokeColor(0.92f, 0.92f, 0.95f);   // 로켓 연기 꼬리 — 흰 회색(풀 배경에서 읽히게, #1211 보정)
	const FLinearColor MuzzleColor(1.f, 0.65f, 0.15f);
	// 튀김 조각 크기·속도. 피격 파편(4cm · 150~300cm/s)과 같은 자리 수 — 별 튀김은 옆으로 퍼지고 나머지는 위로 튄다.
	// 조각 16cm ≈ 8px · 0.35초에 90~160cm 퍼져 지름 ≥ 90px — 4cm 는 「흰 점 몇 개」(M39-07b), 10cm 12개는 퍽에 묻혔다(M39-07c).
	constexpr float SplashSizeScale = 0.16f;
	constexpr float SplashMinSpeed = 250.f;
	constexpr float SplashMaxSpeed = 450.f;
	// 닿는 순간 퍽 — 공이 70cm(≈37px)에서 85cm(≈45px)로 커지며 불투명도 1 → 0. 감독 「화면 35~45px」(M39-07c) —
	// 100 → 160cm 불투명 공은 돼지를 통째로 덮는 「하얀 원판」 이었다(`captures/584_gun_hit.png`). 가장자리는 재질이 0 으로.
	constexpr float ImpactPuffStartScale = 0.70f;
	constexpr float ImpactPuffEndScale = 0.85f;
	// 동물 털빛 조각 — 동물 피격 파편과 같은 마른 흙 갈색(`AnimalCharacter.cpp` HitDebrisColor). 종별 털색 칸이 생기면 옮긴다.
	const FLinearColor FurColor(0.45f, 0.35f, 0.25f);
	// 동물 별 튀김이 위로 뜨는 몫 — 위에서 내려다보는 카메라에서 사방(수평)으로 퍼지는 게 먼저 보이게 작게.
	constexpr float AnimalSplashLiftMin = 0.3f;
	constexpr float AnimalSplashLiftMax = 0.7f;
	// 동물 맞음은 도착 자리가 몸 가운데라 퍽·튀김이 몸에 반쯤 가렸다(루프#584 첫 캡처) — 이 기기 카메라 쪽으로
	// 이만큼 당겨 몸 겉에서 터지게 한다. 사냥 동물 캡슐 반지름(돼지 약 40cm)보다 조금 크게.
	constexpr float AnimalSplashTowardCameraCm = 50.f;
	constexpr float SplashGravity = 980.f;
	constexpr float SplashFirstFrameSeconds = 1.f / 30.f;
	// 위를 보는 면(법선 Z 이상)이면 땅 — 그보다 서 있으면 바위 옆면.
	constexpr float GroundNormalZ = 0.7f;
	const TCHAR* GlowMaterialPath = TEXT("/Game/Game/VFX/Hit/M_HitFlash.M_HitFlash");
	// 퍽만 가운데 진하고 가장자리 0 인 공(`create_impact_puff_material.py`) — 동물 몸이 가장자리 너머로 비친다.
	const TCHAR* PuffMaterialPath = TEXT("/Game/Game/VFX/Hit/M_ImpactPuff.M_ImpactPuff");
}

ASnareGunTracerActor::ASnareGunTracerActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	USceneComponent* const Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	SphereMesh = SphereAsset.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = CubeAsset.Object;
	// 발광 겹은 동물 흰 깜빡임과 같은 언릿 반투명 에셋(`create_hit_flash_material.py`) — 새 에셋 0.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowAsset(GlowMaterialPath);
	GlowMaterial = GlowAsset.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PuffAsset(PuffMaterialPath);
	PuffMaterial = PuffAsset.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SplashAsset(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	SplashMaterial = SplashAsset.Object;
}

UStaticMeshComponent* ASnareGunTracerActor::MakeGlowPart(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color,
	UMaterialInterface* Material)
{
	if (Mesh == nullptr)
	{
		return nullptr;
	}
	UStaticMeshComponent* const Part = NewObject<UStaticMeshComponent>(this, Name);
	Part->SetStaticMesh(Mesh);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCastShadow(false);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetUsingAbsoluteLocation(true);
	Part->SetUsingAbsoluteRotation(true);
	Part->SetUsingAbsoluteScale(true);
	UMaterialInterface* const BaseMaterial = Material != nullptr ? Material : GlowMaterial.Get();
	if (BaseMaterial != nullptr)
	{
		UMaterialInstanceDynamic* const Mid = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Mid->SetVectorParameterValue(TEXT("FlashColor"), Color);
		Mid->SetScalarParameterValue(TEXT("FlashOpacity"), 1.f);
		Part->SetMaterial(0, Mid);
	}
	Part->SetupAttachment(GetRootComponent());
	Part->RegisterComponent();
	return Part;
}

void ASnareGunTracerActor::Start(const FVector& Muzzle, const FVector& End, float InFlightSeconds, ESnareGunSplash InSplash,
	int32 InPelletCount, float InSpreadHalfDegrees, float InBlastRadiusCm)
{
	BlastRadiusCm = FMath::Max(InBlastRadiusCm, 0.f);
	StartLocation = Muzzle;
	EndLocation = End;
	Direction = (End - Muzzle).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector();
	}
	FlightSeconds = FMath::Max(InFlightSeconds, 0.f);
	Splash = InSplash;
	ElapsedSeconds = 0.f;
	bStarted = true;
	SetActorLocation(Muzzle);

	// 꼬리 = 속도 × TailSeconds. 속도는 거리 ÷ 시간으로 되짚는다 — 회선에 속도를 따로 싣지 않는다.
	const float DistanceCm = FVector::Dist(Muzzle, End);
	const bool bSmoke = BlastRadiusCm > 0.f;
	TailThicknessScaleNow = TailThicknessScale * (bSmoke ? SmokeTailThicknessMul : 1.f);
	TailLengthCm = FlightSeconds > 0.f
		? FMath::Min(DistanceCm / FlightSeconds * (bSmoke ? SmokeTailSeconds : TailSeconds), DistanceCm) : 0.f;

	MuzzleFlash = MakeGlowPart(TEXT("MuzzleFlash"), SphereMesh, MuzzleColor);
	if (MuzzleFlash != nullptr)
	{
		// 앞으로 길쭉한 불꽃 — 총구에서 뿜는 쪽이 읽힌다.
		MuzzleFlash->SetWorldLocationAndRotation(Muzzle, Direction.Rotation());
		MuzzleFlash->SetWorldScale3D(FVector(MuzzleFlashScale * 1.6f, MuzzleFlashScale, MuzzleFlashScale));
	}
	// 산탄: 펠릿마다 갈래 줄 하나 — 부채꼴 안에 고르게(`CountPelletHits` 와 같은 알파 배치).
	if (InPelletCount > 1)
	{
		const float Distance = FMath::Min(DistanceCm, PelletLineLengthCm);
		for (int32 Index = 0; Index < InPelletCount; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / static_cast<float>(InPelletCount - 1);
			const float YawOffset = FMath::Lerp(-InSpreadHalfDegrees, InSpreadHalfDegrees, Alpha);
			const FRotator Rot = Direction.Rotation() + FRotator(0.f, YawOffset, 0.f);
			UStaticMeshComponent* const Line = MakeGlowPart(*FString::Printf(TEXT("PelletLine%d"), Index), CubeMesh, TailColor);
			if (Line != nullptr)
			{
				Line->SetWorldRotation(Rot);
				Line->SetWorldLocation(Muzzle + Rot.Vector() * (Distance * 0.5f));
				Line->SetWorldScale3D(FVector(Distance / EngineShapeSizeCm, PelletLineThicknessScale, PelletLineThicknessScale));
				PelletLines.Add(Line);
			}
		}
	}
	Bullet = MakeGlowPart(TEXT("Bullet"), SphereMesh, BulletColor);
	if (Bullet != nullptr)
	{
		Bullet->SetWorldLocation(Muzzle);
		Bullet->SetWorldScale3D(FVector(BulletSizeScale));
	}
	// 퍽은 미리 숨겨 만들어 둔다 — 닿는 그 장에 새로 만들면 첫 장에 안 그려져(루프#585 캡처 05번째 장 퍽 없음)
	// 「맞았다」 가 한 장 늦었다. 색은 닿을 때 정한다.
	// 폭발은 가장자리가 부드러운 안개 재질 대신 언릿 불투명 공(발광 재질)으로 — 「노란 안개」 가 아니라 또렷한 불덩이.
	ImpactPuff = MakeGlowPart(TEXT("ImpactPuff"), SphereMesh, FLinearColor::White, bSmoke ? nullptr : PuffMaterial.Get());
	if (bSmoke)
	{
		BlastSmoke = MakeGlowPart(TEXT("BlastSmoke"), SphereMesh, GetBlastSmokeColor());
		if (BlastSmoke != nullptr)
		{
			BlastSmoke->SetWorldLocation(End);
			BlastSmoke->SetVisibility(false);
		}
	}
	if (ImpactPuff != nullptr)
	{
		ImpactPuff->SetWorldLocation(End);
		ImpactPuff->SetWorldScale3D(FVector(ImpactPuffStartScale));
		ImpactPuff->SetVisibility(false);
	}
	if (bSmoke && FlightSeconds > 0.f)
	{
		// 가장자리가 0 인 퍽 재질 공 — 어느 각도에서 봐도 모서리 없는 둥근 연기.
		SmokePuffSpacingNowCm = FMath::Min(DistanceCm / SmokePuffCount, SmokePuffSpacingCm);
		for (int32 Index = 0; Index < SmokePuffCount; ++Index)
		{
			UStaticMeshComponent* const Puff = MakeGlowPart(*FString::Printf(TEXT("SmokePuff%d"), Index), SphereMesh, SmokeColor, PuffMaterial.Get());
			if (Puff != nullptr)
			{
				Puff->SetWorldLocation(GetSmokePuffLocation(Index));
				Puff->SetVisibility(false);
				SmokePuffs.Add(Puff);
			}
		}
	}
	Tail = bSmoke ? nullptr : MakeGlowPart(TEXT("Tail"), CubeMesh, TailColor);
	if (Tail != nullptr)
	{
		Tail->SetWorldRotation(Direction.Rotation());
		Tail->SetVisibility(false);   // 첫 장은 총구에 붙은 점 — 꼬리는 움직인 만큼만 자란다
	}

	UE_LOG(LogSnarehaven, Log, TEXT("[총알] 거리 %.0fcm · %.3f초 · 꼬리 %.0fcm · 튀김 %d · 출발 %s · 첫 덩이 %s"),
		DistanceCm, FlightSeconds, TailLengthCm, static_cast<int32>(Splash), *Muzzle.ToString(),
		SmokePuffs.Num() > 0 ? *GetSmokePuffLocation(0).ToString() : TEXT("(없음)"));
	if (SmokePuffs.Num() > 0)
	{
		UpdateSmokePuffs(0.f, 0.f);   // 쏘는 장에 포구 곁 덩이를 바로 켠다
	}

	if (FlightSeconds <= 0.f)
	{
		Land();
	}
}

void ASnareGunTracerActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bStarted)
	{
		return;
	}
	ElapsedSeconds += DeltaSeconds;

	if (IsValid(MuzzleFlash) && MuzzleFlash->IsVisible() && ElapsedSeconds >= MuzzleFlashSeconds)
	{
		MuzzleFlash->SetVisibility(false);
		for (UStaticMeshComponent* const Line : PelletLines)
		{
			if (IsValid(Line))
			{
				Line->SetVisibility(false);
			}
		}
	}

	if (!bLanded)
	{
		if (ElapsedSeconds >= FlightSeconds)
		{
			Land();
		}
		else
		{
			// 고른 속도로 — 피해가 들어가는 시각(거리 ÷ 속도)과 같은 박자. 로켓만 약하게 휜다.
			const float Traveled = FVector::Dist(StartLocation, EndLocation) * (ElapsedSeconds / FlightSeconds);
			const FVector Head = GetFlightPoint(Traveled);
			if (IsValid(Bullet))
			{
				Bullet->SetWorldLocation(Head);
			}
			// 로켓 연기 덩이는 로켓이 지나간 자리부터 켜진다 — 총구부터 이어져 화면에 길게 남는다.
			UpdateSmokePuffs(Traveled, 0.f);
			const float Visible = FMath::Min(TailLengthCm, Traveled);
			if (IsValid(Tail) && Visible > 0.f)
			{
				Tail->SetVisibility(true);
				Tail->SetWorldLocation(Head - Direction * (Visible * 0.5f));
				Tail->SetWorldScale3D(FVector(Visible / EngineShapeSizeCm, TailThicknessScaleNow, TailThicknessScaleNow));
			}
		}
	}

	if (bLanded && IsValid(ImpactPuff) && ImpactPuff->IsVisible())
	{
		const float Alpha = (ElapsedSeconds - LandedSeconds) / (BlastRadiusCm > 0.f ? BlastPuffSeconds : ImpactPuffSeconds);
		if (Alpha >= 1.f)
		{
			ImpactPuff->SetVisibility(false);
			ImpactPuffOpacity = 0.f;
		}
		else
		{
			ImpactPuff->SetWorldScale3D(FVector(FMath::Lerp(GetPuffStartScale(), GetPuffEndScale(), Alpha)));
			ImpactPuffOpacity = BlastRadiusCm > 0.f ? 1.f - Alpha * Alpha : 1.f - Alpha;   // 폭발 불덩이는 끝에서 급히 꺼진다 — 또렷하게 머물다 사라짐
			if (UMaterialInstanceDynamic* const Mid = Cast<UMaterialInstanceDynamic>(ImpactPuff->GetMaterial(0)))
			{
				Mid->SetScalarParameterValue(TEXT("FlashOpacity"), ImpactPuffOpacity);
			}
		}
	}

	if (bLanded && SmokePuffs.Num() > 0)
	{
		UpdateSmokePuffs(BIG_NUMBER, (ElapsedSeconds - LandedSeconds) / SmokeTrailFadeSeconds);
	}

	if (bLanded && IsValid(BlastSmoke) && BlastSmoke->IsVisible())
	{
		const float SmokeAlpha = (ElapsedSeconds - LandedSeconds) / BlastSmokeSeconds;
		if (SmokeAlpha >= 1.f)
		{
			BlastSmoke->SetVisibility(false);
		}
		else
		{
			BlastSmoke->SetWorldScale3D(FVector(BlastRadiusCm * 2.f / EngineShapeSizeCm * FMath::Lerp(BlastSmokeStartRatio, BlastSmokeEndRatio, SmokeAlpha)));
			if (UMaterialInstanceDynamic* const Mid = Cast<UMaterialInstanceDynamic>(BlastSmoke->GetMaterial(0)))
			{
				// 처음엔 불덩이에 가려 옅게, 불덩이가 꺼질 무렵 짙어졌다가 사라진다.
				Mid->SetScalarParameterValue(TEXT("FlashOpacity"), 0.85f * (1.f - SmokeAlpha) * FMath::Min(1.f, SmokeAlpha * 5.f));
			}
		}
	}

	if (bLanded && GetSplashInstanceCount() > 0)
	{
		const float Age = ElapsedSeconds - LandedSeconds;
		if (Age >= SplashLifeSeconds)
		{
			if (IsValid(SplashPieces))
			{
				SplashPieces->ClearInstances();
			}
			if (IsValid(FurPieces))
			{
				FurPieces->ClearInstances();
			}
		}
		else
		{
			// 별 튀김은 떠서 퍼지고(중력 없음) 흙·나무·돌·털은 떨어진다. 끝으로 갈수록 작아진다.
			const float Shrink = 1.f - Age / SplashLifeSeconds;
			const int32 SplashPieceCount = IsValid(SplashPieces) ? SplashPieces->GetInstanceCount() : 0;
			UpdateSplashPart(SplashPieces, 0, DeltaSeconds, Splash == ESnareGunSplash::Animal ? 0.f : SplashGravity, Shrink);
			UpdateSplashPart(FurPieces, SplashPieceCount, DeltaSeconds, SplashGravity, Shrink);
		}
	}

	const bool bFlashDone = !IsValid(MuzzleFlash) || !MuzzleFlash->IsVisible();
	const bool bSmokeDone = (!IsValid(BlastSmoke) || !BlastSmoke->IsVisible()) && !IsTailVisible();
	if (bLanded && bFlashDone && bSmokeDone && ElapsedSeconds - LandedSeconds >= SplashLifeSeconds)
	{
		Destroy();
	}
}

void ASnareGunTracerActor::Land()
{
	bLanded = true;
	LandedSeconds = ElapsedSeconds;
	if (IsValid(Bullet))
	{
		Bullet->SetWorldLocation(EndLocation);
		Bullet->SetVisibility(false);
	}
	if (IsValid(Tail))
	{
		Tail->SetVisibility(false);
	}
	UpdateSmokePuffs(BIG_NUMBER, 0.f);   // 닿은 장에 연기 덩이 전부 — Tick 이 옅게 지운다
	if (BlastRadiusCm > 0.f)
	{
		USnareSoundSubsystem::PlayAt(this, BlastCue, EndLocation, BlastVolume);
	}
	else
	{
		USnareSoundSubsystem::PlayAt(this, ImpactCue, EndLocation, ImpactVolume);
	}
	++ImpactSoundCount;
	// 그림이 터지는 자리 — 동물이면 이 기기 카메라 쪽 몸 겉(연출 액터라 기기마다 제 카메라 기준이다).
	SplashLocation = EndLocation;
	if (Splash == ESnareGunSplash::Animal)
	{
		if (const APlayerCameraManager* const CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
		{
			SplashLocation += (CameraManager->GetCameraLocation() - EndLocation).GetSafeNormal() * AnimalSplashTowardCameraCm;
		}
	}
	// 퍽 — 동물은 흰 번쩍, 그 밖은 튀김 색을 밝힌 공. 조각보다 먼저 눈에 띄어 「맞았다」 가 한 장에 읽힌다.
	const FLinearColor PuffColor = BlastRadiusCm > 0.f ? GetBlastColor() : Splash == ESnareGunSplash::Animal
		? FLinearColor::White
		: FLinearColor::LerpUsingHSV(GetSplashColor(Splash), FLinearColor::White, 0.4f);
	if (IsValid(ImpactPuff))
	{
		if (UMaterialInstanceDynamic* const Mid = Cast<UMaterialInstanceDynamic>(ImpactPuff->GetMaterial(0)))
		{
			Mid->SetVectorParameterValue(TEXT("FlashColor"), PuffColor);
		}
		ImpactPuff->SetWorldLocation(SplashLocation);
		ImpactPuff->SetWorldScale3D(FVector(GetPuffStartScale()));
		ImpactPuff->SetVisibility(true);
		ImpactPuffOpacity = 1.f;
	}
	if (IsValid(BlastSmoke))
	{
		BlastSmoke->SetWorldLocation(SplashLocation + FVector(0.f, 0.f, BlastRadiusCm * 0.4f));
		BlastSmoke->SetWorldScale3D(FVector(BlastRadiusCm * 2.f * BlastSmokeStartRatio / EngineShapeSizeCm));
		BlastSmoke->SetVisibility(true);
	}
	if (CubeMesh == nullptr)
	{
		return;
	}

	// 모양: 별 = 사방(수평)으로 고르게 퍼지는 하얀 조각 + 털빛 조각 몇 개 · 나무 = 길쭉한 조각 · 흙·돌 = 깍두기.
	const bool bAnimal = Splash == ESnareGunSplash::Animal;
	const bool bBlast = BlastRadiusCm > 0.f;
	const int32 FurCount = bAnimal && !bBlast ? FurPieceCount : 0;
	const int32 MainCount = SplashCount - FurCount;
	// 폭발 파편은 맞은 것과 무관하게 검붉은 흙덩이 — 동물에 맞아도 흰 종이 조각처럼 번쩍이지 않는다(#1211 캡처).
	SplashPieces = MakeSplashPart(TEXT("Splash"), bBlast ? GetBlastDebrisColor() : GetSplashColor(Splash), bAnimal && !bBlast);
	FurPieces = FurCount > 0 ? MakeSplashPart(TEXT("FurSplash"), FurColor, true) : nullptr;

	const FVector Back = -Direction;
	SplashPieceScale = Splash == ESnareGunSplash::Wood
		? FVector(SplashSizeScale * 2.f, SplashSizeScale * 0.5f, SplashSizeScale * 0.5f)
		: FVector(SplashSizeScale * (BlastRadiusCm > 0.f ? BlastPieceSizeMul : 1.f));   // 폭발 파편은 크게 — 불덩이 옆에서 읽힌다
	SplashVelocities.Reset(SplashCount);
	for (int32 Index = 0; Index < SplashCount; ++Index)
	{
		const bool bFur = Index >= MainCount;
		FVector Dir;
		if (bAnimal && !bFur)
		{
			// 위에서 보는 카메라라 수평 고리로 — 조각 사이 각을 조금 흔들어 별 모양이 너무 반듯하지 않게.
			const float Angle = 2.f * PI * (Index + FMath::FRandRange(-0.3f, 0.3f)) / MainCount;
			Dir = FVector(FMath::Cos(Angle), FMath::Sin(Angle), FMath::FRandRange(AnimalSplashLiftMin, AnimalSplashLiftMax)) + Back * 0.3f;
		}
		else
		{
			// 맞은 면에서 쏜 쪽·위로 튄다 — 땅에 박혀 사라지지 않게 Z 는 위로.
			Dir = FMath::VRand() + Back * 0.5f;
			Dir.Z = FMath::Abs(Dir.Z) + 0.3f;
		}
		const FVector Velocity = Dir.GetSafeNormal() * FMath::FRandRange(SplashMinSpeed, SplashMaxSpeed)
			* (BlastRadiusCm > 0.f ? BlastPieceSpeedScale : 1.f);
		SplashVelocities.Add(Velocity);
		UInstancedStaticMeshComponent* const Part = bFur ? FurPieces.Get() : SplashPieces.Get();
		if (IsValid(Part))
		{
			// 첫 장부터 한 장(30fps) 날아간 자리 — 한 점에 20개가 뭉치면 그늘에서 검은 덩이로 보였다(루프#585 캡처).
			const FVector PieceLocation = SplashLocation + Velocity * SplashFirstFrameSeconds;
			Part->AddInstance(FTransform(FRotator(45.f, FMath::FRandRange(0.f, 360.f), 45.f), PieceLocation, SplashPieceScale), true);
		}
	}
}

UInstancedStaticMeshComponent* ASnareGunTracerActor::MakeSplashPart(const TCHAR* Name, const FLinearColor& Color, bool bGlow)
{
	UInstancedStaticMeshComponent* const Part = NewObject<UInstancedStaticMeshComponent>(this, Name);
	Part->SetStaticMesh(CubeMesh);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCastShadow(false);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetUsingAbsoluteLocation(true);
	Part->SetUsingAbsoluteRotation(true);
	Part->SetUsingAbsoluteScale(true);
	// 동물 튀김은 퍽 재질을 고른 발광(EdgeFalloff 0)으로 — 조명 받는 조각은 나무 그늘에서 검은 덩이였다(루프#585 캡처).
	// 흙·나무·돌은 땅의 일부처럼 조명을 받는 게 맞아 기본 재질 그대로.
	if (bGlow && PuffMaterial != nullptr)
	{
		UMaterialInstanceDynamic* const Mid = UMaterialInstanceDynamic::Create(PuffMaterial, this);
		Mid->SetVectorParameterValue(TEXT("FlashColor"), Color);
		Mid->SetScalarParameterValue(TEXT("FlashOpacity"), 1.f);
		Mid->SetScalarParameterValue(TEXT("EdgeFalloff"), 0.f);
		Part->SetMaterial(0, Mid);
	}
	else if (SplashMaterial != nullptr)
	{
		UMaterialInstanceDynamic* const Mid = UMaterialInstanceDynamic::Create(SplashMaterial, this);
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Part->SetMaterial(0, Mid);
	}
	Part->SetupAttachment(GetRootComponent());
	Part->RegisterComponent();
	Part->SetWorldLocationAndRotation(SplashLocation, FRotator::ZeroRotator);
	return Part;
}

void ASnareGunTracerActor::UpdateSplashPart(UInstancedStaticMeshComponent* Part, int32 FirstVelocity, float DeltaSeconds,
	float Gravity, float Shrink)
{
	if (!IsValid(Part))
	{
		return;
	}
	const int32 Count = FMath::Min(Part->GetInstanceCount(), SplashVelocities.Num() - FirstVelocity);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FVector& Velocity = SplashVelocities[FirstVelocity + Index];
		FTransform Instance;
		Part->GetInstanceTransform(Index, Instance, true);
		Velocity.Z -= Gravity * DeltaSeconds;
		Instance.AddToTranslation(Velocity * DeltaSeconds);
		Instance.SetScale3D(SplashPieceScale * Shrink);
		Part->UpdateInstanceTransform(Index, Instance, true, Index == Count - 1);
	}
}

ESnareGunSplash ASnareGunTracerActor::ClassifyWorldHit(const FHitResult& Hit)
{
	if (const AGatherableActor* const Gatherable = Cast<AGatherableActor>(Hit.GetActor()))
	{
		return UGatherComponent::IsMiningTool(Gatherable->GetRequiredToolItemID()) ? ESnareGunSplash::Stone : ESnareGunSplash::Wood;
	}
	return Hit.ImpactNormal.Z >= GroundNormalZ ? ESnareGunSplash::Dust : ESnareGunSplash::Stone;
}

FLinearColor ASnareGunTracerActor::GetSplashColor(ESnareGunSplash InSplash)
{
	switch (InSplash)
	{
	case ESnareGunSplash::Animal: return FLinearColor(1.f, 1.f, 1.f);
	case ESnareGunSplash::Wood:   return FLinearColor(0.7f, 0.5f, 0.28f);
	case ESnareGunSplash::Stone:  return FLinearColor(0.5f, 0.5f, 0.52f);
	case ESnareGunSplash::Dust:
	default:                      return FLinearColor(0.45f, 0.35f, 0.25f);   // 피격 파편 흙 갈색과 같다
	}
}

FLinearColor ASnareGunTracerActor::GetBlastColor()
{
	return FLinearColor(1.f, 0.32f, 0.04f);   // 주황 불덩이 — 언릿이라 밝게 떠 노랗게 보여서 붉은 쪽으로 — 총구 불꽃(1, 0.65, 0.15)보다 붉게
}

FLinearColor ASnareGunTracerActor::GetBlastDebrisColor()
{
	return FLinearColor(0.3f, 0.14f, 0.07f);   // 검붉은 흙덩이
}

FLinearColor ASnareGunTracerActor::GetBlastSmokeColor()
{
	return FLinearColor(0.1f, 0.09f, 0.09f);   // 검은 연기
}

float ASnareGunTracerActor::GetPuffStartScale() const
{
	return BlastRadiusCm > 0.f ? BlastRadiusCm * 2.f * BlastPuffStartRatio / EngineShapeSizeCm : ImpactPuffStartScale;
}

float ASnareGunTracerActor::GetPuffEndScale() const
{
	return BlastRadiusCm > 0.f ? BlastRadiusCm * 2.f / EngineShapeSizeCm : ImpactPuffEndScale;
}

bool ASnareGunTracerActor::IsBulletVisible() const
{
	return IsValid(Bullet) && Bullet->IsVisible();
}

FVector ASnareGunTracerActor::GetBulletLocation() const
{
	return IsValid(Bullet) ? Bullet->GetComponentLocation() : FVector::ZeroVector;
}

bool ASnareGunTracerActor::IsMuzzleFlashVisible() const
{
	return IsValid(MuzzleFlash) && MuzzleFlash->IsVisible();
}

bool ASnareGunTracerActor::IsImpactPuffVisible() const
{
	return IsValid(ImpactPuff) && ImpactPuff->IsVisible();
}

float ASnareGunTracerActor::GetImpactPuffDiameterCm() const
{
	return IsValid(ImpactPuff) ? ImpactPuff->GetComponentScale().X * EngineShapeSizeCm : 0.f;
}

void ASnareGunTracerActor::UpdateSmokePuffs(float ReachedCm, float FadeAlpha)
{
	const int32 Count = SmokePuffs.Num();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UStaticMeshComponent* const Puff = SmokePuffs[Index];
		if (!IsValid(Puff))
		{
			continue;
		}
		// 포구 곁 SmokePuffInstantCount 개는 쏘는 장에 바로 — 나머지는 로켓이 지나간 만큼.
		const bool bReached = Index < SmokePuffInstantCount || Index * SmokePuffSpacingNowCm <= ReachedCm;
		if (!bReached || FadeAlpha >= 1.f)
		{
			Puff->SetVisibility(false);
			continue;
		}
		const float Far = Count > 1 ? static_cast<float>(Index) / (Count - 1) : 0.f;   // 0 = 포구 곁, 1 = 맨 끝
		const float SizeCm = FMath::Lerp(SmokePuffNearCm, SmokePuffFarCm, Far) * FMath::Lerp(1.f, SmokeTrailFadeWidenMul, FadeAlpha);
		Puff->SetVisibility(true);
		Puff->SetWorldScale3D(FVector(SizeCm / EngineShapeSizeCm));
		if (UMaterialInstanceDynamic* const Mid = Cast<UMaterialInstanceDynamic>(Puff->GetMaterial(0)))
		{
			Mid->SetScalarParameterValue(TEXT("FlashOpacity"), FMath::Lerp(SmokePuffNearOpacity, SmokePuffFarOpacity, Far) * (1.f - FadeAlpha));
		}
	}
}

FVector ASnareGunTracerActor::GetSmokePuffLocation(int32 Index) const
{
	// 첫 덩이 가운데 = 포구 끝 — 덩이가 총구에 붙어 시작한다.
	return GetFlightPoint(Index * SmokePuffSpacingNowCm);
}

FVector ASnareGunTracerActor::GetFlightPoint(float Traveled) const
{
	const FVector OnLine = StartLocation + Direction * Traveled;
	const float DistanceCm = FVector::Dist(StartLocation, EndLocation);
	if (BlastRadiusCm <= 0.f || DistanceCm <= 0.f)
	{
		return OnLine;
	}
	const float Alpha = FMath::Clamp(Traveled / DistanceCm, 0.f, 1.f);
	return OnLine + FVector::UpVector * (DistanceCm * RocketArcApexRatio * 4.f * Alpha * (1.f - Alpha));
}

bool ASnareGunTracerActor::IsTailVisible() const
{
	return (IsValid(Tail) && Tail->IsVisible()) || GetVisibleSmokePuffCount() > 0;
}

int32 ASnareGunTracerActor::GetVisibleSmokePuffCount() const
{
	int32 Visible = 0;
	for (const UStaticMeshComponent* const Puff : SmokePuffs)
	{
		Visible += IsValid(Puff) && Puff->IsVisible() ? 1 : 0;
	}
	return Visible;
}

float ASnareGunTracerActor::GetTailThicknessCm()
{
	return TailThicknessScale * EngineShapeSizeCm;
}

int32 ASnareGunTracerActor::GetSplashInstanceCount() const
{
	return (IsValid(SplashPieces) ? SplashPieces->GetInstanceCount() : 0) + GetFurPieceCount();
}

int32 ASnareGunTracerActor::GetFurPieceCount() const
{
	return IsValid(FurPieces) ? FurPieces->GetInstanceCount() : 0;
}

float ASnareGunTracerActor::GetImpactPuffOpacity() const
{
	return IsImpactPuffVisible() ? ImpactPuffOpacity : 0.f;
}

float ASnareGunTracerActor::GetSplashPieceSizeCm()
{
	return SplashSizeScale * EngineShapeSizeCm;
}
