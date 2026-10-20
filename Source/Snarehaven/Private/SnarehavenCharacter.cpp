#include "SnarehavenCharacter.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "World/SnareCampCageActor.h"
#include "World/SnareLighthouseActor.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/SkeletalMeshActor.h"
#include "Player/SnareGasp.h"
#include "Player/SnareShiver.h"
#include "Player/SnareLowHealth.h"
#include "Player/SnareRevive.h"
#include "Player/SnareWarmth.h"
#include "Player/PlayerBodyAnimInstance.h"
#include "Player/SnareCaptureBurstActor.h"
#include "Player/SnareEmoteBubble.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "EngineUtils.h"
#include "Combat/CaptureComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Equipment/EquipmentComponent.h"
#include "Player/PlayerFaceBlinkComponent.h"
#include "Equipment/SnareGearMemorySubsystem.h"
#include "Gathering/GatherComponent.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareBlockListSubsystem.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "UI/CraftingScreenWidget.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Data/ItemRowLookup.h"
#include "Data/PlayerCombatRow.h"
#include "Data/PlayerVitalsRow.h"
#include "Inventory/InventoryComponent.h"
#include "Inventory/LootClaimComponent.h"
#include "Inventory/BackpackComponent.h"
#include "UI/BackpackScreenWidget.h"
#include "World/SnareWorldDensity.h"
#include "World/SnarePerfLog.h"
#include "Building/SnarePlacedStructureActor.h"
#include "UI/WorldMapScreenWidget.h"
#include "UI/WardrobeWidget.h"
#include "UI/FootProgressWidget.h"
#include "UI/PlayerNameTagWidget.h"
#include "Components/WidgetComponent.h"
#include "Player/PlayerVitalsComponent.h"
#include "Gathering/GatherableActor.h"
#include "Gathering/TapFishingComponent.h"
#include "World/WaterTravelComponent.h"
#include "World/SnareSound.h"
#include "World/SnareWeatherView.h"
#include "World/SnareZoneRules.h"
#include "Data/ZoneRow.h"
#include "World/BoatActor.h"
#include "World/SeaActor.h"
#include "World/FruitTreeActor.h"
#include "World/FruitTreeSync.h"
#include "World/ShopVendorActor.h"
#include "Player/MountComponent.h"
#include "Player/SnareHaptics.h"
#include "World/ZoneEffectComponent.h"
#include "Ranch/CoopReportComponent.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchInteractComponent.h"
#include "Ranch/RanchPetActor.h"
#include "Ranch/RanchSlotActor.h"
#include "Containers/Ticker.h"
#include "Ranch/RanchSyncComponent.h"
#include "SnarehavenGameMode.h"
#include "World/ZonePortalActor.h"
#include "World/ZoneTravelComponent.h"
#include "UI/AchievementScreenWidget.h"
#include "UI/CodexScreenWidget.h"
#include "UI/CrateScreenWidget.h"
#include "UI/GachaScreenWidget.h"
#include "UI/HuntHudWidget.h"
#include "UI/SnareFirstRunGuideWidget.h"
#include "UI/SnareNextGoalWidget.h"
#include "UI/SnareStoryChapterCardWidget.h"
#include "UI/SnareStoryDialogueWidget.h"
#include "UI/SnareStoryGoalWidget.h"
#include "World/QuestGiverActor.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "UI/QuestScreenWidget.h"
#include "UI/ShopScreenWidget.h"
#include "UI/SnareScreenCloser.h"
#include "UI/PauseMenuWidget.h"
#include "UI/PlayerSafetyWidget.h"
#include "UI/RanchHudWidget.h"
#include "UI/AttendanceScreenWidget.h"
#include "UI/GiftScreenWidget.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "AnimationRuntime.h"
#include "Engine/StaticMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/TouchInterface.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/SViewport.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "UI/WardrobeShopWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

namespace
{
	// 검사 전용 값이다 — 게임 규칙 숫자가 아니라 "얼마나 자주 되묻나" 라서 설계서 값이 아니다
	// (`-SnareMoveKey=` 가 0.01초로 키를 다시 누르는 것과 같은 성격).
	constexpr float CheckCapturePollSeconds = 0.1f;

	// `-SnareWardrobe`(M28-09 옷장 행 실측) — 배낭 화면이 붙은 뒤 장비 탭·「상점 열기」 를 누르기까지 기다리는 초.
	constexpr float CheckWardrobeOpenSeconds = 3.f;

	// `-SnareEat=<ItemID>`(M28-09 음식 행 실측) — 음식을 넣고 배낭을 연 뒤 「먹기」 를 누르기까지 기다리는 초(칸이 다시 그려질 틈).
	constexpr float CheckEatPressSeconds = 1.f;

	// `-SnareCraft=<ItemID>`(M28-09g 온라인 제작 행 실측) — 판이 열린 뒤 레시피 줄을 누르기까지 기다리는 초(로그인·보유 스냅샷이 붙을 틈).
	constexpr float CheckCraftPressSeconds = 12.f;

	// `-SnareTravel` 은 **한 프로세스에 한 번만** 떠난다. 맵을 갈아타면 폰이 새로 생겨 스위치가 다시
	// 걸리는데(`-SnareAutoAttack` 이 "맵마다 다시 걸린다" 던 것과 같다), 막지 않으면 두 맵의 포탈을
	// 오가며 캡처가 어느 존인지 알 수 없게 된다. 멤버로는 못 센다 — 세던 폰이 맵과 같이 사라진다.
	bool bCheckTravelUsed = false;

	// `-SnareCraft=<ItemID>`(M28-09 제작 판 행 실측)도 한 프로세스에 한 번만 누른다 — 상황 버튼 연타로 판이 다시 열려도.
	bool bCheckCraftUsed = false;

	// 아래 넷도 같은 성격의 검사 전용 값이다 (M4-04 `-SnareTouch=`).
	/** 터치를 넣기 전에 기다리는 시간 — 위젯이 한 번 그려져야 화면 좌표(캐시된 지오메트리)가 잡힌다. */
	constexpr float CheckTouchStartSeconds = 2.f;
	/** 손가락을 붙잡고 있는(또는 버튼이 켜지길 되묻는) 주기. */
	constexpr float CheckTouchPollSeconds = 0.1f;
	/** 첫 손가락. 엔진의 가짜 터치도 0번을 쓴다 (`SlateApplication.cpp` 의 `-faketouches` 경로). */
	constexpr int32 CheckTouchIndex = 0;
	/** 누르는 세기. 엔진 가짜 터치와 같은 1.0. */
	constexpr float CheckTouchForce = 1.f;

	/**
	 * `-SnareTouch=` 값이 장비 버튼을 가리키면 그 **부위**를, 아니면 `None` 을 돌려준다 (M9-03).
	 * `equip`(=무기, M5-06c 때 쓰던 이름 그대로) · `equip:tool` · `equip:top` · `equip:bottom`.
	 * 새 입력 액션을 만들지 않고 스위치만 늘린 것이라 옛 명령줄이 그대로 돈다.
	 */
	EEquipSlot ParseCheckTouchEquipSlot(const FString& TouchTarget)
	{
		if (!TouchTarget.StartsWith(TEXT("equip")))
		{
			return EEquipSlot::None;
		}

		const FString SlotName = TouchTarget.RightChop(5).Replace(TEXT(":"), TEXT(""));
		if (SlotName.IsEmpty() || SlotName == TEXT("weapon"))
		{
			return EEquipSlot::Weapon;
		}
		if (SlotName == TEXT("tool"))
		{
			return EEquipSlot::Tool;
		}
		if (SlotName == TEXT("top"))
		{
			return EEquipSlot::OutfitTop;
		}
		if (SlotName == TEXT("bottom"))
		{
			return EEquipSlot::OutfitBottom;
		}

		// 모르는 부위 이름은 장비 대상이 아닌 것으로 본다 — 아래 "모르는 대상" 경고로 떨어진다.
		return EEquipSlot::None;
	}

	/** 검사 터치가 들어갈 창. 헤드리스에도 오프스크린 창이 하나 있다. 없으면 nullptr 로 보낸다. */
	TSharedPtr<FGenericWindow> GetCheckTouchWindow()
	{
		const TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow();
		return Window.IsValid() ? Window->GetNativeWindow() : TSharedPtr<FGenericWindow>();
	}

	void SendCheckTouchDown(const FVector2D& ScreenPos)
	{
		FSlateApplication::Get().OnTouchStarted(GetCheckTouchWindow(), ScreenPos, CheckTouchForce, CheckTouchIndex,
			FSlateApplicationBase::SlateAppPrimaryPlatformUser, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice());
	}

	void SendCheckTouchMove(const FVector2D& ScreenPos)
	{
		FSlateApplication::Get().OnTouchMoved(ScreenPos, CheckTouchForce, CheckTouchIndex,
			FSlateApplicationBase::SlateAppPrimaryPlatformUser, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice());
	}

	void SendCheckTouchUp(const FVector2D& ScreenPos)
	{
		FSlateApplication::Get().OnTouchEnded(ScreenPos, CheckTouchIndex,
			FSlateApplicationBase::SlateAppPrimaryPlatformUser, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice());
	}

	/**
	 * 터치 인터페이스 에셋의 좌표 한 축을 화면 좌표로 푼다.
	 * 규칙은 우리가 정한 게 아니라 엔진이 그 에셋을 읽는 규칙 그대로다
	 * (`SVirtualJoystick.cpp` 의 `ResolveRelativePosition` — 0~1 은 화면 비율, 음수는 반대쪽 모서리 기준,
	 * 1 보다 크면 절대 픽셀). 여기서 다르게 풀면 손가락이 스틱 밖에 떨어져 검사가 거짓말을 한다.
	 */
	float ResolveTouchCoord(float Value, float ScreenSize, float ScaleFactor)
	{
		if (Value < -1.f)
		{
			return ScreenSize + Value * ScaleFactor;
		}
		if (Value < 0.f)
		{
			return ScreenSize + Value * ScreenSize;
		}
		if (Value <= 1.f)
		{
			return Value * ScreenSize;
		}
		return Value * ScaleFactor;
	}

	/** 엔진이 조이스틱 크기를 재는 기준 너비 (`SVirtualJoystick.cpp` 의 `GetScaleFactor`). */
	constexpr float VirtualJoystickDesiredWidth = 1024.f;

	/** 포획 순간 조이는 소리 크기 — DESIGN 3.1 「포획 순간」. */
	constexpr float CaptureCueVolume = 0.9f;

	/** 포획 누름(올가미 던지기) 소리 크기 — 휘두르기 `Swing` 과 같은 크기(`UMeleeAttackComponent`, DESIGN 2.2 소리). */
	constexpr float CaptureThrowCueVolume = 0.5f;
}

ASnarehavenCharacter::ASnarehavenCharacter()
{
	// M5-01c: 걷기·서기 클립을 속도로 고르려면 매 프레임 봐야 한다. 애님 블루프린트가 없어
	// (돼지와 같은 SingleNode 방식) 이 판단을 대신해 줄 AnimGraph 가 없다.
	PrimaryActorTick.bCanEverTick = true;

	// 탑다운: 컨트롤러 회전(마우스/시점)을 따라가지 않고 가는 방향을 본다.
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	// ROADMAP M0-04 — 고정 각 탑다운 카메라. 팔이 캐릭터를 따라가며 화면 중앙에 두고,
	// 컨트롤러·캐릭터가 돌아도 각이 흔들리지 않게 상속과 폰 회전 추종을 전부 끈다.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;
	// 팔 회전은 월드 기준 — 위 상속 끔은 팔이 **다음 틱에** 소켓을 되돌리는 것이라, 몸을 홱 돌린 그 장에는
	// 카메라가 몸과 같이 돌았다가 돌아와 화면 전체가 돌며 번졌다(M39-07b 쏘기 캡처). 월드 기준이면 그 틈이 없다.
	CameraBoom->SetUsingAbsoluteRotation(true);
	// 모바일 탑다운은 벽에 팔이 끼어 카메라가 튀는 일이 없어야 해서 충돌 보정을 끈다.
	CameraBoom->bDoCollisionTest = false;

	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCamera->bUsePawnControlRotation = false;

	// 보이는 몸 자리표시 — ACharacter 의 콜리전 캡슐은 렌더되지 않아서, 이걸 안 붙이면
	// 3층 캡처에 바닥만 찍히고 캐릭터가 없다(M0-06 에서 실제로 확인). 엔진 기본 실린더를
	// 콜리전 캡슐과 같은 크기로 늘여 "움직이는 캡슐"을 눈으로 볼 수 있게 한다.
	// 크기는 숫자를 새로 만들지 않고 캡슐 컴포넌트에서 읽어 계산한다 (DataDriven.md 3).
	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	// 충돌은 캡슐이 전담한다 — 자리표시가 물리에 끼어들면 나중에 진짜 메시로 바꿀 때 동작이 달라진다.
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderMeshAsset(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (PlaceholderMeshAsset.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(PlaceholderMeshAsset.Object);
		// 엔진 기본 실린더는 지름·높이 100cm(중심이 원점) — 캡슐 치수에 맞추려면 그 절반으로 나눈다.
		const float EngineCylinderHalfExtent = 50.f;
		const UCapsuleComponent* Capsule = GetCapsuleComponent();
		const float RadiusScale = Capsule->GetUnscaledCapsuleRadius() / EngineCylinderHalfExtent;
		const float HeightScale = Capsule->GetUnscaledCapsuleHalfHeight() / EngineCylinderHalfExtent;
		PlaceholderBody->SetRelativeScale3D(FVector(RadiusScale, RadiusScale, HeightScale));
	}

	// 머리 위 이름표 — 캡슐 꼭대기에서 110cm 위(시트 「머리 위 약 40px」 — 60cm 는 모자에 닿았다, 루프#838). 레퍼런스처럼 자기 것도 보인다.
	NameTag = CreateDefaultSubobject<UWidgetComponent>(TEXT("NameTag"));
	NameTag->SetupAttachment(GetCapsuleComponent());
	NameTag->SetRelativeLocation(FVector(0.f, 0.f, GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 110.f));
	NameTag->SetWidgetSpace(EWidgetSpace::Screen);
	NameTag->SetDrawAtDesiredSize(true);
	NameTag->SetPivot(FVector2D(0.5f, 1.f));
	NameTag->SetWidgetClass(UPlayerNameTagWidget::StaticClass());
	NameTag->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NameTag->SetGenerateOverlapEvents(false);

	// 발밑 행동 진행바(M15-19) — 캡슐 바닥에 걸고 위젯 위쪽 가운데를 그 자리에 맞춘다(발 **아래**로 그려진다).
	// 크기는 시트 값 그대로라 `SetDrawAtDesiredSize` 를 쓰지 않는다 — 막대의 바라는 크기는 브러시가 정해
	// 시트 60×6 과 무관하다.
	FootProgress = CreateDefaultSubobject<UWidgetComponent>(TEXT("FootProgress"));
	FootProgress->SetupAttachment(GetCapsuleComponent());
	FootProgress->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
	FootProgress->SetWidgetSpace(EWidgetSpace::Screen);
	FootProgress->SetDrawSize(FVector2D(UFootProgressWidget::SheetWidthPx, UFootProgressWidget::SheetHeightPx));
	FootProgress->SetPivot(FVector2D(0.5f, 0.f));
	FootProgress->SetWidgetClass(UFootProgressWidget::StaticClass());
	FootProgress->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FootProgress->SetGenerateOverlapEvents(false);

	// 근접 공격(M1-03) — 사거리 안 가장 가까운 동물을 스스로 골라 때린다. 숫자는 컴포넌트가 들고 있다.
	MeleeAttack = CreateDefaultSubobject<UMeleeAttackComponent>(TEXT("MeleeAttack"));

	// 포획(M1-05) — 대상은 근접 공격 컴포넌트가 하이라이트한 한 마리를 그대로 쓴다 (DESIGN 3.3 자동 상호작용).
	Capture = CreateDefaultSubobject<UCaptureComponent>(TEXT("Capture"));

	// 채집(M1-06) — 사거리는 위 근접 공격 컴포넌트의 자동 상호작용 거리를 그대로 빌려 쓴다 (DESIGN 3.3).
	Gather = CreateDefaultSubobject<UGatherComponent>(TEXT("Gather"));

	// 장비 겉모습(M5-06b) — 표가 말하는 메시를 손 소켓에 붙인다. 스탯이 없어 판정에는 끼지 않는다 (DESIGN 3.5).
	Equipment = CreateDefaultSubobject<UEquipmentComponent>(TEXT("Equipment"));
	// 레퍼런스 캐릭터는 **등에 배낭은 늘 있고 머리는 비어 있다** (레퍼런스 시트 `character_player.md`
	// "등에 배낭 — 항상" · "머리: 모자·고글 1슬롯"). M15-10 에서 기본 모자를 뺀 이유는 그것이
	// **진짜 모자를 써도 티가 안 나게** 만들었기 때문이다 (INBOX 2026-09-25 ④). 화면만이고 지급이 아니다.
	// 기본 배낭은 `DefaultLookScale` 만큼 작게 걸린다 (DESIGN 3.5 「기본 겉모습」).
	Equipment->DefaultLookItemIDs = { FName(TEXT("item.hiking_backpack")) };
	// 모자가 없으면 민머리 공 대신 머리카락 덩어리 (M19-01 · 시트 "머리카락은 덩어리 하나(모자와 교체)").
	Equipment->DefaultHairMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Game/Game/Characters/Player/Hair/SM_Player_Hair_01.SM_Player_Hair_01")));
	// 덮는 모자를 쓰면 머리카락을 모자 뒤 아랫단 아래만 그린다 (DESIGN 3.5 「모자 밑 머리카락」).
	Equipment->HairUnderHatMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/Game/Characters/Player/Hair/M_Player_HairUnderHat.M_Player_HairUnderHat")));

	// 얼굴 그림 눈 깜빡임 (INBOX 12:40 · 레퍼런스 얼굴 그림의 깜빡임 짝) — 감은 짝은 `make_player_face_texture.py` 가 그린다.
	FaceBlink = CreateDefaultSubobject<UPlayerFaceBlinkComponent>(TEXT("FaceBlink"));
	FaceBlink->ClosedEyesTexture = TSoftObjectPtr<UTexture>(
		FSoftObjectPath(TEXT("/Game/Game/Characters/Player/Snare/T_Player_Body_BaseColor_Blink.T_Player_Body_BaseColor_Blink")));
	// 표정 3종 짝 그림 (M85-01·03)
	FaceBlink->HurtTexture = TSoftObjectPtr<UTexture>(
		FSoftObjectPath(TEXT("/Game/Game/Characters/Player/Snare/T_Player_Body_BaseColor_Hurt.T_Player_Body_BaseColor_Hurt")));
	FaceBlink->DownTexture = TSoftObjectPtr<UTexture>(
		FSoftObjectPath(TEXT("/Game/Game/Characters/Player/Snare/T_Player_Body_BaseColor_Down.T_Player_Body_BaseColor_Down")));
	FaceBlink->HappyTexture = TSoftObjectPtr<UTexture>(
		FSoftObjectPath(TEXT("/Game/Game/Characters/Player/Snare/T_Player_Body_BaseColor_Happy.T_Player_Body_BaseColor_Happy")));

	// 인벤토리 캐시(M1-04). 보유 원본은 백엔드라 여기서는 캐시와 지급 요청 대기열만 든다 (DESIGN 4.5).
	Inventory = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));
	LootClaim = CreateDefaultSubobject<ULootClaimComponent>(TEXT("LootClaim"));
	Backpack = CreateDefaultSubobject<UBackpackComponent>(TEXT("Backpack"));

	// 목장 캐시(M2-04a)와 슬롯 상호작용(M2-04c). 상호작용 사거리·주기는 위 근접 공격 컴포넌트에서 빌린다.
	Ranch = CreateDefaultSubobject<URanchComponent>(TEXT("Ranch"));
	RanchInteract = CreateDefaultSubobject<URanchInteractComponent>(TEXT("RanchInteract"));

	// 낚시터 거점 캐시(M10-02). 목장과 **같은 클래스**다 — DESIGN 8.4 가 "목장·작물과 같은 짜임,
	// 새 메커니즘을 만들지 않는다" 라고 못 박았다. 갈리는 것은 백엔드 키·CloudScript 짝뿐이라
	// `StationKind` 한 칸으로 가른다. 찾을 때는 `URanchComponent::FindStation` 으로 — 둘이 같은
	// 클래스라 `FindComponentByClass` 는 먼저 붙은 목장을 준다.
	Fishing = CreateDefaultSubobject<URanchComponent>(TEXT("Fishing"));
	Fishing->SetStationKind(ERanchStationKind::Fishing);

	// 목장·낚시터 대기열 → 서버, 로그인 뒤 거점 저장값 되살리기 (BACKEND.md `GetBaseState`). 로직은 컴포넌트 안에 있다.
	RanchSync = CreateDefaultSubobject<URanchSyncComponent>(TEXT("RanchSync"));

	// 존 포탈 상호작용(M6-02b) — 사거리·주기는 위 근접 공격 컴포넌트에서 빌린다. 이동 자체는 서버 몫이라
	// 게스트 폰에서는 요청만 나간다 (DESIGN 4.1 · `docs/WORLD_ZONES.md` 표 B).
	ZoneTravel = CreateDefaultSubobject<UZoneTravelComponent>(TEXT("ZoneTravel"));

	// 체력·스태미나(M11-01) — 숫자는 하나도 여기 없다. 표를 읽는 것은 `PostInitializeComponents` 다.
	PlayerVitals = CreateDefaultSubobject<UPlayerVitalsComponent>(TEXT("PlayerVitals"));
	WaterTravel = CreateDefaultSubobject<UWaterTravelComponent>(TEXT("WaterTravel"));
	Mount = CreateDefaultSubobject<UMountComponent>(TEXT("Mount"));
	ZoneEffect = CreateDefaultSubobject<UZoneEffectComponent>(TEXT("ZoneEffect"));
	TapFishing = CreateDefaultSubobject<UTapFishingComponent>(TEXT("TapFishing"));

	// 제작·요리·건축(DESIGN 8.10·8.11) — 화면·놓기·불 곁 회복을 이 컴포넌트가 다 가진다.
	CreateDefaultSubobject<USnareBaseBuildComponent>(TEXT("BaseBuild"));

	// 같이 하기 보고(M21-06b2) — 같은 방 다른 사람 id 를 모아 서버에 알린다. 생산을 당기는 판정은 서버(DESIGN 3.2).
	CreateDefaultSubobject<UCoopReportComponent>(TEXT("CoopReport"));

	// 동물이 `DT_AnimalTable` 을 생성자에서 무는 것과 같은 방식이다 (C++ 전용 프로젝트라 경로 참조뿐).
	static ConstructorHelpers::FObjectFinder<UDataTable> PlayerVitalsTableAsset(
		TEXT("/Game/Game/Data/Balance/DT_PlayerVitalsTable.DT_PlayerVitalsTable"));
	if (PlayerVitalsTableAsset.Succeeded())
	{
		PlayerVitalsTable = PlayerVitalsTableAsset.Object;
	}

	// 근접 공격 네 숫자도 표에서 온다 (M11-05) — 읽는 것은 위 체력·스태미나와 같은 자리다.
	static ConstructorHelpers::FObjectFinder<UDataTable> PlayerCombatTableAsset(
		TEXT("/Game/Game/Data/Balance/DT_PlayerCombatTable.DT_PlayerCombatTable"));
	if (PlayerCombatTableAsset.Succeeded())
	{
		PlayerCombatTable = PlayerCombatTableAsset.Object;
	}

	// 입력 에셋은 tools/unreal/setup_input_assets.py 가 만든다. C++ 전용 프로젝트라
	// 블루프린트 없이 기본값을 주려면 생성자 경로 참조뿐이다 — 하드 참조라 런타임 문자열 조회가 아니다.
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultContextAsset(
		TEXT("/Game/Game/Input/IMC_Default.IMC_Default"));
	if (DefaultContextAsset.Succeeded())
	{
		DefaultMappingContext = DefaultContextAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> MoveActionAsset(TEXT("/Game/Game/Input/IA_Move.IA_Move"));
	if (MoveActionAsset.Succeeded())
	{
		MoveAction = MoveActionAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> AttackActionAsset(
		TEXT("/Game/Game/Input/IA_Attack.IA_Attack"));
	if (AttackActionAsset.Succeeded())
	{
		AttackAction = AttackActionAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> CaptureActionAsset(
		TEXT("/Game/Game/Input/IA_Capture.IA_Capture"));
	if (CaptureActionAsset.Succeeded())
	{
		CaptureAction = CaptureActionAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> PauseActionAsset(
		TEXT("/Game/Game/Input/IA_Pause.IA_Pause"));
	if (PauseActionAsset.Succeeded())
	{
		PauseAction = PauseActionAsset.Object;
	}

	// M5-01a·M5-01b 가 임포트한 사람 몸과 클립 넷. 입력 에셋과 같은 이유로 생성자 하드 참조다
	// (C++ 전용 프로젝트라 블루프린트 기본값을 줄 자리가 없다 — engines/unreal.md 코드 규칙 4).
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> PlayerBodyAsset(
		TEXT("/Game/Game/Characters/Player/Snare/SK_Player_Body.SK_Player_Body"));
	if (PlayerBodyAsset.Succeeded())
	{
		PlayerBodyMesh = PlayerBodyAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleAnimAsset(
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Idle.A_Player_Idle"));
	if (IdleAnimAsset.Succeeded())
	{
		IdleAnim = IdleAnimAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> WalkAnimAsset(
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Walk.A_Player_Walk"));
	if (WalkAnimAsset.Succeeded())
	{
		WalkAnim = WalkAnimAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> RunAnimAsset(
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Run.A_Player_Run"));
	if (RunAnimAsset.Succeeded())
	{
		RunAnim = RunAnimAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> AttackAnimAsset(
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Attack.A_Player_Attack"));
	if (AttackAnimAsset.Succeeded())
	{
		AttackAnim = AttackAnimAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> HitAnimAsset(
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Hit.A_Player_Hit"));
	if (HitAnimAsset.Succeeded())
	{
		HitAnim = HitAnimAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> DeathAnimAsset(
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Death.A_Player_Death"));
	if (DeathAnimAsset.Succeeded())
	{
		DeathAnim = DeathAnimAsset.Object;
	}

	// 수영·배·낚시 클립 6개 (감독 2026-09-24, DESIGN 8.8). 위와 같은 생성자 하드 참조를 한 줄씩 줄여 쓴다.
	const TPair<TObjectPtr<UAnimSequence>*, const TCHAR*> WaterClips[] = {
		{ &SwimIdleAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_SwimIdle.A_Player_SwimIdle") },
		{ &SwimMoveAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_SwimMove.A_Player_SwimMove") },
		{ &BoatSitAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_BoatSit.A_Player_BoatSit") },
		{ &FishCastAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_FishCast.A_Player_FishCast") },
		{ &FishIdleAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_FishIdle.A_Player_FishIdle") },
		{ &FishReelAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_FishReel.A_Player_FishReel") },
		{ &RideAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_Ride.A_Player_Ride") },   // 탈것 (DESIGN 8.3)
		{ &EatAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_Eat.A_Player_Eat") },       // 아이템 사용 (DESIGN 8.8)
		{ &GatherAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_Gather.A_Player_Gather") },
		{ &ChopAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_Chop.A_Player_Chop") },
		{ &WaveAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_Wave.A_Player_Wave") },       // 이모트 (DESIGN 2.2)
		{ &CheerAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_Cheer.A_Player_Cheer") },
		{ &DanceAnim, TEXT("/Game/Game/Characters/Player/Snare/A_Player_Dance.A_Player_Dance") },
	};
	for (const TPair<TObjectPtr<UAnimSequence>*, const TCHAR*>& Clip : WaterClips)
	{
		ConstructorHelpers::FObjectFinder<UAnimSequence> ClipAsset(Clip.Value);
		*Clip.Key = ClipAsset.Object;
	}

	// 대기 변주 둘 (INBOX 결함 12:40 · DESIGN 3.3) — 레퍼런스 `idleVariation1·2` 자리.
	const TCHAR* const IdleVariantPaths[] = {
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_IdleLook.A_Player_IdleLook"),
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_IdleStretch.A_Player_IdleStretch"),
	};
	for (const TCHAR* const Path : IdleVariantPaths)
	{
		ConstructorHelpers::FObjectFinder<UAnimSequence> ClipAsset(Path);
		if (ClipAsset.Succeeded())
		{
			IdleVariantAnims.Add(ClipAsset.Object);
		}
	}

	// 디자인이 얹힌 WBP 가 있으면 그걸 쓴다. 없으면 아래 CreateHuntHud 가 C++ 클래스로 떨어뜨린다
	// (engines/unreal.md 코드 규칙 4 — 생성자 하드 참조).
	static ConstructorHelpers::FClassFinder<UHuntHudWidget> HuntHudAsset(
		TEXT("/Game/Game/UI/WBP_HUD_Hunt"));
	if (HuntHudAsset.Succeeded())
	{
		HuntHudClass = HuntHudAsset.Class;
	}

	static ConstructorHelpers::FClassFinder<URanchHudWidget> RanchHudAsset(
		TEXT("/Game/Game/UI/WBP_HUD_Ranch"));
	if (RanchHudAsset.Succeeded())
	{
		RanchHudClass = RanchHudAsset.Class;
	}

	static ConstructorHelpers::FClassFinder<UPauseMenuWidget> PauseMenuAsset(
		TEXT("/Game/Game/UI/WBP_PauseMenu"));
	if (PauseMenuAsset.Succeeded())
	{
		PauseMenuClass = PauseMenuAsset.Class;
	}
}

void ASnarehavenCharacter::BeginPlay()
{
	Super::BeginPlay();

	ApplyCameraSettings(CameraDistance, CameraPitchDegrees);
	if (UPlayerNameTagWidget* const Tag = Cast<UPlayerNameTagWidget>(NameTag->GetUserWidgetObject()))
	{
		Tag->SetOwnerPawn(this);
	}
	if (UFootProgressWidget* const FootBar = Cast<UFootProgressWidget>(FootProgress->GetUserWidgetObject()))
	{
		FootBar->SetOwnerPawn(this);
	}

	// 3층 검사용 스위치 (INBOX 지시 ★4). 맵 전역에 흩어진 동물이 한 장에 다 들어오려면 위에서 내려다봐야
	// 하는데, 게임 카메라는 쿼터뷰 고정(DESIGN 3.3)이라 **한 화면에 두세 마리밖에 안 들어온다**.
	// `-SnareTopDown=12000` 이 붙으면 그 거리에서 수직(90도)으로 내려다본다 — 사람이 쓰는 길과 **같은
	// 함수**(`ApplyCameraSettings`)를 탄다. 게임 값(`CameraDistance`·`CameraPitchDegrees`)은 안 건드린다.
	float TopDownDistance = 0.f;
	if (SnareCheckSwitch::Value(TEXT("SnareTopDown="), TopDownDistance) && TopDownDistance > 0.f)
	{
		ApplyCameraSettings(TopDownDistance, 90.f);
		// 검사 거리들은 엔진 기본 화각(90)에 맞춰 정해 둔 값이다 — 게임 화각(좁음)이 걸리면 한 장에 들어오는 땅이 1/3 로 준다.
		TopDownCamera->SetFieldOfView(GetDefault<UCameraComponent>()->FieldOfView);
		TopDownCamera->bOverrideAspectRatioAxisConstraint = false;
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 탑다운 카메라 (-SnareTopDown=%.0f) — 거리 %.0fcm · 각도 90도"),
			TopDownDistance, TopDownDistance);
	}

	// 3층 검사용 스위치 (M17-10). 손에 든 도구가 탑다운 720cm 에서는 폭 30px 이라 붙었는지 안 읽힌다 —
	// `-SnareCameraDistance=250` 이 붙으면 게임 각도·화각 그대로 그 거리까지만 다가간다. 같은 `ApplyCameraSettings`
	// 를 타고 게임 값은 안 건드린다. 탑다운 스위치와 같이 주면 그쪽이 이긴다(위에서 먼저 걸렸으므로 아래가 덮는다).
	// `-SnareCameraPitch=25` 를 같이 주면 각도만 그 값으로 낮춘다 — 55도에서는 머리가 손을 가려 도구가 안 보였다(루프#252).
	float CloseDistance = 0.f;
	if (TopDownDistance <= 0.f && SnareCheckSwitch::Value(TEXT("SnareCameraDistance="), CloseDistance) && CloseDistance > 0.f)
	{
		float ClosePitch = CameraPitchDegrees;
		float PitchOverride = 0.f;
		if (SnareCheckSwitch::Value(TEXT("SnareCameraPitch="), PitchOverride) && PitchOverride > 0.f && PitchOverride <= 90.f)
		{
			ClosePitch = PitchOverride;
		}
		ApplyCameraSettings(CloseDistance, ClosePitch);
		// `-SnareCameraYaw=90` 이면 카메라를 그만큼 옆으로 돌린다 — 뒤에서 보면 머리가 손을 가려 옆모습이 필요했다(M17-15).
		// 각도는 **캐릭터가 보는 쪽 기준**이다(M18-11) — 월드 기준이면 공격하며 몸을 돌릴 때 등 뒤 비스듬히 찍혔다.
		// `-90` 이 오른손(무기 쪽) 정측면이다.
		float YawOverride = 0.f;
		if (SnareCheckSwitch::Value(TEXT("SnareCameraYaw="), YawOverride) && IsValid(CameraBoom))
		{
			CameraBoom->bInheritYaw = true;
			CameraBoom->SetUsingAbsoluteRotation(false);   // 이 검사 각은 몸 기준이라 몸 회전을 받아야 한다
			CameraBoom->SetRelativeRotation(FRotator(-ClosePitch, YawOverride, 0.f));
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 카메라 옆돌림 (-SnareCameraYaw=%.0f)"), YawOverride);
		}
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 근접 카메라 (-SnareCameraDistance=%.0f) — 거리 %.0fcm · 각도 %.0f도"),
			CloseDistance, CloseDistance, ClosePitch);
	}

#if !UE_BUILD_SHIPPING
	// `-SnareRanchSoundAt=2/3/4` — 3층 목장 소리 확인 전용 (M50-07). 적은 초마다 넣기→빼기→칸 늘리기 순으로 서버가
	// 받아들인 답을 목장 동기 컴포넌트의 사람 길 답 처리에 그대로 넣는다. 칸·유지는 다시 받은 값으로만 바뀌므로
	// 오프라인에선 바뀌는 것이 없고 소리 길만 돈다 (CLAUDE.md 8). 쉼표는 `FParse::Value` 가 끊으므로 `/` (M47-06).
	FString RanchSoundAtList;
	if (IsLocallyControlled() && IsValid(RanchSync)
		&& SnareCheckSwitch::Value(TEXT("SnareRanchSoundAt="), RanchSoundAtList))
	{
		static const TCHAR* const RanchFunctions[] = { TEXT("RegisterPet"), TEXT("RemovePet"), TEXT("ExpandRanchSlot") };
		TArray<FString> RanchSeconds;
		RanchSoundAtList.ParseIntoArray(RanchSeconds, TEXT("/"));
		for (int32 Index = 0; Index < RanchSeconds.Num(); ++Index)
		{
			const FString Function = RanchFunctions[Index % UE_ARRAY_COUNT(RanchFunctions)];
			const TWeakObjectPtr<URanchSyncComponent> WeakSync(RanchSync);
			FTSTicker::GetCoreTicker().AddTicker(TEXT("RanchSoundAt"), FCString::Atof(*RanchSeconds[Index]),
				[WeakSync, Function](float)
				{
					if (URanchSyncComponent* const Sync = WeakSync.Get())
					{
						Sync->DebugFeedAcceptedReply(Function);
					}
					return false;
				});
		}
	}

	// `-SnareEmoteSoundAt=2/3/4` — 3층 말풍선 소리 확인 전용 (M54-05). 적은 초마다 번호 0, 1, 2… 말풍선을 사람 길
	// (`ShowEmoteBubble`)로 띄운다. 소리·말풍선만 돌고 재화·인벤토리는 안 건드린다. 쉼표는 `FParse::Value` 가 끊으므로 `/`.
	FString EmoteSoundAtList;
	if (IsLocallyControlled() && SnareCheckSwitch::Value(TEXT("SnareEmoteSoundAt="), EmoteSoundAtList))
	{
		TArray<FString> EmoteSeconds;
		EmoteSoundAtList.ParseIntoArray(EmoteSeconds, TEXT("/"));
		for (int32 Index = 0; Index < EmoteSeconds.Num(); ++Index)
		{
			const TWeakObjectPtr<ASnarehavenCharacter> WeakSelf(this);
			FTSTicker::GetCoreTicker().AddTicker(TEXT("EmoteSoundAt"), FCString::Atof(*EmoteSeconds[Index]),
				[WeakSelf, Index](float)
				{
					if (ASnarehavenCharacter* const Self = WeakSelf.Get())
					{
						Self->ShowEmoteBubble(Index);
					}
					return false;
				});
		}
	}
#endif

	// 3층 검사용 스위치 (INBOX 지시 2026-09-23 11:10·12:40). 새로 반입한 동물 메시·클립은 아직
	// `DT_AnimalTable` 에 행이 없어 게임 안에 한 마리도 안 선다 — 눈으로 보려면 세울 길이 있어야 한다.
	// `-SnareShowAnimal=Fox[,Goat,...]` 가 그 종들을 **내 앞에 한 줄로** 세우고
	// `-SnareShowAnimalClip=<클립>`(기본 Idle)을 재생한다. 게임 데이터(DT·레벨)는 안 건드린다.
	FString ShowAnimals;
	if (IsLocallyControlled()
		// 네 번째 인자 `false` 가 없으면 `FParse::Value` 가 **쉼표에서 값을 끊어** `Fox,Goat,…` 가
		// `Fox` 한 마리로 줄어든다 — 루프#157 은 한 종만 세워 봐서 이걸 못 봤다(루프#158 실측).
		&& SnareCheckSwitch::Value(TEXT("SnareShowAnimal="), ShowAnimals, false)
		&& !ShowAnimals.IsEmpty())
	{
		FString ClipName = TEXT("Idle");
		SnareCheckSwitch::Value(TEXT("SnareShowAnimalClip="), ClipName);

		// 135도면 대개 얼굴이 카메라 쪽 3/4 로 온다(감독 지시 2026-09-23 15:25). 다만 **꿩·공작은
		// 머리가 옆을 보게 리깅돼** 그 각도에서 한쪽 눈만 나온다 — 종마다 다시 맞출 수 있게 뺀다.
		float ShowYaw = 135.f;
		SnareCheckSwitch::Value(TEXT("SnareShowAnimalYaw="), ShowYaw);

		TArray<FString> Species;
		ShowAnimals.ParseIntoArray(Species, TEXT(","));

		// `-SnareShowAnimalCosmetic=item.ranch_ribbon[,…]`(스킨 조각 n) — 그 칸은 메시만 세우지 않고 목장 펫 몸
		// (`ARanchPetActor`)을 세워 칸 순서대로 꾸미기를 씌운다. 목장 칸과 **같은 함수**(`ApplyRow`→`SetCosmetic`)다.
		// 칸 캐시·서버는 안 건드린다 — 씌운 모습만 본다. 행은 `DT_AnimalTable` 의 `Animal_<종>`.
		FString ShowCosmetics;
		TArray<FString> Cosmetics;
		if (SnareCheckSwitch::Value(TEXT("SnareShowAnimalCosmetic="), ShowCosmetics, false))
		{
			ShowCosmetics.ParseIntoArray(Cosmetics, TEXT(","));
		}

		// `-SnareShowAnimalWithPlayer` 면 **내 몸을 숨기지 않고 줄 한 칸에 같이 선다**. 크기를 견주는
		// 캡처는 옆에 사람이 서 있어야 뜻이 있다(INBOX 지시 2026-09-23 17:05 — 곰이 나보다 작은지).
		const bool bStandWithPlayer = SnareCheckSwitch::Param(TEXT("SnareShowAnimalWithPlayer"));
		const int32 Slots = Species.Num() + (bStandWithPlayer ? 1 : 0);   // 같이 설 때는 내 칸도 한 칸
		// **시작점을 벗어나 세운다** — 루프#160 때는 시작점 옆 존 포탈 자리표시 큐브가 900cm 뒤로 물린
		// 카메라와 줄 사이를 가렸다(캡처 3장에서 종·내 몸이 번갈아 가려졌다). M13-39 부터 포탈은 해안
		// 선착장으로 옮겼지만, 1000cm 앞은 그대로 빈 자리라 캡처 자리를 바꾸지 않는다.
		if (bStandWithPlayer)
		{
			SetActorLocation(GetActorLocation() + FVector(1000.f, 0.f, 0.f));
		}

		// 같이 설 때 나는 **제자리에 그대로 서고 동물이 내 왼쪽(-Y)으로 한 줄**이 된다.
		// 가운데 맞춤을 하면 시작점 +Y 쪽 가림물 뒤로 칸이 들어가 한 마리도 안 찍혔다(루프#160 캡처 2장).
		// **줄 순서가 곧 카메라와의 거리**라, 키를 정확히 견줄 종(가장 큰 종)을 앞에 적는다 —
		// 옆으로 200cm 떨어진 첫 칸은 원근 축소가 2.4%, 600cm 인 셋째 칸은 17% 다.
		const bool bRowToTheLeft = bStandWithPlayer;

		// 발바닥이 z=0 인 메시라 캡슐 중심이 아니라 **바닥 높이**에 세운다. 줄은 가운데 맞춤이고
		// 간격 200cm 는 몸 길이 1m(반입 규격)의 두 배라 뿔 달린 종이 와도 안 겹친다.
		// 같이 설 때는 줄을 **내 앞이 아니라 내가 선 깊이**에 둔다 — 100cm 앞에 세우면 원근 때문에
		// 동물이 실제보다 작게 찍혀 키 비교가 거짓이 된다.
		const float FootZ = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
		const FVector RowStart = bRowToTheLeft
			? GetActorLocation() + FVector(0.f, -200.f, -FootZ)            // 내 바로 옆 칸부터
			: GetActorLocation() + FVector(100.f, -100.f * static_cast<float>(Slots - 1), -FootZ);

		// 게임 카메라(쿼터뷰 720cm)로는 몸 길이 1m 짜리가 돼지 뒤에서 점만 하게 나온다 —
		// 원화와 나란히 견주려면 가까이·낮게 봐야 한다. 줄이 길어지면 그만큼 물러선다.
		// 사람이 쓰는 길과 같은 `ApplyCameraSettings` 를 타고 게임 값은 안 건드린다.
		// **한 종일 때 승인된 150cm 를 기준으로 종마다 75cm 씩만 물러선다** — 루프#157 의 `60+90*N`
		// 은 종 수에 비례해 원점부터 물러서서 12종이면 1140cm 까지 갔고 한 마리가 60px 로 줄어
		// 눈이 안 보였다(루프#158 캡처 실측). 계수 75cm 는 실측으로 맞춘 값이다: 285cm(45 계수)에서는
		// 4종 줄 600cm 의 양 끝이 화면 밖으로 잘렸고, 375cm 면 네 마리가 다 들어오면서 눈이 보인다.
		// **종을 더 늘리면 그만큼 작아진다 — 눈까지 보려면 4종씩 나눠 찍는다**(1080p 한계, 루프#158).
		// **같이 설 때는 더 물러선다** — 카메라가 가까우면 줄 양 끝이 가운데보다 멀어져 작게 찍힌다.
		// 반폭 w, 거리 d 일 때 양 끝의 축소율은 d/√(d²+w²) 라, d = 3w 면 5% 안쪽으로 눌린다.
		// 그래야 "곰이 나보다 작다" 를 캡처 픽셀로 견줄 수 있다(INBOX 지시 2026-09-23 17:05).
		const float RowHalfWidthCm = 100.f * static_cast<float>(Slots - 1);
		const float ShowCameraCm = bStandWithPlayer
			? FMath::Max(150.f, 3.f * RowHalfWidthCm)
			: 150.f + 75.f * static_cast<float>(Slots - 1);
		ApplyCameraSettings(ShowCameraCm, 10.f);
		TopDownCamera->SetFieldOfView(GetDefault<UCameraComponent>()->FieldOfView); // 위 탑다운 스위치와 같은 이유
		TopDownCamera->bOverrideAspectRatioAxisConstraint = false;
		// 카메라는 내 뒤에 있어 **내 몸이 줄 한가운데를 가린다** — 혼자 찍을 때만 몸을 숨긴다.
		SetActorHiddenInGame(!bStandWithPlayer);

		for (int32 Index = 0; Index < Species.Num(); ++Index)
		{
			const FString Name = Species[Index].TrimStartAndEnd();
			const FVector ShowSpot = RowStart + FVector(0.f, (bRowToTheLeft ? -200.f : 200.f) * static_cast<float>(Index), 0.f);
			if (Cosmetics.IsValidIndex(Index))
			{
				const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"));
				const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
				const FAnimalRow* const AnimalRow = AnimalTable ? AnimalTable->FindRow<FAnimalRow>(*(TEXT("Animal_") + Name), TEXT("SnareShowAnimalCosmetic")) : nullptr;
				const FName CosmeticID(*Cosmetics[Index].TrimStartAndEnd());
				const FItemRow* const CosmeticRow = SnareItemRows::FindByItemID(ItemTable, CosmeticID);
				ARanchPetActor* const Pet = AnimalRow ? GetWorld()->SpawnActor<ARanchPetActor>(ShowSpot, FRotator(0.f, ShowYaw, 0.f)) : nullptr;
				if (Pet == nullptr || !Pet->ApplyRow(*AnimalRow))
				{
					UE_LOG(LogSnarehaven, Warning, TEXT("검사용 펫 꾸미기 - 펫을 못 세웠다: %s"), *Name);
					continue;
				}
				// `-SnareShowAnimalWander` 면 목장 칸과 같이 그 자리에 묶어 서성이게 둔다 — 걷는 다리 자세를
				// 연속 캡처로 보려는 것(M41-07). 없으면 서성이지 않게 Tick 을 끄고, Tick 이 하던 발 맞춤
				// (메시 밑면을 바닥에)만 한 번 한다.
				if (SnareCheckSwitch::Param(TEXT("SnareShowAnimalWander")))
				{
					Pet->SetTetherHome(ShowSpot);
				}
				else
				{
					Pet->SetActorTickEnabled(false);
					const USkeletalMeshComponent* const PetBody = Pet->FindComponentByClass<USkeletalMeshComponent>();
					const FBoxSphereBounds PetBounds = PetBody->GetSkeletalMeshAsset()->GetBounds();
					Pet->SetActorLocation(ShowSpot - FVector(0.f, 0.f, (PetBounds.Origin.Z - PetBounds.BoxExtent.Z) * Pet->GetActorScale3D().Z));
				}
				Pet->SetCosmetic(CosmeticRow);
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 펫 꾸미기 (-SnareShowAnimalCosmetic=%s) — %s · %s · 씌움 %s · 자리 %s"),
					*ShowCosmetics, *Name, *CosmeticID.ToString(), Pet->GetCosmeticItemID().IsNone() ? TEXT("아니오") : TEXT("예"),
					*Pet->GetActorLocation().ToCompactString());
				continue;
			}
			const FString Folder = FString::Printf(TEXT("/Game/Game/Characters/Animals/%s"), *Name);
			USkeletalMesh* const ShowMesh = LoadObject<USkeletalMesh>(nullptr,
				*FString::Printf(TEXT("%s/SK_Animal_%s.SK_Animal_%s"), *Folder, *Name, *Name));
			if (ShowMesh == nullptr)
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("검사용 동물 세우기 - 메시가 없다: %s"), *Name);
				continue;
			}

			ASkeletalMeshActor* const Shown = GetWorld()->SpawnActor<ASkeletalMeshActor>(
				ShowSpot,
				FRotator(0.f, ShowYaw, 0.f));   // 얼굴이 카메라 쪽 3/4 로 오게 (감독 지시 2026-09-23 15:25 — -45 는 뒤통수였다)
			USkeletalMeshComponent* const ShownBody = Shown ? Shown->GetSkeletalMeshComponent() : nullptr;
			if (ShownBody == nullptr)
			{
				continue;
			}

			// 스폰한 `ASkeletalMeshActor` 는 기본이 Static 이라 그대로는 클립이 안 돈다.
			ShownBody->SetMobility(EComponentMobility::Movable);
			ShownBody->SetSkeletalMesh(ShowMesh);

			UAnimSequence* const ShowClip = LoadObject<UAnimSequence>(nullptr,
				*FString::Printf(TEXT("%s/A_Animal_%s_%s.A_Animal_%s_%s"),
					*Folder, *Name, *ClipName, *Name, *ClipName));
			if (ShowClip != nullptr)
			{
				ShownBody->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				// 죽음처럼 한 번만 도는 클립은 끝 자세로 멈춰 있어야 캡처에 찍힌다 — Idle 만 반복한다.
				ShownBody->PlayAnimation(ShowClip, ClipName == TEXT("Idle"));
			}

			UE_LOG(LogSnarehaven, Log,
				TEXT("검사용 동물 세우기 (-SnareShowAnimal=%s) — %s 메시 %s · 클립 %s · 자리 %s"),
				*ShowAnimals, *Name, *ShowMesh->GetName(),
				ShowClip ? *ShowClip->GetName() : TEXT("없음"),
				*Shown->GetActorLocation().ToCompactString());
		}

		if (bRowToTheLeft)
		{
			// **카메라가 줄에 수직으로 서야** 옆으로 펼친 줄이 한 화면에 들어온다 — 시작 회전을
			// 그대로 두면 줄이 깊이 방향으로 늘어서 앞뒤로 겹친다(루프#160 첫 캡처: 한 마리도 안 찍혔다).
			// 카메라는 컨트롤 회전을 따르므로 몸과 컨트롤러를 같이 +X 로 돌린다.
			SetActorRotation(FRotator::ZeroRotator);
			if (AController* const ShowController = GetController())
			{
				ShowController->SetControlRotation(FRotator::ZeroRotator);
			}
			UE_LOG(LogSnarehaven, Log,
				TEXT("검사용 동물 세우기 — 나도 줄에 선다(-SnareShowAnimalWithPlayer) · %d칸 · 내 자리 %s · 카메라 %.0fcm"),
				Slots, *GetActorLocation().ToCompactString(), ShowCameraCm);
		}
	}

	// 보이는 몸부터 — HUD·백엔드가 실패해도 화면에는 사람이 서 있어야 한다 (동물과 같은 순서).
	ApplyPlayerBody();

	// 멀티(M9-06): 남의 화면에 뜬 복제본은 **장비 목록이 몸보다 먼저 도착한다** — 초기 복제의 RepNotify 는
	// `PostNetInit` 안에서 `BeginPlay` 보다 앞서 돌고, 그때는 위 `ApplyPlayerBody` 전이라 소켓이 달릴
	// 몸 메시가 아직 비어 있다(붙일 자리가 없어 아무것도 안 걸린다). 몸을 올린 직후 한 번 다시 건다.
	// 호스트에서는 이 시점의 목록이 비어 있어 아무 일도 안 한다.
	if (IsValid(Equipment))
	{
		Equipment->RefreshEquippedVisuals();
	}

	// 존을 옮겨 온 폰이면 들고 있던 도구·배낭을 다시 건다 (M15-10). 몸을 올린 뒤라야 소켓이 있다.
	RestoreRememberedGear();

	// 3층 검사용 스위치 (M5-06b). 인벤토리에서 장비를 눌러 드는 길은 M5-06c 라 아직 없고,
	// 헤드리스에는 누를 손가락도 없다 — `-SnareEquip=Item_Club` 이 붙으면 그 행을 바로 든다.
	// 사람이 누를 길이 생기면 그것도 **같은 함수**(`UEquipmentComponent::EquipItem`)를 탄다.
	// 몸을 올린 **뒤**에 둔다 — 소켓은 몸 메시에 달려 있어서 그 전에는 붙일 자리가 없다.
	FString EquipRowName;
	if (IsValid(Equipment) && SnareCheckSwitch::Value(TEXT("SnareEquip="), EquipRowName))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 장비 착용 (-SnareEquip=%s) — 결과 %s"),
			*EquipRowName, Equipment->EquipItem(FName(*EquipRowName)) ? TEXT("들었다") : TEXT("못 들었다"));
	}

#if !UE_BUILD_SHIPPING
	// 3층 검사용 스위치 (M39-07). 헤드리스는 오프라인이라 `LoadAmmo` 답이 없어 탄창이 늘 0 — 쏘는 연속 캡처를 찍으려고
	// `-SnareLoadedRounds=7` 이 붙으면 호스트 탄창만 그 수로 둔다(행 탄창으로 잘림). 가방 탄약·재화는 안 건드린다.
	int32 CaptureRounds = 0;
	if (HasAuthority() && IsValid(MeleeAttack) && SnareCheckSwitch::Value(TEXT("SnareLoadedRounds="), CaptureRounds))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 탄창 (-SnareLoadedRounds=%d) — 결과 %d발"), CaptureRounds, MeleeAttack->SetLoadedRounds(CaptureRounds));
	}

	// 3층 검사용 스위치 (스킨 조각 f). 꾸미기 행은 `EquipItem` 이 거절한다 — `-SnareCosmetic=Item_HarborWhale` 이 붙으면
	// 그 행의 칸에 옷장이 입히는 **같은 함수**(`UEquipmentComponent::SetCosmetic`)로 겉모습 덮개를 씌운다.
	FString CosmeticRowName;
	if (IsValid(Equipment) && SnareCheckSwitch::Value(TEXT("SnareCosmetic="), CosmeticRowName))
	{
		const UDataTable* const Items = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
		const FItemRow* const CosmeticRow = IsValid(Items) ? Items->FindRow<FItemRow>(FName(*CosmeticRowName), TEXT("SnareCosmetic")) : nullptr;
		const bool bWorn = CosmeticRow != nullptr && Equipment->SetCosmetic(CosmeticRow->EquipSlot, FName(*CosmeticRowName));
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 꾸미기 착용 (-SnareCosmetic=%s) — 결과 %s"),
			*CosmeticRowName, bWorn ? TEXT("입었다") : TEXT("못 입었다"));
	}

	// 3층 캡처 `-SnareMates=<1~3>` (스토어 스크린샷 ③ 멀티) — 오프라인 한 프로세스에서 이름표 단 동료 폰을 내 둘레에 세운다.
	// 이름은 방 이름 꼴 「플레이어 N」(알약 색이 번호대로 갈린다), 꾸미기는 칸마다 다르게. 접속·복제는 안 탄다 — 보는 그림일 뿐이다.
	int32 MateCount = 0;
	if (IsLocallyControlled() && SnareCheckSwitch::Value(TEXT("SnareMates="), MateCount) && MateCount > 0 && GetWorld() != nullptr)
	{
		static const FVector2D MateOffsets[] = {FVector2D(60.f, -230.f), FVector2D(-130.f, 230.f), FVector2D(130.f, 170.f)};
		static const TCHAR* const MateCosmetics[] = {TEXT("Item_MeadowFox"), TEXT("Item_HarborWhale"), TEXT("Item_HarborOctopusPack")};
		const AGameModeBase* const Mode = GetWorld()->GetAuthGameMode();
		const TSubclassOf<APlayerState> StateClass = Mode != nullptr && Mode->PlayerStateClass != nullptr
			? Mode->PlayerStateClass : TSubclassOf<APlayerState>(APlayerState::StaticClass());
		if (APlayerState* const MyState = GetPlayerState())
		{
			MyState->SetPlayerName(TEXT("플레이어 1"));
		}
		const UDataTable* const Items = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
		for (int32 Index = 0; Index < FMath::Min(MateCount, static_cast<int32>(UE_ARRAY_COUNT(MateOffsets))); ++Index)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			ASnarehavenCharacter* const Mate = GetWorld()->SpawnActor<ASnarehavenCharacter>(GetClass(),
				GetActorLocation() + FVector(MateOffsets[Index].X, MateOffsets[Index].Y, 0.f), GetActorRotation(), Params);
			APlayerState* const MateState = GetWorld()->SpawnActor<APlayerState>(StateClass);
			if (Mate == nullptr || MateState == nullptr)
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("검사용 동료 - %d번째를 못 세웠다"), Index + 1);
				continue;
			}
			MateState->SetPlayerName(FString::Printf(TEXT("플레이어 %d"), Index + 2));
			MateState->SetPlayerId(Index + 2);
			Mate->SetPlayerState(MateState);
			const FItemRow* const Row = IsValid(Items) ? Items->FindRow<FItemRow>(FName(MateCosmetics[Index]), TEXT("SnareMates")) : nullptr;
			const bool bWorn = Row != nullptr && IsValid(Mate->Equipment) && Mate->Equipment->SetCosmetic(Row->EquipSlot, FName(MateCosmetics[Index]));
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 동료 (-SnareMates=%d) — %s · 꾸미기 %s %s · 자리 %s"), MateCount,
				*MateState->GetPlayerName(), MateCosmetics[Index], bWorn ? TEXT("입었다") : TEXT("못 입었다"), *Mate->GetActorLocation().ToCompactString());
		}
	}

	// 3층 캡처 `-SnareWearAll=<칸 이름>` (입는 것 전수 착용 검사) — 그 칸(Head·OutfitTop …)의 입는 행을 표 순서대로 한 쪽 6개씩
	// 내 앞 한 줄로 동료 폰에 하나씩 입힌다. 쪽은 `-SnareWearAllPage=<n>`(0부터). 입히는 길은 사람이 입는 `EquipItem` 그대로
	// (소켓·크기 맞춤·머리카락 처리 공통 경로). 로그 `검사용 전수 착용 …` 줄로 어느 자리가 어느 행인지 읽는다.
	// `-SnareWearAllYaw=<도>` 는 입힌 동료를 그만큼 돌려 세운다 — 게임 카메라는 쿼터뷰 앞 한 각도라 배낭(등)·챙·귀 옆 관통이 안 보였다(루프#1349).
	// 180 이면 등, 90 이면 옆모습. 카메라를 돌리면 줄이 화면 밖으로 나가므로 몸을 돌린다.
	// `-SnareWearAllViews` 는 한 행을 3명(앞·옆·뒤)이 입고 한 쪽에 2행 — 한 방향 캡처로는 배낭·하의·신발을 판정 못 했다(INBOX 결함 10-20 00:10 ⑥).
	// 줄을 카메라가 보는 내 자리 쪽으로 당겨 화면 가운데에 세운다(앞 260cm 면 모자 윗부분이 화면 위로 잘렸다, 루프#1355).
	// `-SnareWearAllCycle` 은 캡처 한 장이 끝날 때마다 다음 쪽으로 갈아입힌다 — `-SnareCaptureBurst=<남은 쪽 수>` 와 같이 주면
	// 한 번 띄워 그 칸 전부를 `<이름>_00.png`… 로 남긴다(쪽마다 엔진을 새로 띄우면 칸 9개 × 수십 쪽이 걸린다).
	FString WearAllSlot;
	if (IsLocallyControlled() && SnareCheckSwitch::Value(TEXT("SnareWearAll="), WearAllSlot) && GetWorld() != nullptr)
	{
		struct FWearAllRun
		{
			TArray<FName> Rows;
			EEquipSlot Slot = EEquipSlot::Head;
			int32 Page = 0;
			int32 PageCount = 0;
			int32 ItemsPerPage = 6;
			float Yaw = 0.f;
			bool bViews = false;
			TArray<TWeakObjectPtr<AActor>> Spawned;
		};
		const TSharedRef<FWearAllRun> Run = MakeShared<FWearAllRun>();
		SnareCheckSwitch::Value(TEXT("SnareWearAllPage="), Run->Page);
		SnareCheckSwitch::Value(TEXT("SnareWearAllYaw="), Run->Yaw);
		Run->bViews = SnareCheckSwitch::Param(TEXT("SnareWearAllViews"));
		Run->ItemsPerPage = Run->bViews ? 2 : 6;
		const int64 SlotValue = StaticEnum<EEquipSlot>()->GetValueByNameString(WearAllSlot);
		const UDataTable* const Items = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
		if (Items == nullptr || SlotValue == INDEX_NONE)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("검사용 전수 착용 (-SnareWearAll=%s) — 칸 이름이나 표를 못 찾았다"), *WearAllSlot);
		}
		else
		{
			Run->Slot = static_cast<EEquipSlot>(SlotValue);
			for (const FName RowName : Items->GetRowNames())
			{
				const FItemRow* const Row = Items->FindRow<FItemRow>(RowName, TEXT("SnareWearAll"));
				if (Row != nullptr && static_cast<int64>(Row->EquipSlot) == SlotValue)
				{
					Run->Rows.Add(RowName);
				}
			}
			Run->PageCount = (Run->Rows.Num() + Run->ItemsPerPage - 1) / Run->ItemsPerPage;
			const bool bCycle = SnareCheckSwitch::Param(TEXT("SnareWearAllCycle"));
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 전수 착용 (-SnareWearAll=%s) — 이 칸 행 %d개 · 쪽 %d/%d · 돌림 %.0f도 · %s%s"), *WearAllSlot, Run->Rows.Num(),
				Run->Page, Run->PageCount, Run->Yaw, Run->bViews ? TEXT("한 행 앞·옆·뒤 3명") : TEXT("한 행 1명"),
				bCycle ? *FString::Printf(TEXT(" · 쪽 넘김 — 버스트 %d장이면 끝까지"), FMath::Max(Run->PageCount - Run->Page, 0)) : TEXT(""));
			// 내 몸은 숨긴다 — 카메라를 낮추고 당기면(`-SnareCameraDistance=`·`-SnareCameraPitch=`) 줄 앞에 선 내가 가운데 둘을 가렸다(루프#1350).
			SetActorHiddenInGame(true);
			const AGameModeBase* const Mode = GetWorld()->GetAuthGameMode();
			const TSubclassOf<APlayerState> StateClass = Mode != nullptr && Mode->PlayerStateClass != nullptr
				? Mode->PlayerStateClass : TSubclassOf<APlayerState>(APlayerState::StaticClass());
			const TWeakObjectPtr<ASnarehavenCharacter> WeakSelf(this);
			const TFunction<void()> SpawnPage = [WeakSelf, Run, StateClass]()
			{
				ASnarehavenCharacter* const Self = WeakSelf.Get();
				if (Self == nullptr || Self->GetWorld() == nullptr)
				{
					return;
				}
				for (const TWeakObjectPtr<AActor>& Old : Run->Spawned)
				{
					if (Old.IsValid())
					{
						Old->Destroy();
					}
				}
				Run->Spawned.Reset();
				constexpr int32 Columns = 6;
				constexpr int32 ViewsPerItem = 3;
				for (int32 Index = 0; Index < Columns; ++Index)
				{
					const int32 ItemIndex = Run->bViews ? Index / ViewsPerItem : Index;
					const int32 View = Run->bViews ? Index % ViewsPerItem : 0;
					if (ItemIndex >= Run->ItemsPerPage || Run->Page * Run->ItemsPerPage + ItemIndex >= Run->Rows.Num())
					{
						break;
					}
					const FName RowName = Run->Rows[Run->Page * Run->ItemsPerPage + ItemIndex];
					// 한 행 1명: 오른쪽(+Y)으로 열 120cm, 앞(+X, 화면 위쪽) 260cm — 아래쪽은 가림 나무 디더에 먹힌다. 돌림은 내 몸 기준.
					// 앞·옆·뒤: 카메라가 보는 쪽 기준으로 세운다 — 몸 기준 0·90·180 은 쿼터뷰 카메라에 비스듬한 앞·앞·뒤로 찍혔다(루프#1357 시험 캡처).
					// 3명 사이 85cm(배낭 멘 옆모습 폭 남짓), 행 사이 60cm 더 띄워 묶음이 갈리게, 내 자리 앞 40cm 로 당겨 화면 가운데.
					// 오른쪽 HUD 단추 칸이 마지막 사람(뒤)의 배낭을 덮어 줄 가운데를 왼쪽으로 45cm 옮긴다(루프#1357 시험 캡처).
					const float ViewYaw = IsValid(Self->CameraBoom) ? Self->CameraBoom->GetComponentRotation().Yaw : Self->GetActorRotation().Yaw;
					const FRotator ViewFlat(0.f, ViewYaw, 0.f);
					const FVector Offset = Run->bViews
						? ViewFlat.RotateVector(FVector(40.f, Index * 85.f + ItemIndex * 60.f - (5 * 85.f + 60.f) * 0.5f - 45.f, 0.f))
						: FVector(260.f, (Index - 2.5f) * 120.f, 0.f);
					const float MateYaw = Run->bViews ? ViewYaw + 180.f + Run->Yaw + View * 90.f : Self->GetActorRotation().Yaw + Run->Yaw;
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
					ASnarehavenCharacter* const Mate = Self->GetWorld()->SpawnActor<ASnarehavenCharacter>(Self->GetClass(), Self->GetActorLocation() + Offset,
						FRotator(0.f, MateYaw, 0.f), Params);
					APlayerState* const MateState = Self->GetWorld()->SpawnActor<APlayerState>(StateClass);
					if (Mate == nullptr || MateState == nullptr)
					{
						UE_LOG(LogSnarehaven, Warning, TEXT("검사용 전수 착용 - %d번째를 못 세웠다"), Index + 1);
						continue;
					}
					Run->Spawned.Add(Mate);
					Run->Spawned.Add(MateState);
					MateState->SetPlayerName(RowName.ToString());
					MateState->SetPlayerId(Index + 2);
					Mate->SetPlayerState(MateState);
					// 꾸미기 행은 `EquipItem` 이 거절한다 — 옷장이 입히는 `SetCosmetic` 으로 입힌다.
					const bool bWorn = IsValid(Mate->Equipment) && (Mate->Equipment->EquipItem(RowName) || Mate->Equipment->SetCosmetic(Run->Slot, RowName));
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 전수 착용 — 쪽 %d · 자리 0줄 %d열 · %s · %s → %s"), Run->Page, Index,
						Run->bViews ? (View == 0 ? TEXT("앞") : View == 1 ? TEXT("옆") : TEXT("뒤")) : TEXT("한 방향"),
						*RowName.ToString(), bWorn ? TEXT("입었다") : TEXT("못 입었다"));
				}
			};
			// 첫 쪽은 한 박자 뒤 — `-SnareStandAt=` 순간이동이 첫 틱에 걸려 그 전에 세우면 줄만 옛 자리에 남는다.
			FTimerHandle FirstPageTimer;
			GetWorld()->GetTimerManager().SetTimer(FirstPageTimer, FTimerDelegate::CreateLambda(SpawnPage), 0.5f, false);
			if (bCycle)
			{
				FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Run, SpawnPage]()
				{
					if (Run->Page + 1 < Run->PageCount)
					{
						++Run->Page;
						SpawnPage();
					}
				});
			}
		}
	}

	// 3층 캡처 `-SnareStoreShot=<코인>` (스토어 스크린샷) — 오프라인 코인 칩에 「서버 연결 안 됨」 대신 그 숫자를 보인다.
	// 표시용 캐시만 바꾼다(`DevGrantOffline` 과 같은 규칙: 백엔드 스냅샷이 온 실행이면 거절). 자동 입력 띠는 `UHuntHudWidget` 이 같은 스위치로 숨긴다.
	int32 StoreShotCoins = 0;
	if (IsLocallyControlled() && SnareCheckSwitch::Value(TEXT("SnareStoreShot="), StoreShotCoins) && GetGameInstance() != nullptr)
	{
		USnareBackendSubsystem* const Backend = GetGameInstance()->GetSubsystem<USnareBackendSubsystem>();
		if (Backend != nullptr && !Backend->IsSnapshotFromBackend())
		{
			FSnarePlayerSnapshot Next = Backend->GetPlayerSnapshot();
			if (!Next.HasOwner())
			{
				Next.PlayFabId = TEXT("offline.dev");
			}
			Next.CoinBalance = StoreShotCoins;
			Backend->ApplyPlayerSnapshot(Next, /*bFromBackend=*/false);
		}
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 스토어 촬영 (-SnareStoreShot=%d) — 표시 코인 %s"), StoreShotCoins,
			Backend != nullptr && !Backend->IsSnapshotFromBackend() ? TEXT("넣었다") : TEXT("백엔드 스냅샷이라 안 넣었다"));
	}
#endif

	// 3층 검사용 스위치 (M9-01). 왼손 `Socket_Tool` 이 진짜 왼손에 있는지 눈으로 보려면 뭔가 걸어야 하는데
	// **도구 행이 아직 없다**(도구 2종 에셋은 M9-04, 바르코 대기). 그래서 아무 장비 행이나 받아 **도구 자리로
	// 덮어써서** 건다 — 자리표시다. 2층 `Equip.WeaponAndToolTogether` 와 **같은 길**을 탄다.
	// `-SnareEquip=` 과 같이 주면 양손에 하나씩 걸려 두 소켓이 갈렸는지가 한 장에 보인다.
	FString ToolRowName;
	if (IsValid(Equipment) && SnareCheckSwitch::Value(TEXT("SnareEquipTool="), ToolRowName))
	{
		const bool bWorn = Equipment->EquipItem(FName(*ToolRowName), EEquipSlot::Tool, TEXT("Socket_Tool"));
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 도구 착용 (-SnareEquipTool=%s) — 결과 %s"),
			*ToolRowName, bWorn ? TEXT("들었다") : TEXT("못 들었다"));
	}

	// 입력 매핑보다 먼저 — 아래는 IMC 가 없으면 일찍 빠져나가는데, HUD 는 그와 상관없이 떠야 한다.
	CreateHuntHud();
	CreateRanchHud();
	// 상점 판이 열릴 때 배낭도 같이 열어야 해서(M15-18) 화면을 들고 있는다 — 상인 액터가 이 길로 찾는다.
	BackpackScreen = UBackpackScreenWidget::AttachTo(this);
	UWorldMapScreenWidget::AttachTo(this);
	GiftScreen = UGiftScreenWidget::AttachTo(this);

	// 3층 검사용 스위치 (M28-09 옷장 행). 옷장은 배낭 「장비」 탭 안 「상점 열기」 라 상황 버튼 스위치로 못 누른다 —
	// `-SnareWardrobe` 가 붙으면 사람이 누르는 **같은 함수** 둘(`PressGearTab`→`PressWardrobe`)을 차례로 부른다.
	// (`PressWardrobeShop` 은 젬 꺼짐이면 바로 돌아와 화면 「옷장」 단추 길이 아니다 — 1.0 에서 닿을 길이 없어 그대로 둔다.)
	if (IsLocallyControlled() && IsValid(BackpackScreen) && SnareCheckSwitch::Param(TEXT("SnareWardrobe")))
	{
		FTimerHandle WardrobeTimer;
		GetWorldTimerManager().SetTimer(WardrobeTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!IsValid(BackpackScreen))
			{
				return;
			}
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 옷장 열기 (-SnareWardrobe) — 장비 탭 · 「옷장」 을 누른다"));
			BackpackScreen->PressGearTab();
			BackpackScreen->PressWardrobe();
			// 오프라인엔 가진 꾸미기가 없다 — `-SnareWardrobeAll` 이면 사람이 누르는 `전체` 탭을 눌러 격자를 채운다.
			if (SnareCheckSwitch::Param(TEXT("SnareWardrobeAll")) && IsValid(BackpackScreen->GetWardrobe()))
			{
				BackpackScreen->GetWardrobe()->PressAllTab();
			}
		}), CheckWardrobeOpenSeconds, false);
	}

	// 3층 검사용 스위치 (M99-06 옷장 상점 판 실측). 젬이 꺼진 1.0 에서는 사람 길(`PressWardrobeShop`)이 닫혀 있어
	// 상점 판을 볼 길이 없다 — `-SnareWardrobeShop` 이 붙으면 판 열기 함수(`OpenOnViewport`)를 곧바로 부른다.
	if (IsLocallyControlled() && IsValid(BackpackScreen) && SnareCheckSwitch::Param(TEXT("SnareWardrobeShop")))
	{
		FTimerHandle WardrobeShopTimer;
		GetWorldTimerManager().SetTimer(WardrobeShopTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
				? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
			UWardrobeShopWidget* const Shop = UWardrobeShopWidget::OpenOnViewport(Cast<APlayerController>(GetController()), GetWorld(), Backend);
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 옷장 상점 열기 (-SnareWardrobeShop) — 판을 %s"), Shop != nullptr ? TEXT("열었다") : TEXT("못 열었다"));
		}), CheckWardrobeOpenSeconds, false);
	}

#if !UE_BUILD_SHIPPING
	// 3층 검사용 스위치 (M28-09 음식 행). 먹기는 배낭 칸을 고르고 카드 「먹기」 를 누르는 길이라 상황 버튼 스위치로 못 누른다 —
	// `-SnareEat=<ItemID>` 가 붙으면 오프라인 배낭에 그 음식 1개(`Snare.Give` 와 같은 `DevGrantOffline`)를 넣고 판을 연 뒤,
	// 사람이 누르는 **같은 함수** 둘(`SelectSlot`→`PressUse`)을 부른다.
	FString EatItemID;
	if (IsLocallyControlled() && IsValid(BackpackScreen) && SnareCheckSwitch::Value(TEXT("SnareEat="), EatItemID))
	{
		FTimerHandle EatTimer;
		GetWorldTimerManager().SetTimer(EatTimer, FTimerDelegate::CreateWeakLambda(this, [this, EatItemID]()
		{
			USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
				? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
			if (!IsValid(BackpackScreen) || Backend == nullptr)
			{
				return;
			}
			const bool bGiven = Backend->DevGrantOffline(FName(*EatItemID), 1);
			BackpackScreen->SetPanelOpen(true);
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 먹기 (-SnareEat=%s) — 배낭에 %s · 판을 열었다"),
				*EatItemID, bGiven ? TEXT("넣었다") : TEXT("못 넣었다"));
			FTimerHandle PressTimer;
			GetWorldTimerManager().SetTimer(PressTimer, FTimerDelegate::CreateWeakLambda(this, [this, EatItemID]()
			{
				if (!IsValid(BackpackScreen))
				{
					return;
				}
				const int32 Index = BackpackScreen->GetShownSlots().IndexOfByPredicate(
					[&EatItemID](const FInventorySlot& InvSlot) { return InvSlot.ItemID == FName(*EatItemID); });
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 먹기 (-SnareEat=%s) — 칸 %d 고르고 「먹기」 를 누른다"), *EatItemID, Index);
				if (Index != INDEX_NONE)
				{
					BackpackScreen->SelectSlot(Index);
					BackpackScreen->PressUse();
				}
			}), CheckEatPressSeconds, false);
		}), CheckWardrobeOpenSeconds, false);
	}

	// 3층 검사용 스위치 (M28-09g 온라인 수확 행). 유지가 끊긴 계정은 수확이 `locked` 로 거절되니, `-SnareRanchFeed=<초>`
	// 그 초에 목장 판 「나무 넣기」 버튼과 **같은 함수**(`TryFeedUpkeep`)를 한 번 누른다. 넣는 양·유지는 서버 판정(CLAUDE.md 8).
	// 목장 화면 쪽에는 타이머를 두지 않는다(`Snarehaven.Ranch.Slot.NoClientSideTimer`).
	float RanchFeedSeconds = 0.f;
	if (IsLocallyControlled() && IsValid(RanchHud) && SnareCheckSwitch::Value(TEXT("SnareRanchFeed="), RanchFeedSeconds)
		&& RanchFeedSeconds > 0.f)
	{
		FTimerHandle RanchFeedTimer;
		GetWorldTimerManager().SetTimer(RanchFeedTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (IsValid(RanchHud))
			{
				const bool bAsked = RanchHud->TryFeedUpkeep();
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 나무 넣기 (-SnareRanchFeed) — 버튼 「%s」 · 서버에 %s"),
					*RanchHud->GetFeedUpkeepButtonText().ToString(), bAsked ? TEXT("물었다") : TEXT("안 물었다(버튼 꺼짐)"));
			}
		}), RanchFeedSeconds, false);
	}

	// 3층 검사용 스위치 (M28-09g 온라인 제작 행). `-SnareCraftScreen` 으로 연 제작 판에서 `-SnareCraft=<ItemID>` 레시피 줄을
	// 사람이 누르는 **같은 함수**(`UCraftingScreenWidget::PressRecipe`)로 한 번 누른다. 재료·결과물은 서버 `CraftItem` 판정(CLAUDE.md 8).
	FString CraftItemID;
	if (IsLocallyControlled() && SnareCheckSwitch::Value(TEXT("SnareCraft="), CraftItemID))
	{
		FTimerHandle CraftTimer;
		GetWorldTimerManager().SetTimer(CraftTimer, FTimerDelegate::CreateWeakLambda(this, [this, CraftItemID]()
		{
			const USnareBaseBuildComponent* const Build = FindComponentByClass<USnareBaseBuildComponent>();
			UCraftingScreenWidget* const CraftScreen = Build != nullptr ? Build->GetCraftingScreen() : nullptr;
			const bool bAsked = IsValid(CraftScreen) && CraftScreen->PressRecipe(FName(*CraftItemID));
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 제작 (-SnareCraft=%s) — 판 %s · 서버에 %s"), *CraftItemID,
				IsValid(CraftScreen) ? TEXT("있음") : TEXT("없음(-SnareCraftScreen 같이)"), bAsked ? TEXT("물었다") : TEXT("안 물었다"));
		}), CheckCraftPressSeconds, false);
	}

	// 3층 검사용 스위치 (총 11·탄 4 캡처). `-SnareBagGuns` 가 붙으면 오프라인 배낭에 총 11종 1개씩·탄 4종 20발씩을
	// 미리보기로 넣고(`DevGrantOffline` — 백엔드 스냅샷이 있으면 거절돼 서버 배낭은 안 바뀐다) 판을 열어 칸 아이콘을 한 장에 본다.
	if (IsLocallyControlled() && IsValid(BackpackScreen) && SnareCheckSwitch::Param(TEXT("SnareBagGuns")))
	{
		FTimerHandle BagGunsTimer;
		GetWorldTimerManager().SetTimer(BagGunsTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
				? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
			if (!IsValid(BackpackScreen) || Backend == nullptr)
			{
				return;
			}
			static const TCHAR* const GunIds[] = { TEXT("item.pistol_semi"), TEXT("item.pistol_auto"), TEXT("item.rifle_modern"),
				TEXT("item.rifle_burst"), TEXT("item.rifle_semi"), TEXT("item.shotgun"), TEXT("item.shotgun_auto"),
				TEXT("item.shotgun_short"), TEXT("item.smg"), TEXT("item.mg"), TEXT("item.crossbow") };
			static const TCHAR* const AmmoIds[] = { TEXT("item.ammo"), TEXT("item.ammo_rifle"), TEXT("item.ammo_shotgun"), TEXT("item.arrow") };
			int32 Given = 0;
			for (const TCHAR* GunId : GunIds)
			{
				Given += Backend->DevGrantOffline(FName(GunId), 1) ? 1 : 0;
			}
			for (const TCHAR* AmmoId : AmmoIds)
			{
				Given += Backend->DevGrantOffline(FName(AmmoId), 20) ? 1 : 0;
			}
			BackpackScreen->SetPanelOpen(true);
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 배낭 총·탄 (-SnareBagGuns) — 15종 중 %d종 넣고 판을 열었다"), Given);
		}), CheckEatPressSeconds, false);
	}

	// 3층 검사용 스위치 (쏘는 캡처의 탄 칸). `-SnareGrantAmmo=<발수>` 는 오프라인 배낭에 탄 4종을 그 수만큼 미리보기로 넣는다
	// (`DevGrantOffline` — 백엔드 스냅샷이 있으면 거절돼 서버 배낭은 안 바뀐다). 탄 칸이 「0/0」 빈 그림이 되지 않게 한다.
	int32 GrantAmmoCount = 0;
	if (IsLocallyControlled() && SnareCheckSwitch::Value(TEXT("SnareGrantAmmo="), GrantAmmoCount) && GrantAmmoCount > 0)
	{
		FTimerHandle GrantAmmoTimer;
		GetWorldTimerManager().SetTimer(GrantAmmoTimer, FTimerDelegate::CreateWeakLambda(this, [this, GrantAmmoCount]()
		{
			USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
				? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
			if (Backend == nullptr)
			{
				return;
			}
			static const TCHAR* const AmmoIds[] = { TEXT("item.ammo"), TEXT("item.ammo_rifle"), TEXT("item.ammo_shotgun"), TEXT("item.arrow") };
			int32 Given = 0;
			for (const TCHAR* AmmoId : AmmoIds)
			{
				Given += Backend->DevGrantOffline(FName(AmmoId), GrantAmmoCount) ? 1 : 0;
			}
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 탄 (-SnareGrantAmmo=%d) — 4종 중 %d종 넣었다"), GrantAmmoCount, Given);
		}), 0.5f, false);
	}

	// 3층 검사용 스위치 (M28-09 설치물 행). `-SnarePlaceTent` 는 놓기를 바로 시작해 배낭 카드를 건너뛴다 —
	// `-SnarePlace=<ItemID>` 는 오프라인 배낭에 그 물건 1개를 넣고 사람이 누르는 같은 함수(`SelectSlot`→`PressPlace`)로
	// 놓기를 시작한 뒤, 놓기 바 「놓기」 버튼이 부르는 같은 `ConfirmPlacement` 를 부른다. 세우기·차감은 그대로 서버다.
	FString PlaceItemID;
	if (IsLocallyControlled() && IsValid(BackpackScreen) && SnareCheckSwitch::Value(TEXT("SnarePlace="), PlaceItemID))
	{
		FTimerHandle PlaceTimer;
		GetWorldTimerManager().SetTimer(PlaceTimer, FTimerDelegate::CreateWeakLambda(this, [this, PlaceItemID]()
		{
			USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
				? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
			if (!IsValid(BackpackScreen) || Backend == nullptr)
			{
				return;
			}
			const bool bGiven = Backend->DevGrantOffline(FName(*PlaceItemID), 1);
			BackpackScreen->SetPanelOpen(true);
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 놓기 (-SnarePlace=%s) — 배낭에 %s · 판을 열었다"),
				*PlaceItemID, bGiven ? TEXT("넣었다") : TEXT("못 넣었다"));
			FTimerHandle PressTimer;
			GetWorldTimerManager().SetTimer(PressTimer, FTimerDelegate::CreateWeakLambda(this, [this, PlaceItemID]()
			{
				USnareBaseBuildComponent* const Build = FindComponentByClass<USnareBaseBuildComponent>();
				if (!IsValid(BackpackScreen) || !IsValid(Build))
				{
					return;
				}
				const int32 Index = BackpackScreen->GetShownSlots().IndexOfByPredicate(
					[&PlaceItemID](const FInventorySlot& InvSlot) { return InvSlot.ItemID == FName(*PlaceItemID); });
				if (Index == INDEX_NONE)
				{
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 놓기 (-SnarePlace=%s) — 배낭 칸에 없다"), *PlaceItemID);
					return;
				}
				BackpackScreen->SelectSlot(Index);
				BackpackScreen->PressPlace();
				const bool bPlacing = Build->IsPlacing();
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 놓기 (-SnarePlace=%s) — 칸 %d 「놓기」 · 놓기 시작 %s · 판 열림 %d · 막는 까닭 「%s」"),
					*PlaceItemID, Index, bPlacing ? TEXT("예") : TEXT("아니오"), BackpackScreen->IsPanelOpen() ? 1 : 0,
					*Build->GetPlaceBlockReason(FName(*PlaceItemID)).ToString());
				if (!bPlacing)
				{
					return;
				}
				FTimerHandle ConfirmTimer;
				GetWorldTimerManager().SetTimer(ConfirmTimer, FTimerDelegate::CreateWeakLambda(this, [this, PlaceItemID]()
				{
					USnareBaseBuildComponent* const ConfirmBuild = FindComponentByClass<USnareBaseBuildComponent>();
					if (!IsValid(ConfirmBuild) || !ConfirmBuild->IsPlacing())
					{
						return;
					}
					const ESnarePlacementVerdict Verdict = ConfirmBuild->GetPlacementVerdict();
					const bool bSent = ConfirmBuild->ConfirmPlacement(FString());
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 놓기 (-SnarePlace=%s) — 놓기 바 「놓기」 · 판정 %s · 서버 요청 %s · 놓는 중 %s"),
						*PlaceItemID, *UEnum::GetValueAsString(Verdict), bSent ? TEXT("보냄") : TEXT("안 보냄"),
						ConfirmBuild->IsPlacing() ? TEXT("예") : TEXT("아니오"));
				}), CheckEatPressSeconds, false);
			}), CheckEatPressSeconds, false);
		}), CheckWardrobeOpenSeconds, false);
	}
#endif

	// 3층 검사용 (`-SnareCapture=`, M9-06). 캡처를 거는 자리는 원래 게임모드 하나뿐인데 **게임모드는
	// 호스트에만 산다** — 게스트 프로세스에서는 영영 안 찍힌다. 손님 화면도 한 장 찍어야 "남의 화면에도
	// 보인다"를 확인할 수 있어서, 클라이언트에서는 내 폰이 뜨는 이 시점에 같은 함수를 부른다.
	// 내가 조종하는 폰 하나로 제한한다 — 남의 복제본까지 부르면 한 프로세스에서 여러 번 걸린다.
	if (GetNetMode() == NM_Client && IsLocallyControlled())
	{
		ASnarehavenGameMode::ArmCaptureIfRequested(GetWorld());
	}

	if (IsLocallyControlled())
	{
		ASnarehavenGameMode::HideEngineDebugText();
	}

	// 3층 검사용 스위치 (M7-04). 사람 입구는 배낭 장비 탭 「올가미 기계」 — 둘 다 `OpenOnViewport` 한 길로 띄운다.
	if (SnareCheckSwitch::Param(TEXT("SnareGachaScreen")))
	{
		UGachaScreenWidget* const GachaScreen = UGachaScreenWidget::OpenOnViewport(
			Cast<APlayerController>(GetController()), nullptr, nullptr);
		if (IsValid(GachaScreen))
		{
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 뽑기 화면 열기 (-SnareGachaScreen)"));
		}
		else
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("뽑기 화면 위젯을 만들지 못했다"));
		}
	}

	// 3층 검사용 스위치 (M94 ④ 젬 숨김 캡처). 상자 화면의 사람 입구는 배낭의 상자 아이템 「사용」이라 손이 필요하다 —
	// `-SnareCrateScreen` 은 같은 `OpenFor` 한 길로 가을 상자 화면을 띄운다(재화·열쇠는 건드리지 않는다).
	if (IsLocallyControlled() && SnareCheckSwitch::Param(TEXT("SnareCrateScreen")))
	{
		APlayerController* const CrateOwner = Cast<APlayerController>(GetController());
		USnareBackendSubsystem* const CrateBackend = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
		const bool bCrateOpened = UCrateScreenWidget::OpenFor(CrateOwner, CrateBackend, Backpack, FName(TEXT("item.crate_autumn26"))) != nullptr;
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 상자 화면 열기 (-SnareCrateScreen) — %s"), bCrateOpened ? TEXT("열었다") : TEXT("못 열었다"));
	}

	// 3층 검사용 스위치 (M10-05). 사람 입구는 배낭 도감 탭(M13-29) — 둘 다 `OpenOnViewport` 한 길로 띄운다.
	// `-SnareAchievementScreen` 은 도감을 연 뒤 그 판의 「업적」 버튼과 같은 길로 업적 판을 띄운다(M38-06 캡처 —
	// 사람 입구는 도감 제목 줄 버튼뿐이라 손으로 누르지 않고 찍을 길이 없었다).
	const bool bOpenAchievementScreen = SnareCheckSwitch::Param(TEXT("SnareAchievementScreen"));
	if (SnareCheckSwitch::Param(TEXT("SnareCodexScreen")) || bOpenAchievementScreen)
	{
		// 도감 값은 백엔드 스냅샷에서만 온다 — 붙이지 않으면 진행률이 `-` 로 남는다 (CLAUDE.md 8).
		UGameInstance* const CodexGameInstance = GetGameInstance();
		UCodexScreenWidget* const CodexScreen = UCodexScreenWidget::OpenOnViewport(Cast<APlayerController>(GetController()),
			/*World=*/nullptr, CodexGameInstance != nullptr ? CodexGameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr);
		if (IsValid(CodexScreen))
		{
			// 3층 캡처용 자리표시 (`-SnareCodexDemo=<칸 수>`). 백엔드는 M2-03 전까지 늘 빈 도감을
			// 주므로 이게 없으면 **채운 칸 강조가 캡처에 한 번도 안 나온다**(ROADMAP M10-05 합격선).
			// 화면 글자·색만 바뀐다 — 스냅샷은 그대로라 M10-06 탑승 판정은 이 길로 안 열린다.
			int32 CodexDemoCount = 0;
			if (SnareCheckSwitch::Value(TEXT("SnareCodexDemo="), CodexDemoCount))
			{
				CodexScreen->SetDemoFilledCount(CodexDemoCount);
			}
			// `-SnareCodexTab=<번호>` — 처음 보일 탭(0 낚시 … 5 변종). 변종 탭 캡처(M21-07c1)가 손으로 누르지 않게.
			int32 CodexTab = 0;
			if (SnareCheckSwitch::Value(TEXT("SnareCodexTab="), CodexTab)
				&& CodexTab >= 0 && CodexTab < static_cast<int32>(ECodexCategory::Count))
			{
				CodexScreen->SetSelectedCategory(static_cast<ECodexCategory>(CodexTab));
			}
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 도감 화면 열기 (-SnareCodexScreen)"));
			if (bOpenAchievementScreen)
			{
				const bool bAchievementOpened = IsValid(CodexScreen->OpenAchievementScreen());
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 업적 화면 열기 (-SnareAchievementScreen) — 결과 %s"),
					bAchievementOpened ? TEXT("열었다") : TEXT("못 열었다"));
			}
		}
		else
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("도감 화면 위젯을 만들지 못했다"));
		}
	}

	// 3층 검사용 스위치 (M38-06). 존 이동 버튼은 포탈 사거리 안에서만 뜨는데 포탈은 해안 부두 끝이라
	// 시작점에서는 늘 숨어 있다 — `-SnareNearPortal` 이면 가장 가까운 갈 수 있는 포탈의 시작점 쪽 1m 로 옮긴다.
	// 1m 는 화면을 찍기 위한 검사 전용 거리다(설계 숫자 아님 — 사거리는 근접 공격 컴포넌트 값 그대로).
	if (SnareCheckSwitch::Param(TEXT("SnareNearPortal")) && GetWorld() != nullptr)
	{
		const FVector StartLocation = GetActorLocation();
		const AZonePortalActor* NearestPortal = nullptr;
		for (TActorIterator<AZonePortalActor> It(GetWorld()); It; ++It)
		{
			if (IsValid(*It) && It->CanTravel() && (NearestPortal == nullptr
				|| FVector::DistSquared2D(It->GetActorLocation(), StartLocation)
					< FVector::DistSquared2D(NearestPortal->GetActorLocation(), StartLocation)))
			{
				NearestPortal = *It;
			}
		}
		if (NearestPortal != nullptr)
		{
			const FVector PortalLocation = NearestPortal->GetActorLocation();
			const FVector TowardStart = (StartLocation - PortalLocation).GetSafeNormal2D();
			FVector Destination = PortalLocation + TowardStart * 100.f;
			Destination.Z = PortalLocation.Z + GetSimpleCollisionHalfHeight();
			const bool bMoved = TeleportTo(Destination, GetActorRotation());
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 포탈 옆 이동 (-SnareNearPortal) — 포탈 %s · 위치 %s · 결과 %s"),
				*NearestPortal->GetName(), *GetActorLocation().ToCompactString(), bMoved ? TEXT("옮겼다") : TEXT("못 옮겼다"));
		}
		else
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("검사용 포탈 옆 이동 (-SnareNearPortal) — 갈 수 있는 포탈이 없다"));
		}
	}

	// 사망 처리 (M11-03). 생존 값은 복제되지 않아 **각자 자기 폰에서** 죽는다 — 남의 복제본은
	// 이동 입력을 안 받아 스태미나가 안 닳으니 영영 이 알림이 오지 않는다.
	if (IsValid(PlayerVitals))
	{
		PlayerVitals->OnDied.AddDynamic(this, &ASnarehavenCharacter::HandlePlayerDied);
	}

	// 인벤토리 칸의 원본은 백엔드다 (DESIGN 4.5). 폰이 새로 생기면 지금 값을 한 번 받아 오고,
	// 그 뒤로는 백엔드 캐시가 바뀔 때마다 듣는다 (engines/unreal.md 코드 규칙 5 — EndPlay 에서 해제).
	if (USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr)
	{
		Backend->OnPlayerSnapshotChanged.AddDynamic(this, &ASnarehavenCharacter::HandleBackendSnapshotChanged);

		// 시작 장비(M9-07b)는 **백엔드가 줬다고 말한 뒤**에만 손에 걸린다 — 첫 접속인지를 폰이 재지
		// 않는다 (DESIGN 3.5 「지급 횟수」 · CLAUDE.md 8).
		Backend->OnStarterKitGranted.AddDynamic(this, &ASnarehavenCharacter::HandleStarterKitGranted);
		Backend->OnDisplayNameChanged.AddDynamic(this, &ASnarehavenCharacter::HandleDisplayNameChanged);
		PullInventoryFromBackend();
		ReportNicknameToServer();
	}

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(PlayerController) || !IsValid(DefaultMappingContext))
	{
		return;
	}

	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
	{
		InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
		UE_LOG(LogSnarehaven, Log, TEXT("IMC_Default 적용"));
	}

	// 매핑을 걸고 나서 — 눌린 채 넘어온 키가 첫 `IA_Attack` 을 만들기 전에 끊는다 (M1-10e).
	FlushLevelEntryInput();
}

void ASnarehavenCharacter::ApplyHudHidden(UUserWidget* Widget, bool bHide, FHudHideSlot& Slot)
{
	if (!IsValid(Widget))
	{
		Slot.bHidden = false;
		return;
	}
	if (bHide && !Slot.bHidden)
	{
		Slot.Saved = Widget->GetVisibility();
		Widget->SetVisibility(ESlateVisibility::Collapsed);
		Slot.bHidden = true;
	}
	else if (!bHide && Slot.bHidden)
	{
		Widget->SetVisibility(Slot.Saved);
		Slot.bHidden = false;
	}
}

void ASnarehavenCharacter::UpdateHudForOpenScreens(float DeltaSeconds)
{
	if (!IsLocallyControlled())
	{
		return;
	}
	// 0.1초마다 — 열린 창 목록 훑기를 매 프레임 하지 않는다.
	HudHideWait -= DeltaSeconds;
	if (HudHideWait > 0.f)
	{
		return;
	}
	HudHideWait = 0.1f;
	const bool bOpen = FSnareScreenCloser::HasOpenScreen(GetWorld());
	ApplyHudHidden(HuntHud, bOpen, HuntHudHide);
	ApplyHudHidden(RanchHud, bOpen, RanchHudHide);
	ApplyHudHidden(NextGoalHud, bOpen, NextGoalHide);
	ApplyHudHidden(StoryGoalHud, bOpen, StoryGoalHide);
	// 상인·의뢰인 머리 옆 「열기」 카드는 상점·의뢰 위젯 안에 있어 뿌리를 못 접는다 — 카드만 접는다.
	TArray<UUserWidget*> Prompts;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), Prompts, UUserWidget::StaticClass(), /*TopLevelOnly=*/false);
	for (UUserWidget* const Prompt : Prompts)
	{
		if (UShopScreenWidget* const Shop = Cast<UShopScreenWidget>(Prompt))
		{
			Shop->SetPromptSuppressed(bOpen);
		}
		else if (UQuestScreenWidget* const Quest = Cast<UQuestScreenWidget>(Prompt))
		{
			Quest->SetPromptSuppressed(bOpen);
		}
	}
}

void ASnarehavenCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 검사용 추격은 사람 손가락과 같은 자리에서 — 입력을 먼저 넣어야 이번 프레임 속도·스태미나에 반영된다.
	if (bCheckChasing)
	{
		TickCheckChase();
	}
	// `-SnareMoveScale=` 도 같은 자리에서 매 프레임 — 0.01초 타이머는 120fps 에서 프레임을 건너뛰어 입력 없는 프레임이 생겼다(루프#1306).
	if (CheckMoveAxisScale > 0.f)
	{
		MoveByAxis(FVector2D(0.f, -CheckMoveAxisScale));
	}

	// 속도를 먼저 맞추고 클립을 고른다 — 클립의 재생 속도가 이번 프레임 실제 속도를 나눠 쓰기 때문이다.
	UpdateMoveSpeed();
	if (IsValid(PlayerVitals) && IsValid(Equipment))
	{
		const FItemRow* const Hat = Equipment->GetEquippedRow(EEquipSlot::Head);
		PlayerVitals->SetEquipmentHealthRegen(Hat ? Hat->HealthRegenPerSecond : 0.f); // 13.5 딸기 모자
		const FItemRow* const Top = Equipment->GetEquippedRow(EEquipSlot::OutfitTop);
		PlayerVitals->SetEquipmentSwimStaminaMul(Top ? Top->SwimStaminaMul : 0.f); // 13.8 구명조끼
	}
	if (IsValid(FaceBlink) && IsValid(PlayerVitals))
	{
		// 표정 (M85-03) — 체력·쓰러짐은 읽기만 한다. 쓰러짐 판정은 `bDowned` 또는 죽음.
		FaceBlink->TickExpression(DeltaSeconds, PlayerVitals->GetHealth(), PlayerVitals->GetMaxHealth(),
			IsDowned() || PlayerVitals->IsDead());
	}
	UpdateHudForOpenScreens(DeltaSeconds);
	TickGaspSound(DeltaSeconds);
	TickShiverSound(DeltaSeconds);
	if (IsLocallyControlled())
	{
		TickHeartbeatSound(DeltaSeconds);   // 내 기기만 (M60-04)
	}
	TickWarmthClock(DeltaSeconds);
	UpdateToolCamera(DeltaSeconds);
	UpdateBodyAnimation();
	UpdateIdleVariant(DeltaSeconds);
	UpdateBodyPlacement(DeltaSeconds);
	HitReactSecondsLeft = FMath::Max(HitReactSecondsLeft - DeltaSeconds, 0.f);
	if (!bItemActionLoop)
	{
		ItemActionSecondsLeft = FMath::Max(ItemActionSecondsLeft - DeltaSeconds, 0.f);
	}
	FishCastSecondsLeft = FMath::Max(FishCastSecondsLeft - DeltaSeconds, 0.f);
	HeldUseTailSecondsLeft = FMath::Max(HeldUseTailSecondsLeft - DeltaSeconds, 0.f);
	FishReelBoostSecondsLeft = FMath::Max(FishReelBoostSecondsLeft - DeltaSeconds, 0.f);

	// 손을 뗐으면 스태미나 소모를 여기서 끈다 — 뗌은 입력 액션으로 오지 않아(Triggered 가 그냥 멈춘다)
	// "이번 프레임에 `MoveByAxis` 가 안 불렸다" 가 유일한 신호다 (M11-02).
	if (IsValid(PlayerVitals) && !bMoveInputThisFrame)
	{
		PlayerVitals->SetMoveInput(false);
		PlayerVitals->SetRunInput(false);
	}
	bMoveInputThisFrame = false;

	// 발소리 — 땅 위에서 보폭만큼 걸을 때마다. 물속은 `UWaterTravelComponent` 가 헤엄 소리를 낸다.
	const float GroundSpeed = static_cast<float>(GetVelocity().Size2D());
	const bool bInWater = IsValid(WaterTravel) && WaterTravel->IsInWater();
	if (GetCharacterMovement()->IsMovingOnGround() && !bInWater && GroundSpeed > 50.f)
	{
		StepDistanceCm += GroundSpeed * DeltaSeconds;
		if (StepDistanceCm >= SnareSound::StrideCm(GroundSpeed))
		{
			StepDistanceCm = 0.f;
			const FZoneRow* const Region = SnareZones::FindRegionAt(this, GetActorLocation());
			// 발밑 액터가 부두(포탈)나 배치 구조물이면 나무 바닥.
			const AActor* const FloorActor = GetCharacterMovement()->CurrentFloor.HitResult.GetActor();
			const bool bOnWood = FloorActor && (FloorActor->IsA<AZonePortalActor>() || FloorActor->IsA<ASnarePlacedStructureActor>());
			float Rain = 0.f;
			float Snow = 0.f;
			if (const USnareWeatherSubsystem* const Weather = GetWorld()->GetSubsystem<USnareWeatherSubsystem>())
			{
				Weather->GetAmounts(Rain, Snow);
			}
			const FName StepCue = SnareSound::FootstepCue(Region ? Region->ZoneID : NAME_None, bOnWood);
			USnareSoundSubsystem::PlayAt(this, StepCue, GetActorLocation(), 0.35f * SnareSound::WeatherFootstepScale(StepCue, Rain));
		}
	}

	if (bCheckMoveTracing)
	{
		LogMoveTrace(DeltaSeconds);
	}
}

void ASnarehavenCharacter::LogMoveTrace(float DeltaSeconds)
{
	// 클립 시각은 되감김을 잡는 유일한 증거다 — 앞으로만 가야 하고, 줄어드는 곳은 루프 끝이거나 되감김이다.
	float ClipTime = -1.f;
	if (const UPlayerBodyAnimInstance* const BodyAnim = GetBodyAnimInstance())
	{
		ClipTime = BodyAnim->GetShownClipTime();
	}

	// 사람이 본 "멈칫" 은 **몸 기준 발이 멈추는 것**이다. 캐릭터가 나아가는 속도는 `CharacterMovement` 가
	// 만들어 늘 매끈하니 거기엔 안 보이고, 재생되는 클립 안에서만 보인다 — 그래서 몸(액터) 기준
	// 발 위치가 한 프레임에 얼마나 흘렀는지를 같이 찍는다 (결함 ②, 루프#139).
	float FootFlow = -1.f;
	if (const USkeletalMeshComponent* const Body = GetMesh(); IsValid(Body) && DeltaSeconds > 0.f)
	{
		const FVector FootLocal = Body->GetSocketLocation(CheckTraceFootBone) - GetActorLocation();
		if (!LastCheckFootLocal.IsZero())
		{
			FootFlow = static_cast<float>((FootLocal - LastCheckFootLocal).Size()) / DeltaSeconds;
		}

		LastCheckFootLocal = FootLocal;
	}

	UE_LOG(LogSnarehaven, Log, TEXT("이동 추적 — 속도 %.1f · 재생 속도 %.2f · 클립 %s 시각 %.3f · 입력 크기 %.2f · 발 흐름 %.1f"),
		GetVelocity().Size2D(), CurrentBodyPlayRate,
		IsValid(CurrentBodyAnim) ? *CurrentBodyAnim->GetName() : TEXT("없음"), ClipTime, LastMoveInputSize, FootFlow);
}

bool ASnarehavenCharacter::ApplyPlayerBody()
{
	USkeletalMeshComponent* const Body = GetMesh();
	if (!IsValid(Body) || !IsValid(PlayerBodyMesh))
	{
		// 에셋이 없으면 자리표시 실린더 그대로 — 동물(`ApplyBodyFromRow`)과 같은 규칙이다.
		return false;
	}

	Body->SetSkeletalMesh(PlayerBodyMesh);

	// 몸을 판정 캡슐에 맞춘다. 숫자를 새로 만들지 않고 캡슐 치수와 메시 바운드에서 계산한다
	// (DataDriven.md 3) — 카메라 합격폭(M0-04)이 캡슐 기준이라 몸이 캡슐보다 크면 화면이 달라진다.
	if (const UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		const float CapsuleHeight = Capsule->GetUnscaledCapsuleHalfHeight() * 2.f;
		const float MeshHeight = PlayerBodyMesh->GetBounds().BoxExtent.Z * 2.f;
		if (MeshHeight > KINDA_SMALL_NUMBER)
		{
			// 클립에서 잰 속도는 **클립 공간** 값이라 이 배율을 곱해야 화면 속도가 된다 —
			// 안 곱하면 몸만 커지고 걸음은 그대로라 그 배율만큼 발이 미끄러진다 (루프#85).
			BodyMeshScale = CapsuleHeight / MeshHeight;
			Body->SetRelativeScale3D(FVector(BodyMeshScale));
		}

		// 임포트한 몸은 원점이 발밑이라 캡슐 중심에 그대로 붙이면 반쯤 떠 있다. 바닥으로 내린다.
		// 임포트한 몸은 +Y 를 보고 서 있는데 액터의 앞은 +X 다 — 안 돌리면 옆으로 걷는다.
		// UE 마네킹·돼지(`AAnimalCharacter::ApplyBodyFromRow`)가 쓰는 것과 같은 보정이다.
		BodyPlacement = EBodyPlacement::Ground;
		SetBodyPlacement(FVector(0.f, 0.f, -Capsule->GetUnscaledCapsuleHalfHeight()), FRotator(0.f, -90.f, 0.f));
	}

	// 이미 타 있으면(`-SnareMountNow`·복제가 몸보다 먼저 옴) 위 줄이 몸을 바닥으로 되돌렸다 — 탄 모습을 새 몸 크기로 다시 맞춘다.
	if (IsValid(Mount) && Mount->IsMounted())
	{
		Mount->ApplyMountBody();
	}

	// 애님 블루프린트 에셋은 안 쓴다 — 빈 AnimGraph 짜리 ABP 를 붙이면 레퍼런스 포즈로 굳는다
	// (AnimalCharacter.cpp 의 같은 판단, 루프#46). 대신 코드 애님 인스턴스가 클립을 섞는다 (M18-10) —
	// SingleNode 는 클립 하나만 틀어서 전환이 툭 끊기고, 걸으며 휘두르면 다리가 멈춘 채 미끄러졌다.
	// 클립 고르기는 그대로 `UpdateBodyAnimation` 이 한다.
	EnsureBodyAnimInstance();
	UpdateBodyAnimation();

	// 진짜 몸이 올라왔으니 자리표시를 내린다. 지우지 않고 숨기기만 — 에셋이 없는 빌드에선 이게 몸이다.
	if (IsValid(PlaceholderBody))
	{
		PlaceholderBody->SetVisibility(false);
	}

	return true;
}

void ASnarehavenCharacter::UpdateBodyAnimation()
{
	if (!IsValid(GetMesh()) || GetMesh()->GetSkeletalMeshAsset() == nullptr)
	{
		return;
	}

	// 문턱 하나로 Idle/걷기를 가르면 가속·감속·방향 전환 때 속도가 그 하나를 여러 번 넘나들고,
	// 그때마다 `PlayBodyAnimation` 이 클립을 프레임 0 으로 되감아 걸음이 멈칫한다 (감독 재현
	// 2026-09-22 14:10). 들어가는 문턱과 나오는 문턱을 벌려 그 사이에서는 지금 상태를 그대로 둔다.
	// 행동 중에도 따진다 — 행동 아래 하체가 걷기인지 대기인지가 이 값으로 갈린다 (M18-10).
	const float GroundSpeed = GetVelocity().Size2D();
	if (GroundSpeed >= MoveAnimEnterSpeedCmPerSecond)
	{
		bBodyMoving = true;
	}
	else if (GroundSpeed <= MoveAnimExitSpeedCmPerSecond)
	{
		bBodyMoving = false;
	}

	// 쓰러짐이 **모든 것보다 세다** (M5-01e2 · DESIGN 3.1 「쓰러짐 → 부활」) — 쓰러지는 동안에는
	// 입력을 안 받으니 걷기로 갈릴 일이 없지만, 죽는 순간 휘두르던 중이면 공격이 위에서 덮는다.
	// 되풀이 없이 한 번만 틀고(마지막 프레임 = 누운 자세로 멈춘다) 재생 속도는 손대지 않는다 —
	// 이 클립은 공격처럼 맞출 박자가 없다.
	if (bDowned && IsValid(DeathAnim))
	{
		PlayBodyAnimation(DeathAnim, false, 1.f);
		return;
	}

	// 동물에게 맞은 순간 한 번 (M5-07d2 · DESIGN 3.1). 쓰러짐 다음으로 세다 — 휘두르던 중에 맞아도
	// 맞은 것이 보여야 한다. 입력은 막지 않으므로 클립이 끝나면 걷기·공격이 그대로 이어진다.
	if (HitReactSecondsLeft > 0.f && IsValid(HitAnim))
	{
		PlayBodyAction(HitAnim, false, 1.f, bHitReactRestart);
		bHitReactRestart = false;
		return;
	}

	// 전기톱: 누르는 동안 내민 채 버티고, 떼면 든 자세로 돌아온다 — 아이템 동작·휘두르기 시간이 끝나도 버튼이 이긴다.
	if (UpdateHeldUseHold())
	{
		return;
	}

	// 아이템 사용 (DESIGN 8.8): 먹기·줍기·베기 한 번. 레퍼런스처럼 걷기 시작하면 바로 끊는다 — 입력을 막지 않는다.
	// 물속·탈것 위에서는 선 자세 클립이 어색하니 틀지 않는다.
	if (ItemActionSecondsLeft > 0.f && IsValid(ItemActionAnim))
	{
		// 되풀이 이모트(춤·앉기)는 때리기·낚시가 시작되면 끊는다 — 안 끊으면 휘두르는 동안 춤만 보였다(감독 조작 조사 #6).
		const bool bBusy = GetVelocity().Size2D() >= MoveAnimEnterSpeedCmPerSecond
			|| (IsValid(WaterTravel) && WaterTravel->IsInWater()) || (IsValid(Mount) && Mount->IsMounted())
			|| (bItemActionLoop && ((IsValid(MeleeAttack) && MeleeAttack->IsSwinging())
				|| (IsValid(TapFishing) && TapFishing->GetShownState() != ETapFishingState::Idle)));
		if (!bBusy)
		{
			PlayBodyAction(ItemActionAnim, bItemActionLoop, ItemActionPlayRate, bItemActionRestart);
			bItemActionRestart = false;
			return;
		}
		ItemActionSecondsLeft = 0.f;
		bItemActionLoop = false;
	}

	// 때리는 동안이 가장 세다 — 걸으면서 때려도 공격이 보여야 한다 (공격 판정은 컴포넌트가 한다).
	// 보는 값은 "버튼이 눌려 있나"(`IsAttacking`)가 아니라 **"휘두르기가 도는 중인가"**(`IsSwinging`)다 —
	// 전자로 보던 때는 손을 떼는 프레임에 대기로 갈아 끼워져 동작이 몇 프레임 만에 툭 끊겼다
	// (사용자 결함 2026-09-23 16:40 ①). 한 바퀴 길이는 컴포넌트가 공격 간격으로 잰다 (DESIGN 3.1).
	// 손에 든 것의 든 자세·쓰기 클립 (M18-11). 비면 옛 공용 클립(AttackAnim·대기)으로 간다.
	UAnimSequence* HeldHold = nullptr;
	UAnimSequence* HeldUse = nullptr;
	UAnimSequence* HeldUseAlt = nullptr;
	ResolveHeldBodyClips(HeldHold, HeldUse, HeldUseAlt);

	// 재장전 (M39-06b2): 서버가 받아들인 순간부터 행 `ReloadSeconds` 한 바퀴 동안 탄창 넣는 클립 — 그동안 쏘기는 막혀 반동과 겹치지 않는다.
	if (IsValid(MeleeAttack) && MeleeAttack->IsReloading())
	{
		const FItemRow* const GunRow = GetSwingHandRow();
		UAnimSequence* const ReloadClip = GunRow != nullptr ? GunRow->ReloadAnim.LoadSynchronous() : nullptr;
		if (IsValid(ReloadClip) && GunRow->ReloadSeconds > KINDA_SMALL_NUMBER)
		{
			const int32 ReloadCount = MeleeAttack->GetReloadMotionCount();
			const bool bNewReload = ReloadCount != LastBodyReloadCount;
			LastBodyReloadCount = ReloadCount;
			PlayBodyAction(ReloadClip, false, ReloadClip->GetPlayLength() / GunRow->ReloadSeconds, bNewReload);
			return;
		}
	}

	const int32 SwingCount = IsValid(MeleeAttack) ? MeleeAttack->GetSwingCount() : 0;
	// 맨손은 잽·크로스를 번갈아 — 짝수 번째 휘두르기가 크로스다 (레퍼런스 맨손 원투).
	UAnimSequence* const SwingAnim = (HeldUseAlt != nullptr && SwingCount % 2 == 0) ? HeldUseAlt
		: (HeldUse != nullptr ? HeldUse : ToRawPtr(AttackAnim));
	if (IsValid(MeleeAttack) && MeleeAttack->IsSwinging() && IsValid(SwingAnim))
	{
		// 한 바퀴가 공격 간격과 같아야 **타격 한 번에 휘두르기 한 번**이 보인다. 재생 속도 1.0 으로
		// 두면 클립이 제 길이대로 돌아 타격과 어긋나고, 그게 사람이 말한 "휘두르기가 늘어진다" 다
		// (실플레이 결함 ③ 2026-09-22 · DESIGN 3.3). 길이는 박지 않고 클립에서 읽는다 — 클립을
		// 갈아 끼워도 따라온다 (DataDriven.md 3).
		// 「정해진 길이」 행(전기톱·고급/전기 곡괭이)은 한 바퀴가 곧 클립 길이라 제 속도 1 로 끝까지 튼다.
		const float Interval = MeleeAttack->GetActionLengthSeconds();
		const float ClipLength = SwingAnim->GetPlayLength();
		const float AttackPlayRate = (Interval > KINDA_SMALL_NUMBER && ClipLength > KINDA_SMALL_NUMBER)
			? ClipLength / Interval
			: 1.f;
		// 휘두르기가 새로 시작됐으면 클립을 처음으로 되감는다 — 되풀이 재생에 기대면 반복 타이머와
		// 클립 위치가 조금씩 어긋나 타격 순간과 휘두르는 그림이 갈린다 (결함 16:40 ②).
		const bool bNewSwing = SwingCount != LastBodySwingCount;
		LastBodySwingCount = SwingCount;

		PlayBodyAction(SwingAnim, true, AttackPlayRate, bNewSwing);
		return;
	}

	// 낚시 (DESIGN 8.13): 던지기 한 번 → 입질 대기 → 입질 뒤 감기 → 끝나면(쉼) 아래 대기로 돌아간다.
	// 상태는 `GetShownState` 로 본다 — 낚시 판정은 주인 기계에서만 돌아서, 남의 화면은 복제된 이 값뿐이다.
	const ETapFishingState FishState = IsValid(TapFishing) ? TapFishing->GetShownState() : ETapFishingState::Idle;
	const bool bFishing = FishState != ETapFishingState::Idle;
	if (bFishing && !bWasFishing && IsValid(FishCastAnim))
	{
		FishCastSecondsLeft = FishCastAnim->GetPlayLength();
	}
	bWasFishing = bFishing;
	if (bFishing && IsValid(FishIdleAnim) && IsValid(FishReelAnim))
	{
		if (FishState == ETapFishingState::Tapping || FishState == ETapFishingState::Catching)
		{
			// 누른 순간 잠깐 빨리 감는다 — 보이는 누름 수(주인은 자기 연타, 남의 화면은 복제 값)가 바뀔 때마다.
			// `>` 가 아니라 `!=` — 복제 값은 uint8 이라 255 다음에 0 으로 돈다.
			const int32 PressCount = TapFishing->GetShownPressCount();
			if (PressCount != LastFishPressCount)
			{
				FishReelBoostSecondsLeft = FishReelTapBoostSeconds;
			}
			LastFishPressCount = PressCount;
			PlayBodyAnimation(FishReelAnim, true, FishReelBoostSecondsLeft > 0.f ? FishReelTapPlayRate : 1.f);
			return;
		}
		LastFishPressCount = 0;
		if (FishCastSecondsLeft > 0.f && IsValid(FishCastAnim))
		{
			PlayBodyAnimation(FishCastAnim, false, 1.f);
			return;
		}
		PlayBodyAnimation(FishIdleAnim, true, 1.f);
		return;
	}

	float BasePlayRate = 1.f;
	UAnimSequence* const Base = SelectBaseBodyClip(BasePlayRate);
	// 든 자세 (M18-11): 땅 위 대기·걷기·달리기에서만 얹는다 — 물·탈것·배 위 자세를 주먹 자세가 덮으면 안 된다.
	// 서 있으면 몸 전체, 걸으면 상체만 받아 하체는 이동 클립 그대로다(`PlayBodyAction` 규칙).
	if (IsValid(HeldHold) && Base != nullptr && (Base == IdleAnim || Base == WalkAnim || Base == RunAnim))
	{
		if (UPlayerBodyAnimInstance* const BodyAnim = EnsureBodyAnimInstance())
		{
			BodyAnim->SetBaseClip(Base, true, BasePlayRate, false);
			BodyAnim->SetActionClip(HeldHold, true, 1.f, false, Base != IdleAnim);
			// 보이는 클립 값은 이동 상태로 둔다 — 든 자세는 이동 위의 덧칠이고, 대기·걷기 판정 시험이 이 값을 읽는다.
			NoteShownBodyClip(Base, BasePlayRate);
		}
		return;
	}
	// 가끔 나오는 대기 변주 (DESIGN 3.3) — 맨손으로 가만히 섰을 때만. 고르는 건 `UpdateIdleVariant`, 여기선 틀기만 한다.
	// 움직이거나 행동이 끼면 위 갈래가 먼저 돌아 변주가 바로 끊기고, 다음 틱에 `UpdateIdleVariant` 가 접는다.
	if (Base == IdleAnim && IsValid(IdleVariantAnim) && IdleVariantSecondsLeft > 0.f)
	{
		PlayBodyAnimation(IdleVariantAnim, false, 1.f);
		return;
	}
	PlayBodyAnimation(Base, true, BasePlayRate);
}

void ASnarehavenCharacter::UpdateIdleVariant(float DeltaSeconds)
{
	// 도는 중이면 끝날 때까지 센다. 끝나면 다음 `UpdateBodyAnimation` 이 대기로 섞어 돌아간다(첫 장·끝 장이 대기 첫 장).
	if (IsValid(IdleVariantAnim) && CurrentBodyAnim == IdleVariantAnim)
	{
		IdleVariantSecondsLeft = FMath::Max(IdleVariantSecondsLeft - DeltaSeconds, 0.f);
		if (IdleVariantSecondsLeft <= 0.f)
		{
			IdleVariantAnim = nullptr;
			IdleStillSeconds = 0.f;
		}
		return;
	}
	// 대기 말고 다른 것이 보이거나(걷기·행동·물·탈것), 골랐는데 대기가 그대로면(든 자세가 덮음) 처음부터 다시 센다.
	if (CurrentBodyAnim != IdleAnim || IsValid(IdleVariantAnim))
	{
		IdleVariantAnim = nullptr;
		IdleVariantSecondsLeft = 0.f;
		IdleStillSeconds = 0.f;
		return;
	}
	if (IdleStillSeconds <= 0.f)
	{
		NextIdleVariantSeconds = FMath::FRandRange(IdleVariantDelayMinSeconds, IdleVariantDelayMaxSeconds);
	}
	IdleStillSeconds += DeltaSeconds;
	if (IdleStillSeconds < NextIdleVariantSeconds)
	{
		return;
	}

	TArray<UAnimSequence*, TInlineAllocator<2>> Valid;
	for (UAnimSequence* const Anim : IdleVariantAnims)
	{
		if (IsValid(Anim))
		{
			Valid.Add(Anim);
		}
	}
	IdleStillSeconds = 0.f;
	if (Valid.IsEmpty())
	{
		return;
	}
	IdleVariantAnim = Valid[FMath::RandRange(0, Valid.Num() - 1)];
	IdleVariantSecondsLeft = IdleVariantAnim->GetPlayLength();
	UE_LOG(LogSnarehaven, Log, TEXT("[IdleVariant] %s (%.2f초)"), *IdleVariantAnim->GetName(), IdleVariantSecondsLeft);
}

void ASnarehavenCharacter::SetIdleVariantDelay(float MinSeconds, float MaxSeconds)
{
	IdleVariantDelayMinSeconds = MinSeconds;
	IdleVariantDelayMaxSeconds = FMath::Max(MinSeconds, MaxSeconds);
	IdleStillSeconds = 0.f;
}

namespace
{
	/** 든 도구가 휘두르는 손을 차지하나 — 쓰기 클립이 있고, 무기가 없거나 무기와 같은 손(소켓)일 때. */
	bool ToolTakesHand(const FItemRow* ToolRow, const FItemRow* WeaponRow)
	{
		return ToolRow != nullptr && !ToolRow->UseAnim.IsNull()
			&& (WeaponRow == nullptr || ToolRow->EquipSocketName == WeaponRow->EquipSocketName);
	}
}

void ASnarehavenCharacter::ResolveHeldBodyClips(UAnimSequence*& OutHold, UAnimSequence*& OutUse, UAnimSequence*& OutUseAlt) const
{
	OutHold = OutUse = OutUseAlt = nullptr;
	const FItemRow* const ToolRow = IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Tool) : nullptr;
	const FItemRow* const WeaponRow = IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Weapon) : nullptr;
	// 휘두르는 도구(쓰기 클립이 있는 행)는 무기 손에 들려 무기를 가린다 — 그래서 도구 행이 먼저다. 무기 손이 아닌
	// 도구(왼손 Socket_Tool — 바구니·망원경·낚싯대)는 무기가 그대로 보이므로, 무기를 함께 들었으면 휘두르기는 무기 행이다.
	// 무기가 없을 때만 그 도구의 쓰기 클립(바구니 줍기·망원경 보기)을 튼다 (감독 2026-10-05).
	const bool bToolSwings = ToolTakesHand(ToolRow, WeaponRow);
	const FItemRow* const HandRow = bToolSwings ? ToolRow : WeaponRow;
	// 든 자세만 있는 도구(낚싯대 — 왼손 허리 옆, 레퍼런스 관찰표 19행)는 서 있는 몸을 그 도구 자세로 덮는다.
	// 쓰기 클립까지 있는 왼손 도구(바구니·망원경 — 든 자세 = 대기)는 무기를 함께 들면 무기 든 자세에 양보한다.
	UAnimSequence* const ToolHold = (!bToolSwings && ToolRow != nullptr && ToolRow->UseAnim.IsNull())
		? ToolRow->HoldPoseAnim.LoadSynchronous() : nullptr;
	if (HandRow != nullptr)
	{
		OutHold = ToolHold != nullptr ? ToolHold : HandRow->HoldPoseAnim.LoadSynchronous();
		OutUse = HandRow->UseAnim.LoadSynchronous();
		return;
	}
	if (ToolHold != nullptr)
	{
		OutHold = ToolHold;
		return;
	}
	// 클립이 빈 도구를 들었으면 주먹 자세가 아니다 — 옛 대기 그대로.
	if (ToolRow != nullptr || !IsValid(PlayerCombatTable))
	{
		return;
	}
	if (const FPlayerCombatRow* const Bare = PlayerCombatTable->FindRow<FPlayerCombatRow>(
			PlayerCombatRowName, TEXT("ASnarehavenCharacter::ResolveHeldBodyClips"), false))
	{
		OutHold = Bare->HoldPoseAnim.LoadSynchronous();
		OutUse = Bare->UseAnim.LoadSynchronous();
		OutUseAlt = Bare->UseAnimAlt.LoadSynchronous();
	}
}

const FItemRow* ASnarehavenCharacter::GetSwingHandRow() const
{
	// 든 행 고르기는 `ResolveHeldBodyClips` 와 같은 순서다 — 휘두르는 도구가 무기 손을 가린다.
	const FItemRow* const ToolRow = IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Tool) : nullptr;
	const FItemRow* const WeaponRow = IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Weapon) : nullptr;
	return ToolTakesHand(ToolRow, WeaponRow) ? ToolRow : WeaponRow;
}

bool ASnarehavenCharacter::UpdateHeldUseHold()
{
	UPlayerBodyAnimInstance* const BodyAnim = GetBodyAnimInstance();
	const FItemRow* const HandRow = GetSwingHandRow();
	UAnimSequence* const Use = (HandRow != nullptr && HandRow->UseHoldEndSeconds > HandRow->UseHoldStartSeconds)
		? HandRow->UseAnim.LoadSynchronous() : nullptr;
	const bool bShowingUse = BodyAnim != nullptr && IsValid(Use) && BodyAnim->GetActionClip() == Use;
	// 아이템 동작과 같이 물·탈것 위에선 선 자세 클립을 붙들지 않는다.
	const bool bBlocked = (IsValid(WaterTravel) && WaterTravel->IsInWater()) || (IsValid(Mount) && Mount->IsMounted());

	if (bShowingUse && BodyAnim->IsActionHolding())
	{
		if (bUseInputHeld && !bBlocked)
		{
			PlayBodyAction(Use, false, 1.f, false);
			// 버티는 동안 모터 도구가 세게 떤다 — 벨 때와 같은 떨림(M18-06)을 돌아오기 끝까지 이어 둔다.
			if (IsValid(Equipment))
			{
				Equipment->SetToolWorkSeconds(Use->GetPlayLength() - HandRow->UseHoldEndSeconds);
			}
			return true;
		}
		// 한 번도 감기 전에 뗐으면(짧게 누름) 클립 위치·아이템 동작·휘두르기 시간이 그대로 맞으니 아래 원래 갈래가 끝까지 튼다.
		if (BodyAnim->ReleaseActionHold())
		{
			// 감은 뒤면 돌아오기(구간 끝 → 끝 장 = 든 자세)를 이 갈래가 끝까지 튼다 — 아이템 동작·휘두르기 시간은 이미 지났다.
			HeldUseTailSecondsLeft = Use->GetPlayLength() - HandRow->UseHoldEndSeconds;
			ItemActionSecondsLeft = 0.f;
			UE_LOG(LogSnarehaven, Log, TEXT("[전기톱] 손을 뗌 — 버팀 끝, 든 자세로 %.2f초 돌아온다"), HeldUseTailSecondsLeft);
		}
	}

	// 돌아오는 중에 새로 누르면(새 휘두르기·새 도구질) 그쪽이 처음부터 튼다.
	const bool bNewUse = bItemActionRestart || (IsValid(MeleeAttack) && MeleeAttack->GetSwingCount() != LastBodySwingCount);
	if (HeldUseTailSecondsLeft > 0.f)
	{
		if (bShowingUse && !bBlocked && !bNewUse)
		{
			PlayBodyAction(Use, false, 1.f, false);
			return true;
		}
		HeldUseTailSecondsLeft = 0.f;
	}

	// 쓰기 클립이 내밀기·버팀 구간을 도는 중에 버튼이 눌려 있으면 구간을 건다 — 이번 틱 클립은 아래 원래 갈래가 정한다.
	const bool bUsing = ItemActionSecondsLeft > 0.f || (IsValid(MeleeAttack) && MeleeAttack->IsSwinging());
	if (bShowingUse && bUseInputHeld && !bBlocked && bUsing && BodyAnim->GetShownClipTime() < HandRow->UseHoldEndSeconds)
	{
		BodyAnim->SetActionHold(HandRow->UseHoldStartSeconds, HandRow->UseHoldEndSeconds);
		UE_LOG(LogSnarehaven, Log, TEXT("[전기톱] 누르는 동안 버팀 — %s %.2f~%.2f초 되풀이"), *Use->GetName(),
			HandRow->UseHoldStartSeconds, HandRow->UseHoldEndSeconds);
	}
	return false;
}

float ASnarehavenCharacter::GetHeldFixedUseSeconds() const
{
	const FItemRow* const HandRow = GetSwingHandRow();
	if (HandRow == nullptr || !HandRow->bUseAnimFixedLength)
	{
		return 0.f;
	}
	const UAnimSequence* const Use = HandRow->UseAnim.LoadSynchronous();
	return IsValid(Use) ? Use->GetPlayLength() : 0.f;
}

UAnimSequence* ASnarehavenCharacter::SelectBaseBodyClip(float& OutPlayRate) const
{
	OutPlayRate = 1.f;

	// 배 위 (DESIGN 8.9): 운전석·승객석 모두 앉기. 자리(좌석·흔들림)는 `UpdateBodyPlacement` 가 맞춘다.
	// 누가 어느 배에 탔는지는 배의 복제된 좌석 목록이라 남의 화면도 같은 클립을 고른다.
	if (IsValid(WaterTravel) && WaterTravel->GetRiddenBoat() != nullptr && IsValid(BoatSitAnim))
	{
		return BoatSitAnim;
	}

	// 물속 (DESIGN 8.9): 걷기와 같은 두 문턱으로 제자리 헤엄/자유형을 가른다. 물속 판정은 기계마다 위치로 다시 해서
	// 남의 화면도 같다. 자유형 클립은 제자리라 발이 미끄러질 일이 없어 재생 속도는 1배다.
	if (IsValid(WaterTravel) && WaterTravel->IsInWater() && IsValid(SwimIdleAnim) && IsValid(SwimMoveAnim))
	{
		return bBodyMoving ? SwimMoveAnim : SwimIdleAnim;
	}

	// 탈것 위 (DESIGN 8.3): 서 있든 달리든 걸터앉은 한 클립. 걸음은 탈것 몸(`UMountComponent`)이 맡는다. `bMounted` 는 복제라 남의 화면도 같다.
	if (IsValid(Mount) && Mount->IsMounted() && IsValid(RideAnim))
	{
		return RideAnim;
	}

	if (!bBodyMoving)
	{
		return IdleAnim;
	}

	// 달리기 버튼은 없다 — 조이스틱을 얼마나 밀었나로 가른다 (감독 결정 0.9, DESIGN 3.3).
	// 스태미나가 바닥이면 꽉 밀어도 걷기다 — 그 판단은 `IsRunningNow()` 한 곳이 한다 (DESIGN 3.1).
	const bool bRunning = IsRunningNow() && IsValid(RunAnim);
	UAnimSequence* const Wanted = bRunning ? ToRawPtr(RunAnim) : ToRawPtr(WalkAnim);

	// 발이 안 미끄러지려면 클립이 밟는 속도와 실제 속도가 같아야 한다. 살짝 민 조이스틱은 기준보다
	// 느리게 나아가므로 그 비율만큼 클립도 느리게 돌린다 (사용자 지적 2026-09-21 · DESIGN 3.3).
	const float ClipSpeed = (bRunning ? RunClipSpeedCmPerSecond : WalkClipSpeedCmPerSecond) * BodyMeshScale;
	OutPlayRate = ClipSpeed > KINDA_SMALL_NUMBER ? GetVelocity().Size2D() / ClipSpeed : 1.f;
	return Wanted;
}

void ASnarehavenCharacter::UpdateBodyPlacement(float DeltaSeconds)
{
	USkeletalMeshComponent* const Body = GetMesh();
	const UCapsuleComponent* const Capsule = GetCapsuleComponent();
	// 탈것 위 자리는 `UMountComponent::ApplyMountBody` 몫이다 (물에 들면 탈것은 저절로 내린다).
	if (!IsValid(Body) || Body->GetSkeletalMeshAsset() == nullptr || !IsValid(Capsule) || !IsValid(WaterTravel)
		|| (IsValid(Mount) && Mount->IsMounted()))
	{
		return;
	}
	const FRotator BodyYaw(0.f, -90.f, 0.f);
	const ASeaActor* const Sea = WaterTravel->GetSea();
	const FTransform& ActorTransform = GetActorTransform();

	// 배: 좌석은 Hull 을 따라 기계마다 출렁이고 기운다 — 몸도 그 자리·기울기를 그대로 탄다 (섞지 않는다).
	if (const ABoatActor* const Boat = WaterTravel->GetRiddenBoat())
	{
		const int32 SeatIndex = Boat->FindSeat(this);
		const FTransform Seat = Boat->GetSeatWorldTransform(SeatIndex);
		const FQuat BodyQuat = ActorTransform.GetRotation().Inverse() * Seat.GetRotation() * BodyYaw.Quaternion();
		SetBodyPlacement(ActorTransform.InverseTransformPositionNoScale(Seat.GetLocation()), BodyQuat.Rotator());
		BodyWorldZ = static_cast<float>(Seat.GetLocation().Z);
		if (BodyPlacement != EBodyPlacement::Boat)
		{
			BodyPlacement = EBodyPlacement::Boat;
			const float SurfaceZ = Sea != nullptr ? Sea->GetSurfaceZ() : 0.f;
			UE_LOG(LogSnarehaven, Log, TEXT("[Boat] %s 앉은 자리 — 좌석 %d · 몸 원점 Z %.1f · 수면 Z %.1f · 수면 위 %.1fcm"),
				*GetName(), SeatIndex, BodyWorldZ, SurfaceZ, BodyWorldZ - SurfaceZ);
		}
		return;
	}

	// 땅에 붙어 있으면 `ApplyPlayerBody` 가 놓은 자리 그대로 — 매 틱 건드리지 않는다.
	const bool bSwimming = WaterTravel->IsInWater() && Sea != nullptr;
	const float ActorZ = static_cast<float>(ActorTransform.GetLocation().Z);
	const float GroundZ = -Capsule->GetUnscaledCapsuleHalfHeight();
	if (!bSwimming && BodyPlacement == EBodyPlacement::Ground)
	{
		return;
	}
	if (BodyPlacement == EBodyPlacement::Ground)
	{
		BodyWorldZ = ActorZ + GroundZ;
	}
	BodyPlacement = EBodyPlacement::Swim;

	// 물: 수면에서 클립별 깊이(클립 공간 × 몸 배율) 아래에 몸 원점을 둔다. 나오면 캡슐 바닥으로 섞어 돌아간다.
	const float SinkCm = (CurrentBodyAnim == SwimMoveAnim ? SwimMoveSinkCm : SwimIdleSinkCm) * BodyMeshScale;
	const float TargetWorldZ = bSwimming ? Sea->GetSurfaceZ() - SinkCm : ActorZ + GroundZ;
	BodyWorldZ = FMath::FInterpTo(BodyWorldZ, TargetWorldZ, DeltaSeconds, BodyPlacementBlendSpeed);
	const bool bSettled = FMath::Abs(BodyWorldZ - TargetWorldZ) < 0.5f;
	if (bSettled)
	{
		BodyWorldZ = TargetWorldZ;
	}
	SetBodyPlacement(FVector(0.f, 0.f, BodyWorldZ - ActorZ), BodyYaw);

	if (!bSwimming)
	{
		PlacementLoggedFor = nullptr;
		if (bSettled)
		{
			BodyPlacement = EBodyPlacement::Ground;
		}
		return;
	}
	if (bSettled && PlacementLoggedFor != CurrentBodyAnim)
	{
		PlacementLoggedFor = CurrentBodyAnim;
		UE_LOG(LogSnarehaven, Log, TEXT("[Swim] %s 몸 높이 — 클립 %s · 수면 Z %.1f · 몸 원점 Z %.1f · 수면 아래 %.1fcm (클립 %.0f × 몸 배율 %.3f)"),
			*GetName(), IsValid(CurrentBodyAnim) ? *CurrentBodyAnim->GetName() : TEXT("없음"), Sea->GetSurfaceZ(), BodyWorldZ,
			Sea->GetSurfaceZ() - BodyWorldZ, CurrentBodyAnim == SwimMoveAnim ? SwimMoveSinkCm : SwimIdleSinkCm, BodyMeshScale);
	}
}

void ASnarehavenCharacter::SetBodyPlacement(const FVector& RelativeLocation, const FRotator& RelativeRotation)
{
	if (USkeletalMeshComponent* const Body = GetMesh())
	{
		Body->SetRelativeLocationAndRotation(RelativeLocation, RelativeRotation);
	}
	CacheInitialMeshOffset(RelativeLocation, RelativeRotation);
}

void ASnarehavenCharacter::UpdateMoveSpeed()
{
	UCharacterMovementComponent* const Movement = GetCharacterMovement();
	if (!IsValid(Movement))
	{
		return;
	}

	// 엔진 기본 600 을 쓰면 클립이 밟는 속도와 안 맞아 발이 미끄러진다. 숫자는 지어내지 않고
	// 클립에서 잰 값을 쓴다 (`tools/unreal/measure_player_anim_speed.py` · DataDriven.md 3).
	// 물속·운전 중·탈것 위엔 달리기가 없다 — 걷기 × 표 배율 (8.9 · 8.3). 물이 탈것보다 먼저다.
	const float WaterMul = IsValid(WaterTravel) ? WaterTravel->GetMoveSpeedMultiplier() : 1.f;
	const float MountMul = IsValid(Mount) ? Mount->GetMoveSpeedMultiplier() : 1.f;
	const float RideMul = !FMath::IsNearlyEqual(WaterMul, 1.f) ? WaterMul : MountMul;
	// 13.5 달리기 모자 — 발로 달릴 때만 곱한다(걷기·물·탈것엔 없다).
	const FItemRow* const Hat = IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Head) : nullptr;
	const float HatRunMul = Hat && Hat->RunSpeedMul > 0.f ? Hat->RunSpeedMul : 1.f;
	// 13.6 이동 버프(에너지 드링크) — 발로 걷고 달릴 때만(물·탈것엔 없다).
	const float BuffMul = 1.f + (IsValid(PlayerVitals) ? PlayerVitals->GetBuffValue(EBuffKind::Speed) : 0.f);
	Movement->MaxWalkSpeed = !FMath::IsNearlyEqual(RideMul, 1.f)
		? WalkClipSpeedCmPerSecond * BodyMeshScale * RideMul
		: (IsRunningNow() ? RunClipSpeedCmPerSecond * HatRunMul : WalkClipSpeedCmPerSecond) * BodyMeshScale * BuffMul;
	// 반 밀기도 눈에 띄게 걷게(DESIGN 3.3) — 단 최고 속도를 넘지 않게 눌러 둔다(물·탈것·쓰러짐은 최고가 더 낮다).
	Movement->MinAnalogWalkSpeed = FMath::Min(MinAnalogWalkCmPerSecond, Movement->MaxWalkSpeed);
}

bool ASnarehavenCharacter::IsRunningNow() const
{
	if (LastMoveInputSize < RunInputThreshold)
	{
		return false;
	}

	// 체력·스태미나가 아직 안 붙은 곳(자리표시 폰·2층 검사용 캐릭터)에서는 예전 그대로 달린다 —
	// 컴포넌트가 없다고 이동이 느려지면 M11 이전 조각들의 속도 검사가 조용히 뒤집힌다.
	return !IsValid(PlayerVitals) || PlayerVitals->CanRun();
}

void ASnarehavenCharacter::PlayBodyAnimation(UAnimSequence* Anim, bool bLoop, float PlayRate, bool bRestart)
{
	UPlayerBodyAnimInstance* const BodyAnim = Anim != nullptr ? EnsureBodyAnimInstance() : nullptr;
	if (BodyAnim == nullptr)
	{
		return;
	}

	// 몸 전체 기본 클립. 다른 클립이면 인스턴스가 `BodyCrossfadeSeconds` 동안 교차 섞는다 — 예전엔 프레임 0 으로
	// 툭 갈아 끼워 전환마다 자세가 튀었다 (M18-10 ①). 행동이 남아 있었으면 `BodyActionBlendSeconds` 동안 빠진다.
	BodyAnim->SetBaseClip(Anim, bLoop, PlayRate, bRestart);
	BodyAnim->ClearActionClip();
	NoteShownBodyClip(Anim, PlayRate);
}

void ASnarehavenCharacter::PlayBodyAction(UAnimSequence* Anim, bool bLoop, float PlayRate, bool bRestart)
{
	UPlayerBodyAnimInstance* const BodyAnim = Anim != nullptr ? EnsureBodyAnimInstance() : nullptr;
	if (BodyAnim == nullptr)
	{
		return;
	}

	// 행동 아래에도 이동 상태 클립이 돈다. 서 있으면(대기) 행동이 몸 전체를 덮고, 걷기·달리기·헤엄·탈것·배 위에서는
	// 상체만 받는다 — 걸으며 휘두르면 다리가 멈춘 채 미끄러지던 결함 (M18-10 ②).
	float BasePlayRate = 1.f;
	UAnimSequence* const Base = SelectBaseBodyClip(BasePlayRate);
	BodyAnim->SetBaseClip(Base, true, BasePlayRate, false);
	BodyAnim->SetActionClip(Anim, bLoop, PlayRate, bRestart, Base != nullptr && Base != IdleAnim);
	NoteShownBodyClip(Anim, PlayRate);
}

UPlayerBodyAnimInstance* ASnarehavenCharacter::GetBodyAnimInstance() const
{
	return IsValid(GetMesh()) ? Cast<UPlayerBodyAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
}

UPlayerBodyAnimInstance* ASnarehavenCharacter::EnsureBodyAnimInstance()
{
	USkeletalMeshComponent* const Body = GetMesh();
	if (!IsValid(Body) || Body->GetSkeletalMeshAsset() == nullptr)
	{
		return nullptr;
	}
	if (UPlayerBodyAnimInstance* const Existing = GetBodyAnimInstance())
	{
		return Existing;
	}

	// 처음 몸을 올릴 때, 그리고 시험이 `PlayAnimation`(SingleNode)으로 자세를 잡아 둔 뒤에 다시 건다.
	// 새 인스턴스는 클립이 없으니 지금 클립을 잊어야 다음 `PlayBodyAnimation` 이 다시 건다.
	Body->SetAnimInstanceClass(UPlayerBodyAnimInstance::StaticClass());
	UPlayerBodyAnimInstance* const Created = GetBodyAnimInstance();
	if (Created != nullptr)
	{
		Created->SetBlendSeconds(BodyCrossfadeSeconds, BodyActionBlendSeconds);
		Created->SetUpperBodyRootBone(UpperBodyBoneName);
		Created->SetHeadLift(HeadBoneName, HeadLiftDegrees);
		CurrentBodyAnim = nullptr;
		CurrentBodyPlayRate = 1.f;
	}
	return Created;
}

void ASnarehavenCharacter::NoteShownBodyClip(UAnimSequence* Anim, float PlayRate)
{
	if (Anim != CurrentBodyAnim)
	{
		// 맨 위에 보이는 클립이 바뀔 때만 찍는다 — 달리는 도중 이 줄이 되풀이되면 그게 사람이 말한 "멈칫" 이다
		// (결함 ②). 평소에는 로그가 늘지 않는다.
		UE_LOG(LogSnarehaven, Log, TEXT("몸 클립 전환 %s -> %s (속도 %.1f · 입력 크기 %.2f)"),
			IsValid(CurrentBodyAnim) ? *CurrentBodyAnim->GetName() : TEXT("없음"),
			*Anim->GetName(), GetVelocity().Size2D(), LastMoveInputSize);
		CurrentBodyAnim = Anim;
	}
	CurrentBodyPlayRate = PlayRate;
}

void ASnarehavenCharacter::FlushLevelEntryInput()
{
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		return;
	}

	// 로비 `혼자 사냥` 버튼의 왼쪽 클릭이 레벨 이동을 넘어 새 폰까지 따라왔다 — 아무도 안 눌렀는데
	// 진입 직후 반복 공격이 돌아 돼지가 3~5초에 죽었다 (감독 재현 2026-09-20 13:00 (1)).
	PlayerController->FlushPressedKeys();

	// 씻고도 눌려 있으면 그 누름은 이 레벨에서 사람이 누른 것이 아니다 — 뗄 때까지 무시한다.
	// **눌린 키가 없으면 잠그지 않는다**: 무조건 잠그면 레벨마다 사람의 첫 클릭 한 번이 먹혀 버린다.
	if (IsAnyAttackKeyDown(PlayerController))
	{
		DisarmAttackInputUntilRelease();
	}
}

bool ASnarehavenCharacter::IsAnyAttackKeyDown(const APlayerController* PlayerController) const
{
	if (!IsValid(PlayerController) || !IsValid(DefaultMappingContext) || !IsValid(AttackAction))
	{
		return false;
	}

	// 키 이름을 여기 적지 않는다 — `IMC_Default` 가 `IA_Attack` 에 뭘 걸었든(M1-10a 의 마우스·A·패드)
	// 그대로 따라간다. 매핑을 바꿔도 이 함수는 안 고친다.
	for (const FEnhancedActionKeyMapping& Mapping : DefaultMappingContext->GetMappings())
	{
		if (Mapping.Action == AttackAction && PlayerController->IsInputKeyDown(Mapping.Key))
		{
			UE_LOG(LogSnarehaven, Log, TEXT("[입력] 레벨 진입 뒤에도 공격 키가 눌려 있다: %s"),
				*Mapping.Key.ToString());
			return true;
		}
	}

	return false;
}

void ASnarehavenCharacter::ApplyHudInputMode(APlayerController* PlayerController)
{
	if (!IsValid(PlayerController))
	{
		return;
	}

	// UMG 버튼이 삼킨 클릭이 뷰포트로도 가면, HUD `포획` 버튼을 누른 순간 같은 클릭이 `IA_Attack`
	// 으로도 들어가 5타째가 버튼 OnClicked 보다 먼저 나가 동물이 죽었다 (감독 재현 2026-09-20 13:00 (2)).
	// 이 게임엔 마우스 시점 조작이 없다(이동은 키보드·조이스틱뿐) — 커서를 켠 채 GameAndUI 로 둔다.
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);
}

void ASnarehavenCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 존을 옮기면 이 폰은 사라지고 다음 맵에서 새로 스폰된다 — 떠나기 전에 걸친 것을 게임 인스턴스에
	// 맡겨 둔다 (M15-10). 내가 조종하는 폰만: 호스트 프로세스에 있는 **게스트 복제본**까지 맡기면
	// 남의 도구를 내 기억에 덮어쓴다.
	if (IsLocallyControlled() && IsValid(Equipment))
	{
		if (USnareGearMemorySubsystem* const Memory = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USnareGearMemorySubsystem>() : nullptr)
		{
			Memory->RememberSlots(Equipment->GetEquippedSlots());
		}
	}

	// 거점 서브시스템은 게임 인스턴스와 같이 산다 — 사라진 폰을 부르지 않게 푼다.
	if (BaseMessageHandle.IsValid())
	{
		if (USnareBaseSubsystem* const Base = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USnareBaseSubsystem>() : nullptr)
		{
			Base->OnBaseMessage.Remove(BaseMessageHandle);
		}
		BaseMessageHandle.Reset();
	}

	// 폰이 사라져도 위젯은 뷰포트에 남는다 — 죽은 캐릭터의 HUD 가 화면에 얼어붙지 않게 직접 내린다.
	if (IsValid(HuntHud))
	{
		HuntHud->RemoveFromParent();
		HuntHud = nullptr;
	}

	if (IsValid(PauseMenu))
	{
		PauseMenu->RemoveFromParent();
		PauseMenu = nullptr;
	}

	if (IsValid(RanchHud))
	{
		RanchHud->RemoveFromParent();
		RanchHud = nullptr;
	}

	if (IsValid(GiftScreen))
	{
		GiftScreen->RemoveFromParent();
		GiftScreen = nullptr;
	}

	if (IsValid(AttendanceScreen))
	{
		AttendanceScreen->RemoveFromParent();
		AttendanceScreen = nullptr;
	}

	if (USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr)
	{
		Backend->OnPlayerSnapshotChanged.RemoveDynamic(this, &ASnarehavenCharacter::HandleBackendSnapshotChanged);
		Backend->OnStarterKitGranted.RemoveDynamic(this, &ASnarehavenCharacter::HandleStarterKitGranted);
		Backend->OnDisplayNameChanged.RemoveDynamic(this, &ASnarehavenCharacter::HandleDisplayNameChanged);
	}

	if (IsValid(PlayerVitals))
	{
		PlayerVitals->OnDied.RemoveDynamic(this, &ASnarehavenCharacter::HandlePlayerDied);
	}

	// 누운 시간 타이머가 살아 있으면 지운다 — 폰이 사라진 뒤 부활을 부르면 죽은 포인터를 탄다.
	GetWorldTimerManager().ClearTimer(DownedTimerHandle);
	GetWorldTimerManager().ClearTimer(CheckKillTimer);

	Super::EndPlay(EndPlayReason);
}

void ASnarehavenCharacter::HandlePlayerDied()
{
	// ① 잃는 것은 **지급 대기열 하나뿐**이다 (DESIGN 3.1 「사망 시 잃는 것」 표).
	// 대기열은 "이런 걸 얻었으니 반영해 달라" 는 아직 안 보낸 요청이라 누구의 보유도 아니다 —
	// 버려도 재화·인벤토리 숫자를 클라이언트가 만지는 일이 없다 (CLAUDE.md 8 · DESIGN 4.8).
	// **백엔드 스냅샷이 채운 보유 칸은 한 칸도 안 건드린다** — 죽어도 재화는 전액 남는다.
	// (재화 쪽 함수 이름은 여기 주석에도 쓰지 않는다 — 2층 훑기가 주석까지 같이 보기 때문이다.)
	const int32 LostRequests = IsValid(Inventory) ? Inventory->GetPendingGrants().Num() : 0;
	if (IsValid(Inventory))
	{
		Inventory->ClearPendingGrants();
	}

	// ② 백엔드로 나가는 것은 **"죽었다" 사실 한 줄**뿐이다 (M8-02 통로 — 값을 바꾸지 않는다).
	// 대기열을 실제로 폐기하는 원장도 백엔드 몫이고, 그 CloudScript 는 M2-03(사람 몫)이다.
	UGameInstance* const GameInstance = GetGameInstance();
	if (USnareBackendSubsystem* const Backend =
		IsValid(GameInstance) ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr)
	{
		TMap<FName, FString> Payload;
		Payload.Add(TEXT("zone_id"), AZonePortalActor::FindCurrentZoneID(GetWorld()).ToString());
		Payload.Add(TEXT("lost_pending_count"), FString::FromInt(LostRequests));
		Backend->WriteGameEvent(TEXT("player_died"), Payload);
	}

	// ②-2 배낭은 서버가 죽은 자리 가방으로 옮긴다 — 무엇을 남길지는 서버 `DropOnDeath` 판정 (DESIGN 8.12).
	if (IsValid(Backpack))
	{
		Backpack->RequestDropOnDeath();
	}

	// ③ 쓰러지는 모습을 끝까지 보여 주고 **그 뒤에** 부활시킨다 (DESIGN 3.1 「쓰러짐 → 부활」).
	// 바로 옮기면 죽은 것이 화면에 안 보인다 — 옮기기·값 되돌리기는 `FinishDownedSequence` 몫이다.
	UE_LOG(LogSnarehaven, Log,
		TEXT("플레이어 사망 - 지급 대기열 %d줄을 버렸다 (보유 인벤토리-재화는 그대로) - 이제 쓰러진다"),
		LostRequests);
	BeginDownedSequence();
}

float ASnarehavenCharacter::GetDownedDurationSeconds() const
{
	// 클립 길이는 **클립에서**, 누운 시간은 **표에서**. 코드에는 어느 쪽 숫자도 없다 (DataDriven.md 3).
	const float LingerSeconds = IsValid(PlayerVitals) ? PlayerVitals->GetDeathLingerSeconds() : -1.f;
	if (!IsValid(DeathAnim) || LingerSeconds < 0.f)
	{
		return -1.f;
	}
	return DeathAnim->GetPlayLength() + LingerSeconds;
}

void ASnarehavenCharacter::BeginDownedSequence()
{
	const float DownedSeconds = GetDownedDurationSeconds();
	if (DownedSeconds <= 0.f)
	{
		// 클립이 없거나 표가 미정이면 **아무 숫자도 지어내지 않고** 예전처럼 그 자리에서 부활시킨다.
		// 시간만큼 조작을 빼앗아 놓고 이유를 로그에 안 남기면 다음 사람이 원인을 못 찾는다.
		UE_LOG(LogSnarehaven, Warning,
			TEXT("쓰러짐 시간이 미정이다 (클립 %s · 누운 시간 %.2f) - 쓰러지는 모습 없이 바로 부활한다"),
			IsValid(DeathAnim) ? *DeathAnim->GetName() : TEXT("없음"),
			IsValid(PlayerVitals) ? PlayerVitals->GetDeathLingerSeconds() : -1.f);
		FinishDownedSequence();
		return;
	}

	bDowned = true;

	// 죽는 순간 누름 상태를 끊는다 — 누운 채로 "누르는 중" 이 남지 않게.
	if (IsValid(MeleeAttack))
	{
		MeleeAttack->StopAttacking();
	}
	// 밀고 있던 관성도 여기서 끊는다. 입력은 `MoveByAxis` 가 막지만 이미 붙은 속도는 안 막힌다.
	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
	LastMoveInputSize = 0.f;
	if (IsValid(PlayerVitals))
	{
		PlayerVitals->SetMoveInput(false);
		PlayerVitals->SetRunInput(false);
	}

	// 쓰러짐 클립을 그 프레임에 바로 건다 — Tick 을 기다리면 한 프레임 동안 대기 자세로 서 있는다.
	UpdateBodyAnimation();
	// 클립과 같은 순간 「털썩」 — 모든 기기가 그 자리에서 듣는다 (M44-03 · DESIGN 2.2).
	Server_PlayDownVoice();

	UE_LOG(LogSnarehaven, Log, TEXT("쓰러짐 - 클립 %s %.2f초 + 누운 채 %.2f초 = %.2f초 뒤 부활한다"),
		*DeathAnim->GetName(), DeathAnim->GetPlayLength(),
		PlayerVitals->GetDeathLingerSeconds(), DownedSeconds);

	// 월드가 없으면(검사가 만든 떠 있는 폰) 타이머가 안 도니 그 자리에서 마무리한다 —
	// 안 그러면 영영 누운 채로 남는다.
	if (GetWorld() != nullptr)
	{
		GetWorldTimerManager().SetTimer(DownedTimerHandle, this,
			&ASnarehavenCharacter::FinishDownedSequence, DownedSeconds, false);
	}
	else
	{
		FinishDownedSequence();
	}
}

void ASnarehavenCharacter::FinishDownedSequence()
{
	bDowned = false;

	// 시작 자리로 옮기는 것은 호스트가(Server RPC), 생존 값 되돌리기는 각자 자기 것 (M11-03 의 ③).
	Server_RespawnAtPlayerStart();
	if (IsValid(PlayerVitals))
	{
		PlayerVitals->ReviveAtRespawnValues();
	}
	UpdateBodyAnimation();
	// 부활 소리는 여기서 내지 않는다 — 자리 옮기기가 서버 몫이라 새 자리는 서버만 안다 (`Server_RespawnAtPlayerStart_Implementation`).

	UE_LOG(LogSnarehaven, Log, TEXT("부활 - 쓰러짐 끝 (자리는 호스트가 고름, 보유 인벤토리-재화는 그대로)"));
}

void ASnarehavenCharacter::Server_RespawnAtPlayerStart_Implementation()
{
	// 시작 자리를 아는 것은 게임모드뿐이고, 게임모드는 호스트에만 산다.
	AGameModeBase* const GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode() : nullptr;
	// 집·텐트가 있으면 그 문 앞으로, 없으면 시작 자리로 (DESIGN 8.12 「사망」 표 「부활 자리」).
	// 집 앞이 막혀 `TeleportTo` 가 실패해도 시작 자리로 간다 — 갇히지 않는다.
	if (TryRespawnAtOwnHome())
	{
		PlayReviveSound(true);
		return;
	}
	AActor* const StartSpot = IsValid(GameMode) ? GameMode->FindPlayerStart(GetController()) : nullptr;
	if (!IsValid(StartSpot))
	{
		// 시작 자리가 없으면 **옮기지 않는다** — 원점(0,0,0)으로 떨어뜨리면 바닥 밑일 수 있다.
		UE_LOG(LogSnarehaven, Warning, TEXT("부활 자리(PlayerStart)를 못 찾았다 - 그 자리에서 다시 시작한다"));
		// 이 함수는 죽은 뒤에만 불린다 — 그 자리에서 일어서도 소리는 한 번 (M59-03).
		PlayReviveSound(true);
		return;
	}

	// `TeleportTo` 는 겹침을 풀어 자리를 잡아 준다 — `SetActorLocation` 은 시작 자리에 다른 폰이
	// 서 있으면 서로 끼인 채로 놓는다 (멀티에서 같은 자리에 둘이 부활할 수 있다).
	TeleportTo(StartSpot->GetActorLocation(), StartSpot->GetActorRotation());
	// 옮긴 뒤 자리에서 낸다 — 게스트 기기에선 아직 쓰러진 자리라 그쪽에서 내면 엉뚱한 곳에서 들린다 (M59-03).
	PlayReviveSound(true);
}

bool ASnarehavenCharacter::TryRespawnAtOwnHome()
{
	UWorld* const World = GetWorld();
	// 검사 전용 `-SnareHomeAt=x,y` — 오프라인엔 집이 없다. 그 자리에 문이 동쪽(+X)인 3x3 집이 있다고 치고 같은 길을 탄다 (루프#1070).
	FString HomeAtText;
	if (World != nullptr && SnareCheckSwitch::Value(TEXT("SnareHomeAt="), HomeAtText, false))
	{
		TArray<FString> Parts;
		HomeAtText.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() >= 2)
		{
			return TeleportToHomeFront(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), 0.f), 0.f, FIntPoint(3, 3), 100.f);
		}
	}
	if (World == nullptr || KnownPlayFabId.IsEmpty())
	{
		return false;
	}
	// 이 레벨에 서 있는 내 텐트/집 하나 (집은 텐트 액터가 `bHouse` 로 바뀐 것). 목장 거점존의 칸도 같은 액터다.
	const ASnarePlacedStructureActor* Home = nullptr;
	for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
	{
		if (!It->IsPreview() && It->IsTent() && It->GetOwnerPlayFabId() == KnownPlayFabId)
		{
			Home = *It;
			break;
		}
	}
	if (Home == nullptr)
	{
		return false;
	}
	return TeleportToHomeFront(Home->GetActorLocation(), Home->GetActorRotation().Yaw, Home->GetFootprint(), Home->GetCellSizeCm());
}

bool ASnarehavenCharacter::TeleportToHomeFront(const FVector& HomeLocation, float HomeYaw, const FIntPoint& Footprint, float CellCm)
{
	UWorld* const World = GetWorld();
	FVector Spot = SnareRevive::HomeFrontLocation(HomeLocation, HomeYaw, Footprint, CellCm);
	// 바닥 위에 내려놓는다 — 집 가운데 높이에서 위아래로 쏜다 (가방 `Server_SpawnDropBag` 와 같은 물체 종류).
	FHitResult Hit;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RespawnHomeGround), false, this);
	if (World->LineTraceSingleByObjectType(Hit, Spot + FVector(0.f, 0.f, 200.f), Spot - FVector(0.f, 0.f, 500.f),
		FCollisionObjectQueryParams(ECC_WorldStatic), TraceParams))
	{
		Spot.Z = Hit.ImpactPoint.Z + GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	// 집을 마주 보고 선다(문에서 나온 방향의 반대).
	const FRotator Facing(0.f, HomeYaw + 180.f, 0.f);
	const bool bMoved = TeleportTo(Spot, Facing);
	UE_LOG(LogSnarehaven, Log, TEXT("부활 - 내 집 앞 (%s) %s"), *Spot.ToCompactString(), bMoved ? TEXT("섰다") : TEXT("막혀서 시작 자리로"));
	return bMoved;
}

void ASnarehavenCharacter::HandleBackendSnapshotChanged()
{
	PullInventoryFromBackend();
}

void ASnarehavenCharacter::HandleStarterKitGranted()
{
	// 내 계정의 시작 장비다 — 남의 폰(호스트에 있는 게스트 복제본)에 걸지 않는다
	// (`PullInventoryFromBackend` 와 같은 규칙).
	if (!IsLocallyControlled() || !IsValid(Equipment))
	{
		return;
	}

	const USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	if (Backend == nullptr)
	{
		return;
	}

	// 무엇이 어느 부위에 걸리는지는 `DT_ItemTable` 이 안다 — 행 이름도 부위도 여기 안 적는다
	// (DataDriven.md 1). 사람이 버튼으로 드는 길과 **같은 `EquipItem`** 을 탄다. 플레이어가 고른 것이 아니라 소리는 없다 (M47-03).
	const UEquipmentComponent::FQuietChanges Quiet(Equipment);
	for (const FName ItemID : Backend->GetStarterKitGrantedItems())
	{
		const bool bWorn = Equipment->EquipItemByItemID(ItemID);
		UE_LOG(LogSnarehaven, Log, TEXT("시작 장비 장착 - %s · 결과 %s"),
			*ItemID.ToString(), bWorn ? TEXT("들었다") : TEXT("장비 행이 아니라 안 들었다"));
	}
}

void ASnarehavenCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// 게스트 기기에서는 폰이 먼저 서고 조종은 나중에 붙는다 — `BeginPlay` 때는 아직 내 폰이 아니라
	// 되걸 수 없었다. 조종이 붙은 지금 한 번 더 본다 (한 번 걸었으면 `bGearRestored` 가 막는다).
	RestoreRememberedGear();

	// 닉네임도 같은 까닭 — 게스트는 조종이 붙어야 이 폰으로 호스트에 말할 수 있다.
	ReportNicknameToServer();
}

void ASnarehavenCharacter::ReportNicknameToServer()
{
	if (!IsLocallyControlled())
	{
		return;
	}
	const USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	if (Backend != nullptr && !Backend->GetPlayFabId().IsEmpty() && Backend->GetPlayFabId() != KnownPlayFabId)
	{
		Server_ReportPlayFabId(Backend->GetPlayFabId());
	}
	const FString Nickname = Backend != nullptr ? Backend->GetDisplayName() : FString();
	if (Nickname.IsEmpty() || Nickname == ReportedNickname)
	{
		return;
	}
	ReportedNickname = Nickname;
	Server_ReportNickname(Nickname);
}

void ASnarehavenCharacter::HandleDisplayNameChanged()
{
	ReportNicknameToServer();
}

void ASnarehavenCharacter::Server_ReportPlayFabId_Implementation(const FString& PlayFabId)
{
	KnownPlayFabId = PlayFabId;
}

void ASnarehavenCharacter::Server_ReportNickname_Implementation(const FString& Nickname)
{
	if (ASnarehavenGameMode* const Mode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ASnarehavenGameMode>() : nullptr)
	{
		Mode->ApplyPlayerNickname(GetController(), Nickname);
	}
}

void ASnarehavenCharacter::RestoreRememberedGear()
{
	// 남의 복제본에 내 기억을 걸지 않는다 — 그 사람 기기가 자기 기억으로 걸고, 그 결과가 복제로 내려온다.
	if (bGearRestored || !IsLocallyControlled() || !IsValid(Equipment))
	{
		return;
	}

	USnareGearMemorySubsystem* const Memory = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USnareGearMemorySubsystem>() : nullptr;
	if (Memory == nullptr)
	{
		return;
	}

	// 처음 접속(기억 없음)에도 한 번 본 것으로 친다 — 아래 `-SnareEquip=` 검사 스위치와 겹쳐 두 번 걸리면
	// 같은 부위를 두 번 덮어쓴다.
	bGearRestored = true;
	// 존에 들어설 때마다 철컥·부스럭이 한꺼번에 터지지 않게 (M47-03 무음 길) — 게스트면 요청에 실려 호스트도 조용하다.
	const UEquipmentComponent::FQuietChanges Quiet(Equipment);
	const TArray<FEquippedSlot> Remembered = Memory->GetRememberedSlots();
	for (const FEquippedSlot& Entry : Remembered)
	{
		// 사람이 가방 카드로 드는 길과 **같은 함수**다 — 게스트면 그 안에서 호스트에게 요청이 올라간다.
		const bool bWorn = Equipment->EquipItem(Entry.ItemRowName, Entry.Slot, Entry.SocketName);
		UE_LOG(LogSnarehaven, Log, TEXT("[Gear] 맵 이동 뒤 다시 걺 - %s (%s) · 결과 %s"),
			*Entry.ItemRowName.ToString(), *UEnum::GetValueAsString(Entry.Slot),
			bWorn ? TEXT("들었다") : TEXT("요청만 보냈거나 못 들었다"));
	}
}

void ASnarehavenCharacter::PullInventoryFromBackend()
{
	// 남의 폰(호스트 프로세스에 있는 게스트 복제본)에 내 계정 인벤토리를 밀어 넣지 않는다 —
	// 백엔드 계정은 이 기기에 로그인한 사람 하나다 (DESIGN 4.4).
	if (!IsLocallyControlled() || !IsValid(Inventory))
	{
		return;
	}

	const USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	if (Backend == nullptr)
	{
		return;
	}

	Inventory->ApplyBackendSnapshot(Backend->GetPlayerSnapshot().InventorySlots);
	if (IsValid(Backpack) && Backend->IsSnapshotFromBackend())
	{
		Backpack->ApplyBackendInventory(Backend->GetPlayerSnapshot().InventorySlots);
	}
}

void ASnarehavenCharacter::CreateHuntHud()
{
	if (IsValid(HuntHud))
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		return;
	}

	// 목장엔 사냥할 동물이 없다 — 포획 버튼이 뜨면 안 된다 (감독 지시 2026-09-19 · DESIGN 3.2).
	// 맵 이름이 아니라 게임모드가 답한다. 게임모드를 못 보면(멀티 클라이언트 — M3) 사냥 쪽으로 둔다:
	// HUD 가 남는 쪽이 조작을 잃는 쪽보다 낫고, 목장은 아직 혼자 들어가는 곳이다 (DESIGN 3.2 확장 단계).
	const ASnarehavenGameMode* GameMode = GetWorld() != nullptr
		? GetWorld()->GetAuthGameMode<ASnarehavenGameMode>() : nullptr;
	if (GameMode != nullptr && !GameMode->ShouldShowHuntHud())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("이 레벨의 게임모드가 사냥 HUD 를 띄우지 않는다"));
		return;
	}

	// WBP 가 아직 없어도 HUD 는 떠야 3층 캡처를 찍는다 (자리표시 트리는 위젯이 스스로 만든다).
	const TSubclassOf<UHuntHudWidget> WidgetClass =
		HuntHudClass != nullptr ? HuntHudClass : TSubclassOf<UHuntHudWidget>(UHuntHudWidget::StaticClass());

	HuntHud = CreateWidget<UHuntHudWidget>(PlayerController, WidgetClass);
	if (!IsValid(HuntHud))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("사냥 HUD 위젯을 만들지 못했다"));
		return;
	}

	HuntHud->AddToViewport();
	HuntHud->BindToCharacter(this);
	BindBaseMessages();
	// 화면에 버튼이 생겼으니 클릭이 버튼에서 끝나야 한다 (M1-10e (a)).
	ApplyHudInputMode(PlayerController);
	UE_LOG(LogSnarehaven, Log, TEXT("사냥 HUD 표시: %s"), *WidgetClass->GetName());

	// 처음 켠 기기에만 첫 실행 안내(M17-01). 한 번 끝내거나 건너뛰면 저장돼 다시 안 뜬다.
	if (USnareFirstRunGuideWidget::ShouldShowGuide() && !IsValid(FirstRunGuide))
	{
		FirstRunGuide = CreateWidget<USnareFirstRunGuideWidget>(PlayerController, USnareFirstRunGuideWidget::StaticClass());
		if (IsValid(FirstRunGuide))
		{
			FirstRunGuide->AddToViewport(10);
			FirstRunGuide->BindToCharacter(this);
			FirstRunGuide->OnFinished.AddUObject(this, &ASnarehavenCharacter::CreateNextGoalHud);
			UE_LOG(LogSnarehaven, Log, TEXT("[UI] 첫 실행 안내를 띄웠다 (%d장)"), USnareFirstRunGuideWidget::GetSteps().Num());
		}
	}
	if (!IsValid(FirstRunGuide) || FirstRunGuide->IsFinished())
	{
		CreateNextGoalHud();
	}
}

void ASnarehavenCharacter::CreateNextGoalHud()
{
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	if (IsValid(NextGoalHud) || !IsValid(PlayerController) || !PlayerController->IsLocalController()
		|| (SnareCheckSwitch::Param(TEXT("SnareNoGuide")) && !SnareCheckSwitch::Param(TEXT("SnareShowGoals"))))  // 검사 스위치: 안내 끈 캡처에도 목표 줄을 본다
	{
		return;
	}
	NextGoalHud = CreateWidget<USnareNextGoalWidget>(PlayerController, USnareNextGoalWidget::StaticClass());
	if (IsValid(NextGoalHud))
	{
		NextGoalHud->AddToViewport(5);
		UGameInstance* const GameInstance = GetGameInstance();
		NextGoalHud->BindToBackend(GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr);
		UE_LOG(LogSnarehaven, Log, TEXT("[UI] 다음 목표 줄을 띄웠다: %s"), *NextGoalHud->GetGoalText().ToString());
	}
	StoryGoalHud = CreateWidget<USnareStoryGoalWidget>(PlayerController, USnareStoryGoalWidget::StaticClass());
	if (IsValid(StoryGoalHud))
	{
		StoryGoalHud->AddToViewport(5);
		StoryGoalHud->OnLineShown.AddWeakLambda(this, [this](bool bShown)
		{
			if (IsValid(NextGoalHud))
			{
				NextGoalHud->SetSuppressed(bShown);  // 스토리 줄이 있으면 옛 「다음 목표」 줄은 접는다
			}
		});
		StoryGoalHud->OnViewChanged.AddWeakLambda(this, [this](const SnareStory::FStoryView& NewView)
		{
			const SnareStory::EGiverMark Mark = SnareStory::GiverMark(NewView);  // 의뢰인 머리 위 「!」「?」
			const SnareStory::EStoryNpc Target = SnareStory::TargetNpc(NewView.CurrentStep);  // 지금 단계의 상대에게만 붙인다
			SnareStory::SetCurrentTargetNpc(Target);  // 미니맵·월드 지도 표식이 읽는다
			for (TActorIterator<AShopVendorActor> It(GetWorld()); It; ++It)
			{
				const bool bTarget = Target != SnareStory::EStoryNpc::None && It->GetStoryNpc() == Target;
				It->SetStoryMark(bTarget ? Mark : SnareStory::EGiverMark::None);
			}
			ShowStoryDialogueIfNew(NewView.CurrentStep);  // 같은 순간이면 대사부터 — 장 끝은 대사를 다 닫은 뒤 (감독 보정 10-20 17:15)
			ShowChapterCardIfNew(SnareStory::CompletedChapter(NewView));
		});
		StoryGoalHud->OnTapped.AddWeakLambda(this, [this]() { OpenStoryQuestScreen(); });
		UGameInstance* const GameInstance = GetGameInstance();
		StoryGoalHud->BindToBackend(GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr);
		UE_LOG(LogSnarehaven, Log, TEXT("[UI] 스토리 지금 할 일 줄을 띄웠다"));
		if (SnareCheckSwitch::Param(TEXT("SnareOpenStory")))  // 검사 스위치: 줄 누르기를 흉내 내 의뢰 판(줄기 탭)이 열린 캡처를 찍는다
		{
			FTimerHandle OpenStoryTimer;
			GetWorldTimerManager().SetTimer(OpenStoryTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { OpenStoryQuestScreen(); }), 8.0f, false);
		}
		int32 PreviewChapter = 0;
		if (SnareCheckSwitch::Value(TEXT("SnareStoryChapterCard="), PreviewChapter) && PreviewChapter > 0)  // 검사 스위치: 서버 없이 그 장 끝 카드를 띄운다 (본 장은 저장 안 함)
		{
			FTimerHandle CardTimer;
			GetWorldTimerManager().SetTimer(CardTimer, FTimerDelegate::CreateWeakLambda(this, [this, PreviewChapter]()
			{
				APlayerController* const PlayerCtrl = Cast<APlayerController>(GetController());
				USnareStoryChapterCardWidget* const Card = IsValid(PlayerCtrl)
					? CreateWidget<USnareStoryChapterCardWidget>(PlayerCtrl, USnareStoryChapterCardWidget::StaticClass()) : nullptr;
				if (IsValid(Card))
				{
					Card->bPersistSeen = false;
					if (Card->ShowChapter(PreviewChapter))
					{
						Card->AddToViewport(9);
						UE_LOG(LogSnarehaven, Log, TEXT("[Story] 검사 스위치로 %d장 끝 카드를 띄웠다"), PreviewChapter);
					}
				}
			}), 8.0f, false);
		}
		int32 GlanceChapter = 0;
		if (SnareCheckSwitch::Value(TEXT("SnareStoryGlance="), GlanceChapter) && GlanceChapter > 0)  // 검사 스위치: 서버 없이 장 끝 연출(등대 비추기 → 그 장 카드)을 돌린다 (본 장은 저장 안 함)
		{
			FTimerHandle GlanceTimer;
			// 차례 길(`StartChapterEnd`) 그대로 — `-SnareStoryDialogue=` 판(8초)과 같이 주면 대사가 먼저 떠 있고 장 끝이 미뤄진다
			GetWorldTimerManager().SetTimer(GlanceTimer, FTimerDelegate::CreateWeakLambda(this, [this, GlanceChapter]()
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Story] 검사 스위치로 %d장 끝 연출을 청한다"), GlanceChapter);
				StartChapterEnd(GlanceChapter, false);
			}), 8.5f, false);
		}
		int32 PreviewDialogueStep = 0;
		if (SnareCheckSwitch::Value(TEXT("SnareStoryDialogue="), PreviewDialogueStep) && PreviewDialogueStep > 0)  // 검사 스위치: 서버 없이 그 단계 대사 판을 띄운다 (본 단계는 저장 안 함)
		{
			FTimerHandle DialogueTimer;
			GetWorldTimerManager().SetTimer(DialogueTimer, FTimerDelegate::CreateWeakLambda(this, [this, PreviewDialogueStep]()
			{
				APlayerController* const PlayerCtrl = Cast<APlayerController>(GetController());
				if (!IsValid(PlayerCtrl))
				{
					return;
				}
				StoryDialogueHud = CreateWidget<USnareStoryDialogueWidget>(PlayerCtrl, USnareStoryDialogueWidget::StaticClass());
				if (IsValid(StoryDialogueHud))
				{
					StoryDialogueHud->bPersistSeen = false;
					if (StoryDialogueHud->ShowStep(PreviewDialogueStep))
					{
						StoryDialogueHud->OnFinished.AddUObject(this, &ASnarehavenCharacter::HandleStoryDialogueClosed);
						int32 PreviewDialogueSkip = 0;
						SnareCheckSwitch::Value(TEXT("SnareStoryDialogueSkip="), PreviewDialogueSkip);  // 검사 스위치: 대사를 N 줄 넘긴 모습 (긴 줄 캡처용)
						for (int32 SkipIndex = 0; SkipIndex < PreviewDialogueSkip; ++SkipIndex)
						{
							StoryDialogueHud->Advance();
						}
						StoryDialogueHud->AddToViewport(8);
						UE_LOG(LogSnarehaven, Log, TEXT("[Story] 검사 스위치로 %d단계 대사 판을 띄웠다"), PreviewDialogueStep);
					}
				}
			}), 8.0f, false);
		}
	}
}

void ASnarehavenCharacter::ShowChapterCardIfNew(int32 CompletedChapter)
{
	if (USnareStoryChapterCardWidget::ShouldShow(CompletedChapter))
	{
		StartChapterEnd(CompletedChapter, true);
	}
}

bool ASnarehavenCharacter::IsStoryDialogueOpen() const
{
	return IsValid(StoryDialogueHud) && !StoryDialogueHud->IsFinished();
}

bool ASnarehavenCharacter::IsChapterEndOpen() const
{
	return IsValid(LighthouseGlanceCamera) || (IsValid(StoryChapterCardHud) && !StoryChapterCardHud->IsFinished());
}

void ASnarehavenCharacter::StartChapterEnd(int32 Chapter, bool bPersistSeen)
{
	if (!IsValid(Cast<APlayerController>(GetController())))
	{
		return;
	}
	if (!StoryPopupQueue.RequestChapterEnd(Chapter, bPersistSeen, IsStoryDialogueOpen(), IsChapterEndOpen()))
	{
		if (StoryPopupQueue.WaitingChapter == Chapter)
		{
			UE_LOG(LogSnarehaven, Log, TEXT("[Story] %d장 끝 — 대사 판이 열려 있어 닫은 뒤로 미룬다"), Chapter);
		}
		return;
	}
	// 장 끝 연출 — 카드 전에 카메라가 등대를 한 번 비춘다. 등대가 없는 맵(목장)이면 바로 카드.
	if (!PlayLighthouseGlance(Chapter, [this, Chapter, bPersistSeen]() { OpenChapterCard(Chapter, bPersistSeen); }))
	{
		OpenChapterCard(Chapter, bPersistSeen);
	}
}

void ASnarehavenCharacter::StartWaitingChapterEnd()
{
	if (StoryPopupQueue.WaitingChapter <= 0 || IsStoryDialogueOpen() || IsChapterEndOpen())
	{
		return;
	}
	bool bPersistSeen = true;
	const int32 Chapter = StoryPopupQueue.TakeChapterAfterDialogue(bPersistSeen);
	UE_LOG(LogSnarehaven, Log, TEXT("[Story] 대사 판을 닫았다 — 미룬 %d장 끝을 잇는다 (등대 → 카드)"), Chapter);
	StartChapterEnd(Chapter, bPersistSeen);
}

void ASnarehavenCharacter::HandleStoryDialogueClosed()
{
	StartWaitingChapterEnd();
}

void ASnarehavenCharacter::HandleChapterEndClosed()
{
	if (StoryPopupQueue.TakeDialogueAfterChapterEnd())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Story] 장 끝 카드를 닫았다 — 미룬 대사를 잇는다"));
		ShowStoryDialogueIfNew(SnareStory::GetKnownCurrentStep());
	}
}

void ASnarehavenCharacter::OpenChapterCard(int32 Chapter, bool bPersistSeen)
{
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(PlayerController) || (IsValid(StoryChapterCardHud) && !StoryChapterCardHud->IsFinished()))
	{
		return;
	}
	StoryChapterCardHud = CreateWidget<USnareStoryChapterCardWidget>(PlayerController, USnareStoryChapterCardWidget::StaticClass());
	if (IsValid(StoryChapterCardHud))
	{
		StoryChapterCardHud->bPersistSeen = bPersistSeen;
	}
	if (IsValid(StoryChapterCardHud) && StoryChapterCardHud->ShowChapter(Chapter))
	{
		StoryChapterCardHud->OnClosed.AddUObject(this, &ASnarehavenCharacter::HandleChapterEndClosed);
		StoryChapterCardHud->AddToViewport(9);
		UE_LOG(LogSnarehaven, Log, TEXT("[Story] %d장 끝 카드를 띄웠다"), Chapter);
	}
	else
	{
		StoryChapterCardHud = nullptr;
		HandleChapterEndClosed();  // 카드를 못 띄워도 장 끝은 끝났다 — 미룬 대사가 갇히지 않게
	}
}

bool ASnarehavenCharacter::PlayLighthouseGlance(int32 Chapter, TFunction<void()> OnDone)
{
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	UWorld* const World = GetWorld();
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController() || IsValid(LighthouseGlanceCamera) || World == nullptr)
	{
		return false;
	}
	TActorIterator<ASnareLighthouseActor> Lighthouse(World);
	if (!Lighthouse)
	{
		return false;
	}
	const FVector TowerBase = Lighthouse->GetActorLocation();
	const FTransform Pose = SnareStory::LighthouseGlancePose(TowerBase, GetActorLocation());
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	LighthouseGlanceCamera = World->SpawnActor<ACameraActor>(Pose.GetLocation(), Pose.Rotator(), Params);
	if (!IsValid(LighthouseGlanceCamera))
	{
		LighthouseGlanceCamera = nullptr;
		return false;
	}
	UCameraComponent* const GlanceCam = LighthouseGlanceCamera->GetCameraComponent();
	GlanceCam->SetConstraintAspectRatio(false);  // 폰 가로 비율에 검은 띠가 안 생기게
	GlanceCam->SetFieldOfView(IsValid(TopDownCamera) ? TopDownCamera->FieldOfView : GlanceCam->FieldOfView);
	const float Blend = SnareStory::LighthouseGlanceBlendSeconds;
	PlayerController->SetViewTargetWithBlend(LighthouseGlanceCamera, Blend, VTBlend_Cubic);
	UE_LOG(LogSnarehaven, Log, TEXT("[Story] 장 끝 연출 — 카메라가 등대를 비춘다 (카메라 (%.0f, %.0f, %.0f) · 탑까지 %.0fcm)"),
		Pose.GetLocation().X, Pose.GetLocation().Y, Pose.GetLocation().Z, FVector::Dist2D(Pose.GetLocation(), TowerBase));
	if (Chapter >= SnareStory::TotalSteps / SnareStory::StepsPerChapter)  // 마지막 장 = 25단계 「등대 꼭대기에서 불 켜기」 — 비추는 동안 불이 붙는다
	{
		Lighthouse->StartIgnition(World->GetTimeSeconds());
		UE_LOG(LogSnarehaven, Log, TEXT("[Story] %d장 끝 — 등대에 불이 붙는다"), Chapter);
	}

	FTimerHandle BackTimer;
	GetWorldTimerManager().SetTimer(BackTimer, FTimerDelegate::CreateWeakLambda(this, [this, Blend]()
	{
		if (APlayerController* const Ctrl = Cast<APlayerController>(GetController()))
		{
			Ctrl->SetViewTargetWithBlend(this, Blend, VTBlend_Cubic);
		}
	}), Blend + SnareStory::LighthouseGlanceHoldSeconds, false);
	FTimerHandle DoneTimer;
	GetWorldTimerManager().SetTimer(DoneTimer, FTimerDelegate::CreateWeakLambda(this, [this, OnDone = MoveTemp(OnDone)]()
	{
		if (IsValid(LighthouseGlanceCamera))
		{
			LighthouseGlanceCamera->Destroy();
		}
		LighthouseGlanceCamera = nullptr;
		UE_LOG(LogSnarehaven, Log, TEXT("[Story] 장 끝 연출 끝 — 카메라가 플레이어로 돌아왔다"));
		OnDone();
	}), Blend * 2.f + SnareStory::LighthouseGlanceHoldSeconds, false);
	return true;
}

bool ASnarehavenCharacter::IsStoryArrived(int32 CurrentStep) const
{
	const SnareStory::FArrival Arrival = SnareStory::ArrivalFor(CurrentStep);
	if (Arrival.Kind == SnareStory::EArrival::None)
	{
		return true;
	}
	const FVector Here = GetActorLocation();
	float GiverDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AQuestGiverActor> It(GetWorld()); It; ++It)
	{
		GiverDistance = FMath::Min(GiverDistance, static_cast<float>(FVector::Dist2D(Here, It->GetActorLocation())));
	}
	const FZoneRow* const Region = SnareZones::FindRegionAt(this, Here);
	const float LighthouseDistance = static_cast<float>(FVector2D::Distance(FVector2D(Here), FVector2D(SnareStory::LighthouseSpotX, SnareStory::LighthouseSpotY)));
	const float CampDistance = static_cast<float>(FVector2D::Distance(FVector2D(Here), FVector2D(SnareStory::CampSpotX, SnareStory::CampSpotY)));
	float MollaeDistance = TNumericLimits<float>::Max();
	float HookDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AShopVendorActor> It(GetWorld()); It; ++It)
	{
		// 낚시 상인이 여럿일 수 있어 첫 하나가 아니라 가장 가까운 모래 자리를 본다
		const SnareStory::EStoryNpc Npc = IsValid(*It) ? It->GetStoryNpc() : SnareStory::EStoryNpc::None;
		float& Nearest = Npc == SnareStory::EStoryNpc::Mollae ? MollaeDistance : HookDistance;
		if (Npc == SnareStory::EStoryNpc::Mollae || Npc == SnareStory::EStoryNpc::Hook)
		{
			Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist2D(Here, It->GetActorLocation())));
		}
	}
	return SnareStory::IsArrived(Arrival, GiverDistance, Region != nullptr ? Region->ZoneID : NAME_None, LighthouseDistance, CampDistance,
		Arrival.Kind == SnareStory::EArrival::CagesBroken && ASnareCampCageActor::AllBroken(GetWorld()), MollaeDistance, bLighthouseLightPressed, HookDistance);
}

bool ASnarehavenCharacter::CanLightLighthouse() const
{
	// 도착 판정(`IsStoryArrived`)과 같은 탑 자리 — 버튼이 뜨는 곳과 눌러서 도착하는 곳이 갈리지 않게.
	const float LighthouseDistance = static_cast<float>(FVector2D::Distance(FVector2D(GetActorLocation()),
		FVector2D(SnareStory::LighthouseSpotX, SnareStory::LighthouseSpotY)));
	return SnareStory::CanPressLighthouseLight(SnareStory::GetKnownCurrentStep(), LighthouseDistance, bLighthouseLightPressed);
}

void ASnarehavenCharacter::ShowStoryDialogueIfNew(int32 CurrentStep)
{
	if (CurrentStep > 0 && USnareStoryDialogueWidget::ShouldShow(CurrentStep) && !StoryPopupQueue.RequestDialogue(IsChapterEndOpen()))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Story] %d단계 대사 — 장 끝(등대·카드)이 열려 있어 카드를 닫은 뒤로 미룬다"), CurrentStep);
		return;
	}
	if (CurrentStep > 0 && USnareStoryDialogueWidget::ShouldShow(CurrentStep) && !IsStoryArrived(CurrentStep))
	{
		if (PendingStoryArrivalStep != CurrentStep)
		{
			PendingStoryArrivalStep = CurrentStep;
			UE_LOG(LogSnarehaven, Log, TEXT("[Story] %d단계 — 목표 지점에 닿을 때까지 대사를 미룬다 (선 자리 %s)"),
				CurrentStep, *GetActorLocation().ToCompactString());
		}
		APlayerController* const GoController = Cast<APlayerController>(GetController());
		if (StoryGoShownStep != CurrentStep && IsValid(GoController) && !(IsValid(StoryDialogueHud) && !StoryDialogueHud->IsFinished())
			&& !SnareStory::ArrivalDialogue(CurrentStep).IsEmpty())
		{
			// 가기 단계는 시작 때 「가 보게」, 도착 때 발견 대사 (감독 보정 10-19). 이쪽은 저장·보고 없음.
			StoryGoShownStep = CurrentStep;
			USnareStoryDialogueWidget* const GoHud = CreateWidget<USnareStoryDialogueWidget>(GoController, USnareStoryDialogueWidget::StaticClass());
			if (IsValid(GoHud) && GoHud->ShowGoStep(CurrentStep))
			{
				GoHud->OnFinished.AddUObject(this, &ASnarehavenCharacter::HandleStoryDialogueClosed);
				GoHud->AddToViewport(8);
				StoryDialogueHud = GoHud;
				UE_LOG(LogSnarehaven, Log, TEXT("[Story] %d단계 가기 대사를 띄웠다 (선 자리 %s)"), CurrentStep, *GetActorLocation().ToCompactString());
			}
		}
		if (!GetWorldTimerManager().IsTimerActive(StoryArrivalTimer))
		{
			GetWorldTimerManager().SetTimer(StoryArrivalTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				const bool bReading = IsStoryDialogueOpen() || IsChapterEndOpen();  // 장 끝 동안도 발견 대사를 미룬다
				if (PendingStoryArrivalStep > 0 && !bReading && IsStoryArrived(PendingStoryArrivalStep))
				{
					const int32 Step = PendingStoryArrivalStep;
					PendingStoryArrivalStep = 0;
					GetWorldTimerManager().ClearTimer(StoryArrivalTimer);
					UE_LOG(LogSnarehaven, Log, TEXT("[Story] %d단계 목표 지점에 닿았다 (선 자리 %s)"), Step, *GetActorLocation().ToCompactString());
					ShowStoryDialogueIfNew(Step);
				}
			}), 0.5f, true);
		}
		return;
	}
	if (PendingStoryArrivalStep != 0 && PendingStoryArrivalStep != CurrentStep)
	{
		PendingStoryArrivalStep = 0;  // 단계가 넘어갔다
		GetWorldTimerManager().ClearTimer(StoryArrivalTimer);
	}
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	if ((IsValid(StoryDialogueHud) && !StoryDialogueHud->IsFinished()) || !IsValid(PlayerController) || !USnareStoryDialogueWidget::ShouldShow(CurrentStep))
	{
		return;
	}
	StoryDialogueHud = CreateWidget<USnareStoryDialogueWidget>(PlayerController, USnareStoryDialogueWidget::StaticClass());
	if (IsValid(StoryDialogueHud) && StoryDialogueHud->ShowStep(CurrentStep, !SnareStory::ArrivalDialogue(CurrentStep).IsEmpty()))
	{
		StoryDialogueHud->AddToViewport(8);
		StoryDialogueHud->OnFinished.AddWeakLambda(this, [this, CurrentStep]()
		{
			if (IsValid(StoryGoalHud))
			{
				StoryGoalHud->RequestReport(CurrentStep);  // 보고·가기 단계는 대사를 다 읽으면 서버에 알린다 (판정은 서버)
			}
			HandleStoryDialogueClosed();
		});
		UE_LOG(LogSnarehaven, Log, TEXT("[Story] %d단계 대사 판을 띄웠다 (%d줄 · 선 자리 %s)"), CurrentStep,
			StoryDialogueHud->GetLineCount(), *GetActorLocation().ToCompactString());
	}
	else
	{
		StoryDialogueHud = nullptr;
	}
}

void ASnarehavenCharacter::OpenStoryQuestScreen()
{
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(PlayerController))
	{
		return;
	}
	if (!IsValid(StoryQuestScreen))
	{
		StoryQuestScreen = CreateWidget<UQuestScreenWidget>(PlayerController, UQuestScreenWidget::StaticClass());
		if (!IsValid(StoryQuestScreen))
		{
			return;
		}
		StoryQuestScreen->AddToViewport(10);
		UGameInstance* const GameInstance = GetGameInstance();
		StoryQuestScreen->BindToBackend(GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr);
		StoryQuestScreen->SetGiverName(NSLOCTEXT("Snarehaven", "StoryQuestTitle", "섬의 이야기"));
	}
	StoryQuestScreen->OpenOnChainTab();  // 줄기 탭 — 서버 GetQuestChain 을 실제로 묻는다
	UE_LOG(LogSnarehaven, Log, TEXT("[Story] 지금 할 일 줄을 눌러 의뢰 판(줄기 탭)을 열었다"));
}

void ASnarehavenCharacter::BindBaseMessages()
{
	if (BaseMessageHandle.IsValid())
	{
		return;
	}
	if (USnareBaseSubsystem* const Base = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USnareBaseSubsystem>() : nullptr)
	{
		BaseMessageHandle = Base->OnBaseMessage.AddUObject(this, &ASnarehavenCharacter::HandleBaseMessage);
	}
}

void ASnarehavenCharacter::HandleBaseMessage(const FText& Message, bool bError)
{
	const USnareBaseBuildComponent* const BaseBuild = FindComponentByClass<USnareBaseBuildComponent>();
	RouteBaseMessage(Message, bError, BaseBuild != nullptr ? BaseBuild->GetCraftingScreen() : nullptr, HuntHud, RanchHud);
}

bool ASnarehavenCharacter::RouteBaseMessage(const FText& Message, bool bError, const UCraftingScreenWidget* CraftScreen,
	UHuntHudWidget* Hunt, URanchHudWidget* Ranch)
{
	if (Message.IsEmpty() || (IsValid(CraftScreen) && CraftScreen->IsOpen()))
	{
		return false;
	}
	if (IsValid(Hunt))
	{
		if (bError)
		{
			Hunt->ShowErrorBanner(Message);
		}
		else
		{
			Hunt->ShowNoticeBanner(Message);
		}
		return true;
	}
	if (IsValid(Ranch))
	{
		Ranch->ShowNotice(Message);
		return true;
	}
	return false;
}

void ASnarehavenCharacter::CreateRanchHud()
{
	if (IsValid(RanchHud))
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		return;
	}

	// 사냥 HUD 와 **판단이 반대다**: 게임모드를 못 보면 목장 HUD 를 띄우지 않는다.
	// 목장 칸이 없는 맵에 띄우면 `비어 있음` 다섯 줄만 남는다 (M2-06a · DESIGN 3.2).
	const ASnarehavenGameMode* GameMode = GetWorld() != nullptr
		? GetWorld()->GetAuthGameMode<ASnarehavenGameMode>() : nullptr;
	if (GameMode == nullptr || !GameMode->ShouldShowRanchHud())
	{
		return;
	}

	const TSubclassOf<URanchHudWidget> WidgetClass =
		RanchHudClass != nullptr ? RanchHudClass : TSubclassOf<URanchHudWidget>(URanchHudWidget::StaticClass());

	RanchHud = CreateWidget<URanchHudWidget>(PlayerController, WidgetClass);
	if (!IsValid(RanchHud))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("목장 HUD 위젯을 만들지 못했다"));
		return;
	}

	RanchHud->AddToViewport();
	RanchHud->BindToCharacter(this);
	BindBaseMessages();

	// 7일 출석 달력 (M21-05b) — 목장에서 연다. 칸·지급은 서버 답으로만 그린다 (CLAUDE.md 8).
	AttendanceScreen = CreateWidget<UAttendanceScreenWidget>(PlayerController, UAttendanceScreenWidget::StaticClass());
	if (IsValid(AttendanceScreen))
	{
		AttendanceScreen->BindToBackend(GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr);
		AttendanceScreen->AddToViewport(/*ZOrder=*/5);
	}
	// 목장 슬롯 버튼도 같은 이유로 클릭을 삼켜야 한다 (M1-10e (a)).
	ApplyHudInputMode(PlayerController);
	UE_LOG(LogSnarehaven, Log, TEXT("목장 HUD 표시: %s"), *WidgetClass->GetName());
}

void ASnarehavenCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 내가 죽인 동물의 드롭을 받으려고 듣는다 (M1-04). BeginPlay 가 아니라 여기서 묶는 이유:
	// 스폰만 하고 월드를 시작하지 않는 2층 검사에서도 배선이 살아 있어야 한다.
	if (IsValid(MeleeAttack))
	{
		MeleeAttack->OnAnimalKilled.AddDynamic(this, &ASnarehavenCharacter::HandleAnimalKilled);
	}

	// 포획 결과도 같은 자리에서 듣는다 (M1-05).
	if (IsValid(Capture))
	{
		Capture->OnAnimalCaptured.AddDynamic(this, &ASnarehavenCharacter::HandleAnimalCaptured);
		Capture->OnCapturePressed.AddDynamic(this, &ASnarehavenCharacter::HandleCapturePressed);
	}

	// 채집 결과도 같은 자리에서 듣는다 (M1-06). 호스트가 물린 까닭도 같은 길로 온다 (M15-15).
	if (IsValid(Gather))
	{
		Gather->OnGathered.AddDynamic(this, &ASnarehavenCharacter::HandleGathered);
		Gather->OnGatherRejected.AddDynamic(this, &ASnarehavenCharacter::HandleGatherRejected);
	}

	// 체력·스태미나를 표 한 줄로 채운다 (M11-02). BeginPlay 가 아니라 여기인 이유는 위와 같다 —
	// 월드를 시작하지 않는 2층 검사에서도 값이 서 있어야 HUD 게이지를 볼 수 있다.
	// 행이 미정이면 컴포넌트가 스스로 안 켜지고 경고만 남긴다 (Verification.md 7).
	if (IsValid(PlayerVitals) && IsValid(PlayerVitalsTable))
	{
		if (const FPlayerVitalsRow* const Row = PlayerVitalsTable->FindRow<FPlayerVitalsRow>(
				PlayerVitalsRowName, TEXT("ASnarehavenCharacter::PostInitializeComponents")))
		{
			PlayerVitals->InitializeFromRow(*Row);
		}
	}

	// 공격 숫자 넷(데미지·사거리·간격·타격 지연)도 같은 자리에서 표 한 줄로 채운다 (M11-05).
	// **BeginPlay 보다 앞이어야 한다** — 대상 찾기 타이머(간격)와 채집·목장·존의 상호작용 거리(사거리)가
	// 각자 BeginPlay 에서 이 값을 읽어 간다.
	if (IsValid(MeleeAttack) && IsValid(PlayerCombatTable))
	{
		if (const FPlayerCombatRow* const Row = PlayerCombatTable->FindRow<FPlayerCombatRow>(
				PlayerCombatRowName, TEXT("ASnarehavenCharacter::PostInitializeComponents")))
		{
			MeleeAttack->ApplyCombatRow(*Row);
			// M21-09a 올가미 링 — 같은 행의 링 칸. 미정·0 이면 예전처럼 즉시 포획이다.
			if (IsValid(Capture))
			{
				Capture->ApplyCombatRow(*Row);
			}
		}
	}
}

void ASnarehavenCharacter::SetWardrobeCamera(bool bOn, float ScreenX, float BodyScreenHeight)
{
	constexpr float WardrobeCameraPitchDegrees = 20.f;
	if (!IsValid(CameraBoom) || !IsValid(TopDownCamera))
	{
		return;
	}
	if (!bOn)
	{
		if (bWardrobeCameraOn)
		{
			CameraBoom->TargetArmLength = WardrobeCameraSavedArmLength;
			CameraBoom->SocketOffset = WardrobeCameraSavedSocketOffset;
			CameraBoom->SetRelativeRotation(WardrobeCameraSavedRotation);
			bWardrobeCameraOn = false;
			for (const TWeakObjectPtr<UPrimitiveComponent>& Hidden : WardrobeHiddenOccluders)
			{
				if (Hidden.IsValid())
				{
					Hidden->SetVisibility(true);
				}
			}
			WardrobeHiddenOccluders.Reset();
		}
		return;
	}
	if (!bWardrobeCameraOn)
	{
		WardrobeCameraSavedArmLength = CameraBoom->TargetArmLength;
		WardrobeCameraSavedSocketOffset = CameraBoom->SocketOffset;
		WardrobeCameraSavedRotation = CameraBoom->GetRelativeRotation();
		bWardrobeCameraOn = true;
		// 원본 옷장처럼 거의 앞에서 본다 — 위에서 내려다보면 모자만 보인다.
		CameraBoom->SetRelativeRotation(FRotator(-WardrobeCameraPitchDegrees, WardrobeCameraSavedRotation.Yaw, 0.f));
	}
	// 기본 거리에서 몸이 화면 높이의 7.44%(DESIGN 3.3 실측)이고 몫은 거리에 반비례한다 — 그 몫이 BodyScreenHeight 가 되는 거리.
	constexpr float DefaultBodyScreenFraction = 0.0744f;
	const float Fraction = FMath::Clamp(BodyScreenHeight, 0.05f, 0.95f);
	CameraBoom->TargetArmLength = WardrobeCameraSavedArmLength * DefaultBodyScreenFraction / Fraction;
	// 화각은 가로 값을 16:9 기준 세로로 옮겨 고정한다(DESIGN 3.3) — 이 거리에서 화면 높이가 몇 cm 인가.
	const float HalfFovH = FMath::DegreesToRadians(FMath::Max(1.f, TopDownCamera->FieldOfView) * 0.5f);
	const float HalfFovV = FMath::Atan(FMath::Tan(HalfFovH) / (16.f / 9.f));
	const float VisibleHeight = 2.f * CameraBoom->TargetArmLength * FMath::Tan(HalfFovV);
	// 몸이 ScreenX 에 오게 카메라를 오른쪽으로 민다(화면 폭 = 보이는 높이 × 가로세로 비).
	FVector2D ViewSize(16.f, 9.f);
	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->GetViewportSize(ViewSize);
	}
	const float Aspect = ViewSize.Y > 1.f ? ViewSize.X / ViewSize.Y : 16.f / 9.f;
	CameraBoom->SocketOffset = WardrobeCameraSavedSocketOffset + FVector(0.f, (0.5f - ScreenX) * Aspect * VisibleHeight, 0.f);
	HideWardrobeOccluders();
}

void ASnarehavenCharacter::HideWardrobeOccluders()
{
	// 가까이 당긴 옷장 카메라와 몸 사이에 선 것(L_Hunt 시작 자리 가로등과 그 빛기둥)이 몸을 가리고 화면에 점무늬 띠를 남겼다.
	// 팔 끝 = 팔 시작 − 방향 × 길이 + 방향으로 돌린 소켓 밀기(스프링암 계산과 같은 꼴, 충돌 당김은 없다고 본다).
	constexpr float OccluderMarginCm = 30.f;
	/** 이보다 큰 것(땅·바다·하늘)은 가림막이 아니다. */
	constexpr float MaxOccluderExtentCm = 2000.f;
	const FRotator ArmRotation = CameraBoom->GetComponentRotation();
	const FVector CameraLocation = CameraBoom->GetComponentLocation() + CameraBoom->TargetOffset
		- ArmRotation.Vector() * CameraBoom->TargetArmLength + ArmRotation.RotateVector(CameraBoom->SocketOffset);
	const FVector BodyCenter = GetActorLocation();
	const FVector BodyTop = BodyCenter + FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* const Other = *It;
		if (Other == this || Other->IsAttachedTo(this) || Other->IsHidden())
		{
			continue;
		}
		TInlineComponentArray<UPrimitiveComponent*> Parts(Other);
		for (UPrimitiveComponent* const Part : Parts)
		{
			if (!Part->IsVisible() || Part->Bounds.BoxExtent.GetMax() > MaxOccluderExtentCm)
			{
				continue;
			}
			const FBox Box = Part->Bounds.GetBox().ExpandBy(OccluderMarginCm);
			if (FMath::LineBoxIntersection(Box, CameraLocation, BodyCenter, BodyCenter - CameraLocation)
				|| FMath::LineBoxIntersection(Box, CameraLocation, BodyTop, BodyTop - CameraLocation))
			{
				Part->SetVisibility(false);
				WardrobeHiddenOccluders.Add(Part);
			}
		}
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[Wardrobe] 카메라를 가린 부품 %d 개 숨김"), WardrobeHiddenOccluders.Num());
}

void ASnarehavenCharacter::ApplyCameraSettings(float Distance, float PitchDegrees)
{
	if (!IsValid(CameraBoom))
	{
		return;
	}

	// 아직 사람이 안 정한 숫자(-1)는 추측으로 채우지 않는다 (DataDriven.md 3) —
	// 엔진 기본값을 그대로 두고 경고만 남긴다 (Verification.md 7).
	if (Distance < 0.f || PitchDegrees < 0.f)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("카메라 거리·각도가 아직 미정(-1)이라 엔진 기본값으로 둔다 — INBOX 질문 대기"));
		return;
	}

	CameraBoom->TargetArmLength = Distance;
	CameraBoom->SetRelativeRotation(FRotator(-PitchDegrees, 0.f, 0.f));
	if (IsValid(TopDownCamera) && CameraFieldOfViewDegrees > 0.f)
	{
		TopDownCamera->SetFieldOfView(CameraFieldOfViewDegrees);
		// 화각은 **세로** 기준 (레퍼런스 = Unity 카메라 FOV 가 세로다). 세로로 고정하면 4:3 캡처든 19.5:9 폰이든
		// 캐릭터가 화면 높이의 같은 몫이다 (DESIGN 3.3). 엔진 `LocalPlayer` 기본도 세로지만 ini 한 줄에 기대지 않고
		// 카메라가 직접 못 박는다 — 가로 고정으로 바뀌면 넓은 폰에서 캐릭터가 1.6배로 커진다.
		TopDownCamera->bOverrideAspectRatioAxisConstraint = true;
		TopDownCamera->AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV;
	}
}

void ASnarehavenCharacter::UpdateToolCamera(float DeltaSeconds)
{
	const FItemRow* const Tool = IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Tool) : nullptr;
	const float ToolMul = Tool != nullptr && Tool->CameraDistanceMul > 0.f ? Tool->CameraDistanceMul : 1.f;
	CameraZoomCurrent = FMath::FInterpTo(CameraZoomCurrent, CameraZoomTarget, DeltaSeconds, CameraZoomInterpSpeed);
	if (FMath::IsNearlyEqual(CameraZoomCurrent, CameraZoomTarget, 0.001f))
	{
		CameraZoomCurrent = CameraZoomTarget;
	}
	const float InteriorTarget = bInteriorCamera ? InteriorCameraMul : 1.f;
	InteriorCameraCurrent = FMath::FInterpTo(InteriorCameraCurrent, InteriorTarget, DeltaSeconds, InteriorCameraInterpSpeed);
	if (FMath::IsNearlyEqual(InteriorCameraCurrent, InteriorTarget, 0.001f))
	{
		InteriorCameraCurrent = InteriorTarget;
	}
	const float Mul = ToolMul * CameraZoomCurrent * InteriorCameraCurrent;
	if (Mul != AppliedToolCameraMul)
	{
		AppliedToolCameraMul = Mul;
		ApplyCameraSettings(CameraDistance * Mul, CameraPitchDegrees);
	}
}

void ASnarehavenCharacter::SetCameraZoomTarget(float Zoom)
{
	CameraZoomTarget = FMath::Clamp(Zoom, CameraZoomMin, CameraZoomMax);
}

void ASnarehavenCharacter::OnZoomWheelIn()
{
	SetCameraZoomTarget(CameraZoomTarget - CameraZoomWheelStep);
}

void ASnarehavenCharacter::OnZoomWheelOut()
{
	SetCameraZoomTarget(CameraZoomTarget + CameraZoomWheelStep);
}

void ASnarehavenCharacter::OnWorldTouchPressed(ETouchIndex::Type FingerIndex, FVector Location)
{
	if (WorldTouches.IsEmpty())
	{
		bPinchedSinceTouch = false;
	}
	WorldTouches.Add(FingerIndex, FVector2D(Location));
	PinchLastDistancePx = 0.f;
}

void ASnarehavenCharacter::OnWorldTouchMoved(ETouchIndex::Type FingerIndex, FVector Location)
{
	if (!WorldTouches.Contains(FingerIndex))
	{
		return;
	}
	WorldTouches[FingerIndex] = FVector2D(Location);
	if (WorldTouches.Num() != 2)
	{
		PinchLastDistancePx = 0.f;
		return;
	}
	TArray<FVector2D> Points;
	WorldTouches.GenerateValueArray(Points);
	const float Distance = FVector2D::Distance(Points[0], Points[1]);
	// 손가락을 벌리면(거리 커짐) 가까이 — 배율은 거리 비에 반비례.
	if (PinchLastDistancePx > 1.f && Distance > 1.f)
	{
		SetCameraZoomTarget(CameraZoomTarget * PinchLastDistancePx / Distance);
		bPinchedSinceTouch = true;
	}
	PinchLastDistancePx = Distance;
}

void ASnarehavenCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 3층 검사용 스위치. 헤드리스(-game -RenderOffscreen)에는 화면도 손가락도 없어서 동물을 때릴
	// 방법이 아예 없다 — `-SnareAutoAttack` 이 붙으면 내 폰이 공격 버튼을 누른 상태로 시작한다.
	// 사람이 직접 부르는 길(OnAttackStarted)과 같은 함수를 쓴다. `-SnareRoom=`·`-SnareCapture=` 와
	// 같은 검사 전용 스위치라 일반 플레이에는 없다 (docs/checks/unreal.md).
	// 여기 두는 이유: 이 함수는 **로컬 조종 폰이 입력을 받을 때** 불린다 — BeginPlay 때는 게스트 폰에
	// 컨트롤러가 아직 안 붙어 있을 수 있다.
	if (IsValid(MeleeAttack) && SnareCheckSwitch::Param(TEXT("SnareAutoAttack")))
	{
		// `-SnareAttackDelay=12` 면 그만큼 **쫓기만 하다가** 때리기 시작한다. "5타를 다 친 순간 스태미나가
		// 얼마 남나"를 보려면 달리기 소모(DESIGN 3.1 의 5/초)를 먼저 쌓아 둬야 해서 넣었다 — 바로 때리면
		// 5타가 2.4초 만에 끝나 게이지가 거의 가득 찬 화면밖에 안 나온다.
		float AttackDelay = 0.f;
		SnareCheckSwitch::Value(TEXT("SnareAttackDelay="), AttackDelay);
		if (AttackDelay > 0.f)
		{
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 자동 공격 %.2f초 뒤에 시작 (-SnareAttackDelay=)"), AttackDelay);
			FTimerHandle AttackStartHandle;
			GetWorldTimerManager().SetTimer(AttackStartHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 자동 공격 시작 (-SnareAttackDelay= 만큼 기다렸다)"));
				MeleeAttack->StartAttacking();
			}), AttackDelay, false);
		}
		else
		{
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 자동 공격 시작 (-SnareAutoAttack)"));
			MeleeAttack->StartAttacking();
		}

		// 한 번 누름 = 한 번이라(2026-10-04) 누른 채로는 한 대뿐이다 — 공격 간격마다 **다시 누르는 손**을 흉내 낸다.
		// 포획 창(Capturable) 대상이면 안 누른다: 옛 자동 반복이 거기서 멈추던 화면(4타에 서 있는 동물)을 그대로 둔다.
		// `-SnareAttackSeconds=` 로 손을 뗀 뒤에는 다시 누르지 않는다.
		if (const float Tap = MeleeAttack->GetAttackIntervalSeconds(); Tap > 0.f)
		{
			GetWorldTimerManager().SetTimer(AutoAttackTapHandle, this, &ASnarehavenCharacter::RepressHeldAttackOnce,
				Tap, true, AttackDelay + Tap * 1.05f);   // 간격 타이머와 같은 틱에 겹치지 않게 조금 뒤에
		}

		// 프레임별 추적을 같이 켠다 — "휘두르기가 늘어진다"(결함 ③)는 시간 축 증상이라 캡처 한 장으로는
		// 안 보인다. 추적 줄의 **클립 시각**이 0 -> 길이로 도는 주기가 곧 휘두르기 한 번의 길이다.
		bCheckMoveTracing = true;

		// `-SnareAttackSeconds=0.3` 이면 그만큼만 누르고 손을 뗀다 (사람이 버튼에서 손을 떼는 길과
		// 같은 함수). 안 주면 계속 누른 채다 — 그러면 4타 만에 포획 창(DESIGN 3.1)에 들어가 동물이
		// **그 자리에 선다**. 도망가는 동물을 쫓는 화면을 보려면 타격 수를 그 앞에서 끊어야 한다.
		float AttackSeconds = 0.f;
		if (SnareCheckSwitch::Value(TEXT("SnareAttackSeconds="), AttackSeconds) && AttackSeconds > 0.f)
		{
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 자동 공격 %.2f초만 (-SnareAttackSeconds=)"), AttackSeconds);
			FTimerHandle AttackStopHandle;
			GetWorldTimerManager().SetTimer(AttackStopHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 자동 공격 손 뗌 (-SnareAttackSeconds=)"));
				EndAttack();
			}), AttackDelay + AttackSeconds, false);
		}

		// `-SnareAttackRepress=11.9` 면 그 시각에 **손을 뗐다가 다시 누른다**. 한 마리만 쫓으면
		// 4타째에 포획 창이 열려 위 검사용 손이 더 안 누르므로 누르고만
		// 있어서는 5타가 영영 안 나간다 — 감독이 건 합격 조건 "5타 뒤 스태미나"를 찍으려면 재입력이 든다.
		// 사람이 버튼을 다시 누르는 길과 **같은 두 함수**(EndAttack -> BeginAttack)를 쓴다.
		float AttackRepress = 0.f;
		if (SnareCheckSwitch::Value(TEXT("SnareAttackRepress="), AttackRepress) && AttackRepress > 0.f)
		{
			UE_LOG(LogSnarehaven, Log,
				TEXT("검사용 자동 공격 %.2f초에 다시 누름 (-SnareAttackRepress=)"), AttackRepress);
			FTimerHandle AttackRepressHandle;
			GetWorldTimerManager().SetTimer(AttackRepressHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 자동 공격 다시 누름 (-SnareAttackRepress=)"));
				EndAttack();
				BeginAttack();
			}), AttackRepress, false);
		}
	}

	// 가구 직접 탭 (M13-20). 조이스틱·HUD 버튼 위의 누름은 Slate 가 먹어 여기 오지 않는다.
	// 클릭은 공격(IA_Attack)과 같은 버튼이라 먹지 않고 흘려보낸다 — 공격 입력은 그대로다.
	PlayerInputComponent->BindTouch(IE_Released, this, &ASnarehavenCharacter::OnWorldTouchReleased);
	PlayerInputComponent->BindTouch(IE_Pressed, this, &ASnarehavenCharacter::OnWorldTouchPressed);
	PlayerInputComponent->BindTouch(IE_Repeat, this, &ASnarehavenCharacter::OnWorldTouchMoved);
	PlayerInputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ASnarehavenCharacter::OnZoomWheelIn);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ASnarehavenCharacter::OnZoomWheelOut);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ASnarehavenCharacter::OnWorldClickReleased)
		.bConsumeInput = false;

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!IsValid(EnhancedInput) || !IsValid(MoveAction))
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("Enhanced Input 바인딩 실패 — IA_Move 또는 EnhancedInputComponent 없음"));
		return;
	}

	EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASnarehavenCharacter::OnMove);

	if (!IsValid(AttackAction))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("Enhanced Input 바인딩 실패 — IA_Attack 없음"));
		return;
	}

	// 누르는 동안 반복은 컴포넌트의 타이머가 한다 — Triggered 를 매 프레임 받아 세지 않는다.
	EnhancedInput->BindAction(AttackAction, ETriggerEvent::Started, this, &ASnarehavenCharacter::OnAttackStarted);
	EnhancedInput->BindAction(AttackAction, ETriggerEvent::Completed, this, &ASnarehavenCharacter::OnAttackCompleted);
	EnhancedInput->BindAction(AttackAction, ETriggerEvent::Canceled, this, &ASnarehavenCharacter::OnAttackCompleted);

	if (!IsValid(CaptureAction))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("Enhanced Input 바인딩 실패 — IA_Capture 없음"));
		return;
	}

	// 포획은 누르고 있는 동작이 아니라 한 번 누르는 동작이라 Started 만 받는다.
	EnhancedInput->BindAction(CaptureAction, ETriggerEvent::Started, this, &ASnarehavenCharacter::OnCaptureStarted);

	if (!IsValid(PauseAction))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("Enhanced Input 바인딩 실패 — IA_Pause 없음"));
		return;
	}

	// 메뉴도 한 번 누르는 동작이다 — 누르고 있는 동안 열렸다 닫혔다 하면 안 된다.
	EnhancedInput->BindAction(PauseAction, ETriggerEvent::Started, this, &ASnarehavenCharacter::OnPauseStarted);

	// 3층 검사용 스위치. 헤드리스(-game -RenderOffscreen)에는 ESC 를 눌러 줄 손가락이 없어서
	// 메뉴가 뜬 화면을 찍을 방법이 없다 — `-SnarePauseMenu` 가 붙으면 시작하자마자 메뉴를 연다.
	// 사람이 ESC 를 누르는 길(TogglePauseMenu)과 **같은 함수**를 쓴다 (`-SnareAutoAttack` 과 같은 규칙).
	if (SnareCheckSwitch::Param(TEXT("SnarePauseMenu")))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 일시정지 메뉴 열기 (-SnarePauseMenu)"));
		TogglePauseMenu();
		// `-SnareOptions` 까지 붙으면 옵션 버튼을 누른 뒤 화면(소리 크기 카드)까지 연다.
		if (IsValid(PauseMenu) && SnareCheckSwitch::Param(TEXT("SnareOptions")))
		{
			PauseMenu->SetOptionsOpen(true);
			// `-SnareCredits` 까지 붙으면 「만든 사람·라이선스」 칸도 편다 (M98-g).
			PauseMenu->SetCreditsOpen(SnareCheckSwitch::Param(TEXT("SnareCredits")));
		}
		// `-SnarePlayerSafety` 까지 붙으면 「플레이어」 를 누른 길로 신고·차단 판을 열고, 가짜 이웃 둘 중 첫 사람·이유를 고른 채로 둔다(서버 안 부름).
		if (IsValid(PauseMenu) && SnareCheckSwitch::Param(TEXT("SnarePlayerSafety")))
		{
			if (UPlayerSafetyWidget* const Safety = PauseMenu->OpenPlayerSafety())
			{
				Safety->PeerSource = []()
				{
					FGiftPeer A; A.PlayFabId = TEXT("DEMO_A"); A.Name = NSLOCTEXT("Snarehaven", "SafetyDemoPeerA", "솔방울"); A.DistanceCm = 300.f;
					FGiftPeer B; B.PlayFabId = TEXT("DEMO_B"); B.Name = NSLOCTEXT("Snarehaven", "SafetyDemoPeerB", "도토리"); B.DistanceCm = 600.f;
					return TArray<FGiftPeer>{ A, B };
				};
				Safety->Reload();
				Safety->SelectPeer(TEXT("DEMO_A"));
				Safety->SelectReason(TEXT("harass"));
			}
		}
	}

	// 3층 검사용 스위치. 같은 이유로 W/A/S/D 를 눌러 줄 손가락도 없다 —
	// `-SnareMoveKey=W` 가 붙으면 그 키를 누른 채로 시작한다 (M1-10d 의 "이동 전후" 캡처).
	FString MoveKeyName;
	if (SnareCheckSwitch::Value(TEXT("SnareMoveKey="), MoveKeyName))
	{
		StartCheckMove(MoveKeyName);
	}

	// 3층 검사용 스위치. 키보드로는 조이스틱을 "살짝" 밀 수 없어 걷기 상태를 화면으로 볼 길이 없다 —
	// `-SnareMoveScale=0.5` 가 붙으면 그 크기로 민 채로 시작한다 (감독 검증 요구 2026-09-21).
	float MoveScale = 0.f;
	if (SnareCheckSwitch::Value(TEXT("SnareMoveScale="), MoveScale))
	{
		StartCheckMoveAxis(MoveScale);
	}

	// 3층 검사용 스위치. 시작 자리에서는 몸이 카메라 반대(맵 위쪽)를 봐 대기 칸에 뒤통수만 찍힌다 —
	// `-SnareFaceCamera` 는 선 채로 카메라 쪽을 보게 돌리고 0.25초마다 머리 실측 줄을 남긴다
	// (INBOX 결함 12:40 「대기·걷기·달리기 8칸 시트」, 루프#401). 이동 스위치와 같이 주면 이동이 방향을 정한다.
	if (SnareCheckSwitch::Param(TEXT("SnareFaceCamera")) && IsValid(CameraBoom))
	{
		SetActorRotation(FRotator(0.f, CameraBoom->GetComponentRotation().Yaw + 180.f, 0.f));
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 카메라 보기 (-SnareFaceCamera) — 몸 방향 %.1f"), GetActorRotation().Yaw);
		FTimerHandle FaceLogTimerHandle;
		GetWorldTimerManager().SetTimer(FaceLogTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			LogCheckHeadFacing();
		}), 0.25f, true);
	}

	// 3층 검사용 스위치. 위 둘은 방향이 고정이라 **도망가는 동물을 따라가는 화면**을 못 만든다 —
	// `-SnareChase` 는 쫓을 동물 쪽으로 조이스틱을 꽉 민 채로 둔다 (밸런스 패치 검증, 감독 2026-09-23).
	if (SnareCheckSwitch::Param(TEXT("SnareChase")))
	{
		StartCheckChase();
	}

	// 3층 검사용 스위치. 모바일에는 키보드가 없다 — `-SnareTouch=stick|attack|capture|equip[:부위]` 는 화면에
	// **진짜 터치**를 넣어 가상 조이스틱·공격·포획 버튼이 자기 히트테스트로 받는지 본다 (M4-04).
	FString TouchTargetName;
	if (SnareCheckSwitch::Value(TEXT("SnareTouch="), TouchTargetName))
	{
		StartCheckTouch(TouchTargetName);
	}

	// 3층 검사용 스위치. `-SnareAutoAttack` 이 동물을 포획 가능까지 깎아 놓으면
	// `-SnareAutoCapture` 가 그 순간 포획한다 — 헤드리스에는 `포획` 버튼을 누를 마우스가 없다.
	// 포획할 수 있는 때를 미리 계산하지 않고 되묻는다: 공격 몇 대에 임계값에 닿는지는 데이터가 정한다.
	if (SnareCheckSwitch::Param(TEXT("SnareAutoCapture")))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 자동 포획 대기 (-SnareAutoCapture)"));
		GetWorldTimerManager().SetTimer(
			CheckCaptureTimer, this, &ASnarehavenCharacter::TickCheckCapture, CheckCapturePollSeconds, true);
	}

	// 3층 검사용 스위치 (M15-22). `-SnareMeasureDensity` 가 붙으면 몇 초 뒤 화면 안 나무·바위 밀도를 한 번 재서
	// 로그 숫자로 찍는다(`SnareWorldDensity::LogWorldMeasure`). 캡처 픽셀로 재지 않는다.
	if (IsLocallyControlled() && SnareCheckSwitch::Param(TEXT("SnareMeasureDensity")))
	{
		FTimerHandle MeasureTimer;
		GetWorldTimerManager().SetTimer(MeasureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!SnareWorldDensity::LogWorldMeasure(Cast<APlayerController>(GetController())))
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Density] 화면이 없어 못 쟀다 (-SnareMeasureDensity)"));
			}
		}), 5.f, false);
	}

	// 성능 예산 자 (M17-04). `-SnarePerfLog` 가 붙으면 1초마다 프레임 시간·동물 수·드로콜을 로그 한 줄로 찍는다.
	// M21-08b: 내 몸에 입력이 붙는 지금이 「조작 가능」 — 프로세스당 한 번 콜드 스타트 초를 스위치 없이 찍는다.
#if !UE_BUILD_SHIPPING
	if (IsLocallyControlled())
	{
		SnarePerfLog::LogColdStartOnce();
	}
	if (IsLocallyControlled() && SnareCheckSwitch::Param(TEXT("SnarePerfLog")))
	{
		SnarePerfLog::StartLogging(this);
	}
#endif

	// 3층 검사용 스위치. 헤드리스에는 포탈 버튼을 누를 손가락이 없다 — `-SnareTravel` 이 붙으면
	// 포탈이 사거리에 들어오는 순간 한 번 누른다 (`-SnareAutoCapture` 와 같은 규칙: 언제 누를 수
	// 있는지 미리 재지 않고 되묻는다. 판정은 `UZoneTravelComponent::CanTravel()` 하나다).
	if (SnareCheckSwitch::Param(TEXT("SnareTravel")) && !bCheckTravelUsed)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 존 이동 대기 (-SnareTravel)"));
		GetWorldTimerManager().SetTimer(
			CheckTravelTimer, this, &ASnarehavenCharacter::TickCheckTravel, CheckCapturePollSeconds, true);
	}

	// 3층 검사용 스위치 (M5-01e2). 쓰러짐-부활을 눈으로 보려면 죽어야 하는데, 지금 체력이 닳는 길은
	// 탈진뿐이고 달리기 스태미나 소모가 0 이라(DESIGN 3.1 — 스태미나는 수영 전용) 헤드리스에서는
	// 죽을 방법이 없다. `-SnareKillPlayer=<초>` 가 그 시각에 체력을 0 으로 내린다 — 그 뒤로는
	// **사람이 죽었을 때와 완전히 같은 길**(`OnDied` → `HandlePlayerDied` → 쓰러짐)을 탄다.
	// 동물이 때려 죽는 진짜 길은 M5-07d2 몫이고, 이 스위치는 그 뒤에도 시각을 고르는 용도로 남는다.
	float KillDelaySeconds = 0.f;
	if (IsLocallyControlled()
		&& SnareCheckSwitch::Value(TEXT("SnareKillPlayer="), KillDelaySeconds)
		&& KillDelaySeconds > 0.f)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 사망 예약 (-SnareKillPlayer=%.2f초)"), KillDelaySeconds);
		GetWorldTimerManager().SetTimer(
			CheckKillTimer, this, &ASnarehavenCharacter::TickCheckKill, KillDelaySeconds, false);
	}

	// `-SnareHurtPlayer=<양>` — 3초 뒤 한 번 체력을 그만큼 깎는다 (M58-05 불 옆 온기 소리 확인). 죽이지는 않는다 —
	// 동물이 때리는 같은 `ApplyDamage` 길이라, 깎인 채 모닥불 옆에 세워 회복 틱의 진짜 길을 태울 수 있다.
	float HurtAmount = 0.f;
	if (IsLocallyControlled() && SnareCheckSwitch::Value(TEXT("SnareHurtPlayer="), HurtAmount) && HurtAmount > 0.f)
	{
		FTimerHandle HurtTimer;
		GetWorldTimerManager().SetTimer(HurtTimer, FTimerDelegate::CreateWeakLambda(this, [this, HurtAmount]()
		{
			if (IsValid(PlayerVitals))
			{
				PlayerVitals->ApplyDamage(HurtAmount);
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 체력 깎기 (-SnareHurtPlayer=%.1f)"), HurtAmount);
			}
		}), 3.f, false);
	}

	// `-SnareEatPlayer=<초>` — 그 시각에 한 번 웃는 얼굴 신호만 보낸다 (M86-01). 먹기는 서버 응답이 있어야 하는데
	// 오프라인 캡처에선 못 먹으니, 표정 컴포넌트의 `NoteAte` 한 줄만 부른다. 체력·가방·서버 호출 0.
	float EatDelaySeconds = 0.f;
	if (IsLocallyControlled() && SnareCheckSwitch::Value(TEXT("SnareEatPlayer="), EatDelaySeconds)
		&& SnareFaceExpression::IsValidCheckDelay(EatDelaySeconds))
	{
		FTimerHandle EatTimer;
		GetWorldTimerManager().SetTimer(EatTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (IsValid(FaceBlink))
			{
				FaceBlink->NoteAte(1.f);
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 먹기 표정 (-SnareEatPlayer) - 웃음 신호 보냄"));
			}
		}), EatDelaySeconds, false);
	}
}

void ASnarehavenCharacter::TickCheckKill()
{
	if (IsValid(PlayerVitals))
	{
		PlayerVitals->KillForCheck();
	}
}

void ASnarehavenCharacter::TickCheckTravel()
{
	if (!IsValid(ZoneTravel) || !ZoneTravel->CanTravel())
	{
		return;
	}

	// 한 번이면 끝이다 — 떠나고 나면 이 폰도 이 월드도 사라진다.
	GetWorldTimerManager().ClearTimer(CheckTravelTimer);
	bCheckTravelUsed = true;

	const AZonePortalActor* const Portal = ZoneTravel->GetPortalTarget();
	UE_LOG(LogSnarehaven, Log, TEXT("검사용 존 이동 — %s 포탈로 떠난다 (위치 %s)"),
		IsValid(Portal) ? *Portal->GetTargetDisplayName().ToString() : TEXT("(없음)"),
		*GetActorLocation().ToCompactString());

	// 화면 버튼과 같은 함수를 탄다 — 이동 조건을 검사용으로 따로 적지 않는다.
	ZoneTravel->TryTravelToNearest();
}

void ASnarehavenCharacter::TickCheckCapture()
{
	if (!IsValid(Capture))
	{
		return;
	}

	// M21-09a 올가미 링이 돌고 있으면 성공 창 한가운데를 지난 첫 되묻기에 둘째 누름을 한다 (사람과 같은 버튼 함수).
	if (Capture->IsCaptureRingActive())
	{
		const float Ring = Capture->GetCaptureRingSeconds();
		if (Capture->GetCaptureRingElapsed() >= Ring - Capture->GetCaptureWindowFor(Capture->GetCaptureRingTarget()) * 0.5f)
		{
			GetWorldTimerManager().ClearTimer(CheckCaptureTimer);
			Capture->TryCaptureTarget();
		}
		return;
	}

	if (!Capture->CanCapture())
	{
		return;
	}

	// 링이 없으면 한 번이면 끝이다 — 포획한 동물은 사라져 다음 되묻기에 대상이 없다.
	Capture->TryCaptureTarget();
	if (!Capture->IsCaptureRingActive())
	{
		GetWorldTimerManager().ClearTimer(CheckCaptureTimer);
	}
}

void ASnarehavenCharacter::StartCheckMove(const FString& MoveKeyName)
{
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	const FKey MoveKey(*MoveKeyName);
	if (!IsValid(PlayerController) || !MoveKey.IsValid())
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("검사용 이동 실패 — 컨트롤러가 없거나 모르는 키 이름 (-SnareMoveKey=%s)"), *MoveKeyName);
		return;
	}

	// 키를 **그대로** 넣는다 — Enhanced Input 의 액션에 값을 직접 주입하면 IMC_Default 의 키 매핑
	// (M1-10a 의 Swizzle/Negate)을 건너뛰어, 매핑이 깨져 있어도 캡처는 잘 나온다. 그건 검사가 아니다.
	// 키 떼기는 보내지 않는다 — 캡처 시각까지 누른 채로 있어야 움직인 뒤 화면이 찍힌다
	// (프로세스는 캡처가 끝나면 `-SnareCapture=` 가 스스로 닫는다).
	UE_LOG(LogSnarehaven, Log, TEXT("검사용 이동 시작 (-SnareMoveKey=%s) — 시작 위치 %s"),
		*MoveKeyName, *GetActorLocation().ToCompactString());

	bCheckMoveTracing = true;

	// **한 번 누르는 것으로는 안 된다.** 헤드리스에는 창이 없어 시작 직후 포커스가 빠지면서 눌린 키가
	// 씻겨 나간다 — 처음엔 52cm 만 가고 멈췄다(루프#56 `logs/game_loop56b.log`). OS 의 키 반복처럼
	// 매 프레임 다시 눌러 준다. 0.01초 타이머는 프레임마다 한 번 도는 셈이다.
	// 장치는 기본 키보드(내부 번호 0) — `IPlatformInputDeviceMapper` 는 ApplicationCore 모듈이라,
	// 검사용 스위치 하나 때문에 의존성을 늘리지 않는다 (혼자 하는 로컬 플레이라 장치도 하나다).
	// `-SnareMoveKeyDelay=3` 이면 그만큼 기다렸다 누르기 시작한다 — 스페이스바(상황 버튼, INBOX 18:20 ③)처럼
	// 누른 순간 한 번만 먹는 키는, 순간이동 직후 곁 나무가 채집물로 서기 전에 눌리면 휘두르기로 끝난다.
	float PressDelay = 0.f;
	SnareCheckSwitch::Value(TEXT("SnareMoveKeyDelay="), PressDelay);
	FTimerHandle PressTimerHandle;
	GetWorldTimerManager().SetTimer(PressTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this, MoveKey]()
	{
		if (APlayerController* const InputController = Cast<APlayerController>(GetController()))
		{
			// 이미 눌려 있으면 다시 누르지 않는다 — 프레임마다 새 누름을 넣으면 입력이 매번 「시작」 으로 읽혀
			// 걷기가 Idle↔Run 으로 뒤집혔다(루프#850, 걷기 캡처 팔 흔듦이 안 찍힘). 포커스가 빠져 씻긴 경우만 되살린다.
			if (InputController->IsInputKeyDown(MoveKey))
			{
				return;
			}
			InputController->InputKey(FInputKeyEventArgs(
				/*Viewport*/ nullptr, FInputDeviceId::CreateFromInternalId(0), MoveKey, IE_Pressed, FPlatformTime::Cycles64()));
		}
	}), 0.01f, true, PressDelay > 0.f ? PressDelay : -1.f);

	// `-SnareMoveKeyRepeat=<초>`(M28-09 낚시 연타 실측) — 위처럼 누른 채로는 Enhanced Input 이 첫 누름 하나만
	// 「시작」 으로 본다. 그 간격마다 한 번 떼었다 다음 프레임 누름이 다시 들어가게 해 누름 하나 = 상황 버튼 한 번.
	float RepeatSeconds = 0.f;
	if (SnareCheckSwitch::Value(TEXT("SnareMoveKeyRepeat="), RepeatSeconds) && RepeatSeconds > 0.f)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 되풀이 누름 (-SnareMoveKeyRepeat=%.2f) — %s 를 그 간격마다 뗐다 누른다"),
			RepeatSeconds, *MoveKeyName);
		FTimerHandle ReleaseTimerHandle;
		GetWorldTimerManager().SetTimer(ReleaseTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this, MoveKey]()
		{
			if (APlayerController* const InputController = Cast<APlayerController>(GetController()))
			{
				InputController->InputKey(FInputKeyEventArgs(
					/*Viewport*/ nullptr, FInputDeviceId::CreateFromInternalId(0), MoveKey, IE_Released, FPlatformTime::Cycles64()));
			}
		}), RepeatSeconds, true, (PressDelay > 0.f ? PressDelay : 0.f) + RepeatSeconds);
	}

	// 캡처 두 장만 보면 "움직였다" 가 눈대중이다 — 1초마다 위치를 로그에 남겨 숫자로도 남긴다.
	FTimerHandle MoveLogTimerHandle;
	GetWorldTimerManager().SetTimer(MoveLogTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 이동 중 — 위치 %s"), *GetActorLocation().ToCompactString());
		LogCheckHeadFacing();
	}), 1.0f, true);
}

void ASnarehavenCharacter::LogCheckHeadFacing() const
{
	const USkeletalMeshComponent* const Body = GetMesh();
	if (!IsValid(Body) || Body->GetSkeletalMeshAsset() == nullptr || !IsValid(TopDownCamera))
	{
		return;
	}
	const int32 HeadIndex = Body->GetBoneIndex(HeadBoneName);
	if (HeadIndex == INDEX_NONE || !Body->GetComponentSpaceTransforms().IsValidIndex(HeadIndex))
	{
		return;
	}
	const FQuat RefHead = FAnimationRuntime::GetComponentSpaceTransformRefPose(
		Body->GetSkeletalMeshAsset()->GetRefSkeleton(), HeadIndex).GetRotation();
	const FTransform& HeadNow = Body->GetComponentSpaceTransforms()[HeadIndex];
	const FVector FaceLocal = (HeadNow.GetRotation() * RefHead.Inverse()).RotateVector(FVector::YAxisVector);
	const FVector Face = Body->GetComponentTransform().TransformVectorNoScale(FaceLocal).GetSafeNormal();
	const FVector HeadWorld = Body->GetComponentTransform().TransformPosition(HeadNow.GetLocation());
	const FVector ToCamera = (TopDownCamera->GetComponentLocation() - HeadWorld).GetSafeNormal();
	// 몸 앞(참조 자세 +Y)을 월드로 — 메시 상대 회전이 몸 앞을 액터 앞에 맞췄는지도 같이 본다.
	const FVector BodyFront = Body->GetComponentTransform().TransformVectorNoScale(FVector::YAxisVector).GetSafeNormal2D();
	const FVector CameraFlat = ToCamera.GetSafeNormal2D();
	const auto Degrees = [](const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(A | B, -1.f, 1.f)));
	};
	UE_LOG(LogSnarehaven, Log,
		TEXT("[HeadFacing] 얼굴 높이 %.1f° · 카메라 높이 %.1f° · 얼굴-카메라 사이각 %.1f° · 몸 앞-카메라 수평 차 %.1f° · 몸 앞-액터 앞 %.1f° · 메시 상대 회전 %s"),
		FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Face.Z, -1.f, 1.f))),
		FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(ToCamera.Z, -1.f, 1.f))),
		Degrees(Face, ToCamera), Degrees(BodyFront, CameraFlat), Degrees(BodyFront, GetActorForwardVector().GetSafeNormal2D()),
		*Body->GetRelativeRotation().ToCompactString());
	// 2층은 같은 클립·젖힘으로 얼굴이 위를 본다고 했다 — 게임 안에서 도는 클립·몸·젖힘 값이 2층과 같은지 같이 남긴다.
	const UPlayerBodyAnimInstance* const BodyAnim = Cast<UPlayerBodyAnimInstance>(Body->GetAnimInstance());
	UE_LOG(LogSnarehaven, Log, TEXT("[HeadFacing] 몸 %s · 애님 %s · 기본 클립 %s · 행동 클립 %s · 젖힘 %.1f°"),
		*GetNameSafe(Body->GetSkeletalMeshAsset()), *GetNameSafe(Body->GetAnimInstance()),
		BodyAnim != nullptr ? *GetNameSafe(BodyAnim->GetBaseClip()) : TEXT("-"),
		BodyAnim != nullptr ? *GetNameSafe(BodyAnim->GetActionClip()) : TEXT("-"),
		BodyAnim != nullptr ? BodyAnim->GetHeadLiftDegrees() : 0.f);
}

void ASnarehavenCharacter::StartCheckMoveAxis(float Scale)
{
	UE_LOG(LogSnarehaven, Log, TEXT("검사용 이동 시작 (-SnareMoveScale=%.2f) — 시작 위치 %s"),
		Scale, *GetActorLocation().ToCompactString());

	bCheckMoveTracing = true;

	// 이동 입력은 프레임마다 쓰이고 비워진다 — Tick 이 매 프레임 같은 크기로 다시 민다. 조이스틱을 그만큼 잡고 있는 손과 같다.
	// (0.01초 타이머로 넣던 때는 프레임을 건너뛴 틈이 손을 뗀 것으로 처리돼 속도가 0 과 28 을 오가고 걷기 클립이 되감겼다.)
	CheckMoveAxisScale = Scale;

	// 속도를 눈대중이 아니라 숫자로 남긴다 — 0.5초마다 입력 뒤 경과·실제 속도·걷기 클립 재생 위치·이동 설정값.
	// 반만 민 걷기가 가속 첫 순간인지 정상 속도인지 가르려고 경과 시간과 엔진 이동 값을 같이 찍는다(감독 확인 2026-10-19).
	FTimerHandle MoveLogTimerHandle;
	const double StartSeconds = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(MoveLogTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this, StartSeconds]()
	{
		const UCharacterMovementComponent* const Movement = GetCharacterMovement();
		const UPlayerBodyAnimInstance* const BodyAnim = GetBodyAnimInstance();
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 이동 중 — 경과 %.2f초 · 위치 %s · 속도 %.1f · 재생 속도 %.2f · 클립 %s 위치 %.3f초 · MaxWalkSpeed %.1f · MaxSpeed %.1f · MinAnalogWalkSpeed %.1f · MaxAcceleration %.0f · 입력 크기 %.2f"),
			GetWorld()->GetTimeSeconds() - StartSeconds, *GetActorLocation().ToCompactString(), GetVelocity().Size2D(), GetBodyAnimPlayRate(),
			BodyAnim != nullptr ? *GetNameSafe(BodyAnim->GetBaseClip()) : TEXT("-"), BodyAnim != nullptr ? BodyAnim->GetShownClipTime() : -1.f,
			Movement->MaxWalkSpeed, Movement->GetMaxSpeed(), Movement->MinAnalogWalkSpeed, Movement->MaxAcceleration, LastMoveInputSize);
	}), 0.5f, true);
}

void ASnarehavenCharacter::SetCheckChaseTargetForTest(AAnimalCharacter* Target)
{
	CheckChaseTarget = Target;
}

AAnimalCharacter* ASnarehavenCharacter::FindNearestAnimal() const
{
	// 때리고 있는 동물이 있으면 **그 동물**을 쫓는다 — 사냥터는 동물이 빽빽해서, 거리만 보면
	// 도망간 놈 대신 옆에 서 있는 놈이 잡혀 "따라붙었다" 가 거짓이 된다.
	// 한 번 고른 동물을 **끝까지** 쫓는다 (루프#154) — 도망자가 멀어지면 옆 동물이 최근접이 돼
	// 대상이 갈아타고, 그러면 "달리기로 몇 초 만에 좁히나" 를 잰 적 없이 잰 것처럼 보인다.
	if (AAnimalCharacter* const Locked = CheckChaseTarget.Get())
	{
		const UAnimalStateComponent* const LockedState = Locked->GetAnimalState();
		if (IsValid(LockedState) && !LockedState->IsFinished())
		{
			return Locked;
		}
	}

	if (IsValid(MeleeAttack))
	{
		AAnimalCharacter* const Target = MeleeAttack->GetCurrentTarget();
		const UAnimalStateComponent* const TargetState = IsValid(Target) ? Target->GetAnimalState() : nullptr;
		if (IsValid(TargetState) && !TargetState->IsFinished())
		{
			CheckChaseTarget = Target;
			return Target;
		}
	}

	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	// 사거리 제한만 없을 뿐 고르는 규칙은 `UMeleeAttackComponent::FindNearestAnimalInRange` 와 같다.
	AAnimalCharacter* Nearest = nullptr;
	float NearestDistanceSquared = 0.f;
	for (TActorIterator<AAnimalCharacter> It(World); It; ++It)
	{
		AAnimalCharacter* const Animal = *It;
		const UAnimalStateComponent* const State = IsValid(Animal) ? Animal->GetAnimalState() : nullptr;
		if (!IsValid(State) || State->IsFinished())
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(GetActorLocation(), Animal->GetActorLocation());
		if (Nearest == nullptr || DistanceSquared < NearestDistanceSquared)
		{
			Nearest = Animal;
			NearestDistanceSquared = DistanceSquared;
		}
	}

	CheckChaseTarget = Nearest;
	return Nearest;
}

void ASnarehavenCharacter::StartCheckChase()
{
	UE_LOG(LogSnarehaven, Log, TEXT("검사용 추격 시작 (-SnareChase) - 시작 위치 %s"),
		*GetActorLocation().ToCompactString());

	bCheckMoveTracing = true;

	// 미는 크기는 늘 1.0 이라 **달리기**다. 스태미나가 바닥나면 코드가 알아서 걷기로 내린다(DESIGN 3.1) -
	// 검사가 걷기로 바꾸지 않는다. 그걸 검사가 정하면 패치가 노린 것을 검사가 흉내 내는 꼴이 된다.
	//
	// `-SnareChaseDelay=4` 면 그만큼 **가만히 서서 보낸 뒤** 쫓기 시작한다 (루프#154). 도주 속도가
	// 45cm/s 라 걷기(실측 56.7)보다도 느려서, 바로 쫓으면 사람이 한 번도 뒤처지지 않는다 - 그러면
	// "달리기로 몇 초 만에 1타 거리로 좁히나"(DESIGN 3.1 의 0.8배 근거)를 잴 거리가 아예 안 생긴다.
	// 먼저 벌려 놓고 재려고 넣었다.
	float ChaseDelay = 0.f;
	if (SnareCheckSwitch::Value(TEXT("SnareChaseDelay="), ChaseDelay) && ChaseDelay > 0.f)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 추격 %.2f초 뒤에 시작 (-SnareChaseDelay=)"), ChaseDelay);
		FTimerHandle ChaseStartHandle;
		GetWorldTimerManager().SetTimer(ChaseStartHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 추격 시작 (-SnareChaseDelay= 만큼 기다렸다)"));
			bCheckChasing = true;
		}), ChaseDelay, false);
	}
	else
	{
		bCheckChasing = true;
	}

	// 캡처 한 장으로는 "따라붙는다" 를 못 본다 - 1초마다 거리와 스태미나를 같이 남긴다.
	FTimerHandle ChaseLogTimerHandle;
	GetWorldTimerManager().SetTimer(ChaseLogTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		const AAnimalCharacter* const Target = FindNearestAnimal();
		const float DistanceCm = IsValid(Target)
			? FVector::Dist2D(GetActorLocation(), Target->GetActorLocation())
			: -1.f;
		// **누구까지의 거리인지 같이 찍는다** (루프#154). `FindNearestAnimal` 은 공격 타깃이 풀리면
		// 최근접으로 갈아타는데, 이름이 없으면 "쫓아가 좁혔다" 와 "옆 동물로 바뀌었다" 가 로그에서
		// 똑같이 보인다 - 실제로 루프#154 의 첫 측정이 239cm -> 51cm 를 1초 만에 찍어 놓고
		// 그게 접근인지 갈아타기인지 못 가렸다.
		// **상대가 지금 어떤 클립으로 움직이는지도 같이 찍는다** (루프#167). 클립 전환 줄은 바뀌는
		// 순간에만 나와서, 캡처 시각에 무엇이 돌고 있었는지를 캡처 그림으로 우겨야 했다.
		const UAnimSequence* const TargetAnim = IsValid(Target) ? Target->GetCurrentBodyAnim() : nullptr;
		UE_LOG(LogSnarehaven, Log,
			TEXT("검사용 추격 중 - 대상 %s · 거리 %.1fcm · 내 속도 %.1f · 스태미나 %.1f · 체력 %.1f · 달리기 %s · 상대 속도 %.1fcm/s · 상대 클립 %s"),
			IsValid(Target) ? *Target->GetName() : TEXT("없음"),
			DistanceCm, GetVelocity().Size2D(),
			IsValid(PlayerVitals) ? PlayerVitals->GetStamina() : -1.f,
			IsValid(PlayerVitals) ? PlayerVitals->GetHealth() : -1.f,
			IsValid(PlayerVitals) && PlayerVitals->CanRun() ? TEXT("가능") : TEXT("불가(걷기)"),
			IsValid(Target) ? Target->GetVelocity().Size2D() : -1.f,
			IsValid(TargetAnim) ? *TargetAnim->GetName() : TEXT("없음"));
	}), 1.0f, true);
}

void ASnarehavenCharacter::TickCheckChase()
{
	const AAnimalCharacter* const Target = FindNearestAnimal();
	if (!IsValid(Target))
	{
		return;
	}

	// 탑다운 고정 카메라라 Axis.Y 가 월드 +X(앞), Axis.X 가 월드 +Y(오른쪽)다 (`MoveByAxis`).
	const FVector Direction = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	// `-SnareChaseStop=<cm>` — 포획 가능 체력이 된 뒤에는 이 거리까지 뒤로 물러나 선다. 붙어 있으면 위에서 보는
	// 카메라에서 동물이 머리 밑에 가려 캡처가 못 쓴다.
	float StopCm = 0.f;
	const UAnimalStateComponent* const State = Target->GetAnimalState();
	if (SnareCheckSwitch::Value(TEXT("SnareChaseStop="), StopCm) && StopCm > 0.f
		&& State && State->GetCurrentHP() <= State->GetCaptureThresholdHP())
	{
		const float Dist = FVector::Dist2D(Target->GetActorLocation(), GetActorLocation());
		if (Dist < StopCm)
		{
			MoveByAxis(FVector2D(-Direction.Y, -Direction.X));
		}
		return;
	}
	MoveByAxis(FVector2D(Direction.Y, Direction.X));
}

void ASnarehavenCharacter::StartCheckTouch(const FString& TouchTargetName)
{
	CheckTouchTarget = TouchTargetName.ToLower();

	// 터치 인터페이스가 안 떠 있으면 조이스틱은 화면에 없다 — 그걸 모르고 손가락만 넣으면
	// "안 움직였다" 의 원인을 코드에서 찾게 된다. 시작할 때 있는지 없는지를 먼저 적는다.
	UE_LOG(LogSnarehaven, Log, TEXT("검사용 터치 준비 (-SnareTouch=%s) — 터치 인터페이스 %s"),
		*CheckTouchTarget,
		SVirtualJoystick::ShouldDisplayTouchInterface() ? TEXT("있음") : TEXT("없음 — -faketouches 를 붙였는가"));

	// 첫 프레임에는 위젯이 아직 안 그려져 화면 좌표가 없다. 한 박자 뒤부터 되묻는다.
	GetWorldTimerManager().SetTimer(CheckTouchTimer, this, &ASnarehavenCharacter::TickCheckTouch,
		CheckTouchPollSeconds, true, CheckTouchStartSeconds);

	// 화면만 보면 "눌린 것 같다" 가 눈대중이다 — 1초마다 숫자로도 남긴다.
	FTimerHandle TouchLogTimerHandle;
	GetWorldTimerManager().SetTimer(TouchLogTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		const UMeleeAttackComponent* const Melee = GetMeleeAttack();
		UE_LOG(LogSnarehaven, Log,
			TEXT("검사용 터치 중 — 위치 %s · 속도 %.1f · 입력 크기 %.2f · 공격 %s"),
			*GetActorLocation().ToCompactString(), GetVelocity().Size2D(), LastMoveInputSize,
			(IsValid(Melee) && Melee->IsAttacking()) ? TEXT("중") : TEXT("안 함"));
	}), 1.0f, true);
}

void ASnarehavenCharacter::TickCheckTouch()
{
	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("검사용 터치 실패 — Slate 가 없다 (-nullrhi 로 돌렸는가)"));
		GetWorldTimerManager().ClearTimer(CheckTouchTimer);
		return;
	}

	if (CheckTouchTarget == TEXT("stick"))
	{
		FVector2D StickCenter;
		FVector2D FullTilt;
		if (!GetMoveStickOnScreen(StickCenter, FullTilt))
		{
			GetWorldTimerManager().ClearTimer(CheckTouchTimer);
			return;
		}

		if (!bCheckTouchDown)
		{
			SendCheckTouchDown(StickCenter);
			bCheckTouchDown = true;
			UE_LOG(LogSnarehaven, Log,
				TEXT("검사용 터치 — 이동 조이스틱 (%.0f, %.0f) 에 손가락을 놓고 (%.0f, %.0f) 로 끝까지 민다"),
				StickCenter.X, StickCenter.Y, FullTilt.X, FullTilt.Y);
		}

		// 손가락은 끝까지 민 자리에 그대로 있다. 같은 자리로 다시 밀어도 값은 같다 —
		// 헤드리스에서 포커스가 빠져 이벤트가 씻겨도 다음 되묻기가 되살린다 (`-SnareMoveKey=` 와 같은 함정).
		CheckTouchScreenPos = FullTilt;
		SendCheckTouchMove(FullTilt);
		return;
	}

	if (CheckTouchTarget == TEXT("attack"))
	{
		if (bCheckTouchDown)
		{
			// 공격 버튼은 누르고 있는 동안 공격한다(`OnPressed`/`OnReleased`) — 떼지 않고 잡고 있는다.
			return;
		}

		FVector2D ButtonCenter;
		if (!GetHudButtonOnScreen(TEXT("AttackButton"), ButtonCenter))
		{
			return;
		}

		SendCheckTouchDown(ButtonCenter);
		bCheckTouchDown = true;
		CheckTouchScreenPos = ButtonCenter;
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 터치 — 공격 버튼 (%.0f, %.0f) 을 누른 채로 둔다"),
			ButtonCenter.X, ButtonCenter.Y);
		return;
	}

	if (CheckTouchTarget == TEXT("capture"))
	{
		const UHuntHudWidget* const Hud = GetHuntHud();
		if (!IsValid(Hud))
		{
			return;
		}

		if (bCheckTouchDown)
		{
			// 포획 버튼은 `OnClicked` 라 누르고 떼야 한 번이다 (공격 버튼과 다르다).
			SendCheckTouchUp(CheckTouchScreenPos);
			GetWorldTimerManager().ClearTimer(CheckTouchTimer);
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 터치 — 포획 버튼에서 손가락을 뗐다 (%.0f, %.0f)"),
				CheckTouchScreenPos.X, CheckTouchScreenPos.Y);
			return;
		}

		// 포획할 수 있는 때를 미리 계산하지 않고 되묻는다 — 판정은 `UCaptureComponent` 하나뿐이고
		// HUD 가 그걸 그대로 쓴다 (`-SnareAutoCapture` 와 같은 규칙).
		if (!Hud->IsCaptureButtonEnabled())
		{
			return;
		}

		FVector2D ButtonCenter;
		if (!GetHudButtonOnScreen(TEXT("CaptureButton"), ButtonCenter))
		{
			return;
		}

		SendCheckTouchDown(ButtonCenter);
		bCheckTouchDown = true;
		CheckTouchScreenPos = ButtonCenter;
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 터치 — 포획 버튼 (%.0f, %.0f) 을 눌렀다"),
			ButtonCenter.X, ButtonCenter.Y);
		return;
	}

	if (CheckTouchTarget == TEXT("minimap"))
	{
		// 미니맵 전체가 누름 칸이다 — 한 번 눌렀다 떼면 큰 지도가 열린다 (`HandleMinimapClicked`).
		if (bCheckTouchDown)
		{
			SendCheckTouchUp(CheckTouchScreenPos);
			GetWorldTimerManager().ClearTimer(CheckTouchTimer);
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 터치 — 미니맵에서 손가락을 뗐다 (%.0f, %.0f)"),
				CheckTouchScreenPos.X, CheckTouchScreenPos.Y);
			return;
		}

		FVector2D ButtonCenter;
		if (!GetHudButtonOnScreen(TEXT("MinimapTouchButton"), ButtonCenter))
		{
			return;
		}

		SendCheckTouchDown(ButtonCenter);
		bCheckTouchDown = true;
		CheckTouchScreenPos = ButtonCenter;
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 터치 — 미니맵 (%.0f, %.0f) 을 눌렀다"), ButtonCenter.X, ButtonCenter.Y);
		return;
	}

	const EEquipSlot TouchEquipSlot = ParseCheckTouchEquipSlot(CheckTouchTarget);
	if (TouchEquipSlot != EEquipSlot::None)
	{
		const UHuntHudWidget* const Hud = GetHuntHud();
		if (!IsValid(Hud))
		{
			return;
		}

		if (bCheckTouchDown)
		{
			// 장비 버튼도 포획 버튼과 같은 `OnClicked` 다 — 누르고 떼야 한 번이다.
			SendCheckTouchUp(CheckTouchScreenPos);
			GetWorldTimerManager().ClearTimer(CheckTouchTimer);
			UE_LOG(LogSnarehaven, Log,
				TEXT("검사용 터치 — %s 에서 손가락을 뗐다 (%.0f, %.0f) · 지금 그 부위에 든 것 %s"),
				*UHuntHudWidget::GetEquipButtonWidgetName(TouchEquipSlot).ToString(),
				CheckTouchScreenPos.X, CheckTouchScreenPos.Y,
				*Hud->GetEquipButtonItem(TouchEquipSlot).ToString());
			return;
		}

		// 버튼이 떠 있는지도 HUD 에게 묻는다 — 들 장비가 있는지 판단을 여기 옮겨 적지 않는다.
		if (!Hud->IsEquipButtonVisible(TouchEquipSlot))
		{
			return;
		}

		// 짚을 버튼 이름도 HUD 가 준다 (M9-03) — 이름을 여기 적어 두면 한쪽이 바뀔 때 조용히 빗나간다.
		FVector2D ButtonCenter;
		if (!GetHudButtonOnScreen(UHuntHudWidget::GetEquipButtonWidgetName(TouchEquipSlot), ButtonCenter))
		{
			return;
		}

		SendCheckTouchDown(ButtonCenter);
		bCheckTouchDown = true;
		CheckTouchScreenPos = ButtonCenter;
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 터치 — %s (%.0f, %.0f) 을 눌렀다 · 글자 %s"),
			*UHuntHudWidget::GetEquipButtonWidgetName(TouchEquipSlot).ToString(),
			ButtonCenter.X, ButtonCenter.Y, *Hud->GetEquipButtonText(TouchEquipSlot).ToString());
		return;
	}

	UE_LOG(LogSnarehaven, Warning,
		TEXT("검사용 터치 실패 — 모르는 대상 (-SnareTouch=%s), ")
		TEXT("stick·attack·capture·minimap·equip[:tool|top|bottom] 중 하나"),
		*CheckTouchTarget);
	GetWorldTimerManager().ClearTimer(CheckTouchTimer);
}

bool ASnarehavenCharacter::GetMoveStickOnScreen(FVector2D& OutCenter, FVector2D& OutFullTilt) const
{
	const UWorld* const World = GetWorld();
	UGameViewportClient* const ViewportClient = (World != nullptr) ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> ViewportWidget = (ViewportClient != nullptr) ? ViewportClient->GetGameViewportWidget() : nullptr;
	const UInputSettings* const InputSettings = UInputSettings::GetInputSettings();
	if (!ViewportWidget.IsValid() || InputSettings == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("검사용 터치 실패 — 게임 뷰포트가 없다"));
		return false;
	}

	// 자리를 우리가 짐작하지 않는다 — `Config/DefaultInput.ini` 가 가리키는 그 에셋이 자리를 안다.
	UTouchInterface* const TouchInterface = Cast<UTouchInterface>(InputSettings->DefaultTouchInterface.TryLoad());
	if (TouchInterface == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("검사용 터치 실패 — DefaultTouchInterface 를 못 읽었다 (%s)"),
			*InputSettings->DefaultTouchInterface.ToString());
		return false;
	}

	// 이동 스틱은 `IMC_Default` 가 IA_Move 로 받는 키(`Gamepad_LeftX`)를 내보내는 컨트롤이다 —
	// 순서(0번)로 고르면 에셋이 바뀔 때 조용히 오른쪽 스틱을 밀게 된다.
	const FTouchInputControl* const MoveStick = TouchInterface->Controls.FindByPredicate(
		[](const FTouchInputControl& Control) { return Control.MainInputKey == EKeys::Gamepad_LeftX; });
	if (MoveStick == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("검사용 터치 실패 — 터치 인터페이스에 Gamepad_LeftX 스틱이 없다"));
		return false;
	}

	const FGeometry& ViewportGeometry = ViewportWidget->GetCachedGeometry();
	const FVector2D LocalSize(ViewportGeometry.GetLocalSize());
	if (LocalSize.X <= 0.f || LocalSize.Y <= 0.f)
	{
		// 아직 한 번도 안 그려졌다. 다음 되묻기에 다시 본다.
		return false;
	}

	const float ScaleFactor = (ViewportGeometry.GetDrawSize().GetMax() / VirtualJoystickDesiredWidth) / ViewportGeometry.Scale;
	const FVector2D LocalCenter(
		ResolveTouchCoord(MoveStick->Center.X, LocalSize.X, ScaleFactor),
		ResolveTouchCoord(MoveStick->Center.Y, LocalSize.Y, ScaleFactor));
	const FVector2D LocalVisualSize(
		ResolveTouchCoord(MoveStick->VisualSize.X, LocalSize.X, ScaleFactor),
		ResolveTouchCoord(MoveStick->VisualSize.Y, LocalSize.Y, ScaleFactor));

	// 끝까지 민 자리 = 중심에서 스틱 반지름만큼 **아래**(화면 아래 = 카메라 쪽으로 물러난다).
	// 거리를 우리가 정하지 않고 에셋의 스틱 크기에서 가져온다 — 엔진도 이 크기로 입력을 1.0 에 맞춘다.
	const FVector2D LocalFullTilt = LocalCenter + FVector2D(0.f, LocalVisualSize.Y * 0.5f);

	OutCenter = FVector2D(ViewportGeometry.LocalToAbsolute(FVector2f(LocalCenter)));
	OutFullTilt = FVector2D(ViewportGeometry.LocalToAbsolute(FVector2f(LocalFullTilt)));
	return true;
}

bool ASnarehavenCharacter::GetHudButtonOnScreen(FName ButtonName, FVector2D& OutCenter) const
{
	const UHuntHudWidget* const Hud = GetHuntHud();
	if (!IsValid(Hud) || Hud->WidgetTree == nullptr)
	{
		return false;
	}

	const UButton* const Button = Hud->WidgetTree->FindWidget<UButton>(ButtonName);
	if (Button == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("검사용 터치 실패 — HUD 에 %s 가 없다"), *ButtonName.ToString());
		return false;
	}

	const FGeometry& ButtonGeometry = Button->GetCachedGeometry();
	const FVector2D LocalSize(ButtonGeometry.GetLocalSize());
	if (LocalSize.X <= 0.f || LocalSize.Y <= 0.f)
	{
		// 아직 안 그려져 자리가 없다 (0,0 을 누르면 화면 왼쪽 위 아무 데나 누른 셈이 된다).
		return false;
	}

	OutCenter = FVector2D(ButtonGeometry.LocalToAbsolute(FVector2f(LocalSize * 0.5f)));
	return true;
}

void ASnarehavenCharacter::OnPauseStarted(const FInputActionValue& Value)
{
	TogglePauseMenu();
}

void ASnarehavenCharacter::TogglePauseMenu()
{
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		// 남의 폰(서버에 있는 게스트 복제본)에서는 메뉴를 만들지 않는다 — HUD 와 같은 이유.
		return;
	}

	// 로비 화면에는 이미 버튼이 넷 있다 — 그 위에 메뉴가 겹치면 어느 쪽을 눌러야 할지 알 수 없다
	// (감독 지시 2026-09-20 12:05).
	const UWorld* const World = GetWorld();
	if (World == nullptr || !UPauseMenuWidget::ShouldAllowPauseMenu(World->GetMapName()))
	{
		return;
	}

	if (IsValid(PauseMenu) && PauseMenu->IsInViewport())
	{
		PauseMenu->CloseMenu();
		return;
	}

	if (!IsValid(PauseMenu))
	{
		// WBP 가 아직 없어도 메뉴는 떠야 한다 (자리표시 트리는 위젯이 스스로 만든다 — HUD 와 같은 규칙).
		const TSubclassOf<UPauseMenuWidget> WidgetClass = PauseMenuClass != nullptr
			? PauseMenuClass : TSubclassOf<UPauseMenuWidget>(UPauseMenuWidget::StaticClass());
		PauseMenu = CreateWidget<UPauseMenuWidget>(PlayerController, WidgetClass);
		if (!IsValid(PauseMenu))
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("일시정지 메뉴 위젯을 만들지 못했다"));
			return;
		}
	}

	PauseMenu->ShowMenu();
}

void ASnarehavenCharacter::OnWorldTouchReleased(ETouchIndex::Type FingerIndex, FVector Location)
{
	WorldTouches.Remove(FingerIndex);
	PinchLastDistancePx = 0.f;
	if (bPinchedSinceTouch)
	{
		return;
	}
	if (USnareBaseBuildComponent* const BaseBuild = FindComponentByClass<USnareBaseBuildComponent>())
	{
		BaseBuild->TryTapFurniture(FVector2D(Location));
	}
}

void ASnarehavenCharacter::OnWorldClickReleased()
{
	const APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	USnareBaseBuildComponent* const BaseBuild = FindComponentByClass<USnareBaseBuildComponent>();
	float X = 0.f;
	float Y = 0.f;
	if (IsValid(PlayerController) && BaseBuild != nullptr && PlayerController->GetMousePosition(X, Y))
	{
		BaseBuild->TryTapFurniture(FVector2D(X, Y));
	}
}

void ASnarehavenCharacter::OnAttackStarted(const FInputActionValue& Value)
{
	HandleAttackInputStarted();
}

void ASnarehavenCharacter::OnAttackCompleted(const FInputActionValue& Value)
{
	HandleAttackInputCompleted();
}

void ASnarehavenCharacter::HandleAttackInputStarted()
{
	if (!bAttackInputArmed)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[입력] 레벨 진입 때부터 눌려 있던 공격 키라 무시한다"));
		return;
	}

	BeginAttack();
}

void ASnarehavenCharacter::HandleAttackInputCompleted()
{
	// 뗐다 = 이 레벨에서 사람 손가락이 실제로 움직였다. 다음 누름부터는 그대로 받는다.
	bAttackInputArmed = true;
	EndAttack();
}

void ASnarehavenCharacter::DisarmAttackInputUntilRelease()
{
	bAttackInputArmed = false;
	UE_LOG(LogSnarehaven, Log, TEXT("[입력] 잔류 공격 입력 차단 — 키를 뗄 때까지 누름을 무시한다"));
}

namespace
{
	// 과일나무 곁 상황 버튼 (DESIGN 8.18): 열매가 달렸으면 따기, 빈 자리에 심을 과일이 있으면 심기. 아니면 Attack(해당 없음).
	// 나무를 바꾸는 건 서버 답뿐이다 — 여기는 어떤 요청을 보낼지만 고른다(CLAUDE.md 8).
	ESnareInteractVerb PickTreeVerb(const ASnarehavenCharacter* Player, AFruitTreeActor*& OutTree, FName& OutFruit)
	{
		OutTree = AFruitTreeActor::FindInRange(Player);
		OutFruit = NAME_None;
		if (!IsValid(OutTree))
		{
			// 내 나무가 없고 친구 나무 곁이면 「흔들기」 — 내 나무 요청(따기·심기·베기)은 친구 나무에 안 걸린다.
			OutTree = AFruitTreeActor::FindFriendInRange(Player);
			return IsValid(OutTree) ? ESnareInteractVerb::ShakeTree : ESnareInteractVerb::Attack;
		}
		if (OutTree->GetStage() == SnareFruitTree::EStage::Grown && OutTree->GetShownFruitCount() > 0)
		{
			return ESnareInteractVerb::ShakeTree;
		}
		const FItemRow* const ToolRow = IsValid(Player->GetGather()) ? Player->GetGather()->FindEquippedToolRow() : nullptr;
		if (SnareFruitTree::CanChop(OutTree->GetStage(), OutTree->GetShownFruitCount(), ToolRow != nullptr && ToolRow->UseAction == EItemAction::Chop))
		{
			return ESnareInteractVerb::ChopFruitTree;
		}
		const UInventoryComponent* const Bag = Player->GetInventory();
		if (OutTree->GetStage() == SnareFruitTree::EStage::Empty && IsValid(Bag))
		{
			for (const FName Fruit : SnareFruitTree::FruitItemIDs())
			{
				if (Bag->GetItemCount(Fruit) > 0)
				{
					OutFruit = Fruit;
					return ESnareInteractVerb::PlantFruit;
				}
			}
		}
		return ESnareInteractVerb::Attack;
	}
}

void ASnarehavenCharacter::BeginAttack()
{
	// 쓰러져 있는 동안은 때리지도 줍지도 않는다 (위 `MoveByAxis` 와 같은 줄 — DESIGN 3.1).
	if (bDowned || !IsValid(MeleeAttack))
	{
		return;
	}
	bUseInputHeld = true;
	// 전수 표 실측(M28-09) — 누른 순간 버튼에 떠 있던 글자와 손에 든 것. 이 뒤 몸·소리·알림 줄이 결과다.
	if (SnareSound::IsActionLogOn())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[실측] 누름 — 버튼 「%s」 · 무기 %s · 도구 %s"),
			*UHuntHudWidget::GetInteractVerbText(GetInteractVerb()).ToString(),
			IsValid(Equipment) ? *Equipment->GetEquippedItem(EEquipSlot::Weapon).ToString() : TEXT("-"),
			IsValid(Equipment) ? *Equipment->GetEquippedItem(EEquipSlot::Tool).ToString() : TEXT("-"));
	}

	// 상황 버튼 하나가 낚시 연타까지 맡는다 (INBOX 18:20 ② 「그 상황에 맞게 누르면 그게 작동」).
	// 연타는 간격보다 빨라야 해 공격 간격보다 먼저 본다. 따로 있는 연타 버튼과 같은 함수다.
	// 포획은 맡지 않는다 — 포획 버튼이 따로 있어 「포획」이 둘 떴다(감독 PIE 10-06 02:40). 포획 가능 동물
	// 앞에서도 이 버튼은 「공격」이다: 더 때리면 죽는다는 선택지를 남긴다 (감독 결정, DESIGN 3.3).
	if (IsValid(TapFishing) && TapFishing->GetState() == ETapFishingState::Tapping)
	{
		TapFishing->Tap();
		return;
	}
	// 던진 뒤엔 낚시가 버튼을 쥔다 — 휘두르면 낚시 자세가 깨지고 허공만 친다(전수 표 실측 #0507).
	// 입질을 기다리는 동안은 물가 판 「그만두기」 와 같이 거두고, 서버 응답을 기다리는 동안은 아무것도 안 한다.
	if (IsValid(TapFishing) && TapFishing->GetState() != ETapFishingState::Idle)
	{
		if (TapFishing->GetState() == ETapFishingState::WaitingBite)
		{
			TapFishing->Quit();
		}
		return;
	}

	// 공격 간격 안의 누름은 무엇을 하든 버린다 — 채집 도구질도 휘두르기와 같은 박자다 (사용자 2026-10-04
	// 「한 번 클릭, 다시 누를 수 있을 때까지 딜레이」). 휘두르기 길은 `StartAttacking` 이 세고 버린다.
	if (!MeleeAttack->IsActionReady())
	{
		MeleeAttack->StartAttacking();
		return;
	}

	// 버튼 하나가 사냥·채집·목장 슬롯을 같이 받는다 — DESIGN 3.3 의 "자동 상호작용 버튼(근접 오브젝트
	// 자동 하이라이트)" 은 하나뿐이고, 모바일 화면에 상호작용 버튼을 종류마다 늘리지 않으려는 선택이다.
	// 동물이 우선이다: 동물은 반격하고 채집물·슬롯은 도망가지 않는다.
	// 목장 슬롯이 맨 뒤인 이유는 자리 때문이 아니라 사거리가 겹치는 맵이 없기 때문이다 (목장엔 동물이 없다).
	if (MeleeAttack->RefreshTarget() == nullptr)
	{
		// 채집·목장은 한 번 누르면 끝이라 반복 공격 타이머를 켜지 않는다 (DESIGN 3.3 — 진행바 없음, 즉시).
		// 떨어진 가방 줍기도 같은 버튼이다 (DESIGN 8.12).
		if (IsValid(Backpack) && Backpack->TryPickupNearest())
		{
			return;
		}
		const EItemAction GatherSwing = PickGatherSwingBeforePress();
		if (IsValid(Gather) && Gather->TryGatherNearest())
		{
			PlayGatherSwingOnPress(GatherSwing);
			MeleeAttack->MarkActionUsed();
			return;
		}
		// 맞는 도구를 안 든 채 나무·광맥 앞이면 허공만 휘둘렀다 (감독 직접 플레이 INBOX 22:00) —
		// 가방에 있으면 그 도구를 들고 바로 베고, 아예 없으면 무엇이 필요한지 한 줄만 띄우고 휘두르지 않는다.
		if (AGatherableActor* const Need = FindGatherableNeedingOtherTool(); IsValid(Need))
		{
			const FName ToolRow = FindOwnedToolRowFor(Need);
			if (ToolRow.IsNone() || !IsValid(Equipment))
			{
				// 문구는 호스트가 물릴 때와 같은 곳에서 가져온다 (M15-15 — 같은 상황에 두 가지 말이 생기지 않게).
				if (IsValid(HuntHud))
				{
					HuntHud->ShowErrorBanner(UGatherComponent::GetRejectMessage(
						EGatherRejectReason::NeedTool, Need->GetRequiredToolItemID()));
				}
				return;
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[상황 버튼] %s 를 가방에서 들고 바로 쓴다"), *ToolRow.ToString());
			// 게스트는 호스트가 걸어 줘야 손에 들린다 — 이번 누름은 들기만 되고 다음 누름부터 벤다 (DESIGN 3.4 권위).
			Equipment->EquipItem(ToolRow);
			const EItemAction NeedSwing = PickGatherSwingBeforePress();
			if (Gather->TryGatherNearest())
			{
				PlayGatherSwingOnPress(NeedSwing);
				MeleeAttack->MarkActionUsed();
			}
			return;
		}
		if (IsValid(RanchInteract) && RanchInteract->TryInteractNearest())
		{
			return;
		}
		// 과일나무 곁이면 따기·심기 요청을 서버에 보낸다. 요청이 못 나가도(이미 요청 중) 휘두르지 않는다.
		{
			AFruitTreeActor* Tree = nullptr;
			FName Fruit;
			if (const ESnareInteractVerb TreeVerb = PickTreeVerb(this, Tree, Fruit); TreeVerb != ESnareInteractVerb::Attack)
			{
				if (Tree->IsFriendTree())
				{
					const bool bSentFriend = FruitTreeSync::PickFriend(GetWorld(), Tree->GetFriendHostId());
					UE_LOG(LogSnarehaven, Log, TEXT("[상황 버튼] 친구 과일나무 흔들기 — 요청 %s"), bSentFriend ? TEXT("보냄") : TEXT("안 나감"));
					if (bSentFriend)
					{
						PlayItemAction(EItemAction::Gather);
					}
					return;
				}
				const bool bSent = FruitTreeSync::Request(GetWorld(),
					TreeVerb == ESnareInteractVerb::ShakeTree ? FruitTreeSync::EAction::Harvest
						: TreeVerb == ESnareInteractVerb::ChopFruitTree ? FruitTreeSync::EAction::Chop : FruitTreeSync::EAction::Plant,
					Tree->SlotIndex, Fruit);
				UE_LOG(LogSnarehaven, Log, TEXT("[상황 버튼] 과일나무 %d번 %s — 요청 %s"), Tree->SlotIndex,
					TreeVerb == ESnareInteractVerb::ShakeTree ? TEXT("따기") : TreeVerb == ESnareInteractVerb::ChopFruitTree ? TEXT("베기") : TEXT("심기"), bSent ? TEXT("보냄") : TEXT("안 나감"));
				if (bSent)
				{
					// 나무는 서버 답으로 바뀌지만 사람 몸은 요청이 나가는 순간 움직인다 (INBOX 21:58 — 서 있기만 하던 결함).
					// 베기 = 든 도구의 UseAction(없으면 Chop), 따기·심기 = Gather — 다른 채집과 같은 박자.
					const FItemRow* const HeldTool = IsValid(Gather) ? Gather->FindEquippedToolRow() : nullptr;
					const EItemAction ToolAction = (HeldTool != nullptr && HeldTool->UseAction != EItemAction::None) ? HeldTool->UseAction : EItemAction::Chop;
					PlayItemAction(TreeVerb == ESnareInteractVerb::ChopFruitTree ? ToolAction : EItemAction::Gather);
				}
				return;
			}
		}
		// 버튼이 「넣기」·「수확」 이면 요청이 안 쌓여도(넣을 것 없음·요청 중) 휘두르지 않는다 — 글자와 하는 일이
		// 갈리면 빈 칸 앞에서 허공 주먹질이 났다(전수 표 실측 #0512). 무엇이 없는지는 목장 HUD 가 띄운다.
		if (const ESnareInteractVerb Verb = GetInteractVerb(); Verb == ESnareInteractVerb::Place || Verb == ESnareInteractVerb::Harvest)
		{
			return;
		}
		// 낚싯대를 들고 포인트 곁이면 던진다 — 물가 「낚시」 버튼과 같은 함수 (INBOX 18:20 ②).
		if (IsValid(TapFishing) && TapFishing->CanCast())
		{
			TapFishing->CastAtNearest();
			return;
		}
		// 낚싯대를 안 들었으면 가방 것을 들고 던지고, 없으면 알림만 (INBOX 22:00 — 위 나무와 같은 규칙).
		if (IsValid(TapFishing) && TapFishing->GetState() == ETapFishingState::Idle && TapFishing->HasSpotInCastRange())
		{
			const FName RodRow = FindOwnedRodRow();
			if (RodRow.IsNone() || !IsValid(Equipment))
			{
				if (IsValid(HuntHud))
				{
					HuntHud->ShowErrorBanner(NSLOCTEXT("SnarehavenCharacter", "NeedRod", "낚싯대가 있어야 낚을 수 있다"));
				}
				return;
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[상황 버튼] %s 를 가방에서 들고 바로 던진다"), *RodRow.ToString());
			Equipment->EquipItem(RodRow);
			TapFishing->CastAtNearest();   // 게스트는 들린 뒤 다음 누름부터 (`CastAtNearest` 가 `CanCast` 로 다시 본다)
			return;
		}
		// 놓인 작업대·불·보관함 앞이면 그 화면을 연다 (DESIGN 8.10·8.11).
		if (USnareBaseBuildComponent* const BaseBuild = FindComponentByClass<USnareBaseBuildComponent>();
			BaseBuild != nullptr && BaseBuild->TryInteractNearest())
		{
			// 전수 표 실측(M28-09) — 「열기」 누름 뒤 판이 정말 화면에 붙었는지, 어느 쪽·어느 자리로 열렸는지.
			if (SnareSound::IsActionLogOn())
			{
				const UCraftingScreenWidget* const Screen = BaseBuild->GetCraftingScreen();
				UE_LOG(LogSnarehaven, Log, TEXT("[실측] 판 열림 — 가구 판 · 열림 %d · 화면 붙음 %d · 쪽 %s · 자리 %s"),
					IsValid(Screen) && Screen->IsOpen() ? 1 : 0,
					IsValid(Screen) && Screen->IsInViewport() ? 1 : 0,
					IsValid(Screen) ? *UEnum::GetValueAsString(Screen->GetPage()) : TEXT("-"),
					IsValid(Screen) ? *UEnum::GetValueAsString(Screen->GetCraftPlace()) : TEXT("-"));
			}
#if !UE_BUILD_SHIPPING
			// 3층 검사용 스위치 (M28-09 제작 판 행). 레시피 줄은 판 안 버튼이라 상황 버튼 스위치로 못 누른다 —
			// `-SnareCraft=<결과 ItemID>` 가 붙으면 판의 레시피 줄(잠김·재료)을 찍고, 모자란 재료를 오프라인 배낭에
			// 넣은 뒤(`DevGrantOffline`) 줄 버튼이 부르는 **같은 함수** `PressRecipe` 를 부른다. 지급·차감은 그대로 서버다.
			FString CraftItemID;
			if (!bCheckCraftUsed && SnareCheckSwitch::Value(TEXT("SnareCraft="), CraftItemID)
				&& IsValid(BaseBuild->GetCraftingScreen()) && BaseBuild->GetCraftingScreen()->IsOpen())
			{
				bCheckCraftUsed = true;
				USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
					? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
				const FName CraftID(*CraftItemID);
				int32 Granted = 0;
				for (const FSnareRecipeView& View : BaseBuild->GetCraftingScreen()->BuildRecipes())
				{
					FString Ingredients;
					for (const FSnareIngredientView& Ingredient : View.Ingredients)
					{
						Ingredients += FString::Printf(TEXT(" %s %d/%d"), *Ingredient.ItemID.ToString(), Ingredient.Have, Ingredient.Need);
						if (View.ResultItemID == CraftID && !Ingredient.IsEnough() && Backend != nullptr
							&& Backend->DevGrantOffline(Ingredient.ItemID, Ingredient.Need - Ingredient.Have))
						{
							Granted += Ingredient.Need - Ingredient.Have;
						}
					}
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 제작 (-SnareCraft=%s) — 줄 %s · 잠김 %d · 누를 수 %d ·%s"),
						*CraftItemID, *View.ResultItemID.ToString(), View.bLocked ? 1 : 0, View.CanCraft() ? 1 : 0, *Ingredients);
				}
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 제작 (-SnareCraft=%s) — 모자란 재료 %d개를 배낭에 넣었다"), *CraftItemID, Granted);
				FTimerHandle CraftTimer;
				GetWorldTimerManager().SetTimer(CraftTimer, FTimerDelegate::CreateWeakLambda(this, [this, CraftItemID, CraftID]()
				{
					const USnareBaseBuildComponent* const PressBuild = FindComponentByClass<USnareBaseBuildComponent>();
					UCraftingScreenWidget* const PressScreen = IsValid(PressBuild) ? PressBuild->GetCraftingScreen() : nullptr;
					if (!IsValid(PressScreen) || !PressScreen->IsOpen())
					{
						UE_LOG(LogSnarehaven, Log, TEXT("검사용 제작 (-SnareCraft=%s) — 판이 닫혀 못 누름"), *CraftItemID);
						return;
					}
					const TArray<FSnareRecipeView> Views = PressScreen->BuildRecipes();
					const FSnareRecipeView* const View = Views.FindByPredicate(
						[&CraftID](const FSnareRecipeView& Candidate) { return Candidate.ResultItemID == CraftID; });
					const bool bCanPress = View != nullptr && View->CanCraft();
					const bool bAsked = bCanPress && PressScreen->PressRecipe(CraftID);
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 제작 (-SnareCraft=%s) — 줄 %s · 버튼 켜짐 %d · 누름 결과 %s"),
						*CraftItemID, View != nullptr ? TEXT("있음") : TEXT("없음"), bCanPress ? 1 : 0,
						bAsked ? TEXT("서버에 물었다/불에 올렸다") : TEXT("안 나감"));
				}), CheckEatPressSeconds, false);
			}
#endif
			return;
		}
		// 상인·의뢰인 곁이면 판을 편다 — 「공격」 이 떠 허공 주먹질하던 자리(전수 표 실측 #0512, 위 가구와 같은 결).
		// 버튼이 「열기」 인 동안은 화면을 못 올려도(컨트롤러 없음) 휘두르지 않는다 — 글자와 하는 일을 맞춘다.
		if (AShopVendorActor* const Vendor = AShopVendorActor::FindInTalkRange(this); IsValid(Vendor))
		{
			Vendor->OpenScreenFor(this);
			return;
		}
		// 25단계 등대 탑 앞 — 「불 켜기」. 대사는 0.5초 도착 타이머가 이 값을 보고 띄우고, 단계 끝은 대사 뒤 보고를 서버가 정한다.
		if (CanLightLighthouse())
		{
			bLighthouseLightPressed = true;
			UE_LOG(LogSnarehaven, Log, TEXT("[Story] 25단계 — 등대 앞에서 「불 켜기」 를 눌렀다 (선 자리 %s)"), *GetActorLocation().ToCompactString());
			return;
		}
	}
	// 동물 앞이면 나무 앞에서 저절로 든 도구를 내린다 — 도구가 무기 소켓을 차지해 몽둥이가 숨고, 동물도 도구로
	// 쳤다(사람 2026-10-05 「무기도 안 나와」). 무기가 없으면 도구가 그대로 무기 노릇을 한다(`PlayItemAction` 손 줄).
	else if (IsValid(Equipment) && !Equipment->GetEquippedItem(EEquipSlot::Weapon).IsNone()
		&& Equipment->UnequipSlot(EEquipSlot::Tool))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[상황 버튼] 동물 앞 — 도구를 내리고 무기로 친다"));
	}

	MeleeAttack->StartAttacking();
}

void ASnarehavenCharacter::RepressHeldAttackOnce()
{
	// 나무처럼 여러 번 쳐야 하는 채집물 앞이면 공격 중이 아니어도 다시 누른다 (DESIGN 8.10 타격 수 3층).
	const bool bGatherAhead = IsValid(Gather) && IsValid(Gather->RefreshTarget());
	// "누른 채인가"는 손(bUseInputHeld)으로 본다 — 공격 중 표시만 보면, 다시 누른 BeginAttack 이 대상이 없어
	// 다른 가지(상인 판 등)로 빠졌을 때 공격 중이 꺼진 채 남아 손이 영영 안 누르던 결함(맞힌 뒤 2발째 없음).
	if (!IsValid(MeleeAttack) || (!MeleeAttack->IsAttacking() && !bUseInputHeld && !bGatherAhead))
	{
		return;
	}
	const AAnimalCharacter* const Target = MeleeAttack->RefreshTarget();
	const UAnimalStateComponent* const State = IsValid(Target) ? Target->GetAnimalState() : nullptr;
	// 총은 포획 창 동물에게도 쏜다 — 한 발에 체력이 임계 아래로 가면 검사용 손이 멈춰 2발째가 안 나가던 결함(루프#1144).
	// `-SnareFinishOff` 는 포획 창 동물도 마저 때려 죽인다 — 5타째 도살로 재료(꼬리·뿔·고기)를 모으는 스토리 검사용(루프#1273).
	if (IsValid(State) && State->GetState() == EAnimalState::Capturable && !MeleeAttack->IsHoldingGun()
		&& !SnareCheckSwitch::Param(TEXT("SnareFinishOff")))
	{
		return;
	}
	MeleeAttack->StopAttacking();
	BeginAttack();
}

ESnareInteractVerb ASnarehavenCharacter::GetInteractVerb() const
{
	// `BeginAttack` 의 순서를 그대로 따른다 — 글자와 실제로 되는 일이 갈리면 글자가 거짓말이 된다.
	// 대상은 각 컴포넌트의 하이라이트 훑기(0.6초)가 이미 골라 둔 것을 읽기만 한다. 포획은 포획 버튼 몫이다.
	if (IsValid(TapFishing) && TapFishing->GetState() != ETapFishingState::Idle)
	{
		return TapFishing->GetState() == ETapFishingState::WaitingBite ? ESnareInteractVerb::QuitFishing : ESnareInteractVerb::Fish;
	}
	if (!IsValid(MeleeAttack) || IsValid(MeleeAttack->GetCurrentTarget()))
	{
		return ESnareInteractVerb::Attack;
	}
	if (IsValid(Backpack) && Backpack->FindNearestBagInRange(MeleeAttack->GetAttackRangeCm()) != nullptr)
	{
		return ESnareInteractVerb::PickUp;
	}
	if (const AGatherableActor* const Target = IsValid(Gather) ? Gather->GetGatherTarget() : nullptr; IsValid(Target))
	{
		// 몸 동작을 고르는 `HandleGathered` 와 같은 기준 — 맨손이면 채집, 도구면 든 도구 행의 `UseAction`.
		if (Target->GetRequiredToolItemID().IsNone())
		{
			return ESnareInteractVerb::Gather;
		}
		// 행 칸이 비면 `HandleGathered` 처럼 베기다. 바구니(`Gather`)는 도구를 들어도 채집이다.
		const FItemRow* const ToolRow = Gather->FindEquippedToolRow();
		const EItemAction ToolAction = ToolRow != nullptr ? ToolRow->UseAction : EItemAction::None;
		return ToolAction == EItemAction::Mine ? ESnareInteractVerb::Mine
			: ToolAction == EItemAction::Gather ? ESnareInteractVerb::Gather : ESnareInteractVerb::Chop;
	}
	// 맞는 도구를 안 들었으면 누를 때 가방에서 들 도구의 행동으로, 아예 없으면 「…필요」로 (INBOX 22:00).
	if (const AGatherableActor* const Need = FindGatherableNeedingOtherTool(); IsValid(Need))
	{
		const FName ToolRow = FindOwnedToolRowFor(Need);
		const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
		const FItemRow* const Row = !ToolRow.IsNone() && IsValid(ItemTable)
			? ItemTable->FindRow<FItemRow>(ToolRow, TEXT("GetInteractVerb"), false) : nullptr;
		if (Row == nullptr)
		{
			return UGatherComponent::IsMiningTool(Need->GetRequiredToolItemID())
				? ESnareInteractVerb::NeedPickaxe : ESnareInteractVerb::NeedHatchet;
		}
		return Row->UseAction == EItemAction::Mine ? ESnareInteractVerb::Mine
			: Row->UseAction == EItemAction::Gather ? ESnareInteractVerb::Gather : ESnareInteractVerb::Chop;
	}
	if (const ARanchSlotActor* const Slot = IsValid(RanchInteract) ? RanchInteract->GetSlotTarget() : nullptr; IsValid(Slot))
	{
		const URanchComponent* const RanchStation = URanchComponent::FindStation(this, ERanchStationKind::Ranch);
		if (IsValid(RanchStation) && RanchStation->GetSlot(Slot->GetSlotIndex()) != nullptr)
		{
			return RanchStation->IsSlotEmpty(Slot->GetSlotIndex()) ? ESnareInteractVerb::Place : ESnareInteractVerb::Harvest;
		}
	}
	{
		AFruitTreeActor* Tree = nullptr;
		FName Fruit;
		if (const ESnareInteractVerb TreeVerb = PickTreeVerb(this, Tree, Fruit); TreeVerb != ESnareInteractVerb::Attack)
		{
			return TreeVerb;
		}
	}
	if (IsValid(TapFishing) && TapFishing->CanCast())
	{
		return ESnareInteractVerb::Fish;
	}
	if (IsValid(TapFishing) && TapFishing->GetState() == ETapFishingState::Idle && TapFishing->HasSpotInCastRange())
	{
		return FindOwnedRodRow().IsNone() ? ESnareInteractVerb::NeedRod : ESnareInteractVerb::Fish;
	}
	if (const USnareBaseBuildComponent* const BaseBuild = FindComponentByClass<USnareBaseBuildComponent>();
		BaseBuild != nullptr && BaseBuild->HasInteractTarget())
	{
		return ESnareInteractVerb::Open;
	}
	if (AShopVendorActor::FindInTalkRange(this) != nullptr)
	{
		return ESnareInteractVerb::Open;
	}
	if (CanLightLighthouse())
	{
		return ESnareInteractVerb::LightBeacon;
	}
	return ESnareInteractVerb::Attack;
}

AGatherableActor* ASnarehavenCharacter::FindGatherableNeedingOtherTool() const
{
	AGatherableActor* const Need = IsValid(Gather) ? Gather->FindNearestNeedingTool() : nullptr;
	return IsValid(Need) && !Need->GetRequiredToolItemID().IsNone()
		&& !Need->CanGatherWithTool(Gather->GetEquippedToolItemID()) ? Need : nullptr;
}

namespace
{
	// 가진 도구인가 — 든 것도 가진 것으로 센다. 인벤토리 캐시는 백엔드 응답 사이에 비는데(M2-03),
	// 그때 들고 있던 도구가 "없는 도구"가 되면 버튼이 손에 든 것을 못 가리킨다.
	bool OwnsToolRow(const UInventoryComponent* Inventory, const UEquipmentComponent* Equipment, const FItemRow& Row, FName RowName)
	{
		return (IsValid(Inventory) && Inventory->GetItemCount(Row.ItemID) > 0)
			|| (IsValid(Equipment) && Equipment->GetEquippedItem(EEquipSlot::Tool) == RowName);
	}
}

FName ASnarehavenCharacter::FindOwnedToolRowFor(const AGatherableActor* Need) const
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	if (!IsValid(Need) || Need->GetRequiredToolItemID().IsNone() || !IsValid(ItemTable))
	{
		return NAME_None;
	}

	// **맞는지는 채집물이 정한다**(`CanGatherWithTool`) — 손도끼·곡괭이·윗급 도구 목록을 여기 또 적으면
	// 표가 늘 때 한쪽만 늙는다 (DataDriven.md 1).
	FName BonusTool = NAME_None;
	FName RequiredTool = NAME_None;
	FName AlsoTool = NAME_None;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row == nullptr || Row->EquipSlot != EEquipSlot::Tool || !OwnsToolRow(Inventory, Equipment, *Row, Pair.Key)
			|| !Need->CanGatherWithTool(Row->ItemID))
		{
			continue;
		}

		// 덤을 주는 도구(전기톱·전기 곡괭이)가 있으면 그것부터 — 같은 한 번에 하나를 더 준다 (DESIGN 13.5).
		if (Row->ItemID == Need->GetBonusToolItemID())
		{
			BonusTool = Pair.Key;
		}
		else if (Row->ItemID == Need->GetRequiredToolItemID())
		{
			RequiredTool = Pair.Key;
		}
		else if (AlsoTool.IsNone())
		{
			AlsoTool = Pair.Key;
		}
	}
	return !BonusTool.IsNone() ? BonusTool : !RequiredTool.IsNone() ? RequiredTool : AlsoTool;
}

FName ASnarehavenCharacter::FindOwnedRodRow() const
{
	// 어느 것이 낚싯대인지도 표가 정한다(`FishingSpeedMul`).
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	if (!IsValid(ItemTable))
	{
		return NAME_None;
	}
	FName BestRod = NAME_None;
	float BestSpeedMul = 0.f;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row == nullptr || Row->EquipSlot != EEquipSlot::Tool || Row->FishingSpeedMul <= 0.f
			|| !OwnsToolRow(Inventory, Equipment, *Row, Pair.Key))
		{
			continue;
		}
		// 여러 대를 가졌으면 빨리 무는 대부터 — 표 순서(행 맵)는 정해진 순서가 아니라 기준이 못 된다.
		if (Row->FishingSpeedMul > BestSpeedMul)
		{
			BestSpeedMul = Row->FishingSpeedMul;
			BestRod = Pair.Key;
		}
	}
	return BestRod;
}

FName ASnarehavenCharacter::FindContextToolRow() const
{
	// ① 도구를 안 따진 가장 가까운 채집물이 도구를 요구하면 그 도구 — 하이라이트(`GetGatherTarget`)는 맞는 도구를
	// 이미 든 뒤에야 서서, 그것만 보면 손도끼를 영영 건네주지 못한다.
	if (const AGatherableActor* const Near = IsValid(Gather) ? Gather->FindNearestNeedingTool() : nullptr;
		IsValid(Near) && !Near->GetRequiredToolItemID().IsNone())
	{
		if (const FName ToolRow = FindOwnedToolRowFor(Near); !ToolRow.IsNone())
		{
			return ToolRow;
		}
	}
	// ② 던질 거리 안에 낚시 포인트가 있으면 낚싯대.
	if (IsValid(TapFishing) && TapFishing->HasSpotInCastRange())
	{
		return FindOwnedRodRow();
	}
	return NAME_None;
}

void ASnarehavenCharacter::EndAttack()
{
	bUseInputHeld = false;
	if (IsValid(MeleeAttack))
	{
		MeleeAttack->StopAttacking();
	}
}

void ASnarehavenCharacter::OnCaptureStarted(const FInputActionValue& Value)
{
	if (IsValid(Capture))
	{
		Capture->TryCaptureTarget();
	}
}

void ASnarehavenCharacter::OnMove(const FInputActionValue& Value)
{
	MoveByAxis(Value.Get<FVector2D>());
}

void ASnarehavenCharacter::MoveByAxis(const FVector2D& Axis)
{
	// 쓰러져 있는 동안은 입력을 받지 않는다 (DESIGN 3.1 「쓰러짐 → 부활」 · M5-01e2).
	// 누운 몸이 조이스틱을 따라 미끄러지면 쓰러진 것으로 안 보인다.
	if (bDowned)
	{
		return;
	}

	// 얼마나 밀었나를 기억해 둔다 — 걷기·달리기 가르기와 최고 속도가 이 값을 본다 (DESIGN 3.3).
	LastMoveInputSize = FMath::Min(Axis.Size(), 1.f);

	// 스태미나는 **입력**을 보고 닳는다 (DESIGN 3.1) — 속도를 보면 벽에 막혀 제자리걸음하는 동안
	// 소모가 멈춰, 밀고 있는데도 숨이 안 차는 화면이 된다. 달리기 판정은 `IsRunningNow()` 한 곳이다.
	bMoveInputThisFrame = true;
	if (IsValid(PlayerVitals))
	{
		PlayerVitals->SetMoveInput(LastMoveInputSize > 0.f);
		PlayerVitals->SetRunInput(IsRunningNow());
	}

	// 탑다운 고정 카메라(CameraBoom 이 회전을 상속하지 않음)라 입력을 월드 축에 그대로 건다.
	AddMovementInput(FVector::ForwardVector, Axis.Y);
	AddMovementInput(FVector::RightVector, Axis.X);
}

void ASnarehavenCharacter::HandleAnimalKilled(AAnimalCharacter* Target, const TArray<FName>& DroppedItemIDs)
{
	if (DroppedItemIDs.Num() == 0)
	{
		return;
	}

	// 판정은 호스트가 했다. 하지만 **쌓는 건 잡은 사람 쪽이다** (M3-06) — 호스트 프로세스에 있는
	// 게스트 복제본에 쌓아 두면 나중에 백엔드로 보낼 때 누구 계정인지가 흐려진다 (DESIGN 4.1).
	// 호스트가 굴린 목록(`DroppedItemIDs`)은 「드롭이 있는 종인가」만 본다 — 보내는 것은 종 ID 이고, 드롭은 서버가 굴린다 (지시 ①).
	const UAnimalStateComponent* const State = IsValid(Target) ? Target->GetAnimalState() : nullptr;
	const FName AnimalID = State != nullptr ? State->GetAnimalID() : NAME_None;
	if (AnimalID.IsNone())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("사냥 처치 - %s 의 동물 ID 를 몰라 지급 요청을 안 쌓는다"), *GetNameSafe(Target));
		return;
	}
	Client_QueueKillGrant(AnimalID);
}

void ASnarehavenCharacter::Client_ReceiveAnimalHit_Implementation(float Damage)
{
	if (!IsValid(PlayerVitals) || !PlayerVitals->ApplyDamage(Damage))
	{
		return;
	}

	// 클립 길이는 클립에서 읽는다 (DataDriven.md 3) — 클립이 없으면 체력만 깎이고 그림은 없다.
	HitReactSecondsLeft = IsValid(HitAnim) ? HitAnim->GetPlayLength() : 0.f;
	bHitReactRestart = true;
	UE_LOG(LogSnarehaven, Log, TEXT("동물에게 맞았다 - 데미지 %.1f · 남은 체력 %.1f/%.1f"),
		Damage, PlayerVitals->GetHealth(), PlayerVitals->GetMaxHealth());
}

FName ASnarehavenCharacter::GetHurtCue()
{
	return TEXT("PlayerHurt");
}

FName ASnarehavenCharacter::GetDownCue()
{
	return TEXT("PlayerDown");
}

FName ASnarehavenCharacter::GetEatCue()
{
	return TEXT("Eat");
}

FName ASnarehavenCharacter::GetEatPoisonCue()
{
	return TEXT("EatPoison");
}

void ASnarehavenCharacter::PlayEatVoice(float Heal)
{
	// 크기 0.6 = 줍기 `Pickup` 0.7 보다 한 칸 작게 — 귀 바로 앞 2D 라 감쇠가 없다 (DESIGN 2.2).
	// 간격 막이는 없다 — 서버가 받아 준 먹기마다 한 번이다.
	constexpr float EatVoiceVolume = 0.6f;
	const FName Cue = Heal < 0.f ? GetEatPoisonCue() : GetEatCue();
	++BodyVoiceRequestCounts.FindOrAdd(Cue);
	USnareSoundSubsystem::Play2D(this, Cue, EatVoiceVolume);
	if (IsValid(FaceBlink))
	{
		FaceBlink->NoteAte(Heal); // 얼굴 표정 (M85-03)
	}
	PlayEatHaptic(Heal < 0.f ? SnareBodyHaptic::EEat::Poison : SnareBodyHaptic::EEat::Food);
	if (SnareSound::IsActionLogOn())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[실측] 몸 소리 %s · %s · 회복 %.0f"), *Cue.ToString(), *GetName(), Heal);
	}
}

void ASnarehavenCharacter::Multicast_PlayHurtVoice_Implementation()
{
	// 쓰러지는 그 대의 신음은 건너뛴다 — 쓰러짐 소리만 (DESIGN 2.2 「맞음·쓰러짐 겹침」).
	// 맞은 기기는 `Client_ReceiveAnimalHit` 가 먼저 와서 이미 누워 있다.
	if (bDowned)
	{
		return;
	}
	// 크기 0.8 = 같은 순간 동물 덤빔·공격 소리 1.0 아래 (DESIGN 2.2 · 8.7 동물 맞음과 같음).
	constexpr float HurtVoiceVolume = 0.8f;
	PlayBodyVoice(GetHurtCue(), HurtVoiceVolume);
	PlayBodyHaptic(SnareBodyHaptic::EEvent::Hurt);
}

void ASnarehavenCharacter::Server_PlayDownVoice_Implementation()
{
	Multicast_PlayDownVoice();
}

void ASnarehavenCharacter::Multicast_PlayDownVoice_Implementation()
{
	// 크기 1.0 = 그 순간 혼자 나는 알림 (DESIGN 2.2).
	constexpr float DownVoiceVolume = 1.f;
	PlayBodyVoice(GetDownCue(), DownVoiceVolume);
	PlayBodyHaptic(SnareBodyHaptic::EEvent::Down);
}

bool ASnarehavenCharacter::PlayBodyHaptic(SnareBodyHaptic::EEvent Event)
{
	UWorld* const World = GetWorld();
	if (!IsLocallyControlled() || World == nullptr)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	const double* const Last = LastBodyHapticSeconds.Find(static_cast<uint8>(Event));
	// 처음이면 간격이 없다 — 아주 먼 옛날로 본다.
	const float SinceLast = Last != nullptr ? static_cast<float>(Now - *Last) : MAX_flt;
	const SnareBodyHaptic::FCue Cue = SnareBodyHaptic::CueFor(Event, SinceLast);
	if (Cue.IsNone() || !SnareHaptics::Play(Cast<APlayerController>(GetController()), Cue.Intensity, Cue.Seconds))
	{
		return false;
	}
	LastBodyHapticSeconds.Add(static_cast<uint8>(Event), Now);
	return true;
}

bool ASnarehavenCharacter::PlayShotHaptic(SnareBodyHaptic::EGun Gun)
{
	UWorld* const World = GetWorld();
	if (!IsLocallyControlled() || World == nullptr)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	// 처음이면 간격이 없다 — 아주 먼 옛날로 본다.
	const float SinceLast = LastShotHapticSeconds >= 0.0 ? static_cast<float>(Now - LastShotHapticSeconds) : MAX_flt;
	const SnareBodyHaptic::FCue Cue = SnareBodyHaptic::CueFor(Gun, SinceLast);
	if (Cue.IsNone() || !SnareHaptics::Play(Cast<APlayerController>(GetController()), Cue.Intensity, Cue.Seconds))
	{
		return false;
	}
	LastShotHapticSeconds = Now;
	return true;
}

bool ASnarehavenCharacter::PlayStrikeHaptic(SnareBodyHaptic::EStrike Strike)
{
	UWorld* const World = GetWorld();
	if (!IsLocallyControlled() || World == nullptr)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	// 처음이면 간격이 없다 — 아주 먼 옛날로 본다.
	const float SinceLast = LastStrikeHapticSeconds >= 0.0 ? static_cast<float>(Now - LastStrikeHapticSeconds) : MAX_flt;
	const SnareBodyHaptic::FCue Cue = SnareBodyHaptic::CueFor(Strike, SinceLast);
	if (Cue.IsNone() || !SnareHaptics::Play(Cast<APlayerController>(GetController()), Cue.Intensity, Cue.Seconds))
	{
		return false;
	}
	LastStrikeHapticSeconds = Now;
	return true;
}

bool ASnarehavenCharacter::PlayEatHaptic(SnareBodyHaptic::EEat Eat)
{
	UWorld* const World = GetWorld();
	if (!IsLocallyControlled() || World == nullptr)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	// 처음이면 간격이 없다 — 아주 먼 옛날로 본다.
	const float SinceLast = LastEatHapticSeconds >= 0.0 ? static_cast<float>(Now - LastEatHapticSeconds) : MAX_flt;
	const SnareBodyHaptic::FCue Cue = SnareBodyHaptic::CueFor(Eat, SinceLast);
	if (Cue.IsNone() || !SnareHaptics::Play(Cast<APlayerController>(GetController()), Cue.Intensity, Cue.Seconds))
	{
		return false;
	}
	LastEatHapticSeconds = Now;
	return true;
}

bool ASnarehavenCharacter::PlayBodyVoice(FName Cue, float Volume)
{
	// DESIGN 2.2 「같은 사람 맞음 최소 간격」 0.6초 = 8.7 동물 소리 간격 그대로.
	constexpr double MinGapSeconds = 0.6;

	UWorld* const World = GetWorld();
	if (Cue.IsNone() || World == nullptr)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	if (const double* const Last = LastBodyVoiceSeconds.Find(Cue); Last != nullptr && Now - *Last < MinGapSeconds)
	{
		return false;
	}
	LastBodyVoiceSeconds.Add(Cue, Now);
	++BodyVoiceRequestCounts.FindOrAdd(Cue);
	USnareSoundSubsystem::PlayAt(this, Cue, GetActorLocation(), Volume);
	// 3층 실측 줄(M44-06) — 누가 어떤 몸 소리를 냈나. `-SnareActionLog` 일 때만.
	if (SnareSound::IsActionLogOn())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[실측] 몸 소리 %s · %s · %.2f초"), *Cue.ToString(), *GetName(), Now);
	}
	return true;
}

void ASnarehavenCharacter::Client_QueueKillGrant_Implementation(FName AnimalID)
{
	if (!IsValid(Inventory) || !ULootClaimComponent::IsKillLine(AnimalID))
	{
		return;
	}

	// **보유 수량을 여기서 늘리지 않는다.** 인벤토리 원본은 백엔드고, 지급은 백엔드가 확정한 뒤
	// 스냅샷으로 돌아온다 (DESIGN 4.1 권위 표·4.5 · CLAUDE.md 8). 여기서는 「한 마리 잡았다」 요청만 쌓는다.
	Inventory->QueuePendingGrant(AnimalID, 1);

	UE_LOG(LogSnarehaven, Log, TEXT("사냥 처치 %s 1마리를 백엔드 지급 요청 대기열에 넣었다"), *AnimalID.ToString());
}

void ASnarehavenCharacter::HandleAnimalCaptured(AAnimalCharacter* Target, FName PetItemID)
{
	if (PetItemID.IsNone())
	{
		return;
	}

	// 드롭과 같은 길 — 포획한 사람의 클라이언트에서 쌓는다 (M3-06).
	Client_QueuePetGrant(PetItemID);
	// 몸짓은 누를 때 이미 틀었다(`HandleCapturePressed` 올가미) — 여기서 다른 클립을 덮으면 성공 때만 던지기가 끊긴다.
	// 조이는 소리·진동 (M21-03b) — 동물이 사라지기 전 자리에서.
	USkeletalMeshComponent* const PetBody = IsValid(Target) ? Target->GetMesh() : nullptr;
	Multicast_PlayCaptureFeedback(IsValid(Target) ? Target->GetActorLocation() : GetActorLocation(),
		IsValid(PetBody) ? PetBody->GetSkeletalMeshAsset() : nullptr,
		IsValid(PetBody) ? PetBody->GetComponentScale() : FVector::OneVector);
}

void ASnarehavenCharacter::HandleCapturePressed(AAnimalCharacter* Target)
{
	// 올가미 던져 당기기 한 번 (DESIGN 8.8 「포획 던지기」) — 판정 전에 틀어 성공·실패 둘 다 같은 몸짓, `PlayItemAction` 복제 길로 남에게도.
	// 소리는 휘두르기와 같은 휙.
	PlayItemAction(EItemAction::Lasso);
	PlayActionSound(TEXT("Swing"), GetActorLocation(), CaptureThrowCueVolume);
	UE_LOG(LogSnarehaven, Log, TEXT("[Capture] 포획 누름 — 던지기 몸짓·소리 Swing · 대상 %s"),
		IsValid(Target) ? *Target->GetName() : TEXT("없음"));
}

FName ASnarehavenCharacter::GetCaptureCue()
{
	return TEXT("Cinch");
}

void ASnarehavenCharacter::Multicast_PlayCaptureFeedback_Implementation(FVector_NetQuantize Location, USkeletalMesh* PetMesh,
	FVector_NetQuantize100 PetScale)
{
	// 값은 DESIGN 3.1 「포획 순간」 줄. 진동은 잡은 사람 손에서만 — 남의 포획에 내 폰이 떨면 안 된다.
	USnareSoundSubsystem::PlayAt(this, GetCaptureCue(), Location, CaptureCueVolume);
	bLastCaptureVibrated = IsLocallyControlled() && SnareHaptics::PlayLight(Cast<APlayerController>(GetController()));
	++CaptureFeedbackCount;
	// 반짝·펫 날아가기 — 이 기기에서만 사는 연출 액터. 데디 서버처럼 그릴 화면이 없으면 안 띄운다.
	if (UWorld* const World = GetWorld(); World != nullptr && GetNetMode() != NM_DedicatedServer)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ASnareCaptureBurstActor* const Burst = World->SpawnActor<ASnareCaptureBurstActor>(Location, FRotator::ZeroRotator, Params))
		{
			Burst->Start(Location, PetMesh, PetScale, this);
		}
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[Capture] 포획 순간 소리 %s · 진동 %s"),
		*GetCaptureCue().ToString(), bLastCaptureVibrated ? TEXT("켬") : TEXT("없음"));
}

void ASnarehavenCharacter::Client_QueuePetGrant_Implementation(FName PetItemID)
{
	if (!IsValid(Inventory) || PetItemID.IsNone())
	{
		return;
	}

	// 사냥 드롭과 같은 길이다 — **여기서 펫을 보유로 만들지 않는다.** 포획한 클라이언트는
	// "펫 등록" 을 요청할 뿐이고, 목장 슬롯에 적는 건 백엔드다 (DESIGN 4.1·4.5 · CLAUDE.md 8).
	Inventory->QueuePendingGrant(PetItemID, 1);

	UE_LOG(LogSnarehaven, Log, TEXT("포획한 펫 %s 을(를) 백엔드 등록 요청 대기열에 넣었다"), *PetItemID.ToString());
}

void ASnarehavenCharacter::HandleGathered(FName GatherableID, FName ItemID, int32 Count, int32 ToolUses)
{
	if (ItemID.IsNone() || Count <= 0)
	{
		return;
	}

	// 드롭·포획과 같은 길 — 집은 사람의 클라이언트에서 쌓는다 (M3-06).
	Client_QueueGatherGrant(GatherableID, ItemID, Count, ToolUses);

	// 채집 판정은 호스트에서 났다 — 여기서 모두에게 동작을 뿌린다 (DESIGN 8.8).
	// 곡괭이 광맥은 내리찍기(공격 클립), 그 밖의 도구(손도끼·전기톱 나무·마체테 덤불)는 그 행 동작, 맨손은 줍기.
	// 덤(수지 등)은 같은 채집의 두 번째 알림이라 동작을 다시 틀지 않는다(감독 조작 조사 #8).
	if (LastGatherActionFrame == GFrameCounter)
	{
		return;
	}
	LastGatherActionFrame = GFrameCounter;
	// 누른 순간 이미 틀었으면(`PlayGatherSwingOnPress`) 막타 알림이 같은 동작을 다시 틀어 끊지 않는다.
	if (GetWorld() != nullptr && GetWorld()->GetTimeSeconds() - LastGatherPressSwingTime < 0.6)
	{
		return;
	}
	const FName Tool = IsValid(Gather) ? Gather->LastGatherToolItemID() : NAME_None;
	// 도구가 필요한 채집은 든 도구 행의 `UseAction` 을 그대로 튼다(M16-01) — 도구 이름은 가르지 않는다.
	// 행 칸이 비면(None) 옛 동작 그대로 수평 베기.
	const FItemRow* const ToolRow = IsValid(Gather) ? Gather->FindEquippedToolRow() : nullptr;
	const EItemAction Action = Tool.IsNone() ? EItemAction::Gather
		: (ToolRow != nullptr && ToolRow->UseAction != EItemAction::None) ? ToolRow->UseAction : EItemAction::Chop;
	Multicast_PlayItemAction(Action, false);
}

EItemAction ASnarehavenCharacter::PickGatherSwingBeforePress()
{
	// 채집하면 대상이 사라지고 하이라이트가 다음 것으로 넘어가므로, 어떤 동작인지는 누르기 **전에** 고른다.
	// 도구가 필요한 채집은 든 도구 행의 `UseAction`, 맨손 줍기는 Gather (`HandleGathered` 와 같은 고름).
	const AGatherableActor* const Target = IsValid(Gather) ? Gather->RefreshTarget() : nullptr;
	const FName HeldTool = IsValid(Gather) ? Gather->GetEquippedToolItemID() : NAME_None;
	const bool bUsesTool = IsValid(Target) && !UGatherComponent::GetActingToolItemID(
		Target->GetRequiredToolItemID(), Target->GetBonusToolItemID(), HeldTool).IsNone();
	const FItemRow* const ToolRow = IsValid(Gather) ? Gather->FindEquippedToolRow() : nullptr;
	return !bUsesTool ? EItemAction::Gather
		: (ToolRow != nullptr && ToolRow->UseAction != EItemAction::None) ? ToolRow->UseAction : EItemAction::Chop;
}

void ASnarehavenCharacter::PlayGatherSwingOnPress(EItemAction Action)
{
	// 여러 번 쳐야 쓰러지는 나무·광맥은 막타 전까지 `OnGathered` 가 없다 — 누른 매 타마다 여기서 동작을 튼다.
	if (UWorld* const World = GetWorld())
	{
		LastGatherPressSwingTime = World->GetTimeSeconds();
	}
	PlayItemAction(Action);
}

void ASnarehavenCharacter::HandleGatherRejected(const FText& Message)
{
	// 문구는 채집 컴포넌트가 이미 골랐다 — 여기서는 띄우기만 한다 (INBOX ★★★★ ③ "까닭 한 줄").
	// 거절이라 빨간 오류 알약으로 (M26-04).
	if (IsValid(HuntHud))
	{
		HuntHud->ShowErrorBanner(Message);
	}
}

void ASnarehavenCharacter::PlayItemAction(EItemAction Action)
{
	StartItemAction(Action);
	if (HasAuthority())
	{
		Multicast_PlayItemAction(Action, true);
	}
	else
	{
		Server_PlayItemAction(Action);
	}
}

void ASnarehavenCharacter::Server_PlayItemAction_Implementation(EItemAction Action)
{
	Multicast_PlayItemAction(Action, true);
}

void ASnarehavenCharacter::Multicast_PlayItemAction_Implementation(EItemAction Action, bool bOwnerAlreadyPlayed)
{
	if (!(bOwnerAlreadyPlayed && IsLocallyControlled()))
	{
		StartItemAction(Action);
	}
}

void ASnarehavenCharacter::PlayActionSound(FName Cue, const FVector& Location, float Volume)
{
	if (Cue.IsNone())
	{
		return;
	}
	USnareSoundSubsystem::PlayAt(this, Cue, Location, Volume);
	++SoundRequestCount;
	if (HasAuthority())
	{
		Server_PlayActionSound_Implementation(Cue, Location, Volume);
	}
	else
	{
		Server_PlayActionSound(Cue, Location, Volume);
	}
}

void ASnarehavenCharacter::Server_PlayActionSound_Implementation(FName Cue, FVector_NetQuantize Location, float Volume)
{
	++RelayedSoundCount;
	LastRelayedSoundCue = Cue;
	LastRelayedSoundVolume = Volume;
	Multicast_PlayActionSound(Cue, Location, Volume);
}

void ASnarehavenCharacter::Multicast_PlayActionSound_Implementation(FName Cue, FVector_NetQuantize Location, float Volume)
{
	if (!IsLocallyControlled())
	{
		USnareSoundSubsystem::PlayAt(this, Cue, Location, Volume);
	}
}

void ASnarehavenCharacter::TickGaspSound(float DeltaSeconds)
{
	GaspSecondsSinceLast += DeltaSeconds;
	if (!HasAuthority() || !IsValid(PlayerVitals) || PlayerVitals->IsDead())
	{
		return;
	}
	const SnareSound::FChangeCue Gasp = SnareGasp::CueFor(PlayerVitals->IsSwimming(), PlayerVitals->GetStamina(), GaspSecondsSinceLast);
	if (Gasp.Cue.IsNone())
	{
		return;
	}
	GaspSecondsSinceLast = 0.f;
	++GaspPlayCount;
	Multicast_PlayGasp(Gasp.Volume);
}

void ASnarehavenCharacter::Multicast_PlayGasp_Implementation(float Volume)
{
	USnareSoundSubsystem::PlayAt(this, TEXT("Gasp"), GetActorLocation(), Volume);
}

void ASnarehavenCharacter::TickShiverSound(float DeltaSeconds)
{
	ShiverSecondsSinceLast += DeltaSeconds;
	if (!HasAuthority() || !IsValid(PlayerVitals) || PlayerVitals->IsDead())
	{
		return;
	}
	const SnareSound::FChangeCue Shiver = SnareShiver::CueFor(PlayerVitals->GetZoneHealthDrain(),
		PlayerVitals->GetBuffValue(EBuffKind::ColdResist), ShiverSecondsSinceLast);
	if (Shiver.Cue.IsNone())
	{
		return;
	}
	ShiverSecondsSinceLast = 0.f;
	++ShiverPlayCount;
	Multicast_PlayShiver(Shiver.Volume);
}

void ASnarehavenCharacter::Multicast_PlayShiver_Implementation(float Volume)
{
	USnareSoundSubsystem::PlayAt(this, TEXT("Shiver"), GetActorLocation(), Volume);
}

void ASnarehavenCharacter::PlayWarmthIfDue(float HealPerSecond, bool bHealthFull)
{
	if (!IsValid(PlayerVitals) || PlayerVitals->IsDead())
	{
		return;
	}
	const SnareSound::FChangeCue Warmth = SnareWarmth::CueFor(HealPerSecond, bHealthFull, WarmthSecondsSinceLast);
	if (Warmth.Cue.IsNone())
	{
		return;
	}
	WarmthSecondsSinceLast = 0.f;
	++WarmthPlayCount;
	PlayActionSound(Warmth.Cue, GetActorLocation(), Warmth.Volume);
}

void ASnarehavenCharacter::PlayReviveSound(bool bWasDowned)
{
	const SnareSound::FChangeCue Revive = SnareRevive::CueFor(bWasDowned);
	if (Revive.Cue.IsNone())
	{
		return;
	}
	++RevivePlayCount;
	// 서버가 한 번만 쏘고 본인 기기 포함 모든 기기가 한 번씩 낸다 — `PlayActionSound` 는 부른 기기에서 한 번 더
	// 내고 본인 기기는 중계를 건너뛰어, 서버가 게스트 몫으로 부르면 호스트는 겹치고 게스트는 무음이 된다 (M59-03).
	Multicast_PlayReviveSound(Revive.Cue, GetActorLocation(), Revive.Volume);
}

void ASnarehavenCharacter::Multicast_PlayReviveSound_Implementation(FName Cue, FVector_NetQuantize Location, float Volume)
{
	USnareSoundSubsystem::PlayAt(this, Cue, Location, Volume);
	++SoundRequestCount;
}

void ASnarehavenCharacter::TickHeartbeatSound(float DeltaSeconds)
{
	HeartbeatSecondsSinceLast += DeltaSeconds;
	const float Strength = IsValid(PlayerVitals)
		? SnareLowHealth::StateFor(PlayerVitals->GetHealth(), PlayerVitals->GetMaxHealth(), PlayerVitals->IsDead())
		: 0.f;
	if (Strength <= 0.f)
	{
		HeartbeatSecondsSinceLast = 1000.f;   // 다음에 위험해지면 바로 첫 박동
		return;
	}
	if (HeartbeatSecondsSinceLast < SnareLowHealth::BeatInterval(Strength))
	{
		return;
	}
	HeartbeatSecondsSinceLast = 0.f;
	++HeartbeatPlayCount;
	USnareSoundSubsystem::Play2D(this, TEXT("Heartbeat"), SnareLowHealth::HeartbeatVolume);
	PlayBodyHaptic(SnareBodyHaptic::EEvent::Heartbeat);   // 소리와 같은 박자 (M61-04)
}

void ASnarehavenCharacter::ShowEmoteBubble(int32 Index)
{
	if (Index < 0 || Index >= SnareEmoteBubble::Num())
	{
		return;
	}
	Multicast_ShowEmoteBubble_Implementation(static_cast<uint8>(Index));
	++EmoteBubbleRequestCount;
	if (HasAuthority())
	{
		Server_ShowEmoteBubble_Implementation(static_cast<uint8>(Index));
	}
	else
	{
		Server_ShowEmoteBubble(static_cast<uint8>(Index));
	}
}

void ASnarehavenCharacter::Server_ShowEmoteBubble_Implementation(uint8 Index)
{
	if (Index >= SnareEmoteBubble::Num())
	{
		return;
	}
	++RelayedEmoteBubbleCount;
	Multicast_ShowEmoteBubble(Index);
}

void ASnarehavenCharacter::Multicast_ShowEmoteBubble_Implementation(uint8 Index)
{
	const UWorld* const World = GetWorld();
	// 내가 차단한 사람의 말풍선·소리는 내 화면에서 뺀다 (10-16 신고·차단).
	if (Index >= SnareEmoteBubble::Num() || World == nullptr || USnareBlockListSubsystem::IsPawnBlocked(this))
	{
		return;
	}
	EmoteBubbleIndex = Index;
	EmoteBubbleEndSeconds = World->GetTimeSeconds() + SnareEmoteBubble::ShowSeconds;
	UE_LOG(LogSnarehaven, Log, TEXT("[Emote] 말풍선 %d 「%s」 %.1f초"), EmoteBubbleIndex,
		*SnareEmoteBubble::GetGlyph(EmoteBubbleIndex).ToString(), SnareEmoteBubble::ShowSeconds);

	// 소리 (DESIGN 2.2 「말풍선 소리」) — 모든 기기가 이 함수를 받으니 각자 띄운 사람 자리에서 한 번. 연타는 간격 안이면 건너뛴다.
	const SnareSound::FChangeCue Cue = SnareEmoteBubble::CueFor(Index);
	const double Now = World->GetTimeSeconds();
	if (!Cue.Cue.IsNone() && Now - LastEmoteSoundSeconds >= Cue.MinGapSeconds)
	{
		LastEmoteSoundSeconds = Now;
		++EmoteSoundCount;
		USnareSoundSubsystem::PlayAt(this, Cue.Cue, GetActorLocation(), Cue.Volume);
		if (SnareSound::IsActionLogOn())
		{
			UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 말풍선 %s — 크기 %.2f · %s"), *Cue.Cue.ToString(), Cue.Volume, *GetName());
		}
	}
}

int32 ASnarehavenCharacter::GetEmoteBubbleAt(double NowSeconds) const
{
	return NowSeconds < EmoteBubbleEndSeconds ? EmoteBubbleIndex : INDEX_NONE;
}

int32 ASnarehavenCharacter::GetEmoteBubble() const
{
	const UWorld* const World = GetWorld();
	return World != nullptr ? GetEmoteBubbleAt(World->GetTimeSeconds()) : INDEX_NONE;
}

void ASnarehavenCharacter::ShowHealPop(int32 Amount)
{
	const UWorld* const World = GetWorld();
	if (Amount == 0 || World == nullptr)
	{
		return;
	}
	HealPopAmount = Amount;
	HealPopStartSeconds = World->GetTimeSeconds();
	++HealPopCount;
	UE_LOG(LogSnarehaven, Log, TEXT("[Eat] 머리 위 %+d %.1f초"), HealPopAmount, HealPopSeconds);
}

float ASnarehavenCharacter::GetHealPopProgressAt(double NowSeconds) const
{
	if (HealPopStartSeconds < 0.0 || NowSeconds < HealPopStartSeconds || NowSeconds >= HealPopStartSeconds + HealPopSeconds)
	{
		return -1.f;
	}
	return static_cast<float>((NowSeconds - HealPopStartSeconds) / HealPopSeconds);
}

float ASnarehavenCharacter::GetHealPopProgress() const
{
	const UWorld* const World = GetWorld();
	return World != nullptr ? GetHealPopProgressAt(World->GetTimeSeconds()) : -1.f;
}

bool ASnarehavenCharacter::IsLoopingItemAction(EItemAction Action)
{
	return Action == EItemAction::Dance || Action == EItemAction::Sit;
}

void ASnarehavenCharacter::StartItemAction(EItemAction Action)
{
	UAnimSequence* Anim = AttackAnim.Get();
	switch (Action)
	{
	case EItemAction::Eat: Anim = EatAnim.Get(); break;
	case EItemAction::Gather: Anim = GatherAnim.Get(); break;
	case EItemAction::Chop: Anim = IsValid(ChopAnim) ? ChopAnim.Get() : Anim; break;
	case EItemAction::Wave: Anim = WaveAnim.Get(); break;
	case EItemAction::Cheer: Anim = CheerAnim.Get(); break;
	case EItemAction::Dance: Anim = DanceAnim.Get(); break;
	case EItemAction::Sit: Anim = BoatSitAnim.Get(); break;
	case EItemAction::Lasso: Anim = FishCastAnim.Get(); break;  // 낚시 던지기 = 한 팔 던졌다 당겨 옴 (DESIGN 8.8 「포획 던지기」)
	case EItemAction::Smash: break;  // 공격 클립(위에서 내려치기) 그대로 — 손도끼
	default: break;
	}
	ItemActionPlayRate = 1.f;
	const bool bToolWork = Action == EItemAction::Chop || Action == EItemAction::Mine || Action == EItemAction::Smash;
	if (bToolWork)
	{
		// 도구질은 든 도구 행의 쓰기 클립으로 (M18-11 — 손도끼 내려찍기·곡괭이 두 손·전기톱 베기·마체테 베기).
		// 한 번을 공격 간격에 맞춰 돌린다 — 옛 ChopAnim(0.6초)과 같은 박자라 채집 손맛이 그대로다.
		UAnimSequence* Hold = nullptr;
		UAnimSequence* Use = nullptr;
		UAnimSequence* UseAlt = nullptr;
		ResolveHeldBodyClips(Hold, Use, UseAlt);
		// 「정해진 길이」 행이면 간격 = 클립 길이라 재생 속도 1 (전기톱·고급/전기 곡괭이 1.5초).
		const float Interval = IsValid(MeleeAttack) ? MeleeAttack->GetActionLengthSeconds() : 0.f;
		if (IsValid(Use) && Use->GetPlayLength() > KINDA_SMALL_NUMBER)
		{
			Anim = Use;
			ItemActionPlayRate = Interval > KINDA_SMALL_NUMBER ? Use->GetPlayLength() / Interval : 1.f;
		}
	}
	ItemActionAnim = Anim;
	// 어느 동작을 골랐는지 남긴다 — 클립이 겹치는 행동(광맥 = 공격 클립)을 2층 검사가 가르는 값이다 (M15-12).
	LastItemAction = Action;
	++ItemActionCount;
	ItemActionSecondsLeft = IsValid(Anim) ? Anim->GetPlayLength() / ItemActionPlayRate : 0.f;
	// 도구질 동안 모터 도구(전기톱)가 세게 떤다 — 공회전은 장비 컴포넌트가 알아서 약하게 (M18-06).
	if (IsValid(Equipment) && bToolWork)
	{
		Equipment->SetToolWorkSeconds(ItemActionSecondsLeft);
	}
	bItemActionRestart = true;
	bItemActionLoop = IsLoopingItemAction(Action);
	UE_LOG(LogSnarehaven, Log, TEXT("아이템 동작 %s - %s %.2f초"), *UEnum::GetValueAsString(Action),
		IsValid(Anim) ? *Anim->GetName() : TEXT("클립 없음"), ItemActionSecondsLeft);
}

void ASnarehavenCharacter::Client_QueueGatherGrant_Implementation(FName GatherableID, FName ItemID, int32 Count, int32 ToolUses)
{
	if (!IsValid(Inventory) || ItemID.IsNone() || Count <= 0)
	{
		return;
	}

	// 사냥 드롭·포획과 같은 길이다 — **보유 수량을 여기서 늘리지 않는다.** 채집물도 인벤토리 원본은
	// 백엔드고, 지급은 백엔드가 확정한 뒤 스냅샷으로 돌아온다 (DESIGN 4.5 · CLAUDE.md 8).
	Inventory->QueuePendingGrant(ItemID, Count, ToolUses);

	UE_LOG(LogSnarehaven, Log, TEXT("채집한 %s 의 %s %d개를 백엔드 지급 요청 대기열에 넣었다"),
		*GatherableID.ToString(), *ItemID.ToString(), Count);
}
