#include "Equipment/EquipmentComponent.h"
#include "Snarehaven.h"
#include "Components/AudioComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "World/SnareSound.h"
#include "UObject/ConstructorHelpers.h"

UEquipmentComponent::UEquipmentComponent()
{
	// 붙인 뒤에는 몸이 알아서 끌고 다닌다 — 매 프레임 볼 것은 모터 도구 떨림뿐이라 그때만 켠다 (M18-06).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// M9-06: 남의 화면에도 보이려면 컴포넌트가 복제 대상이어야 한다 — `Server_` 요청이 올라가는 길이기도 하다.
	SetIsReplicatedByDefault(true);

	// 어떤 장비가 어떤 메시를 어느 소켓에 다는지는 코드가 아니라 표가 안다 (DataDriven.md 1).
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(
		TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

namespace
{
	/**
	 * 몸이 바라보는 쪽(월드). 몸 메시는 +Y 가 앞이다(플레이어 `SetBodyPlacement` −90°, 상인 `MeshFacesPlusY`).
	 * 액터 앞이 아니라 몸에서 읽는다 — 상인은 액터는 그대로 두고 몸만 돌려 손님을 본다(`AShopVendorActor::SnapFacing`).
	 */
	FVector BodyFacing(const USkeletalMeshComponent& Body)
	{
		return Body.GetRightVector();
	}

	/**
	 * M15-05 — 작게 건 겉모습이 몸에서 뜨지 않게 **소켓 공간에서 몸 쪽으로 도로 미는 양**.
	 *
	 * 소켓 자리는 배율 1 로 잡혀 있다(`setup_equipment_socket.py` 가 배낭 몸 쪽 면을 등 표면에 맞췄다).
	 * 배율 s 를 걸면 메시가 소켓 원점 쪽으로 s 배 쪼그라들어 그 면이 (1−s) × (소켓 원점에서 면까지)만큼
	 * 몸에서 멀어진다 — 그만큼 되민다. 미는 방향은 **몸의 앞**(배낭은 그 반대편 면이 등에 닿는다)을
	 * 소켓 공간으로 옮긴 축이다: 몸이 돌거나 가슴 뼈가 기울어도 소켓과 같이 돌아 어긋나지 않는다.
	 */
	FVector BodyHuggingOffset(const USkeletalMeshComponent* Body, FName SocketName, const UStaticMesh* Mesh, float VisualScale)
	{
		if (Body == nullptr || Mesh == nullptr || FMath::IsNearlyEqual(VisualScale, 1.f))
		{
			return FVector::ZeroVector;
		}
		const FVector Forward = Body->GetSocketTransform(SocketName, RTS_World)
			.InverseTransformVectorNoScale(BodyFacing(*Body)).GetSafeNormal();
		// 몸에 닿는 면 = 메시 상자 꼭짓점 중 앞쪽으로 가장 먼 점. 월드 AABB 는 돌면 부푸니 로컬 상자로 잰다.
		const FBox Local = Mesh->GetBoundingBox();
		double Front = -UE_BIG_NUMBER;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y,
				(Corner & 4) ? Local.Max.Z : Local.Min.Z);
			Front = FMath::Max(Front, FVector::DotProduct(Point, Forward));
		}
		return Forward * (Front * (1.f - VisualScale));
	}

	/**
	 * INBOX 결함 10-20 ② · DESIGN 3.5 「모자 아랫단」 — 모자를 정수리 소켓에서 **위로 올릴 양**(몸 메시 cm).
	 *
	 * 모자 받침점이 정수리라 메시마다 아랫단 깊이가 달라, 깊은 모자(벙거지·밀짚·해파리)는 눈을 덮었다. 크기는 폭(`HatWidthCm`)이
	 * 정하고, 높이는 규칙 하나 — 얼굴 앞(몸 앞쪽 반, 두 눈 너비 안) 모자 꼭짓점 중 가장 낮은 점이 눈썹선보다 낮으면 그만큼 올린다.
	 * 경계상자 바닥은 뒤로 처진 챙·뒷자락이라 얼굴 앞 아랫단이 아니다 — 실제 꼭짓점으로 잰다(게임 빌드는 CPU 접근 켠 메시만, 아니면 0).
	 * 소켓은 축마다 배율이 달라 소켓 공간이 cm 가 아니다 — 몸 컴포넌트 공간(몸 메시 cm)에서 잰다.
	 */
	float HatBrimLift(const USkeletalMeshComponent* Body, FName SocketName, const UStaticMeshComponent* Visual,
		float BrowBelowCrownCm, float EyeHalfSpanCm)
	{
		const UStaticMesh* const Mesh = Visual ? Visual->GetStaticMesh() : nullptr;
		const FStaticMeshRenderData* const RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
		if (Body == nullptr || RenderData == nullptr || RenderData->LODResources.IsEmpty())
		{
			return 0.f;
		}
		const FPositionVertexBuffer& Positions = RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer;
#if !WITH_EDITOR
		if (!Positions.GetAllowCPUAccess())
		{
			return 0.f;
		}
#endif
		const FTransform& BodyToWorld = Body->GetComponentTransform();
		const FTransform HatToBody = Visual->GetComponentTransform().GetRelativeTransform(BodyToWorld);
		const FVector Crown = Body->GetSocketTransform(SocketName, RTS_Component).GetLocation();
		FVector Forward = BodyToWorld.InverseTransformVectorNoScale(BodyFacing(*Body));
		Forward.Z = 0.0;
		Forward = Forward.GetSafeNormal();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
		double LowestZ = UE_BIG_NUMBER;
		for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
		{
			const FVector Point = HatToBody.TransformPosition(FVector(Positions.VertexPosition(Index))) - Crown;
			if (FVector::DotProduct(Point, Forward) > 0.0 && FMath::Abs(FVector::DotProduct(Point, Side)) <= EyeHalfSpanCm)
			{
				LowestZ = FMath::Min(LowestZ, Point.Z);
			}
		}
		return LowestZ < UE_BIG_NUMBER * 0.5 ? static_cast<float>(FMath::Max(0.0, -BrowBelowCrownCm - LowestZ)) : 0.f;
	}

	/**
	 * INBOX 결함 10-20 00:10 · DESIGN 3.5 「모자 밑 머리카락」 — 모자 **뒤쪽 아랫단** 높이를 머리카락 메시 공간(정수리 원점)으로.
	 *
	 * 머리 뒤 반(몸 뒤쪽)을 옆→뒤→옆 부채꼴 `Sectors` 칸으로 나눠 칸마다 가장 낮은 모자 점을 찾고, 그중 **가장 높은 것**을 돌려준다.
	 * 칸마다 가장 낮은 점 = 그 방향 아랫단, 그중 가장 높은 곳까지 머리카락을 올려야 어느 방향에서도 아랫단과 머리카락 사이에
	 * 살색 틈이 없다(뒤로 늘어진 꼬리·귀덮개가 한 칸만 낮아도 다른 칸이 끌려 내려가지 않는다). 꼭짓점은 `HatBrimLift` 처럼
	 * 실제 메시에서 읽는다(게임 빌드는 CPU 접근 켠 메시만 — 못 읽으면 거짓 → 머리카락을 통째로 숨긴다).
	 */
	bool HatBackRimInHairSpace(const UStaticMeshComponent* Hat, const UStaticMeshComponent* Hair, double& OutZ)
	{
		constexpr int32 Sectors = 6;
		const USkeletalMeshComponent* const Body = Hair ? Cast<USkeletalMeshComponent>(Hair->GetAttachParent()) : nullptr;
		const UStaticMesh* const Mesh = Hat ? Hat->GetStaticMesh() : nullptr;
		const FStaticMeshRenderData* const RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
		if (Body == nullptr || RenderData == nullptr || RenderData->LODResources.IsEmpty())
		{
			return false;
		}
		const FPositionVertexBuffer& Positions = RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer;
#if !WITH_EDITOR
		if (!Positions.GetAllowCPUAccess())
		{
			return false;
		}
#endif
		const FTransform HatToHair = Hat->GetComponentTransform().GetRelativeTransform(Hair->GetComponentTransform());
		FVector Forward = Hair->GetComponentTransform().InverseTransformVectorNoScale(BodyFacing(*Body));
		Forward.Z = 0.0;
		Forward = Forward.GetSafeNormal();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
		double SectorLowest[Sectors];
		for (double& Z : SectorLowest)
		{
			Z = UE_BIG_NUMBER;
		}
		for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
		{
			const FVector Point = HatToHair.TransformPosition(FVector(Positions.VertexPosition(Index)));
			const double Ahead = FVector::DotProduct(Point, Forward);
			if (Ahead >= 0.0)
			{
				continue;
			}
			// 오른옆(0) → 뒤(0.5) → 왼옆(1)
			const double Around = FMath::Atan2(-Ahead, FVector::DotProduct(Point, Side)) / UE_DOUBLE_PI;
			const int32 Sector = FMath::Clamp(FMath::FloorToInt32(Around * Sectors), 0, Sectors - 1);
			SectorLowest[Sector] = FMath::Min(SectorLowest[Sector], Point.Z);
		}
		double Highest = -UE_BIG_NUMBER;
		for (const double Z : SectorLowest)
		{
			if (Z < UE_BIG_NUMBER * 0.5)
			{
				Highest = FMath::Max(Highest, Z);
			}
		}
		OutZ = Highest;
		return Highest > -UE_BIG_NUMBER * 0.5;
	}
}

USkeletalMeshComponent* UEquipmentComponent::FindBodyMesh() const
{
	const AActor* Owner = GetOwner();
	return IsValid(Owner) ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

void UEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 회선에 싣는 것은 이 목록 하나뿐 — 메시는 게스트도 자기 `DT_ItemTable` 에서 같은 것을 읽는다.
	DOREPLIFETIME(UEquipmentComponent, EquippedSlots);
	DOREPLIFETIME(UEquipmentComponent, CosmeticSlots);
}

bool UEquipmentComponent::HasEquipAuthority() const
{
	const AActor* Owner = GetOwner();
	// 액터에 안 붙은 컴포넌트는 네트워크 역할 자체가 없다 (2층 검사가 만드는 것) — 권위로 본다.
	return Owner == nullptr || Owner->HasAuthority();
}

void UEquipmentComponent::Server_EquipItem_Implementation(FName ItemRowName, EEquipSlot SlotOverride,
	FName SocketOverride, bool bQuiet)
{
	// 호스트에서 같은 함수를 다시 탄다 — 장비 행인지·소켓이 있는지 판정은 여기서 한 번만 일어난다.
	TGuardValue<bool> Quiet(bQuietChanges, bQuietChanges || bQuiet);
	EquipItem(ItemRowName, SlotOverride, SocketOverride);
}

void UEquipmentComponent::Server_UnequipSlot_Implementation(EEquipSlot Slot, bool bQuiet)
{
	TGuardValue<bool> Quiet(bQuietChanges, bQuietChanges || bQuiet);
	UnequipSlot(Slot);
}

void UEquipmentComponent::Server_SetCosmetic_Implementation(EEquipSlot Slot, FName CosmeticRowName, bool bQuiet)
{
	TGuardValue<bool> Quiet(bQuietChanges, bQuietChanges || bQuiet);
	SetCosmetic(Slot, CosmeticRowName);
}

UEquipmentComponent::FQuietChanges::FQuietChanges(UEquipmentComponent* InEquipment)
	: Equipment(InEquipment)
{
	if (IsValid(InEquipment))
	{
		bWasQuiet = InEquipment->bQuietChanges;
		InEquipment->bQuietChanges = true;
	}
}

UEquipmentComponent::FQuietChanges::~FQuietChanges()
{
	if (UEquipmentComponent* const Self = Equipment.Get())
	{
		Self->bQuietChanges = bWasQuiet;
	}
}

void UEquipmentComponent::RequestChangeCue(EEquipSlot Slot, const FItemRow* HandRow)
{
	if (bQuietChanges)
	{
		return;
	}
	const bool bHand = Slot == EEquipSlot::Weapon || Slot == EEquipSlot::Tool;
	const SnareSound::FChangeCue Cue = bHand ? SnareSound::EquipCue(HandRow) : SnareSound::OutfitCue(Slot);
	if (Cue.Cue.IsNone())
	{
		return;
	}
	// 같은 사람이 표의 간격 안에 또 바꾸면 첫 한 번만 — 단축 칸을 훑어도 철컥이 겹쳐 쌓이지 않는다.
	const UWorld* const World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;
	double& Last = bHand ? LastHandCueSeconds : LastOutfitCueSeconds;
	if (Now - Last < Cue.MinGapSeconds)
	{
		return;
	}
	Last = Now;
	++ChangeCueCount;
	LastChangeCue = Cue.Cue;
	UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 장비 %s — 부위 %s · 크기 %.2f · %s"), *Cue.Cue.ToString(),
		*UEnum::GetValueAsString(Slot), Cue.Volume, Cue.bWorld ? TEXT("모든 기기") : TEXT("내 기기만"));
	Multicast_PlayChangeCue(Cue.Cue, Cue.Volume, Cue.bWorld);
}

void UEquipmentComponent::Multicast_PlayChangeCue_Implementation(FName Cue, float Volume, bool bWorld)
{
	const AActor* const Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}
	if (bWorld)
	{
		USnareSoundSubsystem::PlayAt(this, Cue, Owner->GetActorLocation(), Volume);
		return;
	}
	// 옷 갈아입기는 옷장·배낭 판 안 일이라 그 사람 귀에만 (보관함과 같음).
	if (const APawn* const Pawn = Cast<APawn>(Owner); Pawn != nullptr && Pawn->IsLocallyControlled())
	{
		USnareSoundSubsystem::Play2D(this, Cue, Volume);
	}
}

void UEquipmentComponent::OnRep_EquippedSlots()
{
	RefreshEquippedVisuals();
}

void UEquipmentComponent::ApplyCosmeticSlots(const TArray<FEquippedSlot>& InSlots)
{
	CosmeticSlots = InSlots;
	RefreshEquippedVisuals();
}

void UEquipmentComponent::ApplyEquippedSlots(const TArray<FEquippedSlot>& InSlots)
{
	EquippedSlots = InSlots;
	RefreshEquippedVisuals();
}

void UEquipmentComponent::RefreshEquippedVisuals()
{
	// 먼저 전부 내린다 — 목록에서 사라진 부위를 따로 세지 않으려고 그런다. 컴포넌트는 지우지 않고
	// 메시만 비운다(`UnequipSlot` 과 같은 이유 — 다시 걸 때 그대로 쓴다).
	for (const TPair<EEquipSlot, TObjectPtr<UStaticMeshComponent>>& Pair : SlotMeshes)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->SetStaticMesh(nullptr);
		}
	}

	for (const TPair<EEquipSlot, TObjectPtr<USkeletalMeshComponent>>& Pair : OutfitMeshes)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->SetSkeletalMeshAsset(nullptr);
		}
	}
	SetHeadLight(nullptr);

	// 목록에 든 부위·소켓은 **호스트가 이미 푼 값**이라 덮어쓰기 자리에 그대로 넣으면 같은 결과가 나온다.
	for (const FEquippedSlot& Entry : EquippedSlots)
	{
		// 꾸미기가 덮은 부위는 장비 메시를 걸지 않는다 — 효과는 목록에 남아 `GetEquippedRow` 가 그대로 읽는다.
		if (!GetCosmeticItem(Entry.Slot).IsNone())
		{
			continue;
		}
		EEquipSlot AppliedSlot = EEquipSlot::None;
		FName AppliedSocket = NAME_None;
		ApplyEquipVisual(Entry.ItemRowName, Entry.Slot, Entry.SocketName, AppliedSlot, AppliedSocket);
	}
	for (const FEquippedSlot& Entry : CosmeticSlots)
	{
		ApplyCosmeticVisual(Entry.Slot);
	}
	ApplyDefaultLook();
	UpdateHandVisibility();
	UpdateHair();

	// 복제로 내려온 목록도 화면에 알린다 — 게스트의 HUD 는 자기가 건 적이 없어서 이 길로만 안다.
	UpdateHoldLoop();
	OnEquipmentChanged.Broadcast();
}

void UEquipmentComponent::UpdateHandVisibility()
{
	const TObjectPtr<UStaticMeshComponent>* const Weapon = SlotMeshes.Find(EEquipSlot::Weapon);
	if (Weapon == nullptr || !IsValid(Weapon->Get()))
	{
		return;
	}
	const TObjectPtr<UStaticMeshComponent>* const Tool = SlotMeshes.Find(EEquipSlot::Tool);
	const bool bToolInWeaponHand = Tool != nullptr && IsValid(Tool->Get()) && (*Tool)->GetStaticMesh() != nullptr
		&& (*Tool)->GetAttachSocketName() == (*Weapon)->GetAttachSocketName();
	(*Weapon)->SetVisibility(!bToolInWeaponHand);
}

bool UEquipmentComponent::IsHairHiddenByHat() const
{
	const TObjectPtr<UStaticMeshComponent>* const Head = SlotMeshes.Find(EEquipSlot::Head);
	return Head != nullptr && IsValid(Head->Get()) && (*Head)->GetStaticMesh() != nullptr && !bHeadKeepsHair;
}

void UEquipmentComponent::UpdateHair()
{
	const bool bHeadCovered = IsHairHiddenByHat();
	// 붙인 적이 없으면 처음 붙인다 — 몸이 서기 전(초기 복제가 BeginPlay 보다 먼저)이면
	// 몸을 올린 직후 `RefreshEquippedVisuals` 가 다시 부른다. 모자를 쓴 채로 시작해도 아랫단 밑 머리카락이 필요해 늘 붙인다.
	USkeletalMeshComponent* const Body = FindBodyMesh();
	if (!IsValid(HairVisual) && !DefaultHairMesh.IsNull() && IsValid(Body) && Body->GetSkeletalMeshAsset() != nullptr)
	{
		static const FName HairSocket(TEXT("Socket_Head"));
		UStaticMesh* const Mesh = DefaultHairMesh.LoadSynchronous();
		if (!Body->DoesSocketExist(HairSocket))
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("장비: 몸에 소켓 %s 가 없다 — 머리카락을 안 붙인다"), *HairSocket.ToString());
		}
		else if (Mesh == nullptr)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("장비: 머리카락 메시 %s 를 못 불러왔다"), *DefaultHairMesh.ToString());
		}
		else
		{
			HairVisual = NewObject<UStaticMeshComponent>(GetOwner());
			HairVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			HairVisual->SetMobility(EComponentMobility::Movable);
			HairVisual->SetStaticMesh(Mesh);
			HairVisual->RegisterComponent();
			HairVisual->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, HairSocket);
			// 메시는 몸 원본 단위로 정수리 공간에 만들었다(`make_player_hair_mesh.py`) — 몸과 같은 월드 배율이면 머리 공에 딱 덮인다.
			// 소켓 배율(모자 자리표시 0.3·0.12)은 받지 않는다.
			HairVisual->SetWorldScale3D(Body->GetComponentScale());
			const FVector WorldSize = HairVisual->CalcBounds(HairVisual->GetComponentTransform()).BoxExtent * 2.f;
			UE_LOG(LogSnarehaven, Log, TEXT("장비: 기본 머리카락 %s 를 소켓 %s 에 붙였다 — 크기 %.1f×%.1f×%.1fcm"),
				*Mesh->GetName(), *HairSocket.ToString(), WorldSize.X, WorldSize.Y, WorldSize.Z);
		}
	}
	if (!IsValid(HairVisual))
	{
		return;
	}
	HairTopZ = TNumericLimits<float>::Max();
	if (HairColor.IsSet() && !IsValid(HairColorMID))
	{
		HairColorMID = UMaterialInstanceDynamic::Create(HairVisual->GetStaticMesh()->GetMaterial(0), this);
		HairColorMID->SetVectorParameterValue(TEXT("DiffuseColor"), HairColor.GetValue());
	}
	if (!bHeadCovered)
	{
		HairVisual->SetMaterial(0, HairColorMID);   // 색을 안 받았으면 nullptr = 메시 제 머티리얼(`MI_Player_Hair`) — 통째로
		HairVisual->SetVisibility(true);
		return;
	}
	// DESIGN 3.5 「모자 밑 머리카락」 — 껍질이 모자 안쪽 틈보다 두꺼워 통째로 두면 모자를 뚫는다(#1336). 모자 뒤 아랫단보다
	// 조금 위(`HatHairTuckCm`)부터 아래만 그려 작은 모자 밑 뒤통수가 민머리로 안 보이게 한다.
	if (!IsValid(HairUnderHatMID) && !HairUnderHatMaterial.IsNull())
	{
		if (UMaterialInterface* const Base = HairUnderHatMaterial.LoadSynchronous())
		{
			HairUnderHatMID = UMaterialInstanceDynamic::Create(Base, this);
			FLinearColor Color;
			const UMaterialInterface* const Own = HairVisual->GetStaticMesh()->GetMaterial(0);
			if (HairColor.IsSet())
			{
				HairUnderHatMID->SetVectorParameterValue(TEXT("DiffuseColor"), HairColor.GetValue());
			}
			else if (Own != nullptr && Own->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("DiffuseColor")), Color))
			{
				HairUnderHatMID->SetVectorParameterValue(TEXT("DiffuseColor"), Color);
			}
		}
	}
	const TObjectPtr<UStaticMeshComponent>* const Head = SlotMeshes.Find(EEquipSlot::Head);
	double RimZ = 0.0;
	if (!IsValid(HairUnderHatMID) || !HatBackRimInHairSpace(Head->Get(), HairVisual, RimZ))
	{
		HairVisual->SetVisibility(false);
		return;
	}
	HairTopZ = static_cast<float>(RimZ) + HatHairTuckCm;
	HairUnderHatMID->SetScalarParameterValue(TEXT("HairTopZ"), HairTopZ);
	HairVisual->SetMaterial(0, HairUnderHatMID);
	HairVisual->SetVisibility(true);
}

void UEquipmentComponent::SetHairColor(const FLinearColor& Color)
{
	HairColor = Color;
	for (UMaterialInstanceDynamic* const Mid : {HairColorMID.Get(), HairUnderHatMID.Get()})
	{
		if (IsValid(Mid))
		{
			Mid->SetVectorParameterValue(TEXT("DiffuseColor"), Color);
		}
	}
}

bool UEquipmentComponent::IsHairShown() const
{
	return IsValid(HairVisual) && HairVisual->GetStaticMesh() != nullptr && HairVisual->IsVisible()
		&& HairTopZ == TNumericLimits<float>::Max();
}

bool UEquipmentComponent::IsHairUnderHat() const
{
	return IsValid(HairVisual) && HairVisual->GetStaticMesh() != nullptr && HairVisual->IsVisible()
		&& HairTopZ < TNumericLimits<float>::Max();
}

FName UEquipmentComponent::FindDefaultLookRow(EEquipSlot Slot) const
{
	for (const FName ItemID : DefaultLookItemIDs)
	{
		const FName RowName = FindEquipmentRowByItemID(ItemID);
		const FItemRow* const Row = RowName.IsNone() ? nullptr : ItemTable->FindRow<FItemRow>(RowName, TEXT("DefaultLook"));
		if (Row != nullptr && Row->EquipSlot == Slot)
		{
			return RowName;
		}
	}
	return NAME_None;
}

void UEquipmentComponent::ApplyDefaultLook()
{
	// 몸이 서기 전(초기 복제가 BeginPlay 보다 먼저 올 때)에는 붙일 자리가 없다 — 경고 없이 넘기고,
	// 몸을 올린 직후 `RefreshEquippedVisuals` 가 다시 부른다.
	const USkeletalMeshComponent* const Body = FindBodyMesh();
	if (DefaultLookItemIDs.IsEmpty() || !IsValid(Body) || Body->GetSkeletalMeshAsset() == nullptr)
	{
		return;
	}

	for (const FName ItemID : DefaultLookItemIDs)
	{
		const FName RowName = FindEquipmentRowByItemID(ItemID);
		const FItemRow* const Row = RowName.IsNone() ? nullptr : ItemTable->FindRow<FItemRow>(RowName, TEXT("DefaultLook"));
		if (Row == nullptr || GetEquippedItem(Row->EquipSlot) != NAME_None || !GetCosmeticItem(Row->EquipSlot).IsNone())
		{
			continue;
		}
		EEquipSlot AppliedSlot = EEquipSlot::None;
		FName AppliedSocket = NAME_None;
		// 기본 겉모습만 작게 걸린다 (M15-10 · DESIGN 3.5) — 같은 메시를 진짜로 가졌을 때 차이가 보이게.
		ApplyEquipVisual(RowName, EEquipSlot::None, NAME_None, AppliedSlot, AppliedSocket, DefaultLookScale);
	}
}

void UEquipmentComponent::SetEquippedSlot(EEquipSlot Slot, FName ItemRowName, FName SocketName)
{
	FEquippedSlot* Found = EquippedSlots.FindByPredicate(
		[Slot](const FEquippedSlot& Entry) { return Entry.Slot == Slot; });
	if (Found == nullptr)
	{
		Found = &EquippedSlots.AddDefaulted_GetRef();
		Found->Slot = Slot;
	}

	Found->ItemRowName = ItemRowName;
	Found->SocketName = SocketName;
	UpdateHoldLoop();
	OnEquipmentChanged.Broadcast();
}

FName UEquipmentComponent::FindEquipmentRowByItemID(FName ItemID) const
{
	if (!IsValid(ItemTable) || ItemID.IsNone())
	{
		return NAME_None;
	}

	// 어떤 행이 장비인지는 표가 정한다 — `UHuntHudWidget::GetEquipButtonItem` 과 같은 판정이다.
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->ItemID == ItemID && Row->ItemType == EItemType::Equipment)
		{
			return Pair.Key;
		}
	}

	return NAME_None;
}

bool UEquipmentComponent::EquipItemByItemID(FName ItemID)
{
	const FName RowName = FindEquipmentRowByItemID(ItemID);
	if (RowName.IsNone())
	{
		// 장비가 아닌 것을 받는 것은 오류가 아니다 — 시작 지급 목록에 재료가 섞여도 그건 배낭으로 간다.
		return false;
	}

	return EquipItem(RowName);
}

bool UEquipmentComponent::EquipItem(FName ItemRowName, EEquipSlot SlotOverride, FName SocketOverride)
{
	// 멀티(M9-06): 게스트는 자기 화면만 고치지 않는다 — 호스트가 걸고 그 결과가 모두에게 내려온다.
	// 내 화면이 한 박자 늦는 대신 **네 화면과 내 화면이 갈라지지 않는다** (DESIGN 3.4 · 4.1 권위 표).
	if (!HasEquipAuthority())
	{
		Server_EquipItem(ItemRowName, SlotOverride, SocketOverride, bQuietChanges);
		return false;
	}

	// 꾸미기 행은 장비 줄에 못 든다 — 효과 칸에 섞이면 꾸미기를 입는 순간 효과 장비가 빠진다 (`SetCosmetic` 으로).
	const FItemRow* const Row = IsValid(ItemTable)
		? ItemTable->FindRow<FItemRow>(ItemRowName, FString(), /*bWarnIfRowMissing=*/false) : nullptr;
	if (Row != nullptr && Row->ItemType == EItemType::Cosmetic)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: %s 는 꾸미기 행이다 — 장비로 안 든다"), *ItemRowName.ToString());
		return false;
	}

	EEquipSlot AppliedSlot = EEquipSlot::None;
	FName AppliedSocket = NAME_None;
	if (!ApplyEquipVisual(ItemRowName, SlotOverride, SocketOverride, AppliedSlot, AppliedSocket))
	{
		return false;
	}

	// 같은 행을 다시 들면 바뀐 것이 없다 — 소리도 없다 (M47-03).
	if (GetEquippedItem(AppliedSlot) != ItemRowName)
	{
		RequestChangeCue(AppliedSlot, Row);
	}
	SetEquippedSlot(AppliedSlot, ItemRowName, AppliedSocket);
	// 꾸미기가 덮은 부위면 방금 건 장비 메시 위에 꾸미기를 도로 건다 — 보이는 것은 그대로, 효과만 바뀐다.
	// 장비 줄을 고친 **뒤에** 건다: 머리 불빛을 새 장비 행 기준으로 다시 정해야 한다(먼저 걸면 옛 모자 불빛이 남았다).
	ApplyCosmeticVisual(AppliedSlot);
	return true;
}

bool UEquipmentComponent::SetCosmetic(EEquipSlot Slot, FName CosmeticRowName)
{
	if (!HasEquipAuthority())
	{
		Server_SetCosmetic(Slot, CosmeticRowName, bQuietChanges);
		return false;
	}
	if (Slot == EEquipSlot::None)
	{
		return false;
	}

	if (CosmeticRowName.IsNone())
	{
		if (CosmeticSlots.RemoveAll([Slot](const FEquippedSlot& Entry) { return Entry.Slot == Slot; }) == 0)
		{
			return false;
		}
		UE_LOG(LogSnarehaven, Log, TEXT("장비: 부위 %d 꾸미기를 벗겼다"), static_cast<int32>(Slot));
		RequestChangeCue(Slot, nullptr);
		RefreshEquippedVisuals();
		return true;
	}

	const FItemRow* const Row = IsValid(ItemTable)
		? ItemTable->FindRow<FItemRow>(CosmeticRowName, TEXT("UEquipmentComponent::SetCosmetic")) : nullptr;
	if (Row == nullptr || Row->ItemType != EItemType::Cosmetic || Row->EquipSlot != Slot)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: %s 는 부위 %d 꾸미기 행이 아니다"),
			*CosmeticRowName.ToString(), static_cast<int32>(Slot));
		return false;
	}

	FEquippedSlot* Found = CosmeticSlots.FindByPredicate([Slot](const FEquippedSlot& Entry) { return Entry.Slot == Slot; });
	if (Found == nullptr)
	{
		Found = &CosmeticSlots.AddDefaulted_GetRef();
		Found->Slot = Slot;
	}
	if (Found->ItemRowName != CosmeticRowName)
	{
		RequestChangeCue(Slot, nullptr);
	}
	Found->ItemRowName = CosmeticRowName;
	Found->SocketName = IsOutfitSlot(Slot) ? NAME_None : Row->EquipSocketName;
	UE_LOG(LogSnarehaven, Log, TEXT("장비: 부위 %d 에 꾸미기 %s 를 덮었다"), static_cast<int32>(Slot), *CosmeticRowName.ToString());
	RefreshEquippedVisuals();
	return true;
}

bool UEquipmentComponent::SetCosmeticByItemID(EEquipSlot Slot, FName ItemID)
{
	if (ItemID.IsNone())
	{
		return SetCosmetic(Slot, NAME_None);
	}
	if (!IsValid(ItemTable))
	{
		return false;
	}
	// 행 이름은 표가 정한다 — `FindEquipmentRowByItemID` 와 같은 찾기, 종류만 꾸미기.
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->ItemID == ItemID && Row->ItemType == EItemType::Cosmetic)
		{
			return SetCosmetic(Slot, Pair.Key);
		}
	}
	UE_LOG(LogSnarehaven, Warning, TEXT("장비: %s 의 꾸미기 행이 표에 없다"), *ItemID.ToString());
	return false;
}

FName UEquipmentComponent::GetCosmeticItem(EEquipSlot Slot) const
{
	const FEquippedSlot* const Found = CosmeticSlots.FindByPredicate(
		[Slot](const FEquippedSlot& Entry) { return Entry.Slot == Slot; });
	return (Found != nullptr) ? Found->ItemRowName : NAME_None;
}

bool UEquipmentComponent::ApplyCosmeticVisual(EEquipSlot Slot)
{
	const FName RowName = GetCosmeticItem(Slot);
	if (RowName.IsNone())
	{
		return false;
	}
	EEquipSlot AppliedSlot = EEquipSlot::None;
	FName AppliedSocket = NAME_None;
	if (!ApplyEquipVisual(RowName, Slot, NAME_None, AppliedSlot, AppliedSocket))
	{
		return false;
	}
	// 머리 불빛은 효과다(채굴 모자, DESIGN 8.16) — 꾸미기 행이 끈 것을 장비 행 기준으로 되돌린다.
	if (Slot == EEquipSlot::Head)
	{
		SetHeadLight(GetEquippedRow(EEquipSlot::Head));
	}
	return true;
}

bool UEquipmentComponent::ApplyEquipVisual(FName ItemRowName, EEquipSlot SlotOverride, FName SocketOverride,
	EEquipSlot& OutSlot, FName& OutSocket, float VisualScale)
{
	if (!IsValid(ItemTable))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: 아이템 표가 없다 — %s 를 못 든다"), *ItemRowName.ToString());
		return false;
	}

	const FItemRow* Row = ItemTable->FindRow<FItemRow>(ItemRowName, TEXT("UEquipmentComponent::EquipItem"));
	if (Row == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: 행 %s 를 못 찾았다"), *ItemRowName.ToString());
		return false;
	}

	// 재료·펫을 손에 들리지 않는다 — 표가 장비·꾸미기라고 적은 행만 붙는다 (2층이 같은 것을 데이터 쪽에서 본다).
	if ((Row->ItemType != EItemType::Equipment && Row->ItemType != EItemType::Cosmetic) || Row->EquipSlot == EEquipSlot::None)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: %s 는 장비 행이 아니다"), *ItemRowName.ToString());
		return false;
	}

	// 평소에는 행이 적은 대로 간다 — 덮어쓰기는 3층 검사가 자리표시를 걸어 볼 때만 값이 온다 (M9-01).
	const EEquipSlot Slot = (SlotOverride != EEquipSlot::None) ? SlotOverride : Row->EquipSlot;
	const FName SocketName = SocketOverride.IsNone() ? Row->EquipSocketName : SocketOverride;

	// **부위가 붙이는 종류를 정한다** (M9-02 · 결정 006) — 옷은 소켓에 매다는 물건이 아니라
	// 몸 위에 덮는 한 겹이라, 읽는 칸도 만드는 컴포넌트도 여기서 갈린다.
	if (IsOutfitSlot(Slot))
	{
		if (Row->EquipOutfitMeshRef.IsNull())
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("장비: %s 는 옷 부위인데 덮을 스켈레탈 메시가 없다"),
				*ItemRowName.ToString());
			return false;
		}

		USkeletalMesh* OutfitMesh = Row->EquipOutfitMeshRef.LoadSynchronous();
		if (OutfitMesh == nullptr)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("장비: %s 의 옷 메시를 못 불러왔다"), *ItemRowName.ToString());
			return false;
		}

		if (!EquipOutfitMesh(Slot, OutfitMesh))
		{
			return false;
		}

		// 옷은 소켓에 매다는 물건이 아니라 몸에 겹치는 한 겹이라 소켓 이름이 없다.
		OutSlot = Slot;
		OutSocket = NAME_None;
		return true;
	}

	// 손에 들지 않는 도구(위성 통신기) — 레퍼런스는 좌표 HUD 만 생기고 몸은 빈손이다(docs/reference/character_motion.md).
	// 도구 칸에는 들지만(효과·HUD 줄은 행이 정한다) 메시가 없으니 앞서 든 도구 메시만 비운다.
	if (Slot == EEquipSlot::Tool && Row->EquipMeshRef.IsNull())
	{
		if (TObjectPtr<UStaticMeshComponent>* Found = SlotMeshes.Find(Slot); Found != nullptr && IsValid(Found->Get()))
		{
			(*Found)->SetStaticMesh(nullptr);
		}
		OutSlot = Slot;
		OutSocket = NAME_None;
		UpdateHandVisibility();
		UE_LOG(LogSnarehaven, Log, TEXT("장비: %s 를 빈손으로 들었다(손 메시 없는 도구)"), *ItemRowName.ToString());
		return true;
	}

	if (Row->EquipMeshRef.IsNull() || SocketName.IsNone())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: %s 에 붙일 메시나 소켓 이름이 없다"), *ItemRowName.ToString());
		return false;
	}

	USkeletalMeshComponent* Body = FindBodyMesh();
	if (!IsValid(Body))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: 몸(스켈레탈 메시)이 없다 — %s 를 붙일 자리가 없다"),
			*ItemRowName.ToString());
		return false;
	}

	// **소켓이 진짜 있는지 먼저 본다.** 없는 소켓 이름으로 붙이면 엔진은 조용히 **액터 원점**에 매달고,
	// 화면에서는 무기가 발밑에 떠 있는 것으로 보인다 — 이름 오타가 여기서 걸려야 한다.
	if (!Body->DoesSocketExist(SocketName))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: 몸에 소켓 %s 가 없다 — %s 를 안 붙인다"),
			*SocketName.ToString(), *ItemRowName.ToString());
		return false;
	}

	UStaticMesh* Mesh = Row->EquipMeshRef.LoadSynchronous();
	if (Mesh == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: %s 의 메시를 못 불러왔다"), *ItemRowName.ToString());
		return false;
	}

	// 부위마다 컴포넌트 하나를 만들어 두고 계속 쓴다 — 갈아 들 때마다 만들고 지우면
	// 같은 프레임에 붙였다 뗐다 하는 UI(M5-06c)에서 컴포넌트가 쌓인다.
	TObjectPtr<UStaticMeshComponent>* Existing = SlotMeshes.Find(Slot);
	UStaticMeshComponent* Visual = (Existing != nullptr) ? Existing->Get() : nullptr;
	if (!IsValid(Visual))
	{
		Visual = NewObject<UStaticMeshComponent>(GetOwner());
		// 몸에 딸려 움직이는 겉모습이라 부딪히지 않는다 — 공격 판정은 UMeleeAttackComponent 몫이다.
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->SetMobility(EComponentMobility::Movable);
		Visual->RegisterComponent();
		SlotMeshes.Add(Slot, Visual);
	}

	Visual->SetStaticMesh(Mesh);
	// 소켓이 주는 자리 그대로 붙인다(`SnapToTarget`) — 소켓의 상대 자리·각이 곧 손에 쥔 모양이다.
	// M13-43: 몸 리그 기준 크기인 행(낚싯대·배낭)은 소켓 배율(= 몸 배율)까지 받는다. 컴포넌트를 돌려쓰니
	// `NotIncludingScale` 은 월드 배율을 **그대로 둔다** — 낚싯대(몸 배율) 다음 손도끼를 들면 배율이 남으니 1 로 되돌린다.
	Visual->AttachToComponent(Body, Row->bEquipFollowsBodyScale
		? FAttachmentTransformRules::SnapToTargetIncludingScale
		: FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	// **배율은 붙이는 규칙에 맡기지 않고 늘 여기서 다시 정한다.** 같은 소켓에 이미 붙어 있으면
	// `AttachToComponent` 는 규칙을 다시 적용하지 않고 빠져나가서, 앞서 걸었던 것의 배율이 그대로 남는다
	// (M15-10: 작은 기본 배낭 0.6 이 다음에 멘 진짜 배낭까지 줄여 놓았다).
	// 손에 든 총은 게임 카메라(4500cm)에서 막대 몇 픽셀로만 보였다(감독 10-15 보정) — 총(`MagazineSize > 0`)만 `HeldGunScale` 배.
	// DESIGN 8.6-c 「손에 든 총 보임 배율」. 총구 자리는 메시 끝을 컴포넌트 변환으로 옮겨 구하므로 불꽃도 같이 따라간다.
	const float ShownScale = (Slot == EEquipSlot::Weapon && Row->MagazineSize > 0) ? VisualScale * HeldGunScale : VisualScale;
	if (Row->bEquipFollowsBodyScale)
	{
		Visual->SetRelativeScale3D(FVector(ShownScale));   // 소켓(= 몸) 배율을 그대로 받고 그 위에 겹친다
	}
	else
	{
		Visual->SetWorldScale3D(FVector(ShownScale));
	}
	// **작게 거는 겉모습은 소켓 원점 쪽으로 쪼그라든다** (M15-05) — 소켓 자리는 배율 1 을 전제로
	// 잡혀 있어서(`setup_equipment_socket.py` 가 배낭 몸 쪽 면을 등 표면에 딱 맞춰 놓았다) 0.6 을 걸면
	// 그 면이 몸에서 **11cm 떨어져 뜬다**(2층 `Player.Look` 첫 실행). 몸에 닿아야 할 면을 제자리에 두도록
	// 줄어든 만큼 소켓 공간에서 몸 쪽으로 도로 민다. 머리는 제 크기 규칙(아래 `HatWidthCm`)이 따로라 빼고,
	// 배율 1(진짜로 멘 것)이면 밀림이 0 이다 — 돌려쓰는 컴포넌트라 그때도 **반드시 0 으로 되돌린다**.
	// M18-11 쥐는 자리 — 행의 `GripOffset` 점을 손목(소켓 원점)에 놓고 그 각으로 돌린다. 돌려쓰는 컴포넌트라
	// 값 없는 행은 각을 **반드시 0 으로 되돌린다**(같은 소켓이면 `AttachToComponent` 가 다시 맞추지 않는다).
	// 위치는 월드로 정한다 — 상대 자리는 소켓(몸) 배율이 곱해지는데 도구 월드 배율은 1 이라 어긋난다.
	const FEquipGrip& Grip = Row->GripOffset;
	if (Slot != EEquipSlot::Head && Grip.IsSet())
	{
		Visual->SetRelativeRotation(Grip.Rotation);
		Visual->SetWorldLocation(Body->GetSocketLocation(SocketName)
			- Visual->GetComponentQuat().RotateVector(Grip.Location * Visual->GetComponentScale()));
	}
	else
	{
		Visual->SetRelativeRotation(FRotator::ZeroRotator);
		Visual->SetRelativeLocation(Slot == EEquipSlot::Head
			? FVector::ZeroVector
			: BodyHuggingOffset(Body, SocketName, Mesh, VisualScale));
	}
	if (Slot == EEquipSlot::Head)
	{
		// M13-35 · DESIGN 3.5 「모자 크기」 — **한 배율**: 메시 바닥 긴 변(X·Y 큰 쪽)을 행의 폭에 맞추고 높이는
		// 메시 비율대로 따라간다(축마다 늘리면 높은 모자가 눌린다). 월드 배율이라 몸·소켓 배율은 상쇄된다.
		const FVector MeshSize = Mesh->GetBounds().BoxExtent * 2.f;
		const float WidthCm = Row->HatWidthCm > 0.f ? Row->HatWidthCm : DefaultHatWidthCm;
		Visual->SetWorldScale3D(
			FVector(VisualScale * WidthCm / FMath::Max3<double>(MeshSize.X, MeshSize.Y, KINDA_SMALL_NUMBER)));
		const float BrimLiftCm = HatBrimLift(Body, SocketName, Visual, HatBrowBelowCrownCm, HatEyeHalfSpanCm);
		Visual->SetWorldLocation(Visual->GetComponentLocation() + Body->GetComponentTransform().TransformVector(FVector(0.f, 0.f, BrimLiftCm)));
		SetHeadLight(Row);
		// 왕관처럼 머리를 덮지 않는 장식은 머리카락을 남긴다(DESIGN 3.5 「모자 쓸 때 머리카락」) — 아래 `UpdateHair` 가 읽는다.
		bHeadKeepsHair = Row->bHatKeepsHair;
	}
	if (Slot == EEquipSlot::Tool)
	{
		// 떨림(틱)은 이 자리 위에 얹는다 — 0 에 얹으면 쥐는 자리가 지워져 전기톱이 손목에서 22cm 밀려 몸에 묻혔다(INBOX 2026-10-05 18:20).
		ToolRestLocation = Visual->GetRelativeLocation();
	}
	OutSlot = Slot;
	OutSocket = SocketName;
	UpdateHandVisibility();
	UpdateHair();

	// 크기·자리까지 찍는다 — 3층 캡처에서 "안 보인다" 가 **안 붙었다**인지 **너무 작다**인지는
	// 그림만 봐서는 못 가른다(바퀴#89 에서 실제로 헷갈렸다). 몸 메시가 캡슐에 맞춰 줄어들면
	// 붙은 무기도 같이 줄어든다.
	const FVector WorldSize = Visual->CalcBounds(Visual->GetComponentTransform()).BoxExtent * 2.f;
	// `모양` 은 기울기를 뺀 메시 크기 × 월드 배율 — 머리가 기울면 `크기`(월드 상자)만 커진다.
	const FVector ShapeSize = Mesh->GetBounds().BoxExtent * 2.f * Visual->GetComponentScale();
	UE_LOG(LogSnarehaven, Log, TEXT("장비: %s 를 소켓 %s 에 들었다 — 크기 %.1f×%.1f×%.1fcm · 모양 %.1f×%.1f×%.1fcm · 자리 %s"),
		*ItemRowName.ToString(), *SocketName.ToString(),
		WorldSize.X, WorldSize.Y, WorldSize.Z, ShapeSize.X, ShapeSize.Y, ShapeSize.Z,
		*Visual->GetComponentLocation().ToCompactString());
	return true;
}

void UEquipmentComponent::SetHeadLight(const FItemRow* HeadRow)
{
	if (HeadRow == nullptr || HeadRow->HeadLightIntensityCd <= 0.f)
	{
		if (IsValid(HeadLight))
		{
			HeadLight->SetVisibility(false);
		}
		return;
	}
	AActor* const Owner = GetOwner();
	USceneComponent* const Root = Owner->GetRootComponent();
	if (!IsValid(HeadLight))
	{
		// DESIGN 8.16·13.5 「갱도 투구」 — 몸 뼈가 아니라 루트에 붙인다: 뛰는 동작마다 흔들리면 굴 벽에 비친 빛이 떨린다.
		HeadLight = NewObject<USpotLightComponent>(Owner);
		HeadLight->SetMobility(EComponentMobility::Movable);
		HeadLight->SetIntensityUnits(ELightUnits::Candelas);
		HeadLight->SetCastShadows(false);
		HeadLight->SetLightColor(FLinearColor(1.f, 0.93f, 0.78f));
		HeadLight->SetupAttachment(Root);
		HeadLight->RegisterComponent();
	}
	// 램프 자리 = 지금 보이는 모자 메시 상자의 몸 앞 끝 + `HeadLampAheadCm`, 높이는 상자 가운데(루트 공간).
	// 예전 점광은 몸 중심 앞 40 · 위 70cm 고정이라 큰 머리의 얼굴 표면에 닿아 얼굴이 하얗게 날아갔다(#1357) —
	// 원뿔빛을 모자 앞에서 앞·아래로 비추면 램프 뒤(쓴 사람)는 원뿔 밖이다.
	const USkeletalMeshComponent* const Body = FindBodyMesh();
	FVector Forward = FVector::ForwardVector;
	if (Body != nullptr)
	{
		Forward = Root->GetComponentTransform().InverseTransformVectorNoScale(BodyFacing(*Body)).GetSafeNormal2D();
	}
	FVector Lamp(40.f, 0.f, 70.f);
	const TObjectPtr<UStaticMeshComponent>* Hat = SlotMeshes.Find(EEquipSlot::Head);
	if (Hat != nullptr && IsValid(Hat->Get()) && Hat->Get()->GetStaticMesh() != nullptr)
	{
		const FTransform HatToRoot = Hat->Get()->GetComponentTransform().GetRelativeTransform(Root->GetComponentTransform());
		const FBox Box = Hat->Get()->GetStaticMesh()->GetBoundingBox().TransformBy(HatToRoot);
		const FVector Center = Box.GetCenter();
		double Front = -UE_BIG_NUMBER;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point((Corner & 1) ? Box.Max.X : Box.Min.X, (Corner & 2) ? Box.Max.Y : Box.Min.Y,
				(Corner & 4) ? Box.Max.Z : Box.Min.Z);
			Front = FMath::Max(Front, FVector::DotProduct(Point, Forward));
		}
		Lamp = Center + Forward * (Front + HeadLampAheadCm - FVector::DotProduct(Center, Forward));
	}
	HeadLight->SetRelativeLocation(Lamp);
	HeadLight->SetRelativeRotation(FRotator(-HeadLampPitchDeg, Forward.Rotation().Yaw, 0.f));
	HeadLight->SetInnerConeAngle(HeadLampInnerConeDeg);
	HeadLight->SetOuterConeAngle(HeadLampOuterConeDeg);
	HeadLight->SetIntensity(HeadRow->HeadLightIntensityCd);
	HeadLight->SetAttenuationRadius(HeadRow->HeadLightRadiusCm);
	HeadLight->SetVisibility(true);
}

bool UEquipmentComponent::IsOutfitSlot(EEquipSlot Slot)
{
	// 신발·장갑도 소켓에 매다는 것이 아니라 발·손 뼈를 따라 휘는 한 겹이다 (스킨 c — `skin-parts.md` 5장 3).
	return Slot == EEquipSlot::OutfitTop || Slot == EEquipSlot::OutfitBottom
		|| Slot == EEquipSlot::Shoes || Slot == EEquipSlot::Gloves;
}

bool UEquipmentComponent::EquipOutfitMesh(EEquipSlot Slot, USkeletalMesh* OutfitMesh)
{
	// 손에 드는 부위에 옷을 덮으면 몽둥이 자리에 셔츠가 통째로 뜬다 — 종류 착오를 여기서 끊는다.
	if (!IsOutfitSlot(Slot))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: 옷 부위가 아닌 곳에 옷을 덮으려 했다"));
		return false;
	}

	if (OutfitMesh == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: 덮을 옷 메시가 없다"));
		return false;
	}

	USkeletalMeshComponent* Body = FindBodyMesh();
	if (!IsValid(Body))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("장비: 몸(스켈레탈 메시)이 없다 — 옷을 덮을 자리가 없다"));
		return false;
	}

	// 부위마다 컴포넌트 하나를 만들어 두고 계속 쓴다 (손에 드는 쪽과 같은 이유 — 갈아입을 때마다
	// 만들고 지우면 쌓인다). 부위별로 따로라 상의만 갈아입어도 하의는 그대로 남는다.
	TObjectPtr<USkeletalMeshComponent>* Existing = OutfitMeshes.Find(Slot);
	USkeletalMeshComponent* Layer = (Existing != nullptr) ? Existing->Get() : nullptr;
	if (!IsValid(Layer))
	{
		Layer = NewObject<USkeletalMeshComponent>(GetOwner());
		Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Layer->SetMobility(EComponentMobility::Movable);
		Layer->RegisterComponent();
		// 소켓 이름 없이 몸 자체에 붙인다 — 옷은 손처럼 한 점에 매다는 것이 아니라 몸에 겹치는 것이다.
		// 배율도 몸을 따른다(M13-42): 몸은 캡슐 키에 맞춰 커지는데(`ApplyPlayerBody`) 옷만 원래 크기면
		// 리더 포즈 뼈 자리를 옷 컴포넌트 공간에서 읽어 몸의 절반 남짓 크기로 허리 아래에 뜬다.
		Layer->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetIncludingScale);
		OutfitMeshes.Add(Slot, Layer);
	}

	Layer->SetSkeletalMeshAsset(OutfitMesh);
	// **여기가 이 조각의 핵심이다** — 몸을 리더로 삼으면 옷은 자기 애니메이션을 돌리지 않고
	// 몸의 뼈 자세를 그대로 읽는다. 이게 없으면 걸을 때 몸만 움직이고 옷은 T 포즈로 서 있다.
	Layer->SetLeaderPoseComponent(Body);

	UE_LOG(LogSnarehaven, Log, TEXT("장비: 옷을 부위 %d 에 덮었다 — 리더 %s"),
		static_cast<int32>(Slot), *GetNameSafe(Layer->LeaderPoseComponent.Get()));
	return true;
}

bool UEquipmentComponent::UnequipSlot(EEquipSlot Slot)
{
	// 장착과 같은 규칙 — 게스트는 요청만 올린다 (M9-06).
	if (!HasEquipAuthority())
	{
		Server_UnequipSlot(Slot, bQuietChanges);
		return false;
	}

	// 컴포넌트를 지우지 않고 메시만 비운다 — 다시 들 때 그대로 쓴다(위 EquipItem 의 이유와 짝).
	// 들고 있는가를 아는 것은 **목록 쪽**이다 — 메시 컴포넌트는 한 번 만들면 빈 채로 남아 있어서
	// 그걸로 보면 빈 부위를 떼고도 "뗐다" 고 답한다 (2층이 이걸 잡았다).
	bool bRemoved = EquippedSlots.RemoveAll(
		[Slot](const FEquippedSlot& Entry) { return Entry.Slot == Slot; }) > 0;

	if (TObjectPtr<UStaticMeshComponent>* Found = SlotMeshes.Find(Slot); Found != nullptr && IsValid(Found->Get()))
	{
		// 기본 겉모습(M15-05)은 든 것이 아니다 — 빈 머리를 또 벗겨도 "뗐다" 고 답하지 않게 센다에서 뺀다.
		// 꾸미기가 덮은 메시도 든 것이 아니다 — 그 부위 판단은 목록만 본다.
		bRemoved = bRemoved || ((*Found)->GetStaticMesh() != nullptr && FindDefaultLookRow(Slot).IsNone()
			&& GetCosmeticItem(Slot).IsNone());
		(*Found)->SetStaticMesh(nullptr);
	}

	// 옷은 행 없이도 덮을 수 있어서(`EquipOutfitMesh` 직접 호출) 행 이름만 보면 벗겨지지 않는다 —
	// 덮인 메시가 있는지도 같이 본다.
	if (TObjectPtr<USkeletalMeshComponent>* Found = OutfitMeshes.Find(Slot); Found != nullptr && IsValid(Found->Get()))
	{
		bRemoved = bRemoved || ((*Found)->GetSkeletalMeshAsset() != nullptr && GetCosmeticItem(Slot).IsNone());
		(*Found)->SetSkeletalMeshAsset(nullptr);
	}

	if (Slot == EEquipSlot::Head)
	{
		SetHeadLight(nullptr);
	}

	// 꾸미기가 덮은 부위는 장비를 벗어도 꾸미기가 그대로 보인다.
	ApplyCosmeticVisual(Slot);
	// 벗은 부위는 기본 겉모습으로 돌아온다 (빈 등이 아니라 — M15-05), 빈 머리는 머리카락으로 (M19-01).
	ApplyDefaultLook();
	UpdateHandVisibility();
	UpdateHair();

	if (bRemoved)
	{
		RequestChangeCue(Slot, nullptr);
		UpdateHoldLoop();
		OnEquipmentChanged.Broadcast();
	}

	return bRemoved;
}

FName UEquipmentComponent::GetEquippedItem(EEquipSlot Slot) const
{
	const FEquippedSlot* Found = EquippedSlots.FindByPredicate(
		[Slot](const FEquippedSlot& Entry) { return Entry.Slot == Slot; });
	return (Found != nullptr) ? Found->ItemRowName : NAME_None;
}

const FItemRow* UEquipmentComponent::GetEquippedRow(EEquipSlot Slot) const
{
	const FName RowName = GetEquippedItem(Slot);
	if (RowName.IsNone() || !IsValid(ItemTable))
	{
		return nullptr;
	}

	// 못 찾아도 경고를 남기지 않는다 — 부르는 쪽이 "없으면 기본값" 을 정상으로 치는 길이라
	// (M11-04, DESIGN 8.6-b) 맨손일 때마다 로그가 쌓이면 진짜 문제가 묻힌다.
	return ItemTable->FindRow<FItemRow>(RowName, FString(), /*bWarnIfRowMissing=*/false);
}

UStaticMeshComponent* UEquipmentComponent::GetEquippedMesh(EEquipSlot Slot) const
{
	const TObjectPtr<UStaticMeshComponent>* Found = SlotMeshes.Find(Slot);
	return (Found != nullptr) ? Found->Get() : nullptr;
}

USkeletalMeshComponent* UEquipmentComponent::GetEquippedOutfit(EEquipSlot Slot) const
{
	const TObjectPtr<USkeletalMeshComponent>* Found = OutfitMeshes.Find(Slot);
	return (Found != nullptr) ? Found->Get() : nullptr;
}

void UEquipmentComponent::UpdateHoldLoop()
{
	const FItemRow* const ToolRow = GetEquippedRow(EEquipSlot::Tool);
	const FName WantedCue = ToolRow != nullptr ? ToolRow->HoldLoopSoundCue : NAME_None;
	// 떨림은 소리와 같은 조건(모터 도구를 들었다)이지만 오디오 컴포넌트에 기대지 않는다 — 소리 없는 기기에서도 떤다.
	bMotorToolHeld = !WantedCue.IsNone();
	if (!bMotorToolHeld)
	{
		ToolWorkSecondsLeft = 0.f;
	}
	SetComponentTickEnabled(bMotorToolHeld);
	// 소리는 여기서 켜지 않는다 — 도구질하는 동안만 틱이 켠다(INBOX 2026-10-05 18:20 「베기 한 번에 소리가 안 멈춘다」).
	// 바꿔 들거나 벗으면 나던 소리를 바로 끊는다.
	if (WantedCue != HoldLoopCue)
	{
		StopHoldLoop();
		HoldLoopCue = WantedCue;
	}
}

void UEquipmentComponent::StopHoldLoop()
{
	if (IsValid(HoldLoop))
	{
		HoldLoop->Stop();
		HoldLoop->DestroyComponent();
		UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 도구질 되풀이 소리 멈춤 — %s"), *HoldLoopCue.ToString());
	}
	HoldLoop = nullptr;
}

bool UEquipmentComponent::IsHoldLoopPlaying() const
{
	return IsValid(HoldLoop) && HoldLoop->IsPlaying();
}

float UEquipmentComponent::GetToolVibrationAmplitudeCm() const
{
	if (!bMotorToolHeld)
	{
		return 0.f;
	}
	return ToolVibrationAmplitudeCm * (ToolWorkSecondsLeft > 0.f ? 1.f : IdleVibrationRatio);
}

void UEquipmentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ToolWorkSecondsLeft = FMath::Max(ToolWorkSecondsLeft - DeltaTime, 0.f);
	// 모터 소리는 쓰는 동안만 — 동작이 끝나면 멈춘다(INBOX 2026-10-05 18:20). 소리는 몸에 붙어 따라다니고,
	// 남의 폰에서도 복제로 내려온 동작이 이 길을 태우므로 그 사람 자리에서 난다.
	const bool bWorking = bMotorToolHeld && ToolWorkSecondsLeft > 0.f && !HoldLoopCue.IsNone();
	if (bWorking && !IsValid(HoldLoop))
	{
		USceneComponent* const Root = GetOwner() != nullptr ? GetOwner()->GetRootComponent() : nullptr;
		HoldLoop = USnareSoundSubsystem::SpawnLoopAttached(this, HoldLoopCue, Root);
		if (IsValid(HoldLoop))
		{
			UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 도구질 되풀이 소리 시작 — %s %.2f초"), *HoldLoopCue.ToString(), ToolWorkSecondsLeft);
		}
	}
	else if (!bWorking && IsValid(HoldLoop))
	{
		StopHoldLoop();
	}
	// 메시에 톱날 부품이 없어(루프#215) 톱날을 돌리는 대신 부착물 전체를 떤다 (M18-06 감독 결정 ①).
	// 원을 그리며 떤다 — 30Hz 사인 한 축은 60fps 에서 매 프레임 0 에 걸려 안 보이지만, 원은 진폭이 늘 그대로다.
	ToolVibrationPhase = FMath::Fmod(ToolVibrationPhase + UE_TWO_PI * ToolVibrationHz * DeltaTime, UE_TWO_PI);
	const float Amplitude = GetToolVibrationAmplitudeCm();
	UStaticMeshComponent* const Tool = GetEquippedMesh(EEquipSlot::Tool);
	if (IsValid(Tool))
	{
		// 쥐는 자리(`ApplyEquipVisual` 이 둔 상대 자리) 위에 떨림만 얹는다.
		Tool->SetRelativeLocation(ToolRestLocation
			+ FVector(FMath::Cos(ToolVibrationPhase), FMath::Sin(ToolVibrationPhase), 0.f) * Amplitude);
	}
}
