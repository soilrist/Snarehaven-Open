#pragma once

#include "CoreMinimal.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UFont;
class UOverlay;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UUserWidget;
class UWidget;
class UWidgetTree;
enum class EItemGrade : uint8;

// ROADMAP M5-05a·M5-05b: 화면 네 개(로비·사냥 HUD·목장 HUD·일시정지 메뉴)가 **글꼴·색 한 벌**을 쓰게 하는 곳.
// 여기 모아 둔 이유: 자리표시 트리를 세우는 코드가 위젯마다 따로 있어, 글꼴·색을 각자 적으면 한 화면만
// 다른 모습이 되는 사고가 조용히 난다.
namespace SnareUiStyle
{
	// DESIGN 2.1 UI 팔레트(감독 결정 2026-09-21). **여기 말고 다른 데서 색을 적지 않는다** — 표 밖의
	// 색은 만들지 않는다는 것이 설계서의 규칙이다. 적힌 값은 sRGB 헥스라 Slate 가 쓰는 선형 색으로 바꿔 준다.
	// 상수가 아니라 함수인 이유: `FColor::FromHex` 가 런타임 계산이라 전역 초기화 순서에 기대지 않는다.
	FLinearColor UiButtonBase();       // #F2E4C9 버튼 기본 바탕 (아이보리)
	FLinearColor UiButtonPressed();    // #D8C29E 버튼 눌림/비활성
	FLinearColor UiButtonHighlight();  // #F7D98C 선택됨·호버 강조
	FLinearColor UiTextOnButton();     // #4A3826 버튼 위 글자 (진한 갈색)

	// DESIGN 7.1 「목장 나무판과 밧줄」(M97-c-1) — 레퍼런스 크림 알약과 같은 게임으로 읽히지 않게 판·단추를 나무로 칠한다.
	FLinearColor UiWoodButton();       // #88603E 나무 단추 바탕 (10-16 접근성: #8B6340 → 크림 글 대비 4.47 → 4.67)
	FLinearColor UiWoodButtonPressed();// #74512F 나무 단추 눌림·비활성 (결 줄 `UI_WoodGrain` 과 같은 값)
	FLinearColor UiRope();             // #D9BF8C 밧줄(삼끈) 테두리

	// DESIGN 2.1 "배낭 패널" 줄 — 레퍼런스 시트 `docs/reference/inventory_backpack.md` 의 값 그대로.
	FLinearColor UiPanelCream();       // #F5EBD2 배낭 패널 바탕
	FLinearColor UiTitleTab();         // #5A3E36 제목 탭(진갈색 알약)
	FLinearColor UiEmptySlot();        // #E9E0C8 빈 칸
	FLinearColor UiSlotWell();         // #2E2218 배낭 칸 안쪽 홈(M101-02 — 크림 칸 → 판(`UiWoodPanel`)보다 한 단계 어두운 나무 홈)

	// DESIGN 2.1 "인게임 HUD 색"(M15-02) — 레퍼런스 시트 `docs/reference/hud_ingame.md` 의 값 그대로.
	FLinearColor UiNavy();             // #2B2D5C 오른쪽 위 존·잔액 알약
	FLinearColor UiBoltYellow();       // #F5D33B 기력 번개
	FLinearColor UiAccentRed();        // #BF3B38 왼쪽 아래 체력 알약 (10-16 접근성: 시트 #E8514F 는 흰 글 3.67 → 5.39)
	FLinearColor UiIconMint();         // #64EBA9 체력 알약 속 무기 아이콘 동그라미

	// DESIGN 2.1 "상점 패널" 줄(M15-18) — 레퍼런스 시트 `docs/reference/shop_vendor.md` 「색·서체·크기」 값 그대로.
	// 이름 알약 빨강은 위 `UiAccentRed` 와 같은 값이라 새로 만들지 않는다.
	FLinearColor UiShopPriceGreen();   // #27783F 거래 줄 가격 글자 (10-16 접근성: 시트 #3DBB63 은 크림 위 2.09 → 4.6) (▲ 화살표·포획 원 색 — 나무 위 가격은 `StylePriceOnWood`)
	FLinearColor UiWarnRed();          // #B8322F 크림 카드 위 경고 글(M97-c-5, 크림 대비 약 5배)
	FLinearColor UiTextOnWood();       // #FFF8E8 나무 줄 위 글(M99-06, 나무 대비 5.2 — 단추 글 크림 `UiPanelCream` 은 4.67)
	FLinearColor UiWarnOnWood();       // #FFB08A 모자란 재료 칩 글(M99-06 보정, 아래 칩 바탕 위 대비 8 이상 — 따뜻한 주황)
	FLinearColor UiWarnChip();         // #4A1C12 모자란 재료 칩 바탕(진한 붉은 갈색 — 나무 줄 위에서도 한눈에 구분)
	FLinearColor UiDisabledGray();     // #6E675F 꺼진 단추 바탕(나무색 채도를 뺀 회갈색 — 켜진 나무 단추와 한눈에 갈린다, 10-16 메뉴 보정)
	FLinearColor UiTextDim();          // #756551 크림 판 위 흐린 글(10-16 접근성: 진갈 알파 0.45 는 실제 대비 2.3 → 단색 4.74)
	FLinearColor UiWoodPanel();       // #4A3826 제작 판 바탕(M99-06 — 크림 판 → 어두운 나무 표찰, 줄 `UiWoodButton` 이 한 단계 밝음)

	// 오른쪽 아래 아이콘 기둥(배낭·지도·메뉴·이모트·출석·선물) — 2열×3행, 공격·포획 버튼 위 (1080 높이 기준).
	// 한 칸 121 = 기준 폰(가로 1334×616, 배율 0.57)에서 69px = 44pt(애플 최소 누름 칸). 라벨 30 = 17px.
	// 6칸 한 줄은 높이(6×121)가 화면에 안 들어가 두 줄로 세운다. 열 0 = 오른쪽, 행 0 = 아래.
	constexpr float PillarButtonSize = 121.f;
	constexpr float PillarGap = 14.f;
	constexpr float PillarRight = 40.f;            // 포획 버튼 오른쪽 여백과 같은 선
	constexpr float PillarBottom = 238.f;          // 포획 버튼 윗변(220) + 18
	constexpr float PillarLabelFontSize = 20.f;    // 아이콘 아래 겹치는 작은 라벨 (10-16 HUD 3차)
	constexpr float PillarLabelOnlyFontSize = 30.f; // 아이콘 없는 타일은 글자만 크게
	constexpr float PillarIconPadding = 4.f;
	constexpr float PillarIconSize = 96.f;         // 타일 폭의 약 80% (10-16 HUD 3차 사람 지적 「아이콘이 너무 작다」)
	/** 기둥 왼쪽 끝 + 버튼 간격 20 — 기둥 왼쪽에 서야 하는 것(이동 버튼·물 버튼 줄·이모트 알약)의 오른쪽 여백. */
	constexpr float PillarClearRight = PillarRight + 2.f * PillarButtonSize + PillarGap + 20.f;

	/** 기둥 칸 (열, 행) 의 캔버스 위치 — 오른쪽 아래 기준(앵커·정렬 (1,1)). */
	FVector2D PillarPosition(int32 Column, int32 Row);

	/**
	 * 기둥 버튼 속을 [아이콘 / 라벨] 세로 한 벌로 채우고 캔버스 (열, 행) 자리에 놓는다.
	 * 아이콘이 없으면 라벨만 가운데. 라벨은 버튼 안이라 버튼을 접으면 같이 사라진다.
	 * 아이콘 위젯 이름 = 라벨 이름의 「Label」 을 「Icon」 으로 바꾼 것(예: `AttendanceOpenLabel` → `AttendanceOpenIcon`).
	 */
	void PlacePillarButton(UWidgetTree& Tree, UCanvasPanel& Canvas, UButton& Button, UTexture2D* Icon,
		const FText& Label, FName LabelName, int32 Column, int32 Row);

	/** WCAG 대비(1~21). 두 색의 상대 휘도 비 — 글자 읽힘 시험용. */
	float ContrastRatio(const FLinearColor& A, const FLinearColor& B);

	/** 머리 위 이름표 알약 색 (DESIGN 2.2 「이름표」) — 8색(노랑·초록·파랑·분홍·주황·보라·청록·연두)을 번호 순서로 돌려 쓴다. */
	FLinearColor UiNameTagColor(int32 PlayerIndex);

	/** HUD 모서리 둥글기 — 시트 "모든 패널 모서리 둥글게 12~16px" 의 가운데 값. */
	constexpr float HudCornerRadius = 14.f;

	/** 고른 탭(`UiButtonHighlight` 바탕 + 진갈색 글)과 안 고른 탭(나무 바탕 + 크림 글)을 한 곳에서 칠한다 — 화면마다 따로 적으면 글자색이 어긋난다. */
	void MarkSelectedTab(UButton* Button, bool bSelected);

	/** 나무 표찰 탭 모서리 — 각진 2px (M101-01, 크림 알약 탭 대신). */
	constexpr float WoodTabRadius = 2.f;

	/** 나무 표찰 탭을 칠한다: 고른 탭 = 노란 강조(`UiButtonHighlight`) + 진갈색 글, 안 고른 탭 = 나무 바탕 + 크림 글. 고름 표시 밧줄 밑줄은 부르는 쪽이 따로 둔다. */
	void StyleWoodTab(UButton* Button, bool bSelected);

	/**
	 * 버튼 네 상태 브러시를 **둥근 네모**로 바꾼다(반지름 = 지름/2 이면 동그라미). 색은 건드리지 않는다 —
	 * 뒤에 부르는 `ApplyUiStyle` 이 팔레트 색만 덮으므로 모양은 남는다. `ApplyUiStyle` **앞**에 부른다.
	 * @param Opacity 버튼 바탕 불투명도(1 = 불투명). 월드가 비치는 반투명 버튼용.
	 */
	void MakeRoundButton(UButton& Button, float Radius, float Opacity = 1.f);

	/**
	 * 크림(#F5EBD2) 둥근 카드 — 시트의 알림·대상 카드 한 벌(M15-02·M15-03). 처음엔 **접혀** 있다 —
	 * 보일 거리가 생길 때 부르는 쪽이 편다. 사냥·목장 HUD 가 같은 모양을 쓰게 여기 둔다.
	 */
	UBorder* MakeCreamCard(UWidgetTree& Tree, const TCHAR* Name);

	/**
	 * 나무 표찰 카드(M99-02) — 나무 `UiWoodButton` 바탕 + 밧줄 `UiRope` 테두리 + 모서리 4px. 안의 글은 크림으로 칠한다.
	 * `MakeCreamCard` 와 같이 처음엔 접혀 있다. 크림 둥근 말풍선과 다른 인상이 필요한 안내 카드용.
	 */
	UBorder* MakeWoodSignCard(UWidgetTree& Tree, const TCHAR* Name);

	/** `AddMenuPanel` 이 세운 것 — 판·제목 탭·탭 글자. */
	struct FMenuPanelParts
	{
		UBorder* Panel = nullptr;
		UBorder* TitlePill = nullptr;
		UTextBlock* TitleText = nullptr;
	};

	// 로비·일시정지 판 규격(M15-24) — 시트가 없는 화면이라 `hud_ingame`·`inventory_backpack` 규격을 준용한다:
	// 크림 판 `UiPanelCream` · 모서리 `HudCornerRadius` · 테두리·그림자 없음 · 제목 탭(`UiTitleTab` 알약)이 판 위로 걸침.
	constexpr float MenuTitlePillHeight = 44.f;
	constexpr float MenuTitlePillRise = 20.f;

	/**
	 * 캔버스 위쪽 가운데에 크림 판과 그 위로 걸치는 제목 탭을 넣는다. **버튼보다 먼저** 부른다 —
	 * 캔버스는 나중에 넣은 것이 위에 그려진다. 판은 눌림을 먹지 않는다(`HitTestInvisible`).
	 * @param PanelTop 판 위쪽 변의 세로 자리(px). 탭은 그 위로 `MenuTitlePillRise` 만큼 나온다.
	 */
	FMenuPanelParts AddMenuPanel(UWidgetTree& Tree, UCanvasPanel& Root, const FText& Title, float PanelTop,
		float PanelWidth, float PanelHeight, float PillWidth);

	// 판 제목 알약(M24-02~04) — 배낭·제작·출석 판과 같은 규격. 판 왼쪽 위에 걸쳐 `MenuTitlePillRise` 만큼 위로 나온다.
	/** DESIGN 7.1 머리 표찰 모서리 4px — 알약(높이/2)이 아니라 거의 각진 나무 표찰. */
	constexpr float PanelTitlePlateRadius = 4.f;
	constexpr float PanelTitlePillMinWidth = 130.f;
	constexpr float PanelTitlePillHeight = 60.f;
	constexpr float PanelTitlePillFontSize = 28.f;   // 10-16 「머리표 13px」 → 폰 616 높이에서 약 21px
	constexpr float PanelTitlePillPaddingX = 24.f;

	/**
	 * 진갈색(`UiTitleTab`) 알약에 제목 글자를 담아 상자(최소 폭 130·높이 50)를 돌려준다. 자리는 부르는 쪽이 정한다 —
	 * **판보다 뒤에** 넣어야 판 위에 그려진다. 긴 제목은 알약이 늘어난다. 눌림을 먹지 않는다.
	 */
	USizeBox* MakePanelTitlePill(UWidgetTree& Tree, FName BoxName, FName PillName, UTextBlock& Title);

	/** 글자 크기만 바꾼다(글꼴·색은 `ApplyUiStyle` 몫). */
	void SetFontSize(UTextBlock& Text, float Size);
	/** 나무 단추·줄 위 가격 글(M97-c-5) — 크림 글 + 진갈색 외곽선 2px. 초록은 나무 위에서 안 읽혔다(감독 c-1 검수). */
	void StylePriceOnWood(UTextBlock& Text);

	// DESIGN 8.12 "등급 색" — 일반 초록 #6CCB8A · 레어 주황 #F0895A · 에픽 보라 #A46BE0 · 레전더리 #F2C14E.
	FLinearColor ItemGradeColor(EItemGrade Grade);
	/** 칸 아래 등급 띠 색(M101-04) — 흔함 회갈색·드묾 초록·귀함 파랑·전설 금색. 원판 색(`ItemGradeColor`)과 다르다. */
	FLinearColor ItemGradeBandColor(EItemGrade Grade);
	/** 칸 아래 1/5 에 등급 띠를 얹는다(배낭·상점·제작 칸 공통, M101-04). `Stack` 은 칸 크기 `CellSize` 의 오버레이. */
	void AddGradeBand(UWidgetTree& Tree, UOverlay& Stack, FName Name, const FLinearColor& Color, float CellSize);

	// 전체 화면 판 (#0174 도감 → M13-31 업적). 레퍼런스 도감처럼 월드·HUD 를 판 하나로 덮는다 — 글자가
	// 미니맵·조이스틱·풀밭 위에 겹치면 못 읽는다. **두 화면이 이 값 한 벌을 쓴다** — 화면마다 적으면 하나만 달라진다.
	// 색은 배낭·상점 판과 같은 크림(`UiPanelCream`)·진갈색 테두리(M25-01, 시트 「패널 크림 #F5EBD2 · 글자 진갈색 #4A3826」) —
	// #0174 의 진갈색 판은 M23·M24 뒤 다른 판이 전부 크림이라 이 화면들만 색이 달랐다. 판 위 글자는 `ScreenTextColor` 로 칠한다.
	/** 판 바깥 어둡게 — 판 가장자리 틈으로 HUD 가 비치지 않게. */
	constexpr float ScreenBackdropDimOpacity = 0.6f;
	/** 판은 거의 불투명 — 뒤 월드가 글자 사이로 비치지 않을 만큼. */
	constexpr float ScreenPanelOpacity = 0.95f;
	/** 화면 가장자리에서 판까지 틈. 테두리가 화면 끝에 붙어 잘려 보이지 않게. */
	constexpr float ScreenPanelInset = 40.f;
	/** DESIGN 7.1 판 모서리 6px (예전 16) — 둥근 알약·카드 인상을 끊는다. */
	constexpr float ScreenPanelRadius = 6.f;
	/** DESIGN 7.1 밧줄 테두리 두께 5px, 그 안쪽 나무 틀 폭 14px, 그림자 오른쪽 아래 4px(불투명도 0.25 `#2B1B10`). */
	constexpr float ScreenRopeWidth = 5.f;
	constexpr float ScreenWoodFrameWidth = 14.f;
	/** M97-c-2: HUD 작은 판(체력 판자·번개 눈금판·카드)은 모서리 6px, 밧줄 테두리 3px (DESIGN 7.1). */
	constexpr float WoodPlateRadius = 6.f;
	constexpr float HudRopeWidth = 3.f;
	constexpr float ScreenShadowOffset = 4.f;
	constexpr float ScreenShadowOpacity = 0.25f;
	constexpr float ScreenPanelOutlineWidth = 4.f;
	/** 메뉴 판(제작·상점·의뢰·선물·배낭) 누르는 칸 짧은 변 최소 — 애플 44pt = 1080 기준 약 121 (DESIGN 2 「누르는 칸 최소 크기」 10-16). */
	constexpr float MenuTouchMin = 121.f;
	/** HUD(사냥·목장·물·짓기) 누르는 칸 짧은 변 최소 — 화면을 가리지 않게 72 유지 (DESIGN 2, 감독 10-07). */
	constexpr float HudTouchMin = 72.f;
	/** 전체 화면 판 닫기 버튼 높이 (M38-03) — 10-16 72 → 121, 메뉴 판 누르는 칸 바닥과 같게 (DESIGN 2). 글 줄 높이(56)와 따로 둔다. */
	constexpr float ScreenCloseButtonHeight = MenuTouchMin;
	/** 메뉴 판 글자 최소 — 폰(616 높이, 배율 약 0.57)에서 17px 이상. */
	constexpr float MenuFontMin = 24.f;

	/** 메뉴 판 「닫기」 한 벌 — 나무 둥근 단추, 높이 `MenuTouchMin`, 글 `MenuFontMin`. 화면마다 모양이 달라지지 않게 여기서만 만든다. */
	USizeBox* MakeCloseButton(UWidgetTree& Tree, FName BoxName, FName ButtonName, const FText& Label, UButton*& OutButton);

	/**
	 * 꺼져도 글이 흐려지지 않는 단추 — 글을 단추 **안**이 아니라 위 겹(오버레이)에 둔다. Slate 는 꺼진 단추 속 글을
	 * 반투명으로 그려 회색 바탕 위에서 안 읽혔다. 글 이름 = `ButtonName` + "Text", 색은 `UiTextOnWood`.
	 */
	UOverlay* MakeClearLabelButton(UWidgetTree& Tree, FName ButtonName, const FText& Label, float FontSize,
		UButton*& OutButton, UTextBlock*& OutLabel);
	/** HUD 뷰포트 Z (사냥·목장 HUD·닫힌 지도 버튼). 출석 버튼(5)·다음 목표(5)는 이 위. */
	constexpr int32 HudViewportZOrder = 0;
	/** HUD 위로 펼친 줄(이모트 몸 동작·말풍선) Z — 같은 HUD Z 로 나중에 붙는 상인 「상점 열기」 카드가 줄을 덮지 않게 (INBOX 보정 10-06 02:45). */
	constexpr int32 HudPopupViewportZOrder = 1;
	/** 전체 화면 판(도감·업적·연 지도) 뷰포트 Z — HUD(0)·출석(5)·제작 판(1)보다 위라야 판 위로 HUD 버튼이 안 그려진다 (M26-03). */
	constexpr int32 ScreenPanelViewportZOrder = 10;
	/** 전체 화면 판 위 글자 색 — 진갈색(`UiTextOnButton`), `bDim`(도감 빈 칸·업적 미달 줄)이면 단색 `UiTextDim`. */
	FLinearColor ScreenTextColor(bool bDim = false);

	/**
	 * 캔버스 뿌리에 바깥 어둡게(전체) + 크림 판(가장자리 `ScreenPanelInset`)을 넣고 판을 돌려준다.
	 * **글자보다 먼저** 부른다 — 캔버스는 나중에 넣은 것이 위에 그려진다. 둘 다 `Visible` 이라 눌림을 먹어
	 * 화면이 열린 동안 뒤 HUD 버튼·조이스틱이 안 눌린다.
	 */
	UBorder* AddScreenPanel(UWidgetTree& Tree, UCanvasPanel& Root, FName BackdropName, FName PanelName);

	// 임포트한 한글 글꼴(`Content/Game/UI/Fonts/Font_UI_Jua`). 에셋이 없으면 nullptr.
	UFont* GetUiFont();

	/** 조상 중에 `UScrollBox` 가 있는지 — 있으면 `ApplyUiStyle` 이 그 버튼을 정확한 탭에만 눌리게 바꾼다. */
	bool IsInsideScrollBox(const UWidget& Widget);

	/** 단추의 VoiceOver 이름 — 단추 안 글 → 같은 겹에 얹은 라벨 → 툴팁 순. `ApplyUiStyle` 이 모든 단추에 붙인다 (10-16 접근성). */
	FText ButtonAccessibleName(const UButton& Button);

	// 위젯 트리를 한 번 돌며 ① 모든 글자(`UTextBlock`)의 글꼴 ② 모든 버튼(`UButton`)의 네 가지 상태 색
	// ③ 버튼 **안**에 든 글자의 색을 팔레트로 맞춘다 ④ 스크롤 안 버튼은 `PreciseTap`·`PreciseClick` 으로
	// (끌면 스크롤, 탭하면 누름 — M22). 버튼 밖 글자 색은 손대지 않는다 — 표가 정한 것은
	// "버튼 위 글자" 뿐이라, 제목·HUD 숫자에까지 갈색을 칠하면 표에 없는 결정을 하는 것이 된다.
	// **크기·자간은 손대지 않는다** — 설계서에 없는 숫자를 만들지 않으려고 각 글자가 쓰던 값을 그대로 둔다.
	// 여러 번 불러도 안전하다(줄이 늘어난 뒤 다시 부르는 곳이 있다).
	// Jua 에 없는 글자(`·`·`—`·`…` 등 문장부호)는 `Font_UI_Jua` 복합 글꼴의 **대체 칸**(엔진 Roboto Bold)이
	//   그린다(M13-27, `tools/unreal/setup_ui_font_fallback.py`). 검사는 `SnarehavenUiStyleTest` 의 `CanUiFontDraw`.
	// `Super::RebuildWidget()` **뒤**에 부른다 — Slate 위젯(`STextBlock`·`SButton`)이 생긴 뒤라야 값이 화면까지 간다.
	void ApplyUiStyle(UUserWidget& Widget);
}
