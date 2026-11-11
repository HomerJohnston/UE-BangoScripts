#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BangoInstance.h"
#include "BangoScriptSubsystem.generated.h"

/** Refers to a spawned script instance. A default (zero) handle refers to nothing. */
USTRUCT(BlueprintType)
struct BANGOSCRIPTS_API FBangoScriptHandle
{
	GENERATED_BODY()

	UPROPERTY()
	int64 Id = 0;

	bool IsValid() const { return Id != 0; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBangoHintEvent, const FText&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBangoScriptFailedEvent, FBangoScriptHandle, Handle, const FString&, Error);

/**
 * Runs Bango scripts in one game world (spec section 10).
 *
 * Every frame, each running script resumes and runs until it pauses (on Sleep or another latent
 * command), finishes or fails. Scripts pause while the game is paused. Errors are logged and shown
 * on screen, and only stop the script that hit them.
 */
UCLASS()
class BANGOSCRIPTS_API UBangoScriptSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem for WorldContext's world, or nullptr (e.g. in an editor world). */
	static UBangoScriptSubsystem* Get(const UObject* WorldContext);

	/**
	 * Compiles (if needed) and starts the script at ScriptPath ("Quest/Intro" means Content/Bango/Quest/Intro.bango).
	 * Args are matched to the script's params line in order (see FBangoInstance::Start); use Bango::ActorToEntity
	 * for entity arguments. The script runs straight away until it first pauses. Returns an invalid handle and
	 * shows an error if the script can't be found, doesn't compile, the arguments don't fit its parameters, or
	 * one of its globals conflicts with an existing global.
	 */
	FBangoScriptHandle Spawn(const FString& ScriptPath, const TArray<FBangoValue>& Args = TArray<FBangoValue>());

	/** Stops a script. Does nothing if it has already ended. */
	void Terminate(FBangoScriptHandle Handle);

	/** True while the script is running or paused. False once it has finished, failed or been terminated. */
	bool IsRunning(FBangoScriptHandle Handle) const;

	void TerminateAll();
	int32 GetNumRunning() const { return Scripts.Num(); }

	/**
	 * Saves every running script and every global variable. Restore with LoadState, typically into a
	 * USaveGame byte array. Loading state into scripts that changed since saving is not supported.
	 */
	bool SaveState(TArray<uint8>& OutData);

	/** Replaces all running scripts and globals with saved ones. Returns false (with an error shown) if the data is unreadable. */
	bool LoadState(const TArray<uint8>& Data);

	/** Stops all scripts and clears all globals (except those for actors in the level), e.g. when starting a new game. */
	void ResetState();

	/** Used by the Hint command: broadcasts OnHint, or shows the text on screen if nothing is bound. */
	void ShowHint(const FText& Text);

	/**
	 * Fired by the Hint command, with the text in the current language. Bind this to show hints in your own UI;
	 * while bound, hints aren't shown as debug text. The text follows language changes, so a UI can keep it.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Bango")
	FBangoHintEvent OnHint;

	/** Fired when a script stops with an error (the error is also logged and shown on screen). */
	UPROPERTY(BlueprintAssignable, Category = "Bango")
	FBangoScriptFailedEvent OnScriptFailed;

	// UTickableWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FRunningScript
	{
		FString Path;
		FBangoInstance Instance;
	};

	bool MakeContext(float DeltaSeconds, FBangoExecContext& OutContext);
	/** Resumes one script. Returns false once it has ended (for any reason). */
	bool ResumeScript(FRunningScript& Script, float DeltaSeconds);
	void ReportFailure(const FRunningScript& Script);

	TMap<int64, TSharedPtr<FRunningScript>> Scripts;
	int64 NextId = 1;
	/** Checks entity names against this world's UBangoActorRegistry. */
	TUniquePtr<IBangoEntityResolver> EntityResolver;
};
