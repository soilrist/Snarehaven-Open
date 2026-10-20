#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "PlayerNameTagWidget.generated.h"

class APawn;
class UBorder;
class UTextBlock;

// 머리 위 이름표 (DESIGN 2.2 「이름표」 · 시트 `docs/reference/hud_ingame.md`) — 레퍼런스처럼 색 알약에 흰 굵은 이름.
// 자기 것도 보인다. 캐릭터의 `UWidgetComponent`(화면 공간)가 이 위젯을 띄운다.
// 이름 알약 바로 위에 감정표현 말풍선이 3초 뜬다 (M20-07 · DESIGN 2.2 「감정표현 말풍선」).
// 그 위에 먹은 뒤 「+N」 이 떠올라 사라진다 (DESIGN 8.8 「먹기」).
UCLASS()
class SNAREHAVEN_API UPlayerNameTagWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 이름이 비면 "플레이어", 길면 잘라 "…" 을 붙인다. */
	static FString MakeLabel(const FString& PlayerName);

	/**
	 * 알약 색 번호 (DESIGN 2.2 「이름표」) — 서버가 준 방 이름 "플레이어 N" 의 N-1. 방에서 안 쓰는 가장 작은 번호라
	 * 나갔다 들어와도 8명까지 겹치지 않는다. 그 꼴이 아니면 엔진 `PlayerId`(접속할 때마다 늘어 겹칠 수 있다).
	 */
	static int32 GetColorSlot(const FString& PlayerName, int32 PlayerId);

	void SetOwnerPawn(APawn* Pawn) { OwnerPawn = Pawn; }

	/** 먹은 뒤 머리 위 글자 — 회복은 「+15」, 독은 「-20」(UI 글꼴 Jua 에 있는 붙임표), 0 이면 빈 글. */
	static FString MakeHealPopLabel(int32 Amount);

	/** 떠오름 몫(0~1)일 때 글자가 위로 간 거리(px)·불투명도 — 앞 절반은 그대로, 뒤 절반에 사라진다. */
	static float GetHealPopRisePx(float Progress);
	static float GetHealPopOpacity(float Progress);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	TWeakObjectPtr<APawn> OwnerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Pill;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Label;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HealPop;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Bubble;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BubbleText;

	FString ShownName;
	int32 ShownColor = INDEX_NONE;
	int32 ShownBubble = INDEX_NONE;
	int32 ShownHealPopCount = 0;
};
