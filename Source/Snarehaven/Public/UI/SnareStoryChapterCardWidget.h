#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "SnareStoryChapterCardWidget.generated.h"

class UTextBlock;

// 장을 끝냈을 때 한가운데 뜨는 **장 끝 카드** (INBOX 메인 스토리 ②, `docs/STORY.md` 5·6절) — 장 이름과 받은 보상.
// 누르면 닫힌다. 지급은 서버가 이미 했고 여기는 보여 주기만 한다. 어디까지 봤는지만 이 기기 설정에 적는다.
UCLASS()
class SNAREHAVEN_API USnareStoryChapterCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 이 기기에서 카드를 본 가장 큰 장 (`[Snarehaven.Story] ChapterCardSeen`). */
	static int32 GetSeenChapter();
	static void SetSeenChapter(int32 Chapter);

	/** 끝낸 장이 있고 아직 안 봤고 `-SnareNoGuide` 가 없을 때. */
	static bool ShouldShow(int32 CompletedChapter);

	/** 장 카드를 싣는다. 범위 밖이면 false. */
	bool ShowChapter(int32 Chapter);

	int32 GetChapter() const { return Chapter; }
	FText GetRewardText() const { return RewardString; }
	bool IsFinished() const { return bFinished; }

	/** 닫는다. */
	void Close();

	/** 닫을 때 본 장을 저장할지. 검사·미리보기는 끈다. */
	bool bPersistSeen = true;

	/** 닫힌 순간 (저장 뒤) — 미룬 대사 판을 이어 띄운다 (감독 보정 10-20 17:15). */
	FSimpleMulticastDelegate OnClosed;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;

private:
	void Refresh();

	int32 Chapter = 0;
	bool bFinished = false;
	FText TitleString;
	FText RewardString;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeadText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RewardText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HintText;
};
