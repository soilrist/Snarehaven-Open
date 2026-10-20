#pragma once

#include "CoreMinimal.h"

struct FRanchSlotState;
struct FRanchUpkeepState;

// 앱 밖 알림 예약 계산 (ROADMAP M21-04 · DESIGN 3.2 「앱 밖 알림」 표).
// 알림은 **안내일 뿐 지급이 아니다** — 재화·재고는 그대로 서버가 판정하고(CLAUDE.md 8), 여기서는 서버가 준
// 시각(마지막 수확·유지 끝)에서 "언제 폰을 울릴지" 만 정한다. 순수 함수만 둔다 — 2층 검사가 월드 없이 부른다.
// 실제 iOS 예약(엔진 로컬 알림 서비스)은 M21-04b 가 이 결과를 읽어 건다.
namespace SnareLocalNotify
{
	/** 알림 종류. */
	enum class EKind : uint8
	{
		/** 목장 칸이 미수확 상한까지 차서 생산이 멈췄다. */
		RanchFull,
		/** 목장 유지가 곧 끊긴다. */
		UpkeepLow
	};

	/** 숫자 묶음 — 기본값은 DESIGN 3.2 「앱 밖 알림」 표 그대로. 검사는 검사 전용 값을 따로 넣는다. */
	struct FRules
	{
		/** 마지막 수확부터 목장이 가득 차기까지(분) = 미수확 상한 5 × 생산 주기 30분. */
		int32 RanchFullMinutes = 150;
		/** 유지 끝 몇 시간 전에 알리나. */
		int32 UpkeepWarnHours = 6;
		/** 기기 현지 하루(자정~자정) 최대 알림 수. */
		int32 DailyCap = 2;
		/** 밤 금지 시작 시(현지, 이 시각부터 금지). */
		int32 QuietStartHour = 22;
		/** 밤 금지 끝 시(현지, 이 시각부터 허용). */
		int32 QuietEndHour = 8;
	};

	/** 예약할 알림 하나. 시각은 **현지 시각**이다(밤 금지·하루 상한이 현지 기준이라서). */
	struct FPlanned
	{
		EKind Kind = EKind::RanchFull;
		FDateTime FireLocal;
	};

	/** 현지 시각이 밤 금지 안이면 다음 허용 시작(QuietEndHour 정각)으로 민다. 밖이면 그대로. */
	SNAREHAVEN_API FDateTime PushOutOfQuietHours(const FDateTime& Local, const FRules& Rules);

	/**
	 * 예약할 알림 목록(시각 순). 꺼져 있으면 비운다.
	 * - 목장 가득 = 마지막 수확 + RanchFullMinutes · 유지 부족 = 유지 끝 − UpkeepWarnHours
	 * - 지금 이전(같으면 포함)이면 뺀다 — 이미 지난 일은 게임 안 HUD 가 보여 준다
	 * - 밤 금지 안이면 아침으로 밀고, 그날 상한(이미 예약한 것 포함)이 찼으면 다음 날 아침으로 민다
	 * - 민 유지 알림이 유지 끝을 넘으면 뺀다 — "곧 끊긴다" 가 거짓이 된다
	 * @param UtcToLocal  현지 − UTC (한국 +9시간)
	 * @param LastHarvestUtc  서버가 준 마지막 수확 시각, 모르면 nullptr
	 * @param UpkeepEndUtc  서버 남은 유지로 낸 유지 끝, 유지 전·잠김이면 nullptr
	 * @param AlreadyScheduledLocal  이미 걸려 있는(이번에 안 바꾸는) 알림 시각 — 하루 상한에 센다
	 */
	SNAREHAVEN_API void Plan(const FDateTime& NowUtc, const FTimespan& UtcToLocal, const FDateTime* LastHarvestUtc,
		const FDateTime* UpkeepEndUtc, const TArray<FDateTime>& AlreadyScheduledLocal, const FRules& Rules,
		bool bEnabled, TArray<FPlanned>& OutPlanned);

	/**
	 * 목장 스냅샷에서 `Plan` 이 받을 두 시각을 꺼낸다 (M21-04b). 서버 값을 읽기만 한다.
	 * - 마지막 수확 = 펫 있는 동물 칸 `OccupiedSinceUtc` 중 **가장 이른 것** — 서버 `HarvestRanch` 가 수확 때 이 시계를
	 *   남은 몫만 남기고 당기므로 칸마다 "마지막 수확" 이고, 가장 먼저 가득 차는 칸이 생산을 먼저 멈춘다
	 * - 유지 끝 = 지금 + 서버 남은 유지 시간(내림) — 내림이라 많아야 1시간 일찍 울린다
	 * - 목장이 잠겼으면 둘 다 없다(잠긴 동안은 서버가 칸 시계를 멈춘다) · 유지 시작 전이면 유지 끝이 없다
	 */
	SNAREHAVEN_API void ReadRanchTimes(const TArray<FRanchSlotState>& Slots, const FRanchUpkeepState& Upkeep,
		const FDateTime& NowUtc, TOptional<FDateTime>& OutLastHarvestUtc, TOptional<FDateTime>& OutUpkeepEndUtc);

	/** 예약을 실제로 거는 쪽. 엔진 로컬 알림 서비스를 감싼 것과 2층 검사의 가짜가 있다. */
	class ISink
	{
	public:
		virtual ~ISink() = default;
		/** 이 앱이 건 알림을 전부 지운다. */
		virtual void ClearAll() = 0;
		/** 현지 시각에 알림 하나를 건다. */
		virtual void Schedule(const FPlanned& Planned) = 0;
	};

	/**
	 * 지우고 다시 건다 (M21-04b). 목장 스냅샷이 바뀔 때마다 부른다 — 시각을 고칠 일만 있고 쌓을 일은 없어서.
	 * 꺼져 있으면 지우기만 한다. 하루 상한에는 **지난번에 걸었고 이미 울린(지금 이전) 알림**을 센다 —
	 * 다 지우고 다시 걸면 오늘 이미 울린 것을 잊어 하루 3통이 될 수 있어서. 그 기록이 `InOutSentLocal` 이다.
	 * @param InOutSentLocal  지난번에 건 현지 시각들 → 이번에 건 것 + 오늘·내일 안 지난 기록으로 바꿔 돌려준다
	 * @return 건 알림 수
	 */
	SNAREHAVEN_API int32 Reschedule(ISink& Sink, const FDateTime& NowUtc, const FTimespan& UtcToLocal,
		const TOptional<FDateTime>& LastHarvestUtc, const TOptional<FDateTime>& UpkeepEndUtc, const FRules& Rules,
		bool bEnabled, TArray<FDateTime>& InOutSentLocal);

	/**
	 * 게임이 부르는 한 줄 — 목장 스냅샷 → 엔진 로컬 알림 서비스(iOS·안드로이드, 맥 에디터엔 없어 로그만).
	 * 건 시각은 `GameUserSettings.ini` 에 남겨 다음 실행의 하루 상한에 센다. 처음 켜진 채 부를 때 알림 권한을 묻는다.
	 */
	SNAREHAVEN_API void RescheduleFromRanch(const TArray<FRanchSlotState>& Slots, const FRanchUpkeepState& Upkeep);

	/** 설정 「알림 켬/끔」. 처음 부를 때 ini 에서 읽는다(없으면 켬). */
	SNAREHAVEN_API bool IsEnabled();

	/** 켬/끔을 기기에 저장한다 (`GameUserSettings.ini`). */
	SNAREHAVEN_API void SetEnabled(bool bEnabled);

	/**
	 * 설정 카드 「알림 켬/끔」 단추 길 (M21-04b2). 켬/끔을 저장하고, **끄면 걸어 둔 알림을 바로 전부 지운다**
	 * (`Reschedule` 꺼짐 길 — 지난 기록도 오늘 이미 울린 것만 남긴다). 켜면 아무것도 걸지 않는다 — 시각을 낼 목장
	 * 스냅샷이 여기 없어서, 다음에 목장을 받을 때(`RescheduleFromRanch`) 다시 건다.
	 */
	SNAREHAVEN_API void ApplySetting(bool bEnabled);

	/** 2층 검사용 — 엔진 알림 서비스 대신 이 싱크로 지우고 건다. nullptr 이면 엔진 서비스로 돌아간다. */
	SNAREHAVEN_API void SetSinkForTest(ISink* Sink);
}
