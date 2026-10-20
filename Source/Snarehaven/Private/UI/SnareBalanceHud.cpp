#include "UI/SnareBalanceHud.h"

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "UI/SnareUiStyle.h"
#include "UI/SnareStoreSettings.h"
#include "Components/TextBlock.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace SnareBalanceHud
{
namespace
{
	// 레퍼런스 시트 `docs/reference/hud_ingame.md` "오른쪽 위: 남색 알약 170×45, 굵게 20px 흰색"(1920 기준).
	// 폭은 고정하지 않는다 — 존 이름·잔액 자릿수에 따라 알약이 늘어난다(시트는 좌표 한 줄이라 폭이 고정).
	constexpr float PlaceholderScreenMargin = 40.f;
	constexpr float PillHeight = 45.f;
	constexpr float PillPaddingX = 18.f;
	constexpr float PillPaddingY = 6.f;
	constexpr float PillGap = 16.f;
	constexpr int32 PillFontSize = 20;

	UTextBlock* AddPillText(UWidgetTree& Tree, UHorizontalBox& Row, FName Name, float LeftGap)
	{
		UTextBlock* const Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = PillFontSize;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
		if (UHorizontalBoxSlot* const Slot = Row.AddChildToHorizontalBox(Text))
		{
			Slot->SetVerticalAlignment(VAlign_Center);
			Slot->SetPadding(FMargin(LeftGap, 0.f, 0.f, 0.f));
		}
		return Text;
	}
}

FText FormatBalanceText(const USnareBackendSubsystem* Backend)
{
	// 주인(`PlayFabId`)이 없으면 응답도 캐시도 아직 없다는 뜻이다 — 그때는 0 을 보여 주지 않는다.
	// 숫자 자리에 `-` 만 두면 `코인 -   젬 -` 가 빈칸처럼 읽혀(`captures/240`, M21-01d) 왜 모르는지를 글로 쓴다.
	if (!IsValid(Backend) || !Backend->GetPlayerSnapshot().HasOwner())
	{
		return IsValid(Backend) && IsBalanceLoading(Backend->GetLoginState(), Backend->GetFetchState())
			? LOCTEXT("BalanceLoading", "불러오는 중…")
			: LOCTEXT("BalanceOffline", "서버 연결 안 됨");
	}

	// 알약 한 줄이라 한 줄로 쓴다(M15-02). 구분 기호 없이 띄어 쓴다.
	if (!USnareStoreSettings::AreGemsEnabled())
	{
		return FText::Format(LOCTEXT("BalanceRowPillCoinOnly", "코인 {0}"), FText::AsNumber(Backend->GetPlayerSnapshot().CoinBalance));
	}
	return FText::Format(LOCTEXT("BalanceRowPill", "코인 {0}   젬 {1}"),
		FText::AsNumber(Backend->GetPlayerSnapshot().CoinBalance), FText::AsNumber(Backend->GetPlayerSnapshot().GemBalance));
}

bool IsBalanceOffline(const USnareBackendSubsystem* Backend)
{
	return !IsValid(Backend)
		|| (!Backend->GetPlayerSnapshot().HasOwner() && !IsBalanceLoading(Backend->GetLoginState(), Backend->GetFetchState()));
}

bool IsBalanceLoading(ESnareBackendLoginState Login, ESnareBackendFetchState Fetch)
{
	// 로그인 중이거나, 로그인은 됐는데 보유 상태 왕복이 아직 안 끝났으면 곧 숫자가 온다.
	// 로그인을 시작도 안 했거나(`-SnareOffline`·자동 로그인 안 하는 자리) 실패했으면 기다려도 안 온다.
	return Login == ESnareBackendLoginState::InProgress
		|| (Login == ESnareBackendLoginState::LoggedIn && Fetch != ESnareBackendFetchState::Failed);
}

UTextBlock* AddPlaceholderRow(UWidgetTree& Tree, UCanvasPanel& Root, UTextBlock** OutZoneText)
{
	UBorder* const Pill = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BalancePill"));
	Pill->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiNavy(), PillHeight * 0.5f));
	Pill->SetPadding(FMargin(PillPaddingX, PillPaddingY));
	Pill->SetVerticalAlignment(VAlign_Center);
	// 레퍼런스 화면(영상 Ov49JXK9pgY)엔 잔액·버전 글자가 없다 — 잔액은 배낭 판 아래 바에서 본다. 글자는 계속 채워
	// 검사·다른 화면이 같은 한 줄을 읽게 두고 알약만 접는다(감독 2026-09-25).
	Pill->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* const Slot = Cast<UCanvasPanelSlot>(Root.AddChild(Pill)))
	{
		Slot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		Slot->SetAlignment(FVector2D(1.f, 0.f));
		Slot->SetPosition(FVector2D(-PlaceholderScreenMargin, PlaceholderScreenMargin));
		Slot->SetAutoSize(true);
	}

	UHorizontalBox* const Row = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BalancePillRow"));
	Pill->SetContent(Row);
	// 존 이름은 사냥 HUD 만 채운다 — 목장은 존이 하나라 이름을 적을 까닭이 없다.
	if (OutZoneText != nullptr)
	{
		*OutZoneText = AddPillText(Tree, *Row, TEXT("PillZoneText"), 0.f);
	}
	return AddPillText(Tree, *Row, TEXT("BalanceText"), OutZoneText != nullptr ? PillGap : 0.f);
}
}

#undef LOCTEXT_NAMESPACE
