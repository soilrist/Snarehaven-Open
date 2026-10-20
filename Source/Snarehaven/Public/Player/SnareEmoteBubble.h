#pragma once

#include "CoreMinimal.h"
#include "World/SnareSound.h"

// 머리 위 감정표현 말풍선 (ROADMAP M20-07 · DESIGN 2.2 「감정표현 말풍선」).
// 레퍼런스 채팅 대신 정해 둔 여섯 개만 고른다 — 사람이 쓴 글이 회선에 실리지 않아 신고·검열 거리가 없다.
// 회선에는 번호만 싣고 각 기기가 이 표로 글자를 고른다. 글자는 UI 글꼴(Jua)에 있는 것만 쓴다(그림 문자 없음).
namespace SnareEmoteBubble
{
	/** 말풍선이 떠 있는 시간(초). */
	constexpr float ShowSeconds = 3.f;

	/** 고를 수 있는 말풍선 수. */
	SNAREHAVEN_API int32 Num();

	/** 버튼에 쓰는 이름 — 손 흔들기·웃음·하트·놀람·물음·좋아요 순서. 범위 밖이면 빈 글. */
	SNAREHAVEN_API FText GetLabel(int32 Index);

	/** 말풍선 속 글자. 범위 밖이면 빈 글. */
	SNAREHAVEN_API FText GetGlyph(int32 Index);

	/** 말풍선 하나 → 소리 한 줄 (DESIGN 2.2 「말풍선 소리」, M54-02). 여섯 번호 모두 `EmotePop`, 띄운 사람 자리 월드 소리.
	 *  같은 사람이 `MinGapSeconds` 안에 다시 띄우면 소리만 건너뛴다. 범위 밖 번호는 무음(빈 이름). */
	SNAREHAVEN_API SnareSound::FChangeCue CueFor(int32 Index);
}
