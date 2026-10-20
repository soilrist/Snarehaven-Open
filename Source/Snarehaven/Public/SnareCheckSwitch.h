#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// M95-a: 검사 전용 명령줄 스위치(`-Snare…`)를 읽는 유일한 길. 출시(Shipping) 빌드에서는 명령줄을 아예 안 읽고 늘 "스위치 없음"이라
// 폰에 설치된 앱이 어떤 인자로 켜져도 검사 길이 열리지 않는다. 소스에서 `FParse::…(FCommandLine::Get(), TEXT("Snare…"))` 를 직접 부르면
// `tools/check_shipping_switches.py` 가 막는다. 게임 동작은 스위치 없는 일반 플레이와 같다.
namespace SnareCheckSwitch
{
	/** `-이름` 형태 스위치가 명령줄에 있나. 출시 빌드는 늘 false. */
	inline bool Param(const TCHAR* Name)
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		return FParse::Param(FCommandLine::Get(), Name);
#endif
	}

	/** `-이름=값` 형태 스위치 읽기(숫자·문자열). 출시 빌드는 늘 false 이고 Out 을 안 건드린다. */
	template <typename T>
	inline bool Value(const TCHAR* Match, T& Out)
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		return FParse::Value(FCommandLine::Get(), Match, Out);
#endif
	}

	/** 쉼표가 든 문자열 값용 — `bShouldStopOnSeparator=false` 로 쉼표에서 끊지 않는다. */
	inline bool Value(const TCHAR* Match, FString& Out, bool bShouldStopOnSeparator)
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		return FParse::Value(FCommandLine::Get(), Match, Out, bShouldStopOnSeparator);
#endif
	}
}
