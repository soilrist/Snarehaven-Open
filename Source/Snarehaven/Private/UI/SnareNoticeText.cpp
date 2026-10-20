#include "UI/SnareNoticeText.h"

#include "Engine/World.h"
#include "TimerManager.h"

void USnareNoticeText::ShowFor(const FText& Message, float Seconds)
{
	SetText(Message);
	SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	SyncCompanion();
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(HideTimer, this, &USnareNoticeText::HideNotice, Seconds, false);
	}
}

void USnareNoticeText::HideNotice()
{
	SetText(FText::GetEmpty());
	SetVisibility(ESlateVisibility::Collapsed);
	SyncCompanion();
}

void USnareNoticeText::SyncCompanion()
{
	if (IsValid(Companion))
	{
		Companion->SetVisibility(GetVisibility());
	}
}

void USnareNoticeText::ReleaseSlateResources(bool bReleaseChildren)
{
	// 위젯이 내려간 뒤 타이머가 죽은 위젯을 부르지 않게 (engines/unreal.md 코드 규칙 7).
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimer);
	}
	Super::ReleaseSlateResources(bReleaseChildren);
}
