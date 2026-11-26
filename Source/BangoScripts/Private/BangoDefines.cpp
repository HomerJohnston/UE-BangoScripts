#include "BangoDefines.h"

#include "BangoCompiler.h"
#include "BangoTypes.h"

namespace BangoDefinesPrivate
{
	struct FPluginSet
	{
		FString Owner;
		TArray<Bango::FBangoDefineText> Rows;
		TArray<FString> RowErrors;
	};

	struct FTable
	{
		TArray<FBangoDefineEntry> Entries;
		TMap<FString, int32> ByName;
		/** The project's rows (SetProjectDefines), and for each, why it was left out, or empty. */
		TArray<Bango::FBangoDefineText> ProjectRows;
		TArray<FString> RowErrors;
		/** Each plugin's (RegisterDefines), in the order they were first registered. */
		TArray<FPluginSet> Plugins;
		/** Told of each error a plugin's registering makes (the module logs them). */
		TFunction<void(const FString&)> Reporter;
		uint64 Fingerprint = 1;
	};

	static FTable& GetTable()
	{
		static FTable Table;
		return Table;
	}

	static bool IsCapital(TCHAR C) { return C >= TEXT('A') && C <= TEXT('Z'); }
	static bool IsDigit(TCHAR C) { return C >= TEXT('0') && C <= TEXT('9'); }
}

bool Bango::IsDefineName(const FString& Name, FString* OutWhy)
{
	using namespace BangoDefinesPrivate;
	auto Refuse = [OutWhy, &Name](const TCHAR* Why)
	{
		if (OutWhy)
		{
			*OutWhy = FString::Printf(TEXT("'%s' can't be a define's name: %s A define's name is SHOUTING_SNAKE_CASE, like SELECT_RANDOM or MAX_PARTY_2."), *Name, Why);
		}
		return false;
	};
	if (Name.IsEmpty())
	{
		if (OutWhy)
		{
			*OutWhy = TEXT("A define needs a name, in SHOUTING_SNAKE_CASE, like SELECT_RANDOM.");
		}
		return false;
	}
	if (!IsCapital(Name[0]))
	{
		return Refuse(TEXT("it must start with a capital letter."));
	}
	bool bHasLetter = false;
	for (int32 Index = 0; Index < Name.Len(); ++Index)
	{
		const TCHAR C = Name[Index];
		if (IsCapital(C))
		{
			bHasLetter = true;
		}
		else if (!IsDigit(C) && C != TEXT('_'))
		{
			return Refuse(TEXT("it can only have capital letters, digits and '_'."));
		}
	}
	if (Name.EndsWith(TEXT("_")) || Name.Contains(TEXT("__")))
	{
		return Refuse(TEXT("its words are joined by one '_', with none at the end."));
	}
	if (IsReservedWord(Name))
	{
		return Refuse(TEXT("it's a word Bango uses (like TEXT)."));
	}
	return bHasLetter;
}

const FBangoDefineEntry* Bango::FindDefine(const FString& Name)
{
	BangoDefinesPrivate::FTable& Table = BangoDefinesPrivate::GetTable();
	if (Table.Entries.Num() == 0 || Name.IsEmpty() || !BangoDefinesPrivate::IsCapital(Name[0]))
	{
		return nullptr; // quick: most names aren't defines
	}
	const int32* Index = Table.ByName.Find(Name);
	return Index ? &Table.Entries[*Index] : nullptr;
}

FString Bango::GetDefineRowError(int32 Row)
{
	const TArray<FString>& RowErrors = BangoDefinesPrivate::GetTable().RowErrors;
	return RowErrors.IsValidIndex(Row) ? RowErrors[Row] : FString();
}

const TArray<FBangoDefineEntry>& Bango::GetDefines()
{
	return BangoDefinesPrivate::GetTable().Entries;
}

uint64 Bango::GetDefinesFingerprint()
{
	return BangoDefinesPrivate::GetTable().Fingerprint;
}

bool Bango::CheckDefine(const FBangoDefineText& Define, const TSet<FString>* Taken, FBangoValue& OutValue, FString& OutError)
{
	const FString Name = Define.Name.TrimStartAndEnd();
	if (!IsDefineName(Name, &OutError))
	{
		return false;
	}
	if (Taken && Taken->Contains(Name))
	{
		OutError = FString::Printf(TEXT("'%s' is defined twice. Each define has a name of its own."), *Name);
		return false;
	}
	if (Define.ValueText.Contains(TEXT("TEXT("), ESearchCase::CaseSensitive) || Define.ValueText.Contains(TEXT("TEXT_NS("), ESearchCase::CaseSensitive))
	{
		OutError = FString::Printf(TEXT("'%s': a define's text isn't gathered for translation, so it's written TEXT_INV(\"...\"). Put a text to be translated in the script."), *Name);
		return false;
	}
	if (Define.ValueText.TrimStartAndEnd().IsEmpty())
	{
		OutError = FString::Printf(TEXT("'%s' needs a value."), *Name);
		return false;
	}
	FString ValueError;
	if (!ParseFixedValue(Define.TypeName, Define.ValueText, OutValue, ValueError))
	{
		OutError = FString::Printf(TEXT("'%s': %s"), *Name, *ValueError);
		return false;
	}
	return true;
}

namespace BangoDefinesPrivate
{
	/** Makes the table again from the plugins' sets (in the order registered) and then the project's rows. */
	static void Rebuild()
	{
		FTable& Table = GetTable();
		// Row by row, so a define's value can use the defines before it (ParseFixedValue reads the table), and only those.
		TArray<FBangoDefineEntry> OldEntries = MoveTemp(Table.Entries);
		Table.Entries.Reset();
		Table.ByName.Reset();
		auto AddRow = [&Table](const Bango::FBangoDefineText& Define, const FString& Source) -> FString
		{
			const FString Name = Define.Name.TrimStartAndEnd();
			if (const int32* Existing = Table.ByName.Find(Name))
			{
				const FBangoDefineEntry& First = Table.Entries[*Existing];
				return FString::Printf(TEXT("'%s' is already a define, %s, so this one is left out. A name has one meaning in every script: rename one of them."),
					*Name, *Bango::DescribeDefineSource(First));
			}
			FBangoValue Value;
			FString Error;
			if (!Bango::CheckDefine(Define, nullptr, Value, Error))
			{
				return Error;
			}
			FBangoDefineEntry& Entry = Table.Entries.AddDefaulted_GetRef();
			Entry.Name = Name;
			Entry.Value = Value;
			Entry.ValueText = Define.ValueText.TrimStartAndEnd();
			Entry.TypeName = Define.TypeName.TrimStartAndEnd();
			Entry.Category = Define.Category.IsEmpty() ? Source : Define.Category;
			Entry.Description = Define.Description;
			Entry.Source = Source;
			Table.ByName.Add(Entry.Name, Table.Entries.Num() - 1);
			return FString();
		};
		// A plugin's defines come first: its commands are written for them, so a project row can't change one.
		for (FPluginSet& Set : Table.Plugins)
		{
			Set.RowErrors.Reset();
			for (const Bango::FBangoDefineText& Define : Set.Rows)
			{
				Set.RowErrors.Add(AddRow(Define, Set.Owner));
			}
		}
		Table.RowErrors.Reset();
		for (const Bango::FBangoDefineText& Define : Table.ProjectRows)
		{
			Table.RowErrors.Add(AddRow(Define, FString()));
		}
		// The same as before: the fingerprint stays, and nothing is compiled again.
		bool bSame = OldEntries.Num() == Table.Entries.Num();
		for (int32 Index = 0; bSame && Index < OldEntries.Num(); ++Index)
		{
			const FBangoDefineEntry& Old = OldEntries[Index];
			const FBangoDefineEntry& New = Table.Entries[Index];
			bSame = Old.Name == New.Name && Old.TypeName == New.TypeName && Old.ValueText == New.ValueText
				&& Old.Category == New.Category && Old.Description == New.Description && Old.Source == New.Source;
		}
		if (!bSame)
		{
			++Table.Fingerprint;
		}
	}

	static void AddErrors(const TArray<FString>& Errors, TArray<FString>* OutErrors)
	{
		for (const FString& Error : Errors)
		{
			if (!Error.IsEmpty() && OutErrors)
			{
				OutErrors->Add(Error);
			}
		}
	}

	/** Reports the errors the last change made: its own rows', and the project's rows that now clash with it. */
	static void ReportNewErrors(const TArray<FString>& Own, const TArray<FString>& ProjectBefore, const FString& Owner, TArray<FString>* OutErrors)
	{
		TArray<FString> Errors;
		for (const FString& Error : Own)
		{
			if (!Error.IsEmpty())
			{
				Errors.Add(FString::Printf(TEXT("%s: %s"), *Owner, *Error));
			}
		}
		const TArray<FString>& ProjectNow = GetTable().RowErrors;
		for (int32 Row = 0; Row < ProjectNow.Num(); ++Row)
		{
			if (!ProjectNow[Row].IsEmpty() && (!ProjectBefore.IsValidIndex(Row) || ProjectBefore[Row] != ProjectNow[Row]))
			{
				Errors.Add(FString::Printf(TEXT("Project Settings > Plugins > Bango > Defines: %s"), *ProjectNow[Row]));
			}
		}
		AddErrors(Errors, OutErrors);
		if (const TFunction<void(const FString&)>& Reporter = GetTable().Reporter)
		{
			for (const FString& Error : Errors)
			{
				Reporter(Error);
			}
		}
	}
}

void Bango::SetProjectDefines(const TArray<FBangoDefineText>& Defines, TArray<FString>* OutErrors)
{
	using namespace BangoDefinesPrivate;
	FTable& Table = GetTable();
	Table.ProjectRows = Defines;
	Rebuild();
	AddErrors(Table.RowErrors, OutErrors);
}

void Bango::RegisterDefines(const FString& Owner, const TArray<FBangoDefineText>& Defines, TArray<FString>* OutErrors)
{
	using namespace BangoDefinesPrivate;
	FTable& Table = GetTable();
	const TArray<FString> ProjectBefore = Table.RowErrors;
	FPluginSet* Set = Table.Plugins.FindByPredicate([&Owner](const FPluginSet& Each) { return Each.Owner == Owner; });
	if (!Set)
	{
		Set = &Table.Plugins.AddDefaulted_GetRef();
		Set->Owner = Owner;
	}
	Set->Rows = Defines; // registering again replaces the set, and keeps its place
	Rebuild();
	const FPluginSet* Registered = Table.Plugins.FindByPredicate([&Owner](const FPluginSet& Each) { return Each.Owner == Owner; });
	ReportNewErrors(Registered ? Registered->RowErrors : TArray<FString>(), ProjectBefore, Owner, OutErrors);
}

void Bango::UnregisterDefines(const FString& Owner)
{
	using namespace BangoDefinesPrivate;
	FTable& Table = GetTable();
	const int32 Removed = Table.Plugins.RemoveAll([&Owner](const FPluginSet& Each) { return Each.Owner == Owner; });
	if (Removed > 0)
	{
		Rebuild();
	}
}

FString Bango::GetRegisteredDefineError(const FString& Owner, int32 Row)
{
	const BangoDefinesPrivate::FPluginSet* Set = BangoDefinesPrivate::GetTable().Plugins.FindByPredicate([&Owner](const BangoDefinesPrivate::FPluginSet& Each) { return Each.Owner == Owner; });
	return Set && Set->RowErrors.IsValidIndex(Row) ? Set->RowErrors[Row] : FString();
}

FString Bango::DescribeDefineSource(const FBangoDefineEntry& Define)
{
	return Define.Source.IsEmpty() ? FString(TEXT("in Project Settings > Plugins > Bango > Defines"))
		: FString::Printf(TEXT("registered by %s"), *Define.Source);
}

void Bango::SetDefineErrorReporter(TFunction<void(const FString&)> Reporter)
{
	BangoDefinesPrivate::GetTable().Reporter = MoveTemp(Reporter);
}
