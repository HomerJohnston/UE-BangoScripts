#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BangoConditions.h"
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

/** A script instance as the editor's debugger sees it (UBangoScriptSubsystem::GetDebugInfo). */
struct FBangoScriptDebugInfo
{
	int64 Id = 0;
	/** The script, like "Quest/Intro.bango". */
	FString Path;
	EBangoInstanceStatus Status = EBangoInstanceStatus::None;
	/** The calls in progress, the main graph first and the innermost (where it is) last. Empty once it has ended. */
	TArray<FBangoCallFrameInfo> CallStack;
	/** The command it's waiting on, like "Sleep", or empty. */
	FString WaitingOn;
	/** Failed only: the error, with the node it happened at. */
	FBangoRuntimeError Error;
	/**
	 * Failed only: the last nodes it ran, up to the error (at most FBangoInstance::TrailSize, oldest first), and how
	 * many it ran in all (see FBangoInstance::GetTrail). A running script's trail comes from GetScriptTrail instead.
	 */
	TArray<int64> Trail;
	uint64 TrailCount = 0;
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

	/**
	 * Compiles Source, the text of a script, and starts it, like Spawn does with a file. Name is what errors show in
	 * place of a file name (like "Dialogue.Intro"), and what the debugger lists it as. Everything else is as for Spawn:
	 * Args fill its params line, it runs straight away until it first pauses, and an invalid handle is returned (with
	 * an error shown) if it doesn't compile or can't start. It's compiled on every call. Its text is kept with it, so
	 * SaveState saves it and LoadState can restore it; it isn't hot reloaded, since it has no file.
	 */
	FBangoScriptHandle SpawnFromSource(const FString& Source, const FString& Name = TEXT("Script"), const TArray<FBangoValue>& Args = TArray<FBangoValue>());

	/**
	 * Runs a condition registered with Bango::RegisterCondition and gives its answer. It runs straight away, to the end,
	 * in this world, with its globals and actors; Args fill its #define params line, if it has one.
	 *
	 * Returns true, with the answer in bOutResult, if it ran and returned true or false. Returns false, with the error in
	 * OutError (and bOutResult false), if the handle isn't registered, its arguments don't fit, it stops with an error, it
	 * reaches its end without returning, or it calls a command that waits (like Sleep): a condition must answer at once.
	 * The error isn't shown or logged here, so a caller checking often can say so once (Bango::ReportError shows one).
	 *
	 * Conditions aren't running scripts: they're not listed by the debugger, saved, or reloaded.
	 */
	bool EvaluateBoolCondition(FBangoConditionHandle Condition, bool& bOutResult, FString& OutError,
		const TArray<FBangoValue>& Args = TArray<FBangoValue>());

	/**
	 * Compiles and runs a condition given as text (see Bango::CompileCondition), as the other EvaluateBoolCondition does.
	 * Name is what errors show in place of a file name. It's compiled on every call and nothing is kept, so for a condition
	 * checked often, register it once with Bango::RegisterCondition instead.
	 */
	bool EvaluateBoolCondition(const FString& Source, bool& bOutResult, FString& OutError,
		const TArray<FBangoValue>& Args = TArray<FBangoValue>(), const FString& Name = TEXT("Condition"));

	/** Stops a script. Does nothing if it has already ended. */
	void Terminate(FBangoScriptHandle Handle);

	/** True while the script is running or paused. False once it has finished, failed or been terminated. */
	bool IsRunning(FBangoScriptHandle Handle) const;

	void TerminateAll();
	int32 GetNumRunning() const { return Scripts.Num(); }

	/**
	 * For the editor's debugger: every running script, in the order they were spawned, then (in editor builds) the
	 * ones that recently stopped with an error, so the error can be looked at. See ForgetEndedScripts.
	 */
	void GetDebugInfo(TArray<FBangoScriptDebugInfo>& OutScripts) const;

	/**
	 * For the debugger's trail: the nodes script Id ran after the first After of them (see FBangoInstance::GetTrail).
	 * Returns how many it has run in all, or 0 if it isn't running.
	 */
	uint64 GetScriptTrail(int64 Id, uint64 After, TArray<int64>& OutNodeIDs) const;

	/** Forgets the scripts that stopped with an error (they're no longer listed by GetDebugInfo). */
	void ForgetEndedScripts() { EndedScripts.Reset(); }

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
		/** The script's path, or for a script spawned from text (SpawnFromSource), its name. */
		FString Path;
		/** True for a script spawned from text (SpawnFromSource): Source is its text, and it has no file. */
		bool bFromSource = false;
		FString Source;
		FBangoInstance Instance;
		/** The newest version it's been asked to switch to (hot reload), so each version is asked for once. */
		TSharedPtr<const FBangoProgram> ReloadTarget;
	};

	bool MakeContext(float DeltaSeconds, FBangoExecContext& OutContext);
	/** Starts a compiled script (Spawn and SpawnFromSource). */
	FBangoScriptHandle StartScript(const TSharedPtr<const FBangoProgram>& Program, TSharedPtr<FRunningScript> Script, const TArray<FBangoValue>& Args);
	/** Compiles a script's text (SpawnFromSource, and loading one that was saved). */
	static bool CompileFromSource(const FString& Source, const FString& Name, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError);
	/** Resumes one script. Returns false once it has ended (for any reason). */
	bool ResumeScript(FRunningScript& Script, float DeltaSeconds);
	void ReportFailure(const FRunningScript& Script);

	/** Hot reload, in the editor: asks the running copies of every script whose file changed to switch to its new version. */
	void ReloadChangedScripts();
	/** Reports what became of a script's reload, if anything did. */
	void ReportReload(FRunningScript& Script);
	/** Seconds until the next check for changed script files. */
	float ReloadCheckCountdown = 0.0f;
	/** The last compile error reported for each changed script, so each error is reported once. */
	TMap<FString, FString> ReloadErrors;

	/** Runs a compiled condition (EvaluateBoolCondition). */
	bool RunCondition(const TSharedPtr<const FBangoProgram>& Program, const TArray<FBangoValue>& Args, bool& bOutResult, FString& OutError);

	/** Editor builds: scripts that stopped with an error, newest last, for the debugger (at most MaxEndedScripts). */
	TArray<FBangoScriptDebugInfo> EndedScripts;
	static constexpr int32 MaxEndedScripts = 32;

	TMap<int64, TSharedPtr<FRunningScript>> Scripts;
	int64 NextId = 1;
	/** Checks entity names against this world's UBangoActorRegistry. */
	TUniquePtr<IBangoEntityResolver> EntityResolver;
};
