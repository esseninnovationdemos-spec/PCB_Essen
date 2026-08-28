#include "FactoryRenderCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "CineCameraActor.h"
#include "EngineUtils.h"
#include "FactorySimTypes.h"
#include "FileHelpers.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelinePrimaryConfig.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace PlantRender
{
	const FString CinematicFolder = TEXT("/Game/Cinematics");
	const FString SequenceName = TEXT("LS_PlantFlythrough");
	const FString ConfigName = TEXT("MRQ_PlantFlythrough");

	/**
	 * One waypoint on the camera path, in metres and degrees.
	 *
	 * An overview: down the hall's centreline, above the middle line, looking
	 * along its length.
	 *
	 * Where the camera can go is decided by the hall, which ships as a storage
	 * warehouse. Its racking is culled only from a 2.2 m corridor either side of
	 * each lane centre (-5, 0 and +5 m), leaving the strips [-7.2,-2.8],
	 * [-2.2,2.2] and [2.8,7.2] clear and everything between them full of
	 * shelving. And the building is low: the roof ridge measures under 5 m, so
	 * anything above about 4.4 m is outside looking at the roof rather than
	 * inside looking at the plant. A genuine bird's-eye does not fit in this
	 * building.
	 *
	 * What does fit is x = 0, which is both the middle of the factory and a
	 * cleared corridor, at 3.9-4.4 m: over the top of line 2's machines, under
	 * the trusses, with the racking rows either side framing the shot and the
	 * lines receding down the hall. The travel stops around y = -1 m because
	 * past that the racking ends and the view opens onto the end wall and the
	 * loading doors, which is a weaker picture than the framed one.
	 */
	struct FWaypoint
	{
		double TimeSeconds;
		double X, Y, Z;      // metres
		double Pitch, Yaw;   // degrees
	};

	// About 1 m/s down the centreline, easing out of a slight crane-down. The
	// yaw drifts by three degrees over the whole shot -- not enough to read as a
	// pan, enough that the frame is never completely static.
	const FWaypoint Path[] = {
		{  0.0, 0.0, -15.5, 4.35, -21.0, 90.0 },
		{  2.5, 0.0, -13.2, 4.25, -20.0, 90.5 },
		{  5.0, 0.0, -10.6, 4.15, -19.0, 91.0 },
		{  7.5, 0.0,  -8.0, 4.05, -18.0, 91.5 },
		{ 10.0, 0.0,  -5.4, 3.98, -17.0, 92.0 },
		{ 12.0, 0.0,  -3.2, 3.92, -16.0, 92.5 },
		{ 13.5, 0.0,  -1.5, 3.88, -15.0, 93.0 },
	};

	constexpr double MetresToCm = 100.0;

	/**
	 * A fixed viewpoint, overriding the flythrough path.
	 *
	 * Exists because there is no other way to photograph a chosen spot in this
	 * level: BugItGo needs a cheat manager the packaged game does not create, so
	 * a running -game session always renders from wherever the player start
	 * happens to be. Modelling work needs to be looked at from a particular
	 * angle, and this is the tool that does it.
	 */
	bool bStillCamera = false;
	FVector StillLocation = FVector::ZeroVector;
	FRotator StillRotation = FRotator::ZeroRotator;

	/** Parses "x,y,z" in metres. Returns false if it is not three numbers. */
	bool ParseVector(const FString& Text, FVector& Out)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 3)
		{
			return false;
		}
		Out = FVector(
			FCString::Atod(*Parts[0]) * MetresToCm,
			FCString::Atod(*Parts[1]) * MetresToCm,
			FCString::Atod(*Parts[2]) * MetresToCm);
		return true;
	}

	/** Linear interpolation between the waypoints bracketing a time. */
	void SampleAt(const double Time, FVector& OutLocation, FRotator& OutRotation)
	{
		if (bStillCamera)
		{
			OutLocation = StillLocation;
			OutRotation = StillRotation;
			return;
		}

		const int32 Count = UE_ARRAY_COUNT(Path);

		int32 Next = 1;
		while (Next < Count - 1 && Path[Next].TimeSeconds < Time)
		{
			++Next;
		}
		const FWaypoint& A = Path[Next - 1];
		const FWaypoint& B = Path[Next];

		const double Span = B.TimeSeconds - A.TimeSeconds;
		const double Alpha = (Span > 0.0)
			? FMath::Clamp((Time - A.TimeSeconds) / Span, 0.0, 1.0) : 0.0;

		OutLocation = FVector(
			FMath::Lerp(A.X, B.X, Alpha) * MetresToCm,
			FMath::Lerp(A.Y, B.Y, Alpha) * MetresToCm,
			FMath::Lerp(A.Z, B.Z, Alpha) * MetresToCm);
		OutRotation = FRotator(
			FMath::Lerp(A.Pitch, B.Pitch, Alpha),
			FMath::Lerp(A.Yaw, B.Yaw, Alpha),
			0.0);
	}

	template <typename T>
	T* CreateAsset(const FString& Folder, const FString& AssetName)
	{
		const FString PackageName = Folder / AssetName;
		UPackage* Package = CreatePackage(*PackageName);
		if (Package == nullptr)
		{
			return nullptr;
		}
		Package->FullyLoad();

		// NewObject cannot claim a name something else still holds.
		if (UObject* Existing = StaticFindObject(nullptr, Package, *AssetName))
		{
			Existing->Rename(nullptr, GetTransientPackage(),
				REN_DontCreateRedirectors | REN_DoNotDirty);
		}

		T* Asset = NewObject<T>(Package, T::StaticClass(), FName(*AssetName),
			RF_Public | RF_Standalone);
		if (Asset != nullptr)
		{
			FAssetRegistryModule::AssetCreated(Asset);
			Package->MarkPackageDirty();
		}
		return Asset;
	}

	bool SaveAsset(UObject* Asset)
	{
		if (Asset == nullptr)
		{
			return false;
		}
		UPackage* Package = Asset->GetOutermost();
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *FileName, Args);
	}
}

using namespace PlantRender;

UFactoryRenderCommandlet::UFactoryRenderCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UFactoryRenderCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	const FString LevelPath = ParamMap.Contains(TEXT("Level"))
		? ParamMap[TEXT("Level")] : TEXT("/Game/level4");

	// -From and -LookAt, both in metres, switch the whole render to a single
	// held viewpoint written as PNG rather than a moving shot written as MP4.
	// Useful for looking at one piece of geometry; useless for anything else.
	FVector From = FVector::ZeroVector;
	FVector LookAt = FVector::ZeroVector;
	if (ParamMap.Contains(TEXT("From")) && ParamMap.Contains(TEXT("LookAt")))
	{
		if (ParseVector(ParamMap[TEXT("From")], From)
			&& ParseVector(ParamMap[TEXT("LookAt")], LookAt))
		{
			bStillCamera = true;
			StillLocation = From;
			StillRotation = (LookAt - From).Rotation();
		}
		else
		{
			UE_LOG(LogFactorySim, Error,
				TEXT("-From and -LookAt each want three comma-separated metres, "
					 "e.g. -From=-3.9,-12.0,1.5"));
			return 1;
		}
	}

	double Seconds = ParamMap.Contains(TEXT("Seconds"))
		? FMath::Clamp(FCString::Atod(*ParamMap[TEXT("Seconds")]), 1.0, 120.0)
		: Path[UE_ARRAY_COUNT(Path) - 1].TimeSeconds;

	// A held camera needs a handful of frames, not a shot: enough for the
	// pipeline to warm up its temporal samples and settle, and no more.
	if (bStillCamera)
	{
		Seconds = 0.2;
	}

	// Up to 120 so a high-frame-rate pass is available, defaulting to 60: that
	// is smooth, and every player handles it. Above about 60 the returns are
	// small and playback support gets patchy, so going higher is a deliberate
	// choice rather than the default.
	const int32 Fps = ParamMap.Contains(TEXT("Fps"))
		? FMath::Clamp(FCString::Atoi(*ParamMap[TEXT("Fps")]), 12, 120) : 60;

	const FIntPoint Resolution(
		ParamMap.Contains(TEXT("Width")) ? FCString::Atoi(*ParamMap[TEXT("Width")]) : 1920,
		ParamMap.Contains(TEXT("Height")) ? FCString::Atoi(*ParamMap[TEXT("Height")]) : 1080);

	if (bStillCamera)
	{
		UE_LOG(LogFactorySim, Display,
			TEXT("Authoring a still over %s from (%.1f, %.1f, %.1f) m looking at "
				 "(%.1f, %.1f, %.1f) m, %dx%d"),
			*LevelPath,
			From.X / MetresToCm, From.Y / MetresToCm, From.Z / MetresToCm,
			LookAt.X / MetresToCm, LookAt.Y / MetresToCm, LookAt.Z / MetresToCm,
			Resolution.X, Resolution.Y);
	}
	else
	{
		UE_LOG(LogFactorySim, Display,
			TEXT("Authoring the flythrough over %s: %.1f s at %d fps, %dx%d"),
			*LevelPath, Seconds, Fps, Resolution.X, Resolution.Y);
	}

	// ---------------------------------------------------------------------
	// The camera, which lives in the level
	// ---------------------------------------------------------------------

	TArray<FString> ToLoad;
	ToLoad.Add(LevelPath);
	if (!UEditorLoadingAndSavingUtils::LoadMap(LevelPath))
	{
		UE_LOG(LogFactorySim, Error, TEXT("Could not load %s"), *LevelPath);
		return 1;
	}

	UWorld* World = GWorld;
	if (World == nullptr)
	{
		UE_LOG(LogFactorySim, Error, TEXT("No world after loading %s"), *LevelPath);
		return 1;
	}

	ACineCameraActor* Camera = nullptr;
	for (TActorIterator<ACineCameraActor> It(World); It; ++It)
	{
		Camera = *It;
		break;
	}
	if (Camera == nullptr)
	{
		UE_LOG(LogFactorySim, Error,
			TEXT("No CineCameraActor in %s; rebuild the level so it places RenderCam."),
			*LevelPath);
		return 1;
	}
	UE_LOG(LogFactorySim, Display, TEXT("  camera '%s'"), *Camera->GetActorLabel());

	// ---------------------------------------------------------------------
	// The sequence
	// ---------------------------------------------------------------------

	ULevelSequence* Sequence = CreateAsset<ULevelSequence>(CinematicFolder, SequenceName);
	if (Sequence == nullptr)
	{
		UE_LOG(LogFactorySim, Error, TEXT("Could not create the sequence asset"));
		return 1;
	}
	Sequence->Initialize();

	UMovieScene* MovieScene = Sequence->GetMovieScene();

	// Tick resolution is deliberately a large multiple of the display rate so
	// every frame boundary lands on a whole tick and the keys stay exact.
	const FFrameRate DisplayRate(Fps, 1);
	const FFrameRate TickResolution(24000, 1);
	MovieScene->SetDisplayRate(DisplayRate);
	MovieScene->SetTickResolutionDirectly(TickResolution);

	const int32 FrameCount = FMath::RoundToInt(Seconds * Fps);
	const FFrameNumber EndTick = ConvertFrameTime(
		FFrameTime(FrameCount), DisplayRate, TickResolution).RoundToFrame();
	MovieScene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), EndTick));

	const FGuid CameraGuid =
		MovieScene->AddPossessable(Camera->GetActorLabel(), Camera->GetClass());
	Sequence->BindPossessableObject(CameraGuid, *Camera, World);

	UMovieScene3DTransformTrack* TransformTrack =
		MovieScene->AddTrack<UMovieScene3DTransformTrack>(CameraGuid);
	UMovieScene3DTransformSection* TransformSection =
		Cast<UMovieScene3DTransformSection>(TransformTrack->CreateNewSection());
	TransformSection->SetRange(TRange<FFrameNumber>::All());
	TransformTrack->AddSection(*TransformSection);

	// Nine channels in order: location XYZ, rotation XYZ, scale XYZ.
	TArrayView<FMovieSceneDoubleChannel*> Channels =
		TransformSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
	if (Channels.Num() < 6)
	{
		UE_LOG(LogFactorySim, Error,
			TEXT("Transform section exposed %d channels; expected at least 6"),
			Channels.Num());
		return 1;
	}

	for (int32 Frame = 0; Frame <= FrameCount; ++Frame)
	{
		const double Time = (Fps > 0) ? (static_cast<double>(Frame) / Fps) : 0.0;

		FVector Location;
		FRotator Rotation;
		SampleAt(Time, Location, Rotation);

		const FFrameNumber Tick = ConvertFrameTime(
			FFrameTime(Frame), DisplayRate, TickResolution).RoundToFrame();

		// Cubic with automatic tangents. A key on every frame would ride fine on
		// linear interpolation, but auto tangents smooth the joins between
		// waypoints so the camera eases through the turns instead of hinging at
		// each one.
		Channels[0]->AddCubicKey(Tick, Location.X, RCTM_Auto);
		Channels[1]->AddCubicKey(Tick, Location.Y, RCTM_Auto);
		Channels[2]->AddCubicKey(Tick, Location.Z, RCTM_Auto);
		Channels[3]->AddCubicKey(Tick, Rotation.Roll, RCTM_Auto);
		Channels[4]->AddCubicKey(Tick, Rotation.Pitch, RCTM_Auto);
		Channels[5]->AddCubicKey(Tick, Rotation.Yaw, RCTM_Auto);
	}

	// Without a camera cut track the render uses the player's view, not this
	// camera, and the whole path is ignored.
	UMovieSceneCameraCutTrack* CutTrack =
		Cast<UMovieSceneCameraCutTrack>(
			MovieScene->AddTrack(UMovieSceneCameraCutTrack::StaticClass()));
	UMovieSceneCameraCutSection* CutSection =
		Cast<UMovieSceneCameraCutSection>(CutTrack->CreateNewSection());
	CutSection->SetRange(MovieScene->GetPlaybackRange());
	CutSection->SetCameraBindingID(UE::MovieScene::FRelativeObjectBindingID(CameraGuid));
	CutTrack->AddSection(*CutSection);

	// ---------------------------------------------------------------------
	// The render configuration
	// ---------------------------------------------------------------------

	UMoviePipelinePrimaryConfig* Config =
		CreateAsset<UMoviePipelinePrimaryConfig>(CinematicFolder, ConfigName);
	if (Config == nullptr)
	{
		UE_LOG(LogFactorySim, Error, TEXT("Could not create the render config asset"));
		return 1;
	}

	Config->FindOrAddSettingByClass(UMoviePipelineDeferredPassBase::StaticClass());

	// Resolved by name rather than linked against. The engine's MP4 encoder
	// exposes its output setting in a public header that includes one of the
	// module's own private headers, so including it from outside the module
	// does not compile. The class itself is perfectly usable once the module
	// is loaded -- it is only the header that is unreachable.
	if (bStillCamera)
	{
		// PNG, because the point of a still is to look at it, and a one-frame
		// MP4 is awkward to open and impossible to read programmatically.
		FModuleManager::Get().LoadModule(TEXT("MovieRenderPipelineRenderPasses"));
		UClass* PngOutput = FindObject<UClass>(nullptr,
			TEXT("/Script/MovieRenderPipelineRenderPasses.MoviePipelineImageSequenceOutput_PNG"));
		if (PngOutput == nullptr)
		{
			UE_LOG(LogFactorySim, Error, TEXT("PNG output class not found"));
			return 1;
		}
		Config->FindOrAddSettingByClass(PngOutput);
	}
	else
	{
		FModuleManager::Get().LoadModule(TEXT("MovieRenderPipelineMP4Encoder"));
		UClass* Mp4Output = FindObject<UClass>(nullptr,
			TEXT("/Script/MovieRenderPipelineMP4Encoder.MoviePipelineMP4EncoderOutput"));
		if (Mp4Output == nullptr)
		{
			UE_LOG(LogFactorySim, Error,
				TEXT("MP4 encoder output class not found; the render would produce "
					 "no video file. Is the MovieRenderPipeline plugin enabled?"));
			return 1;
		}
		Config->FindOrAddSettingByClass(Mp4Output);
	}

	if (UMoviePipelineOutputSetting* Output = Cast<UMoviePipelineOutputSetting>(
		Config->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass())))
	{
		Output->OutputResolution = Resolution;
		Output->OutputDirectory.Path = TEXT("{project_dir}/Saved/Renders/");
		// The frame rate is in the name so passes at different rates sit side by
		// side instead of the last one silently replacing the one before it.
		Output->FileNameFormat = bStillCamera
			? FString(TEXT("Still.{frame_number}"))
			: FString::Printf(TEXT("PlantFlythrough_%dfps"), Fps);
		Output->bUseCustomFrameRate = true;
		Output->OutputFrameRate = DisplayRate;
		Output->bOverrideExistingOutput = true;
	}

	if (UMoviePipelineAntiAliasingSetting* AntiAliasing =
		Cast<UMoviePipelineAntiAliasingSetting>(Config->FindOrAddSettingByClass(
			UMoviePipelineAntiAliasingSetting::StaticClass())))
	{
		// Eight temporal samples per frame. This is what buys real motion blur
		// and clean edges on the roof trusses -- the thin diagonals alias badly
		// on a single sample, and aliasing on a moving camera reads as the whole
		// image crawling.
		AntiAliasing->SpatialSampleCount = 1;
		AntiAliasing->TemporalSampleCount = 8;
		// A few frames discarded at each cut so temporal effects have converged
		// before anything is written.
		AntiAliasing->RenderWarmUpCount = 32;
		AntiAliasing->bUseCameraCutForWarmUp = true;
	}

	const bool bSaved = SaveAsset(Sequence) && SaveAsset(Config);
	if (!bSaved)
	{
		UE_LOG(LogFactorySim, Error, TEXT("Failed to save the cinematic assets"));
		return 1;
	}

	UE_LOG(LogFactorySim, Display,
		TEXT("Wrote %s/%s and %s/%s: %d frames over %.1f s"),
		*CinematicFolder, *SequenceName, *CinematicFolder, *ConfigName,
		FrameCount, Seconds);
	UE_LOG(LogFactorySim, Display,
		TEXT("Render with: -game -LevelSequence=\"%s/%s.%s\" "
			 "-MoviePipelineConfig=\"%s/%s.%s\""),
		*CinematicFolder, *SequenceName, *SequenceName,
		*CinematicFolder, *ConfigName, *ConfigName);
	return 0;
}
