#include "UI/AttendanceScreenWidget.h"
#include "SnareCheckSwitch.h"

#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/ItemRow.h"
#include "World/SnareSweetPotato.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/SnareUiStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SnareSound.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 배치 (1080 높이 기준, DESIGN 11 「7일 출석」 화면 줄 🟡). 게임 규칙 숫자가 아니다.
	// 판은 의뢰 화면(`UQuestScreenWidget`)과 같은 위 가운데 크림 판, 칸 일곱이 한 줄.
	constexpr int32 SlotCount = 7;
	constexpr float PanelTop = 160.f;
	constexpr float PanelPadding = 24.f;
	constexpr float SlotWidth = 128.f;
	// 168 → 224 (M23-05): 날짜 줄과 보상 줄 사이에 원판 56 + 위아래 틈이 들어간다.
	constexpr float SlotHeight = 224.f;
	constexpr float SlotGap = 10.f;
	constexpr float SlotPadding = 8.f;
	constexpr float DayFontSize = 22.f;
	constexpr float RewardFontSize = 18.f;
	constexpr float StateFontSize = 16.f;
	constexpr float RowGap = 12.f;
	// 칸 보상 원판 (M23-05) — 상점 줄 원판(시트 52px)보다 한 칸 크게: 칸 폭 128 안쪽 112 의 절반 남짓.
	constexpr float RewardDiscSize = 56.f;
	/** 원판 안 그림 여백 — 배낭 칸·상점·제작 원판(52 에 6)과 같은 비율. */
	constexpr float DiscIconPadding = 6.f * RewardDiscSize / 52.f;
	constexpr float DiscGap = 6.f;
	/** 코인 보상 그림 — 배낭 「하단 바」 재화 아이콘과 같은 그림. */
	const TCHAR* const CoinIconPath = TEXT("/Game/Game/UI/Icons/Items/T_Icon_CashBundle.T_Icon_CashBundle");
	/** 10-16 80 → 121 (애플 44pt, DESIGN 2). */
	constexpr float ButtonHeight = SnareUiStyle::MenuTouchMin;
	// 여는 버튼은 오른쪽 아래 기둥(`SnareUiStyle::PlacePillarButton`) 왼쪽 열 맨 위 칸 — 목장에만 생기므로 빠져도 기둥 가운데가 비지 않는 자리.
	constexpr float OpenButtonRadius = 12.f;
	/** 여는 버튼 그림 — 진갈색 달력(`tools/unreal/import_ui_calendar.py`), 배낭 버튼처럼 글자 대신 아이콘. */
	const TCHAR* const OpenButtonIconPath = TEXT("/Game/Game/UI/Icons/T_UI_Calendar.T_UI_Calendar");
	// 제목 탭(M24-03) — 배낭·제작 판과 같은 진갈색 알약 규격. 판 왼쪽 위에 걸쳐 `MenuTitlePillRise` 만큼 위로.
	constexpr float TitlePillMinWidth = 130.f;
	constexpr float TitlePillHeight = 50.f;
	constexpr float TitlePillFontSize = 20.f;
	constexpr float TitlePillPaddingX = 24.f;
}

UAttendanceScreenWidget::UAttendanceScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(
		TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

const FItemRow* UAttendanceScreenWidget::FindItemRow(FName ItemID) const
{
	if (IsValid(ItemTable) && !ItemID.IsNone())
	{
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row != nullptr && Row->ItemID == ItemID)
			{
				return Row;
			}
		}
	}
	return nullptr;
}

FText UAttendanceScreenWidget::FindItemDisplayName(FName ItemID) const
{
	const FItemRow* const Row = FindItemRow(ItemID);
	if (Row != nullptr && !Row->DisplayName.IsEmpty())
	{
		return Row->DisplayName;
	}
	// 원 ID(`item.sprout_hat`)를 화면에 흘리지 않는다 (`captures/323_attendance_b.png`) — 서버 표와 아이템 표가
	// 어긋난 것이니 로그로 알리고 화면엔 `보상` 만.
	if (!WarnedUnknownIDs.Contains(ItemID))
	{
		WarnedUnknownIDs.Add(ItemID);
		UE_LOG(LogSnarehaven, Warning, TEXT("[Attendance] 아이템 표에 없는 보상 ID '%s' — 화면엔 '보상' 으로 보인다"), *ItemID.ToString());
	}
	return LOCTEXT("AttendanceUnknownReward", "보상");
}

UWidget* UAttendanceScreenWidget::MakeRewardDisc(const FAttendanceRewardView& Reward, int32 SlotNumber)
{
	// 배낭 칸·상점·제작 줄과 같은 짜임 — 등급 색 원판 위에 그림. 코인은 아이템 표 행이 없어 보통 등급 색 + 재화 그림.
	const FItemRow* const Row = FindItemRow(Reward.ItemID);
	UOverlay* const Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),
		FName(*FString::Printf(TEXT("AttendanceSlotDiscStack_%d"), SlotNumber)));
	UBorder* const Disc = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
		FName(*FString::Printf(TEXT("AttendanceSlotDisc_%d"), SlotNumber)));
	Disc->SetBrush(FSlateRoundedBoxBrush(
		SnareUiStyle::ItemGradeColor(Row != nullptr ? Row->Grade : EItemGrade::Common), RewardDiscSize * 0.5f));
	if (UOverlaySlot* const DiscSlot = Stack->AddChildToOverlay(Disc))
	{
		// 빈 Border 는 원하는 크기가 0 이다 — 칸을 꽉 채워야 원판이 보인다 (배낭 칸과 같은 이유).
		DiscSlot->SetHorizontalAlignment(HAlign_Fill);
		DiscSlot->SetVerticalAlignment(VAlign_Fill);
	}
	UImage* Icon = nullptr;
	if (Reward.ItemID.IsNone() && bKnown)
	{
		Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(*FString::Printf(TEXT("AttendanceSlotIcon_%d"), SlotNumber)));
		Icon->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr, CoinIconPath));
	}
	else if (Row != nullptr && !Row->IconRef.IsNull())
	{
		Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(*FString::Printf(TEXT("AttendanceSlotIcon_%d"), SlotNumber)));
		Icon->SetBrushFromSoftTexture(Row->IconRef);
	}
	if (Icon != nullptr)
	{
		if (UOverlaySlot* const IconSlot = Stack->AddChildToOverlay(Icon))
		{
			IconSlot->SetPadding(FMargin(DiscIconPadding));
			IconSlot->SetHorizontalAlignment(HAlign_Fill);
			IconSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
	USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("AttendanceSlotDiscBox_%d"), SlotNumber)));
	Box->SetWidthOverride(RewardDiscSize);
	Box->SetHeightOverride(RewardDiscSize);
	Box->AddChild(Stack);
	return Box;
}

FText UAttendanceScreenWidget::GetRewardText(const FAttendanceRewardView& Reward) const
{
	if (!Reward.ItemID.IsNone())
	{
		return Reward.Count > 1
			? FText::Format(LOCTEXT("AttendanceItemMany", "{0} x{1}"), FindItemDisplayName(Reward.ItemID), FText::AsNumber(Reward.Count))
			: FindItemDisplayName(Reward.ItemID);
	}
	return FText::Format(LOCTEXT("AttendanceCoins", "코인 {0}"), FText::AsNumber(Reward.Coins));
}

void UAttendanceScreenWidget::BindToBackend(USnareBackendSubsystem* Backend)
{
	if (WatchedBackend == Backend)
	{
		return;
	}
	WatchedBackend = Backend;
	RefreshDisplay();
}

void UAttendanceScreenWidget::HandleOpenClicked() { SetOpen(true); }
void UAttendanceScreenWidget::HandleCloseClicked() { SetOpen(false); }
void UAttendanceScreenWidget::HandleClaimClicked() { RequestClaim(); }

void UAttendanceScreenWidget::SetOpen(bool bInOpen)
{
	bOpen = bInOpen;
	if (bOpen)
	{
		Message = FText::GetEmpty();
		RequestAttendance();
	}
	RefreshDisplay();
}

bool UAttendanceScreenWidget::ParseReward(const TSharedPtr<FJsonObject>& Object, FAttendanceRewardView& OutReward)
{
	if (!Object.IsValid())
	{
		return false;
	}
	OutReward = FAttendanceRewardView();
	FString ItemID;
	if (Object->TryGetStringField(TEXT("itemId"), ItemID) && !ItemID.IsEmpty())
	{
		OutReward.ItemID = FName(ItemID);
		Object->TryGetNumberField(TEXT("count"), OutReward.Count);
		return true;
	}
	return Object->TryGetNumberField(TEXT("coins"), OutReward.Coins);
}

bool UAttendanceScreenWidget::ApplyAttendanceReply(const TSharedPtr<FJsonObject>& Response)
{
	const TArray<TSharedPtr<FJsonValue>>* RewardValues = nullptr;
	int32 InNextSlot = 0;
	if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("rewards"), RewardValues)
		|| !Response->TryGetNumberField(TEXT("nextSlot"), InNextSlot) || InNextSlot < 1 || InNextSlot > SlotCount)
	{
		return false;
	}
	TArray<FAttendanceRewardView> InRewards;
	for (const TSharedPtr<FJsonValue>& Value : *RewardValues)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FAttendanceRewardView Reward;
		if (Value.IsValid() && Value->TryGetObject(Object) && ParseReward(*Object, Reward))
		{
			InRewards.Add(Reward);
		}
	}
	if (InRewards.Num() != SlotCount)
	{
		return false;
	}
	Rewards = MoveTemp(InRewards);
	NextSlot = InNextSlot;
	bClaimedToday = false;
	Response->TryGetBoolField(TEXT("claimedToday"), bClaimedToday);
	const TSharedPtr<FJsonObject>* NextObject = nullptr;
	if (!Response->TryGetObjectField(TEXT("next"), NextObject) || !ParseReward(*NextObject, NextReward))
	{
		NextReward = Rewards[NextSlot - 1];
	}
	bKnown = true;
	RefreshDisplay();
	return true;
}

EAttendanceSlotState UAttendanceScreenWidget::GetSlotState(int32 SlotNumber) const
{
	if (!bKnown || SlotNumber < 1 || SlotNumber > SlotCount)
	{
		return EAttendanceSlotState::Later;
	}
	// 오늘 7일째를 받으면 서버 `nextSlot` 은 이미 1로 돌았다 — 그날 하루는 일곱 칸을 다 받은 것으로 그린다.
	if (bClaimedToday && NextSlot == 1)
	{
		return EAttendanceSlotState::Claimed;
	}
	if (SlotNumber < NextSlot)
	{
		return EAttendanceSlotState::Claimed;
	}
	return SlotNumber == NextSlot && !bClaimedToday ? EAttendanceSlotState::Today : EAttendanceSlotState::Later;
}

FAttendanceRewardView UAttendanceScreenWidget::GetSlotReward(int32 SlotNumber) const
{
	if (!bKnown || SlotNumber < 1 || SlotNumber > Rewards.Num())
	{
		return FAttendanceRewardView();
	}
	// 다음에 받을 칸만 서버가 고친 값(모자를 이미 받았으면 7일째 = 코인)이 따로 온다.
	return SlotNumber == NextSlot && GetSlotState(SlotNumber) != EAttendanceSlotState::Claimed ? NextReward : Rewards[SlotNumber - 1];
}

bool UAttendanceScreenWidget::RequestAttendance()
{
	if (bPending || !IsValid(WatchedBackend))
	{
		return false;
	}
	bPending = true;
	const TWeakObjectPtr<UAttendanceScreenWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("GetAttendance"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UAttendanceScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			Self->bListFailed = !Failure.IsEmpty();
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Attendance] 달력 거절 — %s"), *Failure.ToString());
				Self->Message = Failure;
			}
			else if (!Self->ApplyAttendanceReply(Response))
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("[Attendance] 달력 답을 읽지 못했다"));
			}
			Self->RefreshDisplay();
		});
	RefreshDisplay();
	return true;
}

bool UAttendanceScreenWidget::RequestClaim()
{
	if (!CanClaim() || !IsValid(WatchedBackend))
	{
		return false;
	}
	bPending = true;
	Message = FText::GetEmpty();
	RefreshDisplay();

	const TWeakObjectPtr<UAttendanceScreenWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("ClaimAttendance"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UAttendanceScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Attendance] 받기 거절 — %s"), *Failure.ToString());
				Self->Message = Failure;
				Self->RequestAttendance();   // 이미 받았으면 그 사실을 달력에 다시 그린다
				return;
			}
			int32 SlotNumber = 0;
			FAttendanceRewardView Reward;
			const TSharedPtr<FJsonObject>* RewardObject = nullptr;
			if (Response.IsValid())
			{
				Response->TryGetNumberField(TEXT("slot"), SlotNumber);
				if (Response->TryGetObjectField(TEXT("reward"), RewardObject))
				{
					ParseReward(*RewardObject, Reward);
				}
			}
			USnareSoundSubsystem::Play2D(Self, TEXT("Coins"), 0.8f);   // 서버가 보상을 확정한 뒤 (DESIGN 2.2 소리)
			Self->Message = FText::Format(LOCTEXT("AttendanceClaimed", "{0}일째 출석 — {1} 받았다"),
				FText::AsNumber(SlotNumber), Self->GetRewardText(Reward));
			UE_LOG(LogSnarehaven, Log, TEXT("[Attendance] %d일째 받음 — %s"), SlotNumber, *Self->GetRewardText(Reward).ToString());
			// 코인·배낭·칸은 서버가 바꿨다 — 손으로 고치지 않고 다시 받는다 (CLAUDE.md 8).
			if (IsValid(Self->WatchedBackend))
			{
				Self->WatchedBackend->FetchPlayerState();
			}
			Self->RequestAttendance();
		});
	return true;
}

void UAttendanceScreenWidget::RefreshDisplay()
{
	if (IsValid(OpenButton))
	{
		OpenButton->SetVisibility(bOpen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(Panel))
	{
		Panel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(TitlePillBox))
	{
		// 판 밖에 걸친 알약도 같이 접는다 — 닫힌 판의 제목만 HUD 위에 남지 않게(M23-01 상점과 같은 꼴).
		TitlePillBox->SetVisibility(bOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IsValid(MessageText))
	{
		MessageText->SetText(!Message.IsEmpty() ? Message
			: bPending ? LOCTEXT("AttendancePending", "확인 중...")
			: !bKnown ? LOCTEXT("AttendanceUnknown", "서버 연결 안 됨")
			: bClaimedToday ? LOCTEXT("AttendanceDoneToday", "오늘 출석은 받았다. 내일 또 와!")
			: LOCTEXT("AttendanceHint", "하루 한 번 받는다. 빠진 날이 있어도 처음으로 안 돌아간다."));
	}
	if (IsValid(ClaimButton))
	{
		ClaimButton->SetIsEnabled(CanClaim());
	}
	if (IsValid(RetryBox))
	{
		RetryBox->SetVisibility(bListFailed && !bPending ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(ClaimButtonText))
	{
		ClaimButtonText->SetText(bKnown && bClaimedToday ? LOCTEXT("AttendanceClaimDone", "받음") : LOCTEXT("AttendanceClaim", "받기"));
	}
	if (!IsValid(SlotRow) || WidgetTree == nullptr)
	{
		return;
	}

	SlotRow->ClearChildren();
	for (int32 SlotNumber = 1; SlotNumber <= SlotCount; ++SlotNumber)
	{
		const EAttendanceSlotState State = GetSlotState(SlotNumber);
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("AttendanceSlotBox_%d"), SlotNumber)));
		Box->SetWidthOverride(SlotWidth);
		Box->SetHeightOverride(SlotHeight);
		UBorder* const Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
			FName(*FString::Printf(TEXT("AttendanceSlot_%d"), SlotNumber)));
		Card->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, SnareUiStyle::HudCornerRadius));
		// 받은 칸 = 눌림 색 · 오늘 칸 = 강조 노랑 · 남은 칸 = 기본 아이보리 (팔레트 2.1, 새 색 0개).
		Card->SetBrushColor(State == EAttendanceSlotState::Claimed ? SnareUiStyle::UiButtonPressed()
			: State == EAttendanceSlotState::Today ? SnareUiStyle::UiButtonHighlight() : SnareUiStyle::UiButtonBase());
		Card->SetPadding(FMargin(SlotPadding));
		Box->AddChild(Card);

		UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),
			FName(*FString::Printf(TEXT("AttendanceSlotColumn_%d"), SlotNumber)));
		Card->SetContent(Column);
		auto AddLine = [this, Column, SlotNumber](const TCHAR* Kind, const FText& Text, float FontSize, bool bFill)
		{
			UTextBlock* const Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
				FName(*FString::Printf(TEXT("AttendanceSlot%s_%d"), Kind, SlotNumber)));
			Line->SetText(Text);
			Line->SetJustification(ETextJustify::Center);
			// 자동 줄바꿈은 첫 프레임 폭을 늦게 재 `2` / `일` 로 갈라졌다(3층 `captures/323_attendance.png`) —
			// 보상 줄만 칸 안쪽 폭에서 고정으로 꺾는다.
			Line->SetWrapTextAt(bFill ? SlotWidth - 2.f * SlotPadding : 0.f);
			Line->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
			SnareUiStyle::SetFontSize(*Line, FontSize);
			if (UVerticalBoxSlot* const LineSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Line)))
			{
				LineSlot->SetHorizontalAlignment(HAlign_Center);
				LineSlot->SetVerticalAlignment(VAlign_Center);
				if (bFill)
				{
					LineSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				}
			}
		};
		AddLine(TEXT("Day"), FText::Format(LOCTEXT("AttendanceDay", "{0}일"), FText::AsNumber(SlotNumber)), DayFontSize, false);
		// 답 전엔 빈 원판만 (그림은 서버가 칸을 알려 준 뒤).
		if (UVerticalBoxSlot* const DiscSlot = Cast<UVerticalBoxSlot>(Column->AddChild(MakeRewardDisc(GetSlotReward(SlotNumber), SlotNumber))))
		{
			DiscSlot->SetHorizontalAlignment(HAlign_Center);
			DiscSlot->SetPadding(FMargin(0.f, DiscGap, 0.f, DiscGap));
		}
		AddLine(TEXT("Reward"), bKnown ? GetRewardText(GetSlotReward(SlotNumber)) : FText::FromString(TEXT("-")), RewardFontSize, true);
		AddLine(TEXT("State"), State == EAttendanceSlotState::Claimed ? LOCTEXT("AttendanceSlotClaimed", "받음")
			: State == EAttendanceSlotState::Today ? LOCTEXT("AttendanceSlotToday", "오늘") : FText::GetEmpty(),
			StateFontSize, false);

		if (UHorizontalBoxSlot* const RowSlot = Cast<UHorizontalBoxSlot>(SlotRow->AddChild(Box)))
		{
			RowSlot->SetPadding(FMargin(SlotNumber > 1 ? SlotGap : 0.f, 0.f, 0.f, 0.f));
		}
	}
	SnareUiStyle::ApplyUiStyle(*this);
}

TSharedRef<SWidget> UAttendanceScreenWidget::RebuildWidget()
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

void UAttendanceScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (USnareBackendSubsystem* const Backend = BackendToRebind.Get())
	{
		BindToBackend(Backend);
	}
#if !UE_BUILD_SHIPPING
	// 3층 캡처용 자리표시 (`-SnareAttendanceDemo`) — 오프라인 캡처엔 서버 답이 없어 칸이 전부 `-` 다.
	// 화면 글자만 바뀐다(코인·배낭은 그대로, CLAUDE.md 8). 칸 값은 DESIGN 11 「출석 보상」 표를 옮긴 그림일 뿐이다.
	if (SnareCheckSwitch::Param(TEXT("SnareAttendanceDemo")) && !bKnown)
	{
		TSharedPtr<FJsonObject> Demo;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"nextSlot\":4,\"claimedToday\":false,"
			"\"next\":{\"itemId\":\"item.seed_carrot\",\"count\":3},\"rewards\":[{\"coins\":50},{\"coins\":50},"
			"{\"coins\":100},{\"itemId\":\"item.seed_carrot\",\"count\":3},{\"coins\":100},{\"coins\":150},"
			"{\"itemId\":\"item.sprout_hat\",\"count\":1}]}")), Demo);
		bOpen = true;
		ApplyAttendanceReply(Demo);
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 출석 달력 %s (-SnareAttendanceDemo)"), bKnown ? TEXT("펼침") : TEXT("읽기 실패"));
	}
#endif
}

void UAttendanceScreenWidget::NativeDestruct()
{
	BackendToRebind = WatchedBackend;
	BindToBackend(nullptr);
	Super::NativeDestruct();
}

void UAttendanceScreenWidget::BuildTree()
{
	UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("AttendanceRoot"));
	WidgetTree->RootWidget = Root;
	// 목장 HUD 위를 덮는 온 화면 캔버스 — 빈 곳 클릭은 아래 HUD·월드로 흘려보낸다.
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	auto MakeButton = [this](FName Name, const FText& Label, UTextBlock** OutText = nullptr) -> UButton*
	{
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*(Name.ToString() + TEXT("Text"))));
		Text->SetText(Label);
		Text->SetJustification(ETextJustify::Center);
		Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		Button->AddChild(Text);
		if (OutText != nullptr)
		{
			*OutText = Text;
		}
		return Button;
	};

	// 감독 보정 10-06 02:45: 옆 배낭 버튼과 같은 크림 네모 아이콘 버튼 — 글자 「출석」 대신 달력 그림, 아이콘이 네모를 꽉 채운다.
	OpenButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("AttendanceOpenButton"));
	SnareUiStyle::MakeRoundButton(*OpenButton, OpenButtonRadius);
	OpenButton->SetBackgroundColor(SnareUiStyle::UiPanelCream());
	OpenButton->SetToolTipText(LOCTEXT("AttendanceOpen", "출석"));
	OpenButton->OnClicked.AddDynamic(this, &UAttendanceScreenWidget::HandleOpenClicked);
	// 기둥 (열 0, 행 2) — 달력 아이콘 아래 라벨 「출석」.
	SnareUiStyle::PlacePillarButton(*WidgetTree, *Root, *OpenButton, LoadObject<UTexture2D>(nullptr, OpenButtonIconPath),
		LOCTEXT("AttendanceOpen", "출석"), TEXT("AttendanceOpenLabel"), 1, 2);

	// 판은 자동 크기라 폭을 미리 모른다 — 판과 제목 알약을 한 겹(Overlay)에 두어 알약이 늘 판 왼쪽 위에 붙게 한다.
	// 겹은 알약이 위로 나온 만큼 올려 판 위쪽 변은 `PanelTop` 그대로.
	const float PillRise = SnareUiStyle::MenuTitlePillRise;
	const float PillOverhang = TitlePillHeight - PillRise;   // 알약이 판 안으로 내려온 몫
	UOverlay* const Frame = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("AttendanceFrame"));
	Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UCanvasPanelSlot* const FrameSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Frame)))
	{
		FrameSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		FrameSlot->SetAlignment(FVector2D(0.5f, 0.f));
		FrameSlot->SetPosition(FVector2D(0.f, PanelTop - PillRise));
		FrameSlot->SetAutoSize(true);
	}

	Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AttendancePanel"));
	Panel->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, SnareUiStyle::HudCornerRadius));
	Panel->SetBrushColor(SnareUiStyle::UiPanelCream());
	// 위 여백에 알약이 판 안으로 내려온 몫을 더한다 — 안내 줄이 알약에 안 가리게.
	Panel->SetPadding(FMargin(PanelPadding, PanelPadding + PillOverhang, PanelPadding, PanelPadding));
	if (UOverlaySlot* const PanelSlot = Cast<UOverlaySlot>(Frame->AddChild(Panel)))
	{
		PanelSlot->SetPadding(FMargin(0.f, PillRise, 0.f, 0.f));
	}
	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AttendanceColumn"));
	Panel->SetContent(Column);

	// 제목 탭 — 판 왼쪽 위 모서리에 걸쳐 위로 튀어나온다(판 **뒤에** 넣어야 위에 그려진다). 배낭·제작 판과 같은 진갈색·흰 글자.
	UBorder* const TitlePill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AttendanceTitlePill"));
	TitlePill->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiTitleTab(), TitlePillHeight * 0.5f));
	TitlePill->SetHorizontalAlignment(HAlign_Center);
	TitlePill->SetVerticalAlignment(VAlign_Center);
	TitlePill->SetPadding(FMargin(TitlePillPaddingX, 0.f));
	UTextBlock* const Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AttendanceTitle"));
	Title->SetText(LOCTEXT("AttendanceTitle", "7일 출석"));
	Title->SetJustification(ETextJustify::Center);
	Title->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	SnareUiStyle::SetFontSize(*Title, TitlePillFontSize);
	TitlePill->SetContent(Title);
	TitlePillBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("AttendanceTitlePillBox"));
	TitlePillBox->SetMinDesiredWidth(TitlePillMinWidth);
	TitlePillBox->SetHeightOverride(TitlePillHeight);
	TitlePillBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	TitlePillBox->AddChild(TitlePill);
	if (UOverlaySlot* const PillSlot = Cast<UOverlaySlot>(Frame->AddChild(TitlePillBox)))
	{
		PillSlot->SetHorizontalAlignment(HAlign_Left);
		PillSlot->SetVerticalAlignment(VAlign_Top);
	}

	MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AttendanceMessageText"));
	MessageText->SetJustification(ETextJustify::Center);
	MessageText->SetAutoWrapText(true);
	MessageText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	if (UVerticalBoxSlot* const MessageSlot = Cast<UVerticalBoxSlot>(Column->AddChild(MessageText)))
	{
		MessageSlot->SetPadding(FMargin(0.f, RowGap * 0.5f, 0.f, RowGap));
	}

	SlotRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("AttendanceSlotRow"));
	Column->AddChild(SlotRow);

	// 고구마 장수 안내 (DESIGN 8.17 「달력 안내」) — 달력은 7일 연속 칸이라 요일 칸이 없어 칸 줄 아래 한 줄로 알린다.
	UTextBlock* const PotatoHint = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AttendancePotatoHint"));
	PotatoHint->SetText(SnareSweetPotato::CalendarHintText());
	PotatoHint->SetJustification(ETextJustify::Center);
	PotatoHint->SetAutoWrapText(true);
	PotatoHint->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	if (UVerticalBoxSlot* const HintSlot = Cast<UVerticalBoxSlot>(Column->AddChild(PotatoHint)))
	{
		HintSlot->SetPadding(FMargin(0.f, RowGap, 0.f, RowGap * 0.5f));
	}

	UHorizontalBox* const ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("AttendanceButtonRow"));
	// 「다시 시도」 는 받기 왼쪽 — 달력을 못 받았을 때만 보인다. 판 높이는 그대로.
	UButton* Retry = nullptr;
	RetryBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("AttendanceRetryBox"), TEXT("AttendanceRetryButton"),
		LOCTEXT("AttendanceRetry", "다시 시도"), Retry);
	Retry->OnClicked.AddDynamic(this, &UAttendanceScreenWidget::HandleRetryClicked);
	RetryBox->SetVisibility(ESlateVisibility::Collapsed);
	if (UHorizontalBoxSlot* const RetrySlot = Cast<UHorizontalBoxSlot>(ButtonRow->AddChild(RetryBox)))
	{
		RetrySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RetrySlot->SetPadding(FMargin(0.f, 0.f, SlotGap, 0.f));
	}
	UTextBlock* ClaimText = nullptr;
	ClaimButton = MakeButton(TEXT("AttendanceClaimButton"), LOCTEXT("AttendanceClaim", "받기"), &ClaimText);
	ClaimButtonText = ClaimText;
	ClaimButton->OnClicked.AddDynamic(this, &UAttendanceScreenWidget::HandleClaimClicked);
	CloseButton = MakeButton(TEXT("AttendanceCloseButton"), LOCTEXT("AttendanceClose", "닫기"));
	CloseButton->OnClicked.AddDynamic(this, &UAttendanceScreenWidget::HandleCloseClicked);
	for (UButton* const Button : {ClaimButton.Get(), CloseButton.Get()})
	{
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*(Button->GetName() + TEXT("Box"))));
		Box->SetHeightOverride(ButtonHeight);
		Box->AddChild(Button);
		if (UHorizontalBoxSlot* const ButtonSlot = Cast<UHorizontalBoxSlot>(ButtonRow->AddChild(Box)))
		{
			ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			ButtonSlot->SetPadding(FMargin(Button == CloseButton ? SlotGap : 0.f, 0.f, 0.f, 0.f));
		}
	}
	if (UVerticalBoxSlot* const ButtonRowSlot = Cast<UVerticalBoxSlot>(Column->AddChild(ButtonRow)))
	{
		ButtonRowSlot->SetPadding(FMargin(0.f, RowGap, 0.f, 0.f));
	}
}

void UAttendanceScreenWidget::HandleRetryClicked()
{
	Message = FText::GetEmpty();
	bListFailed = false;
	RequestAttendance();
	RefreshDisplay();
}

#undef LOCTEXT_NAMESPACE
