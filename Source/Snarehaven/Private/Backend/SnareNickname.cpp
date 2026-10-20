#include "Backend/SnareNickname.h"

namespace SnareNickname
{
	const int32 MinLength = 3;
	const int32 MaxLength = 12;

	const TArray<FString>& BannedWords()
	{
		static const TArray<FString> Words = {
			TEXT("시발"), TEXT("씨발"), TEXT("씨빨"), TEXT("시빨"), TEXT("씨팔"), TEXT("시팔"), TEXT("씹"), TEXT("좆"), TEXT("존나"), TEXT("졸라"),
			TEXT("병신"), TEXT("븅신"), TEXT("븅딱"), TEXT("병딱"), TEXT("등신"), TEXT("개새"), TEXT("새끼"), TEXT("쌔끼"), TEXT("썅"), TEXT("개년"),
			TEXT("개놈"), TEXT("미친놈"), TEXT("미친년"), TEXT("애미"), TEXT("애비"), TEXT("느금"), TEXT("느그"), TEXT("니미"), TEXT("엠창"), TEXT("앰창"),
			TEXT("창녀"), TEXT("걸레"), TEXT("보지"), TEXT("자지"), TEXT("섹스"), TEXT("야동"), TEXT("강간"), TEXT("지랄"), TEXT("후장"), TEXT("닥쳐"),
			TEXT("운영자"), TEXT("관리자"), TEXT("개발자"), TEXT("한남충"), TEXT("김치녀"), TEXT("일베"), TEXT("맘충"), TEXT("틀딱"), TEXT("된장녀"), TEXT("노무현"),
			TEXT("문재인"), TEXT("박근혜"), TEXT("이명박"), TEXT("윤석열"), TEXT("이재명"), TEXT("김대중"), TEXT("김영삼"), TEXT("전두환"), TEXT("노태우"), TEXT("박정희"),
			TEXT("이승만"), TEXT("김정은"), TEXT("김정일"), TEXT("김일성"), TEXT("트럼프"), TEXT("바이든"), TEXT("시진핑"), TEXT("푸틴"), TEXT("자살"), TEXT("자해"),
			TEXT("killyourself"), TEXT("killurself"), TEXT("suicide"), TEXT("fuck"), TEXT("fck"), TEXT("fack"), TEXT("phuck"), TEXT("fvck"), TEXT("shit"), TEXT("bitch"),
			TEXT("biatch"), TEXT("pussy"), TEXT("porn"), TEXT("nigger"), TEXT("nigga"), TEXT("faggot"), TEXT("whore"), TEXT("slut"), TEXT("retard"), TEXT("hitler"),
			TEXT("asshole"), TEXT("rapist")
		};
		return Words;
	}

	const TArray<FString>& BannedTokens()
	{
		static const TArray<FString> Tokens = {
			TEXT("cunt"), TEXT("sex"), TEXT("dick"), TEXT("rape"), TEXT("kys"), TEXT("fag"), TEXT("cum"), TEXT("nazi"), TEXT("admin"), TEXT("fuk"),
			TEXT("fuc"), TEXT("ass"), TEXT("pedo"), TEXT("cock")
		};
		return Tokens;
	}

	FString Trim(const FString& RawName)
	{
		return RawName.TrimStartAndEnd();
	}

	namespace
	{
		/** 서버 `/^[가-힣A-Za-z0-9]+$/` 의 한 글자. */
		bool IsAllowedChar(const TCHAR Ch)
		{
			return (Ch >= TEXT('가') && Ch <= TEXT('힣')) || (Ch >= TEXT('A') && Ch <= TEXT('Z'))
				|| (Ch >= TEXT('a') && Ch <= TEXT('z')) || (Ch >= TEXT('0') && Ch <= TEXT('9'));
		}
	}

	FString ProblemCode(const FString& TrimmedName)
	{
		if (TrimmedName.Len() < MinLength || TrimmedName.Len() > MaxLength)
		{
			return TEXT("bad_length");
		}
		for (const TCHAR Ch : TrimmedName)
		{
			if (!IsAllowedChar(Ch))
			{
				return TEXT("bad_chars");
			}
		}
		// 서버처럼 소문자로, 그리고 숫자를 뺀 꼴에서도 찾는다(「시1발」).
		const FString Lower = TrimmedName.ToLower();
		FString Bare;
		for (const TCHAR Ch : Lower)
		{
			if (!FChar::IsDigit(Ch))
			{
				Bare.AppendChar(Ch);
			}
		}
		for (const FString& Word : BannedWords())
		{
			if (Lower.Contains(Word, ESearchCase::CaseSensitive) || Bare.Contains(Word, ESearchCase::CaseSensitive))
			{
				return TEXT("banned_word");
			}
		}
		// 낱말 목록은 영문 조각(대소문자 경계·숫자·한글에서 끊음)과 통째로 같을 때만 — 서버 `nicknameHasBannedWord` 의 첫 갈래.
		// ponytail: 서버의 한글 모음 접기·글자 반복·숫자→글자 꼴은 안 따라 한다 — 덜 막을 뿐 최종 판정은 서버.
		FString Token;
		for (int32 Index = 0; Index <= TrimmedName.Len(); ++Index)
		{
			const TCHAR Ch = Index < TrimmedName.Len() ? TrimmedName[Index] : TEXT('\0');
			const bool bLatin = (Ch >= TEXT('A') && Ch <= TEXT('Z')) || (Ch >= TEXT('a') && Ch <= TEXT('z'));
			const bool bCamelBreak = bLatin && FChar::IsUpper(Ch) && Index > 0 && FChar::IsLower(TrimmedName[Index - 1]);
			if ((!bLatin || bCamelBreak) && !Token.IsEmpty())
			{
				if (BannedTokens().Contains(Token.ToLower()))
				{
					return TEXT("banned_word");
				}
				Token.Reset();
			}
			if (bLatin)
			{
				Token.AppendChar(Ch);
			}
		}
		return FString();
	}

	FString MessageFor(const FString& Code)
	{
		if (Code == TEXT("bad_length"))
		{
			return FString::Printf(TEXT("닉네임은 %d~%d자"), MinLength, MaxLength);
		}
		if (Code == TEXT("bad_chars"))
		{
			return TEXT("닉네임은 한글·영문·숫자만");
		}
		if (Code == TEXT("banned_word"))
		{
			return TEXT("쓸 수 없는 말이 들어 있다");
		}
		return Code;
	}

	FString DisplayNameMessage(const FString& PlayFabErrorName)
	{
		if (PlayFabErrorName == TEXT("NameNotAvailable"))
		{
			return TEXT("누가 이미 쓰는 닉네임이다");
		}
		if (PlayFabErrorName == TEXT("InvalidParams"))
		{
			return MessageFor(TEXT("bad_length"));
		}
		if (PlayFabErrorName == TEXT("ProfaneDisplayName"))
		{
			return MessageFor(TEXT("banned_word"));
		}
		return TEXT("닉네임을 저장하지 못했다 — 잠시 뒤 다시");
	}
}
