// Bango text editor: turns script text into coloured Slate text runs.
// See "Bango Text Editor Spec", section 4.

#include "BangoSyntaxMarshaller.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/ILayoutBlock.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/TextHitPoint.h"
#include "Framework/Text/TextLayout.h"
#include "Rendering/SlateRenderer.h"
#include "BangoLineTokenizer.h"
#include "BangoScriptNames.h"
#include "BangoSyntaxStyle.h"

namespace BangoSyntaxMarshallerPrivate
{
	/** The column after a tab at Column: the next multiple of TabSize. */
	static int32 NextTabStop(int32 Column, int32 TabSize)
	{
		return Column + TabSize - (Column % TabSize);
	}

	/**
	 * A run of tab characters. Slate draws a tab about as wide as one space; here each tab is as wide
	 * as the spaces up to the next tab stop. The text is monospace, so a column is one space wide, and
	 * the marshaller knows which column the run starts at.
	 *
	 * Only width and cursor placement change; the tabs are still tabs in the text.
	 */
	class FBangoTabRun : public FSlateTextRun
	{
	public:
		static TSharedRef<FBangoTabRun> Create(const TSharedRef<const FString>& InText, const FTextBlockStyle& InStyle,
			const FTextRange& InRange, int32 InStartColumn, int32 InTabSize)
		{
			return MakeShareable(new FBangoTabRun(InText, InStyle, InRange, InStartColumn, InTabSize));
		}

		virtual FVector2D Measure(int32 StartIndex, int32 EndIndex, float Scale, const FRunTextContext& TextContext) const override
		{
			// Keep the base height (the font's line height); only the width changes.
			const FVector2D BaseSize = FSlateTextRun::Measure(StartIndex, EndIndex, Scale, TextContext);
			return FVector2D(XOf(EndIndex, Scale) - XOf(StartIndex, Scale), BaseSize.Y);
		}

		virtual int8 GetKerning(int32 CurrentIndex, float Scale, const FRunTextContext& TextContext) const override
		{
			return 0;
		}

		virtual int32 GetTextIndexAt(const TSharedRef<ILayoutBlock>& Block, const FVector2D& Location, float Scale, ETextHitPoint* const OutHitPoint = nullptr) const override
		{
			const FVector2D BlockOffset = Block->GetLocationOffset();
			const FVector2D BlockSize = Block->GetSize();
			if (Location.X < BlockOffset.X || Location.X > BlockOffset.X + BlockSize.X ||
				Location.Y < BlockOffset.Y || Location.Y > BlockOffset.Y + BlockSize.Y)
			{
				return INDEX_NONE;
			}

			// The nearer edge of the tab under the point.
			const FTextRange BlockRange = Block->GetTextRange();
			const float LocalX = Location.X - BlockOffset.X + XOf(BlockRange.BeginIndex, Scale);
			for (int32 Index = BlockRange.BeginIndex; Index < BlockRange.EndIndex; ++Index)
			{
				const float Left = XOf(Index, Scale);
				const float Right = XOf(Index + 1, Scale);
				if (LocalX < Right)
				{
					if (OutHitPoint)
					{
						*OutHitPoint = ETextHitPoint::WithinText;
					}
					return LocalX < (Left + Right) * 0.5f ? Index : Index + 1;
				}
			}
			if (OutHitPoint)
			{
				*OutHitPoint = ETextHitPoint::RightGutter;
			}
			return BlockRange.EndIndex;
		}

		virtual FVector2D GetLocationAt(const TSharedRef<ILayoutBlock>& Block, int32 Offset, float Scale) const override
		{
			const FTextRange BlockRange = Block->GetTextRange();
			const int32 Index = FMath::Clamp(Offset, BlockRange.BeginIndex, BlockRange.EndIndex);
			return Block->GetLocationOffset() + FVector2D(XOf(Index, Scale) - XOf(BlockRange.BeginIndex, Scale), 0.0);
		}

		virtual TSharedRef<IRun> Clone() const override
		{
			return MakeShareable(new FBangoTabRun(*this));
		}

	private:
		FBangoTabRun(const TSharedRef<const FString>& InText, const FTextBlockStyle& InStyle, const FTextRange& InRange, int32 InStartColumn, int32 InTabSize)
			: FSlateTextRun(FRunInfo(), InText, InStyle, InRange)
			, StartColumn(InStartColumn)
			, TabSize(FMath::Max(InTabSize, 1))
		{
		}

		FBangoTabRun(const FBangoTabRun&) = default;

		/** X of the start of character Index (Range.BeginIndex .. Range.EndIndex), from the start of the run. */
		float XOf(int32 Index, float Scale) const
		{
			int32 Column = StartColumn;
			for (int32 I = Range.BeginIndex; I < Index; ++I)
			{
				Column = NextTabStop(Column, TabSize);
			}
			return static_cast<float>(Column - StartColumn) * SpaceWidth(Scale);
		}

		float SpaceWidth(float Scale) const
		{
			const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			return static_cast<float>(FontMeasure->Measure(TEXT(" "), Style.Font, Scale).X);
		}

		int32 StartColumn;
		int32 TabSize;
	};
}

TSharedRef<FBangoSyntaxMarshaller> FBangoSyntaxMarshaller::Create(TSharedRef<const FBangoSyntaxStyle> InStyle)
{
	return MakeShareable(new FBangoSyntaxMarshaller(MoveTemp(InStyle)));
}

FBangoSyntaxMarshaller::FBangoSyntaxMarshaller(TSharedRef<const FBangoSyntaxStyle> InStyle)
	: Style(MoveTemp(InStyle))
{
}

void FBangoSyntaxMarshaller::SetStyle(TSharedRef<const FBangoSyntaxStyle> InStyle)
{
	Style = MoveTemp(InStyle);
	MakeDirty();
}

void FBangoSyntaxMarshaller::SetTabSize(int32 InTabSize)
{
	InTabSize = FMath::Max(InTabSize, 1);
	if (InTabSize != TabSize)
	{
		TabSize = InTabSize;
		MakeDirty();
	}
}

void FBangoSyntaxMarshaller::SetMetadataIsError(bool bInMetadataIsError)
{
	if (bInMetadataIsError != bMetadataIsError)
	{
		bMetadataIsError = bInMetadataIsError;
		MakeDirty();
	}
}

void FBangoSyntaxMarshaller::SetText(const FString& SourceString, FTextLayout& TargetTextLayout)
{
	TArray<FTextRange> LineRanges;
	FTextRange::CalculateLineRangesFromString(SourceString, LineRanges);

	TArray<FTextLayout::FNewLineData> LinesToAdd;
	LinesToAdd.Reserve(LineRanges.Num());

	const FTextBlockStyle& DefaultStyle = Style->GetDefaultStyle();

	// Every line's tokens first, then the file's declarations from them, so each name can be coloured as the
	// graph's local or parameter, or a global (FBangoScriptNames).
	TArray<FString> Lines;
	TArray<TArray<FBangoSyntaxToken>> LineTokens;
	Lines.Reserve(LineRanges.Num());
	LineTokens.SetNum(LineRanges.Num());
	for (int32 LineIndex = 0; LineIndex < LineRanges.Num(); ++LineIndex)
	{
		const FTextRange& LineRange = LineRanges[LineIndex];
		Lines.Add(SourceString.Mid(LineRange.BeginIndex, LineRange.Len()));
		FBangoLineTokenizer::TokenizeLine(FStringView(Lines[LineIndex]), LineTokens[LineIndex]);
	}

	FBangoScriptNames Names;
	Names.Build(Lines, LineTokens);

	for (int32 LineIndex = 0; LineIndex < LineRanges.Num(); ++LineIndex)
	{
		TSharedRef<FString> LineText = MakeShared<FString>(MoveTemp(Lines[LineIndex]));

		TArray<FBangoSyntaxToken>& Tokens = LineTokens[LineIndex];
		Names.RefineTokens(LineIndex, *LineText, Tokens);

		TArray<TSharedRef<IRun>> Runs;
		Runs.Reserve(Tokens.Num() * 2 + 1);

		// The column the next run starts at (tabs count to their tab stop), for tab widths.
		int32 Column = 0;
		const int32 Tabs = TabSize;

		// Adds the text from Begin to End in one style, with each stretch of tabs as its own tab run.
		auto AddRun = [&Runs, &LineText, &Column, Tabs](const FTextBlockStyle& RunStyle, int32 Begin, int32 End)
		{
			int32 I = Begin;
			while (I < End)
			{
				const bool bTabs = (*LineText)[I] == TEXT('\t');
				const int32 RunColumn = Column;
				int32 J = I;
				while (J < End && ((*LineText)[J] == TEXT('\t')) == bTabs)
				{
					Column = bTabs ? BangoSyntaxMarshallerPrivate::NextTabStop(Column, Tabs) : Column + 1;
					++J;
				}

				if (bTabs)
				{
					Runs.Add(BangoSyntaxMarshallerPrivate::FBangoTabRun::Create(LineText, RunStyle, FTextRange(I, J), RunColumn, Tabs));
				}
				else
				{
					Runs.Add(FSlateTextRun::Create(FRunInfo(), LineText, RunStyle, FTextRange(I, J)));
				}
				I = J;
			}
		};

		// Tokens are in order and don't overlap; the gaps between them (spaces) are Default.
		int32 Cursor = 0;
		for (const FBangoSyntaxToken& Token : Tokens)
		{
			AddRun(DefaultStyle, Cursor, Token.Start);
			const EBangoSyntaxKind Kind = bMetadataIsError && Token.Kind == EBangoSyntaxKind::Metadata ? EBangoSyntaxKind::Error : Token.Kind;
			AddRun(Style->GetStyle(Kind, Token.bDisconnected), Token.Start, Token.Start + Token.Len);
			Cursor = Token.Start + Token.Len;
		}
		AddRun(DefaultStyle, Cursor, LineText->Len());

		// Every line needs at least one run, even an empty one, for the cursor to sit on.
		if (Runs.Num() == 0)
		{
			Runs.Add(FSlateTextRun::Create(FRunInfo(), LineText, DefaultStyle, FTextRange(0, 0)));
		}

		LinesToAdd.Emplace(MoveTemp(LineText), MoveTemp(Runs));
	}

	TargetTextLayout.AddLines(LinesToAdd);
}

void FBangoSyntaxMarshaller::GetText(FString& TargetString, const FTextLayout& SourceTextLayout)
{
	SourceTextLayout.GetAsText(TargetString);
}
