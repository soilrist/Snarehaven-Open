#pragma once

#include "CoreMinimal.h"

// 설정 화면 「만든 사람·라이선스」 칸의 글 (ROADMAP M98-g · docs/ASSET_LICENSES.md 기준).
// 표기 의무가 있는 것(Jua 글꼴 SIL OFL 1.1 — 저작권 문구와 라이선스 원문을 사본에 넣어야 한다)과
// 표기를 권하는 CC0 팩·Mixamo·언리얼을 한 곳에 모은다. 순수 글자라 화면 없이 시험할 수 있다.
namespace SnareCredits
{
	/** 맨 위부터: 만든 사람 → 쓴 꾸러미 이름 → Jua OFL 원문. */
	SNAREHAVEN_API FString GetText();

	/** 칸에 반드시 들어가야 하는 이름들 — 시험이 글에 모두 있는지 본다. */
	SNAREHAVEN_API TArray<FString> GetRequiredNames();
}
