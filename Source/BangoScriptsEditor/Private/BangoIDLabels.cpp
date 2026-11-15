#include "BangoIDLabels.h"

#include "BangoIDActorComponent.h"
#include "BangoSettings.h"
#include "BangoTypes.h"
#include "CanvasTypes.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "ShowFlags.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "BangoIDLabels"

namespace BangoIDLabelsPrivate
{
	/** Show > Bango IDs, in every viewport's Show menu. */
	static TCustomShowFlag<> ShowBangoIDs(TEXT("BangoIDs"), true /* on by default */, SFG_Normal, LOCTEXT("ShowBangoIDs", "Bango IDs"));

	static FDelegateHandle DrawHandle;

	/** How far above the top of the actor's bounds the label sits. */
	constexpr float LabelLift = 20.0f;

	const FLinearColor NameColor(1.0f, 1.0f, 1.0f);
	const FLinearColor ProblemColor(1.0f, 0.3f, 0.25f);

	struct FLabel
	{
		const AActor* Actor = nullptr;
		FString Name;
	};

	/** Where the label goes: centred over the actor, just above its bounds (its location, if it has none). */
	static FVector GetLabelLocation(const AActor& Actor)
	{
		FVector Origin;
		FVector Extent;
		Actor.GetActorBounds(false, Origin, Extent, true);
		if (Extent.IsNearlyZero())
		{
			return Actor.GetActorLocation() + FVector::UpVector * LabelLift;
		}
		return FVector(Origin.X, Origin.Y, Origin.Z + Extent.Z + LabelLift);
	}

	/** 1 near the camera, fading to 0 between the settings' fade distance and hide distance. Always 1 in an ortho view. */
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

	static void Draw(UCanvas* Canvas, APlayerController* PlayerController)
	{
		const FSceneView* View = Canvas ? Canvas->SceneView : nullptr;
		const FSceneInterface* Scene = View && View->Family ? View->Family->Scene : nullptr;
		UWorld* World = Scene ? Scene->GetWorld() : nullptr;
		if (!World || World->WorldType != EWorldType::Editor)
		{
			return; // the level editor only: not while playing, not asset editors' previews
		}

		// The level's Bango IDs, and how many actors use each name.
		TArray<FLabel> Labels;
		TMap<FString, int32> Uses;
		for (TObjectIterator<UBangoIDActorComponent> It; It; ++It)
		{
			const UBangoIDActorComponent* Component = *It;
			if (!IsValid(Component) || Component->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || Component->GetWorld() != World)
			{
				continue;
			}
			const AActor* Actor = Component->GetOwner();
			if (!IsValid(Actor) || Actor->IsHiddenEd())
			{
				continue;
			}
			FLabel& Label = Labels.AddDefaulted_GetRef();
			Label.Actor = Actor;
			Label.Name = Component->BangoName.ToString();
			if (!Label.Name.IsEmpty())
			{
				Uses.FindOrAdd(Label.Name)++;
			}
		}
		if (Labels.Num() == 0)
		{
			return;
		}

		UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
		if (!Font)
		{
			return;
		}
		const FVector ViewOrigin = View->ViewMatrices.GetViewOrigin();
		const FVector ViewForward = View->GetViewDirection();
		FFontRenderInfo RenderInfo;
		RenderInfo.bEnableShadow = true;

		for (const FLabel& Label : Labels)
		{
			const FVector Location = GetLabelLocation(*Label.Actor);
			if (View->IsPerspectiveProjection() && FVector::DotProduct(Location - ViewOrigin, ViewForward) <= 0.0f)
			{
				continue; // behind the camera
			}
			const float Fade = GetFade(*View, Location);
			if (Fade <= 0.0f)
			{
				continue;
			}
			const FVector Screen = Canvas->Project(Location);
			if (Screen.Z <= 0.0f && View->IsPerspectiveProjection())
			{
				continue;
			}

			FString Text;
			bool bProblem = true;
			if (Label.Name.IsEmpty())
			{
				Text = LOCTEXT("NoName", "Bango ID: no name").ToString();
			}
			else if (!Bango::IsValidBangoIDName(Label.Name))
			{
				Text = FText::Format(LOCTEXT("InvalidName", "@{0} (invalid name)"), FText::FromString(Label.Name)).ToString();
			}
			else if (Uses.FindRef(Label.Name) > 1)
			{
				Text = FText::Format(LOCTEXT("DuplicateName", "@{0} (used by another actor)"), FText::FromString(Label.Name)).ToString();
			}
			else
			{
				Text = TEXT("@") + Label.Name;
				bProblem = false;
			}

			float Width = 0.0f;
			float Height = 0.0f;
			Canvas->TextSize(Font, Text, Width, Height);
			FLinearColor Color = bProblem ? ProblemColor : NameColor;
			Color.A *= Fade;
			Canvas->SetDrawColor(Color.ToFColor(true));
			Canvas->DrawText(Font, Text, FMath::RoundToFloat(Screen.X - Width * 0.5f), FMath::RoundToFloat(Screen.Y - Height), 1.0f, 1.0f, RenderInfo);
		}
	}
}

namespace BangoIDLabels
{
	void Register()
	{
		using namespace BangoIDLabelsPrivate;
		if (!DrawHandle.IsValid())
		{
			DrawHandle = UDebugDrawService::Register(TEXT("BangoIDs"), FDebugDrawDelegate::CreateStatic(&Draw));
		}
	}

	void Unregister()
	{
		using namespace BangoIDLabelsPrivate;
		if (DrawHandle.IsValid())
		{
			UDebugDrawService::Unregister(DrawHandle);
			DrawHandle.Reset();
		}
	}
}

#undef LOCTEXT_NAMESPACE
