#include "UI/WaterActionWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "Crafting/SnareCraftRules.h"
#include "Gathering/TapFishingComponent.h"
#include "UI/CraftingScreenWidget.h"
#include "Player/MountComponent.h"
#include "Styling/SlateBrush.h"
#include "UI/SnareUiStyle.h"
#include "World/WaterRules.h"
#include "World/WaterTravelComponent.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 배치 (Autonomy.md 🟡 — 게임 규칙 숫자가 아니라 "화면 어디에 얼마나 크게").
	// 오른쪽 여백 = 사냥 HUD 「거점존 이동」 버튼과 같은 선(40 + 공격 칸 130 + 간격 20). 오른쪽 아이콘 기둥(배낭·지도·메뉴·
	// 이모트·출석, 오른쪽 69~141)을 비켜 49 떨어진다 — 32 였을 땐 「배 보관함」 이 이모트 위에 겹쳐 오른쪽 절반이 안 눌렸다
	// (INBOX 결함 10-07 01:18 ①).
	// 10-16 기둥이 2열(121 칸)로 넓어져 오른쪽 여백 = 기둥 왼쪽 끝 + 20 (`SnareUiStyle::PillarClearRight`).
	constexpr float ScreenMargin = SnareUiStyle::PillarClearRight;
	constexpr float ButtonWidth = 220.f;
	constexpr float ButtonHeight = 80.f;
	constexpr float ButtonGap = 12.f;
	constexpr float AlertAboveCenter = 200.f;
	constexpr float MessageAboveCenter = 260.f;
	// ! 말풍선은 찌 위 — 찌 화면 점에서 이만큼 위 (원본: 입질 때 찌 머리 위 느낌표).
	constexpr float AlertAboveBobber = 60.f;
	// 버튼 줄 윗변 = 화면 가운데 위 152 — 아래는 배낭 버튼·공격·포획 줄. 배 위에서 네 칸(낚시·내리기·배 넣기·배 보관함,
	// 368) 다 떠도 예전 세 칸과 같은 가운데 +216 에서 끝난다.
	constexpr int32 MaxShownButtons = 4;
	constexpr float ColumnBottomFromCenter = 216.f;
	constexpr float ColumnTopFromCenter = ColumnBottomFromCenter - (ButtonHeight + ButtonGap) * MaxShownButtons;
	// 떠 있는 동안만 보이는 **작은 둥근 상황 버튼** (M15-02 — 시트: 대상 옆 크림 프롬프트, 모서리 12~16).
	constexpr float ContextButtonRadius = 28.f;

	// 버튼 글 — 버튼 → 높이 상자 → 글 (AddButton).
	UTextBlock* ButtonText(UButton* Button)
	{
		const USizeBox* const Box = Button ? Cast<USizeBox>(Button->GetChildAt(0)) : nullptr;
		return Box ? Cast<UTextBlock>(Box->GetChildAt(0)) : nullptr;
	}

	void Show(UWidget* Widget, bool bVisible)
	{
		if (Widget)
		{
			Widget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}

	// 서버 거절 code → 한 줄. 모르는 code 는 그대로 보인다.
	FText MessageFor(const FString& Code)
	{
		if (Code == TEXT("locked")) { return LOCTEXT("WaterMsgLocked", "먼바다는 낚시 도감 50%가 먼저다"); }
		if (Code == TEXT("no_rod")) { return LOCTEXT("WaterMsgNoRod", "낚싯대가 없다"); }
		if (Code == TEXT("too_fast")) { return LOCTEXT("WaterMsgTooFast", "잠깐 쉬었다 던지자"); }
		if (Code == TEXT("not_enough_items")) { return LOCTEXT("WaterMsgNoItem", "가진 것이 모자라다"); }
		if (Code == TEXT("mount_locked")) { return LOCTEXT("MountMsgLocked", "탈것이 없다 — 잡화점에서 목줄을 바꾸자"); }
		if (Code == TEXT("mount_no_codex")) { return LOCTEXT("MountMsgNoCodex", "도감을 아직 못 받았다 — 잠시 뒤 다시"); }
		if (Code == TEXT("mount_water")) { return LOCTEXT("MountMsgWater", "물 위에선 못 탄다"); }
		if (Code == TEXT("mount_off")) { return LOCTEXT("MountMsgOff", "탈것 표가 비어 있다"); }
		return FText::FromString(Code);
	}
}

void UWaterActionWidget::BindToPawn(UWaterTravelComponent* InWater, UTapFishingComponent* InFishing)
{
	Water = InWater;
	Fishing = InFishing;
	Mount = InWater && InWater->GetOwner() ? InWater->GetOwner()->FindComponentByClass<UMountComponent>() : nullptr;
	RefreshView();
}

TSharedRef<SWidget> UWaterActionWidget::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget == nullptr)
	{
		BuildPlaceholderTree();
	}
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	return Built;
}

UButton* UWaterActionWidget::AddButton(UVerticalBox& Column, const TCHAR* Name, const FText& Label)
{
	UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	if (UVerticalBoxSlot* const ButtonSlot = Column.AddChildToVerticalBox(Button))
	{
		ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, ButtonGap));
	}
	UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Text")));
	Text->SetText(Label);
	Text->SetJustification(ETextJustify::Center);
	// 글 한 줄만 담으면 알약이 48 로 줄어든다 — 안쪽 상자로 손가락 한 마디(72) 이상을 지킨다(STATUS #0633 캡처).
	USizeBox* const MinHeight = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString(Name) + TEXT("Box")));
	MinHeight->SetMinDesiredHeight(SnareUiStyle::HudTouchMin);
	if (USizeBoxSlot* const TextSlot = Cast<USizeBoxSlot>(MinHeight->AddChild(Text)))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
	Button->AddChild(MinHeight);
	SnareUiStyle::MakeRoundButton(*Button, ContextButtonRadius);
	Button->SetVisibility(ESlateVisibility::Collapsed);
	return Button;
}

void UWaterActionWidget::BuildPlaceholderTree()
{
	UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("WaterActionRoot"));
	WidgetTree->RootWidget = Root;

	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("WaterButtons"));
	ButtonColumn = Column;
	if (UCanvasPanelSlot* const ColumnSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Column)))
	{
		ColumnSlot->SetAnchors(FAnchors(1.f, 0.5f, 1.f, 0.5f));
		ColumnSlot->SetAlignment(FVector2D(1.f, 0.f));
		ColumnSlot->SetPosition(FVector2D(-ScreenMargin, ColumnTopFromCenter));
		ColumnSlot->SetSize(FVector2D(ButtonWidth, (ButtonHeight + ButtonGap) * MaxShownButtons));
	}
	CastButton = AddButton(*Column, TEXT("FishCastButton"), LOCTEXT("WaterCast", "낚시"));
	TapButton = AddButton(*Column, TEXT("FishTapButton"), LOCTEXT("WaterTap", "감기"));
	QuitButton = AddButton(*Column, TEXT("FishQuitButton"), LOCTEXT("WaterQuit", "그만두기"));
	LaunchButton = AddButton(*Column, TEXT("BoatLaunchButton"), LOCTEXT("WaterLaunch", "배 띄우기"));
	BoardButton = AddButton(*Column, TEXT("BoatBoardButton"), LOCTEXT("WaterBoard", "타기"));
	LeaveButton = AddButton(*Column, TEXT("BoatLeaveButton"), LOCTEXT("WaterLeave", "내리기"));
	StowButton = AddButton(*Column, TEXT("BoatStowButton"), LOCTEXT("WaterStow", "배 넣기"));
	BoatStorageButton = AddButton(*Column, TEXT("BoatStorageButton"), LOCTEXT("WaterBoatStorage", "배 보관함"));
	MountButton = AddButton(*Column, TEXT("MountToggleButton"), LOCTEXT("MountRide", "탈것 타기"));
	MountPickButton = AddButton(*Column, TEXT("MountPickButton"), LOCTEXT("MountPick", "탈것 바꾸기"));

	BiteAlert = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FishBiteAlert"));
	BiteAlert->SetText(FText::FromString(TEXT("!")));
	BiteAlert->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* const AlertSlot = Cast<UCanvasPanelSlot>(Root->AddChild(BiteAlert)))
	{
		AlertSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		AlertSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		AlertSlot->SetPosition(FVector2D(0.f, -AlertAboveCenter));
		AlertSlot->SetSize(FVector2D(ButtonHeight, ButtonHeight));
	}

	MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WaterMessage"));
	MessageText->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* const MessageSlot = Cast<UCanvasPanelSlot>(Root->AddChild(MessageText)))
	{
		MessageSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		MessageSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		MessageSlot->SetPosition(FVector2D(0.f, -MessageAboveCenter));
		MessageSlot->SetSize(FVector2D(ButtonWidth * 3.f, ButtonHeight * 0.5f));
	}

	CastButton->OnClicked.AddDynamic(this, &UWaterActionWidget::HandleCast);
	TapButton->OnClicked.AddDynamic(this, &UWaterActionWidget::HandleTap);
	QuitButton->OnClicked.AddDynamic(this, &UWaterActionWidget::HandleQuit);
	LaunchButton->OnClicked.AddDynamic(this, &UWaterActionWidget::HandleLaunch);
	BoardButton->OnClicked.AddDynamic(this, &UWaterActionWidget::HandleBoard);
	LeaveButton->OnClicked.AddDynamic(this, &UWaterActionWidget::HandleLeave);
	StowButton->OnClicked.AddDynamic(this, &UWaterActionWidget::HandleStow);
	MountButton->OnClicked.AddDynamic(this, &UWaterActionWidget::HandleMount);
	MountPickButton->OnClicked.AddDynamic(this, &UWaterActionWidget::HandleMountPick);
	BoatStorageButton->OnClicked.AddDynamic(this, &UWaterActionWidget::HandleBoatStorage);
}

void UWaterActionWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshView();
}

void UWaterActionWidget::RefreshView()
{
	const ETapFishingState State = Fishing ? Fishing->GetState() : ETapFishingState::Idle;
	const bool bFishingBusy = State != ETapFishingState::Idle;

	// 제작 판(배 보관함)이 열린 동안 버튼 줄을 접는다 — 줄이 판 위 레이어(`HudViewportZOrder`)라 판 오른쪽 끝을 가렸다
	// (INBOX 결함 10-07 02:00 ②). 판이 닫히면 아래 판정 그대로 다시 뜬다.
	const AActor* const Owner = Water ? Water->GetOwner() : nullptr;
	const USnareBaseBuildComponent* const Build = Owner ? Owner->FindComponentByClass<USnareBaseBuildComponent>() : nullptr;
	const UCraftingScreenWidget* const Panel = Build ? Build->GetCraftingScreen() : nullptr;
	if (ButtonColumn)
	{
		ButtonColumn->SetVisibility(Panel && Panel->IsOpen() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	Show(CastButton, Fishing && Fishing->CanCast());
	Show(TapButton, State == ETapFishingState::Tapping);
	// 입질 기다리는 동안의 「그만두기」 는 상황 버튼(`QuitFishing`)이 맡는다 — 같은 글자 버튼이 둘 떴다(INBOX 10-19 13:30).
	// 연타 중엔 상황 버튼이 「낚시」(감기)라 그만두기는 여기만 남는다.
	Show(QuitButton, State == ETapFishingState::Tapping);
	Show(LaunchButton, !bFishingBusy && Water && Water->CanLaunchBoat());
	Show(BoardButton, !bFishingBusy && Water && Water->CanBoard());
	Show(LeaveButton, !bFishingBusy && Water && Water->CanLeave());
	Show(StowButton, !bFishingBusy && Water && Water->CanStow());
	// 배 보관함은 배 위에서만 — 열쇠(보트 컨테이너)가 가방에 있을 때. 넣기·꺼내기 판정은 서버다.
	const USnareBackendSubsystem* const BagBackend = Water ? SnareWater::FindBackend(Water) : nullptr;
	Show(BoatStorageButton, !bFishingBusy && Water && Water->GetRiddenBoat() && BagBackend
		&& SnareCraftRules::CountItem(BagBackend->GetPlayerSnapshot().InventorySlots, USnareBaseSubsystem::GetBoatContainerItemID()) > 0);
	// 탈 수 있을 때(또는 타고 있을 때)만 뜨는 상황 버튼 — 못 타는 동안 늘 떠 있으면 화면 가운데를 막는다
	// (M15-02, 레퍼런스는 대상 곁에서만 프롬프트가 뜬다). 까닭 판정은 누를 때와 **같은 함수**다.
	const bool bOnLand = Water && !Water->IsInWater() && !Water->GetRiddenBoat();
	const USnareBackendSubsystem* const Backend = Mount ? SnareWater::FindBackend(Mount) : nullptr;
	const bool bCanRide = bOnLand && Backend && Mount->GetMountBlockCode(Backend->GetPlayerSnapshot()).IsEmpty();
	Show(MountButton, !bFishingBusy && Mount && (Mount->IsMounted() || bCanRide));
	if (UTextBlock* const MountText = ButtonText(MountButton))
	{
		MountText->SetText(Mount && Mount->IsMounted() ? LOCTEXT("MountLeave", "탈것 내리기") : LOCTEXT("MountRide", "탈것 타기"));
	}
	const bool bCanPick = !bFishingBusy && bOnLand && Mount && !Mount->IsMounted() && Backend
		&& Backend->GetPlayerSnapshot().Mounts.Num() > 1;
	Show(MountPickButton, bCanPick);
	// 화면에 붙기 전(BindToPawn)엔 버튼이 아직 없다 — 탈것 2종 이상인 계정이 켜자마자 꺼졌다(09-25 기기).
	if (UTextBlock* const PickText = bCanPick ? ButtonText(MountPickButton) : nullptr)
	{
		PickText->SetText(FText::Format(LOCTEXT("MountPickNamed", "탈것 바꾸기 ({0})"),
			Mount->GetSpeciesName(Mount->GetRideSpecies(Backend->GetPlayerSnapshot()))));
	}
	Show(BiteAlert, Fishing && Fishing->ShowsBiteAlert());
	// 찌가 보이면 ! 를 찌 머리 위로 옮긴다. 화면 밖이면 원래 자리(가운데 위).
	if (UCanvasPanelSlot* const AlertSlot = BiteAlert ? Cast<UCanvasPanelSlot>(BiteAlert->Slot) : nullptr)
	{
		const UStaticMeshComponent* const Bobber = Fishing ? Fishing->GetBobberMesh() : nullptr;
		FVector2D OnScreen;
		const bool bOverBobber = Bobber && Bobber->IsVisible() && GetOwningPlayer()
			&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(), Bobber->GetComponentLocation(), OnScreen, false);
		const FVector2D Half = UWidgetLayoutLibrary::GetViewportSize(this) * 0.5f / FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), 0.01f);
		AlertSlot->SetPosition(bOverBobber ? OnScreen - Half - FVector2D(0.f, AlertAboveBobber) : FVector2D(0.f, -AlertAboveCenter));
	}
	// 연타 진행 막대는 여기 없다 — 시트(`hud_ingame.md`)의 진행바 자리는 **플레이어 발밑**이고 화면
	// 가운데는 비운다(M15-19). 그리는 곳은 캐릭터에 붙은 `UFootProgressWidget` 이다.

	if (MessageText)
	{
		const FString Fish = Fishing ? Fishing->GetLastMessage() : FString();
		const FString Boat = Water ? Water->GetLastMessage() : FString();
		const FString Ride = Mount ? Mount->GetLastMessage() : FString();
		const FString& Code = !Fish.IsEmpty() ? Fish : !Boat.IsEmpty() ? Boat : Ride;
		MessageText->SetText(Code.IsEmpty() ? FText::GetEmpty() : MessageFor(Code));
	}
}

void UWaterActionWidget::HandleCast() { if (Fishing) { Fishing->CastAtNearest(); } }
void UWaterActionWidget::HandleTap() { if (Fishing) { Fishing->Tap(); } }
void UWaterActionWidget::HandleQuit() { if (Fishing) { Fishing->Quit(); } }
void UWaterActionWidget::HandleLaunch() { if (Water) { Water->LaunchBoat(); } }
void UWaterActionWidget::HandleBoard() { if (Water) { Water->Board(); } }
void UWaterActionWidget::HandleLeave() { if (Water) { Water->Leave(); } }
void UWaterActionWidget::HandleStow() { if (Water) { Water->Stow(); } }
void UWaterActionWidget::HandleMount() { if (Mount) { Mount->ToggleMount(); } }

void UWaterActionWidget::HandleMountPick()
{
	const USnareBackendSubsystem* const Backend = Mount ? SnareWater::FindBackend(Mount) : nullptr;
	if (Backend)
	{
		Mount->CycleMountSpecies(Backend->GetPlayerSnapshot());
	}
}

void UWaterActionWidget::HandleBoatStorage()
{
	AActor* const Owner = Water ? Water->GetOwner() : nullptr;
	if (USnareBaseBuildComponent* const Build = Owner ? Owner->FindComponentByClass<USnareBaseBuildComponent>() : nullptr)
	{
		Build->OpenBoatStorage();
	}
}

#undef LOCTEXT_NAMESPACE
