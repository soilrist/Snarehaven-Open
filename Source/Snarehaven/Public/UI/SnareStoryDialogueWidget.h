#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Backend/SnareStory.h"

#include "SnareStoryDialogueWidget.generated.h"

class UCanvasPanelSlot;
class USizeBox;
class UTextBlock;

// 새 스토리 단계를 받을 때 화면 아래에 뜨는 **대사 판** (INBOX 메인 스토리 ②, `docs/STORY.md` 4절).
// 누르면 다음 줄, 마지막 줄을 누르면 닫힌다. 게임 상태를 바꾸지 않는다 — 어디까지 봤는지만 이 기기 설정에 적는다.
UCLASS()
class SNAREHAVEN_API USnareStoryDialogueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 이 기기에서 대사를 본 가장 큰 단계 (`GameUserSettings.ini` 의 `[Snarehaven.Story] DialogueSeenStep`). */
	static int32 GetSeenStep();
	static void SetSeenStep(int32 Step);

	/** 지금 단계의 대사를 띄워야 하나 — 단계가 있고, 아직 안 봤고, `-SnareNoGuide` 가 없을 때. */
	static bool ShouldShow(int32 CurrentStep);

	/** 단계의 대사를 싣고 첫 줄로. 대사가 없으면 false. */
	bool ShowStep(int32 Step, bool bArrival = false);

	/** 가기 단계 시작 때 「가 보게」만 띄운다 — 저장·보고 없이 (도착 때 발견 대사가 따로 뜬다). */
	bool ShowGoStep(int32 Step);

	int32 GetStep() const { return Step; }
	int32 GetLineIndex() const { return LineIndex; }
	int32 GetLineCount() const { return Lines.Num(); }
	bool IsFinished() const { return bFinished; }

	/** 화면 가로(위젯 단위)에 맞춘 판 너비 — 가운데 정렬 판이 오른쪽 아래 기력 막대(왼쪽 끝이 화면 오른쪽에서 `StaminaReserve` 안쪽)를 덮지 않게 줄인다. */
	static float FitPanelWidth(float ScreenWidth);

	/** 판 너비를 화면 가로에 맞춘다 — 매 틱 부른다. 검사는 직접 불러 실제 위젯의 폭을 본다. */
	void FitToScreen(float ScreenWidth);

	/** 다음 줄로. 마지막 줄이면 끝낸다. */
	void Advance();

	/** 끝낼 때 본 단계를 저장할지. 검사·미리보기는 끈다. */
	bool bPersistSeen = true;

	/** 끝난 순간 (저장 뒤). */
	FSimpleMulticastDelegate OnFinished;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;

private:
	void Finish();
	void Refresh();

	int32 Step = 0;
	int32 LineIndex = 0;
	bool bFinished = false;
	/** 판이 뜬 뒤 흐른 초 — 검사 스위치 `-SnareStoryReadAt=` 만 읽는다. */
	float ShownSeconds = 0.f;
	TArray<SnareStory::FDialogueLine> Lines;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanelSlot> PanelSlotRef;

	// 자동 크기 슬롯은 SetSize 의 너비를 무시한다 — 판 너비는 이 상자의 WidthOverride 가 정한다.
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> PanelBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SpeakerText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BodyText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HintText;
};
