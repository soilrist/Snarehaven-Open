#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// 서버 판정 함수(`USnareBackendSubsystem::CallServerFunction`) 응답 읽기 한 곳.
// CloudScript 의 `fail(code, message)` 는 **HTTP 성공 안에** `{error, code, ...}` 로 돌아온다 —
// bOk 만 보고 성공이라 믿으면 "재화 부족" 도 지급으로 그린다 (CLAUDE.md 8). 그래서 부르는 쪽은 전부 이걸 탄다.
// 상점·나룻배·도감은 화면 문장(DescribeFailure), 배낭·뽑기·전리품은 코드·사유(IsAccepted)를 쓴다.
namespace SnareServerReply
{
	/**
	 * 서버 답이 실패인지 보고 사유를 돌려준다.
	 * @return 실패면 사유 문장(서버가 준 한국어 사유 + 모자란 수), 성공이면 빈 FText
	 */
	FText DescribeFailure(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error);

	/** 실패 응답의 `code` (`not_enough_coins`·`locked` …). 성공이거나 없으면 빈 문자열. */
	FString GetFailureCode(const TSharedPtr<FJsonObject>& Response);

	/** 실패가 어디서 났는가 — 화면 문구와 「다시 시도」 를 가른다. */
	enum class EFailureKind : uint8
	{
		None,
		/** 서버에 닿지 못했다(인터넷 끊김·시간 초과·서버 점검). 아무것도 바뀌지 않았다. */
		Network,
		/** 로그인 중이거나 세션이 만료됐다. 다시 로그인한 뒤면 된다. */
		Login,
		/** 서버가 받고 거절했다(`fail(code)`·스크립트 오류). 다시 보내도 같은 답이다. */
		Rejected
	};

	/** `CallServerFunction` 이 오류 글 앞에 붙이는 표지. 부르는 쪽 콜백 모양을 바꾸지 않고 종류를 실어 보낸다. */
	FString TagNetworkError(const FString& Detail);
	FString TagLoginError(const FString& Detail);
	FString TagRejectedError(const FString& Detail);

	/** 답 하나가 어떤 실패인가. 표지가 없는 네트워크 실패(옛 길·검사 가짜)는 Network 로 본다. */
	EFailureKind ClassifyFailure(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error);

	/**
	 * 서버가 **받아들였는가**. 네트워크 실패·빈 응답·`error` 칸이 있으면 false.
	 * @param OutCode    거절 사유 코드(`too_soon` 등). 닿지 못했으면(끊김·로그인 중 모두) `network`
	 * @param OutMessage 사람이 읽을 사유(서버 `error` 문자열, 표지 붙은 오류면 짧은 한국어 문구, 아니면 오류 글 그대로)
	 */
	bool IsAccepted(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& NetworkError,
		FString& OutCode, FString& OutMessage);
}
