#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

class UWorld;
struct FKey;

/**
 * ESC·안드로이드 뒤로 버튼으로 열린 창을 닫는 **한 곳** (INBOX 결함 2026-10-04 13:50).
 * Slate 입력 전처리기라 글자 칸에 포커스가 있어도, 입력 모드가 무엇이어도 키를 먼저 받는다.
 * **게임 세계가 있으면 키를 항상 먹는다** (INBOX 2026-10-04 15:40 — 흘려보내면 에디터가 ESC 를 「플레이 중지」로
 * 받아 게임이 꺼졌다). 닫은 창이 없으면 내 캐릭터의 일시정지 메뉴를 연다(로비·타이틀은 메뉴가 없어 아무 일 없음).
 * 게임을 끄는 길은 일시정지 메뉴의 나가기 버튼 하나뿐이다.
 * 각 화면의 닫기는 X 버튼과 같은 함수다 — 백엔드 응답 대기(bPending)는 건드리지 않아 응답은 그대로 처리된다.
 */
class SNAREHAVEN_API FSnareScreenCloser : public IInputProcessor
{
public:
	/** 월드를 안 주면 키를 받을 때마다 지금 도는 게임 월드(PIE·패키지 게임)를 찾는다 — 모듈 전역 등록용. */
	explicit FSnareScreenCloser(UWorld* InWorld = nullptr) : World(InWorld) {}

	static bool IsCloseKey(const FKey& Key);

	/**
	 * 이 월드에 열린 창을 닫는다. 맨 위 하나 규칙: 일시정지 메뉴 → 업적(닫으면 도감이 돌아온다) → 안내서 → 옷 상점(완료 창·확인 창 먼저) → 옷장(배낭으로 돌아간다) → 상자 열기(배낭으로 돌아간다) → 나머지 화면 전부
	 * (상점+배낭처럼 같이 열린 판은 같이 닫힌다). 닫은 게 있으면 true.
	 */
	static bool CloseOpenScreens(UWorld* InWorld);

	/** 이 월드에 `CloseOpenScreens` 가 닫을 창이 하나라도 열려 있는가 — 닫지 않고 보기만 (M28-05b 안내 한 줄이 판 위에 안 뜨게). */
	static bool HasOpenScreen(UWorld* InWorld) { return CountOpenScreens(InWorld) > 0; }

	/** 이 월드에 열린 창 수 — `CloseOpenScreens` 와 같은 목록 (상자 판이 배낭 위에 뜨면 2). */
	static int32 CountOpenScreens(UWorld* InWorld);

	/** 판 열기·닫기 소리 이름 (M45-05 · DESIGN 2.2 「거점·상자 소리」) — 열린 창 수가 늘면 `Ui_Open`, 줄면 `Ui_Close`, 같으면 없음. */
	static FName PanelSoundCue(int32 OpenBefore, int32 OpenAfter);

	/** 판 열기·닫기 크기 0.5 (DESIGN 2.2 — 모든 판마다 나니 가장 작게). */
	static float PanelSoundVolume() { return 0.5f; }

	/**
	 * 판 열기·닫기 소리의 **한 곳** — 열린 창 수를 지난번과 견줘 바뀌었으면 내 기기 `Play2D` 를 한 번 부르고 그 이름을 돌려준다.
	 * X 버튼·ESC·버튼으로 여는 길 어디로 바뀌어도 여기 하나가 듣는다(판마다 소리를 흩지 않는다). 월드가 바뀌면 0 에서 다시 센다.
	 */
	FName UpdatePanelSound(UWorld* InWorld);

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("SnareScreenCloser"); }

private:
	UWorld* FindGameWorld() const;

	// ponytail: 한 프로세스에 PIE 창이 여럿이면 ESC 한 번이 모든 창의 화면을 닫는다. 실제 기기는 월드 하나라 무관.
	TWeakObjectPtr<UWorld> World;

	// 판 열기·닫기 소리 — 지난번에 센 월드와 열린 창 수, 다음 셈까지 남은 시간(0.1초마다 센다).
	TWeakObjectPtr<UWorld> PanelSoundWorld;
	int32 LastOpenCount = 0;
	float PanelSoundWait = 0.f;
};
