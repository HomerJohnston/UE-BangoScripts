#include "BangoScriptReferences.h"

#include "BangoIDActorComponent.h"
#include "BangoLineTokenizer.h"
#include "BangoSettings.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "ShowFlags.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "BangoScriptReferences"

namespace BangoScriptReferencesPrivate
{
	/** Show > Bango Script References, in every viewport's Show menu. */
	static TCustomShowFlag<> ShowBangoScriptReferences(TEXT("BangoScriptReferences"), true /* on by default */, SFG_Normal,
		LOCTEXT("ShowBangoScriptReferences", "Bango Script References"));

	static FDelegateHandle DrawHandle;

	/** One @name in the script: where it is, for the reference under the cursor. */
	struct FReference
	{
		FString Name;
		int32 Line = 0;
		int32 Start = 0; // the '@'
		int32 Len = 0;   // with the '@'
	};

	/** The script being drawn. */
	static TWeakPtr<const SWidget> ActiveEditor;
	static TArray<FReference> References;
	/** Each name once, in the order the script first uses it. */
	static TArray<FString> Names;
	/** The name under the active editor's cursor, if it's on one. */
	static FString CursorName;
	/** When the active editor last ticked: its rings are drawn only while it's on screen. */
	static double LastTouchSeconds = -1.0;
	/** How long after its last tick the active editor's rings are still drawn (Slate may skip a few ticks). */
	constexpr double TouchTimeout = 1.0;

	const FLinearColor RingColor(0.0f, 0.85f, 1.0f);
	const FLinearColor MissingColor(1.0f, 0.3f, 0.25f);
	constexpr float RingThickness = 2.0f;
	constexpr float CursorRingThickness = 4.0f;
	constexpr float MinRingRadius = 14.0f;
	/** A ring around an actor with no bounds (an empty actor, a point): this radius in the world. */
	constexpr float BoundlessRadius = 50.0f;
	constexpr int32 RingSegments = 40;
	/** At most this many missing names are listed; the rest are counted. */
	constexpr int32 MaxMissingListed = 8;

	/** The same fade as the ID labels: 1 near the camera, to 0 between the settings' fade and hide distances. */
	static float GetFade(const FSceneView& View, const FVector& Location)
	{
		if (!View.IsPerspectiveProjection())
		{
			return 1.0f;
		}
		const UBangoSettings* Settings = GetDefault<UBangoSettings>();
		const float Hide = FMath::Max(0.0f, Settings->IDLabelHideDistance);
		const float FadeStart = FMath::Clamp(Settings->IDLabelFadeDistance, 0.0f, Hide);
		const float Distance = FVector::Dist(View.ViewMatrices.GetViewOrigin(), Location);
		if (Distance >= Hide)
		{
			return 0.0f;
		}
		if (Distance <= FadeStart || Hide <= FadeStart)
		{
			return 1.0f;
		}
		return 1.0f - (Distance - FadeStart) / (Hide - FadeStart);
	}

	static void DrawRing(UCanvas& Canvas, const FVector2D& Center, float Radius, float Thickness, const FLinearColor& Color)
	{
		FVector2D Previous = Center + FVector2D(Radius, 0.0f);
		for (int32 i = 1; i <= RingSegments; ++i)
		{
			const float Angle = 2.0f * PI * i / RingSegments;
			const FVector2D Next = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
			FCanvasLineItem Line(Previous, Next);
			Line.LineThickness = Thickness;
			Line.SetColor(Color);
			Line.BlendMode = SE_BLEND_Translucent;
			Canvas.DrawItem(Line);
			Previous = Next;
		}
	}

	/** A ring around Actor's bounds as they look on screen, with a dark edge so it shows on any background. */
	static void DrawActorRing(UCanvas& Canvas, const FSceneView& View, const AActor& Actor, bool bUnderCursor)
	{
		FVector Origin;
		FVector Extent;
		Actor.GetActorBounds(false, Origin, Extent, true);
		const float WorldRadius = Extent.IsNearlyZero() ? BoundlessRadius : (float)Extent.Size();
		if (Extent.IsNearlyZero())
		{
			Origin = Actor.GetActorLocation();
		}
		if (View.IsPerspectiveProjection() && FVector::DotProduct(Origin - View.ViewMatrices.GetViewOrigin(), View.GetViewDirection()) <= 0.0f)
		{
			return; // behind the camera
		}
		const float Fade = GetFade(View, Origin);
		if (Fade <= 0.0f)
		{
			return;
		}
		const FVector Center = Canvas.Project(Origin);
		const FVector Edge = Canvas.Project(Origin + View.GetViewRight() * WorldRadius);
		const float MaxRadius = 0.45f * FMath::Min(Canvas.ClipX, Canvas.ClipY);
		const float Radius = FMath::Clamp((float)FVector2D::Distance(FVector2D(Center), FVector2D(Edge)), MinRingRadius, FMath::Max(MinRingRadius, MaxRadius));

		const float Thickness = bUnderCursor ? CursorRingThickness : RingThickness;
		DrawRing(Canvas, FVector2D(Center), Radius, Thickness + 2.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f * Fade));
		FLinearColor Color = RingColor;
		Color.A *= Fade;
		DrawRing(Canvas, FVector2D(Center), Radius, Thickness, Color);
	}

	/** The names no actor in the level has, listed in the bottom-left corner. */
	static void DrawMissing(UCanvas& Canvas, const TArray<FString>& Missing)
	{
		UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
		if (!Font || Missing.Num() == 0)
		{
			return;
		}
		TArray<FString> Lines;
		Lines.Add(LOCTEXT("MissingHeading", "Not in this level (Bango script being edited):").ToString());
		for (int32 i = 0; i < Missing.Num() && i < MaxMissingListed; ++i)
		{
			Lines.Add(TEXT("    @") + Missing[i]);
		}
		if (Missing.Num() > MaxMissingListed)
		{
			Lines.Add(FText::Format(LOCTEXT("MissingMore", "    and {0} more"), FText::AsNumber(Missing.Num() - MaxMissingListed)).ToString());
		}

		float Width = 0.0f;
		float LineHeight = 0.0f;
		Canvas.TextSize(Font, Lines[0], Width, LineHeight);
		FFontRenderInfo RenderInfo;
		RenderInfo.bEnableShadow = true;
		Canvas.SetDrawColor(MissingColor.ToFColor(true));
		float Y = Canvas.ClipY - 10.0f - LineHeight * Lines.Num();
		for (const FString& Line : Lines)
		{
			Canvas.DrawText(Font, Line, 10.0f, FMath::RoundToFloat(Y), 1.0f, 1.0f, RenderInfo);
			Y += LineHeight;
		}
	}

	static void Draw(UCanvas* Canvas, APlayerController* PlayerController)
	{
		if (!Canvas || Names.Num() == 0 || !ActiveEditor.IsValid() || FPlatformTime::Seconds() - LastTouchSeconds > TouchTimeout)
		{
			return;
		}
		const FSceneView* View = Canvas->SceneView;
		const FSceneInterface* Scene = View && View->Family ? View->Family->Scene : nullptr;
		UWorld* World = Scene ? Scene->GetWorld() : nullptr;
		if (!World || World->WorldType != EWorldType::Editor)
		{
			return; // the level editor only: not while playing, not asset editors' previews
		}

		// The actors in this level with each of the script's names (more than one, if a name is used twice).
		TMap<FString, TArray<const AActor*, TInlineAllocator<1>>> Actors;
		for (TObjectIterator<UBangoIDActorComponent> It; It; ++It)
		{
			const UBangoIDActorComponent* Component = *It;
			if (!IsValid(Component) || Component->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || Component->GetWorld() != World)
			{
				continue;
			}
			const FString& Name = Component->BangoName.ToString();
			if (Name.IsEmpty() || !Names.Contains(Name))
			{
				continue;
			}
			const AActor* Actor = Component->GetOwner();
			if (IsValid(Actor))
			{
				Actors.FindOrAdd(Name).Add(Actor);
			}
		}

		TArray<FString> Missing;
		for (const FString& Name : Names)
		{
			const auto* Found = Actors.Find(Name);
			if (!Found)
			{
				Missing.Add(Name);
				continue;
			}
			for (const AActor* Actor : *Found)
			{
				if (!Actor->IsHiddenEd()) // hidden in the editor: it's there, so not missing, but not drawn either
				{
					DrawActorRing(*Canvas, *View, *Actor, Name == CursorName);
				}
			}
		}
		DrawMissing(*Canvas, Missing);
	}
}

namespace BangoScriptReferences
{
	using namespace BangoScriptReferencesPrivate;

	void Register()
	{
		if (!DrawHandle.IsValid())
		{
			DrawHandle = UDebugDrawService::Register(TEXT("BangoScriptReferences"), FDebugDrawDelegate::CreateStatic(&Draw));
		}
	}

	void Unregister()
	{
		if (DrawHandle.IsValid())
		{
			UDebugDrawService::Unregister(DrawHandle);
			DrawHandle.Reset();
		}
		ActiveEditor.Reset();
		References.Reset();
		Names.Reset();
	}

	void SetActiveScript(const TSharedRef<const SWidget>& Editor, const TArray<FString>& Lines)
	{
		ActiveEditor = Editor;
		References.Reset();
		Names.Reset();
		CursorName.Reset();

		// The editor's own colouring tokenizer: it knows comments, strings and node metadata, so only real @names count,
		// and it doesn't need the script to compile.
		TArray<FBangoSyntaxToken> Tokens;
		int32 DisabledDepth = 0;
		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString& Line = Lines[LineIndex];
			Tokens.Reset();
			FBangoLineTokenizer::TokenizeLine(Line, Tokens, DisabledDepth);
			for (const FBangoSyntaxToken& Token : Tokens)
			{
				// Entities are @john, and the player / self keywords, which aren't Bango IDs.
				if (Token.Kind != EBangoSyntaxKind::Entity || Token.Len < 2 || Line[Token.Start] != TEXT('@'))
				{
					continue;
				}
				FReference& Reference = References.AddDefaulted_GetRef();
				Reference.Name = Line.Mid(Token.Start + 1, Token.Len - 1);
				Reference.Line = LineIndex;
				Reference.Start = Token.Start;
				Reference.Len = Token.Len;
				Names.AddUnique(Reference.Name);
			}
		}
	}

	bool IsActiveScript(const SWidget* Editor)
	{
		const TSharedPtr<const SWidget> Active = ActiveEditor.Pin();
		return Editor && Active.Get() == Editor;
	}

	void TouchActiveScript(const SWidget* Editor, int32 CursorLine, int32 CursorColumn)
	{
		if (!IsActiveScript(Editor))
		{
			return;
		}
		LastTouchSeconds = FPlatformTime::Seconds();
		CursorName.Reset();
		for (const FReference& Reference : References)
		{
			// The cursor is on it, or just after it (as it is right after typing it).
			if (Reference.Line == CursorLine && CursorColumn >= Reference.Start && CursorColumn <= Reference.Start + Reference.Len)
			{
				CursorName = Reference.Name;
				break;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
