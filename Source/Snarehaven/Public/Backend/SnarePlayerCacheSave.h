// ROADMAP M2-05 — 로컬 캐시 파일의 **저장 형식**.
// 이 파일은 세이브가 아니라 **캐시**다 (DESIGN 4.5): 앱을 켜자마자 지난번 화면을 보여 주기 위한 것이고,
// 백엔드 응답이 오면 통째로 덮어쓴다. 여기 있는 값으로 무언가를 판정하지 않는다 — 판정은 전부 백엔드다.
#pragma once

#include "CoreMinimal.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "GameFramework/SaveGame.h"

#include "SnarePlayerCacheSave.generated.h"

/**
 * `Saved/SaveGames/` 에 놓이는 캐시 한 장.
 * 읽는 규칙(`TryReadSnapshot`)을 클래스 안에 같이 둔 이유: 쓰는 쪽과 읽는 쪽이 갈라지면
 * 형식을 바꿀 때 한쪽만 고쳐 놓고 조용히 남의 계정 물건을 보여 주는 사고가 난다.
 */
UCLASS()
class SNAREHAVEN_API USnarePlayerCacheSave : public USaveGame
{
	GENERATED_BODY()

public:
	/** 캐시 파일 이름. 계정마다 파일을 나누지 않는다 — 기기 한 대에 게스트 계정은 하나다 (DESIGN 4.4). */
	static const TCHAR* GetSlotName();

	/**
	 * 형식 번호. 칸을 더하거나 뜻을 바꾸면 **반드시 올린다** — 번호가 다른 캐시는 읽지 않고 버린다.
	 * 밸런스 숫자가 아니라 파일 형식 번호라 데이터 테이블로 빼지 않는다 (DataDriven 1 의 대상이 아님).
	 */
	// 2 = 젬 잔액(`GemBalance`) 칸이 늘었다 (M7-03). 1 로 쓰인 캐시는 읽지 않고 버린다.
	// 3 = 도감 칸(`CodexEntries`) 이 늘었다 (M10-05). 2 로 쓰인 캐시는 도감이 빈 채로 실릴 텐데,
	//     그러면 다 모은 사람의 화면이 0/9 로 뜬다 — 지난 캐시를 버리고 백엔드에 다시 묻는 쪽이 맞다.
	// 4 = 업적 칸(`Achievements`) 이 늘었다 (M12-04). 도감과 같은 이유로 3 캐시는 버린다.
	// 5 = 탈것 칸(`Mounts`·`MountSaddles`) 이 늘었다 (M13-03). 4 캐시는 탈것이 빈 채로 실려 "못 탄다" 가 뜬다 — 버린다.
	static constexpr int32 CurrentFormatVersion = 5;

	/** 스냅샷을 캐시 객체로 감싼다. 디스크 쓰기는 부르는 쪽(`USnareBackendSubsystem`)이 한다. */
	static USnarePlayerCacheSave* MakeFrom(const FSnarePlayerSnapshot& InSnapshot);

	/**
	 * 캐시를 믿어도 되는지 보고, 믿을 수 있을 때만 옮겨 담는다.
	 * 버리는 경우 셋: ① 객체가 없다 ② 형식 번호가 다르다 ③ 지금 로그인한 계정의 것이 아니다.
	 * @param ExpectedPlayFabId 지금 계정. 아직 로그인 전이라 모를 때는 빈 문자열을 주면 주인 검사를 건너뛴다
	 * @param OutSnapshot       믿을 수 있을 때만 채워진다
	 * @return 옮겨 담았으면 true
	 */
	static bool TryReadSnapshot(const USnarePlayerCacheSave* Cache, const FString& ExpectedPlayFabId,
		FSnarePlayerSnapshot& OutSnapshot);

	/** 이 파일이 어떤 형식으로 쓰였는지. */
	UPROPERTY()
	int32 FormatVersion = CurrentFormatVersion;

	/** 마지막으로 백엔드에게 들은 상태. */
	UPROPERTY()
	FSnarePlayerSnapshot Snapshot;
};
