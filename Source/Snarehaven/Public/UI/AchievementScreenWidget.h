#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "AchievementScreenWidget.generated.h"

class UButton;
class UDataTable;
class UTextBlock;
class UVerticalBox;
class USnareBackendSubsystem;
class USnareClickProxy;

/** 업적 화면 한 줄 — 글(`첫 올가미  1/1`)과 서버가 달성이라 적었는가. 테스트가 이 목록으로 화면을 읽는다. */
struct FAchievementLineView
{
	FText Text;
	bool bDone = false;
};

// ROADMAP M12-06: 업적 화면 — `DT_AchievementTable`(DESIGN 8.15) 줄마다 달성 표시와 진행 수 `현재/목표`.
// 도감 화면(`UCodexScreenWidget`)과 같은 코드 생성 위젯이고 그 화면의 "업적" 버튼으로 연다.
//
// **달성은 스냅샷의 `Achievements.Completed` 에 있는가 하나로만 그린다.** 진행 수가 목표를 넘어도
// 미달 원판 그대로다 — 화면이 세어 "달성" 이라 적으면 서버가 안 준 보상을 받은 것처럼 보인다 (CLAUDE.md 8).
// 백엔드에게 못 들었으면 머리글도 진행 수도 `-` 다 (도감 화면과 같은 규칙).
UCLASS()
class SNAREHAVEN_API UAchievementScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UAchievementScreenWidget(const FObjectInitializer& ObjectInitializer);

	/** 머리글 — `업적 3 / 13`. 모르면 `업적 - / 13`. 분모는 표 줄 수다. */
	FText GetProgressText() const;

	/** 표 순서대로 한 줄씩 — 글 `첫 올가미  1/1` + 달성 여부. 달성 표시는 글자가 아니라 줄 왼쪽 원판이다 (M25-03). */
	TArray<FAchievementLineView> BuildLines() const;

	/** 볼 백엔드. nullptr 이면 전부 `-`. 읽기만 한다. */
	void BindToBackend(USnareBackendSubsystem* Backend);

	/** 2층 검사가 표를 갈아 끼울 때 쓴다 (`UCodexScreenWidget::SetSourceTables` 와 같은 문). */
	void SetSourceTable(UDataTable* InAchievementTable);

	/** 닫으면 다시 보일 화면(도감). 닫기 버튼이 이 화면을 걷고 그것을 되살린다. */
	void SetReturnScreen(UUserWidget* Screen) { ReturnScreen = Screen; }

	/** 닫기 버튼과 같은 길 — 돌아갈 화면(도감)을 되살리고 이 화면을 걷는다. ESC 도 이 길 (`FSnareScreenCloser`). */
	void Close();

	UFUNCTION()
	void RefreshDisplay();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "Achievement", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Achievement", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ProgressText;

	UPROPERTY(BlueprintReadOnly, Category = "Achievement", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> LineList;

	UPROPERTY(BlueprintReadOnly, Category = "Achievement", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	/** 업적 목록의 원본 (DESIGN 8.15). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Achievement")
	TObjectPtr<UDataTable> AchievementTable;

	UPROPERTY(Transient)
	TObjectPtr<USnareClickProxy> CloseProxy;

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;

private:
	TWeakObjectPtr<UUserWidget> ReturnScreen;

	bool IsKnown() const;
	void BuildPlaceholderTree();
	/** 한 줄 = 왼쪽 원판(달성 민트 · 미달 빈 칸 색) + 글. */
	UWidget* MakeLineRow(const FAchievementLineView& Line, int32 Index);
};
