#include "UI/SnareZoneTravelHud.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "World/ZonePortalActor.h"
#include "World/ZoneTravelComponent.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace SnareZoneTravelHud
{
bool IsButtonVisible(const UZoneTravelComponent* Travel)
{
	// HUD 가 사거리·표를 따로 보지 않는다 — 판정은 컴포넌트 한 곳이다 (포획 버튼과 같은 규칙).
	return IsValid(Travel) && Travel->CanTravel();
}

FText FormatButtonText(const UZoneTravelComponent* Travel)
{
	const AZonePortalActor* const Portal = IsValid(Travel) ? Travel->GetPortalTarget() : nullptr;
	if (!IsValid(Portal) || !Portal->CanTravel())
	{
		return FText::GetEmpty();
	}

	// 갈 곳 이름을 여기서 짓지 않는다 — 표의 `DisplayName` 을 그대로 쓴다 (DataDriven.md 1).
	// 조사를 붙이지 않는 이유: 존 이름이 바뀌면 `으로`/`로` 가 갈리는데 그 판단은 표에 없다.
	const FText Base = FText::Format(LOCTEXT("ZoneTravelButton", "{0} 이동"), Portal->GetTargetDisplayName());

	// 선착장 요금 (DESIGN 8.14) — 답을 기다리는 중이면 그렇다고, 거절됐으면 서버가 준 사유를 아래 줄에 적는다.
	// 요금 숫자는 여기 없다 — 모자란 수는 서버 답의 `need`/`have` 가 채운다.
	if (Travel->IsFerryPending())
	{
		return FText::Format(LOCTEXT("ZoneTravelPending", "{0}\n요금 확인 중..."), Base);
	}
	if (!Travel->GetFerryMessage().IsEmpty())
	{
		return FText::Format(LOCTEXT("ZoneTravelRefused", "{0}\n{1}"), Base, Travel->GetFerryMessage());
	}
	return Base;
}

UButton* CreateButton(UWidgetTree& Tree, UTextBlock*& OutText)
{
	// 이름도 여기서 정한다 — 두 화면의 버튼이 같은 이름이라야 `-SnareTouch=` 처럼 이름으로 버튼을
	// 찾는 검사가 화면마다 다른 이름을 외우지 않는다.
	UButton* const Button =
		Tree.ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ZoneTravelButton"));
	// 접근성 이름(10-16) — 글이 비어 있을 때도 VoiceOver 가 읽게(`ButtonAccessibleName` 툴팁 단계).
	Button->SetToolTipText(LOCTEXT("ZoneTravelA11y", "존 이동"));

	OutText = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ZoneTravelButtonText"));
	OutText->SetJustification(ETextJustify::Center);
	// 거점 화면은 칸을 자동 크기로 두므로 글자 한 줄 높이만큼만 서던 것을(56 아래) 상자로 받친다 (M38-02).
	USizeBox* const Box = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ZoneTravelButtonBox"));
	Box->SetMinDesiredHeight(MinButtonHeight);
	if (USizeBoxSlot* const TextSlot = Cast<USizeBoxSlot>(Box->AddChild(OutText)))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
	Button->AddChild(Box);
	return Button;
}
}

#undef LOCTEXT_NAMESPACE
