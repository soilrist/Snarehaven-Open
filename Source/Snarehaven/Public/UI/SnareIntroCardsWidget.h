#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "SnareIntroCardsWidget.generated.h"

class UBorder;
class UImage;
class UTextBlock;

// 처음 켠 사람에게 보이는 **그림 카드 3장** (INBOX 정체성 ② · docs/STORY.md):
// 난파해 깨어남 → 올가미로 동물 포획 → 목장에서 돌보면 온기가 모여 등대가 켜짐.
// 타이틀의 「시작」을 처음 누를 때 한 번만 뜨고, 넘기기(건너뛰기)로 닫을 수 있다. 게임 상태를 바꾸지 않는다.
UCLASS()
class SNAREHAVEN_API USnareIntroCardsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static int32 GetCardCount();
	static FText GetCardText(int32 Index);
	/** 본문 아래 작은 글씨 한 묶음 (마지막 장의 첫 할 일·안내). 없으면 빈 글. */
	static FText GetCardNote(int32 Index);
	/** 카드 그림 — `/Game/Game/UI/Intro/T_IntroCard_0N` 경로. 범위 밖이면 빈 문자열. */
	static FString GetCardIconPath(int32 Index);

	/** 이 기기에서 카드를 이미 봤는가 (`GameUserSettings.ini` 의 `[Snarehaven.Guide] IntroSeen`). */
	static bool IsIntroSeen();
	static void SetIntroSeen(bool bSeen);

	/** 안 봤고 `-SnareNoGuide` 가 없을 때만 참. */
	static bool ShouldShowIntro();

	int32 GetCardIndex() const { return CardIndex; }
	bool IsFinished() const { return CardIndex >= GetCardCount(); }

	/** 다음 장으로. 마지막 장에서 부르면 끝낸다. */
	void Advance();

	/** 건너뛰기 — 남은 장을 보지 않고 끝낸다. */
	void Skip();

	/** 끝낼 때 저장할지. 검사·미리보기는 끈다. */
	bool bPersistSeen = true;

	/** 끝난 순간 (저장 뒤). 로비가 여기서 사냥 맵을 연다. */
	FSimpleMulticastDelegate OnFinished;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleNextClicked();

	UFUNCTION()
	void HandleSkipClicked();

	void Finish();
	void Refresh();

	int32 CardIndex = 0;

	UPROPERTY(Transient)
	TObjectPtr<UImage> CardImage;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CardTextBlock;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CardNoteBlock;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NextLabel;
};
