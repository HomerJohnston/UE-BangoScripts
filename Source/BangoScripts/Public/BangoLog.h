#pragma once

#include "CoreMinimal.h"

BANGOSCRIPTS_API DECLARE_LOG_CATEGORY_EXTERN(LogBango, Log, All);

namespace Bango
{
	/** Logs an error and shows it on screen. Every Bango failure goes through here. */
	BANGOSCRIPTS_API void ReportError(const FString& Message);
}
