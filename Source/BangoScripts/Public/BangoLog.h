#pragma once

#include "CoreMinimal.h"

BANGOSCRIPTS_API DECLARE_LOG_CATEGORY_EXTERN(LogBango, Log, All);

namespace Bango
{
	/** Logs an error and shows it on screen. Every Bango failure goes through here. */
	BANGOSCRIPTS_API void ReportError(const FString& Message);

	/** Logs a note and shows it on screen for a few seconds, in Color (like a script being reloaded). */
	BANGOSCRIPTS_API void ReportNote(const FString& Message, const FColor& Color, bool bWarning = false);
}
