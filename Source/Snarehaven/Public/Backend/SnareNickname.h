#pragma once

#include "CoreMinimal.h"

// 닉네임 미리 거름 한 곳 (DESIGN 2.2 「이름표」 닉네임 줄). **판정은 서버** `tools/playfab/cloudscript_main.js`
// `SetNickname` 이 한다 — 여기는 같은 규칙으로 서버에 보내기 전에 막아, 뻔한 거절에 왕복을 쓰지 않게 할 뿐이다.
// 금칙어 원본은 그 파일의 `NICKNAME_BANNED_WORDS` 이고, 여기 사본이 원본과 같은지는 2층 `Snarehaven.Nickname` 이 본다.
namespace SnareNickname
{
	/** 서버 `NICKNAME_MIN_LENGTH`·`NICKNAME_MAX_LENGTH` 와 같은 값. */
	SNAREHAVEN_API extern const int32 MinLength;
	SNAREHAVEN_API extern const int32 MaxLength;

	/** 금칙어 사본 — 소문자. 원본 순서 그대로. */
	SNAREHAVEN_API const TArray<FString>& BannedWords();

	/** 낱말일 때만 막는 말 사본 — 서버 `NICKNAME_BANNED_TOKENS`, 원본 순서 그대로. */
	SNAREHAVEN_API const TArray<FString>& BannedTokens();

	/** 앞뒤 공백을 자른 이름. 서버 `args.name.trim()` 과 같다. */
	SNAREHAVEN_API FString Trim(const FString& RawName);

	/**
	 * 서버 `nicknameProblem` 과 같은 판정. 앞뒤 공백은 부르는 쪽이 자른다.
	 * @return 맞으면 빈 문자열, 아니면 서버 거절 코드(`bad_length`·`bad_chars`·`banned_word`)
	 */
	SNAREHAVEN_API FString ProblemCode(const FString& TrimmedName);

	/** 거절 코드의 문구 — 서버 `fail` 문구와 같은 말. 모르는 코드면 코드를 그대로. */
	SNAREHAVEN_API FString MessageFor(const FString& Code);

	/**
	 * PlayFab `UpdateUserTitleDisplayName` 거절의 한국어 문구 — PlayFab 영어 문구(「Invalid input parameters」)를 그대로
	 * 띄우면 한국 사람이 왜 안 되는지 모른다(사람 폰 10-08 한글 닉네임 결함).
	 * @param PlayFabErrorName `FPlayFabCppError::ErrorName` (`NameNotAvailable`·`InvalidParams`·`ProfaneDisplayName` …)
	 */
	SNAREHAVEN_API FString DisplayNameMessage(const FString& PlayFabErrorName);
}
