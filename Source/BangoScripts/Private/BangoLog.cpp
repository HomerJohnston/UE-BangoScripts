#include "BangoLog.h"

#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY(LogBango);

namespace Bango
{
	void ReportError(const FString& Message)
	{
		const FString Full = FString::Printf(TEXT("[Bango] %s"), *Message);
		UE_LOG(LogBango, Error, TEXT("%s"), *Full);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 30.0f, FColor::Red, Full);
		}
	}

	void ReportNote(const FString& Message, const FColor& Color, bool bWarning)
	{
		const FString Full = FString::Printf(TEXT("[Bango] %s"), *Message);
		if (bWarning)
		{
			UE_LOG(LogBango, Warning, TEXT("%s"), *Full);
		}
		else
		{
			UE_LOG(LogBango, Display, TEXT("%s"), *Full);
		}
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, bWarning ? 10.0f : 3.0f, Color, Full);
		}
	}
}
