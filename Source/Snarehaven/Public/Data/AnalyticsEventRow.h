#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AnalyticsEventRow.generated.h"

// ROADMAP M8-01: 분석 이벤트 하나를 적은 데이터 한 줄 (DT_AnalyticsEventTable 의 행 구조체).
// 무엇을 언제 누가 보내는지의 원본은 `docs/ANALYTICS.md` 표 A 이고, 이 표는 그 사본이다 —
// 둘이 어긋나면 2층 `Snarehaven.Analytics.EventTable.MatchesDocument` 가 운다 (M7-02 와 같은 방식).
//
// **이름을 코드에 박지 않으려고 만든 표다** (DataDriven.md 1). 보내는 코드(M8-02~06)가 쓰는 이름은
// 전부 이 표의 `EventID` 여야 하고, 표에 없는 이름을 보내면 같은 2층 검사가 잡는다.
// **지금 이벤트를 보내는 코드는 0줄이다** — 표가 먼저 서고 통로(M8-02)와 부르는 자리(M8-03~06)가 뒤따른다.
//
// **숫자는 이 표에 없다** — 샘플링 비율·보관 기간 같은 값은 `docs/ANALYTICS.md` 가 범위 밖으로 못 박았다.

/** 이벤트를 누가 보내는가. 같은 사건을 두 쪽이 보내면 한 번 일어난 일이 두 번 세어진다 (DESIGN 4.1 권위 2단). */
UENUM(BlueprintType)
enum class EAnalyticsEventSender : uint8
{
	/** 클라이언트 아무나 — 방과 상관없는 자기 사건 (로그인 같은 것). */
	Client,

	/** 호스트 권위 자리에서만 — 게스트도 보내면 사건이 두 번 세어진다. */
	HostAuthority,

	/** 요청한 본인만 — "눌렀다" 까지가 이벤트이고, 결과는 백엔드가 쓴다. */
	Requester,

	/** 관찰만 — 이벤트를 보내는 코드가 값을 바꾸지 않는다 (재화 잔액). */
	Observer,
};

USTRUCT(BlueprintType)
struct FAnalyticsEventRow : public FTableRowBase
{
	GENERATED_BODY()

	/** PlayFab 에 그대로 나가는 이벤트 이름. **밑줄 꼴**이다 (`animal_killed`) — 이유는 `docs/ANALYTICS.md` 표 B. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Analytics")
	FName EventID;

	/** 보내는 쪽. `docs/ANALYTICS.md` 표 A 의 "보내는 쪽" 칸과 같아야 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Analytics")
	EAnalyticsEventSender Sender = EAnalyticsEventSender::Client;

	/** 이벤트가 싣는 칸 이름들 (payload). 값은 여기 적지 않는다 — 보내는 자리에서 채운다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Analytics")
	TArray<FName> PayloadKeys;
};
