#pragma once

#include "CoreMinimal.h"
#include "BangoIDName.generated.h"

struct FPropertyTag;

/**
 * A Bango ID name, as scripts write it after an '@' (`@john`): an FString, in a struct of its own only so the details
 * panel can show it with an '@' in front (FBangoIDNameCustomization). It converts to and from FString implicitly, so it
 * can be passed wherever a name string goes. Case-sensitive, like the names themselves.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Bango ID Name"))
struct BANGOSCRIPTS_API FBangoIDName
{
	GENERATED_BODY()

	FBangoIDName() = default;
	FBangoIDName(const FString& InName) : Name(InName) {}
	FBangoIDName(FString&& InName) : Name(MoveTemp(InName)) {}
	FBangoIDName(const TCHAR* InName) : Name(InName) {}

	/** The name, without the '@'. Letters, digits and '_', not starting with a digit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bango")
	FString Name;

	operator const FString&() const { return Name; }
	const FString& ToString() const { return Name; }
	const TCHAR* operator*() const { return *Name; }
	bool IsEmpty() const { return Name.IsEmpty(); }

	friend bool operator==(const FBangoIDName& A, const FBangoIDName& B) { return A.Name.Equals(B.Name, ESearchCase::CaseSensitive); }
	friend bool operator!=(const FBangoIDName& A, const FBangoIDName& B) { return !(A == B); }
	friend bool operator==(const FBangoIDName& A, const FString& B) { return A.Name.Equals(B, ESearchCase::CaseSensitive); }
	friend bool operator!=(const FBangoIDName& A, const FString& B) { return !(A == B); }
	friend bool operator==(const FString& A, const FBangoIDName& B) { return B == A; }
	friend bool operator!=(const FString& A, const FBangoIDName& B) { return !(B == A); }
	friend uint32 GetTypeHash(const FBangoIDName& IDName) { return GetTypeHash(IDName.Name); }
};
