#pragma once

#include "CoreMinimal.h"

class ASnarehavenCharacter;
class UDataTable;
class UWorld;
struct FUsageHintRow;
enum class EPlaceableFunction : uint8;
enum class ESnareInteractVerb : uint8;

// 처음 들기·처음 다가가기 안내 한 줄의 표 찾기 + 「본 키」 저장 (M28-05a, `docs/specs/onboarding-usage-hints.md`).
// 띄우는 쪽(HUD 알약)은 M28-05b·c 가 이 함수들만 부른다.
// 🟡 본 키는 PlayFab 이 아니라 **이 기기 `GameUserSettings.ini`** — 첫 실행 안내 `USnareFirstRunGuideWidget::IsGuideSeen` 과 같은 곳.
// 재화·인벤토리·타이머가 아니라 규칙 8(백엔드 판정) 밖이고, 계정에 옮길 땐 `IsHintSeen`·`MarkHintSeen` 속만 바꾼다.
namespace SnareUsageHints
{
	SNAREHAVEN_API extern const TCHAR* const TablePath;

	/** 게임이 쓰는 ini 섹션. 시험은 `TestSection` 을 넘겨 이 기기의 진짜 기록을 건드리지 않는다. */
	SNAREHAVEN_API extern const TCHAR* const DefaultSection;
	SNAREHAVEN_API extern const TCHAR* const TestSection;

	/** `TablePath` 를 읽는다. 없으면 nullptr. */
	SNAREHAVEN_API const UDataTable* LoadTable();

	/** 그 키의 행. 없으면 nullptr. */
	SNAREHAVEN_API const FUsageHintRow* FindRow(const UDataTable* Table, FName Key);

	/**
	 * 손에 든 도구 → 안내 키. 행 이름 `tool.<ItemID>` 가 있으면 그것, 아니면 `AlsoItemIDs` 에 그 ItemID 가 든 행.
	 * 표에 없는 도구(맨손·무기 아닌 것)는 NAME_None.
	 */
	SNAREHAVEN_API FName FindKeyForItem(const UDataTable* Table, FName ItemID);

	/**
	 * 안내서(M28-06) 쪽 키 — `GuideTitle` 이 있는 행만, `GuideOrder` 작은 순(같으면 키 이름 순).
	 * 한 줄 안내와 같은 활동은 그 행(`tool.*`·`place.*`·`ranch.slot`·`vendor.shop`), 계기 없는 활동은 `guide.*` 행.
	 */
	SNAREHAVEN_API TArray<FName> GetGuidePageKeys(const UDataTable* Table);

	/** 놓인 물건의 기능 → 안내 키 `place.<기능>`. 표에 그 행이 없으면(가구·텐트·배) NAME_None. */
	SNAREHAVEN_API FName FindKeyForPlaceable(const UDataTable* Table, EPlaceableFunction Function);

	/** 이 기기에서 그 키를 이미 봤는가. */
	SNAREHAVEN_API bool IsHintSeen(FName Key, const TCHAR* Section = DefaultSection);

	/** 그 키를 봤다고 적고 바로 디스크에 쓴다(앱이 꺼져도 남게). 이미 있으면 그대로. */
	SNAREHAVEN_API void MarkHintSeen(FName Key, const TCHAR* Section = DefaultSection);

	/** 그 섹션의 본 목록을 지운다 — 시험 앞뒤 전용. */
	SNAREHAVEN_API void ResetForTest(const TCHAR* Section = TestSection);

	// ── 띄우는 쪽(사냥 HUD·목장 HUD) 공용 판정 (M28-05c2) — 두 화면이 같은 규칙으로 뜨고 같은 본 목록을 쓴다. ──

	/**
	 * 다가가기 키 — 목장 칸 동사(수확·넣기)면 `ranch.slot`(버튼이 칸을 가리킴, 상인보다 먼저), 곁의 기능(`NearFunction`)이
	 * 있으면 `place.<기능>`(M42-04 — 「열기」 대상 놓인 물건·물가 「배 띄우기」 자리), 잡화점 대화 거리면 `vendor.shop`.
	 * 상인 줄 글은 잡화점 것이라 낚시·농사·옷 상인은 계기가 아니다(명세 7).
	 */
	SNAREHAVEN_API FName PickApproachKey(ESnareInteractVerb Verb, bool bNearGeneralShop, EPlaceableFunction NearFunction);

	/**
	 * 그 캐릭터의 지금 다가가기 키 — 상황 버튼 동사(`GetInteractVerb`) + 「열기」 대상의 기능(`GetInteractTargetFunction`),
	 * 없으면 물가에서 배를 띄울 수 있는지(`CanLaunchBoat` — 배낭 「배 띄우기」 버튼과 같은 판정) + 잡화점(의뢰인 뺌) 대화 거리.
	 * 없으면 NAME_None.
	 */
	SNAREHAVEN_API FName FindApproachKey(const ASnarehavenCharacter* Character);

	/** 지금 띄우면 안 되는가 — 이 월드에 안 끝난 첫 실행 안내가 있거나 판(ESC 닫기 목록)이 열려 있음(명세 2 「실패·예외」). */
	SNAREHAVEN_API bool IsBlocked(UWorld* World);

	/** 쓸 ini 섹션 — 넘긴 게 있으면 그것, 자동 시험 중이면 `TestSection`(다른 HUD 시험이 진짜 기록을 안 건드리게), 아니면 `DefaultSection`. */
	SNAREHAVEN_API const TCHAR* ResolveSection(const TCHAR* Override);

	/**
	 * 그 키를 지금 띄울 수 있으면 본 것으로 적고 행을 돌려준다. 글이 비었거나·시간 0·이미 봤으면 nullptr(아무것도 안 적음).
	 * 막힘(`IsBlocked`)·한 번에 하나는 부르는 쪽이 먼저 본다.
	 */
	SNAREHAVEN_API const FUsageHintRow* ClaimRow(const UDataTable* Table, FName Key, const TCHAR* Section);
}
