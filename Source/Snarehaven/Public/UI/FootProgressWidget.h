#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "FootProgressWidget.generated.h"

class APawn;
class UProgressBar;

// 발밑 얇은 진행바 (레퍼런스 시트 `docs/reference/hud_ingame.md` 「월드 위 오버레이」 — "폭 60px, 높이 6px,
// 회색 바탕+흰 채움 = 행동 진행"). 이름표(`UPlayerNameTagWidget`)와 **같은 길**로 뜬다 — 캐릭터의
// `UWidgetComponent`(화면 공간)가 이 위젯을 띄우고, 여기서는 진행 몫만 읽어 채운다.
//
// 진행하는 행동이 없으면 **아예 접힌다**(시트: "진행이 없으면 안 보인다"). 지금 진행이 있는 행동은
// **연타 낚시** 하나다 — 채집·포획은 DESIGN 3.3 이 "즉시 획득·임계값 도달 즉시 확정" 이라 진행 구간이
// 없다(`UGatherComponent::TryGatherNearest`·`UCaptureComponent::TryCaptureTarget` 둘 다 한 호출에 끝난다).
UCLASS()
class SNAREHAVEN_API UFootProgressWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 시트 규격(1920 기준) — 위젯 컴포넌트가 이 크기로 그리고, 2층 검사가 같은 값을 본다. */
	static constexpr float SheetWidthPx = 60.f;
	static constexpr float SheetHeightPx = 6.f;

	/**
	 * 이 폰이 지금 하는 행동의 진행 몫(0~1).
	 * 남의 몸에는 **안 그린다** — 진행값은 복제되지 않아(복제되는 것은 보이는 단계·누름 수뿐,
	 * `UTapFishingComponent`) 남의 화면에서는 늘 0 으로 보인다. 거짓 0 을 그리느니 접는다.
	 * @return 진행 중인 행동이 있으면 true (그때만 `OutProgress` 가 의미 있다)
	 */
	static bool QueryActionProgress(const APawn* Pawn, float& OutProgress);

	void SetOwnerPawn(APawn* Pawn) { OwnerPawn = Pawn; }

	/** 진행을 다시 읽어 바를 채우거나 접는다. Tick 과 2층 검사가 같은 길을 쓴다. */
	void RefreshView();

	UProgressBar* GetBar() const { return Bar; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	TWeakObjectPtr<APawn> OwnerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> Bar;
};
