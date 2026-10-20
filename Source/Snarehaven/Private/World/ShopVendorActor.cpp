#include "World/ShopVendorActor.h"
#include "SnareCheckSwitch.h"

#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Combat/MeleeAttackComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Equipment/EquipmentComponent.h"
#include "SnarehavenCharacter.h"
#include "Player/PlayerBodyAnimInstance.h"
#include "UI/RanchHudWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/SnareWorldLabelWidget.h"
#include "UI/ShopScreenWidget.h"
#include "UI/SnarePromptCardWidget.h"
#include "UI/SnareMinimapHud.h"
#include "World/SnareSweetPotato.h"
#include "World/QuestGiverActor.h"
#include "World/SnareHookNpcActor.h"
#include "World/SnareZoneRules.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	/** 좌판 수평 긴 변 (DESIGN 8.14 「잡화점·선착장 진짜 모습」 좌판 행). */
	constexpr float StallLongSideCm = 260.f;
	/**
	 * 좌판 앞면(카운터)이 상인 캡슐 중심 **뒤(액터 −X)** 로 물러난 거리 — DESIGN 8.14 「상인 자리 = 좌판 앞」(M13-41):
	 * "좌판 앞면(카운터)을 상인 캡슐 중심 뒤 40cm 에 둔다 — 캡슐 반지름 34 + 틈 6. 상인은 차양 밑이 아니라
	 * 카운터 앞(손님 쪽)에 선다". 고정 카메라가 상인보다 뒤(+X 세계)를 화면 위로 그려 좌판이 상인을 못 가린다.
	 */
	constexpr float StallBehindCm = 40.f;
	/** 고구마 바구니 가장 긴 변 / 상인 앞·옆 거리 — DESIGN 8.17 「일요일 모습」 줄. */
	constexpr float PotatoBasketSizeCm = 80.f;
	constexpr float PotatoBasketAheadCm = 70.f;
	constexpr float PotatoBasketSideCm = -60.f;
	/** 상인 모자 (같은 표 「잡화상 몸」 행 · `DT_ItemTable` 의 ItemID). */
	const FName MerchantHatItemID(TEXT("item.merchant_hat"));
	/** 게임 카메라는 월드 +X 를 본다(`CameraBoom` 요 0) — 카메라 쪽을 보려면 −X, 요 180도. */
	constexpr float FacingCameraYaw = 180.f;
	/** 돌아보는 속도(`FMath::RInterpTo` 의 속도값, 초당 비율). */
	constexpr float TurnInterpSpeed = 8.f;
	/** 이 각도 안이면 다 돌았다고 보고 틱을 끈다. */
	constexpr float TurnDoneDegrees = 0.5f;
	/** `-SnareSell=<ItemID>`(M28-09g 온라인 실측) — 판이 펴진 뒤 팔기 줄을 누르기까지 기다리는 초(로그인·보유 스냅샷이 붙을 틈). */
	constexpr float CheckSellPressSeconds = 12.f;
	/** 같은 스위치 — 누른 뒤 서버 답이 판 글에 붙기까지 기다리는 초. */
	constexpr float CheckSellReplySeconds = 6.f;
}

AShopVendorActor::AShopVendorActor()
{
	// 거리 검사는 타이머가 한다 (engines/unreal.md 코드 규칙 2).
	// 돌아보는 동안만 켠다(`HandleProximityScan` 이 켜고 다 돌면 끈다).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// 몸 크기·바닥 높이는 플레이어 캡슐에서 읽는다 — 같은 몸이라 같은 키다(숫자를 새로 만들지 않는다).
	const UCapsuleComponent* const PlayerCapsule = GetDefault<ASnarehavenCharacter>()->GetCapsuleComponent();
	const float HalfHeight = IsValid(PlayerCapsule) ? PlayerCapsule->GetUnscaledCapsuleHalfHeight() : 0.f;

	// 임포트한 몸·좌판은 둘 다 **+Y 를 앞으로** 서 있다 — 액터 앞(+X)으로 돌린다(플레이어 몸과 같은 보정).
	const FRotator MeshFacesPlusY(0.f, -90.f, 0.f);

	Stall = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Stall"));
	Stall->SetupAttachment(RootComponent);
	Stall->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Stall->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> StallMeshAsset(
		TEXT("/Game/Game/Environment/Props/ShopStall/SM_Prop_ShopStall_01.SM_Prop_ShopStall_01"));
	if (StallMeshAsset.Succeeded())
	{
		// 수평 긴 변을 표 값에 맞추는 한 배율. 받침점이 바닥이라 발밑 높이에 둔다.
		const FBox Box = StallMeshAsset.Object->GetBoundingBox();
		const float Scale = StallLongSideCm / FMath::Max(KINDA_SMALL_NUMBER, FMath::Max(Box.GetSize().X, Box.GetSize().Y));
		// 메시 앞면(+Y 최대)을 돌려 놓으면 액터 +X 쪽 끝(카운터)이 된다 — 카운터는 손님 쪽을 보고,
		// 그 끝이 상인 뒤 40cm 에 오게 민다(좌판 전체가 상인 뒤에 선다).
		const FVector Front = MeshFacesPlusY.RotateVector(FVector(Box.GetCenter().X, Box.Max.Y, 0.f)) * Scale;
		Stall->SetStaticMesh(StallMeshAsset.Object);
		Stall->SetRelativeScale3D(FVector(Scale));
		Stall->SetRelativeLocationAndRotation(FVector(-StallBehindCm - Front.X, -Front.Y, -HalfHeight), MeshFacesPlusY);
	}

	// 고구마 바구니 — 일요일에 연 고구마 장수 앞 땅에만 보인다(바구니 가방 모양을 좌판에 올림).
	PotatoBasket = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PotatoBasket"));
	PotatoBasket->SetupAttachment(RootComponent);
	PotatoBasket->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PotatoBasket->SetGenerateOverlapEvents(false);
	PotatoBasket->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BasketMeshAsset(
		TEXT("/Game/Game/Characters/Equipment/Backpacks/MeadowBasketPack/SM_Backpack_MeadowBasketPack_01.SM_Backpack_MeadowBasketPack_01"));
	if (BasketMeshAsset.Succeeded())
	{
		const FBox Box = BasketMeshAsset.Object->GetBoundingBox();
		const float Scale = PotatoBasketSizeCm / FMath::Max(KINDA_SMALL_NUMBER, FMath::Max3(Box.GetSize().X, Box.GetSize().Y, Box.GetSize().Z));
		PotatoBasket->SetStaticMesh(BasketMeshAsset.Object);
		PotatoBasket->SetRelativeScale3D(FVector(Scale));
		PotatoBasket->SetRelativeLocation(FVector(PotatoBasketAheadCm, PotatoBasketSideCm, -HalfHeight) - Box.GetCenter() * Scale + FVector(0.f, 0.f, Box.GetExtent().Z * Scale));
	}

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BodyMeshAsset(
		TEXT("/Game/Game/Characters/Player/Snare/SK_Player_Body.SK_Player_Body"));
	if (BodyMeshAsset.Succeeded())
	{
		// `ASnarehavenCharacter::ApplyPlayerBody` 와 같은 맞춤 — 캡슐 높이 ÷ 메시 높이, 원점(발밑)을 바닥에.
		const float MeshHeight = BodyMeshAsset.Object->GetBounds().BoxExtent.Z * 2.f;
		Body->SetSkeletalMesh(BodyMeshAsset.Object);
		Body->SetRelativeScale3D(FVector(MeshHeight > KINDA_SMALL_NUMBER ? HalfHeight * 2.f / MeshHeight : 1.f));
		Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -HalfHeight), MeshFacesPlusY);
	}
	static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleAnimAsset(
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Idle.A_Player_Idle"));
	if (IdleAnimAsset.Succeeded())
	{
		IdleAnim = IdleAnimAsset.Object;
	}

	Equipment = CreateDefaultSubobject<UEquipmentComponent>(TEXT("Equipment"));
	// 머리카락은 플레이어와 같은 덩어리·같은 규칙(INBOX 보정 10-19 22:45 ②) — 덮는 모자 밑으로는 아랫단 아래만 (DESIGN 3.5 「모자 밑 머리카락」).
	Equipment->DefaultHairMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Game/Game/Characters/Player/Hair/SM_Player_Hair_01.SM_Player_Hair_01")));
	Equipment->HairUnderHatMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/Game/Characters/Player/Hair/M_Player_HairUnderHat.M_Player_HairUnderHat")));

	// 평일 팻말 — 과일나무 이름표와 같은 길(화면 공간 알약). 고구마 장수가 닫은 날에만 켠다.
	SignCard = CreateDefaultSubobject<UWidgetComponent>(TEXT("SignCard"));
	SignCard->SetupAttachment(RootComponent);
	SignCard->SetRelativeLocation(FVector(0.f, 0.f, HalfHeight + 40.f));
	SignCard->SetWidgetSpace(EWidgetSpace::Screen);
	SignCard->SetDrawAtDesiredSize(true);
	SignCard->SetPivot(FVector2D(0.5f, 1.f));
	SignCard->SetWidgetClass(USnareWorldLabelWidget::StaticClass());
	SignCard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SignCard->SetGenerateOverlapEvents(false);
	SignCard->SetVisibility(false);

	// 머리 위 「!」「?」 — 모자 위로 올라오게 상인 팻말(+40)보다 높이 둔다. 처음엔 꺼져 있다. 메인 스토리 대상(솔방울·모래)만 켠다.
	MarkCard = CreateDefaultSubobject<UWidgetComponent>(TEXT("StoryMarkCard"));
	MarkCard->SetupAttachment(RootComponent);
	MarkCard->SetRelativeLocation(FVector(0.f, 0.f, HalfHeight + 70.f));
	MarkCard->SetWidgetSpace(EWidgetSpace::Screen);
	MarkCard->SetDrawAtDesiredSize(true);
	MarkCard->SetPivot(FVector2D(0.5f, 1.f));
	MarkCard->SetWidgetClass(USnareWorldLabelWidget::StaticClass());
	MarkCard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkCard->SetGenerateOverlapEvents(false);
	MarkCard->SetVisibility(false);

	// 🟡 이름은 감독 가정 — 설계서에 상인 이름이 없다. 레퍼런스 원본 이름은 쓰지 않는다 (Reference.md).
	VendorName = GetKindName(EShopKind::General);
	HatItemID = GetKindHat(EShopKind::General);
}

void AShopVendorActor::SetShopKind(EShopKind InKind)
{
	ShopKind = InKind;
	OnRep_ShopKind();
}

void AShopVendorActor::ApplyKindLook()
{
	VendorName = GetKindName(ShopKind);
	HatItemID = GetKindHat(ShopKind);
	if (IsValid(Equipment))
	{
		Equipment->SetHairColor(GetKindHair(ShopKind).Color);
	}
}

void AShopVendorActor::OnRep_ShopKind()
{
	ApplyKindLook();
	if (UShopScreenWidget* const Shop = GetShopScreen())
	{
		Shop->SetVendorName(VendorName);
		Shop->SetShopKind(ShopKind);
		Shop->SetPromptTarget(GetPromptWorldLocation());
	}
}

FText AShopVendorActor::GetKindName(EShopKind Kind)
{
	switch (Kind)
	{
	case EShopKind::Fishing:
		return LOCTEXT("VendorNameFishing", "낚시 상인 고등어");
	case EShopKind::Farming:
		return LOCTEXT("VendorNameFarming", "농부 상인 감자");
	case EShopKind::Clothes:
		return LOCTEXT("VendorNameClothes", "옷 가게 단추");
	case EShopKind::SweetPotato:
		return LOCTEXT("VendorNameSweetPotato", "고구마 장수 달순");
	default:
		return LOCTEXT("VendorNameDefault", "잡화상 도토리");
	}
}

FName AShopVendorActor::GetKindHat(EShopKind Kind)
{
	switch (Kind)
	{
	case EShopKind::Fishing:
		return TEXT("item.fisher_hat");
	case EShopKind::Farming:
		return TEXT("item.farmer_hat");
	case EShopKind::Clothes:
		return TEXT("item.cook_hat");
	case EShopKind::SweetPotato:
		return TEXT("item.farmer_hat");   // 밀짚 계열 기존 모자 재사용 (DESIGN 8.17)
	default:
		return MerchantHatItemID;
	}
}

void AShopVendorActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShopVendorActor, ShopKind);
}

UShopScreenWidget* AShopVendorActor::GetShopScreen() const
{
	return Cast<UShopScreenWidget>(Screen);
}

void AShopVendorActor::BeginPlay()
{
	Super::BeginPlay();
	// 레벨에 놓인 상인은 `SetShopKind` 를 거치지 않는다 — 놓인 종류대로 이름·모자를 맞춘다.
	ApplyKindLook();
	// 놓인 요(바다 쪽 등)가 어떻든 처음엔 카메라 쪽을 본다 — 등을 돌린 NPC 는 얼굴이 안 보인다.
	FacingTargetYaw = FacingCameraYaw;
	SnapFacing(FacingTargetYaw);

	// 플레이어 몸과 같은 애님 인스턴스로 대기 클립을 돌리고 같은 고개 들기를 건다 — 단일 노드로 돌리면 머리가 숙은
	// 클립 그대로라, 55° 내려다보는 카메라에 큰 모자 윗면만 보여 얼굴이 사라졌다(루프#1426 갈고리 「유령」).
	if (IsValid(Body) && IsValid(IdleAnim))
	{
		Body->SetAnimInstanceClass(UPlayerBodyAnimInstance::StaticClass());
		if (UPlayerBodyAnimInstance* const BodyAnim = Cast<UPlayerBodyAnimInstance>(Body->GetAnimInstance()))
		{
			const ASnarehavenCharacter* const PlayerDefaults = GetDefault<ASnarehavenCharacter>();
			BodyAnim->SetHeadLift(PlayerDefaults->GetHeadBoneName(), PlayerDefaults->GetHeadLiftDegrees());
			BodyAnim->SetBaseClip(IdleAnim, true, 1.f, true);
		}
		else
		{
			Body->PlayAnimation(IdleAnim, true);
		}
	}
	// 모자는 권위 쪽이 걸고 복제로 내려간다 — 게스트는 `OnRep_EquippedSlots` 로 같은 모자를 붙인다.
	if (HasAuthority() && IsValid(Equipment))
	{
		// 상인이 갈아 쓴 것이 아니다 — 소리 없이 (M47-03).
		const UEquipmentComponent::FQuietChanges Quiet(Equipment);
		Equipment->EquipItemByItemID(HatItemID);
	}
	// 화면은 사람이 보는 기기에서만. 헤드리스 서버엔 뷰포트가 없다.
	if (GetNetMode() != NM_DedicatedServer)
	{
		ApplySundayOnly();
		StartProximityScan();
	}
}

UStaticMeshComponent* AShopVendorActor::GetHairMesh() const
{
	return IsValid(Equipment) ? Equipment->GetHairMesh() : nullptr;
}

AShopVendorActor::FNpcHair AShopVendorActor::GetKindHair(EShopKind Kind)
{
	switch (Kind)
	{
	case EShopKind::Fishing:
		return { FLinearColor(0.60f, 0.35f, 0.08f), 1.0f, 0.95f };   // 금갈색 긴 뒷머리
	case EShopKind::Farming:
		return { FLinearColor(0.50f, 0.12f, 0.03f), 1.0f, 0.72f };   // 붉은 갈색
	case EShopKind::Clothes:
		return { FLinearColor(0.02f, 0.02f, 0.03f), 1.0f, 1.0f };   // 검정 길게
	case EShopKind::SweetPotato:
		return { FLinearColor(0.35f, 0.35f, 0.38f), 1.0f, 0.85f };   // 회색
	default:
		return { FLinearColor(0.105f, 0.042f, 0.016f), 1.0f, 0.9f };   // 밤색
	}
}

void AShopVendorActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ProximityTimerHandle);
	}
	if (IsValid(Screen))
	{
		Screen->RemoveFromParent();
	}
	Super::EndPlay(EndPlayReason);
}

void AShopVendorActor::StartProximityScan()
{
	UWorld* const World = GetWorld();
	APawn* const Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	const UMeleeAttackComponent* const Melee = IsValid(Pawn) ? Pawn->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
	const float IntervalSeconds = IsValid(Melee) ? Melee->GetAttackIntervalSeconds() : -1.f;
	if (World == nullptr)
	{
		return;
	}
	if (IntervalSeconds <= 0.f)
	{
		// 폰(과 그 간격 값)이 붙기 전이다 — 주기를 지어내지 않고 다음 틱에 다시 본다.
		World->GetTimerManager().SetTimerForNextTick(this, &AShopVendorActor::StartProximityScan);
		return;
	}
	World->GetTimerManager().SetTimer(
		ProximityTimerHandle, this, &AShopVendorActor::HandleProximityScan, IntervalSeconds, true, 0.f);
}

void AShopVendorActor::HandleProximityScan()
{
	ApplySundayOnly();
	APawn* const LocalPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	TryShowArrivalNotice(LocalPawn);
	UpdateProximity(LocalPawn);
	FacingTargetYaw = ComputeFacingTargetYaw(LocalPawn);
	if (FMath::Abs(FRotator::NormalizeAxis(FacingTargetYaw - GetFacingYaw())) > TurnDoneDegrees)
	{
		SetActorTickEnabled(true);
	}
}

float AShopVendorActor::GetFacingYaw() const
{
	// 몸 메시는 +Y 가 앞이라 몸 요 + 90 이 바라보는 방향이다.
	return IsValid(Body) ? FRotator::NormalizeAxis(Body->GetComponentRotation().Yaw + 90.f) : FacingCameraYaw;
}

float AShopVendorActor::ComputeFacingTargetYaw(const APawn* Pawn) const
{
	if (IsValid(Pawn) && IsPawnInTalkRange(Pawn))
	{
		const FVector ToPawn = Pawn->GetActorLocation() - GetActorLocation();
		if (!ToPawn.IsNearlyZero())
		{
			// 폰이 옆·뒤여도 카메라 쪽에서 한도까지만 돈다 — 그 너머는 모자 윗면만 보인다.
			const float FromCamera = FRotator::NormalizeAxis(ToPawn.Rotation().Yaw - FacingCameraYaw);
			return FRotator::NormalizeAxis(FacingCameraYaw + FMath::Clamp(FromCamera, -MaxTalkTurnDegrees, MaxTalkTurnDegrees));
		}
	}
	return FacingCameraYaw;
}

void AShopVendorActor::SnapFacing(float WorldYaw)
{
	if (IsValid(Body))
	{
		Body->SetWorldRotation(FRotator(0.f, WorldYaw - 90.f, 0.f));
	}
}

void AShopVendorActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FRotator Now(0.f, GetFacingYaw(), 0.f);
	const FRotator Goal(0.f, FacingTargetYaw, 0.f);
	if (FMath::Abs(FRotator::NormalizeAxis(Goal.Yaw - Now.Yaw)) <= TurnDoneDegrees)
	{
		SnapFacing(FacingTargetYaw);
		SetActorTickEnabled(false);
		return;
	}
	SnapFacing(FMath::RInterpTo(Now, Goal, DeltaSeconds, TurnInterpSpeed).Yaw);
}

void AShopVendorActor::ApplySundayOnly()
{
	if (ShopKind != EShopKind::SweetPotato)
	{
		return;
	}
	ApplyOpenState(SnareSweetPotato::IsSundayNow());
}

void AShopVendorActor::ApplyOpenState(bool bOpen)
{
	if (ShopKind != EShopKind::SweetPotato)
	{
		return;
	}
	// 고구마 장수는 일요일에만 서 있다 — 다른 요일은 몸·모자를 숨기고 빈 수레(좌판)와 팻말만 둔다(DESIGN 8.17).
	bClosedToday = !bOpen;
	if (IsValid(Body))
	{
		Body->SetVisibility(bOpen, true);
	}
	if (IsValid(SignCard))
	{
		if (SignCard->GetUserWidgetObject() == nullptr)
		{
			SignCard->InitWidget();
		}
		if (USnareWorldLabelWidget* const Widget = Cast<USnareWorldLabelWidget>(SignCard->GetUserWidgetObject()))
		{
			Widget->SetLabel(bOpen ? SnareSweetPotato::OpenSignText() : SnareSweetPotato::ClosedSignText());
		}
		SignCard->SetVisibility(true);   // 열린 날도 머리 위 이름을 보인다 — 누가 고구마 장수인지 알리려고
	}
	if (IsValid(PotatoBasket))
	{
		PotatoBasket->SetVisibility(bOpen);
	}
}

bool AShopVendorActor::TryShowArrivalNotice(APawn* LocalPawn)
{
	if (ShopKind != EShopKind::SweetPotato || bClosedToday || bArrivalNoticeShown)
	{
		return false;
	}
	const ASnarehavenCharacter* const Character = Cast<ASnarehavenCharacter>(LocalPawn);
	URanchHudWidget* const Ranch = IsValid(Character) ? Character->GetRanchHud() : nullptr;
	if (!IsValid(Ranch))
	{
		return false;   // 목장 HUD 가 아직 없다 — 다음 검사에서 다시 본다
	}
	Ranch->ShowNotice(SnareSweetPotato::ArrivalNoticeText());
	bArrivalNoticeShown = true;
	return true;
}

void AShopVendorActor::SetStoryMark(SnareStory::EGiverMark Mark)
{
	StoryMark = Mark;
	if (!IsValid(MarkCard))
	{
		return;
	}
	if (Mark == SnareStory::EGiverMark::None)
	{
		MarkCard->SetVisibility(false);
		return;
	}
	if (MarkCard->GetUserWidgetObject() == nullptr)
	{
		MarkCard->InitWidget();
	}
	if (USnareWorldLabelWidget* const Widget = Cast<USnareWorldLabelWidget>(MarkCard->GetUserWidgetObject()))
	{
		Widget->SetLabel(SnareStory::GiverMarkText(Mark));
	}
	MarkCard->SetVisibility(true);
}

bool AShopVendorActor::IsStoryMarkShown() const
{
	return IsValid(MarkCard) && MarkCard->IsVisible();
}

FText AShopVendorActor::GetShownStoryMarkText() const
{
	const USnareWorldLabelWidget* const Widget = IsValid(MarkCard) ? Cast<USnareWorldLabelWidget>(MarkCard->GetUserWidgetObject()) : nullptr;
	return Widget != nullptr ? Widget->GetLabel() : FText::GetEmpty();
}

SnareStory::EStoryNpc AShopVendorActor::GetStoryNpc() const
{
	if (IsA<AQuestGiverActor>())
	{
		return SnareStory::EStoryNpc::Solbangul;
	}
	if (IsA<ASnareHookNpcActor>())
	{
		return SnareStory::EStoryNpc::Hook;
	}
	return ShopKind == EShopKind::Fishing ? SnareStory::EStoryNpc::Mollae : SnareStory::EStoryNpc::None;
}

const AShopVendorActor* AShopVendorActor::FindStoryNpc(UWorld* World, SnareStory::EStoryNpc Npc)
{
	if (World == nullptr || Npc == SnareStory::EStoryNpc::None)
	{
		return nullptr;
	}
	for (TActorIterator<AShopVendorActor> It(World); It; ++It)
	{
		if (IsValid(*It) && It->GetStoryNpc() == Npc)
		{
			return *It;
		}
	}
	return nullptr;
}

bool AShopVendorActor::IsSignShown() const
{
	return IsValid(SignCard) && SignCard->IsVisible();
}

FText AShopVendorActor::GetShownSignText() const
{
	const USnareWorldLabelWidget* const Widget = IsValid(SignCard) ? Cast<USnareWorldLabelWidget>(SignCard->GetUserWidgetObject()) : nullptr;
	return Widget != nullptr ? Widget->GetLabel() : FText();
}

bool AShopVendorActor::IsPawnInTalkRange(const APawn* Pawn) const
{
	if (bClosedToday)
	{
		return false;   // 일요일이 아니라 닫은 장수 — 말을 걸 수 없다
	}
	// 존 이동 거리(150)가 아니라 상인 대화 거리(DESIGN 3.1 = 250) — 노점 충돌로 손님이 184cm 밖에서 멈춘다 (M22).
	const UMeleeAttackComponent* const Melee = IsValid(Pawn) ? Pawn->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
	const float RangeCm = IsValid(Melee) ? Melee->GetNpcTalkRangeCm() : -1.f;
	return RangeCm >= 0.f && FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation()) <= FMath::Square(RangeCm);
}

bool AShopVendorActor::UpdateProximity(APawn* LocalPawn)
{
	if (!IsPawnInTalkRange(LocalPawn))
	{
		if (IsValid(Screen) && Screen->IsInViewport())
		{
			// 멀어지면 닫고 내린다 — 다음에 다가오면 다시 "열기" 부터.
			CollapseScreen();
			Screen->RemoveFromParent();
		}
		return false;
	}

	APlayerController* const Controller = Cast<APlayerController>(LocalPawn->GetController());
	if (!IsValid(Screen) && IsValid(Controller))
	{
		Screen = CreateScreen(Controller);
	}
	if (IsValid(Screen) && !Screen->IsInViewport())
	{
		Screen->AddToViewport();
		UE_LOG(LogSnarehaven, Log, TEXT("%s"), GetShopScreen() != nullptr
			? TEXT("[Shop] 상인 앞 — 상점 화면을 올린다") : TEXT("[Quest] 의뢰인 앞 — 의뢰 화면을 올린다"));
	}
	return true;
}

AShopVendorActor* AShopVendorActor::FindInTalkRange(const APawn* Pawn)
{
	UWorld* const World = IsValid(Pawn) ? Pawn->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	AShopVendorActor* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<AShopVendorActor> It(World); It; ++It)
	{
		if (It->IsA<ASnareHookNpcActor>())
		{
			continue;   // 갈고리는 열 판이 없다 — 곁에서 버튼이 「열기」 로 바뀌면 눌러도 아무 일이 없다
		}
		const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), It->GetActorLocation());
		if (DistSq < NearestDistSq && It->IsPawnInTalkRange(Pawn))
		{
			Nearest = *It;
			NearestDistSq = DistSq;
		}
	}
	return Nearest;
}

bool AShopVendorActor::OpenScreenFor(APawn* LocalPawn)
{
	// 타이머(공격 간격) 사이에 막 들어왔으면 화면이 아직 없다 — 같은 검사로 먼저 올린다.
	if (!UpdateProximity(LocalPawn) || !IsValid(Screen))
	{
		return false;
	}
	ExpandScreen();
	UE_LOG(LogSnarehaven, Log, TEXT("%s"), GetShopScreen() != nullptr
		? TEXT("[Shop] 상황 버튼 「열기」 — 상점 판을 편다") : TEXT("[Quest] 상황 버튼 「열기」 — 의뢰 판을 편다"));
	return true;
}

UUserWidget* AShopVendorActor::CreateScreen(APlayerController* Controller)
{
	UShopScreenWidget* const Shop = CreateWidget<UShopScreenWidget>(Controller, UShopScreenWidget::StaticClass());
	if (IsValid(Shop))
	{
		UGameInstance* const GameInstance = GetGameInstance();
		Shop->BindToBackend(GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr);
		Shop->SetVendorName(VendorName);
		Shop->SetShopKind(ShopKind);
		// 명함 둘째 줄 = 내가 선 존 (M15-18 · 시트의 좌표 줄 자리). 이름은 미니맵과 **같은 함수**에서 읽는다 —
		// 두 곳이 각자 적으면 같은 곳을 다른 이름으로 부른다.
		// 존 밖이면 비운다 — 미니맵의 「-」 를 명함에 그대로 찍지 않는다(10-16 검수).
		const FZoneRow* const VendorRegion = SnareZones::FindRegionAt(this, GetActorLocation());
		Shop->SetVendorZone(VendorRegion != nullptr ? SnareMinimapHud::FormatZoneName(VendorRegion) : FText::GetEmpty());
		// 시트 「구성」 — 상점 판이 열릴 때 배낭도 오른쪽에 같이 열린다(사고팔 때 내 칸이 보여야 한다).
		const ASnarehavenCharacter* const Customer = Cast<ASnarehavenCharacter>(Controller != nullptr ? Controller->GetPawn() : nullptr);
		Shop->BindBackpackScreen(Customer != nullptr ? Customer->GetBackpackScreen() : nullptr);
		// 3층 캡처용 (`-SnareShopOpen`) — 다가가자마자 창이 펴진 채로 뜬다. 거래 판정은 그대로 서버다.
		if (SnareCheckSwitch::Param(TEXT("SnareShopOpen")))
		{
			Shop->SetOpen(true);
			// `-SnareSell=<ItemID>`(M28-09g) — 팔기 탭에서 그 줄을 사람이 누르는 **같은 함수**(`RequestSell`, 하나 팔기)로 한 번 누른다.
			// 코인·배낭은 서버 `SellItem` 판정(CLAUDE.md 8). 가방 칸 하나를 비워 받아들여지는 제작을 재려고 쓴다.
			FString SellItemID;
			if (SnareCheckSwitch::Value(TEXT("SnareSell="), SellItemID) && GetWorld() != nullptr)
			{
				FTimerHandle SellTimer;
				GetWorldTimerManager().SetTimer(SellTimer, FTimerDelegate::CreateWeakLambda(Shop, [Shop, SellItemID]()
				{
					Shop->SetSellTab(true);
					const FName ItemID(*SellItemID);
					const bool bOnList = Shop->BuildSellLines().ContainsByPredicate([ItemID](const FShopLineView& Line) { return Line.ItemID == ItemID; });
					const bool bAsked = Shop->RequestSell(ItemID);
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 팔기 (-SnareSell=%s) — 팔기 줄 %s · 서버에 %s"), *SellItemID,
						bOnList ? TEXT("있음") : TEXT("없음"), bAsked ? TEXT("물었다") : TEXT("안 물었다"));
					FTimerHandle ReplyTimer;
					Shop->GetWorld()->GetTimerManager().SetTimer(ReplyTimer, FTimerDelegate::CreateWeakLambda(Shop, [Shop]()
					{
						UE_LOG(LogSnarehaven, Log, TEXT("[실측] 상점 판 글 「%s」 · %s"), *Shop->GetMessage().ToString(), *Shop->GetCoinText().ToString());
					}), CheckSellReplySeconds, false);
				}), CheckSellPressSeconds, false);
			}
			// `-SnareBuy=<ItemID>`(M28-09g 먹기 행) — 사기 탭에서 그 줄을 사람이 누르는 **같은 함수**(`RequestBuy`, 하나 사기)로 한 번 누른다.
			// 코인·배낭은 서버 `BuyItem` 판정. `-SnareSell=` 과 같이 주면 판 글이 붙은 뒤(팔아서 칸을 비운 뒤)에 산다.
			FString BuyItemID;
			if (SnareCheckSwitch::Value(TEXT("SnareBuy="), BuyItemID) && GetWorld() != nullptr)
			{
				const float BuySeconds = CheckSellPressSeconds + (SellItemID.IsEmpty() ? 0.f : CheckSellReplySeconds);
				FTimerHandle BuyTimer;
				GetWorldTimerManager().SetTimer(BuyTimer, FTimerDelegate::CreateWeakLambda(Shop, [Shop, BuyItemID]()
				{
					Shop->SetSellTab(false);
					const FName ItemID(*BuyItemID);
					const bool bOnList = Shop->BuildBuyLines().ContainsByPredicate([ItemID](const FShopLineView& Line) { return Line.ItemID == ItemID; });
					const bool bAsked = Shop->RequestBuy(ItemID);
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 사기 (-SnareBuy=%s) — 사기 줄 %s · 서버에 %s"), *BuyItemID,
						bOnList ? TEXT("있음") : TEXT("없음"), bAsked ? TEXT("물었다") : TEXT("안 물었다"));
					FTimerHandle ReplyTimer;
					Shop->GetWorld()->GetTimerManager().SetTimer(ReplyTimer, FTimerDelegate::CreateWeakLambda(Shop, [Shop]()
					{
						UE_LOG(LogSnarehaven, Log, TEXT("[실측] 상점 판 글 「%s」 · %s"), *Shop->GetMessage().ToString(), *Shop->GetCoinText().ToString());
					}), CheckSellReplySeconds, false);
				}), BuySeconds, false);
			}
		}
	}
	return Shop;
}

FVector AShopVendorActor::GetPromptWorldLocation() const
{
	// 액터 원점 = 캡슐 중심(DESIGN 8.14 「잡화상 액터 자리」) — 반높이만큼 올리면 정수리다.
	return GetActorLocation() + FVector(0.f, 0.f, USnarePromptCardWidget::TapCardTargetLiftCm);
}

void AShopVendorActor::CollapseScreen()
{
	if (UShopScreenWidget* const Shop = GetShopScreen())
	{
		Shop->SetOpen(false);
	}
}

void AShopVendorActor::ExpandScreen()
{
	if (UShopScreenWidget* const Shop = GetShopScreen())
	{
		Shop->SetOpen(true);
	}
}

#undef LOCTEXT_NAMESPACE
