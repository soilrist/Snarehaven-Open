#include "Backend/SnareServerReply.h"

#include "Dom/JsonObject.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	const TCHAR* const NetworkTag = TEXT("[network] ");
	const TCHAR* const LoginTag = TEXT("[login] ");
	const TCHAR* const RejectedTag = TEXT("[rejected] ");

	bool HasTag(const FString& Error)
	{
		return Error.StartsWith(NetworkTag) || Error.StartsWith(LoginTag) || Error.StartsWith(RejectedTag);
	}
}

namespace SnareServerReply
{
FString TagNetworkError(const FString& Detail)
{
	return NetworkTag + Detail;
}

FString TagLoginError(const FString& Detail)
{
	return LoginTag + Detail;
}

FString TagRejectedError(const FString& Detail)
{
	return RejectedTag + Detail;
}

EFailureKind ClassifyFailure(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
{
	if (bOk && Response.IsValid())
	{
		return Response->HasField(TEXT("error")) ? EFailureKind::Rejected : EFailureKind::None;
	}
	if (Error.StartsWith(LoginTag))
	{
		return EFailureKind::Login;
	}
	if (Error.StartsWith(RejectedTag))
	{
		return EFailureKind::Rejected;
	}
	return EFailureKind::Network;
}

bool IsAccepted(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& NetworkError,
	FString& OutCode, FString& OutMessage)
{
	if (!bOk || !Response.IsValid())
	{
		// 코드는 `network` 하나로 둔다 — 목장 문구 표(`network` 행)처럼 이 코드로 줄을 찾는 쪽이 있다.
		OutCode = TEXT("network");
		OutMessage = HasTag(NetworkError) ? DescribeFailure(bOk, Response, NetworkError).ToString() : NetworkError;
		return false;
	}
	if (Response->HasField(TEXT("error")))
	{
		Response->TryGetStringField(TEXT("code"), OutCode);
		Response->TryGetStringField(TEXT("error"), OutMessage);
		return false;
	}
	return true;
}

FString GetFailureCode(const TSharedPtr<FJsonObject>& Response)
{
	FString Code;
	if (Response.IsValid())
	{
		Response->TryGetStringField(TEXT("code"), Code);
	}
	return Code;
}

FText DescribeFailure(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
{
	// 서버에 닿지도 못했다. 이때는 아무것도 바뀌지 않았다 — 왜 못 닿았는지만 가른다.
	if (!bOk || !Response.IsValid())
	{
		switch (ClassifyFailure(bOk, Response, Error))
		{
		case EFailureKind::Login:
			return LOCTEXT("ServerLoggingIn", "서버 접속 중 - 잠시 뒤 다시");
		case EFailureKind::Rejected:
			return LOCTEXT("ServerRejected", "서버가 받지 않았다 - 잠시 뒤 다시");
		default:
			return LOCTEXT("ServerUnreachable", "서버에 닿지 못했다 - 연결을 확인해 줘");
		}
	}

	FString Message;
	if (!Response->TryGetStringField(TEXT("error"), Message))
	{
		return FText::GetEmpty();
	}

	// 사유 문장은 서버가 정한다 — 화면이 사유를 지어내면 서버 판정과 말이 갈린다.
	// 모자란 수(`need`/`have`)만 덧붙인다: "코인이 모자란다" 만으로는 얼마가 필요한지 모른다.
	double Need = 0.0;
	double Have = 0.0;
	if (Response->TryGetNumberField(TEXT("need"), Need) && Response->TryGetNumberField(TEXT("have"), Have))
	{
		return FText::Format(LOCTEXT("ServerFailNeedHave", "{0} ({1} 필요, {2} 있음)"),
			FText::FromString(Message), FText::AsNumber(static_cast<int32>(Need)),
			FText::AsNumber(static_cast<int32>(Have)));
	}
	return FText::FromString(Message);
}
}

#undef LOCTEXT_NAMESPACE
