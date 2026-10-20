#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"

class UDataTable;
class UTexture2D;

// 업적 달성·도감 새 칸 알림 규칙 (DESIGN 8.15 「달성 알림」, M48-02).
// 서버 판정 답의 `achievements.done`·`codex` 를 **지난 스냅샷과 비교한 차이**만 본다 — 코인은 서버가 이미 준 뒤라
// 카드는 결과 표시일 뿐이다(CLAUDE.md 8). 화면·소리는 부르지 않는 순수 함수라 시험이 그대로 부른다(연결은 M48-03).
namespace SnareAchievementNotice
{
	/** 한 답에서 새로 생긴 것. 순서는 새 답에 적힌 순서. */
	struct FDiff
	{
		TArray<FName> NewAchievements;
		TArray<FName> NewCodexEntries;

		bool IsEmpty() const { return NewAchievements.Num() == 0 && NewCodexEntries.Num() == 0; }
	};

	/**
	 * 지난 답과 새 답의 차이. `bFirstAnswer`(접속 뒤 첫 답 — 지난 스냅샷이 아직 기준이 아님)면 기준만 잡고 빈 차이.
	 * 지난 답에 있던 것이 새 답에서 빠진 것은 알리지 않는다(되돌림은 알림 거리가 아님).
	 */
	SNAREHAVEN_API FDiff Diff(const TArray<FName>& WasAchievements, const TArray<FName>& NowAchievements,
		const TArray<FName>& WasCodexEntries, const TArray<FName>& NowCodexEntries, bool bFirstAnswer);

	/** 알림 카드 한 장 — 왼쪽 아이콘 + 제목 + 설명 두 줄, 색 점은 `bAchievement`(금) / 도감(빨강). */
	struct FCard
	{
		bool bAchievement = false;
		FText Title;
		FText Body;
		/** 왼쪽 아이콘 48 — 업적 = 코인 그림, 도감 = 그 칸의 아이템·동물 그림. 비면 카드가 칸을 접고 글만 쓴다. */
		TSoftObjectPtr<UTexture2D> Icon;
	};

	/** 이름·코인을 찾을 곳. 없는 ID 는 이름 자리에 ID 그대로(빈 글 0), 코인을 모르면 「+N 코인」 을 붙이지 않는다. */
	struct FNames
	{
		TMap<FName, FText> AchievementNames;
		TMap<FName, int32> AchievementCoins;
		TMap<FName, FText> CodexNames;
		/** 도감 칸 그림 — 아이템은 표의 `IconRef`, 동물·변종은 그 펫 아이템 행의 그림(도감 화면과 같은 출처). */
		TMap<FName, TSoftObjectPtr<UTexture2D>> CodexIcons;
		/** 업적 카드 그림 — 배낭 하단 바·출석 판 코인 칸과 같은 재화 그림. */
		TSoftObjectPtr<UTexture2D> AchievementIcon;
	};

	/**
	 * 차이 → 카드 줄. 업적 먼저, 그다음 도감. 한 답에 최대 `MaxCards`(2)장 — 넘치면 마지막 카드 설명을
	 * 「<그 카드 이름> 외 <n>개」 로 합친다.
	 */
	SNAREHAVEN_API TArray<FCard> MakeCards(const FDiff& InDiff, const FNames& Names);

	/** 한 답에 소리 한 번 — 업적이 하나라도 있으면 `Achievement`, 도감만 있으면 `CodexNew`, 빈 차이면 None. */
	SNAREHAVEN_API FName SoundCue(const FDiff& InDiff);

	/** 그 소리의 「효과음」 곱 전 크기 — 업적 0.8 · 도감 0.6 · 무음 0. */
	SNAREHAVEN_API float SoundVolume(FName Cue);

	/** `DT_AchievementTable` 행 → 업적 이름·코인 칸 + 업적 카드 코인 그림. 표가 nullptr 이면 아무것도 안 넣는다. */
	SNAREHAVEN_API void AddAchievementNames(const UDataTable* AchievementTable, FNames& InOut);

	/**
	 * 도감 칸 이름·그림 — 동물 표(`AnimalID` → 이름 · `VariantPetItemID` → 「변종 <이름>」)와 아이템 표(`ItemID` → 이름).
	 * 그림은 아이템 표 `IconRef` — 종 행엔 그림이 없어 `PetItemID` 행의 그림을 빌린다(도감 화면 M25-02 와 같음).
	 * 도감 화면(`UCodexScreenWidget::BuildSlots`)이 칸에 붙이는 이름과 같은 출처다. nullptr 표는 건너뛴다.
	 */
	SNAREHAVEN_API void AddCodexNames(const UDataTable* AnimalTable, const UDataTable* ItemTable, FNames& InOut);

	/** 카드 줄 → HUD 알림 카드 한 칸에 넣을 글 — 카드마다 「제목 설명」 한 줄, 카드 사이는 줄바꿈. 빈 줄이면 빈 글. */
	SNAREHAVEN_API FText JoinCards(const TArray<FCard>& Cards);

	/** 한 답 최대 카드 수 (DESIGN 8.15 「쌓기」). */
	constexpr int32 MaxCards = 2;

	/** 업적 카드 코인 그림 경로 — `AttendanceScreenWidget`·`BackpackScreenWidget` 코인 칸과 같은 그림. */
	inline const TCHAR* const AchievementIconPath = TEXT("/Game/Game/UI/Icons/Items/T_Icon_CashBundle.T_Icon_CashBundle");

	/** 카드 한 장 보이는 초 (DESIGN 8.15 「보이는 시간」). */
	constexpr float CardSeconds = 3.f;
}
