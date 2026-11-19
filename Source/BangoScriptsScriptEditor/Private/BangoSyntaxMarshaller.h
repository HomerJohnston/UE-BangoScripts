// Bango text editor: turns script text into coloured Slate text runs.
// See "Bango Text Editor Spec", section 4.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/BaseTextLayoutMarshaller.h"
#include "Framework/Text/TextLineHighlight.h"
#include "BangoEditingAids.h"
#include "BangoLineTokenizer.h"
#include "BangoScriptNames.h"

class FBangoSyntaxStyle;
class ISlateLineHighlighter;

/**
 * Where some characters were last drawn, in paint space (Slate's "absolute" space while painting, which is relative to
 * the window). Filled while the text is painted; convert it with a widget's GetPaintSpaceGeometry().
 */
struct FBangoTextProbe
{
	bool bValid = false;
	FVector2D AbsolutePosition = FVector2D::ZeroVector;
	FVector2D AbsoluteSize = FVector2D::ZeroVector;
};

class FBangoSyntaxMarshaller : public FBaseTextLayoutMarshaller
{
public:
	static TSharedRef<FBangoSyntaxMarshaller> Create(TSharedRef<const FBangoSyntaxStyle> InStyle);

	/** Swaps the palette and re-colours the text. */
	void SetStyle(TSharedRef<const FBangoSyntaxStyle> InStyle);

	/** Tab stops, in columns: a tab moves the text on to the next multiple of this. Default 4. */
	void SetTabSize(int32 InTabSize);
	int32 GetTabSize() const { return TabSize; }

	/**
	 * Draws node metadata (//@...) as an error. For text whose metadata has been taken off and is shown elsewhere
	 * (SBangoTextEditor): any //@ left in it was typed by hand, which it shouldn't be.
	 */
	void SetMetadataIsError(bool bInMetadataIsError);

	/**
	 * Where the cursor is, so the bracket next to it and its match get a box behind them (a red one if it has no
	 * match). Call it when the cursor moves; the marshaller keeps the boxes when it lays the text out again.
	 */
	void SetCursor(const FBangoTextPosition& InCursor);

	/**
	 * Watches where characters BeginColumn to EndColumn of a line are drawn (for placing the autocomplete list beside
	 * the word being typed). Nothing is drawn; GetProbe says where they were, once the text has been painted.
	 */
	void SetProbe(int32 Line, int32 BeginColumn, int32 EndColumn);
	void ClearProbe();

	/**
	 * Underlines a compile error in red: the token at Line and Column (0-based), or the line's last character if the
	 * column is past its end. It stays until ClearError, through relayouts.
	 */
	void SetError(int32 Line, int32 Column);
	void ClearError();
	const FBangoTextProbe& GetProbe() const { return *Probe; }

	/**
	 * Shows Count faint `)`s after the end of Line: the brackets autocomplete left open, not yet typed. Count 0 (or
	 * Line INDEX_NONE) hides them. They stay until changed, through relayouts, at the end of whatever the line holds.
	 */
	void SetGhostParens(int32 Line, int32 Count);

	/** The text's locals, parameters, globals and graphs, as of the last layout. */
	const FBangoScriptNames& GetScriptNames() const { return ScriptNames; }

	/** An inline script's inputs given by its inputs signature: coloured and offered as the main graph's parameters. */
	void SetGivenInputs(const TArray<FBangoVisibleName>& Inputs)
	{
		ScriptNames.SetGivenInputs(Inputs);
		MakeDirty();
	}

	// ITextLayoutMarshaller
	virtual void SetText(const FString& SourceString, FTextLayout& TargetTextLayout) override;
	virtual void GetText(FString& TargetString, const FTextLayout& SourceTextLayout) override;

	/** Re-colour while typing, not only when the text is set from outside. */
	virtual bool RequiresLiveUpdate() const override { return true; }

protected:
	explicit FBangoSyntaxMarshaller(TSharedRef<const FBangoSyntaxStyle> InStyle);

private:
	/** Puts the bracket boxes on the layout for the current cursor, replacing the ones there. */
	void UpdateBracketHighlights();

	/**
	 * With the cursor in (or just after) a name: boxes every use of it, the one at the cursor included, in the bracket
	 * boxes' faint grey. A variable's are the same variable's, not just the same name's (FBangoScriptNames::
	 * GetVariableKey): a local in another block, or a global its graph's local hides, isn't boxed. A label's are in
	 * its graph. A function's or command's, and an @entity's, are any in the text. Keywords and directives aren't names.
	 */
	void UpdateNameHighlights();

	/** Puts the probe's highlight on the layout, replacing the one there. */
	void UpdateProbeHighlight();

	/** Puts the error's underline on the layout, replacing the one there. */
	void UpdateErrorHighlight();

	/** Puts the ghost `)`s on the layout, replacing the ones there. */
	void UpdateGhostHighlight();

	/** Takes Highlights off the layout (those on lines it still has) and empties the list. */
	void RemoveHighlights(TArray<FTextLineHighlight>& Highlights);

	TSharedRef<const FBangoSyntaxStyle> Style;

	/** The text as last laid out: its lines and their tokens, for finding brackets. */
	TArray<FString> CachedLines;
	TArray<TArray<FBangoSyntaxToken>> CachedTokens;
	FBangoScriptNames ScriptNames;

	/**
	 * The layout the text was last laid out in. It belongs to the text widget, which also owns this marshaller's
	 * only other reference, so it lives as long as the marshaller is used.
	 */
	FTextLayout* LastLayout = nullptr;

	/** The cursor, for the bracket boxes. */
	FBangoTextPosition BracketCursor;
	TArray<FTextLineHighlight> BracketHighlights;
	TArray<FTextLineHighlight> NameHighlights;
	TSharedPtr<ISlateLineHighlighter> MatchHighlighter;
	TSharedPtr<ISlateLineHighlighter> UnmatchedHighlighter;

	TSharedRef<FBangoTextProbe> Probe = MakeShared<FBangoTextProbe>();
	TSharedPtr<ISlateLineHighlighter> ProbeHighlighter;
	int32 ProbeLine = INDEX_NONE;
	FTextRange ProbeRange;
	TArray<FTextLineHighlight> ProbeHighlights;

	TSharedPtr<ISlateLineHighlighter> ErrorHighlighter;
	int32 ErrorLine = INDEX_NONE;
	int32 ErrorColumn = 0;
	TArray<FTextLineHighlight> ErrorHighlights;

	TSharedPtr<ISlateLineHighlighter> GhostHighlighter;
	int32 GhostLine = INDEX_NONE;
	int32 GhostCount = 0;
	TArray<FTextLineHighlight> GhostHighlights;

	int32 TabSize = 4;
	bool bMetadataIsError = false;
};
