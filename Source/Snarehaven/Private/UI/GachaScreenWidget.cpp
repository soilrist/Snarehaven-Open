#include "UI/GachaScreenWidget.h"

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Snarehaven.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/ItemRow.h"
#include "Data/GachaCostRow.h"
#include "Data/GachaPoolRow.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Engine/DataTable.h"
#include "Ranch/RanchSyncComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UI/CrateScreenWidget.h"
#include "UI/SnareUiStyle.h"
#include "UI/SnareStoreSettings.h"
#include "World/SnareSound.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 UI 배치 (Autonomy.md 🟡 — 게임 규칙 숫자가 아니라 "화면 어디에 얼마나 크게" 다).
	// 진짜 디자인이 WBP 에 얹히면 아래 트리는 아예 만들어지지 않는다. 가로 한가운데 세로 한 줄로 쌓는
	// 것은 로비·일시정지 메뉴와 같은 배치다 — 화면마다 다른 곳에 두면 손가락이 매번 자리를 다시 찾는다.
	// M15-18 화면 배치 — 상점과 **같은 규격**이다(시트 `docs/reference/shop_vendor.md` 「우리 것과 다르게 할 점」
	// 뽑기 줄: 왼쪽 칸 = 등급 원판·이름·확률 + 사는-것 규격 뽑기 줄, 오른쪽 칸 = 기계 명함).
	// 게임 규칙 숫자가 아니다 — "화면 어디에 얼마나 크게" 다.
	constexpr float PanelWidth = 600.f;
	constexpr float PanelHeight = 600.f;
	constexpr float PanelRadius = 16.f;
	constexpr float PanelPadding = 24.f;
	constexpr float DividerWidth = 4.f;
	constexpr float ColumnGap = 16.f;
	/** 뽑기 줄 — 10-16 68 → 76: 판 배율 1.6 을 곱하면 121.6, 애플 44pt(121) 이상 (DESIGN 2). */
	constexpr float RowHeight = 76.f;
	constexpr float RowGap = 8.f;
	constexpr float DiscSize = 52.f;
	constexpr float ArrowWidth = 28.f;
	constexpr int32 PriceFontSize = 18;
	constexpr int32 RowNameFontSize = 18;
	constexpr int32 NameFontSize = 20;
	constexpr int32 LetterFontSize = 16;
	constexpr float PillWidth = 130.f;
	constexpr float PillHeight = 50.f;
	constexpr float PillRise = 20.f;
	constexpr float NamePillHeight = 44.f;
	// 판은 상점·배낭과 같은 배율로 키운다 — 폰에서 한 창만 작으면 짝이 안 맞는다 (감독 2026-09-25).
	constexpr float PhoneScale = 1.6f;
	const FVector2D SheetSize(1920.f, 1080.f);
	// 서버 `RollGacha` 의 10연차 `rollCount` (MULTI_ROLL_COUNT) — 게임 숫자가 아니라 요청 모양이다.
	constexpr int32 TenPullRollCount = 10;

	UTextBlock* MakeLabel(UWidgetTree& Tree, FName Name, const FText& Label, int32 FontSize, const FLinearColor& Color)
	{
		UTextBlock* const Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetText(Label);
		// 크림 바탕 위라 기본 흰 글자는 안 보인다 — 색은 부르는 쪽이 팔레트에서 고른다.
		Text->SetColorAndOpacity(FSlateColor(Color));
		SnareUiStyle::SetFontSize(*Text, static_cast<float>(FontSize));
		return Text;
	}

	/**
	 * 등급 원판 색 — 뽑기 등급과 아이템 등급은 **같은 네 등급을 같은 차례로** 적는다
	 * (`EGachaGrade`·`EItemGrade` 둘 다 커먼→레어→에픽→레전더리, DESIGN 10.2 · 8.12).
	 * 색을 여기서 새로 정하면 배낭 칸과 뽑기 줄의 "레어" 가 다른 색이 된다.
	 */
	FLinearColor GradeDiscColor(EGachaGrade Grade)
	{
		return SnareUiStyle::ItemGradeColor(static_cast<EItemGrade>(static_cast<uint8>(Grade)));
	}

	// 확률 표기는 소수 한 자리 고정 — 공개 문서(`docs/GACHA_DISCLOSURE.md`)가 `70.0%`·`6.5%` 로 적는다.
	// 자리수를 여기서 바꾸면 화면과 문서가 달라진다(= 오표시, DESIGN 10.4).
	FNumberFormattingOptions MakePercentFormat()
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = 1;
		Options.MaximumFractionalDigits = 1;
		return Options;
	}

	// 미정(-1) 자리표시는 0 이 아니라 `-` 로 보여 준다 — 0 으로 적으면 "공짜" 로 읽힌다
	// (Verification.md 7 · `SnareBalanceHud::FormatBalanceText` 와 같은 규칙).
	FText CostText(int32 Value)
	{
		return Value < 0 ? LOCTEXT("GachaValueUnknown", "-") : FText::AsNumber(Value);
	}

	// 환급 재화 코드(`SC`·`GM`)를 화면 글자로. 코드는 백엔드가 쓰는 이름이라 **한 곳에서만** 읽는다
	// (`USnareBackendSubsystem` — 여기에 "SC" 를 다시 적으면 재화 코드가 두 군데에 산다).
	// 모르는 코드가 오면 이름을 지어내지 않고 `-` 로 둔다 (Verification.md 7).
	FText RefundCurrencyName(const FString& Code)
	{
		if (Code == USnareBackendSubsystem::GetCoinCurrencyCode())
		{
			return LOCTEXT("GachaRefundCoin", "코인");
		}
		if (Code == USnareBackendSubsystem::GetGemCurrencyCode())
		{
			return LOCTEXT("GachaRefundGem", "젬");
		}
		return LOCTEXT("GachaValueUnknown", "-");
	}
}

UGachaScreenWidget::UGachaScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 확률도 비용도 코드가 아니라 표가 안다 (DataDriven.md 1 · engines/unreal.md 코드 규칙 4).
	static ConstructorHelpers::FObjectFinder<UDataTable> RateTableAsset(
		TEXT("/Game/Game/Data/Gacha/DT_GachaRates.DT_GachaRates"));
	if (RateTableAsset.Succeeded())
	{
		GachaRateTable = RateTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> ConfigTableAsset(
		TEXT("/Game/Game/Data/Gacha/DT_GachaConfig.DT_GachaConfig"));
	if (ConfigTableAsset.Succeeded())
	{
		GachaConfigTable = ConfigTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> PoolTableAsset(
		TEXT("/Game/Game/Data/Gacha/DT_GachaPool.DT_GachaPool"));
	if (PoolTableAsset.Succeeded())
	{
		GachaPoolTable = PoolTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> FailureTableAsset(URanchSyncComponent::GetFailureTablePath());
	if (FailureTableAsset.Succeeded())
	{
		FailureTable = FailureTableAsset.Object;
	}
}

const FGachaRateRow* UGachaScreenWidget::FindRateRow(EGachaGrade Grade) const
{
	if (!IsValid(GachaRateTable))
	{
		return nullptr;
	}

	// 행 이름(`Gacha_Common`)이 아니라 행 안의 등급으로 찾는다 — 이름은 표를 다시 채울 때 바뀔 수 있지만
	// 등급은 타입이라 바뀌면 컴파일이 막는다 (DataDriven.md 4).
	for (const TPair<FName, uint8*>& Pair : GachaRateTable->GetRowMap())
	{
		const FGachaRateRow* const Row = reinterpret_cast<const FGachaRateRow*>(Pair.Value);
		if (Row != nullptr && Row->Grade == Grade)
		{
			return Row;
		}
	}

	return nullptr;
}

const FGachaCostRow* UGachaScreenWidget::FindCostRow() const
{
	if (!IsValid(GachaConfigTable))
	{
		return nullptr;
	}

	// 기계가 하나라 행도 하나다 (M7-01 의 2층 검사가 "행이 하나" 를 지킨다) — 그래서 첫 행을 집는다.
	// `SnarehavenGachaRateTest` 가 비용을 읽을 때와 같은 방식이다.
	const TArray<FName> RowNames = GachaConfigTable->GetRowNames();
	return RowNames.Num() > 0
		? GachaConfigTable->FindRow<FGachaCostRow>(RowNames[0], TEXT("GachaScreenWidget"))
		: nullptr;
}

FText UGachaScreenWidget::GetRateLineText(EGachaGrade Grade) const
{
	const FGachaRateRow* const Row = FindRateRow(Grade);
	if (Row == nullptr)
	{
		// 표에 그 등급이 통째로 없다 — 화면에 아무 줄도 안 띄우면 "그 등급은 안 나온다" 로 읽히므로
		// 빈 줄이 아니라 모른다는 표시를 남긴다. (그 상태 자체는 M7-01 의 2층 검사가 먼저 운다.)
		return LOCTEXT("GachaValueUnknown", "-");
	}

	if (Row->ProbabilityPct < 0.0f)
	{
		return FText::Format(LOCTEXT("GachaRateLineUnknown", "{0} {1}"),
			Row->DisplayName, LOCTEXT("GachaValueUnknown", "-"));
	}

	const FNumberFormattingOptions PercentFormat = MakePercentFormat();
	return FText::Format(LOCTEXT("GachaRateLine", "{0} {1}%"),
		Row->DisplayName, FText::AsNumber(Row->ProbabilityPct, &PercentFormat));
}

FText UGachaScreenWidget::GetSinglePullButtonText() const
{
	// 시트의 사는-것 규격 `가격 > 아이템` (M15-18) — 화살표가 가리키는 쪽이 내가 받는 것이다.
	const FGachaCostRow* const Cost = FindCostRow();
	return FText::Format(LOCTEXT("GachaSinglePullGem", "젬 {0}  >  1회 뽑기"),
		CostText(Cost != nullptr ? Cost->SinglePullGems : -1));
}

FText UGachaScreenWidget::GetCoinPullButtonText() const
{
	const FGachaCostRow* const Cost = FindCostRow();
	return FText::Format(LOCTEXT("GachaSinglePullCoin", "코인 {0}  >  1회 뽑기"),
		CostText(Cost != nullptr ? Cost->SinglePullCoins : -1));
}

FText UGachaScreenWidget::GetPoolLineText(const FGachaPoolRow& Row) const
{
	const FText Name = Row.DisplayName.IsEmpty() ? LOCTEXT("GachaValueUnknown", "-") : Row.DisplayName;
	if (Row.ProbabilityPct < 0.0f)
	{
		return FText::Format(LOCTEXT("GachaPoolLineUnknown", "{0} {1}"), Name, LOCTEXT("GachaValueUnknown", "-"));
	}
	const FNumberFormattingOptions PercentFormat = MakePercentFormat();
	return FText::Format(LOCTEXT("GachaPoolLine", "{0} {1}%"), Name, FText::AsNumber(Row.ProbabilityPct, &PercentFormat));
}

bool UGachaScreenWidget::ParsePullResponse(const TSharedPtr<FJsonObject>& Response,
	TArray<FGachaPullResult>& OutResults) const
{
	OutResults.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
	if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("results"), Results))
	{
		return false;
	}
	const UEnum* const GradeEnum = StaticEnum<EGachaGrade>();
	for (const TSharedPtr<FJsonValue>& Value : *Results)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Entry))
		{
			return false;
		}
		FString ItemId;
		FString GradeName;
		(*Entry)->TryGetStringField(TEXT("itemId"), ItemId);
		(*Entry)->TryGetStringField(TEXT("grade"), GradeName);

		// 서버는 `common`·`legendary` 소문자로 준다 — enum 이름과 대소문자만 무시하고 맞댄다.
		int32 GradeIndex = INDEX_NONE;
		for (int32 Index = 0; Index < GradeEnum->NumEnums() - 1; ++Index)
		{
			if (GradeEnum->GetNameStringByIndex(Index).Equals(GradeName, ESearchCase::IgnoreCase))
			{
				GradeIndex = Index;
			}
		}
		if (GradeIndex == INDEX_NONE)
		{
			return false;
		}

		FGachaPullResult& Out = OutResults.AddDefaulted_GetRef();
		Out.Grade = static_cast<EGachaGrade>(GradeEnum->GetValueByIndex(GradeIndex));
		Out.ItemID = FName(*ItemId);
		if (IsValid(GachaPoolTable))
		{
			for (const TPair<FName, uint8*>& Pair : GachaPoolTable->GetRowMap())
			{
				const FGachaPoolRow* const Row = reinterpret_cast<const FGachaPoolRow*>(Pair.Value);
				if (Row != nullptr && Row->ItemID == Out.ItemID)
				{
					Out.ItemName = Row->DisplayName;
				}
			}
		}
		(*Entry)->TryGetBoolField(TEXT("duplicate"), Out.bDuplicate);
		(*Entry)->TryGetNumberField(TEXT("coins"), Out.PrizeCoins);
		if (Out.bDuplicate)
		{
			(*Entry)->TryGetNumberField(TEXT("refund"), Out.RefundAmount);
			// 서버가 환급을 늘 코인(`SC`)으로 준다(`RollGacha` 의 AddUserVirtualCurrency) — 응답에 칸이 없어 계약을 옮긴다.
			Out.RefundCurrency = USnareBackendSubsystem::GetCoinCurrencyCode();
		}
	}
	return true;
}

bool UGachaScreenWidget::RequestPull(const FString& Currency, int32 RollCount)
{
	if (bPullInFlight || !IsValid(Backend))
	{
		return false;
	}
	bPullInFlight = true;
	RetryRequest = nullptr;
	SetStatus(FText::GetEmpty());

	// M8-05b: **요청을 보낸 것까지만** 센다 — 무엇이 나왔는지·재화가 얼마 빠졌는지는 `RollGacha` 가 정하고
	// 그 기록은 백엔드 원장이다 (`docs/ANALYTICS.md` 표 A·C · CLAUDE.md 8). 칸은 요청 모양 둘뿐이다.
	{
		TMap<FName, FString> Payload;
		Payload.Add(TEXT("pull_count"), FString::FromInt(RollCount));
		Payload.Add(TEXT("currency"), Currency);
		Backend->WriteGameEvent(TEXT("gacha_pull_requested"), Payload);
	}

	TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("currency"), Currency);
	Args->SetNumberField(TEXT("rollCount"), RollCount);
	TWeakObjectPtr<UGachaScreenWidget> WeakThis(this);
	Backend->CallServerFunction(TEXT("RollGacha"), Args,
		[WeakThis, Currency, RollCount](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UGachaScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPullInFlight = false;
			FString Code;
			FString Message;
			if (!SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message))
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Gacha] 거절 - %s %s"), *Code, *Message);
				// 서버에 닿지 못했을 때만 다시 — 「젬 모자람」 은 다시 보내도 같다.
				if (Code == TEXT("network"))
				{
					Self->RetryRequest = [WeakThis, Currency, RollCount]() { if (WeakThis.IsValid()) { WeakThis->RequestPull(Currency, RollCount); } };
				}
				Self->SetStatus(URanchSyncComponent::DescribeFailureCode(Self->FailureTable, TEXT("RollGacha"), Code, Message,
					Response, false));
				return;
			}
			TArray<FGachaPullResult> Results;
			if (Self->ParsePullResponse(Response, Results))
			{
				Self->ShowPullResults(Results);
				// 결과 줄을 건 이 자리에서만 — 답 전·거절·읽지 못한 답엔 소리가 없다 (DESIGN 2.2 「거점·상자 소리」 권위 줄).
				++Self->ConfirmSoundCount;
				Self->LastConfirmSoundCue = UCrateScreenWidget::GetConfirmCue();
				USnareSoundSubsystem::Play2D(Self, Self->LastConfirmSoundCue, UCrateScreenWidget::GetConfirmVolume());
			}
			else
			{
				Self->SetStatus(LOCTEXT("GachaResponseUnreadable", "결과를 읽지 못했다 - 인벤토리를 확인해 주세요"));
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Gacha] 받음 - 결과 %d줄"), Results.Num());
			// 재화가 이미 빠졌다 — 읽기에 실패해도 스냅샷은 다시 받는다.
			++Self->RefetchCount;
			if (IsValid(Self->Backend))
			{
				Self->Backend->FetchPlayerState();
			}
		});
	return true;
}

void UGachaScreenWidget::PressSinglePull()
{
	if (!USnareStoreSettings::AreGemsEnabled())
	{
		return;
	}
	RequestPull(USnareBackendSubsystem::GetGemCurrencyCode(), 1);
}

void UGachaScreenWidget::PressCoinPull()
{
	RequestPull(USnareBackendSubsystem::GetCoinCurrencyCode(), 1);
}

void UGachaScreenWidget::PressTenPull()
{
	if (!USnareStoreSettings::AreGemsEnabled())
	{
		return;
	}
	RequestPull(USnareBackendSubsystem::GetGemCurrencyCode(), TenPullRollCount);
}

void UGachaScreenWidget::SetStatus(const FText& Text)
{
	Status = Text;
	if (IsValid(StatusText))
	{
		StatusText->SetText(Status);
	}
	if (IsValid(RetryBox))
	{
		RetryBox->SetVisibility(RetryRequest && !bPullInFlight ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UGachaScreenWidget::HandleRetryClicked()
{
	TFunction<void()> Retry = MoveTemp(RetryRequest);
	RetryRequest = nullptr;
	SetStatus(FText::GetEmpty());
	if (Retry)
	{
		Retry();
	}
}

void UGachaScreenWidget::RefreshPoolList()
{
	if (!IsValid(PoolList) || WidgetTree == nullptr)
	{
		return;
	}
	PoolList->ClearChildren();
	// 한 줄 = 이름(왼쪽, 칸이 모자라면 줄바꿈) + 퍼센트(오른쪽 정렬, 늘 칸 안) — 한 글자 덩어리로 두면 오른쪽에서 잘린다.
	// 글자는 등급 줄과 같은 크기·같은 진한 갈색이다 (크림 바탕에 기본 흰 글자는 안 보인다).
	TArray<TPair<FText, FText>> Lines;
	if (IsValid(GachaPoolTable))
	{
		const FNumberFormattingOptions PercentFormat = MakePercentFormat();
		for (const TPair<FName, uint8*>& Pair : GachaPoolTable->GetRowMap())
		{
			if (const FGachaPoolRow* const Row = reinterpret_cast<const FGachaPoolRow*>(Pair.Value))
			{
				const FText Name = Row->DisplayName.IsEmpty() ? LOCTEXT("GachaValueUnknown", "-") : Row->DisplayName;
				const FText Pct = Row->ProbabilityPct < 0.0f
					? LOCTEXT("GachaValueUnknown", "-")
					: FText::Format(LOCTEXT("GachaPoolPct", "{0}%"), FText::AsNumber(Row->ProbabilityPct, &PercentFormat));
				Lines.Emplace(Name, Pct);
			}
		}
	}
	// 표가 없으면 빈 칸이 아니라 모른다는 `-` — 비우면 "아이템별 확률이 없다" 로 읽힌다.
	if (Lines.IsEmpty())
	{
		Lines.Emplace(LOCTEXT("GachaValueUnknown", "-"), FText::GetEmpty());
	}
	UTextBlock* const Header = MakeLabel(*WidgetTree, TEXT("PoolHeaderText"), LOCTEXT("GachaPoolHeader", "아이템별 확률"),
		RowNameFontSize, SnareUiStyle::UiTextOnButton());
	PoolList->AddChild(Header);
	for (const TPair<FText, FText>& Line : Lines)
	{
		const int32 Index = PoolList->GetChildrenCount();
		UHorizontalBox* const Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
			FName(*FString::Printf(TEXT("PoolRow_%d"), Index)));
		UTextBlock* const NameText = MakeLabel(*WidgetTree, FName(*FString::Printf(TEXT("PoolRowText_%d"), Index)),
			Line.Key, RowNameFontSize, SnareUiStyle::UiTextOnButton());
		NameText->SetAutoWrapText(true);
		if (UHorizontalBoxSlot* const NameSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(NameText)))
		{
			NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			NameSlot->SetVerticalAlignment(VAlign_Center);
			NameSlot->SetPadding(FMargin(0.f, 0.f, RowGap, 0.f));
		}
		UTextBlock* const PctText = MakeLabel(*WidgetTree, FName(*FString::Printf(TEXT("PoolRowPct_%d"), Index)),
			Line.Value, RowNameFontSize, SnareUiStyle::UiTextOnButton());
		PctText->SetJustification(ETextJustify::Right);
		if (UHorizontalBoxSlot* const PctSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(PctText)))
		{
			PctSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			PctSlot->SetHorizontalAlignment(HAlign_Right);
			PctSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (UVerticalBoxSlot* const RowSlot = Cast<UVerticalBoxSlot>(PoolList->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
		}
	}
	SnareUiStyle::ApplyUiStyle(*this);
}

FText UGachaScreenWidget::GetTenPullButtonText() const
{
	const FGachaCostRow* const Cost = FindCostRow();
	return FText::Format(LOCTEXT("GachaTenPull", "젬 {0}  >  10연차"),
		CostText(Cost != nullptr ? Cost->TenPullGems : -1));
}

FText UGachaScreenWidget::GetPityLineText() const
{
	// 천장 횟수는 표(`DT_GachaConfig`)에서만 온다 — 여기에 숫자를 적으면 화면이 표와 다른 보장을 광고한다
	// (DESIGN 10.4 오표시). 모르면 숫자 대신 `-` (Verification.md 7).
	const FGachaCostRow* const Cost = FindCostRow();
	return FText::Format(LOCTEXT("GachaPityLine", "천장 {0}회"),
		CostText(Cost != nullptr ? Cost->PityPullCount : -1));
}

FText UGachaScreenWidget::GetMachineLetter()
{
	// 편지체 한 문단 + 서명 (시트 오른쪽 칸). **원본 문구를 옮기지 않고 새로 썼고 숫자는 한 글자도 안 적는다** —
	// 천장 횟수는 위 `GetPityLineText` 가 표에서 읽는다.
	return LOCTEXT("GachaMachineLetter",
		"손잡이를 당기면 무엇이 나올지는 나도 모른다네.\n\n다만 오래 당긴 사람은 빈손으로 돌려보내지 않지. 이미 가진 것이 또 나오면 코인으로 돌려주겠네.\n\n- 올가미 기계");
}

FText UGachaScreenWidget::GetResultLineText(const FGachaPullResult& Result) const
{
	const FText Unknown = LOCTEXT("GachaValueUnknown", "-");

	// 등급 이름은 위 확률 줄과 **같은 표**에서 온다 — 여기에 `커먼` 을 적으면 표를 고칠 때
	// 확률 줄만 바뀌고 결과 줄은 옛 이름으로 남는다 (M7-02 가 지키는 문서 ↔ 표 ↔ 화면의 마지막 칸).
	const FGachaRateRow* const Row = FindRateRow(Result.Grade);
	const FText GradeName = Row != nullptr ? Row->DisplayName : Unknown;

	// 이름이 빈 응답은 "이름 없는 아이템" 이 아니라 **못 들은 것**이다 — 빈 줄로 두면 뽑았는데
	// 아무것도 안 준 것처럼 읽힌다.
	const FText ItemName = Result.ItemName.IsEmpty() ? Unknown : Result.ItemName;

	// 코인 경품은 「+N 코인」 으로 — 이름만 쓰면 아이템을 받은 것처럼 읽히고 잔액이 왜 늘었는지 모른다(M35-02).
	// 양은 서버가 준 `coins` 그대로다.
	if (Result.PrizeCoins > 0)
	{
		return FText::Format(LOCTEXT("GachaResultLineCoins", "{0} {1} (+{2} {3})"), GradeName, ItemName,
			FText::AsNumber(Result.PrizeCoins), RefundCurrencyName(USnareBackendSubsystem::GetCoinCurrencyCode()));
	}

	if (!Result.bDuplicate)
	{
		return FText::Format(LOCTEXT("GachaResultLine", "{0} {1}"), GradeName, ItemName);
	}

	// 중복은 **다르게 보여야 한다** — 같은 줄로 쓰면 이미 가진 것을 또 준 것처럼 보이고, 플레이어는
	// 재화가 왜 늘었는지 모른다 (DESIGN 10.2 "중복 처리 - 해당 등급 재화로 환급").
	// 환급 양도 재화 종류도 **백엔드가 정한 값**이다. 이 화면은 그것을 계산하지 않는다.
	return FText::Format(LOCTEXT("GachaResultLineDuplicate", "{0} {1} (중복 {2} {3} 환급)"),
		GradeName, ItemName, RefundCurrencyName(Result.RefundCurrency), CostText(Result.RefundAmount));
}

void UGachaScreenWidget::ShowPullResults(const TArray<FGachaPullResult>& Results)
{
	if (!IsValid(ResultList) || WidgetTree == nullptr)
	{
		return;
	}

	// 지난 뽑기 줄을 먼저 치운다 — 남겨 두고 쌓으면 10연차를 두 번 누른 화면이 20줄이 된다.
	ResultList->ClearChildren();

	for (const FGachaPullResult& Result : Results)
	{
		const FName RowName(*FString::Printf(TEXT("ResultRowText_%d"), ResultList->GetChildrenCount()));
		UTextBlock* const RowText =
			WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), RowName);
		RowText->SetText(GetResultLineText(Result));
		ResultList->AddChild(RowText);
	}

	// 이 줄들은 화면이 뜬 **뒤에** 생긴다 — `RebuildWidget` 때 한 번 칠한 글꼴·색이 안 닿는다
	// (`EnsureRateRows` 와 같은 함정, M5-05a·b).
	SnareUiStyle::ApplyUiStyle(*this);
}

void UGachaScreenWidget::SetSourceTables(UDataTable* RateTable, UDataTable* ConfigTable, UDataTable* PoolTable)
{
	GachaRateTable = RateTable;
	GachaConfigTable = ConfigTable;
	GachaPoolTable = PoolTable;
	RefreshDisplay();
}

void UGachaScreenWidget::RefreshDisplay()
{
	if (IsValid(TitleText))
	{
		// 제목 탭은 **무슨 화면인지**를, 오른쪽 명함 알약은 **누구/무엇인지**를 말한다 (시트 두 자리가 다르다).
		TitleText->SetText(LOCTEXT("GachaTitle2", "뽑기"));
	}
	if (IsValid(MachineNameText))
	{
		MachineNameText->SetText(LOCTEXT("GachaTitle", "올가미 기계"));
	}
	if (IsValid(MachinePityText))
	{
		MachinePityText->SetText(GetPityLineText());
	}
	if (IsValid(MachineLetterText))
	{
		MachineLetterText->SetText(GetMachineLetter());
	}

	const UEnum* const GradeEnum = StaticEnum<EGachaGrade>();
	if (IsValid(RateList) && GradeEnum != nullptr)
	{
		// 마지막 항목은 UHT 가 붙이는 `_MAX` 라 뺀다 (M7-01 의 2층 검사와 같은 셈법).
		const int32 GradeCount = GradeEnum->NumEnums() - 1;
		EnsureRateRows(GradeCount);

		for (int32 Index = 0; Index < GradeCount; ++Index)
		{
			const EGachaGrade Grade = static_cast<EGachaGrade>(GradeEnum->GetValueByIndex(Index));
			// WBP 가 옛 모양(글자 하나)으로 만들어 두었으면 그 글자를, 자리표시 줄이면 원판 옆 글자를 적는다.
			UWidget* const Child = RateList->GetChildAt(Index);
			UTextBlock* RowText = Cast<UTextBlock>(Child);
			UBorder* Disc = nullptr;
			if (RowText == nullptr)
			{
				if (const UPanelWidget* const Row = Cast<UPanelWidget>(Child))
				{
					RowText = Cast<UTextBlock>(Row->GetChildAt(1));
					const USizeBox* const DiscBox = Cast<USizeBox>(Row->GetChildAt(0));
					Disc = DiscBox != nullptr ? Cast<UBorder>(DiscBox->GetChildAt(0)) : nullptr;
				}
			}
			if (RowText != nullptr)
			{
				RowText->SetText(GetRateLineText(Grade));
			}
			if (Disc != nullptr)
			{
				Disc->SetBrush(FSlateRoundedBoxBrush(GradeDiscColor(Grade), DiscSize * 0.5f));
			}
		}
	}

	// 버튼 글자는 눌리든 안 눌리든 비용을 말한다 — 얼마인지 모르고 누르는 화면이 되면 안 된다.
	const FGachaCostRow* const Cost = FindCostRow();
	UpdatePullRow(SinglePullButton, FText::Format(LOCTEXT("GachaGemCost", "젬 {0}"),
		CostText(Cost != nullptr ? Cost->SinglePullGems : -1)), LOCTEXT("GachaSingleLabel", "1회 뽑기"));
	UpdatePullRow(CoinPullButton, FText::Format(LOCTEXT("GachaCoinCost", "코인 {0}"),
		CostText(Cost != nullptr ? Cost->SinglePullCoins : -1)), LOCTEXT("GachaSingleLabel", "1회 뽑기"));
	UpdatePullRow(TenPullButton, FText::Format(LOCTEXT("GachaGemCost", "젬 {0}"),
		CostText(Cost != nullptr ? Cost->TenPullGems : -1)), LOCTEXT("GachaTenLabel", "10연차"));
	// 젬이 꺼진 빌드(1.0)는 젬 줄 둘을 접는다 — 코인 줄만 남는다 (INBOX M94 ④).
	const ESlateVisibility GemRowVisibility = USnareStoreSettings::AreGemsEnabled() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (IsValid(SinglePullButton))
	{
		SinglePullButton->SetVisibility(GemRowVisibility);
	}
	if (IsValid(TenPullButton))
	{
		TenPullButton->SetVisibility(GemRowVisibility);
	}
	RefreshPoolList();
}

TSharedRef<SWidget> UGachaScreenWidget::RebuildWidget()
{
	if (NeedsPlaceholderTree())
	{
		BuildPlaceholderTree();
	}

	// 버튼은 `RollGacha`(M7-06) 를 부르기만 한다 — 굴리는 것은 서버다 (DESIGN 10.4·10.5 · CLAUDE.md 8).
	if (!IsValid(Backend))
	{
		const UGameInstance* const GameInstance = GetGameInstance();
		Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	}
	if (IsValid(SinglePullButton))
	{
		SinglePullButton->OnClicked.AddUniqueDynamic(this, &UGachaScreenWidget::PressSinglePull);
	}
	if (IsValid(CoinPullButton))
	{
		CoinPullButton->OnClicked.AddUniqueDynamic(this, &UGachaScreenWidget::PressCoinPull);
	}
	if (IsValid(TenPullButton))
	{
		TenPullButton->OnClicked.AddUniqueDynamic(this, &UGachaScreenWidget::PressTenPull);
	}
	if (IsValid(CloseButton))
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &UGachaScreenWidget::Close);
	}

	RefreshDisplay();

	// 글자·버튼 모양은 화면마다 따로 정하지 않는다 — 화면들이 같은 글꼴·색을 쓴다 (M5-05a·b).
	// `Super` **뒤**라야 한다 — Slate 위젯이 생긴 뒤에 칠해야 화면까지 바뀐다.
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	return Built;
}

UGachaScreenWidget* UGachaScreenWidget::OpenOnViewport(APlayerController* Owner, UWorld* World, USnareBackendSubsystem* InBackend)
{
	UGachaScreenWidget* const Screen = IsValid(Owner) ? CreateWidget<UGachaScreenWidget>(Owner, StaticClass())
		: IsValid(World) ? CreateWidget<UGachaScreenWidget>(World, StaticClass()) : nullptr;
	if (Screen == nullptr)
	{
		return nullptr;
	}
	Screen->BindToBackend(InBackend);
	// 전체 화면 판이라 HUD 보다 위 — 도감과 같은 Z.
	Screen->AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);
	return Screen;
}

bool UGachaScreenWidget::NeedsPlaceholderTree() const
{
	if (WidgetTree == nullptr)
	{
		return false;
	}

	if (WidgetTree->RootWidget == nullptr)
	{
		return true;
	}

	// 새로 만든 위젯 블루프린트의 빈 CanvasPanel 은 아직 디자인이 아니라 빈 도화지다.
	const UPanelWidget* const RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	return RootPanel != nullptr && RootPanel->GetChildrenCount() == 0;
}

UButton* UGachaScreenWidget::AddPullRow(UVerticalBox& Column, FName Name)
{
	UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);

	// 줄 안은 `가격(초록) · > · 이름` 세 조각 — 시트의 사는-것 규격이다. 글자 하나로 적으면 가격만
	// 초록으로 칠할 수가 없다(한 `UTextBlock` 은 색이 하나다).
	UHorizontalBox* const Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
		FName(Name.ToString() + TEXT("Row")));
	UTextBlock* const Price = MakeLabel(*WidgetTree, FName(Name.ToString() + TEXT("Price")), FText::GetEmpty(),
		PriceFontSize, SnareUiStyle::UiShopPriceGreen());
	SnareUiStyle::StylePriceOnWood(*Price);
	if (UHorizontalBoxSlot* const PriceSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Price)))
	{
		PriceSlot->SetVerticalAlignment(VAlign_Center);
	}
	UTextBlock* const Arrow = MakeLabel(*WidgetTree, FName(Name.ToString() + TEXT("Arrow")),
		LOCTEXT("GachaRowArrow", ">"), PriceFontSize, SnareUiStyle::UiTextOnButton());
	Arrow->SetJustification(ETextJustify::Center);
	USizeBox* const ArrowBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(Name.ToString() + TEXT("ArrowBox")));
	ArrowBox->SetWidthOverride(ArrowWidth);
	ArrowBox->AddChild(Arrow);
	if (UHorizontalBoxSlot* const ArrowSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(ArrowBox)))
	{
		ArrowSlot->SetVerticalAlignment(VAlign_Center);
	}
	UTextBlock* const Label = MakeLabel(*WidgetTree, FName(Name.ToString() + TEXT("Label")), FText::GetEmpty(),
		RowNameFontSize, SnareUiStyle::UiTextOnButton());
	if (UHorizontalBoxSlot* const LabelSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Label)))
	{
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}
	// 가격·화살표·문구는 내용만큼만 차지하고 묶음째 버튼 안 가운데에 둔다 (칸을 채우면 가격이 줄 밖으로 나간다).
	if (UButtonSlot* const ContentSlot = Cast<UButtonSlot>(Button->AddChild(Row)))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Center);
		ContentSlot->SetVerticalAlignment(VAlign_Center);
	}
	// 시트 「모든 패널 모서리 둥글게」 — 상점 거래 줄과 같은 둥근 줄 (M27-03).
	SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);

	USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(Name.ToString() + TEXT("Box")));
	Box->SetHeightOverride(RowHeight);
	Box->AddChild(Button);
	if (UVerticalBoxSlot* const RowSlot = Cast<UVerticalBoxSlot>(Column.AddChild(Box)))
	{
		RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
	}
	return Button;
}

void UGachaScreenWidget::UpdatePullRow(UButton* Button, const FText& Price, const FText& Label)
{
	if (!IsValid(Button))
	{
		return;
	}
	// WBP 가 옛 모양(글자 하나)으로 만들어 두었으면 한 줄 통째로 적는다 — 디자인이 붙어도 비용이 뜬다.
	if (UTextBlock* const Single = Cast<UTextBlock>(Button->GetChildAt(0)))
	{
		Single->SetText(FText::Format(LOCTEXT("GachaPullRow", "{0}  >  {1}"), Price, Label));
		return;
	}
	UPanelWidget* const Row = Cast<UPanelWidget>(Button->GetChildAt(0));
	if (Row == nullptr)
	{
		return;
	}
	if (UTextBlock* const PriceText = Cast<UTextBlock>(Row->GetChildAt(0)))
	{
		PriceText->SetText(Price);
	}
	if (UTextBlock* const LabelText = Cast<UTextBlock>(Row->GetChildAt(2)))
	{
		LabelText->SetText(Label);
	}
}

void UGachaScreenWidget::BuildPlaceholderTree()
{
	// WBP 가 만들어 둔 빈 캔버스가 있으면 그걸 쓴다 (루트를 갈아 끼우면 WBP 쪽 설정이 날아간다).
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("GachaScreenRoot"));
		WidgetTree->RootWidget = Root;
	}

	// 시트 판(1920×1080)을 세우고 통째로 키운다 — 상점·배낭과 같은 방식(M15-17·18).
	// 상점과 달리 **가로 한가운데**다 — 뽑기에는 옆에 같이 뜨는 배낭이 없어 비켜 줄 자리가 없다 (🟡).
	UCanvasPanel* const Sheet = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("GachaSheetRoot"));
	if (UCanvasPanelSlot* const SheetSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Sheet)))
	{
		SheetSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		SheetSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		SheetSlot->SetSize(SheetSize);
	}
	Sheet->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	Sheet->SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector, FVector2D(PhoneScale),
		FVector2D::ZeroVector, 0.f));

	const float PanelLeft = (SheetSize.X - PanelWidth) * 0.5f;
	const float PanelTop = (SheetSize.Y - PanelHeight) * 0.5f;

	UBorder* const Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("GachaPanel"));
	Panel->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiPanelCream(), PanelRadius));
	Panel->SetPadding(FMargin(PanelPadding));
	if (UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Sheet->AddChild(Panel)))
	{
		PanelSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		PanelSlot->SetAlignment(FVector2D(0.f, 0.f));
		PanelSlot->SetPosition(FVector2D(PanelLeft, PanelTop));
		PanelSlot->SetSize(FVector2D(PanelWidth, PanelHeight));
	}

	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GachaColumn"));
	Panel->SetContent(Column);
	UHorizontalBox* const Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GachaColumns"));
	if (UVerticalBoxSlot* const ColumnsSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Columns)))
	{
		ColumnsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	// ── 왼쪽 칸: 등급 확률 → 아이템별 확률 → 뽑기 줄 ──
	// 확률이 **누르는 자리보다 위**에 있어야 표시 의무를 지킨 화면이다 (DESIGN 10.4).
	UVerticalBox* const Left = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GachaRateColumn"));
	if (UHorizontalBoxSlot* const LeftSlot = Cast<UHorizontalBoxSlot>(Columns->AddChild(Left)))
	{
		LeftSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LeftSlot->SetPadding(FMargin(0.f, 0.f, ColumnGap, 0.f));
	}
	UScrollBox* const Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("GachaRateScroll"));
	if (UVerticalBoxSlot* const ScrollSlot = Cast<UVerticalBoxSlot>(Left->AddChild(Scroll)))
	{
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	RateList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RateList"));
	Scroll->AddChild(RateList);
	PoolList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PoolList"));
	Scroll->AddChild(PoolList);

	// 뽑기 줄 셋은 목록 **아래** 고정 — 스크롤 안에 넣으면 확률을 내려야 누를 것이 나온다.
	UVerticalBox* const PullRows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GachaPullRows"));
	Left->AddChild(PullRows);
	SinglePullButton = AddPullRow(*PullRows, TEXT("SinglePullButton"));
	CoinPullButton = AddPullRow(*PullRows, TEXT("CoinPullButton"));
	TenPullButton = AddPullRow(*PullRows, TEXT("TenPullButton"));

	// ── 세로 구분선 (시트 "회색 4px") — 팔레트에 회색이 없어 눌린 버튼 색을 쓴다 (🟡, 상점과 같다). ──
	ColumnDivider = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ColumnDivider"));
	ColumnDivider->SetBrushColor(SnareUiStyle::UiButtonPressed());
	USizeBox* const DividerBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ColumnDividerBox"));
	DividerBox->SetWidthOverride(DividerWidth);
	DividerBox->AddChild(ColumnDivider);
	Columns->AddChild(DividerBox);

	// ── 오른쪽 칸: 「닫기」 → 기계 명함 (이름 알약 → 천장 줄 → 편지체 본문) ──
	MachineCard = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MachineCard"));
	if (UHorizontalBoxSlot* const RightSlot = Cast<UHorizontalBoxSlot>(Columns->AddChild(MachineCard)))
	{
		RightSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RightSlot->SetPadding(FMargin(ColumnGap, 0.f, 0.f, 0.f));
	}
	// 「닫기」 는 판 오른쪽 위 — 상점 판(탭 줄 오른쪽 끝)과 같은 자리·같은 공용 모양 (M107-05 ⑥ 닫기 없어 갇힘).
	// 판이 통째로 PhoneScale 배 커지니 상자·글은 배율로 나눠 화면에서 공용 크기(높이 121·폭 242·글 28)가 되게 한다.
	UButton* NewClose = nullptr;
	USizeBox* const CloseBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("GachaCloseBox"), TEXT("CloseButton"),
		LOCTEXT("GachaClose", "닫기"), NewClose);
	CloseButton = NewClose;
	CloseBox->SetHeightOverride(RowHeight);
	CloseBox->SetMinDesiredWidth(RowHeight * 2.f);
	if (UTextBlock* const CloseText = Cast<UTextBlock>(NewClose->GetContent()))
	{
		SnareUiStyle::SetFontSize(*CloseText, (SnareUiStyle::MenuFontMin + 4.f) / PhoneScale);
	}
	if (UVerticalBoxSlot* const CloseSlot = Cast<UVerticalBoxSlot>(MachineCard->AddChild(CloseBox)))
	{
		CloseSlot->SetHorizontalAlignment(HAlign_Right);
		CloseSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
	}
	MachineNamePill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MachineNamePill"));
	MachineNamePill->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiAccentRed(), NamePillHeight * 0.5f));
	MachineNamePill->SetHorizontalAlignment(HAlign_Center);
	MachineNamePill->SetVerticalAlignment(VAlign_Center);
	MachineNameText = MakeLabel(*WidgetTree, TEXT("MachineNameText"), FText::GetEmpty(), NameFontSize, FLinearColor::White);
	MachineNamePill->SetContent(MachineNameText);
	USizeBox* const NamePillBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MachineNamePillBox"));
	NamePillBox->SetHeightOverride(NamePillHeight);
	NamePillBox->AddChild(MachineNamePill);
	MachineCard->AddChild(NamePillBox);

	MachinePityText = MakeLabel(*WidgetTree, TEXT("MachinePityText"), FText::GetEmpty(), LetterFontSize,
		SnareUiStyle::UiButtonPressed());
	MachinePityText->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* const PitySlot = Cast<UVerticalBoxSlot>(MachineCard->AddChild(MachinePityText)))
	{
		PitySlot->SetPadding(FMargin(0.f, RowGap, 0.f, RowGap));
	}
	MachineLetterText = MakeLabel(*WidgetTree, TEXT("MachineLetterText"), FText::GetEmpty(), LetterFontSize,
		SnareUiStyle::UiTextOnButton());
	MachineLetterText->SetAutoWrapText(true);
	MachineCard->AddChild(MachineLetterText);

	// ── 판 아래: 거절 사유 한 줄 + 결과 줄 ──
	StatusText = MakeLabel(*WidgetTree, TEXT("StatusText"), FText::GetEmpty(), RowNameFontSize,
		SnareUiStyle::UiTextOnButton());
	StatusText->SetJustification(ETextJustify::Center);
	StatusText->SetAutoWrapText(true);
	// 「다시 시도」 는 사유 줄 오른쪽 — 서버에 닿지 못했을 때만 보인다.
	UHorizontalBox* const StatusRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GachaStatusRow"));
	if (UHorizontalBoxSlot* const TextSlot = Cast<UHorizontalBoxSlot>(StatusRow->AddChild(StatusText)))
	{
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
	UButton* Retry = nullptr;
	RetryBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("GachaRetryBox"), TEXT("GachaRetryButton"),
		LOCTEXT("GachaRetry", "다시 시도"), Retry);
	Retry->OnClicked.AddDynamic(this, &UGachaScreenWidget::HandleRetryClicked);
	RetryBox->SetVisibility(ESlateVisibility::Collapsed);
	StatusRow->AddChild(RetryBox);
	Column->AddChild(StatusRow);

	// 결과 칸은 **비운 채로** 세운다 (M7-05) — 줄은 `ShowPullResults` 가 백엔드 응답을 받았을 때만
	// 생긴다. 미리 `커먼 ...` 같은 예시 줄을 넣으면 뽑지 않았는데 뽑은 화면이 된다.
	ResultList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ResultList"));
	Column->AddChild(ResultList);

	// 제목 탭 — 판 왼쪽 위에 걸쳐 위로 튀어나온다 (판 **뒤에** 넣어야 위에 그려진다).
	TitlePill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TitlePill"));
	TitlePill->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiIconMint(), PillHeight * 0.5f));
	TitlePill->SetHorizontalAlignment(HAlign_Center);
	TitlePill->SetVerticalAlignment(VAlign_Center);
	TitleText = MakeLabel(*WidgetTree, TEXT("TitleText"), FText::GetEmpty(), NameFontSize, SnareUiStyle::UiTextOnButton());
	TitleText->SetJustification(ETextJustify::Center);
	TitlePill->SetContent(TitleText);
	if (UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(Sheet->AddChild(TitlePill)))
	{
		PillSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		PillSlot->SetAlignment(FVector2D(0.f, 0.f));
		PillSlot->SetPosition(FVector2D(PanelLeft, PanelTop - PillRise));
		PillSlot->SetSize(FVector2D(PillWidth, PillHeight));
	}
}

void UGachaScreenWidget::EnsureRateRows(int32 GradeCount)
{
	if (!IsValid(RateList))
	{
		return;
	}

	// 줄 수는 `EGachaGrade` 가 정한다 — 여기에 4 를 적으면 등급 수가 두 군데에 적힌다 (DESIGN 10.2).
	// 한 줄은 시트대로 **등급 원판 + 글자**다 (M15-18) — 원판 색이 곧 등급이라 글자를 못 읽어도 순서가 보인다.
	while (RateList->GetChildrenCount() < GradeCount)
	{
		const int32 Index = RateList->GetChildrenCount();
		UHorizontalBox* const Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
			FName(*FString::Printf(TEXT("RateRow_%d"), Index)));
		UBorder* const Disc = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
			FName(*FString::Printf(TEXT("RateDisc_%d"), Index)));
		Disc->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, DiscSize * 0.5f));
		USizeBox* const DiscBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("RateDiscBox_%d"), Index)));
		DiscBox->SetWidthOverride(DiscSize);
		DiscBox->SetHeightOverride(DiscSize);
		DiscBox->AddChild(Disc);
		if (UHorizontalBoxSlot* const DiscSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(DiscBox)))
		{
			DiscSlot->SetVerticalAlignment(VAlign_Center);
		}
		UTextBlock* const RowText = MakeLabel(*WidgetTree, FName(*FString::Printf(TEXT("RateRowText_%d"), Index)),
			FText::GetEmpty(), RowNameFontSize, SnareUiStyle::UiTextOnButton());
		if (UHorizontalBoxSlot* const TextSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(RowText)))
		{
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetPadding(FMargin(RowGap, 0.f, 0.f, 0.f));
		}
		if (UVerticalBoxSlot* const RowSlot = Cast<UVerticalBoxSlot>(RateList->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
		}
	}

	while (RateList->GetChildrenCount() > GradeCount)
	{
		RateList->RemoveChildAt(RateList->GetChildrenCount() - 1);
	}

	// 이 줄들은 화면이 뜬 **뒤에** 생길 수 있다 — `RebuildWidget` 때 한 번 칠한 글꼴·색이 안 닿는다
	// (`URanchHudWidget::EnsureSlotRows` 와 같은 이유, M5-05a·b).
	SnareUiStyle::ApplyUiStyle(*this);
}

#undef LOCTEXT_NAMESPACE
