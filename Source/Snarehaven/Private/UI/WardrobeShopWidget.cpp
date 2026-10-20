#include "UI/WardrobeShopWidget.h"

#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/ItemRowLookup.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/SnareClickProxy.h"
#include "UI/SnareUiStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SnareSound.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 화면 배치 — 레퍼런스 옷장 판(`docs/reference/character_player.md` 「오른쪽 크림 판·8열 격자」)을 배낭 옆 판 크기로.
	// 게임 규칙 숫자가 아니다(밸런스 표에 안 적는다). 배낭·상인 판과 같은 폰 배율 1.6.
	constexpr float PhoneScale = 1.6f;
	constexpr float Px(float Sheet) { return Sheet * PhoneScale; }
	constexpr int32 TileColumns = 4;
	constexpr float TileSize = Px(72.f);
	constexpr float TileHeight = Px(116.f);
	constexpr float TileIconSize = Px(68.f);
	constexpr float TileGap = Px(8.f);
	constexpr float PanelPadding = Px(16.f);
	constexpr float PanelRadius = Px(16.f);
	constexpr float GapToTabs = Px(12.f);
	constexpr float TitleFontSize = Px(20.f);
	constexpr float BodyFontSize = Px(14.f);
	constexpr float TileFontSize = Px(11.f);
	constexpr float WindowPadding = Px(16.f);

	UTextBlock* MakeLabel(UWidgetTree& Tree, FName Name, const FText& Label, float FontSize)
	{
		UTextBlock* const Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetText(Label);
		Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		Text->SetJustification(ETextJustify::Center);
		Text->SetAutoWrapText(true);
		SnareUiStyle::SetFontSize(*Text, FontSize);
		return Text;
	}

	UButton* MakeTextButton(UWidgetTree& Tree, FName Name, const FText& Label)
	{
		UButton* const Button = Tree.ConstructWidget<UButton>(UButton::StaticClass(), Name);
		Button->AddChild(MakeLabel(Tree, FName(Name.ToString() + TEXT("Text")), Label, BodyFontSize));
		return Button;
	}

	/** 판 안에 가운데로 뜨는 작은 창(구매 확인·구매 완료) — 판 위를 덮어 뒤 칸이 안 눌린다. */
	UBorder* MakeWindow(UWidgetTree& Tree, UOverlay& Stack, FName Name, UVerticalBox*& OutColumn)
	{
		UBorder* const Window = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		Window->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiButtonBase(), PanelRadius));
		Window->SetPadding(FMargin(WindowPadding));
		OutColumn = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(Name.ToString() + TEXT("Column")));
		Window->SetContent(OutColumn);
		if (UOverlaySlot* const WindowSlot = Stack.AddChildToOverlay(Window))
		{
			WindowSlot->SetHorizontalAlignment(HAlign_Center);
			WindowSlot->SetVerticalAlignment(VAlign_Center);
		}
		Window->SetVisibility(ESlateVisibility::Collapsed);
		return Window;
	}

	void AddRow(UVerticalBox& Column, UWidget* Child, float TopGap)
	{
		if (UVerticalBoxSlot* const RowSlot = Column.AddChildToVerticalBox(Child))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Center);
			RowSlot->SetPadding(FMargin(0.f, TopGap, 0.f, 0.f));
		}
	}
}

UWardrobeShopWidget::UWardrobeShopWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(SnareItemRows::ItemTablePath());
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

UWardrobeShopWidget* UWardrobeShopWidget::OpenOnViewport(APlayerController* Owner, UWorld* World, USnareBackendSubsystem* Backend)
{
	UWardrobeShopWidget* const Screen = IsValid(Owner) ? CreateWidget<UWardrobeShopWidget>(Owner, StaticClass())
		: IsValid(World) ? CreateWidget<UWardrobeShopWidget>(World, StaticClass()) : nullptr;
	if (Screen == nullptr)
	{
		return nullptr;
	}
	Screen->BindToBackend(Backend);
	Screen->AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);
	Screen->SetOpen(true);
	return Screen;
}

void UWardrobeShopWidget::BindToBackend(USnareBackendSubsystem* Backend)
{
	if (WatchedBackend == Backend)
	{
		return;
	}
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(this, &UWardrobeShopWidget::HandleBackendSnapshotChanged);
	}
	WatchedBackend = Backend;
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.AddDynamic(this, &UWardrobeShopWidget::HandleBackendSnapshotChanged);
	}
	RefreshDisplay();
}

void UWardrobeShopWidget::HandleBackendSnapshotChanged()
{
	RefreshDisplay();
}

void UWardrobeShopWidget::SetOpen(bool bInOpen)
{
	SetVisibility(bInOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (bInOpen)
	{
		RequestCatalog();
	}
	RefreshDisplay();
}

void UWardrobeShopWidget::Close()
{
	SetOpen(false);
	RemoveFromParent();
}

void UWardrobeShopWidget::RequestCatalog()
{
	if (bCatalogLoading || !IsValid(WatchedBackend))
	{
		return;
	}
	bCatalogLoading = true;
	Message = LOCTEXT("WardrobeLoading", "진열을 불러오는 중…");
	const TWeakObjectPtr<UWardrobeShopWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("GetWardrobeShop"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UWardrobeShopWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bCatalogLoading = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			Self->Message = Failure;
			Self->bCatalogFailed = !Failure.IsEmpty();
			if (Failure.IsEmpty() && !Self->ApplyCatalogReply(Response))
			{
				Self->Message = LOCTEXT("WardrobeBadCatalog", "진열을 읽지 못했다");
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Wardrobe] 진열 응답 — 칸 %d · 알림 「%s」"), Self->Offers.Num(), *Self->Message.ToString());
			Self->RefreshDisplay();
		});
}

bool UWardrobeShopWidget::ApplyCatalogReply(const TSharedPtr<FJsonObject>& Response)
{
	Offers.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("items"), Items))
	{
		RefreshDisplay();
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Items)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		FString ItemID;
		int32 Gems = 0;
		if (Offers.Num() < TileCount && Value.IsValid() && Value->TryGetObject(Entry)
			&& (*Entry)->TryGetStringField(TEXT("itemId"), ItemID) && (*Entry)->TryGetNumberField(TEXT("gems"), Gems))
		{
			FWardrobeOffer& Offer = Offers.AddDefaulted_GetRef();
			Offer.ItemID = FName(*ItemID);
			Offer.Gems = Gems;
		}
	}
	RefreshDisplay();
	return true;
}

bool UWardrobeShopWidget::SelectOffer(int32 Index)
{
	if (bPending || !Offers.IsValidIndex(Index))
	{
		return false;
	}
	SelectedIndex = Index;
	CompletedItemName = FText::GetEmpty();
	RefreshDisplay();
	return true;
}

void UWardrobeShopWidget::CancelBuy()
{
	if (bPending)
	{
		return;
	}
	SelectedIndex = INDEX_NONE;
	RefreshDisplay();
}

void UWardrobeShopWidget::PressConfirm()
{
	ConfirmBuy();
}

void UWardrobeShopWidget::CloseCompleteWindow()
{
	CompletedItemName = FText::GetEmpty();
	RefreshDisplay();
}

bool UWardrobeShopWidget::ConfirmBuy()
{
	if (bPending || !Offers.IsValidIndex(SelectedIndex) || !IsValid(WatchedBackend))
	{
		return false;
	}
	// 기존 상점 사기 길(`BuyItem`)에 젬 갈래만 단다 — 값·진열 확인·차감·지급은 서버가 한다(DESIGN 10.7).
	const FWardrobeOffer Offer = Offers[SelectedIndex];
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("itemId"), Offer.ItemID.ToString());
	Args->SetNumberField(TEXT("count"), 1);
	Args->SetStringField(TEXT("currency"), WatchedBackend->GetGemCurrencyCode());

	bPending = true;
	Message = FText::GetEmpty();
	RefreshDisplay();

	const TWeakObjectPtr<UWardrobeShopWidget> WeakThis(this);
	const FText ItemName = FindItemName(Offer.ItemID);
	WatchedBackend->CallServerFunction(TEXT("BuyItem"), Args,
		[WeakThis, ItemName](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UWardrobeShopWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			Self->SelectedIndex = INDEX_NONE;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Wardrobe] 구매 거절 — %s"), *Failure.ToString());
				Self->Message = Failure;
				Self->RefreshDisplay();
				return;
			}
			USnareSoundSubsystem::Play2D(Self, TEXT("Coins"), 0.8f);   // 서버가 확정한 뒤
			Self->CompletedItemName = ItemName;
			Self->RefreshDisplay();
			// 젬·배낭은 서버가 바꿨다 — 손으로 고치지 않고 다시 받는다 (CLAUDE.md 8).
			if (IsValid(Self->WatchedBackend))
			{
				Self->WatchedBackend->FetchPlayerState();
			}
		});
	return true;
}

FText UWardrobeShopWidget::GetGemText() const
{
	if (!IsValid(WatchedBackend) || !WatchedBackend->GetPlayerSnapshot().HasOwner())
	{
		return LOCTEXT("WardrobeGemUnknown", "젬 -");
	}
	return FText::Format(LOCTEXT("WardrobeGem", "젬 {0}"), FText::AsNumber(WatchedBackend->GetPlayerSnapshot().GemBalance));
}

UTexture2D* UWardrobeShopWidget::GetTileIconTexture(int32 Index) const
{
	if (!TileIcons.IsValidIndex(Index) || !Offers.IsValidIndex(Index) || TileIcons[Index]->GetVisibility() == ESlateVisibility::Hidden)
	{
		return nullptr;
	}
	return Cast<UTexture2D>(TileIcons[Index]->GetBrush().GetResourceObject());
}

FText UWardrobeShopWidget::FindItemName(FName ItemID) const
{
	// 이름을 모르면 ID 그대로 — 그럴싸한 이름을 지어내지 않는다 (Verification.md 7).
	const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, ItemID);
	return Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName : FText::FromName(ItemID);
}

void UWardrobeShopWidget::RefreshDisplay()
{
	if (!IsValid(GemText))
	{
		return;
	}
	GemText->SetText(GetGemText());
	for (int32 Index = 0; Index < Tiles.Num(); ++Index)
	{
		const bool bFilled = Offers.IsValidIndex(Index);
		// 레퍼런스 상점 칸처럼 아이콘이 주인공 — 이름·값은 아래 작게. 아이콘이 없는 행은 칸만 비운다(그림을 지어내지 않는다).
		const FItemRow* const Row = bFilled ? SnareItemRows::FindByItemID(ItemTable, Offers[Index].ItemID) : nullptr;
		UTexture2D* const Icon = Row != nullptr ? Row->IconRef.LoadSynchronous() : nullptr;
		if (Icon != nullptr)
		{
			TileIcons[Index]->SetBrushFromTexture(Icon);
		}
		TileIcons[Index]->SetVisibility(Icon != nullptr ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		TileNames[Index]->SetText(bFilled ? FindItemName(Offers[Index].ItemID) : FText::GetEmpty());
		TilePrices[Index]->SetText(bFilled ? FText::Format(LOCTEXT("WardrobeTilePrice", "젬 {0}"), FText::AsNumber(Offers[Index].Gems)) : FText::GetEmpty());
		Tiles[Index]->SetIsEnabled(bFilled && !bPending);
	}
	MessageText->SetText(Message);
	MessageText->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	RetryBox->SetVisibility(bCatalogFailed && !bCatalogLoading ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	const bool bBuyOpen = Offers.IsValidIndex(SelectedIndex);
	BuyWindow->SetVisibility(bBuyOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bBuyOpen)
	{
		BuyWindowText->SetText(bPending
			? LOCTEXT("WardrobeBuying", "구매 중…")
			: FText::Format(LOCTEXT("WardrobeBuyAsk", "{0}\n젬 {1} 에 살까요?"),
				FindItemName(Offers[SelectedIndex].ItemID), FText::AsNumber(Offers[SelectedIndex].Gems)));
	}
	CompleteWindow->SetVisibility(CompletedItemName.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	CompleteWindowText->SetText(FText::Format(LOCTEXT("WardrobeBought", "구매 완료\n{0}"), CompletedItemName));
}

void UWardrobeShopWidget::BuildTree()
{
	UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("WardrobeRoot"));
	WidgetTree->RootWidget = Root;

	UBorder* const Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("WardrobePanel"));
	Panel->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiWoodPanel(), PanelRadius, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));
	Panel->SetPadding(FMargin(PanelPadding));
	if (UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Panel)))
	{
		// 열린 배낭 판 **왼쪽 세로 탭 열 밖**에 선다 — 판 왼쪽 변에 붙이면 탭 단추를 덮어 끝만 삐져나왔다(M82-03).
		PanelSlot->SetAnchors(FAnchors(1.f, 0.5f, 1.f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(1.f, 0.5f));
		PanelSlot->SetPosition(FVector2D(-(UBackpackScreenWidget::GetTabsLeftInsetFromRight() + GapToTabs), 0.f));
		PanelSlot->SetAutoSize(true);
	}
	UOverlay* const Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("WardrobeStack"));
	Panel->SetContent(Stack);
	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("WardrobeColumn"));
	Stack->AddChildToOverlay(Column);

	UTextBlock* const TitleText = MakeLabel(*WidgetTree, TEXT("WardrobeTitle"), LOCTEXT("WardrobeTitleText", "꾸미기 상점 · 매주 월요일 바뀜"), TitleFontSize);
	TitleText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
	AddRow(*Column, TitleText, 0.f);
	GemText = MakeLabel(*WidgetTree, TEXT("WardrobeGemText"), FText::GetEmpty(), BodyFontSize);
	GemText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
	AddRow(*Column, GemText, TileGap);

	UUniformGridPanel* const Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("WardrobeGrid"));
	Grid->SetSlotPadding(FMargin(TileGap * 0.5f));
	Grid->SetMinDesiredSlotWidth(TileSize);
	Grid->SetMinDesiredSlotHeight(TileHeight);
	Tiles.Reset();
	TileIcons.Reset();
	TileNames.Reset();
	TilePrices.Reset();
	TileProxies.Reset();
	const TWeakObjectPtr<UWardrobeShopWidget> WeakThis(this);
	for (int32 Index = 0; Index < TileCount; ++Index)
	{
		UButton* const Tile = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*FString::Printf(TEXT("WardrobeTile_%d"), Index)));
		SnareUiStyle::MakeRoundButton(*Tile, Px(8.f));
		UVerticalBox* const TileColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),
			FName(*FString::Printf(TEXT("WardrobeTileColumn_%d"), Index)));
		Tile->AddChild(TileColumn);
		UImage* const TileIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(*FString::Printf(TEXT("WardrobeTileIcon_%d"), Index)));
		USizeBox* const IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(*FString::Printf(TEXT("WardrobeTileIconBox_%d"), Index)));
		IconBox->SetWidthOverride(TileIconSize);
		IconBox->SetHeightOverride(TileIconSize);
		IconBox->SetContent(TileIcon);
		if (UVerticalBoxSlot* const IconSlot = TileColumn->AddChildToVerticalBox(IconBox))
		{
			IconSlot->SetHorizontalAlignment(HAlign_Center);
		}
		UTextBlock* const TileName = MakeLabel(*WidgetTree, FName(*FString::Printf(TEXT("WardrobeTileText_%d"), Index)), FText::GetEmpty(), TileFontSize);
		UTextBlock* const TilePrice = MakeLabel(*WidgetTree, FName(*FString::Printf(TEXT("WardrobeTilePrice_%d"), Index)), FText::GetEmpty(), TileFontSize);
		TileColumn->AddChildToVerticalBox(TileName)->SetHorizontalAlignment(HAlign_Center);
		TileColumn->AddChildToVerticalBox(TilePrice)->SetHorizontalAlignment(HAlign_Center);
		USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
		Proxy->Key = FName(*FString::FromInt(Index));
		Proxy->OnClicked = [WeakThis](FName Key)
		{
			if (UWardrobeShopWidget* const Self = WeakThis.Get())
			{
				Self->SelectOffer(FCString::Atoi(*Key.ToString()));
			}
		};
		Tile->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
		// 격자 칸 기본 정렬이 왼쪽 위라 채우지 않으면 빈 타일이 글 크기(33)로 줄어든다(M38-04d) — 배낭 칸과 같은 채우기.
		if (UUniformGridSlot* const TileSlot = Grid->AddChildToUniformGrid(Tile, Index / TileColumns, Index % TileColumns))
		{
			TileSlot->SetHorizontalAlignment(HAlign_Fill);
			TileSlot->SetVerticalAlignment(VAlign_Fill);
		}
		Tiles.Add(Tile);
		TileIcons.Add(TileIcon);
		TileNames.Add(TileName);
		TilePrices.Add(TilePrice);
		TileProxies.Add(Proxy);
	}
	AddRow(*Column, Grid, TileGap);

	MessageText = MakeLabel(*WidgetTree, TEXT("WardrobeMessage"), FText::GetEmpty(), BodyFontSize);
	MessageText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
	AddRow(*Column, MessageText, TileGap);
	UButton* Retry = nullptr;
	RetryBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("WardrobeRetryBox"), TEXT("WardrobeRetryButton"),
		LOCTEXT("WardrobeRetry", "다시 시도"), Retry);
	Retry->OnClicked.AddDynamic(this, &UWardrobeShopWidget::HandleRetryClicked);
	RetryBox->SetVisibility(ESlateVisibility::Collapsed);
	AddRow(*Column, RetryBox, TileGap);
	UButton* const CloseButton = MakeTextButton(*WidgetTree, TEXT("WardrobeCloseButton"), LOCTEXT("WardrobeClose", "닫기"));
	CloseButton->OnClicked.AddDynamic(this, &UWardrobeShopWidget::Close);
	// 닫기는 글 높이만 받으면 46 — 최소 72×72 상자로 감싼다(M38-04d, DESIGN 2 「🟡 누르는 칸 최소 크기」).
	USizeBox* const CloseBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("WardrobeCloseButtonBox"));
	CloseBox->SetMinDesiredWidth(SnareUiStyle::ScreenCloseButtonHeight);
	CloseBox->SetMinDesiredHeight(SnareUiStyle::ScreenCloseButtonHeight);
	CloseBox->SetContent(CloseButton);
	AddRow(*Column, CloseBox, TileGap);

	// 레퍼런스 `UMG_BuyWindow` — 구매 확인.
	UVerticalBox* BuyColumn = nullptr;
	BuyWindow = MakeWindow(*WidgetTree, *Stack, TEXT("WardrobeBuyWindow"), BuyColumn);
	BuyWindowText = MakeLabel(*WidgetTree, TEXT("WardrobeBuyWindowText"), FText::GetEmpty(), BodyFontSize);
	AddRow(*BuyColumn, BuyWindowText, 0.f);
	UHorizontalBox* const BuyButtons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("WardrobeBuyButtons"));
	UButton* const ConfirmButton = MakeTextButton(*WidgetTree, TEXT("WardrobeConfirmButton"), LOCTEXT("WardrobeConfirm", "구매"));
	ConfirmButton->OnClicked.AddDynamic(this, &UWardrobeShopWidget::PressConfirm);
	UButton* const CancelButton = MakeTextButton(*WidgetTree, TEXT("WardrobeCancelButton"), LOCTEXT("WardrobeCancel", "취소"));
	CancelButton->OnClicked.AddDynamic(this, &UWardrobeShopWidget::CancelBuy);
	BuyButtons->AddChildToHorizontalBox(ConfirmButton)->SetPadding(FMargin(0.f, 0.f, TileGap, 0.f));
	BuyButtons->AddChildToHorizontalBox(CancelButton);
	AddRow(*BuyColumn, BuyButtons, TileGap);

	// 레퍼런스 `UMG_BuyCompleteWindow` — 서버가 지급을 확정한 뒤에만 뜬다.
	UVerticalBox* CompleteColumn = nullptr;
	CompleteWindow = MakeWindow(*WidgetTree, *Stack, TEXT("WardrobeBuyCompleteWindow"), CompleteColumn);
	CompleteWindowText = MakeLabel(*WidgetTree, TEXT("WardrobeBuyCompleteText"), FText::GetEmpty(), BodyFontSize);
	AddRow(*CompleteColumn, CompleteWindowText, 0.f);
	UButton* const OkButton = MakeTextButton(*WidgetTree, TEXT("WardrobeCompleteOkButton"), LOCTEXT("WardrobeOk", "확인"));
	OkButton->OnClicked.AddDynamic(this, &UWardrobeShopWidget::CloseCompleteWindow);
	AddRow(*CompleteColumn, OkButton, TileGap);
}

TSharedRef<SWidget> UWardrobeShopWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
	}
	RefreshDisplay();
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	return Built;
}

void UWardrobeShopWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (USnareBackendSubsystem* const Backend = BackendToRebind.Get())
	{
		BindToBackend(Backend);
	}
}

void UWardrobeShopWidget::NativeDestruct()
{
	BackendToRebind = WatchedBackend;
	BindToBackend(nullptr);
	Super::NativeDestruct();
}

void UWardrobeShopWidget::HandleRetryClicked()
{
	bCatalogFailed = false;
	RequestCatalog();
	RefreshDisplay();
}

#undef LOCTEXT_NAMESPACE
