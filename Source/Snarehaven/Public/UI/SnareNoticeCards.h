#pragma once

#include "CoreMinimal.h"
#include "Components/VerticalBox.h"
#include "Engine/TimerHandle.h"
#include "UI/SnareAchievementNotice.h"

#include "SnareNoticeCards.generated.h"

class UBorder;
class UImage;
class UTextBlock;
class UWidgetTree;

// 업적·도감 알림 카드 두 장 (DESIGN 8.15 「쌓기」·「색 점」, M48-03c).
// 크림 카드 300×70 이 세로로 최대 2장, 왼쪽 아이콘 48(비면 접고 글만), 카드 테두리 2 와 오른쪽 위 모서리에 걸친 기울어진 「!」 꼬리표가
// 같은 색(업적 금 · 도감 빨강) — 레퍼런스 시트 `docs/reference/hud_ingame.md` 5행 · 원본 `steam_16.jpg`(INBOX 보정 10-08 05:10). 사냥·목장 HUD 가 왼쪽 위 알림 줄에 같은 것을 하나씩 세운다.
// 표시 시간만 센다 — 지급·잔액은 건드리지 않는다(CLAUDE.md 8, 카드는 서버 답의 결과 표시).
UCLASS()
class SNAREHAVEN_API USnareNoticeCards : public UVerticalBox
{
	GENERATED_BODY()

public:
	/** 카드 `MaxCards` 장을 접힌 채로 세운다. 자리표시 트리를 세울 때 한 번만 부른다. */
	void BuildCards(UWidgetTree& Tree);

	/** 카드를 앞에서부터 채워 Seconds 초 보인 뒤 접는다. 빈 칸은 접힌다. 다시 부르면 새 카드로 바꾸고 시간이 새로 시작한다. */
	void ShowCards(const TArray<SnareAchievementNotice::FCard>& Cards, float Seconds);

	/** 지금 바로 전부 접는다. */
	void HideCards();

	/** 펴진 카드 수 — 검사용. */
	int32 GetShownCardCount() const;

	/** Index 번째 카드의 겉 칸·제목·설명·「!」 꼬리표 — 검사용. 없으면 nullptr. */
	UBorder* GetCard(int32 Index) const { return Cards.IsValidIndex(Index) ? Cards[Index].Get() : nullptr; }
	UTextBlock* GetTitle(int32 Index) const { return Titles.IsValidIndex(Index) ? Titles[Index].Get() : nullptr; }
	UTextBlock* GetBody(int32 Index) const { return Bodies.IsValidIndex(Index) ? Bodies[Index].Get() : nullptr; }
	UTextBlock* GetTag(int32 Index) const { return Tags.IsValidIndex(Index) ? Tags[Index].Get() : nullptr; }
	UImage* GetIcon(int32 Index) const { return Icons.IsValidIndex(Index) ? Icons[Index].Get() : nullptr; }

	/** 왼쪽 아이콘 한 변 — 시트 `hud_ingame.md` 5행 「왼쪽 아이콘 48px」. 검사용으로도 쓴다. */
	static constexpr float IconSize = 48.f;

	/** 테두리 두께 — 원본 카드 테두리 2px. */
	static constexpr float OutlineWidth = 2.f;

	/** 색 점(테두리·「!」 꼬리표 색) — 업적 금 · 도감 빨강 (DESIGN 8.15 「색 점」). */
	static FLinearColor DotColor(bool bAchievement);

protected:
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	/** 카드 테두리를 Color 로 — 바탕은 크림 그대로. */
	static void SetCardColor(UBorder& Card, const FLinearColor& Color);

	UPROPERTY()
	TArray<TObjectPtr<UBorder>> Cards;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> Titles;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> Bodies;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> Tags;

	UPROPERTY()
	TArray<TObjectPtr<UImage>> Icons;

	FTimerHandle HideTimer;
};
