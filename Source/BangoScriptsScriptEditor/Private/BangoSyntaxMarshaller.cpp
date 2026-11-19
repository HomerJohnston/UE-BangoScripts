// Bango text editor: turns script text into coloured Slate text runs.
// See "Bango Text Editor Spec", section 4.

#include "BangoSyntaxMarshaller.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/ILayoutBlock.h"
#include "Framework/Text/ISlateLineHighlighter.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/TextHitPoint.h"
#include "Framework/Text/TextLayout.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "BangoLineTokenizer.h"
#include "BangoScriptNames.h"
#include "BangoSyntaxStyle.h"

namespace BangoSyntaxMarshallerPrivate
{
	/** Draws a box behind characters: the bracket next to the cursor and its match. Like Slate's search highlighter. */
	class FBangoBracketHighlighter : public ISlateLineHighlighter
	{
	public:
		explicit FBangoBracketHighlighter(const FLinearColor& InColor)
			: Color(InColor)
		{
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FTextLayout::FLineView& Line, const FVector2D Offset, const float Width,
			const FTextBlockStyle& DefaultStyle, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			if (Width <= 0.0f)
			{
				return LayerId;
			}
			const FVector2D Location(Line.Offset.X + Offset.X, Line.Offset.Y + Offset.Y);

			// The block size and offset values are pre-scaled, so undo that for the paint geometry (as Slate's own highlighters do).
			const float InverseScale = Inverse(AllottedGeometry.Scale);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				++LayerId,
				AllottedGeometry.ToPaintGeometry(
					TransformVector(InverseScale, FVector2D(Width, FMath::Max(Line.Size.Y, Line.TextHeight))),
					FSlateLayoutTransform(TransformPoint(InverseScale, Location))),
				FAppStyle::GetBrush("WhiteBrush"),
				bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
				Color * InWidgetStyle.GetColorAndOpacityTint());
			return LayerId;
		}

	private:
		FLinearColor Color;
	};

	/** Draws nothing: records where its characters are, for FBangoSyntaxMarshaller::SetProbe. */
	class FBangoProbeHighlighter : public ISlateLineHighlighter
	{
	public:
		explicit FBangoProbeHighlighter(const TSharedRef<FBangoTextProbe>& InProbe)
			: Probe(InProbe)
		{
		}

		/** Its characters are the one before an empty range: it records that one's right edge, with no width. */
		bool bAfterChar = false;

		virtual int32 OnPaint(const FPaintArgs& Args, const FTextLayout::FLineView& Line, const FVector2D Offset, const float Width,
			const FTextBlockStyle& DefaultStyle, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			// The same position the bracket boxes are drawn at, in the text widget's space, then in desktop space.
			const float InverseScale = Inverse(AllottedGeometry.Scale);
			const FVector2D Location(Line.Offset.X + Offset.X + (bAfterChar ? Width : 0.0f), Line.Offset.Y + Offset.Y);
			const FVector2D LocalTopLeft = TransformPoint(InverseScale, Location);
			const FVector2D LocalSize = TransformVector(InverseScale, FVector2D(bAfterChar ? 0.0f : Width, FMath::Max(Line.Size.Y, Line.TextHeight)));
			const FVector2D AbsoluteTopLeft = AllottedGeometry.LocalToAbsolute(LocalTopLeft);
			Probe->AbsolutePosition = AbsoluteTopLeft;
			Probe->AbsoluteSize = FVector2D(AllottedGeometry.LocalToAbsolute(LocalTopLeft + LocalSize)) - AbsoluteTopLeft;
			Probe->bValid = true;
			return LayerId;
		}

	private:
		TSharedRef<FBangoTextProbe> Probe;
	};

	/** A red line under a compile error's characters (FBangoSyntaxMarshaller::SetError). */
	class FBangoErrorHighlighter : public ISlateLineHighlighter
	{
	public:
		virtual int32 OnPaint(const FPaintArgs& Args, const FTextLayout::FLineView& Line, const FVector2D Offset, const float Width,
			const FTextBlockStyle& DefaultStyle, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			if (Width <= 0.0f)
			{
				return LayerId;
			}
			// Along the bottom of the line, as the bracket boxes are drawn (their sizes are pre-scaled).
			const float InverseScale = Inverse(AllottedGeometry.Scale);
			const float LineHeight = FMath::Max(Line.Size.Y, Line.TextHeight);
			const float Thickness = 2.0f * AllottedGeometry.Scale;
			const FVector2D Location(Line.Offset.X + Offset.X, Line.Offset.Y + Offset.Y + LineHeight - Thickness);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				++LayerId,
				AllottedGeometry.ToPaintGeometry(
					TransformVector(InverseScale, FVector2D(Width, Thickness)),
					FSlateLayoutTransform(TransformPoint(InverseScale, Location))),
				FAppStyle::GetBrush("WhiteBrush"),
				bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
				FLinearColor(1.0f, 0.2f, 0.15f) * InWidgetStyle.GetColorAndOpacityTint());
			return LayerId;
		}
	};

	/**
	 * Draws faint text just after its characters (the line's last one): the `)`s autocomplete left open
	 * (FBangoSyntaxMarshaller::SetGhostParens). Not part of the text; nothing after it on the line, so it covers nothing.
	 */
	class FBangoGhostTextHighlighter : public ISlateLineHighlighter
	{
	public:
		FString Text;
		FSlateFontInfo Font;
		FLinearColor Color = FLinearColor::White;

		virtual int32 OnPaint(const FPaintArgs& Args, const FTextLayout::FLineView& Line, const FVector2D Offset, const float Width,
			const FTextBlockStyle& DefaultStyle, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			if (Text.IsEmpty())
			{
				return LayerId;
			}
			// Where the next character would go, at the text's top (the line may be taller than its text).
			const float InverseScale = Inverse(AllottedGeometry.Scale);
			const float LineHeight = FMath::Max(Line.Size.Y, Line.TextHeight);
			const FVector2D Location(Line.Offset.X + Offset.X + Width, Line.Offset.Y + Offset.Y + FMath::Max(0.0f, Line.Size.Y - Line.TextHeight));
			FSlateDrawElement::MakeText(
				OutDrawElements,
				++LayerId,
				AllottedGeometry.ToPaintGeometry(
					TransformVector(InverseScale, FVector2D(LineHeight * Text.Len(), LineHeight)),
					FSlateLayoutTransform(TransformPoint(InverseScale, Location))),
				Text,
				Font,
				bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
				Color * InWidgetStyle.GetColorAndOpacityTint());
			return LayerId;
		}
	};

	static constexpr int32 GhostZOrder = -5;
	static constexpr int32 ProbeZOrder = -7;
	static constexpr int32 ErrorZOrder = -6;

	/** Above the selection (-10) and search results (-9), below the text. */
	static constexpr int32 BracketHighlightZOrder = -8;

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
	using namespace BangoSyntaxMarshallerPrivate;
	MatchHighlighter = MakeShared<FBangoBracketHighlighter>(FLinearColor(1.0f, 1.0f, 1.0f, 0.16f));
	UnmatchedHighlighter = MakeShared<FBangoBracketHighlighter>(FLinearColor(0.9f, 0.1f, 0.1f, 0.45f));
	ProbeHighlighter = MakeShared<FBangoProbeHighlighter>(Probe);
	ErrorHighlighter = MakeShared<FBangoErrorHighlighter>();

	const TSharedRef<FBangoGhostTextHighlighter> Ghost = MakeShared<FBangoGhostTextHighlighter>();
	const FTextBlockStyle& TextStyle = Style->GetDefaultStyle();
	Ghost->Font = TextStyle.Font;
	Ghost->Color = TextStyle.ColorAndOpacity.GetSpecifiedColor().CopyWithNewOpacity(0.35f);
	GhostHighlighter = Ghost;
}

void FBangoSyntaxMarshaller::SetGhostParens(int32 Line, int32 Count)
{
	if (Count <= 0)
	{
		Line = INDEX_NONE;
	}
	if (Line == GhostLine && Count == GhostCount)
	{
		return;
	}
	GhostLine = Line;
	GhostCount = Count;
	UpdateGhostHighlight();
}

void FBangoSyntaxMarshaller::UpdateGhostHighlight()
{
	if (LastLayout == nullptr)
	{
		return;
	}
	RemoveHighlights(GhostHighlights);
	if (GhostLine < 0 || GhostLine >= LastLayout->GetLineModels().Num() || !CachedLines.IsValidIndex(GhostLine))
	{
		return;
	}
	// On the line's last character, drawn after it. (It always has one: the `(` it's for.)
	const int32 LineLen = CachedLines[GhostLine].Len();
	if (LineLen == 0)
	{
		return;
	}
	StaticCastSharedPtr<BangoSyntaxMarshallerPrivate::FBangoGhostTextHighlighter>(GhostHighlighter)->Text = FString::ChrN(GhostCount, TEXT(')'));
	FTextLineHighlight Highlight(GhostLine, FTextRange(LineLen - 1, LineLen), BangoSyntaxMarshallerPrivate::GhostZOrder, GhostHighlighter.ToSharedRef());
	LastLayout->AddLineHighlight(Highlight);
	GhostHighlights.Add(Highlight);
}

void FBangoSyntaxMarshaller::SetProbe(int32 Line, int32 BeginColumn, int32 EndColumn)
{
	// A word growing as it's typed keeps its top-left corner, so only a new start forgets where it was.
	if (Line != ProbeLine || ProbeRange.BeginIndex != BeginColumn)
	{
		Probe->bValid = false;
	}
	ProbeLine = Line;
	ProbeRange = FTextRange(BeginColumn, FMath::Max(EndColumn, BeginColumn));
	UpdateProbeHighlight();
}

void FBangoSyntaxMarshaller::ClearProbe()
{
	ProbeLine = INDEX_NONE;
	Probe->bValid = false;
	UpdateProbeHighlight();
}

void FBangoSyntaxMarshaller::UpdateProbeHighlight()
{
	if (LastLayout == nullptr)
	{
		return;
	}
	RemoveHighlights(ProbeHighlights);
	// Nothing typed yet (`@light call |`): the character before, whose right edge is where the word starts.
	const bool bEmpty = ProbeRange.IsEmpty();
	if (ProbeLine >= 0 && ProbeLine < LastLayout->GetLineModels().Num() && (!bEmpty || ProbeRange.BeginIndex > 0))
	{
		StaticCastSharedPtr<BangoSyntaxMarshallerPrivate::FBangoProbeHighlighter>(ProbeHighlighter)->bAfterChar = bEmpty;
		const FTextRange Range = bEmpty ? FTextRange(ProbeRange.BeginIndex - 1, ProbeRange.BeginIndex) : ProbeRange;
		FTextLineHighlight Highlight(ProbeLine, Range, BangoSyntaxMarshallerPrivate::ProbeZOrder, ProbeHighlighter.ToSharedRef());
		LastLayout->AddLineHighlight(Highlight);
		ProbeHighlights.Add(Highlight);
	}
}

void FBangoSyntaxMarshaller::RemoveHighlights(TArray<FTextLineHighlight>& Highlights)
{
	// Only from lines that are still there: an edit that removes lines (deleting a selection of several) shrinks the
	// layout before it's rebuilt, and a highlight on a removed line went with it. (Any others are dropped when the
	// layout is rebuilt after the edit.)
	const int32 NumLines = LastLayout ? LastLayout->GetLineModels().Num() : 0;
	for (const FTextLineHighlight& Highlight : Highlights)
	{
		if (Highlight.LineIndex >= 0 && Highlight.LineIndex < NumLines)
		{
			LastLayout->RemoveLineHighlight(Highlight);
		}
	}
	Highlights.Reset();
}

void FBangoSyntaxMarshaller::SetError(int32 Line, int32 Column)
{
	ErrorLine = Line;
	ErrorColumn = Column;
	UpdateErrorHighlight();
}

void FBangoSyntaxMarshaller::ClearError()
{
	ErrorLine = INDEX_NONE;
	UpdateErrorHighlight();
}

void FBangoSyntaxMarshaller::UpdateErrorHighlight()
{
	if (LastLayout == nullptr)
	{
		return;
	}
	RemoveHighlights(ErrorHighlights);
	if (ErrorLine < 0 || ErrorLine >= LastLayout->GetLineModels().Num() || !CachedLines.IsValidIndex(ErrorLine))
	{
		return;
	}
	// The token the error is at; past the end of the line (a missing ';'), its last character; on an empty line, nothing.
	const int32 LineLen = CachedLines[ErrorLine].Len();
	if (LineLen == 0)
	{
		return;
	}
	int32 Begin = FMath::Clamp(ErrorColumn, 0, LineLen - 1);
	int32 End = Begin + 1;
	if (CachedTokens.IsValidIndex(ErrorLine))
	{
		for (const FBangoSyntaxToken& Token : CachedTokens[ErrorLine])
		{
			if (Begin >= Token.Start && Begin < Token.Start + Token.Len)
			{
				Begin = Token.Start;
				End = Token.Start + Token.Len;
				break;
			}
		}
	}
	FTextLineHighlight Highlight(ErrorLine, FTextRange(Begin, End), BangoSyntaxMarshallerPrivate::ErrorZOrder, ErrorHighlighter.ToSharedRef());
	LastLayout->AddLineHighlight(Highlight);
	ErrorHighlights.Add(Highlight);
}

void FBangoSyntaxMarshaller::SetCursor(const FBangoTextPosition& InCursor)
{
	BracketCursor = InCursor;
	UpdateBracketHighlights();
	UpdateNameHighlights();
}

void FBangoSyntaxMarshaller::UpdateNameHighlights()
{
	if (LastLayout == nullptr)
	{
		return;
	}
	RemoveHighlights(NameHighlights);
	if (!CachedLines.IsValidIndex(BracketCursor.Line) || !CachedTokens.IsValidIndex(BracketCursor.Line))
	{
		return;
	}

	// What kind of name a token is, for matching: a variable (the tokenizer marks every name that isn't a call as
	// Global; which variable it is comes from where it is), a function or command (a name followed by '(', or an array
	// command), a label (after label or goto: one graph's), or an @entity. Keywords and directives aren't names
	// (player and self are coloured as entities, but they're keywords).
	enum class ENameGroup : uint8 { None, Variable, Call, Label, Entity };
	auto GroupOf = [this](int32 LineIndex, const FBangoSyntaxToken& Token)
	{
		switch (Token.Kind)
		{
		case EBangoSyntaxKind::Global:    return ENameGroup::Variable;
		case EBangoSyntaxKind::Call:
		case EBangoSyntaxKind::Builtin:   return ENameGroup::Call;
		case EBangoSyntaxKind::LabelName: return ENameGroup::Label;
		case EBangoSyntaxKind::Entity:    return Token.Len > 1 && CachedLines[LineIndex][Token.Start] == TEXT('@') ? ENameGroup::Entity : ENameGroup::None;
		default:                          return ENameGroup::None;
		}
	};

	// The name the cursor is in or just after.
	const FBangoSyntaxToken* Under = nullptr;
	ENameGroup Group = ENameGroup::None;
	for (const FBangoSyntaxToken& Token : CachedTokens[BracketCursor.Line])
	{
		if (BracketCursor.Column >= Token.Start && BracketCursor.Column <= Token.Start + Token.Len)
		{
			const ENameGroup TokenGroup = GroupOf(BracketCursor.Line, Token);
			if (TokenGroup != ENameGroup::None)
			{
				Under = &Token;
				Group = TokenGroup;
				break;
			}
		}
	}
	if (!Under)
	{
		return;
	}
	const FString Name = CachedLines[BracketCursor.Line].Mid(Under->Start, Under->Len);
	const FString Key = Group == ENameGroup::Variable ? ScriptNames.GetVariableKey(BracketCursor.Line, Name) : FString();
	const int32 LabelGraph = ScriptNames.GetLineGraph(BracketCursor.Line);

	// Every use of it, the one at the cursor included.
	const int32 NumLines = FMath::Min(LastLayout->GetLineModels().Num(), CachedLines.Num());
	for (int32 LineIndex = 0; LineIndex < NumLines && LineIndex < CachedTokens.Num(); ++LineIndex)
	{
		const FString& Line = CachedLines[LineIndex];
		for (const FBangoSyntaxToken& Token : CachedTokens[LineIndex])
		{
			if (Token.Len != Name.Len() || GroupOf(LineIndex, Token) != Group || FCString::Strncmp(*Line + Token.Start, *Name, Token.Len) != 0)
			{
				continue;
			}
			// The same variable, not just the same name; a label only in its own graph. Functions, commands and
			// entities are the file's (and the world's), so their name is enough.
			if ((Group == ENameGroup::Variable && ScriptNames.GetVariableKey(LineIndex, Name) != Key)
				|| (Group == ENameGroup::Label && ScriptNames.GetLineGraph(LineIndex) != LabelGraph))
			{
				continue;
			}
			FTextLineHighlight Highlight(LineIndex, FTextRange(Token.Start, Token.Start + Token.Len), BangoSyntaxMarshallerPrivate::BracketHighlightZOrder, MatchHighlighter.ToSharedRef());
			LastLayout->AddLineHighlight(Highlight);
			NameHighlights.Add(Highlight);
		}
	}
}

void FBangoSyntaxMarshaller::UpdateBracketHighlights()
{
	if (LastLayout == nullptr)
	{
		return;
	}
	RemoveHighlights(BracketHighlights);

	const FBangoBracketMatch Match = FBangoEditingAids::FindMatchingBracket(CachedLines, CachedTokens, BracketCursor);
	if (!Match.bFound)
	{
		return;
	}
	const TSharedRef<ISlateLineHighlighter> Highlighter = (Match.bMatched ? MatchHighlighter : UnmatchedHighlighter).ToSharedRef();
	const int32 NumLines = LastLayout->GetLineModels().Num();
	auto Add = [this, &Highlighter, NumLines](const FBangoTextPosition& At)
	{
		if (At.Line >= 0 && At.Line < NumLines)
		{
			FTextLineHighlight Highlight(At.Line, FTextRange(At.Column, At.Column + 1), BangoSyntaxMarshallerPrivate::BracketHighlightZOrder, Highlighter);
			LastLayout->AddLineHighlight(Highlight);
			BracketHighlights.Add(Highlight);
		}
	};
	Add(Match.Bracket);
	if (Match.bMatched)
	{
		Add(Match.Match);
	}
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
	int32 DisabledDepth = 0; // disabled regions open so far (they can run over several lines)
	for (int32 LineIndex = 0; LineIndex < LineRanges.Num(); ++LineIndex)
	{
		const FTextRange& LineRange = LineRanges[LineIndex];
		Lines.Add(SourceString.Mid(LineRange.BeginIndex, LineRange.Len()));
		FBangoLineTokenizer::TokenizeLine(FStringView(Lines[LineIndex]), LineTokens[LineIndex], DisabledDepth);
	}

	ScriptNames.Build(Lines, LineTokens);
	const FBangoScriptNames& Names = ScriptNames;

	// Kept for finding brackets when the cursor moves (before the lines are handed to the layout below).
	CachedLines = Lines;
	CachedTokens = LineTokens;

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
			AddRun(Style->GetStyle(Kind, Token.bDisconnected, Token.bDisabled), Token.Start, Token.Start + Token.Len);
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

	// The layout dropped every highlight before laying the text out again; put the bracket boxes back.
	LastLayout = &TargetTextLayout;
	BracketHighlights.Reset();
	UpdateBracketHighlights();
	NameHighlights.Reset();
	UpdateNameHighlights();
	ProbeHighlights.Reset();
	UpdateProbeHighlight();
	ErrorHighlights.Reset();
	UpdateErrorHighlight();
	GhostHighlights.Reset();
	UpdateGhostHighlight();
}

void FBangoSyntaxMarshaller::GetText(FString& TargetString, const FTextLayout& SourceTextLayout)
{
	SourceTextLayout.GetAsText(TargetString);
}
