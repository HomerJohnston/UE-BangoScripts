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
}
