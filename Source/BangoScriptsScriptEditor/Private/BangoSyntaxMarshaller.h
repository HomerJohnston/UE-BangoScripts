// Bango text editor: turns script text into coloured Slate text runs.
// See "Bango Text Editor Spec", section 4.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/BaseTextLayoutMarshaller.h"
#include "Framework/Text/TextLineHighlight.h"
#include "BangoEditingAids.h"
#include "BangoLineTokenizer.h"

class FBangoSyntaxStyle;
class ISlateLineHighlighter;

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

	TSharedRef<const FBangoSyntaxStyle> Style;

	/** The text as last laid out: its lines and their tokens, for finding brackets. */
	TArray<FString> CachedLines;
	TArray<TArray<FBangoSyntaxToken>> CachedTokens;

	/**
	 * The layout the text was last laid out in. It belongs to the text widget, which also owns this marshaller's
	 * only other reference, so it lives as long as the marshaller is used.
	 */
	FTextLayout* LastLayout = nullptr;

	/** The cursor, for the bracket boxes. */
	FBangoTextPosition BracketCursor;
	TArray<FTextLineHighlight> BracketHighlights;
	TSharedPtr<ISlateLineHighlighter> MatchHighlighter;
	TSharedPtr<ISlateLineHighlighter> UnmatchedHighlighter;

	int32 TabSize = 4;
	bool bMetadataIsError = false;
};
