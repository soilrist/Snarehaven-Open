#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "SnareFirstRunGuideWidget.generated.h"

class AAnimalCharacter;
class ASnarehavenCharacter;
class UBorder;
class UButton;
class UInventoryComponent;
class USnareBaseSubsystem;
class UTextBlock;

// 처음 켠 사람에게 보이는 **첫 실행 안내** (ROADMAP M17-01). 이동·줍기·손도끼 사기·나무 베기(INBOX 18:10 ③)·
// 공격·포획·목장 넣기·수확에 텐트·작업대·불(M28-07)까지 한 줄씩 열한 장으로 말하고, 끝까지 넘기거나 건너뛰면 저장해 다시 안 뜬다. 새 시스템이 아니라 사냥 HUD 위에
// 얹는 크림 카드 하나다 — 게임 상태를 바꾸지 않는다.
// 행동으로 넘기기(M19-08): 공격이 맞으면·잡은 펫이 서버 응답 뒤 가방에 들어오면·`RegisterPet`·`HarvestRanch` 를
// 서버가 받아 주면 그 장을 저절로 넘긴다. 줍기·베기 장은 스냅샷의 나뭇가지가 늘면,
// 손도끼 장은 손도끼가 들어오면 넘긴다. 텐트·불 장은 거점 사본(서버 응답으로만 채워진다)에 내 텐트·불이 서면,
// 작업대 장은 스냅샷의 나무판이 늘면 넘긴다. 전부 **서버 응답 뒤 이벤트만** 듣는다 (CLAUDE.md 8).
UCLASS()
class SNAREHAVEN_API USnareFirstRunGuideWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 안내 한 장 한 장의 글 — 순서 그대로. 2층 검사도 이 배열을 읽는다. */
	static const TArray<FText>& GetSteps();

	/** 이 기기에서 안내를 이미 봤는가 (`GameUserSettings.ini` 의 `[Snarehaven.Guide] Seen`). */
	static bool IsGuideSeen();
	static void SetGuideSeen(bool bSeen);

	/** 명령줄 `-SnareNoGuide` 가 있으면 안 띄운다 (캡처·자동 검사용). */
	static bool ShouldShowGuide();

	/** 지금 보이는 장 번호(0부터). 모든 장을 다 넘기면 `GetSteps().Num()`. */
	int32 GetStepIndex() const { return StepIndex; }

	bool IsFinished() const { return StepIndex >= GetSteps().Num(); }

	/** 다음 장으로. 마지막 장에서 부르면 끝내고 저장한다. */
	void Advance();

	/** 건너뛰기 — 남은 장을 보지 않고 끝내고 저장한다. */
	void Skip();

	/** 이 캐릭터의 공격·포획·가방·목장 응답 이벤트를 들어 해당 장을 저절로 넘긴다 (M19-08). */
	void BindToCharacter(ASnarehavenCharacter* Character);

	/** 거점 사본이 바뀔 때 텐트·불 장을 저절로 넘긴다 (M28-07). `BindToCharacter` 가 게임 인스턴스 것을 건다. */
	void BindToBase(USnareBaseSubsystem* Base);

	/** 끝낼 때 저장할지. 2층 검사는 사용자 저장을 건드리지 않게 끈다. */
	bool bPersistSeen = true;

	/** 다 넘기거나 건너뛰어 카드를 내린 순간 — 그 뒤에 HUD 다음 목표 줄이 뜬다(M22 ⑤). */
	FSimpleMulticastDelegate OnFinished;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleNextClicked();

	UFUNCTION()
	void HandleSkipClicked();

	UFUNCTION()
	void HandleMeleeHit(AAnimalCharacter* Target, float Damage);

	UFUNCTION()
	void HandleAnimalCaptured(AAnimalCharacter* Target, FName PetItemID);

	UFUNCTION()
	void HandleInventoryChanged();

	UFUNCTION()
	void HandleRanchAnswered(const FString& FunctionName, bool bAccepted, const FString& FailureCode, const FText& Message);

	/** 거점 사본이 바뀌었다 — 텐트·불 장이면 내 땅을 본다. */
	void HandleBaseChanged();

	/** `Step` 장의 행동을 해냈다 — 그 장이나 그 앞에 있으면 다음 장으로 건너간다. */
	void CompleteStep(int32 Step);

	void Finish();
	void Refresh();

	/** 잡았지만 아직 서버가 가방에 넣어 주지 않은 펫 — 스냅샷에 들어오면 포획 장을 넘긴다. */
	FName PendingPetID;

	/** 마지막으로 본 스냅샷의 나뭇가지 수 — 늘었을 때만 줍기·베기 장을 넘긴다. */
	int32 LastWoodCount = 0;

	/** 마지막으로 본 스냅샷의 나무판 수 — 늘었을 때만 작업대 장을 넘긴다. */
	int32 LastPlankCount = 0;

	TWeakObjectPtr<USnareBaseSubsystem> BoundBase;
	FDelegateHandle BaseChangedHandle;

	UPROPERTY(Transient)
	TObjectPtr<UInventoryComponent> BoundInventory;

	int32 StepIndex = 0;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Card;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StepText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NextLabel;

	UPROPERTY(Transient)
	TObjectPtr<UButton> NextButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> SkipButton;
};
