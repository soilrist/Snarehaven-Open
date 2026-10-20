#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WaterActionWidget.generated.h"

class UButton;
class UMountComponent;
class UTapFishingComponent;
class UTextBlock;
class UVerticalBox;
class UWaterTravelComponent;

// DESIGN 8.9·8.13 물 버튼 — 낚시 · 감기 · 그만두기 · 배 띄우기 · 타기 · 내리기 · 배 넣기, 입질 "!" 카드, 발밑 연타 게이지.
// 같은 줄에 8.3 탈것 타기/내리기 토글 하나(M6-06) — 뭍에서만 보이고, 못 타면 까닭을 한 줄로 보인다.
// 탈것 종이 둘 이상이면 "탈것 바꾸기" 하나 더(M13-03 · 결정 009) — 누를 때마다 가진 종을 차례로 넘긴다.
// 배에 탄 채 보트 컨테이너가 가방에 있으면 "배 보관함" — 제작 판의 배 보관함 쪽을 연다 (DESIGN 8.9).
// 버튼은 지금 할 수 있는 것만 보인다(오른쪽 가운데 세로 한 줄 — 사냥 HUD 의 오른쪽 아래 버튼과 안 겹친다).
// 판정은 전부 두 컴포넌트가 한다 — 이 위젯은 보이기·누르기만.
// **자리표시 모양** (`UPauseMenuWidget` 과 같은 규칙) — WBP 트리가 비어 있으면 C++ 이 세운다.
UCLASS()
class SNAREHAVEN_API UWaterActionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void BindToPawn(UWaterTravelComponent* InWater, UTapFishingComponent* InFishing);

	/** 보이기·글자를 지금 상태로 맞춘다. NativeTick 과 2층 검사가 같은 길을 쓴다. */
	void RefreshView();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildPlaceholderTree();
	UButton* AddButton(UVerticalBox& Column, const TCHAR* Name, const FText& Label);

	UFUNCTION() void HandleCast();
	UFUNCTION() void HandleTap();
	UFUNCTION() void HandleQuit();
	UFUNCTION() void HandleLaunch();
	UFUNCTION() void HandleBoard();
	UFUNCTION() void HandleLeave();
	UFUNCTION() void HandleStow();
	UFUNCTION() void HandleMount();
	UFUNCTION() void HandleMountPick();
	UFUNCTION() void HandleBoatStorage();

	UPROPERTY(Transient) TObjectPtr<UWaterTravelComponent> Water;
	UPROPERTY(Transient) TObjectPtr<UTapFishingComponent> Fishing;
	UPROPERTY(Transient) TObjectPtr<UMountComponent> Mount;

	/** 버튼 세로 줄 — 제작 판(배 보관함)이 열린 동안 통째로 접는다 (INBOX 결함 10-07 02:00 ②). */
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ButtonColumn;
	UPROPERTY(Transient) TObjectPtr<UButton> CastButton;
	UPROPERTY(Transient) TObjectPtr<UButton> TapButton;
	UPROPERTY(Transient) TObjectPtr<UButton> QuitButton;
	UPROPERTY(Transient) TObjectPtr<UButton> LaunchButton;
	UPROPERTY(Transient) TObjectPtr<UButton> BoardButton;
	UPROPERTY(Transient) TObjectPtr<UButton> LeaveButton;
	UPROPERTY(Transient) TObjectPtr<UButton> StowButton;
	UPROPERTY(Transient) TObjectPtr<UButton> MountButton;
	UPROPERTY(Transient) TObjectPtr<UButton> MountPickButton;
	UPROPERTY(Transient) TObjectPtr<UButton> BoatStorageButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BiteAlert;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText;
};
