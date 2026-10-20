#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/ShopRow.h"
#include "SnarehavenCharacter.h"
#include "UI/SnareUiStyle.h"
#include "WorldMapScreenWidget.generated.h"

class APawn;
class UButton;
class ASeaActor;
class ASceneCapture2D;
class UBorder;
class UCanvasPanel;
class UImage;
class UTextBlock;
class UTextureRenderTarget2D;

// 지도 화면 (감독 2026-09-25 · DESIGN 8.12 🟡) — HUD 오른쪽 아래 둥근 지도 버튼, 누르면 섬 전체를 위에서 찍은
// 그림 + 내 자리 점 + 닫기. 존 표 칸이 없는 목장에서도 나온다(그림은 바다 액터의 섬 크기로 찍는다).
// 화면 위 = 월드 +X, 오른쪽 = +Y — 사냥 HUD 미니맵(`SnareMinimapHud`)과 같은 방향.
// 오른쪽 아래 버튼 기둥(지도·톱니·이모트)을 이 위젯이 다 세운다 — 이모트는 DESIGN 2.2 「이모트」.
UCLASS()
class SNAREHAVEN_API UWorldMapScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 로컬 플레이어 폰에 붙여 화면에 올린다. 버튼만 보이고 지도는 닫힌 채 — `-SnareMapScreen` 이면 연 채. */
	static UWorldMapScreenWidget* AttachTo(APawn* Pawn);

	/** 월드 XY → 지도 그림 안 비율(0~1). 위 = +X, 오른쪽 = +Y. */
	static FVector2D WorldToMapUv(const FVector2D& CenterXY, float WidthCm, const FVector& World);

	/** 오른쪽 위 좌표 글 — 섬 가운데에서 250cm 마다 1도, 위(+X) = N, 오른쪽(+Y) = E. 예 "4°N / 12°W". */
	static FString FormatCoordinates(const FVector2D& CenterXY, const FVector& World);

	UFUNCTION(BlueprintCallable, Category = "WorldMap")
	void SetMapOpen(bool bOpen);

	/** 왼쪽 위 미니맵을 눌렀을 때 — 오른쪽 아래 지도 버튼과 같은 판을 연다(`SetMapOpen(true)`). 붙은 지도가 없으면 false. */
	static bool OpenLocalMap();

	/** 이모트 버튼이 펼치는 줄 — **순서 그대로** 오른쪽(이모트 버튼 곁)부터 왼쪽으로 선다. */
	struct FEmoteEntry
	{
		EItemAction Action;
		FText Label;
	};
	static const TArray<FEmoteEntry>& GetEmotes();

	void SetEmotesOpen(bool bOpen);
	bool AreEmotesOpen() const { return bEmotesOpen; }

	/** 말풍선 알약을 누른 것과 같은 길 — 줄을 접고 내 캐릭터 머리 위에 띄운다. `-SnareEmoteBubble=<번호>` 도 이 길. */
	void PlayBubble(int32 Index);

	bool IsMapOpen() const { return bMapOpen; }

	/** 지도 표식 글자 (INBOX 18:10 ④ · DESIGN 8.12 🟡 「상인 표식」) — 잡화점·낚시·농사·옷. */
	static FText GetVendorMapLabel(EShopKind Kind);

	/** 지도 그림 틀 한 변(1080 높이 기준) — 판 여백 40 + 위아래 틈 25. 좌우 남는 판에 섬 밖 마을 표지 카드가 선다. */
	static constexpr float MapSize = 950.f;

	/** 상인 표식 알약 아래 끝을 상인 자리보다 이만큼 위에 둔다 — 내 빨간 점(지름 26, 반지름 13) + 틈 2.
	 *  가게 앞에 서도 점이 글자를 덮지 않게 (루프#563 `captures/563_map.png` 「잡화점」 「화」 가림). */
	static constexpr float VendorPillLift = 15.f;

	/**
	 * 지도 그림 위 상인 표식을 지금 월드의 상인(`AShopVendorActor`, 의뢰인 빼고)으로 다시 세운다.
	 * 지도를 다시 찍을 때(열 때·3초마다) 부르고, 2층 검사도 직접 부른다. 세운 표식 수를 돌려준다.
	 */
	int32 RefreshVendorMarkers();

	/** 마을 이름 글자 (INBOX 마을 이름 ②) — `DT_OutpostTable` 14곳을 흰 글자 + 진갈색 테로 세운다.
	 *  자리는 제 점 아래 → 위 → 오른쪽 → 왼쪽 순으로 먼저 빈 곳(글자끼리 안 겹침). 이름 겹이 상인 알약 겹 위라
	 *  겹치면 알약이 비킨다(`DodgeRects`). 세운 글자 수를 돌려준다. */
	int32 RefreshOutpostLabels();

	/** 방금 세운 이름 글자 상자(지도 그림 좌표) — 시험이 겹침 0 을 단정하는 데 쓴다. */
	const TArray<FBox2D>& GetOutpostLabelRects() const { return OutpostLabelRects; }

	/** 방금 세운 상인 알약 상자 — 시험이 알약끼리·이름과 겹침 0 을 단정한다. */
	const TArray<FBox2D>& GetVendorPillRects() const { return VendorPillRects; }

	/** 글자 한 줄 상자 크기(글자 수 × 글자 크기 × 1.2(한글+띄어쓰기, 캡처로 잼) · 글자 크기 × 1.3, 테 두께 포함). */
	static FVector2D EstimateTextSize(int32 CharCount, float FontSize, int32 Outline);

	/** 아래 가운데 기준 알약 상자 `Pill` 이 `Avoid` 와 겹치면, 안 겹칠 때까지 위로 올릴 거리(≥0)를 돌려준다. */
	static float DodgeRects(const FBox2D& Pill, const TArray<FBox2D>& Avoid, float Gap = 0.f);

	/** 이름 글자 맨 위를 거점 자리보다 이만큼 아래에 둔다 — 내 점(반지름 13)이 글자를 덮지 않게. */
	static constexpr float OutpostLabelDrop = 16.f;

	/** 이름 상자끼리 최소 간격(px) — 글자 끝과 다음 글자 앞이 맞닿지 않게. */
	static constexpr float OutpostLabelGap = 8.f;
	/** 상인 알약이 이름 상자·다른 알약과 떨어질 최소 간격(px). */
	static constexpr float VendorPillGap = 4.f;

	/** 지금 있어야 할 뷰포트 Z — 열리면 전체 화면 판 Z(HUD·출석 버튼 위), 닫히면 HUD Z(배낭·상자 판 아래),
	 * 닫힌 채 이모트 줄을 폈으면 `HudPopupViewportZOrder`(상인 「상점 열기」 카드 위). */
	int32 GetViewportZOrder() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void ToggleMap();
	UFUNCTION()
	void CloseMap();
	/** 톱니 버튼 — 폰에는 ESC 가 없어 일시정지 메뉴를 여는 길이 이것뿐이다. */
	UFUNCTION()
	void OpenPauseMenu();
	UFUNCTION()
	void ToggleEmotes();
	// UButton 눌림 알림에는 인자가 없어 이모트마다 하나씩 둔다 — 순서는 `GetEmotes()`.
	UFUNCTION()
	void PlayEmote0();
	UFUNCTION()
	void PlayEmote1();
	UFUNCTION()
	void PlayEmote2();
	UFUNCTION()
	void PlayEmote3();
	void PlayEmote(int32 Index);
	// 감정표현 말풍선 여섯 (M20-07) — 몸 동작 줄 바로 위 한 줄, 순서는 `SnareEmoteBubble`.
	UFUNCTION()
	void PlayBubble0();
	UFUNCTION()
	void PlayBubble1();
	UFUNCTION()
	void PlayBubble2();
	UFUNCTION()
	void PlayBubble3();
	UFUNCTION()
	void PlayBubble4();
	UFUNCTION()
	void PlayBubble5();

	void BuildTree();
	void CaptureMap();
	/** 부두·잔교·항구 배·소품을 맵에서 한 번 찾는다 — 상자는 알약 자리, 액터는 지도 캡처에서 숨긴다. */
	void FindHarbor();
	/** 부두 자리 가운데에 작은 알약 「항구」 를 세운다. 부두 없는 맵이면 접는다. */
	void RefreshHarborCover();

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> MapLayer;
	UPROPERTY(Transient)
	TObjectPtr<UImage> MapImage;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> PlayerDot;
	/** 스토리 목표 표식 — 지금 단계의 상대 자리, 내 점 아래. 상대가 없으면 접힘. */
	UPROPERTY(Transient)
	TObjectPtr<UBorder> StoryDot;
	/** 상인 표식 겹 — 그림 위·내 점 아래. */
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> VendorLayer;
	/** 마을 이름 겹 — 그림 위·상인 표식 위·내 점 아래. */
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> OutpostLayer;
	/** 섬 밖 마을 표지 카드 바탕 겹 — 지도 틀 좌우 바깥, 이름 글자 겹 아래. */
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> OutpostCardLayer;
	/** 부두 자리 알약 「항구」 — 부두·배는 캡처에서 숨기고 이 알약만 남긴다. */
	UPROPERTY(Transient)
	TObjectPtr<UBorder> HarborCover;
	/** 부두·잔교 상자(월드) — 맵에서 한 번 찾는다. 없으면 무효 상자. */
	FBox HarborBounds = FBox(ForceInit);
	bool bHarborSearched = false;
	/** 지도 캡처에서 숨길 부두·잔교·항구 배·소품. 위에서 찍으면 흰 띠·흰 배로만 보인다. */
	TArray<TWeakObjectPtr<AActor>> HarborActors;
	TArray<FBox2D> OutpostLabelRects;
	TArray<FBox2D> VendorPillRects;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CoordText;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> EmoteButtons;

	TWeakObjectPtr<ASeaActor> Sea;

	TWeakObjectPtr<ASceneCapture2D> Capture;
	FVector2D MapCenterXY = FVector2D::ZeroVector;
	float MapWidthCm = 10000.f;
	float NextCaptureSeconds = 0.f;
	/** 뷰포트에 붙인 Z — `SetMapOpen` 이 달라졌을 때만 떼었다 다시 붙인다. */
	int32 AddedViewportZOrder = SnareUiStyle::HudViewportZOrder;
	bool bMapOpen = false;
	bool bEmotesOpen = false;
	/** `RebuildWidget` 안 — 이때는 뷰포트에서 떼었다 붙이지 않는다. */
	bool bBuildingTree = false;

	/** `GetViewportZOrder` 가 붙인 Z 와 다르면 떼었다 다시 붙인다 (지도 열고 닫기·이모트 줄 펴고 접기). */
	void ApplyViewportZOrder();
};
