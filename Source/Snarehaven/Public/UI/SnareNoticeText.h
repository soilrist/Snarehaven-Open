#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "Engine/TimerHandle.h"

#include "SnareNoticeText.generated.h"

// 잠깐 떴다 스스로 접히는 한 줄 알림 (M12-05 목장 거절 문구가 처음 쓴다).
// 표시 시간만 센다 — 게임 규칙 시간(목장 유지·생산)과 상관이 없어서 목장 HUD 파일 밖에 둔다
// (`Snarehaven.Ranch.Slot.NoClientSideTimer` 는 목장 파일에서 시간 세는 토막을 막는다).
UCLASS()
class SNAREHAVEN_API USnareNoticeText : public UTextBlock
{
	GENERATED_BODY()

public:
	/** Message 를 Seconds 초 동안 보인 뒤 접는다. 다시 부르면 시간이 새로 시작한다. */
	void ShowFor(const FText& Message, float Seconds);

	/** 지금 바로 접는다. */
	void HideNotice();

	/**
	 * 글자와 같이 펴고 접을 위젯 (M23-03 — 글자를 감싼 알림 카드). 글자만 접히면 빈 카드가 남는다.
	 * 카드 안 다른 줄(아이콘·설명)은 카드가 접히면 같이 안 보이니 따로 걸지 않는다.
	 */
	void SetCompanion(UWidget* InCompanion) { Companion = InCompanion; }

protected:
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	void SyncCompanion();

	UPROPERTY()
	TObjectPtr<UWidget> Companion;

	FTimerHandle HideTimer;
};
