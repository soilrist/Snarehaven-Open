#pragma once

#include "CoreMinimal.h"

enum class ESnareBackendFetchState : uint8;
enum class ESnareBackendLoginState : uint8;
class UCanvasPanel;
class USnareBackendSubsystem;
class UTextBlock;
class UWidgetTree;

// ROADMAP M7-03: 사냥 HUD·목장 HUD **두 화면**이 재화 잔액을 같은 글·같은 자리로 보여주게 하는 곳.
// `SnareUiStyle` 과 같은 취지다 — 자리표시 트리를 세우는 코드가 위젯마다 따로 있어서, 문구와 좌표를
// 각자 적으면 한 화면만 다른 모습이 되거나 한쪽만 고쳐져 조용히 어긋난다.
//
// **여기에 잔액을 바꾸는 길은 없다.** 읽는 것은 `USnareBackendSubsystem::GetPlayerSnapshot()` 하나뿐이고,
// 그 값을 바꾸는 유일한 자리는 `ApplyPlayerSnapshot`(백엔드 응답·로컬 캐시)이다
// (DESIGN 4.1 권위 2단 표 · 4.8 · CLAUDE.md 8). 화면은 숫자를 더하지도 깎지도 않는다.
namespace SnareBalanceHud
{
	/**
	 * 화면에 적히는 잔액 한 줄 — `코인 120   젬 3`.
	 * **백엔드에게 아직 못 들었으면 숫자 대신 까닭 한 줄** (주인 없는 스냅샷 = 응답도 캐시도 없다) —
	 * 오는 중이면 `불러오는 중…`, 안 올 거면 `서버 연결 안 됨` (DESIGN 3.3 화면 비우기 표, M21-01d).
	 * 0 으로 보여 주면 "잔액이 0원" 과 "아직 모른다" 가 같은 화면이 된다 (Verification.md 7).
	 * @param Backend 잔액의 출처. nullptr 이면(게임 인스턴스가 없는 2층 검사 월드 등) `서버 연결 안 됨`
	 */
	FText FormatBalanceText(const USnareBackendSubsystem* Backend);

	/** 잔액을 모르고 기다려도 안 온다 — 글이 `서버 연결 안 됨` 인 때. 칩은 돈 그림 대신 경고 표시를 단다(재화로 읽히지 않게). */
	bool IsBalanceOffline(const USnareBackendSubsystem* Backend);

	/** 잔액을 아직 모르지만 곧 받을 상태인가 — 로그인 중이거나 로그인 뒤 보유 상태 왕복이 안 끝났다. */
	bool IsBalanceLoading(ESnareBackendLoginState Login, ESnareBackendFetchState Fetch);

	/**
	 * 자리표시 트리 **오른쪽 위**에 남색 알약(M15-02, 시트 `hud_ingame.md`)을 얹고 그 안 잔액 글자를 돌려준다.
	 * WBP 에 디자인이 들어오면 안 불린다. 두 화면이 같은 자리·모양을 쓰게 하려고 여기서 정한다.
	 * @param OutZoneText 주면 알약 앞머리에 존 이름 글자(`PillZoneText`)를 세워 돌려준다 — 채우는 것은 부른 쪽.
	 */
	UTextBlock* AddPlaceholderRow(UWidgetTree& Tree, UCanvasPanel& Root, UTextBlock** OutZoneText = nullptr);
}
