// Bango text editor: turns script text into coloured Slate text runs.
// See "Bango Text Editor Spec", section 4.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/BaseTextLayoutMarshaller.h"

class FBangoSyntaxStyle;

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

	// ITextLayoutMarshaller
	virtual void SetText(const FString& SourceString, FTextLayout& TargetTextLayout) override;
	virtual void GetText(FString& TargetString, const FTextLayout& SourceTextLayout) override;

	/** Re-colour while typing, not only when the text is set from outside. */
	virtual bool RequiresLiveUpdate() const override { return true; }

protected:
	explicit FBangoSyntaxMarshaller(TSharedRef<const FBangoSyntaxStyle> InStyle);

private:
	TSharedRef<const FBangoSyntaxStyle> Style;
	int32 TabSize = 4;
	bool bMetadataIsError = false;
};
