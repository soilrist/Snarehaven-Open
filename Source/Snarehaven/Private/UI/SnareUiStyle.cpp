#include "UI/SnareUiStyle.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/ButtonSlot.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Data/ItemRow.h"
#include "Engine/Font.h"
#include "Sound/SoundBase.h"

namespace SnareUiStyle
{
namespace
{
	// 설계서에 적힌 헥스는 sRGB 다. Slate 색은 선형이라 그대로 넣으면 화면에서 더 밝게 뜬다(루프#76 이
	// 바닥색에서 겪은 것과 같은 일) — `FromSRGBColor` 가 그 변환을 한다.
	FLinearColor FromHex(const TCHAR* Hex)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
	}

	// 버튼 누름 소리 (DESIGN 2.2 소리) — Slate 가 눌림마다 알아서 낸다. 한 번 찾아 들고 있는다.
	FSlateSound ClickSound()
	{
		static TSoftObjectPtr<USoundBase> Click(FSoftObjectPath(TEXT("/Game/Game/Audio/SFX/SFX_Ui_Click_01.SFX_Ui_Click_01")));
		FSlateSound Sound;
		Sound.SetResourceObject(Click.LoadSynchronous());
		return Sound;
	}
}

FLinearColor UiButtonBase()      { return FromHex(TEXT("#F2E4C9")); }
FLinearColor UiButtonPressed()   { return FromHex(TEXT("#D8C29E")); }
FLinearColor UiButtonHighlight() { return FromHex(TEXT("#F7D98C")); }
FLinearColor UiTextOnButton()    { return FromHex(TEXT("#4A3826")); }
FLinearColor UiWoodButton()        { return FromHex(TEXT("#88603E")); }
FLinearColor UiWoodButtonPressed() { return FromHex(TEXT("#74512F")); }
FLinearColor UiRope()              { return FromHex(TEXT("#D9BF8C")); }
FLinearColor UiPanelCream()      { return FromHex(TEXT("#F5EBD2")); }
FLinearColor UiTitleTab()        { return FromHex(TEXT("#5A3E36")); }

FLinearColor UiNameTagColor(int32 PlayerIndex)
{
	// 앞 넷 = 레퍼런스 `steam_19` 노랑·초록·파랑·분홍, 뒤 넷 = 방 최대 8명이 겹치지 않게 더한 주황·보라·청록·연두 (DESIGN 2.2 「이름표」).
	// 10-16 접근성: 색상각은 그대로 두고 흰 이름 글 대비 4.6 이상까지 어둡게 (옛 E8A93A·4FB36A·4C86DB·E0679F·E07B3F·9467D6·2AA6A6·8FB33B 은 2.1~3.6).
	static const TCHAR* const Colors[] = {TEXT("#9D6B12"), TEXT("#38824C"), TEXT("#3073D5"), TEXT("#D5307C"),
		TEXT("#BA581E"), TEXT("#8C5BD3"), TEXT("#218181"), TEXT("#647D29")};
	constexpr int32 Num = UE_ARRAY_COUNT(Colors);
	return FromHex(Colors[((PlayerIndex % Num) + Num) % Num]);
}
FLinearColor UiEmptySlot()       { return FromHex(TEXT("#E9E0C8")); }
FLinearColor UiSlotWell()        { return FromHex(TEXT("#2E2218")); }
FLinearColor UiNavy()            { return FromHex(TEXT("#2B2D5C")); }
FLinearColor UiBoltYellow()      { return FromHex(TEXT("#F5D33B")); }
FLinearColor UiAccentRed()       { return FromHex(TEXT("#BF3B38")); }
FLinearColor UiIconMint()        { return FromHex(TEXT("#64EBA9")); }
FLinearColor UiShopPriceGreen()  { return FromHex(TEXT("#27783F")); }
FLinearColor UiWarnRed()         { return FromHex(TEXT("#B8322F")); }
FLinearColor UiTextOnWood()      { return FromHex(TEXT("#FFF8E8")); }
FLinearColor UiWarnOnWood()      { return FromHex(TEXT("#FFB08A")); }
FLinearColor UiWarnChip()        { return FromHex(TEXT("#4A1C12")); }
FLinearColor UiWoodPanel()       { return FromHex(TEXT("#4A3826")); }
FLinearColor UiDisabledGray()    { return FromHex(TEXT("#6E675F")); }
FLinearColor UiTextDim()         { return FromHex(TEXT("#756551")); }

float ContrastRatio(const FLinearColor& A, const FLinearColor& B)
{
	// WCAG 상대 휘도 — 선형 색에 가중합. 큰 쪽 / 작은 쪽.
	const auto Luminance = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
	const float La = Luminance(A);
	const float Lb = Luminance(B);
	return (FMath::Max(La, Lb) + 0.05f) / (FMath::Min(La, Lb) + 0.05f);
}

void MakeRoundButton(UButton& Button, float Radius, float Opacity)
{
	FButtonStyle Style = Button.GetStyle();
	const FSlateRoundedBoxBrush Round(FLinearColor::White, Radius);
	Style.Normal = Round;
	Style.Hovered = Round;
	Style.Pressed = Round;
	Style.Disabled = Round;
	Button.SetStyle(Style);
	Button.SetBackgroundColor(FLinearColor(1.f, 1.f, 1.f, Opacity));
}

UBorder* MakeCreamCard(UWidgetTree& Tree, const TCHAR* Name)
{
	// 시트 카드 안쪽 여백: 가로 16 · 세로 12.
	constexpr float CardPaddingX = 16.f;
	constexpr float CardPaddingY = 12.f;
	UBorder* const Card = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
	// M97-c-2: 크림 둥근 카드 → 밧줄 두른 모서리 6px 판 (설치 안내·말풍선 카드 공용).
	Card->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, WoodPlateRadius, UiRope(), HudRopeWidth));
	Card->SetBrushColor(UiPanelCream());
	Card->SetPadding(FMargin(CardPaddingX, CardPaddingY));
	Card->SetVisibility(ESlateVisibility::Collapsed);
	return Card;
}

UBorder* MakeWoodSignCard(UWidgetTree& Tree, const TCHAR* Name)
{
	// DESIGN 7.1 머리 표찰 모서리 4px · 밧줄 두께는 HUD 작은 판과 같은 3px.
	constexpr float SignRadius = 4.f;
	UBorder* const Card = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
	Card->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, SignRadius, UiRope(), HudRopeWidth));
	Card->SetBrushColor(UiWoodButton());
	Card->SetPadding(FMargin(16.f, 12.f));
	Card->SetVisibility(ESlateVisibility::Collapsed);
	return Card;
}

FMenuPanelParts AddMenuPanel(UWidgetTree& Tree, UCanvasPanel& Root, const FText& Title, float PanelTop,
	float PanelWidth, float PanelHeight, float PillWidth)
{
	constexpr float PillFontSize = 24.f;
	FMenuPanelParts Parts;

	Parts.Panel = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuPanel"));
	Parts.Panel->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, ScreenPanelRadius));
	Parts.Panel->SetBrushColor(UiPanelCream());
	Parts.Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
	Parts.TitlePill = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuTitlePill"));
	Parts.TitlePill->SetBrush(FSlateRoundedBoxBrush(UiTitleTab(), PanelTitlePlateRadius));
	Parts.TitlePill->SetHorizontalAlignment(HAlign_Center);
	Parts.TitlePill->SetVerticalAlignment(VAlign_Center);
	Parts.TitlePill->SetVisibility(ESlateVisibility::HitTestInvisible);
	Parts.TitleText = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	Parts.TitleText->SetText(Title);
	Parts.TitleText->SetJustification(ETextJustify::Center);
	Parts.TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	SetFontSize(*Parts.TitleText, PillFontSize);
	Parts.TitlePill->SetContent(Parts.TitleText);

	const TTuple<UBorder*, float, float, float> Layers[] = {
		{Parts.Panel, PanelTop, PanelWidth, PanelHeight},
		{Parts.TitlePill, PanelTop - MenuTitlePillRise, PillWidth, MenuTitlePillHeight}};
	for (const TTuple<UBorder*, float, float, float>& Layer : Layers)
	{
		if (UCanvasPanelSlot* const Slot = Cast<UCanvasPanelSlot>(Root.AddChild(Layer.Get<0>())))
		{
			Slot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
			Slot->SetAlignment(FVector2D(0.5f, 0.f));
			Slot->SetPosition(FVector2D(0.f, Layer.Get<1>()));
			Slot->SetSize(FVector2D(Layer.Get<2>(), Layer.Get<3>()));
		}
	}
	return Parts;
}

USizeBox* MakePanelTitlePill(UWidgetTree& Tree, FName BoxName, FName PillName, UTextBlock& Title)
{
	UBorder* const Pill = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), PillName);
	Pill->SetBrush(FSlateRoundedBoxBrush(UiTitleTab(), PanelTitlePlateRadius));
	Pill->SetHorizontalAlignment(HAlign_Center);
	Pill->SetVerticalAlignment(VAlign_Center);
	Pill->SetPadding(FMargin(PanelTitlePillPaddingX, 0.f));
	Title.SetJustification(ETextJustify::Center);
	Title.SetColorAndOpacity(FSlateColor(FLinearColor::White));
	SetFontSize(Title, PanelTitlePillFontSize);
	Pill->SetContent(&Title);
	USizeBox* const Box = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass(), BoxName);
	Box->SetMinDesiredWidth(PanelTitlePillMinWidth);
	Box->SetHeightOverride(PanelTitlePillHeight);
	Box->SetVisibility(ESlateVisibility::HitTestInvisible);
	Box->AddChild(Pill);
	return Box;
}

USizeBox* MakeCloseButton(UWidgetTree& Tree, FName BoxName, FName ButtonName, const FText& Label, UButton*& OutButton)
{
	OutButton = Tree.ConstructWidget<UButton>(UButton::StaticClass(), ButtonName);
	MakeRoundButton(*OutButton, HudCornerRadius);
	UTextBlock* const Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*(ButtonName.ToString() + TEXT("Text"))));
	Text->SetText(Label);
	Text->SetJustification(ETextJustify::Center);
	SetFontSize(*Text, MenuFontMin + 4.f);
	OutButton->SetContent(Text);
	USizeBox* const Box = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass(), BoxName);
	Box->SetHeightOverride(MenuTouchMin);
	Box->SetMinDesiredWidth(MenuTouchMin * 2.f);
	Box->AddChild(OutButton);
	return Box;
}

UOverlay* MakeClearLabelButton(UWidgetTree& Tree, FName ButtonName, const FText& Label, float FontSize,
	UButton*& OutButton, UTextBlock*& OutLabel)
{
	UOverlay* const Stack = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass(), FName(*(ButtonName.ToString() + TEXT("Stack"))));
	OutButton = Tree.ConstructWidget<UButton>(UButton::StaticClass(), ButtonName);
	MakeRoundButton(*OutButton, HudCornerRadius);
	if (UOverlaySlot* const ButtonSlot = Stack->AddChildToOverlay(OutButton))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetVerticalAlignment(VAlign_Fill);
	}
	OutLabel = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*(ButtonName.ToString() + TEXT("Text"))));
	OutLabel->SetText(Label);
	OutLabel->SetJustification(ETextJustify::Center);
	OutLabel->SetColorAndOpacity(FSlateColor(UiTextOnWood()));
	OutLabel->SetVisibility(ESlateVisibility::HitTestInvisible);   // 눌림은 밑 단추가 받는다
	SetFontSize(*OutLabel, FontSize);
	if (UOverlaySlot* const LabelSlot = Stack->AddChildToOverlay(OutLabel))
	{
		LabelSlot->SetHorizontalAlignment(HAlign_Center);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(12.f, 0.f));
	}
	return Stack;
}

FVector2D PillarPosition(int32 Column, int32 Row)
{
	return FVector2D(-(PillarRight + Column * (PillarButtonSize + PillarGap)),
		-(PillarBottom + Row * (PillarButtonSize + PillarGap)));
}

void PlacePillarButton(UWidgetTree& Tree, UCanvasPanel& Canvas, UButton& Button, UTexture2D* Icon,
	const FText& Label, FName LabelName, int32 Column, int32 Row)
{
	// 10-16 HUD 3차: 아이콘이 타일의 약 80% 를 채우고, 라벨은 작은 글씨로 타일 맨 아래에 겹친다 (진갈색 테두리로 아이콘 위에서도 읽힘).
	UOverlay* const Stack = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass());
	if (Icon != nullptr)
	{
		UImage* const Image = Tree.ConstructWidget<UImage>(UImage::StaticClass(), FName(*LabelName.ToString().Replace(TEXT("Label"), TEXT("Icon"))));
		FSlateBrush IconBrush;
		IconBrush.SetResourceObject(Icon);
		IconBrush.ImageSize = FVector2D(PillarIconSize);
		Image->SetBrush(IconBrush);
		if (UOverlaySlot* const IconSlot = Stack->AddChildToOverlay(Image))
		{
			IconSlot->SetHorizontalAlignment(HAlign_Center);
			IconSlot->SetVerticalAlignment(VAlign_Top);
		}
	}
	UTextBlock* const Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), LabelName);
	Text->SetText(Label);
	Text->SetJustification(ETextJustify::Center);
	if (Icon != nullptr)
	{
		StylePriceOnWood(*Text);   // 크림 글씨 + 진갈색 테두리
		SetFontSize(*Text, PillarLabelFontSize);
	}
	else
	{
		Text->SetColorAndOpacity(FSlateColor(UiTextOnButton()));
		SetFontSize(*Text, PillarLabelOnlyFontSize);
	}
	if (UOverlaySlot* const TextSlot = Stack->AddChildToOverlay(Text))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetVerticalAlignment(Icon != nullptr ? VAlign_Bottom : VAlign_Center);
	}
	if (UButtonSlot* const StackSlot = Cast<UButtonSlot>(Button.SetContent(Stack)))
	{
		StackSlot->SetPadding(FMargin(PillarIconPadding));
		StackSlot->SetHorizontalAlignment(HAlign_Fill);
		StackSlot->SetVerticalAlignment(VAlign_Fill);
	}
	if (UCanvasPanelSlot* const ButtonSlot = Cast<UCanvasPanelSlot>(Canvas.AddChild(&Button)))
	{
		ButtonSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
		ButtonSlot->SetAlignment(FVector2D(1.f, 1.f));
		ButtonSlot->SetPosition(PillarPosition(Column, Row));
		ButtonSlot->SetSize(FVector2D(PillarButtonSize));
	}
}

void SetFontSize(UTextBlock& Text, float Size)
{
	FSlateFontInfo Font = Text.GetFont();
	Font.Size = Size;
	Text.SetFont(Font);
}

void StylePriceOnWood(UTextBlock& Text)
{
	constexpr int32 PriceOutlinePx = 2;
	Text.SetColorAndOpacity(FSlateColor(UiPanelCream()));
	FSlateFontInfo Font = Text.GetFont();
	Font.OutlineSettings.OutlineSize = PriceOutlinePx;
	Font.OutlineSettings.OutlineColor = UiTextOnButton();
	Text.SetFont(Font);
}

FLinearColor ItemGradeColor(EItemGrade Grade)
{
	switch (Grade)
	{
	case EItemGrade::Rare:      return FromHex(TEXT("#F0895A"));
	case EItemGrade::Epic:      return FromHex(TEXT("#A46BE0"));
	case EItemGrade::Legendary: return FromHex(TEXT("#F2C14E"));
	default:                    return FromHex(TEXT("#6CCB8A"));
	}
}

FLinearColor ItemGradeBandColor(EItemGrade Grade)
{
	switch (Grade)
	{
	case EItemGrade::Rare:      return FromHex(TEXT("#5FAF63"));
	case EItemGrade::Epic:      return FromHex(TEXT("#4C86D6"));
	case EItemGrade::Legendary: return FromHex(TEXT("#E8B53A"));
	default:                    return FromHex(TEXT("#8A7F70"));
	}
}

void AddGradeBand(UWidgetTree& Tree, UOverlay& Stack, const FName Name, const FLinearColor& Color, const float CellSize)
{
	// 칸 틀 안쪽에 얹혀 둥근 모서리를 안 넘는다.
	USizeBox* const Box = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(*(Name.ToString() + TEXT("Box"))));
	Box->SetHeightOverride(CellSize / 5.f);
	UBorder* const Band = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
	Band->SetBrush(FSlateColorBrush(Color));
	Box->SetContent(Band);
	Box->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* const BandSlot = Stack.AddChildToOverlay(Box))
	{
		BandSlot->SetHorizontalAlignment(HAlign_Fill);
		BandSlot->SetVerticalAlignment(VAlign_Bottom);
		BandSlot->SetPadding(FMargin(2.f, 0.f, 2.f, 2.f));
	}
}

FLinearColor ScreenTextColor(bool bDim)
{
	return bDim ? UiTextDim() : UiTextOnButton();
}

UBorder* AddScreenPanel(UWidgetTree& Tree, UCanvasPanel& Root, FName BackdropName, FName PanelName)
{
	UBorder* const Backdrop = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), BackdropName);
	Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, ScreenBackdropDimOpacity));
	Backdrop->SetVisibility(ESlateVisibility::Visible);
	// DESIGN 7.1: 그림자(오른쪽 아래 4px) → 밧줄 테두리 두른 나무 틀(14px) → 크림 판. 틀·그림자는 눌림을 먹지 않는다(판이 먹는다).
	UBorder* const Shadow = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ScreenPanelShadow"));
	FLinearColor ShadowColor = FromHex(TEXT("#2B1B10"));
	ShadowColor.A = ScreenShadowOpacity;
	Shadow->SetBrush(FSlateRoundedBoxBrush(ShadowColor, ScreenPanelRadius));
	Shadow->SetVisibility(ESlateVisibility::HitTestInvisible);
	UBorder* const Frame = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ScreenPanelWoodFrame"));
	Frame->SetBrush(FSlateRoundedBoxBrush(UiWoodButton(), ScreenPanelRadius, UiRope(), ScreenRopeWidth));
	Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
	UBorder* const Panel = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), PanelName);
	FLinearColor PanelColor = UiPanelCream();
	PanelColor.A = ScreenPanelOpacity;
	Panel->SetBrush(FSlateRoundedBoxBrush(PanelColor, ScreenPanelRadius, UiWoodButtonPressed(), 2.f));
	Panel->SetVisibility(ESlateVisibility::Visible);
	const float FrameInset = ScreenPanelInset - ScreenWoodFrameWidth;
	const TTuple<UBorder*, FMargin> Layers[] = {{Backdrop, FMargin(0.f)},
		{Shadow, FMargin(FrameInset + ScreenShadowOffset, FrameInset + ScreenShadowOffset, FrameInset - ScreenShadowOffset, FrameInset - ScreenShadowOffset)},
		{Frame, FMargin(FrameInset)}, {Panel, FMargin(ScreenPanelInset)}};
	for (const TTuple<UBorder*, FMargin>& Layer : Layers)
	{
		if (UCanvasPanelSlot* const Slot = Cast<UCanvasPanelSlot>(Root.AddChild(Layer.Get<0>())))
		{
			Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			Slot->SetOffsets(Layer.Get<1>());
		}
	}
	return Panel;
}

void MarkSelectedTab(UButton* Button, bool bSelected)
{
	if (!IsValid(Button))
	{
		return;
	}
	FButtonStyle Style = Button->GetStyle();
	Style.Normal.TintColor = FSlateColor(bSelected ? UiButtonHighlight() : UiWoodButton());
	// 고른 탭은 색만이 아니라 진갈색 테두리 3px 로도 구분한다 (M99-06).
	constexpr float SelectedTabOutline = 3.f;
	Style.Normal.OutlineSettings.Color = bSelected ? UiTitleTab() : FLinearColor::Transparent;
	Style.Normal.OutlineSettings.Width = bSelected ? SelectedTabOutline : 0.f;
	Button->SetStyle(Style);
	// 고른 탭은 노란 바탕이라 진갈색 글, 나무 바탕은 크림 글. 단추 안 글자(`UScaleBox` 한 겹 포함)만 건드린다.
	UWidget* const Content = Button->GetContent();
	UWidget* const Holder = Cast<UScaleBox>(Content) != nullptr ? Cast<UScaleBox>(Content)->GetContent() : Content;
	if (UTextBlock* const Label = Cast<UTextBlock>(Holder))
	{
		Label->SetColorAndOpacity(FSlateColor(bSelected ? UiTextOnButton() : UiPanelCream()));
	}
}

void StyleWoodTab(UButton* Button, bool bSelected)
{
	if (!IsValid(Button))
	{
		return;
	}
	FButtonStyle Style = Button->GetStyle();
	// 10-16: 고른 탭을 판 색(`UiWoodPanel`)으로 칠하면 판에 묻혀 안 고른 탭이 더 도드라졌다 — 고른 쪽을 노랗게.
	const FLinearColor Base = bSelected ? UiButtonHighlight() : UiWoodButton();
	Style.Normal.TintColor = FSlateColor(Base);
	Style.Hovered.TintColor = FSlateColor(UiWoodButton());
	Style.Pressed.TintColor = FSlateColor(UiWoodButtonPressed());
	Style.Normal.OutlineSettings.Width = 0.f;
	Button->SetStyle(Style);
	if (UTextBlock* const Label = Cast<UTextBlock>(Button->GetContent()))
	{
		Label->SetColorAndOpacity(FSlateColor(bSelected ? UiTextOnButton() : UiTextOnWood()));
	}
}

UFont* GetUiFont()
{
	// 문자열 경로로 매번 찾지 않는다(`docs/engines/unreal.md` 코드 규칙 4) — 한 번 찾아 들고 있는다.
	static TSoftObjectPtr<UFont> UiFont(FSoftObjectPath(TEXT("/Game/Game/UI/Fonts/Font_UI_Jua.Font_UI_Jua")));
	return UiFont.LoadSynchronous();
}

FText ButtonAccessibleName(const UButton& Button)
{
	// 1) 단추 안 글  2) 같은 겹(`UOverlay`) 위에 얹은 라벨(`MakeClearLabelButton`)  3) 툴팁 — 그림만 있는 단추용.
	const auto FirstText = [](UWidget* Root) -> FText
	{
		FText Found;
		const auto Take = [&Found](UWidget* Child)
		{
			const UTextBlock* const Text = Cast<UTextBlock>(Child);
			if (Found.IsEmpty() && Text != nullptr && !Text->GetText().IsEmptyOrWhitespace())
			{
				Found = Text->GetText();
			}
		};
		if (Root != nullptr)
		{
			// `ForWidgetAndChildren` 은 자식만 돈다 — 뿌리(단추 안 글 하나뿐인 경우)는 따로 본다.
			Take(Root);
			UWidgetTree::ForWidgetAndChildren(Root, Take);
		}
		return Found;
	};
	FText Name = FirstText(Button.GetContent());
	if (const UOverlay* const Stack = Cast<UOverlay>(Button.GetParent()); Name.IsEmpty() && Stack != nullptr)
	{
		for (UWidget* const Sibling : Stack->GetAllChildren())
		{
			if (Name.IsEmpty() && Sibling != &Button && !Cast<UButton>(Sibling))
			{
				Name = FirstText(Sibling);
			}
		}
	}
	return Name.IsEmpty() ? Button.GetToolTipText() : Name;
}

bool IsInsideScrollBox(const UWidget& Widget)
{
	for (const UWidget* Parent = Widget.GetParent(); Parent != nullptr; Parent = Parent->GetParent())
	{
		if (Parent->IsA<UScrollBox>())
		{
			return true;
		}
	}
	return false;
}

void ApplyUiStyle(UUserWidget& Widget)
{
	if (Widget.WidgetTree == nullptr)
	{
		return;
	}

	UFont* const Font = GetUiFont();
	const FSlateSound Click = ClickSound();

	Widget.WidgetTree->ForEachWidget([Font, &Click](UWidget* Child)
	{
		if (UButton* const Button = Cast<UButton>(Child))
		{
			// 엔진 기본 스타일을 **덮지 않고 색만 바꾼다** — 모서리 둥글기·여백 같은 모양은 엔진이 정한
			// 것을 그대로 쓴다(그 숫자는 설계서에 없다). 브러시 tint 는 그림에 곱해지는 값이다.
			FButtonStyle Style = Button->GetStyle();
			Style.Normal.TintColor = FSlateColor(UiWoodButton());
			Style.Hovered.TintColor = FSlateColor(UiWoodButton());
			Style.Pressed.TintColor = FSlateColor(UiWoodButtonPressed());
			// 비활성도 눌림과 같은 색이다(DESIGN 2.1 `UI_ButtonPressed` — "버튼 눌림/비활성").
			// **그림까지 기본 것으로 바꿔 끼운다** — 엔진 기본 Disabled 브러시는 다른(회색·반투명) 그림이라
			// tint 만 바꾸면 화면에서 팔레트 색이 안 나온다(3층에서 `참가`·`옵션`·`포획` 이 회색으로 나왔다).
			Style.Disabled = Style.Normal;
			// 10-16: 눌림색(#74512F)이던 비활성은 켜진 나무 단추와 거의 같아 「꺼짐」 이 안 보였다 — 회갈색으로 가른다.
			Style.Disabled.TintColor = FSlateColor(UiDisabledGray());
			Style.PressedSlateSound = Click;
			Button->SetStyle(Style);
			// 스크롤 목록 안 버튼은 손가락을 대는 순간 터치를 잡으면(기본 DownAndUp) 끌기가 스크롤로 안 넘어가
			// 목록이 안 내려가고 줄이 눌린다(폰 상점 제보, M22). 정확히 탭했을 때만 눌리게 한다.
			if (IsInsideScrollBox(*Button))
			{
				Button->SetTouchMethod(EButtonTouchMethod::PreciseTap);
				Button->SetClickMethod(EButtonClickMethod::PreciseClick);
			}
#if WITH_ACCESSIBILITY
			// VoiceOver 이름 (10-16 접근성) — 엔진 기본(Summary)은 단추 **안** 글만 읽어 그림 단추·겹 라벨 단추가 이름 없이 읽혔다.
			// 읽는 순간 다시 찾으므로 나중에 바뀐 라벨(가격·개수)도 따라간다. Slate 가 아직 없으면 그 화면의 다음 `ApplyUiStyle` 이 붙인다.
			if (const TSharedPtr<SWidget> Slate = Button->GetCachedWidget())
			{
				const TWeakObjectPtr<UButton> Weak(Button);
				Slate->SetAccessibleBehavior(EAccessibleBehavior::Custom, TAttribute<FText>::CreateLambda([Weak]()
				{
					return Weak.IsValid() ? ButtonAccessibleName(*Weak.Get()) : FText::GetEmpty();
				}));
			}
#endif
			return;
		}

		UTextBlock* const Text = Cast<UTextBlock>(Child);
		if (!IsValid(Text))
		{
			return;
		}

		if (Font != nullptr)
		{
			FSlateFontInfo FontInfo = Text->GetFont();
			// 화면 글자에 물릴 수 있는 것은 `UFont` 뿐이다 — `UFontFace` 는 `IFontProviderInterface` 가 없다.
			FontInfo.FontObject = Font;
			FontInfo.TypefaceFontName = TEXT("Default");   // 임포트가 만든 기본 타입페이스 이름
			Text->SetFont(FontInfo);
		}

		// 아이보리 바탕 위에서는 기본 흰 글자가 안 보인다. 버튼 **안**에 든 글자만 갈색으로 — 버튼 밖
		// 글자(제목·HUD 숫자)는 어두운 화면 위라 그대로 둔다.
		// #0199 넘침 방지 `UScaleBox` 한 겹을 사이에 둔 버튼 글자도 버튼 글자다.
		const UWidget* const Holder = Cast<UScaleBox>(Text->GetParent()) != nullptr ? static_cast<const UWidget*>(Text->GetParent()) : Text;
		if (Cast<UButton>(Holder->GetParent()) != nullptr)
		{
			Text->SetColorAndOpacity(FSlateColor(UiPanelCream()));   // 나무 단추 위 글은 크림 (DESIGN 7.1)
		}
	});
}
}
