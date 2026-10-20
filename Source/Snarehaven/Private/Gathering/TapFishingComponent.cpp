#include "Gathering/TapFishingComponent.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Data/FishingRow.h"
#include "Data/ItemRow.h"
#include "Data/ItemRowLookup.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Equipment/EquipmentComponent.h"
#include "GameFramework/Pawn.h"
#include "Inventory/LootClaimComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "World/FishingSpotActor.h"
#include "World/WaterRules.h"
#include "World/SnareSound.h"

namespace
{
	const FName WormBaitItemId(TEXT("item.worm_bait"));
	const FName BerryBaitItemId(TEXT("item.berry_bait"));
	const FName LuckyRodItemId(TEXT("item.rod_lucky"));
	// 찌·줄은 엔진 기본 도형에 색만 입힌다 — 새 에셋 없이 (SnareCaptureBurstActor 와 같은 길).
	const TCHAR* const SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* const CylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const FLinearColor BobberColor(0.9f, 0.15f, 0.08f);
	const FLinearColor LineColor(1.f, 0.95f, 0.55f);   // 연노랑 — 파란 물 위에서 흰 줄은 물결 무늬에 묻혔다(감독 10-16)
}

UTapFishingComponent::UTapFishingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UTapFishingComponent::BeginPlay()
{
	Super::BeginPlay();
	WaterTable = LoadObject<UDataTable>(nullptr, SnareWater::TablePath);
	CachedRow = SnareWater::FindRow(WaterTable);
	ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());

	float TapFraction = 0.f;
	CreateBobberAndLine();
	if (SnareCheckSwitch::Value(TEXT("SnareFishTap="), TapFraction))
	{
		HoldTestTapping(TapFraction);
	}
	bTestHoldWait = SnareCheckSwitch::Param(TEXT("SnareFishWait"));
}

void UTapFishingComponent::HoldTestTapping(float Fraction)
{
	const FWaterActivityRow* const Row = GetRow();
	if (Row == nullptr)
	{
		return;
	}
	TestHoldFraction = FMath::Clamp(Fraction, 0.f, 1.f);
	CastLocation = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	// 포인트 찾기 전 자리표시 — 스폰 직후라 `-SnareStandNear` 순간이동 전이다. 진짜 자리는 AdvanceFishing 이 매 틱 포인트로 고친다.
	ShownCastTarget = GetOwner() ? CastLocation + GetOwner()->GetActorForwardVector() * 300.f
		- FVector(0.f, 0.f, GetOwner()->GetSimpleCollisionHalfHeight()) : CastLocation;
	Progress = Row->TapGoal * TestHoldFraction;
	PressCount = 0;
	SecondsAtZero = 0.f;
	SetState(ETapFishingState::Tapping);
	UE_LOG(LogSnarehaven, Log, TEXT("[Fish] 검사용 연타 붙듦 (-SnareFishTap=%.2f) → 몫 %.2f"), TestHoldFraction, TestHoldFraction);
}

void UTapFishingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UTapFishingComponent, ShownState, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UTapFishingComponent, ShownPressCount, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UTapFishingComponent, ShownCastTarget, COND_SkipOwner);
}

void UTapFishingComponent::SetState(ETapFishingState NewState)
{
	State = NewState;
	ShownState = NewState;
	// 연타 시작마다 0 — 지난 판 수가 남아 있으면 남의 화면이 첫 누름 전부터 빨리 감는다.
	if (NewState == ETapFishingState::Tapping)
	{
		ShownPressCount = 0;
	}
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		Server_SetShownState(NewState, ShownCastTarget);
	}
}

void UTapFishingComponent::Server_SetShownState_Implementation(ETapFishingState NewState, FVector_NetQuantize10 CastTarget)
{
	if (static_cast<uint8>(NewState) <= static_cast<uint8>(ETapFishingState::Catching))
	{
		ShownState = NewState;
		ShownCastTarget = CastTarget;
		if (NewState == ETapFishingState::Tapping)
		{
			ShownPressCount = 0;
		}
	}
}

void UTapFishingComponent::Server_SetShownPressCount_Implementation(uint8 NewCount)
{
	ShownPressCount = NewCount;
}

const FWaterActivityRow* UTapFishingComponent::GetRow() const
{
	return CachedRow;
}

float UTapFishingComponent::GetGoal() const
{
	return CachedRow ? CachedRow->TapGoal : 0.f;
}

void UTapFishingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvanceFishing(DeltaTime);
	UpdateBobberAndLine(DeltaTime);
}

void UTapFishingComponent::CreateBobberAndLine()
{
	AActor* const Owner = GetOwner();
	if (Owner == nullptr || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UMaterialInterface* const Material = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
	auto Make = [Owner, Material](const TCHAR* Name, const TCHAR* MeshPath, const FLinearColor& Color)
	{
		UStaticMeshComponent* const Mesh = NewObject<UStaticMeshComponent>(Owner, Name);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
		Mesh->SetUsingAbsoluteLocation(true);
		Mesh->SetUsingAbsoluteRotation(true);
		Mesh->SetUsingAbsoluteScale(true);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetVisibility(false);
		if (Material)
		{
			UMaterialInstanceDynamic* const Mid = UMaterialInstanceDynamic::Create(Material, Mesh);
			Mid->SetVectorParameterValue(TEXT("Color"), Color);
			Mesh->SetMaterial(0, Mid);
		}
		Mesh->RegisterComponent();
		return Mesh;
	};
	BobberMesh = Make(TEXT("FishBobber"), SphereMeshPath, BobberColor);
	for (int32 Index = 0; Index < FMath::Max(LineSegments, 1); ++Index)
	{
		LineMeshes.Add(Make(*FString::Printf(TEXT("FishLine%d"), Index), CylinderMeshPath, LineColor));
	}
}

FVector UTapFishingComponent::GetRodTipLocation() const
{
	const AActor* const Owner = GetOwner();
	if (Owner == nullptr)
	{
		return FVector::ZeroVector;
	}
	const UEquipmentComponent* const Equipment = Owner->FindComponentByClass<UEquipmentComponent>();
	const UStaticMeshComponent* const Rod = Equipment ? Equipment->GetEquippedMesh(EEquipSlot::Tool) : nullptr;
	if (Rod && Rod->GetStaticMesh())
	{
		// 낚싯대 메시는 상자 안에 대각선으로 놓여 있다(SM_Tool_FishingRod_01 87×36×71cm) — 곧은 대의 끝은
		// 손(붙은 소켓)에서 가장 먼 상자 모서리다. 상자 위쪽 가운데를 쓰면 대 중간 허공에서 줄이 끝났다(감독 PIE 10-05).
		// ponytail: 곧은 대 가정, 휜 대가 들어오면 메시 소켓 `Tip` 으로 바꾼다.
		const FBox Box = Rod->GetStaticMesh()->GetBoundingBox();
		const FTransform& RodXf = Rod->GetComponentTransform();
		const FVector Hand = Rod->GetAttachParent()
			? Rod->GetAttachParent()->GetSocketLocation(Rod->GetAttachSocketName())
			: RodXf.GetLocation();
		FVector Tip = RodXf.GetLocation();
		double BestSq = -1.0;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Local((Corner & 1) ? Box.Max.X : Box.Min.X, (Corner & 2) ? Box.Max.Y : Box.Min.Y, (Corner & 4) ? Box.Max.Z : Box.Min.Z);
			const FVector World = RodXf.TransformPosition(Local);
			const double DistSq = FVector::DistSquared(World, Hand);
			if (DistSq > BestSq)
			{
				BestSq = DistSq;
				Tip = World;
			}
		}
		return Tip;
	}
	return Owner->GetActorLocation() + FVector(0.f, 0.f, Owner->GetSimpleCollisionHalfHeight());
}

void UTapFishingComponent::UpdateBobberAndLine(float DeltaSeconds)
{
	AActor* const Owner = GetOwner();
	if (BobberMesh == nullptr || LineMeshes.IsEmpty() || Owner == nullptr)
	{
		return;
	}
	const ETapFishingState Now = ShownState;
	if (Now != DrawnState)
	{
		// 잡기 응답이 바로 와서 Catching 을 한 번도 못 그리고 Idle 이 돼도(감기 → 쉼) 발밑으로 튀어 드는 길은 그린다.
		// Catching → Idle 은 이미 튀는 중이라 이어 간다. 입질 전 취소(대기 → 쉼)는 바로 숨긴다.
		const bool bContinueHop = DrawnState == ETapFishingState::Catching && Now == ETapFishingState::Idle;
		if (!bContinueHop)
		{
			BobberFrom = BobberMesh->IsVisible() ? BobberMesh->GetComponentLocation() : GetRodTipLocation();
			DrawnStateSeconds = 0.f;
		}
		bHopping = Now == ETapFishingState::Catching || bContinueHop
			|| (DrawnState == ETapFishingState::Tapping && Now == ETapFishingState::Idle);
		DrawnState = Now;
	}
	DrawnStateSeconds += DeltaSeconds;
	const float T = DrawnStateSeconds;
	const FVector Target = ShownCastTarget;
	const float Float = BobberFloatAmplitudeCm * FMath::Sin(2.f * PI * T / FMath::Max(BobberFloatPeriodSeconds, 0.01f));

	FVector Bobber;
	if (bHopping)
	{
		const float A = FMath::Clamp(T / FMath::Max(BobberHopSeconds, 0.01f), 0.f, 1.f);
		if (A >= 1.f && Now == ETapFishingState::Idle)
		{
			bHopping = false;
		}
		const FVector Feet = Owner->GetActorLocation() - FVector(0.f, 0.f, Owner->GetSimpleCollisionHalfHeight());
		Bobber = FMath::Lerp(BobberFrom, Feet, A) + FVector(0.f, 0.f, FMath::Sin(PI * A) * BobberArcHeightCm * 0.5f);
	}
	else if (Now == ETapFishingState::WaitingBite && T < BobberFlightSeconds)
	{
		const float A = T / FMath::Max(BobberFlightSeconds, 0.01f);
		Bobber = FMath::Lerp(BobberFrom, Target, A) + FVector(0.f, 0.f, FMath::Sin(PI * A) * BobberArcHeightCm);
	}
	else if (Now == ETapFishingState::WaitingBite)
	{
		Bobber = Target + FVector(0.f, 0.f, Float);
	}
	else if (Now == ETapFishingState::Rolling)
	{
		// 입질 — 물속으로 쿡 잠긴다(! 말풍선은 HUD 가 이 찌 위에 띄운다).
		Bobber = Target - FVector(0.f, 0.f, BobberBiteDipCm + Float);
	}
	else if (Now == ETapFishingState::Tapping)
	{
		// 주인은 실제 끌어온 몫, 남의 화면은 진행을 모르니 시간으로 다가온다.
		const float Pull = State == ETapFishingState::Tapping && GetGoal() > 0.f
			? Progress / GetGoal() : T / FMath::Max(BobberRemotePullSeconds, 0.01f);
		const FVector Shore(Owner->GetActorLocation().X, Owner->GetActorLocation().Y, Target.Z);
		Bobber = FMath::Lerp(Target, Shore, FMath::Clamp(Pull, 0.f, 1.f) * BobberMaxPull) - FVector(0.f, 0.f, BobberBiteDipCm * 0.5f);
	}
	const bool bShowBobber = bHopping || (Now != ETapFishingState::Idle && Now != ETapFishingState::Catching);
	const bool bShowLine = bShowBobber && !bHopping;
	AimRod(bShowLine);
	BobberMesh->SetVisibility(bShowBobber);
	for (UStaticMeshComponent* const Segment : LineMeshes)
	{
		Segment->SetVisibility(bShowLine);
	}
	if (!bShowBobber)
	{
		return;
	}
	if (const UStaticMesh* const Sphere = BobberMesh->GetStaticMesh())
	{
		const float Size = FMath::Max(Sphere->GetBoundingBox().GetSize().X, 1.f);
		BobberMesh->SetWorldLocationAndRotation(Bobber, FRotator::ZeroRotator);
		BobberMesh->SetWorldScale3D(FVector(BobberDiameterCm / Size));
	}
	const UStaticMesh* const Cylinder = LineMeshes[0]->GetStaticMesh();
	if (!bShowLine || Cylinder == nullptr)
	{
		return;
	}
	// 대 끝 → 찌를 처진 곡선으로 — 토막마다 원통 하나, 메시 Z 범위를 늘여 두 끝을 정확히 맞춘다.
	// 곧은 줄은 막대처럼 보였다. 감기 중엔 거의 팽팽, 찌 아래(물속)로는 내려가지 않는다.
	const FVector Tip = GetRodTipLocation();
	const float Sag = FVector::Dist(Tip, Bobber) * (Now == ETapFishingState::Tapping ? LineReelSagRatio : LineSagRatio);
	const FBox Box = Cylinder->GetBoundingBox();
	const float Height = FMath::Max(Box.GetSize().Z, 1.f);
	const float Width = FMath::Max(Box.GetSize().X, 1.f);
	FVector From = Tip;
	for (int32 Index = 0; Index < LineMeshes.Num(); ++Index)
	{
		const float A = static_cast<float>(Index + 1) / LineMeshes.Num();
		FVector To = FMath::Lerp(Tip, Bobber, A) - FVector(0.f, 0.f, Sag * 4.f * A * (1.f - A));
		To.Z = FMath::Max(To.Z, Bobber.Z);
		const FVector Span = To - From;
		const float Length = FMath::Max(Span.Size(), 0.1f);
		const FQuat Rot = FRotationMatrix::MakeFromZ(Span / Length).ToQuat();
		const FVector Scale(LineDiameterCm / Width, LineDiameterCm / Width, Length / Height);
		LineMeshes[Index]->SetWorldTransform(FTransform(Rot, From - Rot.RotateVector(FVector(0.f, 0.f, Box.Min.Z * Scale.Z)), Scale));
		From = To;
	}
}

void UTapFishingComponent::AimRod(bool bAim)
{
	const UEquipmentComponent* const Equipment = GetOwner() ? GetOwner()->FindComponentByClass<UEquipmentComponent>() : nullptr;
	UStaticMeshComponent* const Rod = Equipment ? Equipment->GetEquippedMesh(EEquipSlot::Tool) : nullptr;
	if (AimedRod != nullptr && (!bAim || AimedRod != Rod))
	{
		if (IsValid(AimedRod))
		{
			AimedRod->SetRelativeRotation(RodRestRelative);
		}
		AimedRod = nullptr;
	}
	if (!bAim || Rod == nullptr || Rod->GetStaticMesh() == nullptr || Rod->GetAttachParent() == nullptr)
	{
		return;
	}
	if (AimedRod == nullptr)
	{
		AimedRod = Rod;
		RodRestRelative = Rod->GetRelativeRotation().Quaternion();
	}
	// 대 끝 = 메시 원점(손잡이)에서 가장 먼 상자 모서리(`GetRodTipLocation` 과 같은 가정). 든 자세의 가로 방향은 두고 위로만 세운다.
	const FBox Box = Rod->GetStaticMesh()->GetBoundingBox();
	FVector LocalTip = FVector::ZeroVector;
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Local((Corner & 1) ? Box.Max.X : Box.Min.X, (Corner & 2) ? Box.Max.Y : Box.Min.Y, (Corner & 4) ? Box.Max.Z : Box.Min.Z);
		if (Local.SizeSquared() > LocalTip.SizeSquared())
		{
			LocalTip = Local;
		}
	}
	const FQuat Socket = Rod->GetAttachParent()->GetSocketQuaternion(Rod->GetAttachSocketName());
	const FVector Rest = (Socket * RodRestRelative).RotateVector(LocalTip).GetSafeNormal();
	FVector Flat = FVector(Rest.X, Rest.Y, 0.f).GetSafeNormal();
	if (Flat.IsNearlyZero())
	{
		Flat = GetOwner()->GetActorForwardVector();
	}
	const float Pitch = FMath::DegreesToRadians(RodAimPitchDegrees);
	const FVector Want = Flat * FMath::Cos(Pitch) + FVector::UpVector * FMath::Sin(Pitch);
	const FQuat Lift = FQuat::FindBetweenNormals(Rest, Want);
	Rod->SetRelativeRotation(Socket.Inverse() * Lift * Socket * RodRestRelative);
}

float UTapFishingComponent::ReadGearSpeedMul() const
{
	const UEquipmentComponent* const Equipment = GetOwner() ? GetOwner()->FindComponentByClass<UEquipmentComponent>() : nullptr;
	const FItemRow* const Rod = Equipment ? Equipment->GetEquippedRow(EEquipSlot::Tool) : nullptr;
	if (Rod == nullptr || Rod->FishingSpeedMul <= 0.f)
	{
		return 0.f;
	}
	const FItemRow* const Hat = Equipment->GetEquippedRow(EEquipSlot::Head);
	return Rod->FishingSpeedMul * (Hat && Hat->FishingSpeedMul > 0.f ? Hat->FishingSpeedMul : 1.f);
}

bool UTapFishingComponent::HoldsLuckyRod() const
{
	const UEquipmentComponent* const Equipment = GetOwner() ? GetOwner()->FindComponentByClass<UEquipmentComponent>() : nullptr;
	const FItemRow* const Rod = Equipment ? Equipment->GetEquippedRow(EEquipSlot::Tool) : nullptr;
	return Rod && Rod->ItemID == LuckyRodItemId;
}

float UTapFishingComponent::GetCastRangeCm() const
{
	const FWaterActivityRow* const Row = GetRow();
	return Row ? Row->CastRangeCm : 0.f;
}

bool UTapFishingComponent::HasSpotInCastRange() const
{
	// `CanCast` 와 갈라 둔다 — 저쪽은 "지금 던질 수 있나"(낚싯대·단계까지), 이쪽은 "여기가 낚시 자리인가" 하나다.
	// HUD 가 낚싯대를 **들기 전에** 물어야 해서 도구 조건이 없다 (M15-08).
	return GetOwner() != nullptr && GetCastRangeCm() > 0.f
		&& AFishingSpotActor::FindNearestAvailable(this, GetOwner()->GetActorLocation(), GetCastRangeCm()) != nullptr;
}

bool UTapFishingComponent::CanCast() const
{
	const FWaterActivityRow* const Row = GetRow();
	return State == ETapFishingState::Idle && Row && GetOwner() && ReadGearSpeedMul() > 0.f
		&& AFishingSpotActor::FindNearestAvailable(this, GetOwner()->GetActorLocation(), Row->CastRangeCm) != nullptr;
}

void UTapFishingComponent::CastAtNearest()
{
	if (!CanCast())
	{
		return;
	}
	AFishingSpotActor* const Spot = AFishingSpotActor::FindNearestAvailable(this, GetOwner()->GetActorLocation(), GetRow()->CastRangeCm);
	// 🟡 미끼는 자동 — 지렁이가 있으면 지렁이, 없으면 산딸기, 둘 다 없으면 맨 바늘.
	const bool bWorm = SnareWater::CountOwned(this, WormBaitItemId) > 0;
	BaitArg = bWorm ? TEXT("worm") : SnareWater::CountOwned(this, BerryBaitItemId) > 0 ? TEXT("berry") : TEXT("");
	LastMessage.Reset();
	RodSoundLog.Reset();   // 새 판
	PlayRodCue(&FItemRow::UseSoundCue, TEXT("던지기"), 0.6f);   // 낚싯대마다 다른 휙 소리 (표 칸)
	USnareSoundSubsystem::PlayAt(this, TEXT("Splash"), Spot->GetActorLocation(), 0.5f);   // 찌가 물에 떨어짐 (DESIGN 2.2 소리)
	if (GetOwner()->HasAuthority())
	{
		RollBiteOnAuthority(Spot, bWorm);
	}
	else
	{
		Server_RequestBite(Spot, bWorm);
	}
}

void UTapFishingComponent::PlayRodCue(FName FItemRow::* Pick, const TCHAR* Phase, float Volume)
{
	const UEquipmentComponent* const Equipment = GetOwner() ? GetOwner()->FindComponentByClass<UEquipmentComponent>() : nullptr;
	const FItemRow* const Rod = Equipment ? Equipment->GetEquippedRow(EEquipSlot::Tool) : nullptr;
	if (Rod == nullptr || (Rod->*Pick).IsNone())
	{
		return;
	}
	const FName Cue = Rod->*Pick;
	RodSoundLog.Add(Cue);
	USnareSoundSubsystem::Play2D(this, Cue, Volume);
	UE_LOG(LogSnarehaven, Log, TEXT("[Fishing] 소리 %s: %s (%s)"), Phase, *Cue.ToString(), *Rod->ItemID.ToString());
}

void UTapFishingComponent::StartReelLoop()
{
	StopReelLoop();
	const UEquipmentComponent* const Equipment = GetOwner() ? GetOwner()->FindComponentByClass<UEquipmentComponent>() : nullptr;
	const FItemRow* const Rod = Equipment ? Equipment->GetEquippedRow(EEquipSlot::Tool) : nullptr;
	if (Rod == nullptr || Rod->ReelSoundCue.IsNone())
	{
		return;
	}
	ReelLoop = USnareSoundSubsystem::SpawnLoopAttached(this, Rod->ReelSoundCue, GetOwner()->GetRootComponent(), 0.4f);
	if (IsValid(ReelLoop))
	{
		ReelLoop->Play();
		RodSoundLog.Add(Rod->ReelSoundCue);
		UE_LOG(LogSnarehaven, Log, TEXT("[Fishing] 소리 감기 시작: %s (%s)"), *Rod->ReelSoundCue.ToString(), *Rod->ItemID.ToString());
	}
}

void UTapFishingComponent::StopReelLoop()
{
	if (IsValid(ReelLoop))
	{
		ReelLoop->Stop();
		ReelLoop->DestroyComponent();
	}
	ReelLoop = nullptr;
}

bool UTapFishingComponent::IsReelLoopPlaying() const
{
	return IsValid(ReelLoop) && ReelLoop->IsPlaying();
}

void UTapFishingComponent::Server_RequestBite_Implementation(AFishingSpotActor* Spot, bool bWormBait)
{
	RollBiteOnAuthority(Spot, bWormBait);
}

void UTapFishingComponent::RollBiteOnAuthority(AFishingSpotActor* Spot, bool bWormBait)
{
	const FWaterActivityRow* const Row = GetRow();
	const float Gear = ReadGearSpeedMul();
	// 호스트가 거리를 다시 본다.
	if (!Row || Gear <= 0.f || !IsValid(Spot) || !Spot->IsAvailable()
		|| FVector::Dist2D(Spot->GetActorLocation(), GetOwner()->GetActorLocation()) > Row->CastRangeCm)
	{
		return;
	}
	const float WaitSeconds = SnareWater::BiteWaitSeconds(*Row, FMath::FRand(), Gear, bWormBait);
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	if (Pawn && !Pawn->IsLocallyControlled() && Pawn->GetNetMode() != NM_Standalone)
	{
		Client_StartBiteWait(Spot, WaitSeconds, bWormBait);
	}
	else
	{
		BeginBiteWait(Spot, WaitSeconds, bWormBait);
	}
}

void UTapFishingComponent::Client_StartBiteWait_Implementation(AFishingSpotActor* Spot, float WaitSeconds, bool bWormBait)
{
	BeginBiteWait(Spot, WaitSeconds, bWormBait);
}

void UTapFishingComponent::BeginBiteWait(AFishingSpotActor* Spot, float WaitSeconds, bool bWormBait)
{
	if (State != ETapFishingState::Idle || !IsValid(Spot) || !GetOwner())
	{
		return;
	}
	++Attempt;
	LastCatchPopup = FText::GetEmpty();   // 새 판 — 지난 판 글이 남아 이번 판 결과로 읽히지 않게
	CurrentSpot = Spot;
	CastLocation = GetOwner()->GetActorLocation();
	ShownCastTarget = Spot->GetActorLocation();   // 포인트 Z = 물 표면 (그림자가 Z 0)
	bUsingWorm = bWormBait;
	if (bWormBait)
	{
		BaitArg = TEXT("worm");
	}
	BiteRemainingSeconds = WaitSeconds;
	SetState(ETapFishingState::WaitingBite);
	UE_LOG(LogSnarehaven, Log, TEXT("[Fishing] 던짐 %s · 입질까지 %.2f초 · 미끼 '%s'"), *Spot->SpotId, WaitSeconds, *BaitArg);
}

void UTapFishingComponent::AdvanceFishing(float DeltaSeconds)
{
	const FWaterActivityRow* const Row = GetRow();
	if (Row == nullptr)
	{
		return;
	}
	// 검사 스위치 `-SnareFishWait`: 순간이동(-SnareStandNear) 뒤 포인트가 닿으면 한 번 던지고 입질은 안 오게 붙든다.
	if (bTestHoldWait && State == ETapFishingState::Idle && GetOwner())
	{
		if (AFishingSpotActor* const Spot = AFishingSpotActor::FindNearestAvailable(this, GetOwner()->GetActorLocation(), Row->CastRangeCm))
		{
			bTestHoldWait = false;
			BeginBiteWait(Spot, 1.0e6f, false);
			UE_LOG(LogSnarehaven, Log, TEXT("[Fish] 검사용 입질 대기 붙듦 (-SnareFishWait) %s"), *Spot->SpotId);
		}
	}
	if (State == ETapFishingState::WaitingBite)
	{
		if (FVector::Dist(GetOwner()->GetActorLocation(), CastLocation) > CancelMoveToleranceCm)
		{
			EndFishing(TEXT("움직여서 물고기가 달아났다"));
			return;
		}
		BiteRemainingSeconds -= DeltaSeconds;
		if (BiteRemainingSeconds <= 0.f)
		{
			StartRolling();
		}
		return;
	}
	if (State == ETapFishingState::Tapping)
	{
		// 검사용으로 붙들어 둔 판은 줄지도 끝나지도 않는다 — 캡처 지연(몇 초) 동안 몫이 새어 나가면 빈 바를 찍는다.
		if (TestHoldFraction >= 0.f)
		{
			Progress = Row->TapGoal * TestHoldFraction;
			// BeginPlay 에서 굳힌 자리는 순간이동 전 출발점 앞 땅이었다(감독 캡처 10-16 「줄·찌가 안 보인다」) — 지금 선 곳의 포인트로.
			if (const AFishingSpotActor* const Spot = AFishingSpotActor::FindNearestAvailable(this, GetOwner()->GetActorLocation(), Row->CastRangeCm))
			{
				CastLocation = GetOwner()->GetActorLocation();
				ShownCastTarget = Spot->GetActorLocation();
			}
			return;
		}
		// 끌어오는 중에도 걸어 나가면 줄이 끊긴다 — 전엔 입질 대기만 봐서 연타하며 섬을 돌아다녔다(감독 조작 조사 #7).
		if (FVector::Dist(GetOwner()->GetActorLocation(), CastLocation) > CancelMoveToleranceCm)
		{
			EndFishing(TEXT("움직여서 줄이 끊겼다"));
			return;
		}
		Progress = FMath::Max(0.f, Progress - Row->TapDecayPerSecond * DeltaSeconds);
		SecondsAtZero = Progress <= 0.f ? SecondsAtZero + DeltaSeconds : 0.f;
		if (SecondsAtZero + KINDA_SMALL_NUMBER >= Row->TapFailSeconds)
		{
			EndFishing(TEXT("놓쳤다"));
		}
	}
}

void UTapFishingComponent::StartRolling()
{
	SetState(ETapFishingState::Rolling);
	const int32 ThisAttempt = Attempt;
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("spotId"), CurrentSpot ? CurrentSpot->SpotId : FString());
	if (!BaitArg.IsEmpty())
	{
		Args->SetStringField(TEXT("bait"), BaitArg);
	}
	TWeakObjectPtr<UTapFishingComponent> WeakThis(this);
	SnareWater::CallServer(this, TEXT("RollFish"), Args,
		[WeakThis, ThisAttempt](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UTapFishingComponent* const Self = WeakThis.Get();
			if (Self == nullptr || Self->Attempt != ThisAttempt || Self->State != ETapFishingState::Rolling)
			{
				return;
			}
			if (!bOk)
			{
				Self->EndFishing(Error);
				return;
			}
			FString NewToken;
			FString NewFish;
			Response->TryGetStringField(TEXT("token"), NewToken);
			Response->TryGetStringField(TEXT("fishItemId"), NewFish);
			if (NewToken.IsEmpty())
			{
				Self->EndFishing(TEXT("서버가 표를 안 줬다"));
				return;
			}
			const FFishingRow* const Fish = SnareWater::FindFishByItemId(FName(*NewFish));
			EFishingSpotKind Kind;
			if (Fish == nullptr || !IsValid(Self->CurrentSpot) || !Self->CurrentSpot->GetKind(Kind)
				|| !SnareWater::FishFitsSpot(*Self->CachedRow, Kind, *Fish))
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("[Fishing] %s 는 이 자리 서식지가 아니다 — CatchFish 를 안 부른다"), *NewFish);
				Self->EndFishing(TEXT("서식지가 다른 어종"));
				return;
			}
			Self->Token = NewToken;
			Self->FishItemId = NewFish;
			Self->Progress = 0.f;
			Self->SecondsAtZero = 0.f;
			Self->PressCount = 0;
			Self->PressesNeeded = SnareWater::PressesNeeded(*Self->CachedRow, Fish->CycleMinutes);
			Self->ProgressPerPress = SnareWater::ProgressPerPress(*Self->CachedRow, Fish->CycleMinutes);
			Self->SetState(ETapFishingState::Tapping);
			Self->PlayRodCue(&FItemRow::BiteSoundCue, TEXT("입질"), 0.7f);
			Self->StartReelLoop();
			UE_LOG(LogSnarehaven, Log, TEXT("[Fishing] 입질! %s · %d분 · 필요 %d번 · 한 번 +%.2f"),
				*NewFish, Fish->CycleMinutes, Self->PressesNeeded, Self->ProgressPerPress);
		});
}

void UTapFishingComponent::Tap()
{
	const FWaterActivityRow* const Row = GetRow();
	if (State != ETapFishingState::Tapping || Row == nullptr)
	{
		return;
	}
	++PressCount;
	// 보이는 수는 255 를 넘으면 0 으로 돈다 — 몸 클립은 "바뀌었나" 만 본다.
	ShownPressCount = static_cast<uint8>(PressCount);
	if (!GetOwner()->HasAuthority())
	{
		Server_SetShownPressCount(ShownPressCount);
	}
	Progress += ProgressPerPress;
	SecondsAtZero = 0.f;
	// 부동소수 합이 99.9999 로 끝나도 목표로 친다.
	if (Progress + 0.01f >= Row->TapGoal)
	{
		StartCatching();
	}
}

void UTapFishingComponent::StartCatching()
{
	SetState(ETapFishingState::Catching);
	if (Token.IsEmpty())
	{
		EndFishing(TEXT("표가 없다"));
		return;
	}
	const int32 ThisAttempt = Attempt;
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("token"), Token);
	TWeakObjectPtr<UTapFishingComponent> WeakThis(this);
	SnareWater::CallServer(this, TEXT("CatchFish"), Args,
		[WeakThis, ThisAttempt](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UTapFishingComponent* const Self = WeakThis.Get();
			if (Self == nullptr || Self->Attempt != ThisAttempt || Self->State != ETapFishingState::Catching)
			{
				return;
			}
			if (!bOk)
			{
				Self->EndFishing(Error);
				return;
			}
			FString Caught;
			Response->TryGetStringField(TEXT("caught"), Caught);
			Self->LastCaughtItem = FName(*Caught);
			SnareWater::RefreshPlayerState(Self);
			USnareSoundSubsystem::Play2D(Self, TEXT("Pickup"), 0.7f);   // 백엔드가 지급을 확정한 뒤에만
			// 황금 찌 낚싯대는 포인트를 줄이지 않는다 (8.13).
			if (!Self->HoldsLuckyRod())
			{
				Self->Server_ReportSpotCatch(Self->CurrentSpot);
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Fishing] 잡음: %s (%d번 눌러)"), *Caught, Self->PressCount);
			Self->AnnounceCatch(Self->LastCaughtItem);
			Self->EndFishing(TEXT(""));
		});
}

void UTapFishingComponent::AnnounceCatch(FName Caught)
{
	if (Caught.IsNone())
	{
		return;
	}
	// 이름을 모르면 ID 그대로 — 그럴싸한 이름을 지어내지 않는다 (Verification.md 7). 꼴은 채집 획득 글과 같다.
	const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, Caught);
	const FText Name = Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName : FText::FromName(Caught);
	LastCatchPopup = ULootClaimComponent::FormatGrantPopup({ { Name, 1 } });
	UE_LOG(LogSnarehaven, Log, TEXT("[Fishing] 잡은 것 알림 '%s'"), *LastCatchPopup.ToString());
	OnCatchPopup.Broadcast(LastCatchPopup);
}

void UTapFishingComponent::Server_ReportSpotCatch_Implementation(AFishingSpotActor* Spot)
{
	if (IsValid(Spot))
	{
		Spot->ConsumeCatch();
	}
}

void UTapFishingComponent::Quit()
{
	if (State != ETapFishingState::Idle && State != ETapFishingState::Catching)
	{
		EndFishing(TEXT("그만뒀다"));
	}
}

void UTapFishingComponent::EndFishing(const FString& Why)
{
	if (!Why.IsEmpty())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Fishing] 끝: %s"), *Why);
	}
	LastMessage = Why;
	StopReelLoop();
	SetState(ETapFishingState::Idle);
	Token.Reset();
	BaitArg.Reset();
	Progress = 0.f;
	SecondsAtZero = 0.f;
	BiteRemainingSeconds = 0.f;
}
