#include "UI/HuntHudWidget.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Containers/Ticker.h"
#include "SnarehavenCharacter.h"
#include "Animals/AnimalCharacter.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Combat/CaptureComponent.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/Button.h"
#include "Components/CapsuleComponent.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/ItemRow.h"
#include "Data/UsageHintRow.h"
#include "Inventory/LootClaimComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Gathering/GatherableActor.h"
#include "Gathering/GatherComponent.h"
#include "Gathering/TapFishingComponent.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "TimerManager.h"
#include "World/SeaActor.h"
#include "World/SnareSound.h"
#include "UI/SnareNoticeCards.h"
#include "World/SnareZoneRules.h"
#include "World/ZonePortalActor.h"
#include "World/ZoneTravelComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Player/PlayerVitalsComponent.h"
#include "Player/SnareHaptics.h"
#include "Player/SnareLowHealth.h"
#include "UI/SnareBalanceHud.h"
#include "UI/SnareCaptureRingWidget.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UI/SnareMinimapHud.h"
#include "UI/SnareNoticeText.h"
#include "Data/OutpostRow.h"
#include "World/DangerZoneActor.h"
#include "World/LootChestActor.h"
#include "UI/SnareUiStyle.h"
#include "UI/SnareUsageHints.h"
#include "UI/SnareZoneTravelHud.h"
#include "UI/WorldMapScreenWidget.h"
#include "World/ShopVendorActor.h"
#include "Backend/SnareStory.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 아래는 전부 **자리표시 UI 배치**다 (Autonomy.md 🟡 — UI 배치는 가정하고 진행).
	// 게임 규칙 숫자가 아니라 "화면 어디에 얼마나 크게" 라서 설계서 값이 아니다.
	// 진짜 디자인은 WBP_HUD_Hunt 에 얹히고, 그러면 아래 트리는 아예 만들어지지 않는다.
	// 1920×1080 기준 — 레퍼런스 인게임 HUD 시트(`docs/reference/hud_ingame.md`)에서 옮긴 자리·크기 (M15-02).
	// 네 귀퉁이만 쓰고 가운데는 비운다. 버튼 크기만 모바일 엄지에 맞춰 시트 44px 보다 키웠다(감독 지시 120~140).
	constexpr float PlaceholderScreenMargin = 40.f;
	constexpr float PlaceholderMinimapTop = 92.f;	// 왼쪽 위 「지금 할 일」 판(위 16 + 높이 약 57, 화면 배율 0.67 기준) 아래
	constexpr float PlaceholderAnimalCardWidth = 300.f;   // 시트: 동물 카드 300×56
	constexpr float PlaceholderAnimalBarHeight = 10.f;    // 시트: HP 바 240×10
	constexpr float PlaceholderCardPaddingX = 16.f;
	constexpr float PlaceholderCardFontSize = 18.f;       // 시트: 카드 제목 18px
	constexpr float PlaceholderRoundButtonSize = 130.f;   // 공격·포획 = DESIGN 2 「공격·포획 버튼」 지름 130(감독 지시 120~140) — 2026-09-25 영상 기준 원→크림 네모, 한 변이 지름
	constexpr float PlaceholderRoundButtonOpacity = 0.92f; // 크림 판은 거의 불투명 — 영상 슬롯도 비치지 않는다
	constexpr float PlaceholderButtonGap = 20.f;
	constexpr float PlaceholderVerbFontSize = 26.f;       // 원 버튼 글자 기본 크기 — 넘치면 `GetVerbDiscFontSize` 가 줄인다 (10-16 HUD 3차)
	constexpr float PlaceholderVerbRimMargin = 8.f;       // 원 테두리 안쪽 여백 — 글자 네모의 네 모서리가 (반지름 - 8) 원 안에 든다
	// 10-16 HUD 2차: 30 은 폰에서 납작해 기력인지 못 알아봤다 — 42(≈24px). 공격 버튼 위 = 40+42+20+130 = 232 < 기둥 238
	constexpr float PlaceholderBoltWidth = 34.f;
	constexpr float PlaceholderBoltHeight = 42.f;
	constexpr float PlaceholderStaminaIconSize = 48.f;    // 줄 왼쪽 번개 하나(`T_UI_Lightning`) — 「이 줄은 기력」 표시, 나무 판 안에 든다
	constexpr float PlaceholderBoltGap = 5.f;             // 시트 간격 4 : 칸 20 비율
	constexpr float PlaceholderSpentBoltOpacity = 0.35f;  // 소진 번개·체력 막대 바탕 = 진갈색을 옅게(새 색 없음)
	constexpr float PlaceholderHealthPillWidth = 230.f;   // 시트 145×50 → 10-16 「100 이 뭔지 모른다」: 하트를 더하고 1.3배 (무기·하트·숫자)
	constexpr float PlaceholderHealthPillHeight = 64.f;
	constexpr float PlaceholderHealthFontSize = 28.f;     // 시트: 흰 숫자 28px (탄 글 등 다른 글이 같이 쓴다)
	constexpr float PlaceholderHealthNumberFontSize = 36.f; // 체력 숫자 = 기준 폰 20px
	constexpr float PlaceholderHeartFontSize = 34.f;      // 하트 ♥ (Jua 에 있는 글자 U+2665)
	constexpr int32 PlaceholderHeartOutline = 2;          // 나무 판 위 빨강이 묻히지 않게 크림 테
	constexpr float BoltPlankPad = 6.f;                   // 번개 줄 둘레 나무 판 여백 (M97-c-2)
	constexpr float PlaceholderHealthPillRadius = 14.f;   // 시트: 모서리 12~16 — 알약이 아니라 둥근 네모다(steam_13)
	constexpr float PlaceholderWeaponIconSize = 40.f;     // 시트: 무기 아이콘 40px (민트 동그라미 받침)
	constexpr float PlaceholderAmmoIconSize = 52.f;       // 탄 칸 그림 — 받침 없이 크림 카드 위라 무기 40 보다 키운다(10-18 「탄약은 왜 아이콘이 없냐」: 40 은 갈색 점으로 묻힘)
	constexpr float PlaceholderContextButtonWidth = 220.f;
	constexpr float PlaceholderContextButtonHeight = SnareZoneTravelHud::MinButtonHeight; // M38-02 56 → 72 (안내 줄 올림도 이 값)
	constexpr float PlaceholderHintGap = 8.f;            // 도구 안내 알약과 그 아래 버튼 사이 — 버튼 간격 20 보다 붙여 「이 버튼 얘기」로 읽히게
	constexpr float PlaceholderBuffSlotGap = 12.f;       // 버프 칸 사이 — 가방 칸 간격(DESIGN 2.1 표, 시트 12px)을 빌린다

	// 보물 자리 다시받기가 실패했을 때 다시 묻기까지 (DESIGN 13.8 🟡 M13-16 — 값·근거는 설계서).
	constexpr double TreasureRestoreRetrySeconds = 10.0;

	// HUD 가 칸을 세우는 버프 종류 — `EBuffKind` 순서 그대로(None 빼고). 칸 자리 = 순번 − 1.
	constexpr EBuffKind HudBuffKinds[] = {EBuffKind::Speed, EBuffKind::ColdResist, EBuffKind::SwimSave, EBuffKind::Regen};

	// 버프 칸 이름 — 아이콘 원본이 없어 글자로 대신한다 (〔임시 그래픽〕, DESIGN 13.6).
	FText BuffSlotName(EBuffKind Kind)
	{
		switch (Kind)
		{
		case EBuffKind::Speed:
			return LOCTEXT("HuntHudBuffSpeed", "이동");
		case EBuffKind::ColdResist:
			return LOCTEXT("HuntHudBuffCold", "추위");
		case EBuffKind::SwimSave:
			return LOCTEXT("HuntHudBuffSwim", "수영");
		case EBuffKind::Regen:
			return LOCTEXT("HuntHudBuffRegen", "재생");
		default:
			return FText::GetEmpty();
		}
	}

	// 아래 둘은 자리표시가 아니라 **레퍼런스 시트에서 옮겨 적은 값**이다
	// (`docs/reference/hud_ingame.md` — "노란 번개 10칸 가로 한 줄(각 20px, 간격 4px)").
	// 칸 수는 그대로 10, 간격은 칸 폭의 **비율**로 둔다 — 시트는 1920 기준이고 우리 막대는 폭이 다르다.
	constexpr int32 VitalsGaugeSegmentCount = 10;

	// 칸 단위로 끊어 보여 준다 — 시트의 기력 줄은 연속 막대가 아니라 칸이 켜지고 꺼진다.
	// **올림**인 이유: 조금이라도 남아 있으면 칸 하나는 켜 둔다. 내림이면 스태미나 3%(=아직 달릴 수
	// 있다)가 빈 게이지로 보여 "이미 바닥" 이라는 거짓말이 된다. 0 은 그대로 0 칸이다.
	float SegmentedPercent(float Percent)
	{
		const float Segments = static_cast<float>(VitalsGaugeSegmentCount);
		return FMath::Clamp(FMath::CeilToFloat(Percent * Segments) / Segments, 0.f, 1.f);
	}

	// M100-02: 공격·포획 단추 = 나무 원판 + 밧줄 테두리 (크림 둥근 네모 → 우리 겉모습). 자리·크기는 그대로.
	void StyleWoodDiscButton(UButton& Button)
	{
		const float Radius = PlaceholderRoundButtonSize * 0.5f;
		const auto MakeDisc = [Radius](const FLinearColor& Fill)
		{
			return FSlateRoundedBoxBrush(Fill, Radius, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth);
		};
		FButtonStyle Style = Button.GetStyle();
		Style.Normal = MakeDisc(SnareUiStyle::UiWoodButton());
		Style.Hovered = MakeDisc(SnareUiStyle::UiWoodButton());
		Style.Pressed = MakeDisc(SnareUiStyle::UiWoodButtonPressed());
		Style.Disabled = MakeDisc(SnareUiStyle::UiWoodButtonPressed());
		Button.SetStyle(Style);
		Button.SetBackgroundColor(FLinearColor(1.f, 1.f, 1.f, PlaceholderRoundButtonOpacity));
	}

	// 크림 카드 한 장 — 알림·동물 카드가 같은 모양이다 (시트: 크림, 모서리 12~16, 테두리·그림자 없음).
	// 얇은 둥근 막대 — 동물 HP·플레이어 체력이 같은 모양 (시트: 회색 바탕 + 채움, 높이 10).
	void StyleThinBar(UProgressBar& Bar, const FLinearColor& FillColor, const FLinearColor& BackColor)
	{
		FProgressBarStyle Style = Bar.GetWidgetStyle();
		FSlateRoundedBoxBrush Back(BackColor, PlaceholderAnimalBarHeight * 0.5f);
		Back.ImageSize = FVector2D(PlaceholderAnimalBarHeight, PlaceholderAnimalBarHeight);
		FSlateRoundedBoxBrush Fill(FillColor, PlaceholderAnimalBarHeight * 0.5f);
		Fill.ImageSize = Back.ImageSize;
		Style.BackgroundImage = Back;
		Style.FillImage = Fill;
		Bar.SetWidgetStyle(Style);
	}

	void PlaceBottomRight(UCanvasPanelSlot* CanvasSlot, const FVector2D& Position, const FVector2D& Size)
	{
		if (CanvasSlot == nullptr)
		{
			return;
		}
		CanvasSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
		CanvasSlot->SetAlignment(FVector2D(1.f, 1.f));
		CanvasSlot->SetPosition(Position);
		CanvasSlot->SetSize(Size);
	}

	// 이건 자리표시가 아니다 — 포획 성공 줄이 떠 있는 시간은 **감독이 정했다**
	// (감독 결정 2026-09-20, DESIGN 3.1 표에 적혀 있다). 루프가 만든 숫자가 아니라 옮겨 적은 값이다.
	constexpr float CaptureBannerSeconds = 2.f;

	// 오류 알약 (M26-04) — 시트 「왼쪽 위(오류): 빨간 알약 토스트 480×34, 흰 글자 14px, 3초」(DESIGN 2 「인게임 HUD 색」 표).
	// 글자는 시트 14px@1080 을 폰에서 읽히게 20 으로 — 이름표 알약(DESIGN 2.1)과 같은 키움이다.
	constexpr float ErrorBannerSeconds = 3.f;
	constexpr float ErrorPillMinWidth = 480.f;
	constexpr float ErrorPillMinHeight = 34.f;
	constexpr float ErrorPillPaddingX = 12.f;
	constexpr float ErrorPillPaddingY = 4.f;
	constexpr float ErrorPillFontSize = 20.f;
	constexpr float ErrorMarkGap = 8.f;

	// 수영 기운 경고 (DESIGN 8.9 「수영 기운 경고」 줄) — 기준 30%·3초·처음 입수 안내 5초.
	constexpr float SwimWarnStaminaRatio = 0.3f;
	constexpr float SwimWarnSeconds = 3.f;
	constexpr float SwimIntroSeconds = 5.f;
	constexpr float SwimWarnFontSize = 28.f;
}

UHuntHudWidget::UHuntHudWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 어떤 행이 장비인지·이름이 무엇인지는 표가 안다 (DataDriven.md 1 · URanchHudWidget 과 같은 방식).
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(
		TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}

	// 미니맵 칸(M12-03)도 표가 안다 — 존 규칙(`SnareZones`)이 읽는 것과 같은 표다.
	static ConstructorHelpers::FObjectFinder<UDataTable> ZoneTableAsset(SnareZones::TablePath);
	if (ZoneTableAsset.Succeeded())
	{
		ZoneTable = ZoneTableAsset.Object;
	}

	// 우두머리 고지 줄(M21-07g)의 모자 확률 — 우두머리를 세우는 표와 같은 표다.
	static ConstructorHelpers::FObjectFinder<UDataTable> DangerZoneTableAsset(
		TEXT("/Game/Game/Data/World/DT_DangerZoneTable.DT_DangerZoneTable"));
	if (DangerZoneTableAsset.Succeeded())
	{
		DangerZoneTable = DangerZoneTableAsset.Object;
	}
}

void UHuntHudWidget::BindToCharacter(ASnarehavenCharacter* Character)
{
	// 듣던 쪽을 먼저 전부 끊는다 — 캐릭터가 바뀌었는데 옛 동물의 HP 가 계속 들어오면 HUD 가 거짓말을 한다.
	UnwatchAnimal();
	if (IsValid(WatchedMelee))
	{
		WatchedMelee->OnTargetChanged.RemoveDynamic(this, &UHuntHudWidget::HandleTargetChanged);
		WatchedMelee->OnReloadReply.RemoveDynamic(this, &UHuntHudWidget::HandleGunReloadReply);
	}
	if (IsValid(WatchedZoneTravel))
	{
		WatchedZoneTravel->OnZonePortalTargetChanged.RemoveDynamic(
			this, &UHuntHudWidget::HandleZonePortalTargetChanged);
	}
	if (IsValid(WatchedVitals))
	{
		WatchedVitals->OnHealthChanged.RemoveDynamic(this, &UHuntHudWidget::HandlePlayerHealthChanged);
		WatchedVitals->OnStaminaChanged.RemoveDynamic(this, &UHuntHudWidget::HandlePlayerStaminaChanged);
	}
	if (IsValid(WatchedLootClaim))
	{
		WatchedLootClaim->OnGrantPopup.RemoveAll(this);
	}
	if (IsValid(WatchedFishing))
	{
		WatchedFishing->OnCatchPopup.RemoveAll(this);
	}
	if (IsValid(WatchedCapture))
	{
		WatchedCapture->OnCaptureRingStarted.RemoveDynamic(this, &UHuntHudWidget::HandleCaptureRingStarted);
	}
	LastRingAnimal.Reset();
	WatchedMelee = nullptr;
	WatchedCapture = nullptr;
	WatchedZoneTravel = nullptr;
	WatchedVitals = nullptr;
	WatchedLootClaim = nullptr;
	WatchedFishing = nullptr;

	// 인벤토리 캐시가 바뀌면 들 수 있는 장비가 달라진다 (M5-06c) — 옛 플레이어의 알림을 먼저 끊는다.
	// 따로 들고 있지 않고 `OwnerCharacter` 로 되찾는다 — 아직 갈아 끼우기 전이라 옛 것이 잡힌다.
	if (UInventoryComponent* const OldInventory = GetInventory())
	{
		OldInventory->OnInventoryCacheChanged.RemoveDynamic(this, &UHuntHudWidget::HandleInventoryCacheChanged);
	}

	// 걸친 것도 같은 이유로 끊는다 — 옛 폰이 갈아입어도 내 화면이 흔들리면 안 된다.
	if (UEquipmentComponent* const OldEquipment = GetEquipment())
	{
		OldEquipment->OnEquipmentChanged.RemoveDynamic(this, &UHuntHudWidget::HandleEquipmentChanged);
	}

	OwnerCharacter = Character;
	// 새 몸의 손 칸은 처음 보는 것이다 — 이미 들고 있던 것도 「처음 들기」 로 친다 (M28-05b).
	UsageHintLastTool = NAME_None;
	UsageHintLastWeapon = NAME_None;
	UsageHintLastApproach = NAME_None;

	if (IsValid(Character))
	{
		WatchedMelee = Character->GetMeleeAttack();
		WatchedCapture = Character->GetCapture();
		WatchedZoneTravel = Character->GetZoneTravel();
		if (IsValid(WatchedCapture))
		{
			// 링 화면은 틱이 링 경과를 읽어 그리고, 실패 줄 대상만 링이 서는 순간 기억한다 (M21-09d).
			WatchedCapture->OnCaptureRingStarted.AddDynamic(this, &UHuntHudWidget::HandleCaptureRingStarted);
		}

		// 게이지는 알림으로만 움직인다 (DESIGN 3.1) — HUD 가 체력·스태미나를 매 프레임 되묻지 않는다.
		WatchedVitals = Character->GetPlayerVitals();
		if (IsValid(WatchedVitals))
		{
			WatchedVitals->OnHealthChanged.AddDynamic(this, &UHuntHudWidget::HandlePlayerHealthChanged);
			WatchedVitals->OnStaminaChanged.AddDynamic(this, &UHuntHudWidget::HandlePlayerStaminaChanged);
		}

		// 포탈 버튼은 사거리 안 포탈이 바뀔 때만 폈다 접는다 — HUD 가 타이머를 따로 돌리지 않는다
		// (하이라이트 주기는 이미 `UZoneTravelComponent` 가 근접 공격에서 빌려 돌고 있다).
		if (IsValid(WatchedZoneTravel))
		{
			WatchedZoneTravel->OnZonePortalTargetChanged.AddDynamic(
				this, &UHuntHudWidget::HandleZonePortalTargetChanged);
		}

		if (IsValid(WatchedMelee))
		{
			WatchedMelee->OnTargetChanged.AddDynamic(this, &UHuntHudWidget::HandleTargetChanged);
			WatchedMelee->OnReloadReply.AddDynamic(this, &UHuntHudWidget::HandleGunReloadReply);
			// 바인딩 전에 이미 하이라이트가 잡혀 있을 수 있다 (HUD 가 늦게 올라온 경우).
			WatchAnimal(WatchedMelee->GetCurrentTarget());
		}

		// 채집·드롭·펫 획득 글은 서버가 받아 준 뒤에만 온다 — 줍는 순간이 아니다 (CLAUDE.md 8).
		WatchedLootClaim = Character->FindComponentByClass<ULootClaimComponent>();
		if (IsValid(WatchedLootClaim))
		{
			WatchedLootClaim->OnGrantPopup.AddUObject(this, &UHuntHudWidget::HandleLootGrantPopup);
		}
		// 낚은 것도 서버 `CatchFish` 확정 뒤에만 같은 알림 줄로 (M28-01).
		WatchedFishing = Character->FindComponentByClass<UTapFishingComponent>();
		if (IsValid(WatchedFishing))
		{
			WatchedFishing->OnCatchPopup.AddUObject(this, &UHuntHudWidget::HandleLootGrantPopup);
		}

		// 보유 원본은 백엔드다 — 스냅샷이 도착하는 순간 장비 버튼에 들 것이 생긴다 (DESIGN 4.5).
		if (UInventoryComponent* const Inventory = GetInventory())
		{
			Inventory->OnInventoryCacheChanged.AddDynamic(this, &UHuntHudWidget::HandleInventoryCacheChanged);
		}

		// 사람이 버튼을 눌러 갈아입는 길만 화면을 고치던 때는, 시작 지급처럼 **손을 안 거친 장착**이
		// 화면에 안 나타났다 (M9-07b 3층에서 실제로 그런 그림이 찍혔다).
		if (UEquipmentComponent* const Equipment = GetEquipment())
		{
			Equipment->OnEquipmentChanged.AddDynamic(this, &UHuntHudWidget::HandleEquipmentChanged);
		}
		// 붙기 전에 이미 든 것은 알림이 다시 안 온다 — 붙는 순간 한 번 본다(M42-06 3층에서 권총 줄이 안 뜬 까닭).
		CheckUsageHintForHeldItems();
	}

	// 잔액의 출처도 같이 갈아 끼운다 (M7-03). 폰이 아니라 게임 인스턴스에 붙어 있는 값이라
	// `ASnarehavenCharacter::PullInventoryFromBackend` 와 같은 길로 찾는다.
	UGameInstance* const GameInstance = IsValid(Character) ? Character->GetGameInstance() : nullptr;
	BindToBackend(GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr);
#if !UE_BUILD_SHIPPING
	// `-SnareNoticeAt=3` — 3층 달성 알림 확인 전용 (M48-05). 적은 초에 서버 답 모양 두 벌을 주입한다:
	// 빈 기준 답(첫 답 무음) → 첫 올가미 업적 + 돼지 도감 칸. 서버 답과 같은 `ApplyCodexReply` 길이라 카드·소리가 사람 길 그대로.
	float NoticeAt = 0.f;
	UWorld* const World = GetWorld();
	if (World && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareNoticeAt="), NoticeAt))
	{
		FTimerHandle Unused;
		World->GetTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const auto Reply = [](const TCHAR* Entry, const TCHAR* Done)
			{
				TArray<TSharedPtr<FJsonValue>> Entries;
				TArray<TSharedPtr<FJsonValue>> DoneList;
				if (Entry) { Entries.Add(MakeShared<FJsonValueString>(Entry)); }
				if (Done) { DoneList.Add(MakeShared<FJsonValueString>(Done)); }
				const TSharedPtr<FJsonObject> Ach = MakeShared<FJsonObject>();
				Ach->SetObjectField(TEXT("counters"), MakeShared<FJsonObject>());
				Ach->SetArrayField(TEXT("done"), DoneList);
				const TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
				Out->SetArrayField(TEXT("entries"), Entries);
				Out->SetObjectField(TEXT("achievements"), Ach);
				return Out;
			};
			if (IsValid(WatchedBackend))
			{
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 달성 알림 주입 (-SnareNoticeAt)"));
				WatchedBackend->ApplyCodexReply(Reply(nullptr, nullptr));
				WatchedBackend->ApplyCodexReply(Reply(TEXT("animal.pig"), TEXT("ach.capture_1")));
			}
		}), NoticeAt, false);
	}
#endif

	RefreshDisplay();
}

void UHuntHudWidget::BindToBackend(USnareBackendSubsystem* Backend)
{
	if (WatchedBackend == Backend)
	{
		return;
	}

	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(
			this, &UHuntHudWidget::HandleBackendSnapshotChanged);
		WatchedBackend->OnAchievementNotice.RemoveAll(this);
	}

	WatchedBackend = Backend;

	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.AddDynamic(
			this, &UHuntHudWidget::HandleBackendSnapshotChanged);
		// 업적 달성·도감 새 칸 카드 (M48-03, DESIGN 8.15) — 서버 답의 차이로만 온다.
		WatchedBackend->OnAchievementNotice.AddUObject(this, &UHuntHudWidget::HandleAchievementNotice);
	}

	RefreshDisplay();
}

FText UHuntHudWidget::GetBalanceText() const
{
	// 문구도 미정 표시도 두 화면이 한 곳에서 가져온다 (`SnareBalanceHud`).
	return SnareBalanceHud::FormatBalanceText(WatchedBackend);
}

bool UHuntHudWidget::IsAnimalPanelVisible() const
{
	// 포획 성공 줄이 떠 있는 동안은 동물 칸을 비운다 — 자리표시 트리에서 둘은 **같은 자리**라 겹친다.
	// (포획한 동물은 곧 숨겨져 하이라이트에서도 빠지지만, 그 한 틱을 기다리지 않는다.)
	bool bStoreShot = false;
#if !UE_BUILD_SHIPPING
	// 스토어 스크린샷엔 화면 밖·가려진 동물의 막대가 뜨지 않게 접는다 (출시 구성엔 없음).
	static const bool bStoreShotRun = [] { int32 Coins = 0; return SnareCheckSwitch::Value(TEXT("SnareStoreShot="), Coins); }();
	bStoreShot = bStoreShotRun;
#endif
	return SnareHuntHud::WantAnimalPanel(IsValid(ShownAnimal), IsCaptureBannerVisible(), bStoreShot);
}

bool SnareHuntHud::WantAnimalPanel(const bool bHasAnimal, const bool bCaptureBanner, const bool bStoreShot)
{
	return bHasAnimal && !bCaptureBanner && !bStoreShot;
}

bool UHuntHudWidget::IsCaptureButtonEnabled() const
{
	// 포획 조건을 HUD 가 따로 세지 않는다 — 판정은 UCaptureComponent 한 군데다 (DESIGN 3.1).
	return IsValid(WatchedCapture) && WatchedCapture->CanCapture();
}

float UHuntHudWidget::GetHealthPercent() const
{
	if (!IsValid(ShownAnimal))
	{
		return 0.f;
	}

	const UAnimalStateComponent* State = ShownAnimal->GetAnimalState();
	if (!IsValid(State) || State->GetMaxHP() <= 0.f)
	{
		// 아직 행을 못 읽은 동물이면 최대 체력이 미정(-1)이다 — 꽉 찬 바로 보여 주면 거짓말이 된다.
		return 0.f;
	}

	return FMath::Clamp(State->GetCurrentHP() / State->GetMaxHP(), 0.f, 1.f);
}

float UHuntHudWidget::GetPlayerHealthPercent() const
{
	// 최대치가 미정(-1)인 컴포넌트는 아직 표를 못 읽은 것이다 — 그때는 빈 게이지다 (Verification.md 7).
	if (!IsValid(WatchedVitals) || WatchedVitals->GetMaxHealth() <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(WatchedVitals->GetHealth() / WatchedVitals->GetMaxHealth(), 0.f, 1.f);
}

float UHuntHudWidget::GetPlayerStaminaPercent() const
{
	if (!IsValid(WatchedVitals) || WatchedVitals->GetMaxStamina() <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(WatchedVitals->GetStamina() / WatchedVitals->GetMaxStamina(), 0.f, 1.f);
}

FText UHuntHudWidget::GetAnimalNameText() const
{
	if (!IsValid(ShownAnimal))
	{
		return FText::GetEmpty();
	}

	const UAnimalStateComponent* State = ShownAnimal->GetAnimalState();
	return IsValid(State) ? State->GetCardName() : FText::GetEmpty();
}

FText UHuntHudWidget::GetAnimalNoteText() const
{
	const UAnimalStateComponent* const State = IsValid(ShownAnimal) ? ShownAnimal->GetAnimalState() : nullptr;
	if (!IsValid(State) || !State->IsBoss())
	{
		return FText::GetEmpty();
	}
	return FormatBossDisclosure(FindBossHatChance(DangerZoneTable));
}

FText UHuntHudWidget::FormatBossDisclosure(float HatChance)
{
	FNumberFormattingOptions Percent;
	Percent.SetMaximumFractionalDigits(1).SetUseGrouping(false);
	return FText::Format(LOCTEXT("HuntHudBossDisclosure", "우두머리 모자 {0}% (주 1회)"), FText::AsNumber(HatChance * 100.f, &Percent));
}

float UHuntHudWidget::FindBossHatChance(const UDataTable* Table)
{
	if (!IsValid(Table))
	{
		return 0.f;
	}
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FDangerZoneRow* const Row = reinterpret_cast<const FDangerZoneRow*>(Pair.Value);
		if (Row != nullptr && Row->BossHPMultiplier > 0.f)
		{
			return Row->BossHatChance;
		}
	}
	return 0.f;
}

bool UHuntHudWidget::IsCaptureBannerVisible() const
{
	return !CaptureBannerText.IsEmpty();
}

FText UHuntHudWidget::GetCaptureBannerText() const
{
	return CaptureBannerText;
}

void UHuntHudWidget::ShowCaptureBanner(const FText& AnimalName)
{
	ShowNoticeBanner(FText::Format(LOCTEXT("HuntHudCaptureBanner", "포획 성공 - {0}"), AnimalName));
}

void UHuntHudWidget::ShowNoticeBanner(const FText& Message)
{
	ShowNoticeBannerFor(Message, CaptureBannerSeconds);
}

void UHuntHudWidget::ShowNoticeBannerFor(const FText& Message, float Seconds)
{
	if (SnareSound::IsActionLogOn())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[실측] 화면 알림 「%s」"), *Message.ToString());
	}
	CaptureBannerText = Message;
	ErrorBannerText = FText::GetEmpty(); // 같은 자리 — 나중 것 하나만

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ErrorBannerTimer);
		World->GetTimerManager().SetTimer(
			CaptureBannerTimer, this, &UHuntHudWidget::ClearCaptureBanner, Seconds, false);
	}

	RefreshDisplay();
}

void UHuntHudWidget::HandleLootGrantPopup(const FText& Popup)
{
	if (!Popup.IsEmpty())
	{
		ShowNoticeBanner(Popup);
	}
}

void UHuntHudWidget::ClearCaptureBanner()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CaptureBannerTimer);
	}

	CaptureBannerText = FText::GetEmpty();
	RefreshDisplay();
}

FName UHuntHudWidget::PlayDenySound(bool bMessageEmpty, double NowSeconds)
{
	const SnareSound::FChangeCue Cue =
		SnareSound::DenyCue(bMessageEmpty, static_cast<float>(FMath::Min(NowSeconds - LastDenySoundSeconds, 1.0e6)));
	if (Cue.Cue.IsNone())
	{
		return NAME_None;
	}
	LastDenySoundSeconds = NowSeconds;
	++DenySoundCount;
	USnareSoundSubsystem::Play2D(this, Cue.Cue, Cue.Volume);
	if (SnareSound::IsActionLogOn())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 막힘 %s — 크기 %.2f"), *Cue.Cue.ToString(), Cue.Volume);
	}
	return Cue.Cue;
}

void UHuntHudWidget::ShowErrorBanner(const FText& Message)
{
	PlayDenySound(Message.IsEmpty(), FPlatformTime::Seconds());
	if (SnareSound::IsActionLogOn())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[실측] 화면 오류 「%s」"), *Message.ToString());
	}
	ErrorBannerText = Message;
	CaptureBannerText = FText::GetEmpty(); // 같은 자리 — 나중 것 하나만

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CaptureBannerTimer);
		World->GetTimerManager().SetTimer(
			ErrorBannerTimer, this, &UHuntHudWidget::ClearErrorBanner, ErrorBannerSeconds, false);
	}

	RefreshDisplay();
}

bool UHuntHudWidget::IsErrorBannerVisible() const
{
	return !ErrorBannerText.IsEmpty();
}

FText UHuntHudWidget::GetErrorBannerText() const
{
	return ErrorBannerText;
}

void UHuntHudWidget::ClearErrorBanner()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ErrorBannerTimer);
	}

	ErrorBannerText = FText::GetEmpty();
	RefreshDisplay();
}

UEquipmentComponent* UHuntHudWidget::GetEquipment() const
{
	return IsValid(OwnerCharacter) ? OwnerCharacter->GetEquipment() : nullptr;
}

UInventoryComponent* UHuntHudWidget::GetInventory() const
{
	return IsValid(OwnerCharacter) ? OwnerCharacter->GetInventory() : nullptr;
}

TArray<EEquipSlot> UHuntHudWidget::GetEquipButtonSlots()
{
	// 손에 드는 것 먼저, 입는 것 나중 — 자주 바꾸는 쪽이 위다 (Autonomy.md 🟡 — 화면 순서는 규칙 숫자가 아니다).
	return { EEquipSlot::Weapon, EEquipSlot::Tool, EEquipSlot::OutfitTop, EEquipSlot::OutfitBottom };
}

FName UHuntHudWidget::GetEquipButtonWidgetName(EEquipSlot EquipSlot)
{
	// 위젯 이름은 헤더의 칸 이름과 **글자까지 같아야** 한다 — `BindWidgetOptional` 이 이름으로 붙는다.
	switch (EquipSlot)
	{
	case EEquipSlot::Weapon:       return TEXT("EquipButton_Weapon");
	case EEquipSlot::Tool:         return TEXT("EquipButton_Tool");
	case EEquipSlot::OutfitTop:    return TEXT("EquipButton_OutfitTop");
	case EEquipSlot::OutfitBottom: return TEXT("EquipButton_OutfitBottom");
	default:                       return NAME_None;
	}
}

UButton* UHuntHudWidget::GetEquipButton(EEquipSlot EquipSlot) const
{
	switch (EquipSlot)
	{
	case EEquipSlot::Weapon:       return EquipButton_Weapon;
	case EEquipSlot::Tool:         return EquipButton_Tool;
	case EEquipSlot::OutfitTop:    return EquipButton_OutfitTop;
	case EEquipSlot::OutfitBottom: return EquipButton_OutfitBottom;
	default:                       return nullptr;
	}
}

UTextBlock* UHuntHudWidget::GetEquipButtonTextBlock(EEquipSlot EquipSlot) const
{
	switch (EquipSlot)
	{
	case EEquipSlot::Weapon:       return EquipButton_WeaponText;
	case EEquipSlot::Tool:         return EquipButton_ToolText;
	case EEquipSlot::OutfitTop:    return EquipButton_OutfitTopText;
	case EEquipSlot::OutfitBottom: return EquipButton_OutfitBottomText;
	default:                       return nullptr;
	}
}

void UHuntHudWidget::SetEquipButtonWidgets(EEquipSlot EquipSlot, UButton* Button, UTextBlock* Text)
{
	switch (EquipSlot)
	{
	case EEquipSlot::Weapon:       EquipButton_Weapon = Button;       EquipButton_WeaponText = Text;       break;
	case EEquipSlot::Tool:         EquipButton_Tool = Button;         EquipButton_ToolText = Text;         break;
	case EEquipSlot::OutfitTop:    EquipButton_OutfitTop = Button;    EquipButton_OutfitTopText = Text;    break;
	case EEquipSlot::OutfitBottom: EquipButton_OutfitBottom = Button; EquipButton_OutfitBottomText = Text; break;
	default: break;
	}
}

FName UHuntHudWidget::GetContextToolItem() const
{
	// 고르기는 캐릭터 한 곳이다 — 상황 버튼이 누를 때 자동으로 드는 도구와 이 버튼이 가리키는 도구가 갈리지 않게 (INBOX 22:00).
	return IsValid(OwnerCharacter) ? OwnerCharacter->FindContextToolRow() : NAME_None;
}

FName UHuntHudWidget::GetEquipButtonItem(EEquipSlot EquipSlot) const
{
	if (!IsValid(ItemTable) || EquipSlot == EEquipSlot::None)
	{
		return NAME_None;
	}

	// **도구 칸은 눈앞의 대상이 고른다** — 나무 앞에서 낚싯대를 들던 것을 막는다 (M15-08 · INBOX ②).
	// 대상이 없으면 아래 예전 순서로 내려간다(든 것 → 가진 것) — 그래야 아무 데서나 도구를 내려놓을 수 있다.
	if (EquipSlot == EEquipSlot::Tool)
	{
		const FName ContextTool = GetContextToolItem();
		if (!ContextTool.IsNone())
		{
			return ContextTool;
		}
	}

	const UEquipmentComponent* const Equipment = GetEquipment();
	const UInventoryComponent* const Inventory = GetInventory();
	FName FirstOwned = NAME_None;

	// 장비인지·어느 부위인지는 표가 정한다 — HUD 가 아이템 이름을 코드에 적지 않는다 (DataDriven.md 1).
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row == nullptr || Row->ItemType != EItemType::Equipment || Row->EquipSlot != EquipSlot)
		{
			continue;
		}

		// **든 것이 먼저다.** 인벤토리 캐시는 백엔드가 서기 전까지 비어 있는데(M2-03),
		// 보유한 것만 보면 한 번 든 장비를 벗을 버튼이 사라진다.
		if (Equipment != nullptr && Equipment->GetEquippedItem(EquipSlot) == Pair.Key)
		{
			return Pair.Key;
		}

		if (FirstOwned.IsNone() && Inventory != nullptr && Inventory->GetItemCount(Row->ItemID) > 0)
		{
			FirstOwned = Pair.Key;
		}
	}

	return FirstOwned;
}

bool UHuntHudWidget::IsEquipButtonVisible(EEquipSlot EquipSlot) const
{
	return !GetEquipButtonItem(EquipSlot).IsNone();
}

bool UHuntHudWidget::IsEquipButtonItemEquipped(EEquipSlot EquipSlot) const
{
	const FName RowName = GetEquipButtonItem(EquipSlot);
	const UEquipmentComponent* const Equipment = GetEquipment();

	// 들고 있는지를 HUD 가 따로 세지 않는다 — 판정은 UEquipmentComponent 한 군데다 (포획 버튼과 같은 규칙).
	return !RowName.IsNone() && Equipment != nullptr && Equipment->GetEquippedItem(EquipSlot) == RowName;
}

FText UHuntHudWidget::GetEquipButtonText(EEquipSlot EquipSlot) const
{
	const FName RowName = GetEquipButtonItem(EquipSlot);
	const FItemRow* const Row = (!RowName.IsNone() && IsValid(ItemTable))
		? ItemTable->FindRow<FItemRow>(RowName, TEXT("UHuntHudWidget::GetEquipButtonText"))
		: nullptr;
	if (Row == nullptr)
	{
		return FText::GetEmpty();
	}

	// 글자는 아이템 이름이 아니라 부위 이름이다 ("무기 장착") — 2026-09-25 사용자 "몽둥이 장착은 뭐냐? 무기장착으로".
	FText SlotName;
	switch (EquipSlot)
	{
	case EEquipSlot::Weapon:       SlotName = LOCTEXT("HuntHudSlotWeapon", "무기"); break;
	case EEquipSlot::Tool:         SlotName = LOCTEXT("HuntHudSlotTool", "도구"); break;
	case EEquipSlot::OutfitTop:    SlotName = LOCTEXT("HuntHudSlotTop", "상의"); break;
	case EEquipSlot::OutfitBottom: SlotName = LOCTEXT("HuntHudSlotBottom", "하의"); break;
	case EEquipSlot::Backpack:     SlotName = LOCTEXT("HuntHudSlotBackpack", "배낭"); break;
	case EEquipSlot::Head:         SlotName = LOCTEXT("HuntHudSlotHead", "모자"); break;
	default:                       SlotName = LOCTEXT("HuntHudSlotOther", "장비"); break;
	}
	return IsEquipButtonItemEquipped(EquipSlot)
		? FText::Format(LOCTEXT("HuntHudUnequipButton", "{0} 해제"), SlotName)
		: FText::Format(LOCTEXT("HuntHudEquipButton", "{0} 장착"), SlotName);
}

bool UHuntHudWidget::ToggleEquipButtonItem(EEquipSlot EquipSlot)
{
	UEquipmentComponent* const Equipment = GetEquipment();
	const FName RowName = GetEquipButtonItem(EquipSlot);
	if (!IsValid(Equipment) || RowName.IsNone())
	{
		return false;
	}

	// **재화가 걸린 판정이 아니다** — 장비는 스탯 없는 겉모습이라 보유 수량도 백엔드도 건드리지 않는다
	// (DESIGN 3.5 · CLAUDE.md 8). 바뀌는 것은 내 화면에 무엇이 붙어 있는가 하나다.
	// **부위 하나만 건드린다** — 무기를 바꿔 들어도 도구·옷은 그대로다 (M9-01 이 동시에 들리게 해 뒀다).
	const bool bChanged = IsEquipButtonItemEquipped(EquipSlot)
		? Equipment->UnequipSlot(EquipSlot)
		: Equipment->EquipItem(RowName);

	RefreshDisplay();
	return bChanged;
}

bool UHuntHudWidget::IsZoneTravelButtonVisible() const
{
	// 판정도 글자도 거점 HUD 와 같아야 해서 한 곳이 정한다 (M6-03b — 잔액 줄과 같은 규칙).
	return SnareZoneTravelHud::IsButtonVisible(WatchedZoneTravel);
}

FText UHuntHudWidget::GetZoneTravelButtonText() const
{
	return SnareZoneTravelHud::FormatButtonText(WatchedZoneTravel);
}

bool UHuntHudWidget::TryZoneTravel()
{
	if (!IsValid(WatchedZoneTravel))
	{
		return false;
	}

	// **HUD 는 맵을 열지 않는다.** 호스트면 컴포넌트가 포탈에게 옮기라 하고, 게스트면 서버에 부탁만 한다
	// (CLAUDE.md 8 과 같은 취지 — 판정은 권위가 있는 쪽에서).
	// 선착장 요금(서버 `PayFerry`)을 먼저 내고, 성공 답이 온 뒤에만 그 길을 탄다 (DESIGN 8.14).
	return WatchedZoneTravel->TryFerryToNearest();
}

void UHuntHudWidget::RefreshDisplay()
{
	const bool bShowAnimal = IsAnimalPanelVisible();

	const ESlateVisibility AnimalShown = bShowAnimal ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (IsValid(AnimalCard))
	{
		AnimalCard->SetVisibility(AnimalShown);
	}

	if (IsValid(AnimalNameText))
	{
		AnimalNameText->SetText(GetAnimalNameText());
		AnimalNameText->SetVisibility(AnimalShown);
	}

	if (IsValid(AnimalNoteText))
	{
		const FText Note = GetAnimalNoteText();
		AnimalNoteText->SetText(Note);
		AnimalNoteText->SetVisibility(bShowAnimal && !Note.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (IsValid(AnimalHealthBar))
	{
		AnimalHealthBar->SetPercent(GetHealthPercent());
		AnimalHealthBar->SetVisibility(AnimalShown);
	}

	// 상시 안내 문장은 없다 — 레퍼런스 HUD 는 네 귀퉁이 정보만 두고 가운데를 비운다 (M15-02, hud_ingame 시트).
	// 알림 카드는 포획 성공 같은 알림이 떠 있는 동안만 왼쪽 위에 선다.
	const bool bShowBanner = IsCaptureBannerVisible();
	const ESlateVisibility NoticeShown = bShowBanner ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (IsValid(GuideText))
	{
		GuideText->SetText(GetCaptureBannerText());
		GuideText->SetVisibility(NoticeShown);
	}
	if (IsValid(NoticeCard))
	{
		NoticeCard->SetVisibility(NoticeShown);
	}
	const ESlateVisibility ErrorShown = IsErrorBannerVisible() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (IsValid(ErrorText))
	{
		ErrorText->SetText(GetErrorBannerText());
	}
	if (IsValid(ErrorPill))
	{
		ErrorPill->SetVisibility(ErrorShown);
	}

	if (IsValid(CaptureButton))
	{
		// 버튼은 숨기지 않고 꺼 둔다 — 모바일에서 버튼이 나타났다 사라지면 손가락 위치를 잃는다.
		CaptureButton->SetIsEnabled(IsCaptureButtonEnabled());
	}

	for (const EEquipSlot EquipSlot : GetEquipButtonSlots())
	{
		UButton* const SlotButton = GetEquipButton(EquipSlot);
		if (!IsValid(SlotButton))
		{
			continue;
		}

		// 장비 버튼만은 **접는다** — 공격·포획과 달리 가리킬 장비가 없으면 눌러서 될 일이 없고,
		// 꺼진 채로 두면 "장비가 있는데 왜 안 되지" 로 읽힌다. 부위마다 따로 접힌다 —
		// 도구가 없다고 무기 버튼까지 사라지면 안 된다.
		SlotButton->SetVisibility(
			IsEquipButtonVisible(EquipSlot) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

		if (UTextBlock* const SlotText = GetEquipButtonTextBlock(EquipSlot))
		{
			SlotText->SetText(GetEquipButtonText(EquipSlot));
		}
	}

	if (IsValid(ZoneTravelButton))
	{
		// 포탈 버튼도 장비 버튼처럼 **접는다** — 포탈에서 멀어지면 눌러서 될 일이 없고, 꺼진 채로 두면
		// "여기서 갈 수 있는데 왜 안 되지" 로 읽힌다.
		const bool bShowZoneTravel = IsZoneTravelButtonVisible();
		ZoneTravelButton->SetVisibility(
			bShowZoneTravel ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

		if (IsValid(ZoneTravelButtonText))
		{
			ZoneTravelButtonText->SetText(GetZoneTravelButtonText());
		}
	}

	// 플레이어 게이지는 **접지 않는다** — 동물 칸과 달리 자기 몸 상태는 늘 보여야 하고, 사라졌다
	// 나타나면 "죽기 직전" 과 "표시 없음" 이 같은 화면이 된다 (버튼을 안 숨기는 것과 같은 이유).
	// 막대에 넣는 값만 **칸 단위로 끊는다** — `GetPlayerHealthPercent()` 는 그대로 정확한 비율이다
	// (검사·다른 화면이 읽는 값을 반올림해 두면 거기서도 10% 단위 거짓말이 된다).
	if (IsValid(PlayerHealthBar))
	{
		PlayerHealthBar->SetPercent(SegmentedPercent(GetPlayerHealthPercent()));
	}

	if (IsValid(PlayerHealthText))
	{
		// 시트 왼쪽 아래 빨간 알약의 흰 숫자 자리 — 모르면 `-` (잔액 줄과 같은 규칙).
		PlayerHealthText->SetText(IsValid(WatchedVitals)
			? FText::AsNumber(FMath::CeilToInt(WatchedVitals->GetHealth()))
			: LOCTEXT("HuntHudHealthUnknown", "-"));
	}

	if (IsValid(PlayerWeaponIcon))
	{
		const FName WeaponRow = GetEquipButtonItem(EEquipSlot::Weapon);
		if (WeaponRow != ShownWeaponIconRow)
		{
			ShownWeaponIconRow = WeaponRow;
			const FItemRow* const Row = (!WeaponRow.IsNone() && IsValid(ItemTable))
				? ItemTable->FindRow<FItemRow>(WeaponRow, TEXT("UHuntHudWidget::WeaponIcon")) : nullptr;
			const bool bHasIcon = Row != nullptr && !Row->IconRef.IsNull();
			if (bHasIcon)
			{
				FSlateBrush IconBrush;
				IconBrush.SetResourceObject(Row->IconRef.LoadSynchronous());
				IconBrush.ImageSize = FVector2D(PlaceholderWeaponIconSize, PlaceholderWeaponIconSize);
				PlayerWeaponIcon->SetBrush(IconBrush);
			}
			// 무기가 없으면 빈 민트 동그라미 — 자리는 그대로 두어 숫자가 옆으로 뛰지 않는다.
			PlayerWeaponIcon->SetColorAndOpacity(bHasIcon ? FLinearColor::White : FLinearColor::Transparent);
		}
	}

	if (IsValid(PlayerStaminaBar))
	{
		PlayerStaminaBar->SetPercent(SegmentedPercent(GetPlayerStaminaPercent()));
	}
	UpdateStaminaBolts();
	// 든 장비 줄(M15-11)은 장비 알림을 타고 여기로 온다 — 버프 칸과 달리 시간이 아니라 **든 것**이 바꾸니
	// 틱이 아니라 이 길이 맞다 (DESIGN 3.1 HUD 규칙 "알림으로만 고친다").
	UpdateHeldGearLine();

	if (IsValid(BalanceText))
	{
		// 잔액 줄은 접지 않는다 — 있다가 없어지면 "재화를 잃었다" 로 읽힌다. 모를 때는 `-` 가 뜬다.
		BalanceText->SetText(GetBalanceText());
	}

	// 공격 버튼은 끄지 않는다 — 대상이 없을 때 눌러도 채집·목장 슬롯으로 이어지고(DESIGN 3.3),
	// 모바일에는 이것 말고 공격할 길이 없다.
}

void UHuntHudWidget::NativeDestruct()
{
	// 위젯이 뷰포트에서 내려간 뒤에도 알림이 오면 죽은 위젯을 부른다 (engines/unreal.md 코드 규칙 7 과 같은 이유).
	// 2초 타이머도 같은 이유로 끊는다 — 남겨 두면 사라진 위젯을 타이머가 부른다.
	ClearCaptureBanner();
	ClearErrorBanner();
	BindToCharacter(nullptr);

	Super::NativeDestruct();
}

void UHuntHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateMinimap();
	UpdateOutpostBanner(FPlatformTime::Seconds());   // 들어서는 순간을 보는 거라 알림이 없다 — 틱이 자리를 본다
	UpdateBuffSlots();
	UpdateHeldGearLine();   // 배낭에 넣고 뺀 것(소지 효과)은 장비 알림이 아니라 여기서 따라온다
	UpdateCaptureRing();    // 링은 시간에 따라 줄어드는 그림이라 알림이 아니라 틱이 맞다 (M21-09d)
	UpdateAttackButtonText(FPlatformTime::Seconds());   // 대상은 다가가기만 해도 바뀌어 알림이 없다 — 틱(간격)으로 본다
	TickUsageHint(InDeltaTime);
	UpdateGunHud();         // 탄창 수는 복제 값이라 알림이 없다 — 틱이 글자만 맞춘다
	UpdateLowHealthEdge(FPlatformTime::Seconds());   // 펄스는 시간에 따라 변하는 그림이라 틱이 맞다 (M60-03)
	UpdateSwimWarn();       // 물에 든 순간·기운 기준 넘는 순간을 본다 — 입수는 알림이 없어 틱이 맞다
}

bool UHuntHudWidget::ShouldWarnSwimStamina(bool bSwimming, float Stamina, float MaxStamina, bool bWarnedThisDive)
{
	return bSwimming && !bWarnedThisDive && MaxStamina > 0.f && Stamina <= MaxStamina * SwimWarnStaminaRatio;
}

bool UHuntHudWidget::UpdateSwimWarn()
{
	const bool bSwimming = IsValid(WatchedVitals) && WatchedVitals->IsSwimming() && !WatchedVitals->IsDead();
	if (!bSwimming)
	{
		bSwimWarnedThisDive = false;   // 뭍에 오르면 다음 잠수에 다시 경고한다
		return false;
	}
	if (!bSwimIntroShown)
	{
		bSwimIntroShown = true;
		ShowNoticeBannerFor(LOCTEXT("HuntHudSwimIntro", "수영은 기운(번개)을 써요 — 바닥나면 체력이 닳아요"), SwimIntroSeconds);
	}
	if (!ShouldWarnSwimStamina(true, WatchedVitals->GetStamina(), WatchedVitals->GetMaxStamina(), bSwimWarnedThisDive))
	{
		return false;
	}
	bSwimWarnedThisDive = true;
	if (IsValid(SwimWarnText))
	{
		SwimWarnText->ShowFor(LOCTEXT("HuntHudSwimWarn", "기운이 떨어져요 — 뭍으로!"), SwimWarnSeconds);
	}
	SnareHaptics::PlayLight(GetOwningPlayer());
	UE_LOG(LogSnarehaven, Log, TEXT("[Swim] 기운 경고 — %.0f/%.0f"), WatchedVitals->GetStamina(), WatchedVitals->GetMaxStamina());
	return true;
}

namespace
{
	// DESIGN 2.2 「위험 체력 알림」 붉은 테두리 — 두께는 화면 짧은 변이 아니라 고정 px(폰·PC 모두 가운데를 비운다).
	constexpr float LowHealthBandWidth = SnareLowHealth::EdgeBandWidth;   // 겹 하나의 두께 — 겹마다 안쪽으로 옅어져 번짐이 된다
	const FLinearColor LowHealthRed(1.f, 0.f, 0.f, 1.f);   // 순빨강 — 초록 위에서도 갈색으로 안 섞이게
}

void UHuntHudWidget::BuildLowHealthEdge(UCanvasPanel& Root)
{
	LowHealthEdgeLayer = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LowHealthEdgeLayer"));
	LowHealthEdgeLayer->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* const LayerSlot = Cast<UCanvasPanelSlot>(Root.AddChild(LowHealthEdgeLayer)))
	{
		LayerSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		LayerSlot->SetOffsets(FMargin(0.f));
	}
	LowHealthEdgeBands.Reset();
	// 겹 6 × 방향 4 (겹 0 = 가장자리). 위·아래는 가로로 꽉 차고, 왼·오른쪽은 위·아래 띠 사이를 채운다(모서리 겹침 방지).
	for (int32 Layer = 0; Layer < SnareLowHealth::EdgeBandLayers; ++Layer)
	{
		const float From = Layer * LowHealthBandWidth;
		const float Width = LowHealthBandWidth;
		for (int32 Side = 0; Side < 4; ++Side)
		{
			UBorder* const Band = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
				*FString::Printf(TEXT("LowHealthBand_%d_%d"), Layer, Side));
			Band->SetBrushColor(FLinearColor(LowHealthRed.R, LowHealthRed.G, LowHealthRed.B, 0.f));
			Band->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			UCanvasPanelSlot* const BandSlot = Cast<UCanvasPanelSlot>(LowHealthEdgeLayer->AddChild(Band));
			if (BandSlot == nullptr)
			{
				continue;
			}
			switch (Side)
			{
			case 0:   // 위
				BandSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
				BandSlot->SetOffsets(FMargin(0.f, From, 0.f, Width));
				break;
			case 1:   // 아래
				BandSlot->SetAnchors(FAnchors(0.f, 1.f, 1.f, 1.f));
				BandSlot->SetOffsets(FMargin(0.f, -From - Width, 0.f, Width));
				break;
			case 2:   // 왼쪽
				BandSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 1.f));
				BandSlot->SetOffsets(FMargin(From, From + Width, Width, From + Width));
				break;
			default:   // 오른쪽
				BandSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 1.f));
				BandSlot->SetOffsets(FMargin(-From - Width, From + Width, Width, From + Width));
				break;
			}
			LowHealthEdgeBands.Add(Band);
		}
	}
}

bool UHuntHudWidget::IsLowHealthEdgeVisible() const
{
	return IsValid(LowHealthEdgeLayer) && LowHealthEdgeLayer->GetVisibility() != ESlateVisibility::Collapsed;
}

void UHuntHudWidget::UpdateLowHealthEdge(double TimeSeconds)
{
	if (!IsValid(LowHealthEdgeLayer))
	{
		return;
	}
	float Strength = 0.f;
	if (IsValid(WatchedVitals))
	{
		Strength = SnareLowHealth::StateFor(WatchedVitals->GetHealth(), WatchedVitals->GetMaxHealth(), WatchedVitals->IsDead());
	}
	if (Strength <= 0.f)
	{
		LowHealthEdgeOpacity = 0.f;
		if (LowHealthEdgeLayer->GetVisibility() != ESlateVisibility::Collapsed)
		{
			LowHealthEdgeLayer->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}
	// 펄스: 심장 간격을 한 주기로 한 번 밝았다 어두워진다. 가장 밝을 때가 표 불투명도(상한 0.75 를 넘지 않음).
	const float Period = SnareLowHealth::BeatInterval(Strength);
	const float Phase = static_cast<float>(FMath::Fmod(TimeSeconds, static_cast<double>(Period))) / Period;
	const float Wave = 0.5f + 0.5f * FMath::Cos(Phase * 2.f * PI);   // 1 → 0 → 1
	LowHealthEdgeOpacity = SnareLowHealth::PulsedEdgeOpacity(Strength, Wave);
	LowHealthEdgeLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	for (int32 Index = 0; Index < LowHealthEdgeBands.Num(); ++Index)
	{
		if (UBorder* const Band = LowHealthEdgeBands[Index])
		{
			const float LayerScale = SnareLowHealth::EdgeLayerScale(Index / 4);
			Band->SetBrushColor(FLinearColor(LowHealthRed.R, LowHealthRed.G, LowHealthRed.B, LowHealthEdgeOpacity * LayerScale));
		}
	}
}

FText UHuntHudWidget::GetNoAmmoText()
{
	return LOCTEXT("HuntHudNoAmmo", "탄약이 없다 — 잡화점");
}

FText UHuntHudWidget::GetGunAmmoText() const
{
	if (!IsValid(WatchedMelee) || !WatchedMelee->IsHoldingGun())
	{
		return FText::GetEmpty();
	}
	const UEquipmentComponent* const Equipment = GetEquipment();
	const FItemRow* const Gun = IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Weapon) : nullptr;
	const UInventoryComponent* const Inventory = GetInventory();
	const int32 BagAmmo = Gun != nullptr && IsValid(Inventory) ? Inventory->GetItemCount(Gun->AmmoItemID) : 0;
	return FText::Format(LOCTEXT("HuntHudGunAmmo", "{0}/{1}"),
		FText::AsNumber(WatchedMelee->GetLoadedRounds()), FText::AsNumber(BagAmmo));
}

bool UHuntHudWidget::IsGunHudVisible() const
{
	return IsValid(GunReloadButton) && GunReloadButton->GetVisibility() != ESlateVisibility::Collapsed;
}

void UHuntHudWidget::UpdateGunHud()
{
	const FText Wanted = GetGunAmmoText();
	const ESlateVisibility Shown = Wanted.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	if (IsValid(GunReloadButton) && GunReloadButton->GetVisibility() != Shown)
	{
		GunReloadButton->SetVisibility(Shown);
	}
	// WBP 가 글자만 주면 글자를, 자리표시면 카드를 접는다.
	if (UWidget* const AmmoShell = IsValid(GunAmmoCard) ? static_cast<UWidget*>(GunAmmoCard) : GunAmmoText.Get())
	{
		AmmoShell->SetVisibility(Wanted.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (IsValid(GunAmmoText))
	{
		if (!GunAmmoText->GetText().EqualTo(Wanted))
		{
			GunAmmoText->SetText(Wanted);
		}
	}
	UpdateGunAmmoIcon();
}

void UHuntHudWidget::UpdateGunAmmoIcon()
{
	if (!IsValid(GunAmmoIcon))
	{
		return;
	}
	FName AmmoRow = NAME_None;
	if (IsValid(WatchedMelee) && WatchedMelee->IsHoldingGun())
	{
		const UEquipmentComponent* const Equipment = GetEquipment();
		const FItemRow* const Gun = IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Weapon) : nullptr;
		AmmoRow = Gun != nullptr ? Gun->AmmoItemID : NAME_None;
	}
	if (AmmoRow == ShownAmmoIconRow)
	{
		return;
	}
	ShownAmmoIconRow = AmmoRow;
	// 표 행 이름(`Item_Ammo…`)이 아니라 `ItemID`(`item.ammo…`)로 찾는다 — `AmmoItemID` 는 인벤토리 ID 다.
	const FItemRow* Row = nullptr;
	if (!AmmoRow.IsNone() && IsValid(ItemTable))
	{
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Candidate = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Candidate != nullptr && Candidate->ItemID == AmmoRow)
			{
				Row = Candidate;
				break;
			}
		}
	}
	if (Row == nullptr || Row->IconRef.IsNull())
	{
		GunAmmoIcon->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	FSlateBrush IconBrush;
	IconBrush.SetResourceObject(Row->IconRef.LoadSynchronous());
	IconBrush.ImageSize = FVector2D(PlaceholderAmmoIconSize, PlaceholderAmmoIconSize);
	GunAmmoIcon->SetBrush(IconBrush);
	GunAmmoIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
}

bool UHuntHudWidget::TryReloadGun()
{
	return IsValid(WatchedMelee) && WatchedMelee->RequestReload();
}

void UHuntHudWidget::HandleGunReloadClicked()
{
	TryReloadGun();
}

void UHuntHudWidget::HandleGunReloadReply(bool bAccepted, const FString& Code)
{
	// 탄약이 없을 때만 알약 — 1.4 「…가 필요」 알약 길. 네트워크 실패는 로그(근접 컴포넌트)로만 남긴다.
	if (!bAccepted && Code == TEXT("not_enough_items"))
	{
		ShowErrorBanner(GetNoAmmoText());
	}
	UpdateGunHud();
}

FText UHuntHudWidget::GetInteractVerbText(ESnareInteractVerb Verb)
{
	// 레퍼런스는 대상 옆에 상호작용 표시를 띄운다(`hud_ingame.md` 12행) — 우리는 버튼이 하나라 버튼 글자로 같은 일을 한다.
	switch (Verb)
	{
	case ESnareInteractVerb::PickUp:  return LOCTEXT("HuntHudVerbPickUp", "줍기");
	case ESnareInteractVerb::Chop:    return LOCTEXT("HuntHudVerbChop", "베기");
	case ESnareInteractVerb::Mine:    return LOCTEXT("HuntHudVerbMine", "캐기");
	case ESnareInteractVerb::Harvest: return LOCTEXT("HuntHudVerbHarvest", "수확");
	case ESnareInteractVerb::Place:   return LOCTEXT("HuntHudVerbPlace", "넣기");
	case ESnareInteractVerb::Open:    return LOCTEXT("HuntHudVerbOpen", "열기");
	case ESnareInteractVerb::ShakeTree: return LOCTEXT("HuntHudVerbShakeTree", "따기");
	case ESnareInteractVerb::ChopFruitTree: return LOCTEXT("HuntHudVerbChopFruitTree", "베기");
	case ESnareInteractVerb::PlantFruit: return LOCTEXT("HuntHudVerbPlantFruit", "심기");
	case ESnareInteractVerb::Gather:  return LOCTEXT("HuntHudVerbGather", "채집");
	case ESnareInteractVerb::Capture: return LOCTEXT("HuntHudVerbCapture", "포획");
	case ESnareInteractVerb::Fish:    return LOCTEXT("HuntHudVerbFish", "낚시");
	case ESnareInteractVerb::NeedHatchet: return LOCTEXT("HuntHudVerbNeedHatchet", "손도끼 필요");
	case ESnareInteractVerb::NeedPickaxe: return LOCTEXT("HuntHudVerbNeedPickaxe", "곡괭이 필요");
	case ESnareInteractVerb::NeedRod:     return LOCTEXT("HuntHudVerbNeedRod", "낚싯대 필요");
	case ESnareInteractVerb::QuitFishing: return LOCTEXT("HuntHudVerbQuitFishing", "그만두기");
	case ESnareInteractVerb::LightBeacon: return LOCTEXT("HuntHudVerbLightBeacon", "불 켜기");
	case ESnareInteractVerb::Attack:
	default:                          return LOCTEXT("HuntHudAttackButton", "공격");
	}
}

float UHuntHudWidget::GetVerbDiscTextRadius()
{
	return PlaceholderRoundButtonSize * 0.5f - PlaceholderVerbRimMargin;
}

FText UHuntHudWidget::GetVerbDiscText(const FText& Verb)
{
	// 「손도끼 필요」처럼 두 낱말이면 두 줄로 — 원 버튼은 가로가 좁다 (10-16 HUD 3차 사람 지적 「원 안에 다 안 들어가네」).
	return FText::FromString(Verb.ToString().Replace(TEXT(" "), TEXT("\n")));
}

float UHuntHudWidget::GetVerbDiscFontSize(const FText& DiscText)
{
	if (!FSlateApplication::IsInitialized() || FSlateApplication::Get().GetRenderer() == nullptr)
	{
		return PlaceholderVerbFontSize;
	}
	FSlateFontInfo Font = GetDefault<UTextBlock>()->GetFont();
	Font.Size = PlaceholderVerbFontSize;
	const FVector2D Measured = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(DiscText.ToString(), Font);
	// 글자 네모의 모서리까지 거리(반대각선)가 글자 원 반지름을 넘으면 그만큼 줄인다 — 짧고 납작한 한 줄은 정사각보다 넓게 쓸 수 있다.
	const float HalfDiagonal = FMath::Max(static_cast<float>(Measured.Size()) * 0.5f, 1.f);
	const float Scale = FMath::Min(1.f, GetVerbDiscTextRadius() / HalfDiagonal);
	return FMath::FloorToFloat(PlaceholderVerbFontSize * Scale);
}

void UHuntHudWidget::SetAttackDiscText(const FText& Verb)
{
	const FText Disc = GetVerbDiscText(Verb);
	AttackButtonText->SetText(Disc);
	SnareUiStyle::SetFontSize(*AttackButtonText, GetVerbDiscFontSize(Disc));
}

FText UHuntHudWidget::GetAttackButtonText() const
{
	return GetInteractVerbText(IsValid(OwnerCharacter) ? OwnerCharacter->GetInteractVerb() : ESnareInteractVerb::Attack);
}

void UHuntHudWidget::UpdateAttackButtonText(double NowSeconds)
{
	// 하이라이트 훑기(0.6초, DESIGN 3.3)보다 짧으면 충분하다 — 그 사이엔 대상이 안 바뀐다. 화면만의 값이라 표에 두지 않는다.
	constexpr double AttackButtonTextRefreshSeconds = 0.2;
	if (!IsValid(AttackButtonText) || NowSeconds < AttackButtonTextNextSeconds)
	{
		return;
	}
	AttackButtonTextNextSeconds = NowSeconds + AttackButtonTextRefreshSeconds;
	const FText Wanted = GetAttackButtonText();
	if (!AttackButtonText->GetText().EqualTo(GetVerbDiscText(Wanted)))
	{
		SetAttackDiscText(Wanted);
	}
	UpdateNeedToolHint();   // 같은 대상 훑기를 보므로 같은 간격이다
	CheckUsageHintForApproach();
}

FText UHuntHudWidget::GetNeedToolHintText() const
{
	// 버튼이 「손도끼 필요」「곡괭이 필요」일 때만 어디서 사는지 한 줄 더 (INBOX 18:10 ②). 가방에 있으면 버튼이
	// 누를 때 알아서 드니(INBOX 22:00 ①) 「들어요」 안내는 더 필요 없다.
	const ESnareInteractVerb Verb = IsValid(OwnerCharacter) ? OwnerCharacter->GetInteractVerb() : ESnareInteractVerb::Attack;
	if (Verb != ESnareInteractVerb::NeedHatchet && Verb != ESnareInteractVerb::NeedPickaxe)
	{
		return FText::GetEmpty();
	}
	const UGatherComponent* const Gather = OwnerCharacter->GetGather();
	const AGatherableActor* const Need = IsValid(Gather) ? Gather->FindNearestNeedingTool() : nullptr;
	return IsValid(Need) ? UGatherComponent::GetNeedToolHint(Need->GetRequiredToolItemID(), false) : FText::GetEmpty();
}

bool UHuntHudWidget::IsNeedToolHintVisible() const
{
	return IsValid(NeedToolHint) && NeedToolHint->GetVisibility() != ESlateVisibility::Collapsed;
}

void UHuntHudWidget::UpdateNeedToolHint()
{
	if (!IsValid(NeedToolHint) || !IsValid(NeedToolHintText))
	{
		return;
	}
	const FText Wanted = GetNeedToolHintText();
	if (Wanted.IsEmpty())
	{
		NeedToolHint->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (!NeedToolHintText->GetText().EqualTo(Wanted))
	{
		NeedToolHintText->SetText(Wanted);
	}
	PlaceAboveAttackButton(NeedToolHint);
	NeedToolHint->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UHuntHudWidget::PlaceAboveAttackButton(UBorder* Pill) const
{
	// 포탈 버튼이 공격 버튼 바로 위 같은 자리에 서므로, 그게 떠 있으면 한 칸 위로 비킨다.
	if (UCanvasPanelSlot* const HintSlot = IsValid(Pill) ? Cast<UCanvasPanelSlot>(Pill->Slot) : nullptr)
	{
		const bool bAboveTravel = IsValid(ZoneTravelButton) && ZoneTravelButton->GetVisibility() != ESlateVisibility::Collapsed;
		const float ButtonTop = -PlaceholderScreenMargin - PlaceholderBoltHeight - PlaceholderButtonGap - PlaceholderRoundButtonSize;
		const float Lift = PlaceholderHintGap + (bAboveTravel ? PlaceholderContextButtonHeight + PlaceholderButtonGap : 0.f);
		HintSlot->SetPosition(FVector2D(-SnareUiStyle::PillarClearRight, ButtonTop - Lift));
	}
}

bool UHuntHudWidget::IsUsageHintVisible() const
{
	return IsValid(UsageHint) && UsageHint->GetVisibility() != ESlateVisibility::Collapsed;
}

bool UHuntHudWidget::IsUsageHintBlocked() const
{
	// 두 안내가 겹치면 글이 화면을 덮는다 — 첫 실행 안내가 안 끝났거나 판이 열려 있으면 기다린다(명세 2 「실패·예외」).
	return SnareUsageHints::IsBlocked(GetWorld());
}

void UHuntHudWidget::CheckUsageHintForHeldItems()
{
	UEquipmentComponent* const Equipment = GetEquipment();
	if (!IsValid(Equipment))
	{
		return;
	}
	// 손 칸 둘(도구·몽둥이) 중 **바뀐 칸만** 계기다 — 옷·모자를 갈아입는 알림은 지나간다.
	const FName Tool = Equipment->GetEquippedItem(EEquipSlot::Tool);
	const FName Weapon = Equipment->GetEquippedItem(EEquipSlot::Weapon);
	const bool bToolChanged = Tool != UsageHintLastTool;
	const bool bWeaponChanged = Weapon != UsageHintLastWeapon;
	UsageHintLastTool = Tool;
	UsageHintLastWeapon = Weapon;
	if (!bToolChanged && !bWeaponChanged)
	{
		return;
	}
	if (UsageHintTable == nullptr)
	{
		UsageHintTable = SnareUsageHints::LoadTable();
	}
	for (const EEquipSlot HandSlot : {EEquipSlot::Tool, EEquipSlot::Weapon})
	{
		if ((HandSlot == EEquipSlot::Tool ? !bToolChanged : !bWeaponChanged))
		{
			continue;
		}
		const FItemRow* const Row = Equipment->GetEquippedRow(HandSlot);
		if (Row != nullptr && TryShowUsageHint(SnareUsageHints::FindKeyForItem(UsageHintTable, Row->ItemID)))
		{
			return;
		}
	}
}

FName UHuntHudWidget::PickApproachHintKey(ESnareInteractVerb Verb, bool bNearGeneralShop, EPlaceableFunction NearFunction)
{
	return SnareUsageHints::PickApproachKey(Verb, bNearGeneralShop, NearFunction);
}

void UHuntHudWidget::CheckUsageHintForApproach()
{
	if (!IsValid(OwnerCharacter))
	{
		return;
	}
	const FName Key = SnareUsageHints::FindApproachKey(OwnerCharacter);
	if (Key == UsageHintLastApproach)
	{
		return;
	}
	UsageHintLastApproach = Key;
	if (!Key.IsNone() && UsageHintTable == nullptr)
	{
		UsageHintTable = SnareUsageHints::LoadTable();
	}
	TryShowUsageHint(Key);
}

bool UHuntHudWidget::TryShowUsageHint(FName Key)
{
	// 한 번에 하나 — 떠 있는 동안 온 키는 줄 세우지 않고 버린다(본 것 아님, 다음 기회에 뜬다).
	if (Key.IsNone() || !UsageHintKey.IsNone() || IsUsageHintBlocked())
	{
		return false;
	}
	const FUsageHintRow* const Row =
		SnareUsageHints::ClaimRow(UsageHintTable, Key, SnareUsageHints::ResolveSection(UsageHintSection));
	if (Row == nullptr)
	{
		return false;
	}
	UsageHintKey = Key;
	UsageHintLine = Row->Line;
	UsageHintSecondsLeft = Row->ShowSeconds;
	UE_LOG(LogSnarehaven, Log, TEXT("[UsageHint] 처음 안내 %s — %s (%.1f초)"), *Key.ToString(), *UsageHintLine.ToString(),
		UsageHintSecondsLeft);
	UpdateUsageHintPill();
	return true;
}

void UHuntHudWidget::TickUsageHint(float DeltaSeconds)
{
	if (UsageHintKey.IsNone())
	{
		return;
	}
	// 해야 할 일을 막는 도구 안내 알약이 이긴다 — 그동안 안내 줄은 접혀 기다리고 시간도 안 간다(명세 2 「화면」).
	if (!IsNeedToolHintVisible() && !IsUsageHintBlocked())
	{
		UsageHintSecondsLeft -= DeltaSeconds;
	}
	if (UsageHintSecondsLeft <= 0.f)
	{
		UsageHintKey = NAME_None;
		UsageHintLine = FText::GetEmpty();
		UsageHintSecondsLeft = 0.f;
	}
	UpdateUsageHintPill();
}

void UHuntHudWidget::UpdateUsageHintPill()
{
	if (!IsValid(UsageHint) || !IsValid(UsageHintText))
	{
		return;
	}
	if (UsageHintKey.IsNone() || IsNeedToolHintVisible() || IsUsageHintBlocked())
	{
		UsageHint->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (!UsageHintText->GetText().EqualTo(UsageHintLine))
	{
		UsageHintText->SetText(UsageHintLine);
	}
	PlaceAboveAttackButton(UsageHint);
	UsageHint->SetVisibility(ESlateVisibility::HitTestInvisible);
}

FText UHuntHudWidget::GetCaptureMissText()
{
	return LOCTEXT("HuntHudCaptureMiss", "놓쳤다!");
}

bool UHuntHudWidget::IsCaptureRingVisible() const
{
	return IsValid(CaptureRing) && CaptureRing->GetVisibility() != ESlateVisibility::Collapsed;
}

void UHuntHudWidget::BuildCaptureRing()
{
	UCanvasPanel* const Root = WidgetTree != nullptr ? Cast<UCanvasPanel>(WidgetTree->RootWidget) : nullptr;
	if (Root == nullptr || IsValid(CaptureRing))
	{
		return;
	}
	CaptureRing = WidgetTree->ConstructWidget<USnareCaptureRingWidget>(USnareCaptureRingWidget::StaticClass(), TEXT("CaptureRing"));
	// 맨 아래 칸에 넣어 카드·버튼 뒤에 그린다 — 링은 월드 위 표시라 HUD 글자를 가리면 안 된다.
	if (UCanvasPanelSlot* const RingSlot = Cast<UCanvasPanelSlot>(Root->InsertChildAt(0, CaptureRing)))
	{
		RingSlot->SetAnchors(FAnchors(0.f, 0.f));
		RingSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		RingSlot->SetAutoSize(true);
	}
	CaptureRing->SetVisibility(ESlateVisibility::Collapsed);
}

void UHuntHudWidget::UpdateCaptureRing()
{
	if (!IsValid(CaptureRing))
	{
		return;
	}

	AAnimalCharacter* Target = nullptr;
	float Elapsed = 0.f;
	const float RingSeconds = IsValid(WatchedCapture) ? WatchedCapture->GetCaptureRingSeconds() : 0.f;
	if (IsValid(WatchedCapture) && WatchedCapture->IsCaptureRingActive())
	{
		Target = WatchedCapture->GetCaptureRingTarget();
		Elapsed = WatchedCapture->GetCaptureRingElapsed();
	}
	else if (bCaptureRingHold && RingSeconds > 0.f && IsValid(WatchedCapture) && WatchedCapture->CanCapture())
	{
		// 검사 스위치: 헤드리스엔 창 안 순간에 셔터를 맞출 손가락이 없다 — 창 한가운데 순간을 멈춰 그린다(판정 없음).
		Target = WatchedCapture->GetCaptureTarget();
		Elapsed = RingSeconds - WatchedCapture->GetCaptureWindowFor(Target) * 0.5f;
	}

	if (!IsValid(Target))
	{
		CaptureRing->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	CaptureRing->UpdateRingAt(Elapsed, RingSeconds, WatchedCapture->GetCaptureWindowFor(Target));

	// 발밑 = 캡슐 바닥. 화면 밖(카메라 뒤)으로 나가면 숨긴다. 조종자가 없는 검사 월드에선 자리만 안 옮긴다.
	FVector Foot = Target->GetActorLocation();
	if (const UCapsuleComponent* const Capsule = Target->GetCapsuleComponent())
	{
		Foot.Z -= Capsule->GetScaledCapsuleHalfHeight();
	}
	if (APlayerController* const Controller = GetOwningPlayer())
	{
		FVector2D ScreenPosition;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Controller, Foot, ScreenPosition, false))
		{
			CaptureRing->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
		if (UCanvasPanelSlot* const RingSlot = Cast<UCanvasPanelSlot>(CaptureRing->Slot))
		{
			RingSlot->SetPosition(ScreenPosition);
		}
	}
	CaptureRing->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UHuntHudWidget::UpdateBuffSlots()
{
	// 버프는 소유 기기 바이탈이 걸고 줄인다(`ApplyItemBuff` 는 먹은 기기에서 먼저 돈다) — HUD 는 그 값을 읽기만 한다.
	for (int32 Index = 0; Index < BuffSlots.Num() && Index < UE_ARRAY_COUNT(HudBuffKinds); ++Index)
	{
		UBorder* const BuffSlot = BuffSlots[Index];
		UTextBlock* const Text = BuffSlotTexts.IsValidIndex(Index) ? BuffSlotTexts[Index].Get() : nullptr;
		if (!IsValid(BuffSlot) || !IsValid(Text))
		{
			continue;
		}
		const float SecondsLeft = IsValid(WatchedVitals) ? WatchedVitals->GetBuffSecondsLeft(HudBuffKinds[Index]) : 0.f;
		if (SecondsLeft <= 0.f)
		{
			BuffSlot->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		// 올림 — 0.4초 남은 버프를 `0초` 로 보여 주면 이미 끝난 것처럼 읽힌다.
		Text->SetText(FText::Format(LOCTEXT("HuntHudBuffSlot", "{0} {1}초"),
			BuffSlotName(HudBuffKinds[Index]), FText::AsNumber(FMath::CeilToInt(SecondsLeft))));
		BuffSlot->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

int32 UHuntHudWidget::GetVisibleBuffSlotCount() const
{
	int32 Count = 0;
	for (const UBorder* BuffSlot : BuffSlots)
	{
		Count += IsValid(BuffSlot) && BuffSlot->GetVisibility() != ESlateVisibility::Collapsed ? 1 : 0;
	}
	return Count;
}

FText UHuntHudWidget::GetBuffSlotText(EBuffKind Kind) const
{
	const int32 Index = static_cast<int32>(Kind) - 1;
	if (!BuffSlots.IsValidIndex(Index) || !IsValid(BuffSlots[Index])
		|| BuffSlots[Index]->GetVisibility() == ESlateVisibility::Collapsed
		|| !BuffSlotTexts.IsValidIndex(Index) || !IsValid(BuffSlotTexts[Index]))
	{
		return FText::GetEmpty();
	}
	return BuffSlotTexts[Index]->GetText();
}

TArray<const FItemRow*> UHuntHudWidget::FindCarriedPassiveRows(const UDataTable* Table, const UInventoryComponent* Inventory)
{
	TArray<const FItemRow*> Found;
	if (!IsValid(Table) || !IsValid(Inventory))
	{
		return Found;
	}
	// 어느 도구가 소지 효과인지는 표 칸(`bPassiveWhileCarried`)이 정한다 — 도구 ID 를 여기 적지 않는다.
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->bPassiveWhileCarried && Inventory->GetItemCount(Row->ItemID) > 0)
		{
			Found.Add(Row);
		}
	}
	return Found;
}

FText UHuntHudWidget::MakeHeldGearText(const FItemRow* ToolRow, const TArray<const FItemRow*>& CarriedRows)
{
	TArray<FText> Lines;
	auto AddLine = [&Lines](const FItemRow* Row)
	{
		const FText Line = MakeGearLine(Row);
		if (!Line.IsEmpty())
		{
			Lines.Add(Line);
		}
	};
	AddLine(ToolRow);
	for (const FItemRow* const Carried : CarriedRows)
	{
		if (Carried != ToolRow)
		{
			AddLine(Carried);
		}
	}
	return FText::Join(LOCTEXT("HuntHudGearLineJoin", "  "), Lines);
}

FText UHuntHudWidget::MakeGearLine(const FItemRow* ToolRow)
{
	if (ToolRow == nullptr)
	{
		return FText::GetEmpty();
	}
	// 무엇이 효과인지는 **표 칸**이 정한다 — 도구 ID 목록을 여기 적으면 표가 늘 때 한쪽만 늙는다.
	TArray<FText> Effects;
	if (ToolRow->CameraDistanceMul > 0.f && !FMath::IsNearlyEqual(ToolRow->CameraDistanceMul, 1.f))
	{
		Effects.Add(FText::Format(LOCTEXT("HuntHudGearView", "시야 ×{0}"),
			FText::AsNumber(ToolRow->CameraDistanceMul)));
	}
	if (ToolRow->bShowsMapCoordinates)
	{
		Effects.Add(LOCTEXT("HuntHudGearCoord", "좌표 보임"));
	}
	if (ToolRow->FishingSpeedMul > 0.f && !FMath::IsNearlyEqual(ToolRow->FishingSpeedMul, 1.f))
	{
		// 표의 배율은 **입질 대기를 나눈다**(8.13) — 크면 빨리 문다.
		Effects.Add(FText::Format(LOCTEXT("HuntHudGearFishing", "입질 ×{0}"),
			FText::AsNumber(ToolRow->FishingSpeedMul)));
	}
	if (Effects.Num() == 0)
	{
		return FText::GetEmpty();   // 손도끼처럼 "들면 벨 수 있다" 뿐인 도구는 줄이 없다 — 빈 줄을 띄우지 않는다
	}
	return FText::Format(LOCTEXT("HuntHudHeldGear", "{0} · {1}"),
		ToolRow->DisplayName.IsEmpty() ? FText::FromName(ToolRow->ItemID) : ToolRow->DisplayName,
		FText::Join(LOCTEXT("HuntHudGearJoin", " · "), Effects));
}

void UHuntHudWidget::UpdateHeldGearLine()
{
	if (!IsValid(HeldGearSlot) || !IsValid(HeldGearSlotText))
	{
		return;
	}
	const UEquipmentComponent* const Equipment = GetEquipment();
	const FText Line = MakeHeldGearText(IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Tool) : nullptr,
		FindCarriedPassiveRows(ItemTable, GetInventory()));
	if (!HeldGearSlotText->GetText().EqualTo(Line))
	{
		HeldGearSlotText->SetText(Line);
	}
	HeldGearSlot->SetVisibility(Line.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

FText UHuntHudWidget::GetHeldGearText() const
{
	if (!IsValid(HeldGearSlot) || !IsValid(HeldGearSlotText)
		|| HeldGearSlot->GetVisibility() == ESlateVisibility::Collapsed)
	{
		return FText::GetEmpty();
	}
	return HeldGearSlotText->GetText();
}

double UHuntHudWidget::GetTreasureRestoreRetrySeconds()
{
	return TreasureRestoreRetrySeconds;
}

void UHuntHudWidget::UpdateTreasureRestore(UBackpackComponent* Backpack, double NowSeconds)
{
	if (TreasureRestoreFor.Get() != Backpack)
	{
		// 새로 선 캐릭터(존 이동) — 처음부터 다시 묻는다. 옛 배낭의 늦은 답은 아래 콜백이 버린다.
		TreasureRestoreFor = Backpack;
		bTreasureRestored = false;
		bTreasureRestorePending = false;
		TreasureRestoreNextAskSeconds = NowSeconds;
	}
	if (!IsValid(Backpack) || bTreasureRestored || bTreasureRestorePending || NowSeconds < TreasureRestoreNextAskSeconds)
	{
		return;
	}

	// 다음 물음 시각은 **물은 순간**부터 잰다 — 답이 늦게 와도 간격보다 자주 묻지 않는다.
	TreasureRestoreNextAskSeconds = NowSeconds + TreasureRestoreRetrySeconds;
	const TWeakObjectPtr<UHuntHudWidget> WeakThis(this);
	const TWeakObjectPtr<UBackpackComponent> WeakBackpack(Backpack);
	bTreasureRestorePending = Backpack->RequestRestoreTreasure([WeakThis, WeakBackpack](bool bAccepted)
		{
			UHuntHudWidget* const Self = WeakThis.Get();
			if (Self == nullptr || Self->TreasureRestoreFor != WeakBackpack)
			{
				return;
			}
			Self->bTreasureRestorePending = false;
			Self->bTreasureRestored = bAccepted;
			if (!bAccepted)
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Chest] GetTreasure 실패 - %.0f초 뒤 다시 묻는다(아는 표식은 그대로)"),
					TreasureRestoreRetrySeconds);
			}
		});
}

bool UHuntHudWidget::UpdateOutpostBanner(double NowSeconds)
{
	if (!IsValid(OwnerCharacter) || !IsValid(OutpostBannerText))
	{
		return false;
	}
	if (OutpostTable == nullptr)
	{
		OutpostTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/World/DT_OutpostTable.DT_OutpostTable"));
	}
	const FName Entered = SnareOutpostBanner::FindEntered(OutpostTable, OwnerCharacter->GetActorLocation());
	FName ShowId;
	if (!OutpostBannerTracker.Update(Entered, NowSeconds, ShowId))
	{
		return false;
	}
	const FOutpostRow* const Row = OutpostTable->FindRow<FOutpostRow>(ShowId, TEXT("UpdateOutpostBanner"));
	if (Row == nullptr)
	{
		return false;
	}
	OutpostBannerText->ShowFor(Row->DisplayName, SnareOutpostBanner::ShowSeconds);
	return true;
}

void UHuntHudWidget::UpdateMinimap()
{
	// 이벤트 구역은 호스트가 세워 복제한 액터다 — 떠 있으면 표식·남은 시간, 사라지면 접힌다.
	SnareMinimapHud::UpdateEventMarker(MinimapEventDot, MinimapEventText, MinimapBounds,
		ADangerZoneActor::FindWorldEvent(GetWorld()));

	if (!IsValid(OwnerCharacter))
	{
		return;
	}

	const FVector Location = OwnerCharacter->GetActorLocation();
	if (IsValid(MinimapPlayerDot))
	{
		SnareMinimapHud::MoveDot(*MinimapPlayerDot, MinimapBounds, Location);
	}
	const UEquipmentComponent* const Equipment = GetEquipment();
	const FItemRow* const Tool = IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Tool) : nullptr;
	// 좌표는 든 도구 **또는 배낭에 가진** 소지 효과 도구(위성 통신기)가 켠다 — 버리면 꺼진다.
	bool bShowsCoordinates = Tool != nullptr && Tool->bShowsMapCoordinates;
	for (const FItemRow* const Carried : FindCarriedPassiveRows(ItemTable, GetInventory()))
	{
		bShowsCoordinates |= Carried->bShowsMapCoordinates;
	}
	SnareMinimapHud::UpdateCoordinates(MinimapCoordText, bShowsCoordinates, Location);

	// 스토리 목표 — 지금 단계의 상대가 이 맵에 서 있을 때만 점을 찍는다.
	{
		const AShopVendorActor* const StoryNpc = AShopVendorActor::FindStoryNpc(GetWorld(), SnareStory::GetCurrentTargetNpc());
		const FVector StoryLocation = StoryNpc != nullptr ? StoryNpc->GetActorLocation() : FVector::ZeroVector;
		SnareMinimapHud::UpdateStoryMarker(MinimapStoryDot, MinimapBounds, StoryNpc != nullptr ? &StoryLocation : nullptr);
	}

	// 보물(13.8) — 자리·마감은 서버 답이다. 상자가 이 맵에 섰을 때만 점을 찍는다.
	UBackpackComponent* const Backpack = OwnerCharacter->FindComponentByClass<UBackpackComponent>();
	// 존을 옮기면 캐릭터(배낭)가 새로 서서 표식을 잃는다 — 로그인 뒤 서버 기록에서 다시 받는다(M13-15),
	// 실패하면 간격 뒤 다시(M13-16).
	if (IsValid(WatchedBackend) && WatchedBackend->GetLoginState() == ESnareBackendLoginState::LoggedIn)
	{
		UpdateTreasureRestore(Backpack, GetWorld() != nullptr ? GetWorld()->GetRealTimeSeconds() : 0.0);
	}
	const SnareChestLoot::FTreasure* const Treasure = IsValid(Backpack) ? &Backpack->GetActiveTreasure() : nullptr;
	const AActor* const TreasureChest = IsValid(Backpack) ? Backpack->GetTreasureChest() : nullptr;
	const FVector TreasureLocation = TreasureChest != nullptr ? TreasureChest->GetActorLocation() : FVector::ZeroVector;
	SnareMinimapHud::UpdateTreasureMarker(MinimapTreasureDot, MinimapTreasureText, MinimapBounds,
		TreasureChest != nullptr ? &TreasureLocation : nullptr,
		Treasure != nullptr && Treasure->IsSet() ? Treasure->GetRemainingSeconds() : 0.0);

	// 칸 판정은 존 규칙과 **같은 함수**다 — 설원 체력 감소·늪 건축 불가가 보는 칸과 이름이 어긋나지 않는다.
	const FZoneRow* const Row = SnareZones::FindRegionAt(ZoneTable, MinimapLevelName, Location, ASeaActor::FindSea(this));
	if (Row != ShownMinimapRow)
	{
		if (IsValid(MinimapZoneText))
		{
			MinimapZoneText->SetText(SnareMinimapHud::FormatZoneName(Row));
		}
		if (IsValid(PillZoneText))
		{
			PillZoneText->SetText(SnareMinimapHud::FormatZoneName(Row));
		}
	}
	ShownMinimapRow = Row;

	// 지도는 볼 거리가 있을 때만 편다 — 좌표 도구·보물·월드 이벤트 (M15-02 기본 숨김).
	if (IsValid(MinimapLayer))
	{
		const bool bShowMap = bShowsCoordinates
			|| (Treasure != nullptr && Treasure->IsSet())
			|| ADangerZoneActor::FindWorldEvent(GetWorld()) != nullptr
			|| SnareStory::GetCurrentTargetNpc() != SnareStory::EStoryNpc::None;  // 스토리 목표 점이 있으면 편다
		// 3층 검사 전용 — `-SnareTouch=minimap` 이 누를 미니맵이 접혀 있으면 손가락이 갈 곳이 없다.
		FString CheckTouchName;
		const bool bCheckPressesMinimap = SnareCheckSwitch::Value(TEXT("SnareTouch="), CheckTouchName)
			&& CheckTouchName.Equals(TEXT("minimap"), ESearchCase::IgnoreCase);
		// 3층 검사 전용 `-SnareShowMinimap` — 누르지 않고 편 미니맵만 찍는다(스토리 목표 점 캡처).
		const bool bCheckShowsMinimap = SnareCheckSwitch::Param(TEXT("SnareShowMinimap"));
		MinimapLayer->SetVisibility((bShowMap || bCheckPressesMinimap || bCheckShowsMinimap) ?ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

TSharedRef<SWidget> UHuntHudWidget::RebuildWidget()
{
	// 미니맵이 볼 칸 범위는 WBP 디자인이 들어와도 같다 — 자리표시 여부와 상관없이 여기서 한 번 정한다.
	if (const UWorld* const World = GetWorld())
	{
		MinimapLevelName = FName(*UWorld::RemovePIEPrefix(World->GetMapName()));
	}
	const ASeaActor* const MapSea = ASeaActor::FindSea(this);
	const FBox2D Drawn = MapSea != nullptr ? MapSea->GetMapBounds() : FBox2D(ForceInit);
	MinimapBounds = Drawn.bIsValid ? Drawn : SnareMinimapHud::GetMapBounds(SnareMinimapHud::GetRegionRows(ZoneTable, MinimapLevelName));
	ShownMinimapRow = nullptr;

	// WBP 에 디자인이 들어오면 그 트리를 그대로 쓴다. 아직 비었을 때만 자리표시를 세운다.
	if (NeedsPlaceholderTree())
	{
		BuildPlaceholderTree();
	}
	// 버프 칸은 WBP 디자인이 들어와도 따로 챙긴다(M13-22) — 디자인에 칸이 없으면 여기서 만든다.
	BindOrBuildBuffSlots();
	// 올가미 링 화면(M21-09d)도 같은 이유로 따로 챙긴다 — 동물 발밑에 서는 것이라 WBP 귀퉁이 배치와 겹치지 않는다.
	BuildCaptureRing();
	bCaptureRingHold = SnareCheckSwitch::Param(TEXT("SnareCaptureRingHold"));

#if !UE_BUILD_SHIPPING
	// 3층 캡처용 자리표시 (`-SnareErrorDemo`, M26-04) — 헤드리스엔 손가락이 없어 거절이 한 번도 안 난다.
	// 실제 채집 거절 문구 하나를 타이머 없이 띄운다(캡처 지연이 3초보다 길어서). 화면 글자만 — 게임 상태는 그대로.
	if (SnareCheckSwitch::Param(TEXT("SnareErrorDemo")))
	{
		ErrorBannerText = UGatherComponent::GetRejectMessage(EGatherRejectReason::NeedTool, NAME_None);
		CaptureBannerText = FText::GetEmpty();
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 오류 알약 '%s' (-SnareErrorDemo)"), *ErrorBannerText.ToString());
	}

	// `-SnareDenySoundAt=2/2.3/6` — 3층 막힘 소리 확인 전용 (M55-05). 적은 초마다 사람 길(`ShowErrorBanner`)로 채집 거절 문구를
	// 띄운다. 소리·글자만 돌고 재화·인벤토리는 안 건드린다. 쉼표는 `FParse::Value` 가 끊으므로 `/`.
	FString DenySoundAtList;
	if (SnareCheckSwitch::Value(TEXT("SnareDenySoundAt="), DenySoundAtList))
	{
		TArray<FString> DenySeconds;
		DenySoundAtList.ParseIntoArray(DenySeconds, TEXT("/"));
		for (const FString& Seconds : DenySeconds)
		{
			const TWeakObjectPtr<UHuntHudWidget> WeakSelf(this);
			FTSTicker::GetCoreTicker().AddTicker(TEXT("DenySoundAt"), FCString::Atof(*Seconds),
				[WeakSelf](float)
				{
					if (UHuntHudWidget* const Self = WeakSelf.Get())
					{
						Self->ShowErrorBanner(UGatherComponent::GetRejectMessage(EGatherRejectReason::NeedTool, NAME_None));
					}
					return false;
				});
		}
	}
#endif

	if (IsValid(CaptureButton) && !CaptureButton->OnClicked.IsAlreadyBound(this, &UHuntHudWidget::HandleCaptureClicked))
	{
		CaptureButton->OnClicked.AddDynamic(this, &UHuntHudWidget::HandleCaptureClicked);
	}

	if (IsValid(AttackButton))
	{
		if (!AttackButton->OnPressed.IsAlreadyBound(this, &UHuntHudWidget::HandleAttackPressed))
		{
			AttackButton->OnPressed.AddDynamic(this, &UHuntHudWidget::HandleAttackPressed);
		}
		if (!AttackButton->OnReleased.IsAlreadyBound(this, &UHuntHudWidget::HandleAttackReleased))
		{
			AttackButton->OnReleased.AddDynamic(this, &UHuntHudWidget::HandleAttackReleased);
		}
	}

	// 장비 버튼은 포획 버튼과 같은 `OnClicked`(누르고 떼면 한 번) — 공격처럼 누르고 있을 일이 없다.
	// 부위마다 함수가 따로인 이유는 `OnClicked` 가 인자를 못 싣는 동적 델리게이트라서다 (헤더 주석).
	if (IsValid(EquipButton_Weapon)
		&& !EquipButton_Weapon->OnClicked.IsAlreadyBound(this, &UHuntHudWidget::HandleEquipWeaponClicked))
	{
		EquipButton_Weapon->OnClicked.AddDynamic(this, &UHuntHudWidget::HandleEquipWeaponClicked);
	}

	if (IsValid(EquipButton_Tool)
		&& !EquipButton_Tool->OnClicked.IsAlreadyBound(this, &UHuntHudWidget::HandleEquipToolClicked))
	{
		EquipButton_Tool->OnClicked.AddDynamic(this, &UHuntHudWidget::HandleEquipToolClicked);
	}

	if (IsValid(EquipButton_OutfitTop)
		&& !EquipButton_OutfitTop->OnClicked.IsAlreadyBound(this, &UHuntHudWidget::HandleEquipOutfitTopClicked))
	{
		EquipButton_OutfitTop->OnClicked.AddDynamic(this, &UHuntHudWidget::HandleEquipOutfitTopClicked);
	}

	if (IsValid(EquipButton_OutfitBottom)
		&& !EquipButton_OutfitBottom->OnClicked.IsAlreadyBound(
			this, &UHuntHudWidget::HandleEquipOutfitBottomClicked))
	{
		EquipButton_OutfitBottom->OnClicked.AddDynamic(this, &UHuntHudWidget::HandleEquipOutfitBottomClicked);
	}

	if (IsValid(GunReloadButton) && !GunReloadButton->OnClicked.IsAlreadyBound(this, &UHuntHudWidget::HandleGunReloadClicked))
	{
		GunReloadButton->OnClicked.AddDynamic(this, &UHuntHudWidget::HandleGunReloadClicked);
	}

	if (IsValid(ZoneTravelButton)
		&& !ZoneTravelButton->OnClicked.IsAlreadyBound(this, &UHuntHudWidget::HandleZoneTravelClicked))
	{
		// 포탈도 한 번 누르면 끝이라 장비·포획과 같은 `OnClicked` 다.
		ZoneTravelButton->OnClicked.AddDynamic(this, &UHuntHudWidget::HandleZoneTravelClicked);
	}

	RefreshDisplay();

	// 글자·버튼 모양은 화면마다 따로 정하지 않는다 — 네 화면이 같은 글꼴·색을 쓴다 (M5-05a·b).
	// `Super` **뒤**라야 한다 — Slate 위젯이 생긴 뒤에 칠해야 화면까지 바뀐다.
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	// 공격·포획은 누를 때마다 휘두르기·타격음이 따로 난다 — 딸깍까지 겹치면 시끄럽다.
	for (UButton* const Quiet : {AttackButton.Get(), CaptureButton.Get()})
	{
		if (Quiet != nullptr)
		{
			FButtonStyle Style = Quiet->GetStyle();
			Style.PressedSlateSound = FSlateSound();
			Quiet->SetStyle(Style);
		}
	}
	return Built;
}

bool UHuntHudWidget::NeedsPlaceholderTree() const
{
	if (WidgetTree == nullptr)
	{
		return false;
	}

	if (WidgetTree->RootWidget == nullptr)
	{
		return true;
	}

	// 위젯 블루프린트를 새로 만들면 **빈 CanvasPanel 하나**가 자동으로 들어간다.
	// 그건 아직 디자인이 아니라 빈 도화지라서, 자식이 하나도 없으면 자리표시를 채운다.
	const UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	return RootPanel != nullptr && RootPanel->GetChildrenCount() == 0;
}

void UHuntHudWidget::BuildPlaceholderTree()
{
	// WBP 가 만들어 둔 빈 캔버스가 있으면 그걸 쓴다 (루트를 갈아 끼우면 WBP 쪽 설정이 날아간다).
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HudRoot"));
		WidgetTree->RootWidget = Root;
	}

	// ── 위험 체력 붉은 테두리 (M60-03) — 맨 처음 넣어 다른 UI 아래에 깐다 ──
	BuildLowHealthEdge(*Root);

	// ── 검사용 자동 입력 띠 — 자동 공격·자동 이동 스위치가 켜진 실행에만. 사람이 진짜 플레이와 헷갈리지 않게 (출시 빌드는 스위치가 닫혀 안 뜬다) ──
	{
		FString MoveKeyProbe;
		int32 StoreShotProbe = 0;   // `-SnareStoreShot=` 스토어 스크린샷 실행은 띠를 안 띄운다(자동 입력이 곧 촬영 연출)
		float WalkStairsProbe = 0.f;   // `-SnareWalkStairs=` 계단 오르기도 자동 이동
		if ((SnareCheckSwitch::Param(TEXT("SnareAutoAttack")) || SnareCheckSwitch::Value(TEXT("SnareMoveKey="), MoveKeyProbe)
				|| SnareCheckSwitch::Value(TEXT("SnareWalkStairs="), WalkStairsProbe))
			&& !SnareCheckSwitch::Value(TEXT("SnareStoreShot="), StoreShotProbe))
		{
			UBorder* const AutoBand = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AutoInputBand"));
			AutoBand->SetBrushColor(FLinearColor(0.85f, 0.15f, 0.1f, 0.85f));
			AutoBand->SetPadding(FMargin(16.f, 4.f));
			AutoBand->SetVisibility(ESlateVisibility::HitTestInvisible);
			UTextBlock* const AutoText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AutoInputBandText"));
			AutoText->SetText(FText::FromString(TEXT("검사용 자동 입력 중")));
			SnareUiStyle::SetFontSize(*AutoText, 22.f);
			AutoText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			AutoBand->SetContent(AutoText);
			if (UCanvasPanelSlot* const BandSlot = Cast<UCanvasPanelSlot>(Root->AddChild(AutoBand)))
			{
				BandSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
				BandSlot->SetAlignment(FVector2D(0.5f, 0.f));
				BandSlot->SetAutoSize(true);
				BandSlot->SetZOrder(100);
			}
		}
	}

	// ── 마을 이름 띠 — 위 가운데, 동물 카드 아래 (흰 글자 + 진갈색 테, 큰 지도 마을 이름과 같은 꼴) ──
	OutpostBannerText = WidgetTree->ConstructWidget<USnareNoticeText>(USnareNoticeText::StaticClass(), TEXT("OutpostBannerText"));
	OutpostBannerText->SetJustification(ETextJustify::Center);
	SnareUiStyle::SetFontSize(*OutpostBannerText, 40.f);
	{
		FSlateFontInfo BannerFont = OutpostBannerText->GetFont();
		BannerFont.OutlineSettings.OutlineSize = 4;
		BannerFont.OutlineSettings.OutlineColor = SnareUiStyle::UiTextOnButton();
		OutpostBannerText->SetFont(BannerFont);
	}
	OutpostBannerText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	OutpostBannerText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* const BannerSlot = Cast<UCanvasPanelSlot>(Root->AddChild(OutpostBannerText)))
	{
		BannerSlot->SetAnchors(FAnchors(0.5f, 0.2f, 0.5f, 0.2f));
		BannerSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		BannerSlot->SetAutoSize(true);
	}

	// ── 위 가운데: 하이라이트된 동물 카드 (DESIGN 3.3 확정 — 크림 카드 300, 이름 18 + HP 바 10) ──
	AnimalCard = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("AnimalCard"));
	if (UCanvasPanelSlot* const CardSlot = Cast<UCanvasPanelSlot>(Root->AddChild(AnimalCard)))
	{
		CardSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		CardSlot->SetAlignment(FVector2D(0.5f, 0.f));
		CardSlot->SetPosition(FVector2D(0.f, PlaceholderScreenMargin));
		CardSlot->SetAutoSize(true);
	}
	UVerticalBox* const AnimalColumn =
		WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AnimalCardColumn"));
	AnimalCard->SetContent(AnimalColumn);

	AnimalNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AnimalNameText"));
	AnimalNameText->SetJustification(ETextJustify::Center);
	AnimalNameText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	AnimalNameText->SetMinDesiredWidth(PlaceholderAnimalCardWidth - PlaceholderCardPaddingX * 2.f);
	SnareUiStyle::SetFontSize(*AnimalNameText, PlaceholderCardFontSize);
	AnimalColumn->AddChildToVerticalBox(AnimalNameText);

	// 우두머리 고지 줄(M21-07g) — 이름 아래 작은 글자, 우두머리일 때만 편다.
	AnimalNoteText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AnimalNoteText"));
	AnimalNoteText->SetJustification(ETextJustify::Center);
	AnimalNoteText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*AnimalNoteText, PlaceholderCardFontSize - 4.f);
	AnimalNoteText->SetVisibility(ESlateVisibility::Collapsed);
	AnimalColumn->AddChildToVerticalBox(AnimalNoteText);

	AnimalHealthBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("AnimalHealthBar"));
	StyleThinBar(*AnimalHealthBar, SnareUiStyle::UiAccentRed(), SnareUiStyle::UiEmptySlot());
	if (UVerticalBoxSlot* const BarSlot = AnimalColumn->AddChildToVerticalBox(AnimalHealthBar))
	{
		BarSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}

	// ── 오른쪽 아래: 포획(맨 끝)·공격(그 왼쪽) 크림 네모 버튼(레퍼런스 빠른 슬롯) — 기력 번개 줄 위에 선다 ──
	const float ButtonBottom = -PlaceholderScreenMargin - PlaceholderBoltHeight - PlaceholderButtonGap;
	CaptureButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CaptureButton"));
	StyleWoodDiscButton(*CaptureButton);
	PlaceBottomRight(Cast<UCanvasPanelSlot>(Root->AddChild(CaptureButton)),
		FVector2D(-PlaceholderScreenMargin, ButtonBottom),
		FVector2D(PlaceholderRoundButtonSize, PlaceholderRoundButtonSize));

	CaptureButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CaptureButtonText"));
	CaptureButtonText->SetText(LOCTEXT("HuntHudCaptureButton", "포획"));
	CaptureButtonText->SetJustification(ETextJustify::Center);
	CaptureButtonText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
	SnareUiStyle::SetFontSize(*CaptureButtonText, GetVerbDiscFontSize(CaptureButtonText->GetText()));
	CaptureButton->AddChild(CaptureButtonText);

	// 포획 버튼 **왼쪽** (감독 지시 2026-09-20 12:05 (e)). 오른쪽 아래 기준이라 왼쪽 = 음수 방향.
	AttackButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("AttackButton"));
	StyleWoodDiscButton(*AttackButton);
	PlaceBottomRight(Cast<UCanvasPanelSlot>(Root->AddChild(AttackButton)),
		FVector2D(-PlaceholderScreenMargin - PlaceholderRoundButtonSize - PlaceholderButtonGap, ButtonBottom),
		FVector2D(PlaceholderRoundButtonSize, PlaceholderRoundButtonSize));

	AttackButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AttackButtonText"));
	AttackButtonText->SetJustification(ETextJustify::Center);
	AttackButtonText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
	SetAttackDiscText(GetInteractVerbText(ESnareInteractVerb::Attack));
	AttackButton->AddChild(AttackButtonText);

	// 공격 버튼 바로 위: 도구 안내 알약 — 맞는 도구 없이 나무·광맥 앞일 때만 (INBOX 18:10 ②). 자리는 `UpdateNeedToolHint` 가 잡는다.
	NeedToolHint = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("NeedToolHint"));
	NeedToolHint->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* const HintSlot = Cast<UCanvasPanelSlot>(Root->AddChild(NeedToolHint)))
	{
		HintSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
		HintSlot->SetAlignment(FVector2D(1.f, 1.f));
		HintSlot->SetAutoSize(true);
	}
	NeedToolHintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NeedToolHintText"));
	NeedToolHintText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*NeedToolHintText, PlaceholderCardFontSize);
	NeedToolHint->SetContent(NeedToolHintText);

	// 같은 자리: 처음 들기 안내 알약 (M28-05b) — 도구 안내 알약과 같은 모양, 둘이 같이 서지 않는다.
	UsageHint = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("UsageHint"));
	UsageHint->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* const UsageSlot = Cast<UCanvasPanelSlot>(Root->AddChild(UsageHint)))
	{
		UsageSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
		UsageSlot->SetAlignment(FVector2D(1.f, 1.f));
		UsageSlot->SetAutoSize(true);
	}
	UsageHintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("UsageHintText"));
	UsageHintText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*UsageHintText, PlaceholderCardFontSize);
	UsageHint->SetContent(UsageHintText);

	// ── 왼쪽 위: 알림 카드 — 포획 성공 같은 알림이 떠 있을 때만 (시트: 크림 카드가 세로로 쌓임) ──
	// 알림 줄 = 알림 카드 + 그 아래 업적·도감 카드 두 장 (M48-03c, DESIGN 8.15 「쌓기」).
	UVerticalBox* const NoticeStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NoticeStack"));
	NoticeStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UCanvasPanelSlot* const NoticeSlot = Cast<UCanvasPanelSlot>(Root->AddChild(NoticeStack)))
	{
		NoticeSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		NoticeSlot->SetAlignment(FVector2D(0.f, 0.f));
		NoticeSlot->SetPosition(FVector2D(PlaceholderScreenMargin, PlaceholderScreenMargin));
		NoticeSlot->SetAutoSize(true);
	}
	NoticeCard = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("NoticeCard"));
	if (UVerticalBoxSlot* const CardSlot = NoticeStack->AddChildToVerticalBox(NoticeCard))
	{
		CardSlot->SetHorizontalAlignment(HAlign_Left);
	}
	AchievementCards = WidgetTree->ConstructWidget<USnareNoticeCards>(USnareNoticeCards::StaticClass(), TEXT("AchievementCards"));
	AchievementCards->BuildCards(*WidgetTree);
	NoticeStack->AddChildToVerticalBox(AchievementCards);
	GuideText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GuideText"));
	GuideText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*GuideText, PlaceholderCardFontSize);
	NoticeCard->SetContent(GuideText);

	// ── 왼쪽 위: 오류 알약 — 거절·실패가 떠 있을 때만, 알림 카드와 같은 자리 (M26-04, 시트 「왼쪽 위(오류)」) ──
	// 글꼴 Jua 에 ⚠ 가 없어 왼쪽 표시는 `!` 글자다.
	ErrorPill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ErrorPill"));
	ErrorPill->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, ErrorPillMinHeight * 0.5f));
	ErrorPill->SetBrushColor(SnareUiStyle::UiAccentRed());
	ErrorPill->SetPadding(FMargin(ErrorPillPaddingX, ErrorPillPaddingY));
	ErrorPill->SetVerticalAlignment(VAlign_Center);
	ErrorPill->SetVisibility(ESlateVisibility::Collapsed);
	USizeBox* const ErrorBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ErrorPillBox"));
	ErrorBox->SetMinDesiredWidth(ErrorPillMinWidth);
	ErrorBox->SetMinDesiredHeight(ErrorPillMinHeight);
	ErrorBox->SetContent(ErrorPill);
	if (UCanvasPanelSlot* const ErrorSlot = Cast<UCanvasPanelSlot>(Root->AddChild(ErrorBox)))
	{
		ErrorSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		ErrorSlot->SetAlignment(FVector2D(0.f, 0.f));
		ErrorSlot->SetPosition(FVector2D(PlaceholderScreenMargin, PlaceholderScreenMargin));
		ErrorSlot->SetAutoSize(true);
	}
	UHorizontalBox* const ErrorRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ErrorRow"));
	ErrorPill->SetContent(ErrorRow);
	UTextBlock* const ErrorMark = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ErrorMark"));
	ErrorMark->SetText(LOCTEXT("HuntHudErrorMark", "!"));
	ErrorMark->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
	SnareUiStyle::SetFontSize(*ErrorMark, ErrorPillFontSize);
	if (UHorizontalBoxSlot* const MarkSlot = ErrorRow->AddChildToHorizontalBox(ErrorMark))
	{
		MarkSlot->SetVerticalAlignment(VAlign_Center);
		MarkSlot->SetPadding(FMargin(0.f, 0.f, ErrorMarkGap, 0.f));
	}
	ErrorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ErrorText"));
	ErrorText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
	SnareUiStyle::SetFontSize(*ErrorText, ErrorPillFontSize);
	if (UHorizontalBoxSlot* const TextSlot = ErrorRow->AddChildToHorizontalBox(ErrorText))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}

	// ── 가운데 위: 수영 기운 경고 — 오류 알약과 같은 빨간 알약, 동물 카드·마을 띠 아래 (M107-05 ③) ──
	SwimWarnPill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SwimWarnPill"));
	SwimWarnPill->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, ErrorPillMinHeight));
	SwimWarnPill->SetBrushColor(SnareUiStyle::UiAccentRed());
	SwimWarnPill->SetPadding(FMargin(ErrorPillPaddingX * 2.f, ErrorPillPaddingY * 2.f));
	SwimWarnPill->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* const WarnSlot = Cast<UCanvasPanelSlot>(Root->AddChild(SwimWarnPill)))
	{
		WarnSlot->SetAnchors(FAnchors(0.5f, 0.3f, 0.5f, 0.3f));
		WarnSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		WarnSlot->SetAutoSize(true);
	}
	SwimWarnText = WidgetTree->ConstructWidget<USnareNoticeText>(USnareNoticeText::StaticClass(), TEXT("SwimWarnText"));
	SwimWarnText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
	SnareUiStyle::SetFontSize(*SwimWarnText, SwimWarnFontSize);
	SwimWarnText->SetVisibility(ESlateVisibility::Collapsed);
	SwimWarnText->SetCompanion(SwimWarnPill);
	SwimWarnPill->SetContent(SwimWarnText);

	// 미니맵(M12-03)은 **기본 숨김** — 레퍼런스는 지도를 늘 펴 두지 않는다(M15-02). 자리는 왼쪽 위 그대로,
	// 좌표 도구를 들었거나 보물·월드 이벤트가 떠 있을 때만 펼친다(`UpdateMinimap`).
	MinimapLayer = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MinimapLayer"));
	MinimapLayer->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* const LayerSlot = Cast<UCanvasPanelSlot>(Root->AddChild(MinimapLayer)))
	{
		LayerSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		LayerSlot->SetOffsets(FMargin(0.f));
	}
	const SnareMinimapHud::FPlaceholder Minimap = SnareMinimapHud::AddPlaceholder(*WidgetTree, *MinimapLayer,
		SnareMinimapHud::GetRegionRows(ZoneTable, MinimapLevelName),
		FVector2D(PlaceholderScreenMargin, PlaceholderMinimapTop), ASeaActor::FindSea(this));
	MinimapPlayerDot = Minimap.PlayerDot;
	MinimapZoneText = Minimap.ZoneNameText;
	MinimapEventDot = Minimap.EventDot;
	MinimapEventText = Minimap.EventText;
	MinimapCoordText = Minimap.CoordText;
	MinimapStoryDot = Minimap.StoryDot;
	MinimapTreasureDot = Minimap.TreasureDot;
	MinimapTreasureText = Minimap.TreasureText;

	// 미니맵 그림 전체가 누름 칸 — 누르면 오른쪽 아래 지도 버튼과 같은 큰 지도가 열린다(INBOX 결함 10-11 23:05).
	MinimapTouchButton = Minimap.TouchButton;
	if (IsValid(MinimapTouchButton))
	{
		MinimapTouchButton->OnClicked.AddDynamic(this, &UHuntHudWidget::HandleMinimapClicked);
	}

	// 장비 버튼은 화면에서 뺀다 — 장비는 배낭 창 왼쪽 탭에서 바꾼다(M15-02, inventory_backpack 시트).
	// 버튼·동작은 그대로 두고 접힌 기둥에 넣는다 — 기존 입력 경로(`-SnareTouch=`·검사)가 이름으로 찾는다.
	UVerticalBox* const EquipColumn =
		WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EquipColumn"));
	EquipColumn->SetVisibility(ESlateVisibility::Collapsed);
	Root->AddChild(EquipColumn);
	for (const EEquipSlot EquipSlot : GetEquipButtonSlots())
	{
		const FName ButtonName = GetEquipButtonWidgetName(EquipSlot);
		UButton* const SlotButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), ButtonName);
		EquipColumn->AddChildToVerticalBox(SlotButton);

		UTextBlock* const SlotText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(*(ButtonName.ToString() + TEXT("Text"))));
		SlotText->SetJustification(ETextJustify::Center);
		SlotButton->AddChild(SlotText);

		SetEquipButtonWidgets(EquipSlot, SlotButton, SlotText);
		// 접근성 이름(10-16) — 글이 빈 동안에도 VoiceOver 가 읽게(`ButtonAccessibleName` 툴팁 단계).
		SlotButton->SetToolTipText(EquipSlot == EEquipSlot::Weapon ? LOCTEXT("EquipWeaponA11y", "무기")
			: EquipSlot == EEquipSlot::Tool ? LOCTEXT("EquipToolA11y", "도구")
			: EquipSlot == EEquipSlot::OutfitTop ? LOCTEXT("EquipTopA11y", "윗옷")
			: LOCTEXT("EquipBottomA11y", "아랫옷"));
	}

	// 포탈·선착장은 **곁에 있을 때만** 뜨는 작은 둥근 버튼 — 공격 버튼 위 (시트: 대상 옆 상호작용 프롬프트).
	// 버튼 한 벌은 거점 HUD 와 같은 곳에서 만든다 — **자리만** 화면마다 다르다 (M6-03b).
	UTextBlock* TravelText = nullptr;
	ZoneTravelButton = SnareZoneTravelHud::CreateButton(*WidgetTree, TravelText);
	ZoneTravelButtonText = TravelText;
	SnareUiStyle::MakeRoundButton(*ZoneTravelButton, PlaceholderContextButtonHeight * 0.5f);
	// 오른쪽 아이콘 기둥(2열, `SnareUiStyle::PillarClearRight`)이 공격 버튼 위까지 덮으므로 기둥 왼쪽에 선다.
	PlaceBottomRight(Cast<UCanvasPanelSlot>(Root->AddChild(ZoneTravelButton)),
		FVector2D(-SnareUiStyle::PillarClearRight,
			ButtonBottom - PlaceholderRoundButtonSize - PlaceholderButtonGap),
		FVector2D(PlaceholderContextButtonWidth, PlaceholderContextButtonHeight));

	// 공격 버튼 **왼쪽** 한 줄: 재장전 버튼(공격과 같은 폭 · 높이 72 — M38, 아랫변 맞춤) + 그 왼쪽 탄 글 `7/25`. 총을 들었을 때만 (M39-04).
	// 포획 위에 두었을 땐 오른쪽 아이콘 기둥(배낭·지도, 오른쪽 69~141)에 덮여 안 보였고, 공격 위는 존 이동 버튼 자리라
	// 둘 다 비켜 이 줄에 둔다 (M39-06b 루프#578 게임 카메라 캡처 — 2층 `UI.GunHud.ClearOfHudButtons`).
	const float ReloadRight = -PlaceholderScreenMargin - 2.f * (PlaceholderRoundButtonSize + PlaceholderButtonGap);
	GunReloadButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("GunReloadButton"));
	SnareUiStyle::MakeRoundButton(*GunReloadButton, SnareUiStyle::HudCornerRadius, PlaceholderRoundButtonOpacity);
	GunReloadButton->SetBackgroundColor(SnareUiStyle::UiButtonBase().CopyWithNewOpacity(PlaceholderRoundButtonOpacity));
	GunReloadButton->SetVisibility(ESlateVisibility::Collapsed);
	PlaceBottomRight(Cast<UCanvasPanelSlot>(Root->AddChild(GunReloadButton)),
		FVector2D(ReloadRight, ButtonBottom),
		FVector2D(PlaceholderRoundButtonSize, PlaceholderContextButtonHeight));
	GunReloadButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GunReloadButtonText"));
	GunReloadButtonText->SetText(LOCTEXT("HuntHudGunReload", "재장전"));
	GunReloadButtonText->SetJustification(ETextJustify::Center);
	GunReloadButtonText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*GunReloadButtonText, PlaceholderCardFontSize);
	GunReloadButton->AddChild(GunReloadButtonText);

	// 탄 글은 크림 카드 위 진한 글자 — 맨 땅 위 글자는 풀·눈 색에 묻힌다 (안내 알약과 같은 모양).
	GunAmmoCard = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("GunAmmoCard"));
	GunAmmoCard->SetVisibility(ESlateVisibility::Collapsed);
	GunAmmoText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GunAmmoText"));
	GunAmmoText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*GunAmmoText, PlaceholderHealthFontSize);
	// 탄 칸 = [탄 그림][7/25] — 그림은 든 총의 탄 아이템 `IconRef` (UpdateGunHud 가 채운다).
	UHorizontalBox* const AmmoRow =
		WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GunAmmoRow"));
	GunAmmoIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("GunAmmoIcon"));
	GunAmmoIcon->SetVisibility(ESlateVisibility::Collapsed);
	if (UHorizontalBoxSlot* const IconSlot = AmmoRow->AddChildToHorizontalBox(GunAmmoIcon))
	{
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	}
	if (UHorizontalBoxSlot* const TextSlot = AmmoRow->AddChildToHorizontalBox(GunAmmoText))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
	GunAmmoCard->SetContent(AmmoRow);
	if (UCanvasPanelSlot* const AmmoSlot = Cast<UCanvasPanelSlot>(Root->AddChild(GunAmmoCard)))
	{
		AmmoSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
		AmmoSlot->SetAlignment(FVector2D(1.f, 1.f));
		AmmoSlot->SetPosition(FVector2D(ReloadRight - PlaceholderRoundButtonSize - PlaceholderHintGap, ButtonBottom));
		AmmoSlot->SetAutoSize(true);
	}

	// ── 오른쪽 아래 맨 밑: 기력 = 노란 번개 10칸 한 줄 (시트 그대로, 수영할 때만 준다 — DESIGN 8.9) ──
	// 값은 보이지 않는 막대 하나에 남긴다 — 검사·WBP 가 `SetPercent` 하나만 보면 된다. 보이는 건 번개 칸이다.
	const float BoltRowWidth =
		VitalsGaugeSegmentCount * PlaceholderBoltWidth + (VitalsGaugeSegmentCount - 1) * PlaceholderBoltGap;
	PlayerStaminaBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("PlayerStaminaBar"));
	{
		FProgressBarStyle Style = PlayerStaminaBar->GetWidgetStyle();
		Style.BackgroundImage = FSlateNoResource();
		Style.FillImage = FSlateNoResource();
		Style.MarqueeImage = FSlateNoResource();
		PlayerStaminaBar->SetWidgetStyle(Style);
	}
	PlayerStaminaBar->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceBottomRight(Cast<UCanvasPanelSlot>(Root->AddChild(PlayerStaminaBar)),
		FVector2D(-PlaceholderScreenMargin, -PlaceholderScreenMargin),
		FVector2D(BoltRowWidth, PlaceholderBoltHeight));

	// M97-c-2: 번개 줄 뒤에 나무 눈금 판 — 칸 틀 `#74512F`, 밧줄 테두리 (DESIGN 7.1).
	UBorder* const BoltPlank = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StaminaBoltPlank"));
	BoltPlank->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, SnareUiStyle::WoodPlateRadius,
		SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));
	BoltPlank->SetBrushColor(SnareUiStyle::UiWoodButtonPressed());
	BoltPlank->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceBottomRight(Cast<UCanvasPanelSlot>(Root->AddChild(BoltPlank)),
		FVector2D(-PlaceholderScreenMargin + BoltPlankPad, -PlaceholderScreenMargin + BoltPlankPad),
		FVector2D(BoltRowWidth + PlaceholderBoltGap + PlaceholderStaminaIconSize + 2.f * BoltPlankPad,
			PlaceholderBoltHeight + 2.f * BoltPlankPad));

	UHorizontalBox* const BoltRow =
		WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("StaminaBoltRow"));
	BoltRow->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceBottomRight(Cast<UCanvasPanelSlot>(Root->AddChild(BoltRow)),
		FVector2D(-PlaceholderScreenMargin, -PlaceholderScreenMargin),
		FVector2D(BoltRowWidth, PlaceholderBoltHeight));
	static TSoftObjectPtr<UTexture2D> BoltTexture(FSoftObjectPath(TEXT("/Game/Game/UI/Icons/T_UI_Bolt.T_UI_Bolt")));
	static TSoftObjectPtr<UTexture2D> LightningTexture(
		FSoftObjectPath(TEXT("/Game/Game/UI/Icons/T_UI_Lightning.T_UI_Lightning")));
	UImage* const StaminaIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("StaminaGaugeIcon"));
	{
		FSlateBrush IconBrush;
		IconBrush.SetResourceObject(LightningTexture.LoadSynchronous());
		IconBrush.ImageSize = FVector2D(PlaceholderStaminaIconSize, PlaceholderStaminaIconSize);
		StaminaIcon->SetBrush(IconBrush);
	}
	StaminaIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceBottomRight(Cast<UCanvasPanelSlot>(Root->AddChild(StaminaIcon)),
		FVector2D(-PlaceholderScreenMargin - BoltRowWidth - PlaceholderBoltGap,
			-PlaceholderScreenMargin + 0.5f * (PlaceholderStaminaIconSize - PlaceholderBoltHeight)),
		FVector2D(PlaceholderStaminaIconSize, PlaceholderStaminaIconSize));
	StaminaBolts.Reset();
	for (int32 Index = 0; Index < VitalsGaugeSegmentCount; ++Index)
	{
		UImage* const Bolt = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), FName(*FString::Printf(TEXT("PlayerStaminaBolt_%d"), Index)));
		// ⚡ 그림 한 장(`tools/unreal/import_ui_bolt.py` — 흰 속 + 진갈색 테두리)에 칸 색을 곱한다.
		FSlateBrush BoltBrush;
		BoltBrush.SetResourceObject(BoltTexture.LoadSynchronous());
		BoltBrush.ImageSize = FVector2D(PlaceholderBoltWidth, PlaceholderBoltHeight);
		Bolt->SetBrush(BoltBrush);
		if (UHorizontalBoxSlot* const BoltSlot = BoltRow->AddChildToHorizontalBox(Bolt))
		{
			BoltSlot->SetPadding(FMargin(Index > 0 ? PlaceholderBoltGap : 0.f, 0.f, 0.f, 0.f));
		}
		StaminaBolts.Add(Bolt);
	}

	// ── 왼쪽 아래: 체력 = 작은 빨간 알약 (시트: 145×50, 흰 숫자 28px) — 조이스틱은 그 위 빈 공간 ──
	UBorder* const HealthPill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PlayerHealthPill"));
	// M97-c-2: 빨간 알약 → 밧줄 두른 나무 판자 (DESIGN 7.1) — 글은 크림.
	HealthPill->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, SnareUiStyle::WoodPlateRadius,
		SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));
	HealthPill->SetBrushColor(SnareUiStyle::UiWoodButton());
	HealthPill->SetPadding(FMargin(6.f, 0.f, PlaceholderCardPaddingX, 0.f));
	HealthPill->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(Root->AddChild(HealthPill)))
	{
		PillSlot->SetAnchors(FAnchors(0.f, 1.f, 0.f, 1.f));
		PillSlot->SetAlignment(FVector2D(0.f, 1.f));
		PillSlot->SetPosition(FVector2D(PlaceholderScreenMargin, -PlaceholderScreenMargin));
		PillSlot->SetSize(FVector2D(PlaceholderHealthPillWidth, PlaceholderHealthPillHeight));
	}
	UHorizontalBox* const HealthRow =
		WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PlayerHealthRow"));
	HealthPill->SetContent(HealthRow);

	// 무기 아이콘 = 민트 동그라미 위에 아이콘 (steam_13). 동그라미가 받침이고 그림은 그 안을 채운다.
	UBorder* const WeaponCircle = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PlayerWeaponCircle"));
	WeaponCircle->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, PlaceholderWeaponIconSize * 0.5f));
	WeaponCircle->SetBrushColor(SnareUiStyle::UiIconMint());
	WeaponCircle->SetPadding(FMargin(0.f));
	PlayerWeaponIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PlayerWeaponIcon"));
	{
		FSlateBrush EmptyBrush;
		EmptyBrush.ImageSize = FVector2D(PlaceholderWeaponIconSize, PlaceholderWeaponIconSize);
		PlayerWeaponIcon->SetBrush(EmptyBrush);
	}
	PlayerWeaponIcon->SetColorAndOpacity(FLinearColor::Transparent);
	WeaponCircle->SetContent(PlayerWeaponIcon);
	if (UHorizontalBoxSlot* const IconSlot = HealthRow->AddChildToHorizontalBox(WeaponCircle))
	{
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	}

	// 숫자가 무엇인지 — 빨간 하트(체력). 나무 판 위라 크림 테를 두른다.
	UTextBlock* const HeartText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PlayerHealthHeart"));
	HeartText->SetText(FText::FromString(TEXT("\u2665")));
	HeartText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiAccentRed()));
	{
		FSlateFontInfo HeartFont = HeartText->GetFont();
		HeartFont.Size = PlaceholderHeartFontSize;
		HeartFont.OutlineSettings.OutlineSize = PlaceholderHeartOutline;
		HeartFont.OutlineSettings.OutlineColor = SnareUiStyle::UiPanelCream();
		HeartText->SetFont(HeartFont);
	}
	if (UHorizontalBoxSlot* const HeartSlot = HealthRow->AddChildToHorizontalBox(HeartText))
	{
		HeartSlot->SetVerticalAlignment(VAlign_Center);
		HeartSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	}

	PlayerHealthText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PlayerHealthText"));
	PlayerHealthText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
	SnareUiStyle::SetFontSize(*PlayerHealthText, PlaceholderHealthNumberFontSize);
	if (UHorizontalBoxSlot* const TextSlot = HealthRow->AddChildToHorizontalBox(PlayerHealthText))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
		TextSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	}

	// 시트 알약엔 막대가 없다 — 값은 보이지 않는 막대에 남긴다(기력 막대와 같은 방식, 검사·WBP 는 `SetPercent` 만 본다).
	PlayerHealthBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("PlayerHealthBar"));
	{
		FProgressBarStyle Style = PlayerHealthBar->GetWidgetStyle();
		Style.BackgroundImage = FSlateNoResource();
		Style.FillImage = FSlateNoResource();
		Style.MarqueeImage = FSlateNoResource();
		PlayerHealthBar->SetWidgetStyle(Style);
	}
	if (UHorizontalBoxSlot* const BarSlot = HealthRow->AddChildToHorizontalBox(PlayerHealthBar))
	{
		BarSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BarSlot->SetVerticalAlignment(VAlign_Center);
	}

	// ── 오른쪽 위: 존 이름 + 잔액 남색 알약 하나 — 목장 HUD 와 같아야 해서 한 곳이 정한다 (M7-03) ──
	UTextBlock* ZoneText = nullptr;
	BalanceText = SnareBalanceHud::AddPlaceholderRow(*WidgetTree, *Root, &ZoneText);
	PillZoneText = ZoneText;
	if (IsValid(PillZoneText))
	{
		PillZoneText->SetText(SnareMinimapHud::FormatZoneName(nullptr));
	}
}

void UHuntHudWidget::UpdateStaminaBolts()
{
	// 켜는 칸 수 = 올림한 기력 칸 (`SegmentedPercent`) — 조금이라도 남으면 한 칸은 켜 둔다.
	const int32 Lit = FMath::RoundToInt(SegmentedPercent(GetPlayerStaminaPercent()) * VitalsGaugeSegmentCount);
	// 시트: 소진분은 회색 — 팔레트 진갈색을 옅게 쓴다(새 색 없음).
	const FLinearColor Spent = SnareUiStyle::UiTextOnButton().CopyWithNewOpacity(PlaceholderSpentBoltOpacity);
	for (int32 Index = 0; Index < StaminaBolts.Num(); ++Index)
	{
		if (UImage* const Bolt = StaminaBolts[Index])
		{
			Bolt->SetColorAndOpacity(Index < Lit ? SnareUiStyle::UiBoltYellow() : Spent);
		}
	}
}

void UHuntHudWidget::BindOrBuildBuffSlots()
{
	if (WidgetTree == nullptr)
	{
		return;
	}
	BuffSlots.Reset();
	BuffSlotTexts.Reset();
	for (const EBuffKind Kind : HudBuffKinds)
	{
		const FString KindName = StaticEnum<EBuffKind>()->GetNameStringByValue(static_cast<int64>(Kind));
		// WBP 가 같은 이름으로 칸을 두었으면 그 칸을 쓴다.
		UBorder* BuffSlot = Cast<UBorder>(WidgetTree->FindWidget(FName(*(TEXT("BuffSlot_") + KindName))));
		if (BuffSlot == nullptr)
		{
			if (EnsureBuffRow() == nullptr)
			{
				return;
			}
			// 아이콘 자리 = 팔레트 아이보리 칸 + 이름 글자 (〔임시 그래픽〕 — 아이콘 원본이 없다).
			BuffSlot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*(TEXT("BuffSlot_") + KindName)));
			BuffSlot->SetBrushColor(SnareUiStyle::UiButtonBase());
			BuffSlot->SetPadding(FMargin(PlaceholderBuffSlotGap, PlaceholderBuffSlotGap * 0.5f));
			if (UHorizontalBoxSlot* const RowSlot = BuffRow->AddChildToHorizontalBox(BuffSlot))
			{
				RowSlot->SetPadding(FMargin(PlaceholderBuffSlotGap, 0.f, 0.f, 0.f));
			}
		}
		UTextBlock* Text = Cast<UTextBlock>(WidgetTree->FindWidget(FName(*(TEXT("BuffSlotText_") + KindName))));
		if (Text == nullptr)
		{
			Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*(TEXT("BuffSlotText_") + KindName)));
			BuffSlot->SetContent(Text);
		}
		BuffSlot->SetVisibility(ESlateVisibility::Collapsed);
		BuffSlots.Add(BuffSlot);
		BuffSlotTexts.Add(Text);
	}

	// 든 장비 줄(M15-11)도 같은 줄에 세운다 — 글자가 긴 줄이라 버프 칸 **왼쪽 끝**에 붙여
	// 칸이 켜졌다 꺼질 때 버프 칸 자리가 흔들리지 않게 한다.
	HeldGearSlot = Cast<UBorder>(WidgetTree->FindWidget(TEXT("HeldGearSlot")));
	if (HeldGearSlot == nullptr && EnsureBuffRow() != nullptr)
	{
		HeldGearSlot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HeldGearSlot"));
		HeldGearSlot->SetBrushColor(SnareUiStyle::UiButtonBase());
		HeldGearSlot->SetPadding(FMargin(PlaceholderBuffSlotGap, PlaceholderBuffSlotGap * 0.5f));
		if (UHorizontalBoxSlot* const RowSlot = BuffRow->AddChildToHorizontalBox(HeldGearSlot))
		{
			RowSlot->SetPadding(FMargin(PlaceholderBuffSlotGap, 0.f, 0.f, 0.f));
		}
		BuffRow->ShiftChild(0, HeldGearSlot);
	}
	if (IsValid(HeldGearSlot))
	{
		HeldGearSlotText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HeldGearSlotText")));
		if (HeldGearSlotText == nullptr)
		{
			HeldGearSlotText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HeldGearSlotText"));
			HeldGearSlot->SetContent(HeldGearSlotText);
		}
		HeldGearSlot->SetVisibility(ESlateVisibility::Collapsed);   // 든 것을 볼 때까지 접혀 있다
	}
}

UHorizontalBox* UHuntHudWidget::EnsureBuffRow()
{
	if (IsValid(BuffRow) || WidgetTree == nullptr)
	{
		return BuffRow;
	}
	UPanelWidget* const Root = Cast<UPanelWidget>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		return nullptr;
	}
	// 버프 칸(M13-16)은 공격 버튼 **왼쪽** 한 줄 — 오른쪽 아래 몸 상태 자리에 모인다 (M15-02).
	// 오른쪽 정렬 가로 줄이라 접힌 칸은 자리를 안 먹고, 켜진 칸만 오른쪽부터 붙는다 (🟡 DESIGN 13.6 배치).
	BuffRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BuffRow"));
	if (UCanvasPanelSlot* const BuffRowSlot = Cast<UCanvasPanelSlot>(Root->AddChild(BuffRow)))
	{
		BuffRowSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
		BuffRowSlot->SetAlignment(FVector2D(1.f, 1.f));
		BuffRowSlot->SetAutoSize(true);
		BuffRowSlot->SetPosition(FVector2D(
			-PlaceholderScreenMargin - PlaceholderRoundButtonSize * 2.f - PlaceholderButtonGap * 2.f,
			-PlaceholderScreenMargin - PlaceholderBoltHeight - PlaceholderButtonGap));
	}
	return BuffRow;
}

void UHuntHudWidget::HandleTargetChanged(AAnimalCharacter* NewTarget)
{
	WatchAnimal(NewTarget);
	RefreshDisplay();
}

void UHuntHudWidget::HandleAnimalHealthChanged(float NewHealth)
{
	RefreshDisplay();
}

void UHuntHudWidget::HandlePlayerHealthChanged(float NewHealth)
{
	RefreshDisplay();
}

void UHuntHudWidget::HandlePlayerStaminaChanged(float NewStamina)
{
	RefreshDisplay();
}

void UHuntHudWidget::HandleAnimalStateChanged(EAnimalState NewState)
{
	if (NewState == EAnimalState::Captured)
	{
		// 이름은 **지금** 꺼내 둔다 — 포획한 동물은 곧 숨겨져 하이라이트에서 빠지고, 그러면 이름이 빈다.
		ShowCaptureBanner(GetAnimalNameText());
		LastRingAnimal.Reset();
		return;
	}

	if (NewState == EAnimalState::Escaping && IsValid(ShownAnimal) && LastRingAnimal.Get() == ShownAnimal)
	{
		// 내가 링을 건 동물이 달아난다 = 내 링이 실패했다 — 실패 판정은 호스트 몫이라(4.1) 복제된 상태로 안다.
		// 줄은 도주 시간만큼 — 줄이 사라지는 순간 다시 누를 수 있다 (명세 3 「실패 배너」).
		LastRingAnimal.Reset();
		const float Seconds = IsValid(WatchedCapture) ? WatchedCapture->GetCaptureEscapeSeconds() : 0.f;
		ShowNoticeBannerFor(GetCaptureMissText(), Seconds > 0.f ? Seconds : CaptureBannerSeconds);
		return;
	}

	RefreshDisplay();
}

void UHuntHudWidget::HandleCaptureRingStarted(AAnimalCharacter* Target, float RingSeconds, float WindowSeconds)
{
	LastRingAnimal = Target;
	UpdateCaptureRing();
}

void UHuntHudWidget::HandleMinimapClicked()
{
	UWorldMapScreenWidget::OpenLocalMap();
}

void UHuntHudWidget::HandleCaptureClicked()
{
	if (!IsValid(WatchedCapture))
	{
		return;
	}

	// 화면 버튼과 IA_Capture 가 같은 함수를 부른다 — 포획 조건을 두 군데 적지 않는다.
	WatchedCapture->TryCaptureTarget();
	RefreshDisplay();
}

// 아래 넷은 전부 같은 함수로 들어간다 — 장착/해제 판단을 부위마다 적지 않는다.
void UHuntHudWidget::HandleEquipWeaponClicked()
{
	ToggleEquipButtonItem(EEquipSlot::Weapon);
}

void UHuntHudWidget::HandleEquipToolClicked()
{
	ToggleEquipButtonItem(EEquipSlot::Tool);
}

void UHuntHudWidget::HandleEquipOutfitTopClicked()
{
	ToggleEquipButtonItem(EEquipSlot::OutfitTop);
}

void UHuntHudWidget::HandleEquipOutfitBottomClicked()
{
	ToggleEquipButtonItem(EEquipSlot::OutfitBottom);
}

void UHuntHudWidget::HandleZoneTravelClicked()
{
	TryZoneTravel();
}

void UHuntHudWidget::HandleZonePortalTargetChanged(AZonePortalActor* NewTarget)
{
	RefreshDisplay();
}

void UHuntHudWidget::HandleInventoryCacheChanged()
{
	RefreshDisplay();
}

void UHuntHudWidget::HandleEquipmentChanged()
{
	CheckUsageHintForHeldItems();
	RefreshDisplay();
}

void UHuntHudWidget::HandleAchievementNotice(const TArray<SnareAchievementNotice::FCard>& Cards)
{
	if (!IsValid(AchievementCards))
	{
		// WBP 디자인엔 카드 두 장 칸이 없다 — 알림 줄 한 칸에 두 줄로 (M48-03 모양).
		ShowNoticeBannerFor(SnareAchievementNotice::JoinCards(Cards), SnareAchievementNotice::CardSeconds);
		return;
	}
	if (SnareSound::IsActionLogOn())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[실측] 화면 알림 「%s」 (카드 %d장)"),
			*SnareAchievementNotice::JoinCards(Cards).ToString().Replace(TEXT("\n"), TEXT(" / ")), Cards.Num());
	}
	AchievementCards->ShowCards(Cards, SnareAchievementNotice::CardSeconds);
}

void UHuntHudWidget::HandleBackendSnapshotChanged()
{
	RefreshDisplay();
}

void UHuntHudWidget::HandleAttackPressed()
{
	if (IsValid(OwnerCharacter))
	{
		// 화면 버튼과 IA_Attack 이 같은 함수를 부른다 — 동물·채집·목장 순서를 두 군데 적지 않는다.
		OwnerCharacter->BeginAttack();
	}
}

void UHuntHudWidget::HandleAttackReleased()
{
	if (IsValid(OwnerCharacter))
	{
		OwnerCharacter->EndAttack();
	}
}

void UHuntHudWidget::WatchAnimal(AAnimalCharacter* NewAnimal)
{
	if (ShownAnimal == NewAnimal)
	{
		return;
	}

	UnwatchAnimal();

	ShownAnimal = NewAnimal;

	if (!IsValid(ShownAnimal))
	{
		return;
	}

	if (UAnimalStateComponent* State = ShownAnimal->GetAnimalState())
	{
		State->OnHealthChanged.AddDynamic(this, &UHuntHudWidget::HandleAnimalHealthChanged);
		State->OnStateChanged.AddDynamic(this, &UHuntHudWidget::HandleAnimalStateChanged);
	}

	UE_LOG(LogSnarehaven, Verbose, TEXT("사냥 HUD 대상: %s"), *ShownAnimal->GetName());
}

void UHuntHudWidget::UnwatchAnimal()
{
	if (IsValid(ShownAnimal))
	{
		if (UAnimalStateComponent* State = ShownAnimal->GetAnimalState())
		{
			State->OnHealthChanged.RemoveDynamic(this, &UHuntHudWidget::HandleAnimalHealthChanged);
			State->OnStateChanged.RemoveDynamic(this, &UHuntHudWidget::HandleAnimalStateChanged);
		}
	}

	ShownAnimal = nullptr;
}

#undef LOCTEXT_NAMESPACE
