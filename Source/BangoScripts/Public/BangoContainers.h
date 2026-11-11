#pragma once

#include "CoreMinimal.h"

/**
 * Key funcs for TMap<FString, V> that compare and hash keys case-sensitively.
 * Unreal's default FString map keys ignore case, but Bango identifiers are case-sensitive.
 */
template<typename ValueType>
struct TBangoCaseSensitiveKeyFuncs : TDefaultMapKeyFuncs<FString, ValueType, false>
{
	typedef TDefaultMapKeyFuncs<FString, ValueType, false> Super;
	typedef typename Super::KeyInitType KeyInitType;

	static FORCEINLINE bool Matches(KeyInitType A, KeyInitType B)
	{
		return A.Equals(B, ESearchCase::CaseSensitive);
	}

	static FORCEINLINE uint32 GetKeyHash(KeyInitType Key)
	{
		return FCrc::StrCrc32(*Key);
	}
};

/** TMap<FString, V> with case-sensitive keys. */
template<typename ValueType>
using TBangoNameMap = TMap<FString, ValueType, FDefaultSetAllocator, TBangoCaseSensitiveKeyFuncs<ValueType>>;
