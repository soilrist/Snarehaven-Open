#include "UI/WardrobeWidget.h"

#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
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
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Data/ItemRowLookup.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Equipment/EquipmentComponent.h"
#include "GameFramework/Character.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/InventoryComponent.h"
#include "SnarehavenCharacter.h"
#include "UI/SnareClickProxy.h"
#include "UI/SnareUiStyle.h"
#include "UI/WardrobeShopWidget.h"
#include "UI/WorldMapScreenWidget.h"
#include "UI/BackpackScreenWidget.h"
#include "UObject/UObjectIterator.h"
#include "UI/SnareStoreSettings.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 화면 배치 — 우리 것(M97-c-3, 레퍼런스 옷장 `steam_06.jpg` 의 캐릭터 둘레 고리·오른쪽 판과 다르게):
	// 왼쪽 세로 나무판(꾸미기 목록) · 캐릭터는 남은 폭 가운데 · 부위는 화면 아래 가로 나무 표찰 줄 · 「나가기」 왼쪽 위.
	// 게임 규칙 숫자가 아니다(밸런스 표에 안 적는다). 배낭·옷장 상점 판과 같은 폰 배율 1.6.
	constexpr float PhoneScale = 1.6f;
	constexpr float Px(float Sheet) { return Sheet * PhoneScale; }
	/**
	 * 원판 한 변 — 폰 손가락 칸. 1334×616 화면(짧은 변 616 → 엔진 기본 DPI 0.57)에서 120px 이상이 되게 212(≈121px).
	 * 아이콘이 주인공이라 원판 대부분을 그림이 차지하고 이름은 그 아래 한 줄로 작게(레퍼런스 게임 옷장 격자처럼, 배치는 우리 것).
	 */
	constexpr float TileSize = 212.f;
	constexpr float TileIconSize = 144.f;
	constexpr float TileNameHeight = 44.f;
	constexpr float TileGap = Px(6.f);
	constexpr float PanelPadding = Px(16.f);
	constexpr float PanelInset = Px(24.f);
	/**
	 * 왼쪽 판 폭 — 손가락 칸 3열 + 밀대 + 판 안 여백. 화면 비율 몫이 아니라 고정 폭이라 16:9·4:3 에서도 칸이 안 넘친다.
	 * 캐릭터와 부위 표찰 줄은 판 오른쪽 남은 폭의 가운데(`BodyScreenX`).
	 */
	constexpr float PanelWidth = 3.f * (TileSize + TileGap) + Px(8.f) + 2.f * PanelPadding;
	/** 뷰포트 크기를 못 읽을 때(시험 월드) 캐릭터 가로 자리. */
	constexpr float FallbackBodyCenterX = 0.62f;
	/** 카메라 거리 계산용 몫 — 20° 앞에서 볼 때 몸 전체(모자~신발)가 화면 높이의 약 60%(원본 steam_06). 캡처로 맞춘 값. */
	constexpr float WardrobeBodyScreenHeight = 0.28f;
	/** 「나가기」 가 왼쪽 위 구석에 앉고, 판은 그 아래부터. */
	constexpr float PanelTopInset = Px(24.f) + SnareUiStyle::ScreenCloseButtonHeight + Px(8.f);
	/** 부위 표찰 줄 아래 여백 · 판 아래는 줄 높이 + 틈만큼 비운다. */
	constexpr float SlotRowBottomInset = Px(16.f);
	constexpr float SlotRowGap = Px(8.f);
	/** 격자 아래 빈 띠 — 걸린 줄이 판 아래 테두리에 닿지 않게. */
	constexpr float GridBottomGap = Px(10.f);
	/**
	 * 부위 표찰 — 10-16 높이 80 → 121, 애플 44pt (DESIGN 2). 폭 154 = 손가락 88px＠1334(DPI 0.57).
	 * 한 줄 8칸(≈1334)은 1280×720 판 오른쪽 남은 폭(≈1150)을 넘어서 4칸씩 두 줄(≈667).
	 */
	constexpr float SlotTabWidth = 154.f;
	constexpr int32 SlotTabsPerRow = 4;
	constexpr float SlotTabHeight = SnareUiStyle::MenuTouchMin;
	/** 원판 등급 테두리 굵기 — 입은 칸만 두껍게. */
	constexpr float TileOutlineWidth = 3.f;
	constexpr float WornTileOutlineWidth = Px(5.f);
	constexpr float BodyFontSize = Px(14.f);
	constexpr float SlotFontSize = Px(15.f);
	constexpr float TileFontSize = Px(12.f);
	/** 안 가진 꾸미기(전체 탭)는 이름만 흐리게 — 아이콘은 원색 그대로(흐린 아이콘이 칸 색과 섞여 페즈 빨강이 초록으로 보였다). */
	constexpr float UnownedNameOpacity = 0.55f;
	constexpr float RotateYawDegrees = 180.f;
	/** 판 글 버튼(탭·상점 열기·나가기) 짧은 변 바닥 (M38-04c) — DESIGN 2 누르는 칸 최소 크기 — 10-16 72 → 121(공용 닫기 높이). 글 높이만 받으면 46 이었다. */
	constexpr float PanelButtonMinSize = SnareUiStyle::ScreenCloseButtonHeight;

	FText SlotLabel(EEquipSlot InSlot)
	{
		switch (InSlot)
		{
		case EEquipSlot::Head:          return LOCTEXT("WardrobeSlotHead", "모자");
		case EEquipSlot::FaceAccessory: return LOCTEXT("WardrobeSlotFace", "얼굴");
		case EEquipSlot::OutfitTop:     return LOCTEXT("WardrobeSlotTop", "상의");
		case EEquipSlot::OutfitBottom:  return LOCTEXT("WardrobeSlotBottom", "하의");
		case EEquipSlot::Shoes:         return LOCTEXT("WardrobeSlotShoes", "신발");
		case EEquipSlot::Gloves:        return LOCTEXT("WardrobeSlotGloves", "장갑");
		case EEquipSlot::Backpack:      return LOCTEXT("WardrobeSlotBack", "배낭");
		default:                        return FText::GetEmpty();
		}
	}

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

	UButton* MakeTextButton(UWidgetTree& Tree, FName Name, const FText& Label, float FontSize)
	{
		UButton* const Button = Tree.ConstructWidget<UButton>(UButton::StaticClass(), Name);
		Button->AddChild(MakeLabel(Tree, FName(Name.ToString() + TEXT("Text")), Label, FontSize));
		return Button;
	}

	/** 원판 = 크림 바탕(틴트) + 등급 색 테두리. 아이콘은 틴트를 안 받으니 원색 그대로. 입은 칸은 테두리를 두껍게. */
	void PaintTile(UButton* Button, const FLinearColor& GradeColor, bool bWorn)
	{
		if (!IsValid(Button))
		{
			return;
		}
		const FSlateRoundedBoxBrush Round(FLinearColor::White, Px(10.f), GradeColor, bWorn ? WornTileOutlineWidth : TileOutlineWidth);
		FButtonStyle Style = Button->GetStyle();
		Style.Normal = Round;
		Style.Hovered = Round;
		Style.Pressed = Round;
		Style.Disabled = Round;
		Style.Normal.TintColor = FSlateColor(SnareUiStyle::UiPanelCream());
		Style.Hovered.TintColor = FSlateColor(SnareUiStyle::UiButtonHighlight());
		Style.Pressed.TintColor = FSlateColor(SnareUiStyle::UiButtonHighlight());
		Style.Disabled.TintColor = FSlateColor(SnareUiStyle::UiPanelCream());
		Button->SetStyle(Style);
	}

	/** 글 버튼을 최소 크기(기본 72×72) 상자에 감싼다 — 가로 상자 칸이 버튼을 상자 높이까지 채운다. */
	USizeBox* WrapTouchSize(UWidgetTree& Tree, UButton* Button, float MinWidth = PanelButtonMinSize, float MinHeight = PanelButtonMinSize)
	{
		USizeBox* const Box = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(Button->GetName() + TEXT("Box")));
		Box->SetMinDesiredWidth(MinWidth);
		Box->SetMinDesiredHeight(MinHeight);
		Box->SetContent(Button);
		return Box;
	}

	/** 판 오른쪽 남은 폭의 가운데 — 화면 폭 몫(0~1). 판 오른쪽 끝 = (안쪽 띄움 + 판 폭) × DPI. */
	float BodyScreenX(UWorld* World)
	{
		FVector2D ViewSize = FVector2D::ZeroVector;
		if (GEngine != nullptr && GEngine->GameViewport != nullptr)
		{
			GEngine->GameViewport->GetViewportSize(ViewSize);
		}
		const float Scale = IsValid(World) ? UWidgetLayoutLibrary::GetViewportScale(World) : 0.f;
		if (ViewSize.X < 1.f || Scale <= 0.f)
		{
			return FallbackBodyCenterX;
		}
		const float PanelRight = FMath::Clamp((PanelInset + PanelWidth) * Scale / ViewSize.X, 0.f, 0.9f);
		return (PanelRight + 1.f) * 0.5f;
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

UWardrobeWidget::UWardrobeWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(SnareItemRows::ItemTablePath());
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

UWardrobeWidget* UWardrobeWidget::OpenOnViewport(APlayerController* Owner, UWorld* World, USnareBackendSubsystem* InBackend,
	UBackpackComponent* InBackpack, UEquipmentComponent* InEquipment)
{
	UWardrobeWidget* const Screen = IsValid(Owner) ? CreateWidget<UWardrobeWidget>(Owner, StaticClass())
		: IsValid(World) ? CreateWidget<UWardrobeWidget>(World, StaticClass()) : nullptr;
	if (Screen == nullptr)
	{
		return nullptr;
	}
	Screen->BindTargets(InBackend, InBackpack, InEquipment);
	// 가진 꾸미기가 하나도 없으면 빈 판 대신 `전체` 격자(안 가진 것은 흐린 아이콘)로 연다 — 열자마자 그림이 보이게(INBOX 10-15 「옷장 글자 목록뿐」).
	if (Screen->HasNoOwnedCosmetic())
	{
		Screen->SelectOwnedTab(false);
	}
	Screen->AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);
	Screen->HideSideButtons();
	// 캐릭터를 남은 폭 가운데에 크게 — 닫히면 `NativeDestruct` 가 되돌린다.
	if (ASnarehavenCharacter* const Body = IsValid(Screen->Equipment) ? Cast<ASnarehavenCharacter>(Screen->Equipment->GetOwner()) : nullptr)
	{
		Body->SetWardrobeCamera(true, BodyScreenX(World), WardrobeBodyScreenHeight);
		Screen->bCameraMoved = true;
		// 옷장은 앞모습을 보여 준다(「회전」 으로 다시 돌릴 수 있다) — 등이 카메라 쪽일 때만 돈다.
		// 늘 돌리면 카메라를 보고 선 채 연 몸(오프라인 사냥터 시작 자리)은 거꾸로 등을 보였다.
		const APlayerCameraManager* const Camera = Owner != nullptr ? Owner->PlayerCameraManager.Get() : nullptr;
		const FVector CameraForward = Camera != nullptr ? Camera->GetCameraRotation().Vector() : FVector::ForwardVector;
		if (FVector::DotProduct(Body->GetActorForwardVector().GetSafeNormal2D(), CameraForward.GetSafeNormal2D()) > 0.f)
		{
			Screen->SetRotated(true);
		}
	}
	return Screen;
}

void UWardrobeWidget::BindTargets(USnareBackendSubsystem* InBackend, UBackpackComponent* InBackpack, UEquipmentComponent* InEquipment)
{
	if (IsValid(Backend))
	{
		Backend->OnPlayerSnapshotChanged.RemoveDynamic(this, &UWardrobeWidget::HandleBackendSnapshotChanged);
	}
	if (IsValid(Backpack))
	{
		Backpack->OnReply.RemoveDynamic(this, &UWardrobeWidget::HandleBackpackReply);
	}
	Backend = InBackend;
	Backpack = InBackpack;
	Equipment = InEquipment;
	if (IsValid(Backend))
	{
		Backend->OnPlayerSnapshotChanged.AddDynamic(this, &UWardrobeWidget::HandleBackendSnapshotChanged);
	}
	if (IsValid(Backpack))
	{
		Backpack->OnReply.AddDynamic(this, &UWardrobeWidget::HandleBackpackReply);
	}
	RefreshDisplay();
}

const TArray<EEquipSlot>& UWardrobeWidget::GetSlotOrder()
{
	static const TArray<EEquipSlot> Order = {
		EEquipSlot::Head, EEquipSlot::FaceAccessory, EEquipSlot::OutfitTop, EEquipSlot::OutfitBottom,
		EEquipSlot::Shoes, EEquipSlot::Gloves, EEquipSlot::Backpack };
	return Order;
}

TArray<FWardrobeTile> UWardrobeWidget::CollectTiles(const UDataTable* InItemTable, EEquipSlot InSlot, bool bInOwnedOnly,
	const TArray<FInventorySlot>& Owned)
{
	TArray<FWardrobeTile> Result;
	if (!IsValid(InItemTable))
	{
		return Result;
	}
	for (const TPair<FName, uint8*>& Pair : InItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row == nullptr || Row->ItemType != EItemType::Cosmetic || Row->EquipSlot != InSlot || Row->ItemID.IsNone())
		{
			continue;
		}
		const bool bHave = Owned.ContainsByPredicate(
			[Row](const FInventorySlot& Entry) { return Entry.ItemID == Row->ItemID && Entry.Count > 0; });
		if (bInOwnedOnly && !bHave)
		{
			continue;
		}
		FWardrobeTile& Tile = Result.AddDefaulted_GetRef();
		Tile.RowName = Pair.Key;
		Tile.ItemID = Row->ItemID;
		Tile.Grade = Row->Grade;
		Tile.DisplayName = Row->DisplayName.IsEmpty() ? FText::FromName(Row->ItemID) : Row->DisplayName;
		Tile.bOwned = bHave;
		Tile.Icon = Row->IconRef;
	}
	// 원본 격자처럼 등급끼리 모아 보인다 — 같은 등급은 ItemID 순(표 순서가 바뀌어도 자리가 안 흔들리게).
	Result.Sort([](const FWardrobeTile& A, const FWardrobeTile& B)
	{
		return A.Grade != B.Grade ? A.Grade < B.Grade : A.ItemID.LexicalLess(B.ItemID);
	});
	return Result;
}

void UWardrobeWidget::SelectSlot(EEquipSlot InSlot)
{
	if (!GetSlotOrder().Contains(InSlot))
	{
		return;
	}
	SelectedSlot = InSlot;
	Message = FText::GetEmpty();
	RefreshDisplay();
}

void UWardrobeWidget::SelectOwnedTab(bool bInOwnedOnly)
{
	bOwnedOnly = bInOwnedOnly;
	Message = FText::GetEmpty();
	RefreshDisplay();
}

void UWardrobeWidget::PressOwnedTab()
{
	SelectOwnedTab(true);
}

void UWardrobeWidget::PressAllTab()
{
	SelectOwnedTab(false);
}

FName UWardrobeWidget::GetWornItemID(EEquipSlot InSlot) const
{
	if (!IsValid(Equipment) || !IsValid(ItemTable))
	{
		return NAME_None;
	}
	const FName RowName = Equipment->GetCosmeticItem(InSlot);
	const FItemRow* const Row = RowName.IsNone() ? nullptr : ItemTable->FindRow<FItemRow>(RowName, TEXT("Wardrobe"), false);
	return Row != nullptr ? Row->ItemID : NAME_None;
}

bool UWardrobeWidget::PressTile(int32 Index)
{
	if (bPending || !ShownTiles.IsValidIndex(Index))
	{
		return false;
	}
	const FWardrobeTile& Tile = ShownTiles[Index];
	if (!Tile.bOwned)
	{
		// 안 가진 꾸미기는 입어 보이지 않는다 — 젬 물건이라 서버에 묻지도 않는다 (CLAUDE.md 8).
		Message = USnareStoreSettings::AreGemsEnabled() ? LOCTEXT("WardrobeNotOwned", "아직 없다 — 상점에서 살 수 있다")
			: LOCTEXT("WardrobeNotOwnedNoGems", "아직 없다 — 상자·출석에서 얻는다");
		RefreshDisplay();
		return false;
	}
	// 입은 것을 다시 누르면 벗기 (서버 규약: 빈 itemId).
	const FName Wanted = GetWornItemID(SelectedSlot) == Tile.ItemID ? NAME_None : Tile.ItemID;
	if (!IsValid(Backpack) || !Backpack->RequestSetOutfit(SelectedSlot, Wanted))
	{
		Message = LOCTEXT("WardrobeNoServer", "서버에 닿지 못했다");
		RefreshDisplay();
		return false;
	}
	bPending = true;
	RetryRequest = nullptr;
	const TWeakObjectPtr<UWardrobeWidget> WeakThis(this);
	const EEquipSlot SentSlot = SelectedSlot;
	SentRequest = [WeakThis, SentSlot, Wanted]()
	{
		UWardrobeWidget* const Self = WeakThis.Get();
		if (Self != nullptr && !Self->bPending && IsValid(Self->Backpack) && Self->Backpack->RequestSetOutfit(SentSlot, Wanted))
		{
			Self->bPending = true;
			Self->Message = LOCTEXT("WardrobeSending", "갈아입는 중…");
		}
	};
	Message = LOCTEXT("WardrobeSending", "갈아입는 중…");
	UE_LOG(LogSnarehaven, Log, TEXT("[Wardrobe] 칸 %s · %s 요청"), *UBackpackComponent::GetOutfitSlotKey(SelectedSlot),
		Wanted.IsNone() ? TEXT("벗기") : *Wanted.ToString());
	RefreshDisplay();
	return true;
}

void UWardrobeWidget::HandleBackpackReply(bool bAccepted, const FString& Code, const FString& InMessage)
{
	if (!bPending)
	{
		return;
	}
	bPending = false;
	Message = bAccepted ? FText::GetEmpty()
		: InMessage.IsEmpty() ? LOCTEXT("WardrobeRejected", "갈아입지 못했다") : FText::FromString(InMessage);
	// 서버에 닿지 못했을 때만 같은 요청을 다시 — 「안 가진 옷」 같은 거절은 다시 보내도 같다.
	RetryRequest = !bAccepted && Code == TEXT("network") ? MoveTemp(SentRequest) : nullptr;
	SentRequest = nullptr;
	RefreshDisplay();
}

void UWardrobeWidget::HandleRetryClicked()
{
	TFunction<void()> Retry = MoveTemp(RetryRequest);
	RetryRequest = nullptr;
	Message = FText::GetEmpty();
	SentRequest = Retry;
	if (Retry)
	{
		Retry();
	}
	RefreshDisplay();
}

void UWardrobeWidget::HandleBackendSnapshotChanged()
{
	RefreshDisplay();
}

void UWardrobeWidget::PressRotate()
{
	SetRotated(!bRotated);
}

void UWardrobeWidget::SetRotated(bool bInRotated)
{
	const ACharacter* const Body = IsValid(Equipment) ? Cast<ACharacter>(Equipment->GetOwner()) : nullptr;
	USkeletalMeshComponent* const Mesh = Body != nullptr ? Body->GetMesh() : nullptr;
	if (Mesh == nullptr || bInRotated == bRotated)
	{
		return;
	}
	// 몸 메시만 돌린다 — 액터(이동·복제)는 그대로라 남의 화면·게임 판정에 안 닿는다.
	if (bInRotated)
	{
		MeshRotationBefore = Mesh->GetRelativeRotation();
		Mesh->SetRelativeRotation(MeshRotationBefore + FRotator(0.f, RotateYawDegrees, 0.f));
	}
	else
	{
		Mesh->SetRelativeRotation(MeshRotationBefore);
	}
	bRotated = bInRotated;
}

void UWardrobeWidget::PressOpenShop()
{
	if (!USnareStoreSettings::AreGemsEnabled())
	{
		return;
	}
	if (IsValid(WardrobeShop) && WardrobeShop->IsInViewport())
	{
		WardrobeShop->Close();
		return;
	}
	WardrobeShop = UWardrobeShopWidget::OpenOnViewport(GetOwningPlayer(), GetWorld(), Backend);
	UE_LOG(LogSnarehaven, Log, TEXT("[Wardrobe] 「상점 열기」 — 옷장 상점을 %s"), WardrobeShop != nullptr ? TEXT("열었다") : TEXT("못 열었다"));
}

void UWardrobeWidget::Close()
{
	if (IsValid(WardrobeShop) && WardrobeShop->IsInViewport())
	{
		WardrobeShop->Close();
	}
	SetRotated(false);
	RemoveFromParent();
	NotifyGone();
}

void UWardrobeWidget::NativeDestruct()
{
	SetRotated(false);
	NotifyGone();
	Super::NativeDestruct();
}

void UWardrobeWidget::NotifyGone()
{
	if (bGoneNotified)
	{
		return;
	}
	bGoneNotified = true;
	RestoreSideButtons();
	if (bCameraMoved)
	{
		bCameraMoved = false;
		if (ASnarehavenCharacter* const Body = IsValid(Equipment) ? Cast<ASnarehavenCharacter>(Equipment->GetOwner()) : nullptr)
		{
			Body->SetWardrobeCamera(false, 0.5f, 0.f);
		}
	}
	OnClosed.Broadcast();
}

bool UWardrobeWidget::HasNoOwnedCosmetic() const
{
	const TArray<FInventorySlot> NoItems;
	const TArray<FInventorySlot>& Owned = IsValid(Backend) ? Backend->GetPlayerSnapshot().InventorySlots : NoItems;
	for (const EEquipSlot InSlot : GetSlotOrder())
	{
		if (!CollectTiles(ItemTable, InSlot, true, Owned).IsEmpty())
		{
			return false;
		}
	}
	return true;
}

FString UWardrobeWidget::GetMessageShown() const
{
	return IsValid(MessageText) ? MessageText->GetText().ToString() : FString();
}

FLinearColor UWardrobeWidget::GetTileColor(int32 Index) const
{
	return Tiles.IsValidIndex(Index) && IsValid(Tiles[Index])
		? Tiles[Index]->GetStyle().Normal.OutlineSettings.Color.GetSpecifiedColor() : FLinearColor::Transparent;
}

UTexture2D* UWardrobeWidget::GetTileIconTexture(int32 Index) const
{
	if (!TileIcons.IsValidIndex(Index) || !IsValid(TileIcons[Index]) || TileIcons[Index]->GetVisibility() == ESlateVisibility::Hidden)
	{
		return nullptr;
	}
	return Cast<UTexture2D>(TileIcons[Index]->GetBrush().GetResourceObject());
}

UButton* UWardrobeWidget::GetTileButton(int32 Index) const
{
	return Tiles.IsValidIndex(Index) ? Tiles[Index].Get() : nullptr;
}

void UWardrobeWidget::RefreshDisplay()
{
	const TArray<FInventorySlot> NoItems;
	const TArray<FInventorySlot>& Owned = IsValid(Backend) ? Backend->GetPlayerSnapshot().InventorySlots : NoItems;
	ShownTiles = CollectTiles(ItemTable, SelectedSlot, bOwnedOnly, Owned);
	if (!IsValid(Grid))
	{
		return;
	}
	RebuildGrid();

	const TArray<EEquipSlot>& Order = GetSlotOrder();
	for (int32 Index = 0; Index < SlotButtons.Num(); ++Index)
	{
		SnareUiStyle::MarkSelectedTab(SlotButtons[Index], Order.IsValidIndex(Index) && Order[Index] == SelectedSlot);
	}
	SnareUiStyle::MarkSelectedTab(OwnedTabButton, bOwnedOnly);
	SnareUiStyle::MarkSelectedTab(AllTabButton, !bOwnedOnly);

	FText Shown = Message;
	if (Shown.IsEmpty() && ShownTiles.IsEmpty())
	{
		// 젬이 꺼진 1.0 에는 상점 입구가 접혀 있다 — 닿을 수 없는 길 대신 실제로 얻는 길을 쓴다.
		Shown = !bOwnedOnly ? LOCTEXT("WardrobeNoneAll", "이 칸 꾸미기는 아직 없다")
			: USnareStoreSettings::AreGemsEnabled() ? LOCTEXT("WardrobeNoneOwned", "이 칸 꾸미기가 없다 — 상점 열기")
			: LOCTEXT("WardrobeNoneOwnedNoGems", "이 칸 꾸미기가 없다 — 상자·출석에서 얻는다");
	}
	MessageText->SetText(Shown);
	MessageText->SetVisibility(Shown.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (IsValid(RetryBox))
	{
		RetryBox->SetVisibility(RetryRequest && !bPending ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	// 내 꾸미기가 빈 칸이면 안내 글을 판 한가운데로 옮기고 「전체 보기」 길을 붙인다(아래 알림 줄은 접음).
	const bool bEmptyOwned = Message.IsEmpty() && ShownTiles.IsEmpty() && bOwnedOnly;
	if (IsValid(EmptyPanel))
	{
		EmptyHint->SetText(Shown);
		EmptyPanel->SetVisibility(bEmptyOwned ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (bEmptyOwned)
		{
			MessageText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UWardrobeWidget::RebuildGrid()
{
	Grid->ClearChildren();
	Tiles.Reset();
	TileIcons.Reset();
	TileProxies.Reset();
	const FName Worn = GetWornItemID(SelectedSlot);
	const TWeakObjectPtr<UWardrobeWidget> WeakThis(this);
	for (int32 Index = 0; Index < ShownTiles.Num(); ++Index)
	{
		const FWardrobeTile& Tile = ShownTiles[Index];
		// 이름은 엔진이 짓게 둔다 — 다시 그릴 때마다 같은 이름으로 지으면 아직 안 거둔 옛 원판 객체와 부딪힌다.
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		// 아이콘이 주인공 — 원판 대부분을 그림이, 이름은 아래 한 줄 작게(상점·가방 칸과 같은 아이콘 표 `IconRef`).
		// 2층 `EveryCosmeticHasIcon` 이 꾸미기 행 아이콘 없음 0개를 단정하므로 그림 없는 칸 대비 글자 칸은 두지 않는다.
		UImage* const TileIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		UTexture2D* const IconTexture = Tile.Icon.LoadSynchronous();
		if (IconTexture != nullptr)
		{
			TileIcon->SetBrushFromTexture(IconTexture);
		}
		TileIcon->SetVisibility(IconTexture != nullptr ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		USizeBox* const IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		IconBox->SetWidthOverride(TileIconSize);
		IconBox->SetHeightOverride(TileIconSize);
		IconBox->SetContent(TileIcon);
		UTextBlock* const NameText = MakeLabel(*WidgetTree, NAME_None, Tile.DisplayName, TileFontSize);
		NameText->SetAutoWrapText(false);
		NameText->SetRenderOpacity(Tile.bOwned ? 1.f : UnownedNameOpacity);
		NameText->SetClipping(EWidgetClipping::ClipToBounds);
		USizeBox* const NameBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		NameBox->SetWidthOverride(TileSize - Px(12.f));
		NameBox->SetHeightOverride(TileNameHeight);
		NameBox->SetContent(NameText);
		UVerticalBox* const TileColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		TileColumn->AddChildToVerticalBox(IconBox)->SetHorizontalAlignment(HAlign_Center);
		TileColumn->AddChildToVerticalBox(NameBox)->SetHorizontalAlignment(HAlign_Center);
		// 입은 것은 원판 왼쪽 위에 작은 「입음」 표 — 이름 줄을 늘리지 않는다.
		UOverlay* const TileFace = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		if (UOverlaySlot* const ColumnSlot = TileFace->AddChildToOverlay(TileColumn))
		{
			ColumnSlot->SetHorizontalAlignment(HAlign_Center);
			ColumnSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (Tile.ItemID == Worn)
		{
			UBorder* const WornBadge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			WornBadge->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, Px(6.f)));
			WornBadge->SetBrushColor(SnareUiStyle::UiWoodPanel());
			WornBadge->SetPadding(FMargin(Px(4.f), 0.f));
			UTextBlock* const WornText = MakeLabel(*WidgetTree, NAME_None, LOCTEXT("WardrobeTileWorn", "입음"), TileFontSize);
			WornText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
			WornBadge->SetContent(WornText);
			if (UOverlaySlot* const BadgeSlot = TileFace->AddChildToOverlay(WornBadge))
			{
				BadgeSlot->SetHorizontalAlignment(HAlign_Left);
				BadgeSlot->SetVerticalAlignment(VAlign_Top);
			}
		}
		Button->AddChild(TileFace);
		TileIcons.Add(TileIcon);
		USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
		Proxy->Key = FName(*FString::FromInt(Index));
		Proxy->OnClicked = [WeakThis](FName Key)
		{
			if (UWardrobeWidget* const Self = WeakThis.Get())
			{
				Self->PressTile(FCString::Atoi(*Key.ToString()));
			}
		};
		Button->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
		Button->SetIsEnabled(!bPending);
		USizeBox* const TileBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		TileBox->SetWidthOverride(TileSize);
		TileBox->SetHeightOverride(TileSize);
		TileBox->SetContent(Button);
		if (UUniformGridSlot* const GridSlot = Grid->AddChildToUniformGrid(TileBox, Index / GridColumns, Index % GridColumns))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Fill);
			GridSlot->SetVerticalAlignment(VAlign_Fill);
		}
		Tiles.Add(Button);
		TileProxies.Add(Proxy);
	}
	// 새 원판에도 글꼴·눌림 소리를 입힌 뒤 크림 바탕 + 등급 테두리로 덮는다(등급은 테두리 색, 명세 2장).
	SnareUiStyle::ApplyUiStyle(*this);
	for (int32 Index = 0; Index < Tiles.Num(); ++Index)
	{
		PaintTile(Tiles[Index], SnareUiStyle::ItemGradeColor(ShownTiles[Index].Grade), ShownTiles[Index].ItemID == Worn);
	}
}

void UWardrobeWidget::BuildTree()
{
	UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("WardrobeRoot"));
	WidgetTree->RootWidget = Root;

	// 부위 표찰 — 화면 아래 가운데 4칸씩 두 줄(7칸 + 회전).
	SlotButtons.Reset();
	SlotProxies.Reset();
	const TWeakObjectPtr<UWardrobeWidget> WeakThis(this);
	UVerticalBox* const SlotRow = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("WardrobeSlotRow"));
	TArray<UHorizontalBox*> SlotLines;
	int32 SlotTabCount = 0;
	const auto AddSlotTab = [this, SlotRow, &SlotLines, &SlotTabCount](UButton* Button)
	{
		if (SlotTabCount++ % SlotTabsPerRow == 0)
		{
			UHorizontalBox* const Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
				FName(*FString::Printf(TEXT("WardrobeSlotLine_%d"), SlotLines.Num())));
			SlotRow->AddChildToVerticalBox(Line)->SetPadding(FMargin(0.f, SlotRowGap * 0.5f));
			SlotLines.Add(Line);
		}
		SlotLines.Last()->AddChildToHorizontalBox(WrapTouchSize(*WidgetTree, Button, SlotTabWidth, SlotTabHeight))->SetPadding(FMargin(SlotRowGap * 0.5f, 0.f));
	};
	const TArray<EEquipSlot>& Order = GetSlotOrder();
	for (int32 Index = 0; Index < Order.Num(); ++Index)
	{
		UButton* const Button = MakeTextButton(*WidgetTree, FName(*FString::Printf(TEXT("WardrobeSlot_%d"), Index)), SlotLabel(Order[Index]), SlotFontSize);
		USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
		Proxy->Key = FName(*FString::FromInt(Index));
		Proxy->OnClicked = [WeakThis](FName Key)
		{
			UWardrobeWidget* const Self = WeakThis.Get();
			const int32 SlotIndex = FCString::Atoi(*Key.ToString());
			if (Self != nullptr && GetSlotOrder().IsValidIndex(SlotIndex))
			{
				Self->SelectSlot(GetSlotOrder()[SlotIndex]);
			}
		};
		Button->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
		AddSlotTab(Button);
		SlotButtons.Add(Button);
		SlotProxies.Add(Proxy);
	}
	// 글꼴에 ↻ 그림 문자가 없어 글자로 (열기 카드 `!` 와 같은 사정, INBOX 확인 루프#357).
	UButton* const RotateButton = MakeTextButton(*WidgetTree, TEXT("WardrobeRotate"), LOCTEXT("WardrobeRotateText", "회전"), SlotFontSize);
	RotateButton->OnClicked.AddDynamic(this, &UWardrobeWidget::PressRotate);
	AddSlotTab(RotateButton);
	// 판 오른쪽 남은 폭 전체에 걸친 띠 안 가운데 — 캐릭터 발밑과 같은 가로 자리.
	UOverlay* const SlotBand = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("WardrobeSlotBand"));
	if (UOverlaySlot* const BandSlot = SlotBand->AddChildToOverlay(SlotRow))
	{
		BandSlot->SetHorizontalAlignment(HAlign_Center);
	}
	if (UCanvasPanelSlot* const RowSlot = Cast<UCanvasPanelSlot>(Root->AddChild(SlotBand)))
	{
		RowSlot->SetAnchors(FAnchors(0.f, 1.f, 1.f, 1.f));
		RowSlot->SetAlignment(FVector2D(0.f, 1.f));
		RowSlot->SetOffsets(FMargin(PanelInset + PanelWidth, -SlotRowBottomInset, 0.f, 0.f));
		RowSlot->SetAutoSize(true);
	}

	// 왼쪽 세로 나무판 — 탭 둘 · 원판 격자 · 알림 줄 · 상점 열기. 밧줄 테두리 두른 모서리 6px 판.
	UBorder* const Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("WardrobePanel"));
	Panel->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, SnareUiStyle::WoodPlateRadius, SnareUiStyle::UiRope(), SnareUiStyle::ScreenRopeWidth));
	Panel->SetBrushColor(SnareUiStyle::UiWoodPanel());
	Panel->SetPadding(FMargin(PanelPadding));
	// 판 밖으로 삐져나오는 것(격자 줄·밀대)은 판 테두리에서 자른다.
	Panel->SetClipping(EWidgetClipping::ClipToBounds);
	if (UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Panel)))
	{
		PanelSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 1.f));
		PanelSlot->SetOffsets(FMargin(PanelInset, PanelTopInset, PanelWidth, SlotRowBottomInset + SlotTabHeight + SlotRowGap));
	}
	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("WardrobeColumn"));
	Panel->SetContent(Column);

	UHorizontalBox* const Tabs = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("WardrobeTabs"));
	OwnedTabButton = MakeTextButton(*WidgetTree, TEXT("WardrobeOwnedTab"), LOCTEXT("WardrobeOwnedTabText", "내 꾸미기"), BodyFontSize);
	OwnedTabButton->OnClicked.AddDynamic(this, &UWardrobeWidget::PressOwnedTab);
	AllTabButton = MakeTextButton(*WidgetTree, TEXT("WardrobeAllTab"), LOCTEXT("WardrobeAllTabText", "전체"), BodyFontSize);
	AllTabButton->OnClicked.AddDynamic(this, &UWardrobeWidget::PressAllTab);
	Tabs->AddChildToHorizontalBox(WrapTouchSize(*WidgetTree, OwnedTabButton))->SetPadding(FMargin(0.f, 0.f, TileGap, 0.f));
	Tabs->AddChildToHorizontalBox(WrapTouchSize(*WidgetTree, AllTabButton));
	AddRow(*Column, Tabs, 0.f);

	Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("WardrobeGrid"));
	Grid->SetSlotPadding(FMargin(TileGap * 0.5f));
	Grid->SetMinDesiredSlotWidth(TileSize);
	Grid->SetMinDesiredSlotHeight(TileSize);
	UScrollBox* const Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("WardrobeGridScroll"));
	Scroll->AddChild(Grid);
	USizeBox* const GridBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("WardrobeGridBox"));
	// 세로 밀대 굵기만큼 더 — 밀대가 3열째 원판을 덮지 않게.
	GridBox->SetWidthOverride(GridColumns * (TileSize + TileGap) + Px(8.f));
	// 내 꾸미기가 빈 칸이면 격자 자리 한가운데에 안내 글과 「전체 보기」 단추를 얹는다(판 아래 테두리에 걸리지 않게).
	UOverlay* const GridOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("WardrobeGridOverlay"));
	GridOverlay->AddChildToOverlay(Scroll);
	EmptyHint = MakeLabel(*WidgetTree, TEXT("WardrobeEmptyHint"), FText::GetEmpty(), BodyFontSize);
	EmptyHint->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
	EmptyHint->SetAutoWrapText(true);
	EmptyHint->SetJustification(ETextJustify::Center);
	UButton* const EmptyAllButton = MakeTextButton(*WidgetTree, TEXT("WardrobeEmptyAll"), LOCTEXT("WardrobeEmptyAllText", "전체 꾸미기 보기"), BodyFontSize);
	EmptyAllButton->OnClicked.AddDynamic(this, &UWardrobeWidget::PressAllTab);
	UVerticalBox* const EmptyColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("WardrobeEmptyColumn"));
	AddRow(*EmptyColumn, EmptyHint, 0.f);
	AddRow(*EmptyColumn, WrapTouchSize(*WidgetTree, EmptyAllButton), TileGap);
	EmptyColumn->SetVisibility(ESlateVisibility::Collapsed);
	EmptyPanel = EmptyColumn;
	if (UOverlaySlot* const EmptySlot = GridOverlay->AddChildToOverlay(EmptyColumn))
	{
		EmptySlot->SetHorizontalAlignment(HAlign_Fill);
		EmptySlot->SetVerticalAlignment(VAlign_Center);
	}
	GridBox->SetContent(GridOverlay);
	AddRow(*Column, GridBox, TileGap);
	if (UVerticalBoxSlot* const GridSlot = Cast<UVerticalBoxSlot>(GridBox->Slot))
	{
		GridSlot->SetPadding(FMargin(0.f, TileGap, 0.f, GridBottomGap));
		// 격자는 판 안 남은 높이만 차지한다 — 고정 높이면 작은 화면에서 아래 줄이 판 밖으로 나갔다.
		GridSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	MessageText = MakeLabel(*WidgetTree, TEXT("WardrobeMessage"), FText::GetEmpty(), BodyFontSize);
	MessageText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
	AddRow(*Column, MessageText, TileGap);

	UHorizontalBox* const Bottom = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("WardrobeBottom"));
	UButton* const ShopButton = MakeTextButton(*WidgetTree, TEXT("WardrobeOpenShop"), LOCTEXT("WardrobeOpenShopText", "상점 열기"), BodyFontSize);
	ShopButton->OnClicked.AddDynamic(this, &UWardrobeWidget::PressOpenShop);
	// 옷장 상점은 젬 상품뿐이라 젬이 꺼진 빌드(1.0)는 입구를 접는다 (INBOX M94 ④).
	UButton* const QuitButton = MakeTextButton(*WidgetTree, TEXT("WardrobeQuit"), LOCTEXT("WardrobeQuitText", "나가기"), BodyFontSize);
	QuitButton->OnClicked.AddDynamic(this, &UWardrobeWidget::Close);
	UWidget* const ShopEntry = WrapTouchSize(*WidgetTree, ShopButton);
	ShopEntry->SetVisibility(USnareStoreSettings::AreGemsEnabled() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Bottom->AddChildToHorizontalBox(ShopEntry);
	UButton* Retry = nullptr;
	RetryBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("WardrobeRetryBox"), TEXT("WardrobeRetryButton"),
		LOCTEXT("WardrobeRetry", "다시 시도"), Retry);
	Retry->OnClicked.AddDynamic(this, &UWardrobeWidget::HandleRetryClicked);
	RetryBox->SetVisibility(ESlateVisibility::Collapsed);
	Bottom->AddChildToHorizontalBox(RetryBox);
	AddRow(*Column, Bottom, TileGap);

	// 「나가기」 는 왼쪽 위 구석.
	UWidget* const QuitEntry = WrapTouchSize(*WidgetTree, QuitButton);
	if (UCanvasPanelSlot* const QuitSlot = Cast<UCanvasPanelSlot>(Root->AddChild(QuitEntry)))
	{
		QuitSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		QuitSlot->SetAlignment(FVector2D(0.f, 0.f));
		QuitSlot->SetPosition(FVector2D(PanelInset, PanelInset));
		QuitSlot->SetAutoSize(true);
	}
}

TSharedRef<SWidget> UWardrobeWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
	}
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	RefreshDisplay();
	return Built;
}

#undef LOCTEXT_NAMESPACE

void UWardrobeWidget::HideSideButtons()
{
	// 지도 줄(표정·메뉴·지도)과 배낭 열기 단추는 각자 위젯이라 그 위젯째 접는다. 배낭 판은 옷장이 덮는 동안 이미 접혀 있다.
	const UWorld* const World = GetWorld();
	auto Hide = [this, World](UUserWidget* Widget)
	{
		if (IsValid(Widget) && Widget->GetWorld() == World && !HiddenSideWidgets.Contains(Widget))
		{
			HiddenSideWidgets.Add(Widget, Widget->GetVisibility());
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	};
	for (TObjectIterator<UWorldMapScreenWidget> It; It; ++It)
	{
		Hide(*It);
	}
	for (TObjectIterator<UBackpackScreenWidget> It; It; ++It)
	{
		Hide(*It);
	}
}

void UWardrobeWidget::RestoreSideButtons()
{
	for (const TPair<TWeakObjectPtr<UUserWidget>, ESlateVisibility>& Pair : HiddenSideWidgets)
	{
		if (UUserWidget* const Widget = Pair.Key.Get())
		{
			Widget->SetVisibility(Pair.Value);
		}
	}
	HiddenSideWidgets.Reset();
}
