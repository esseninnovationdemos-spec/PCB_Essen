#include "FactoryBuildButcheryCommandlet.h"

#include "Animation/AnimSequence.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FactoryShapeMaterials.h"
#include "FactorySimTypes.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ButcheryBuild
{
	const FString MeshFolder = TEXT("/Game/Butchery/Meshes");
	constexpr double M = 100.0;          // metres to centimetres

	/** Wall panel and rail/belt module lengths, matching the Blender library. */
	constexpr double PanelWidth = 3.00;
	constexpr double RailModule = 6.00;
	constexpr double RailHeight = 3.10;

	// -----------------------------------------------------------------------

	struct FChamber
	{
		FString Name;
		double X = 0.0, Y = 0.0, W = 0.0, H = 0.0;
		FString Zone;
	};

	struct FSegment
	{
		double AX = 0.0, AY = 0.0, BX = 0.0, BY = 0.0;
		FString Kind;
	};

	struct FLineGroup
	{
		FString Chamber;
		FString Axis;
		FString Kind;
		TArray<FSegment> Segments;
	};

	struct FAssetInfo
	{
		bool bSkeletal = false;
		FVector Size = FVector(1.0, 1.0, 1.0);   // metres
	};

	struct FPlant
	{
		double Width = 0.0, Depth = 0.0;
		TArray<FChamber> Chambers;
		TArray<FLineGroup> Lines;
		TArray<FVector2D> RailRoute;
		TArray<FSegment> Transfers;
		TArray<TTuple<FString, FString, int32>> Placements;
	};

	/** Centred on the origin: a level that straddles zero is easier to fly. */
	FVector ToWorld(const FPlant& Plant, const double X, const double Y, const double Z)
	{
		return FVector((X - Plant.Width * 0.5) * M,
					   (Y - Plant.Depth * 0.5) * M,
					   Z * M);
	}

	bool ReadJson(const FString& Path, TSharedPtr<FJsonObject>& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogFactorySim, Error, TEXT("Could not read %s"), *Path);
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Out) || !Out.IsValid())
		{
			UE_LOG(LogFactorySim, Error, TEXT("Not valid JSON: %s"), *Path);
			return false;
		}
		return true;
	}

	bool LoadPlant(FPlant& Plant, TMap<FString, FAssetInfo>& Assets)
	{
		TSharedPtr<FJsonObject> Layout;
		if (!ReadJson(FPaths::ProjectDir() / TEXT("Tools/butchery/plant_layout.json"), Layout))
		{
			return false;
		}

		const TSharedPtr<FJsonObject> Building = Layout->GetObjectField(TEXT("building"));
		Plant.Width = Building->GetNumberField(TEXT("width"));
		Plant.Depth = Building->GetNumberField(TEXT("depth"));

		for (const TSharedPtr<FJsonValue>& Value : Layout->GetArrayField(TEXT("chambers")))
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FChamber Chamber;
			Chamber.Name = Object->GetStringField(TEXT("name"));
			Chamber.X = Object->GetNumberField(TEXT("x"));
			Chamber.Y = Object->GetNumberField(TEXT("y"));
			Chamber.W = Object->GetNumberField(TEXT("w"));
			Chamber.H = Object->GetNumberField(TEXT("h"));
			Chamber.Zone = Object->GetStringField(TEXT("zone"));
			Plant.Chambers.Add(Chamber);
		}

		for (const TSharedPtr<FJsonValue>& Value : Layout->GetArrayField(TEXT("lines")))
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FLineGroup Group;
			Group.Chamber = Object->GetStringField(TEXT("chamber"));
			Group.Axis = Object->GetStringField(TEXT("axis"));
			Group.Kind = Object->GetStringField(TEXT("kind"));
			for (const TSharedPtr<FJsonValue>& SegValue : Object->GetArrayField(TEXT("segments")))
			{
				const TSharedPtr<FJsonObject> Seg = SegValue->AsObject();
				Group.Segments.Add({ Seg->GetNumberField(TEXT("ax")), Seg->GetNumberField(TEXT("ay")),
									 Seg->GetNumberField(TEXT("bx")), Seg->GetNumberField(TEXT("by")) });
			}
			Plant.Lines.Add(Group);
		}

		for (const TSharedPtr<FJsonValue>& Value : Layout->GetArrayField(TEXT("rail_route")))
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			Plant.RailRoute.Add(FVector2D(Object->GetNumberField(TEXT("x")),
										  Object->GetNumberField(TEXT("y"))));
		}

		for (const TSharedPtr<FJsonValue>& Value : Layout->GetArrayField(TEXT("transfers")))
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			Plant.Transfers.Add({ Object->GetNumberField(TEXT("ax")), Object->GetNumberField(TEXT("ay")),
								  Object->GetNumberField(TEXT("bx")), Object->GetNumberField(TEXT("by")),
								  Object->GetStringField(TEXT("kind")) });
		}

		for (const TSharedPtr<FJsonValue>& Value : Layout->GetArrayField(TEXT("placements")))
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			Plant.Placements.Add(MakeTuple(
				Object->GetStringField(TEXT("chamber")),
				Object->GetStringField(TEXT("asset")),
				static_cast<int32>(Object->GetNumberField(TEXT("count")))));
		}

		TSharedPtr<FJsonObject> Manifest;
		if (!ReadJson(FPaths::ProjectDir() / TEXT("Tools/butchery/asset_manifest.json"), Manifest))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : Manifest->GetArrayField(TEXT("assets")))
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FAssetInfo Info;
			Info.bSkeletal = Object->GetBoolField(TEXT("skeletal"));
			const TArray<TSharedPtr<FJsonValue>> Size = Object->GetArrayField(TEXT("size"));
			if (Size.Num() == 3)
			{
				Info.Size = FVector(Size[0]->AsNumber(), Size[1]->AsNumber(), Size[2]->AsNumber());
			}
			Assets.Add(Object->GetStringField(TEXT("asset")), Info);
		}
		return true;
	}

	const FChamber* FindChamber(const FPlant& Plant, const FString& Name)
	{
		for (const FChamber& Chamber : Plant.Chambers)
		{
			if (Chamber.Name == Name)
			{
				return &Chamber;
			}
		}
		return nullptr;
	}

	// -----------------------------------------------------------------------
	// Placement
	// -----------------------------------------------------------------------

	AActor* PlaceAsset(UWorld* World, const FString& Asset, const FAssetInfo& Info,
		const FVector& Location, const double Yaw, const FString& Label)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FTransform Where(FRotator(0.0, Yaw, 0.0), Location);

		if (Info.bSkeletal)
		{
			const FString MeshPath = FString::Printf(TEXT("%s/SM_%s.SM_%s"),
				*MeshFolder, *Asset, *Asset);
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
			if (Mesh == nullptr)
			{
				return nullptr;
			}

			ASkeletalMeshActor* Actor = World->SpawnActor<ASkeletalMeshActor>(
				ASkeletalMeshActor::StaticClass(), Where, Params);
			if (Actor == nullptr)
			{
				return nullptr;
			}

			USkeletalMeshComponent* Component = Actor->GetSkeletalMeshComponent();
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetSkeletalMeshAsset(Mesh);

			// Single-node playback rather than an anim blueprint: these are one
			// looping machine cycle each, and a blueprint per asset would be 16
			// assets carrying no decisions.
			const FString AnimPath = FString::Printf(TEXT("%s/SM_%s_Anim.SM_%s_Anim"),
				*MeshFolder, *Asset, *Asset);
			if (UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *AnimPath))
			{
				Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				Component->AnimationData.AnimToPlay = Anim;
				Component->AnimationData.bSavedLooping = true;
				Component->AnimationData.bSavedPlaying = true;
				Component->SetUpdateAnimationInEditor(true);
			}

			Actor->SetActorLabel(Label);
			return Actor;
		}

		const FString MeshPath = FString::Printf(TEXT("%s/SM_%s.SM_%s"),
			*MeshFolder, *Asset, *Asset);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		if (Mesh == nullptr)
		{
			return nullptr;
		}

		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), Where, Params);
		if (Actor == nullptr)
		{
			return nullptr;
		}
		Actor->SetMobility(EComponentMobility::Static);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		Actor->SetActorLabel(Label);
		return Actor;
	}

	/**
	 * One of the butchery palette materials, which arrived with the meshes.
	 *
	 * The shell is built from boxes rather than imported assets, so it has to
	 * fetch its own materials; FactoryShapeMaterials is the PCB line's palette
	 * and has no cladding or glazing in it.
	 */
	UMaterialInterface* ButcheryMaterial(const TCHAR* Name)
	{
		return LoadObject<UMaterialInterface>(nullptr,
			*FString::Printf(TEXT("%s/M_Butchery_%s.M_Butchery_%s"), *MeshFolder, Name, Name));
	}

	/** A plain box actor, for floors and slabs. */
	AStaticMeshActor* PlaceBox(UWorld* World, const FVector& Centre, const FVector& SizeCm,
		const TCHAR* MaterialName, const FString& Label)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (Cube == nullptr)
		{
			return nullptr;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), FTransform(FRotator::ZeroRotator, Centre), Params);
		if (Actor == nullptr)
		{
			return nullptr;
		}
		Actor->SetMobility(EComponentMobility::Static);
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		Component->SetStaticMesh(Cube);
		// The engine cube is 100 uu, so a scale of 1 is a metre.
		Component->SetRelativeScale3D(SizeCm / M);
		if (UMaterialInterface* Material = FactoryShapeMaterials::Load(MaterialName))
		{
			Component->SetMaterial(0, Material);
		}
		Actor->SetActorLabel(Label);
		return Actor;
	}

	/** As PlaceBox, but skinned from the butchery palette. */
	AStaticMeshActor* PlaceShellBox(UWorld* World, const FVector& Centre, const FVector& SizeCm,
		const TCHAR* ButcheryMat, const FString& Label)
	{
		AStaticMeshActor* Actor = PlaceBox(World, Centre, SizeCm,
			FactoryShapeMaterials::MachineFrame, Label);
		if (Actor != nullptr)
		{
			if (UMaterialInterface* Material = ButcheryMaterial(ButcheryMat))
			{
				Actor->GetStaticMeshComponent()->SetMaterial(0, Material);
			}
		}
		return Actor;
	}

	/**
	 * True when a wall panel centred here should be left out.
	 *
	 * Openings are wherever product crosses a wall: the ends of every transfer,
	 * and every point the overhead rail passes through. Walling those off would
	 * produce a building the line cannot run in.
	 */
	bool IsOpening(const FPlant& Plant, const double X, const double Y)
	{
		constexpr double Clearance = 3.2;
		for (const FSegment& Transfer : Plant.Transfers)
		{
			if (FVector2D::Distance(FVector2D(X, Y), FVector2D(Transfer.AX, Transfer.AY)) < Clearance
				|| FVector2D::Distance(FVector2D(X, Y), FVector2D(Transfer.BX, Transfer.BY)) < Clearance)
			{
				return true;
			}
		}

		for (int32 Index = 0; Index + 1 < Plant.RailRoute.Num(); ++Index)
		{
			const FVector2D A = Plant.RailRoute[Index];
			const FVector2D B = Plant.RailRoute[Index + 1];
			const FVector2D Point(X, Y);
			const FVector2D AB = B - A;
			const double LengthSq = AB.SizeSquared();
			const double T = LengthSq > 0.0
				? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSq, 0.0, 1.0) : 0.0;
			if (FVector2D::Distance(Point, A + AB * T) < Clearance)
			{
				return true;
			}
		}
		return false;
	}
}

using namespace ButcheryBuild;

UFactoryBuildButcheryCommandlet::UFactoryBuildButcheryCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UFactoryBuildButcheryCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	const FString LevelPath = ParamMap.Contains(TEXT("Level"))
		? ParamMap[TEXT("Level")] : TEXT("/Game/level5");

	// A roofed building cannot be photographed from above, and the plan is most
	// legible from directly overhead. -NoRoof builds the same plant with the
	// deck and rooflights omitted, for showcase views only -- the walls,
	// columns and rafters stay, so it still reads as a building rather than as
	// a floor plan.
	const bool bNoRoof = Switches.Contains(TEXT("NoRoof"));

	FPlant Plant;
	TMap<FString, FAssetInfo> Assets;
	if (!LoadPlant(Plant, Assets))
	{
		return 1;
	}

	UE_LOG(LogFactorySim, Display,
		TEXT("Building %s: %.0f x %.0f m, %d chamber(s), %d asset type(s)"),
		*LevelPath, Plant.Width, Plant.Depth, Plant.Chambers.Num(), Assets.Num());

	FactoryShapeMaterials::EnsureAll();

	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/ false);
	if (World == nullptr)
	{
		UE_LOG(LogFactorySim, Error, TEXT("Could not create a blank map"));
		return 1;
	}

	int32 Walls = 0, Doors = 0, Stations = 0, RailTiles = 0, Belts = 0, Lights = 0, Missing = 0;
	int32 LoadedTiles = 0, ShellParts = 0, Transfers = 0;
	// Carried across segments so the loaded/empty pattern does not restart at
	// every corner, which would put a gap at each turn and nowhere else.
	int32 RunningTile = 0;

	// --- floor ------------------------------------------------------------
	PlaceBox(World,
		ToWorld(Plant, Plant.Width * 0.5, Plant.Depth * 0.5, -0.10),
		FVector(Plant.Width * M, Plant.Depth * M, 0.20 * M),
		FactoryShapeMaterials::MachineFrame, TEXT("Plant_Floor"));

	// --- chambers: walls, doors, light -------------------------------------
	for (const FChamber& Chamber : Plant.Chambers)
	{
		// Four edges, tiled with the 3 m panel. Panels sit inside the chamber
		// bounds so neighbouring chambers do not fight over the same metre.
		struct FEdge { double X, Y, DX, DY, Length, Yaw; };
		const FEdge Edges[] = {
			{ Chamber.X, Chamber.Y,                        1.0, 0.0, Chamber.W,   0.0 },
			{ Chamber.X, Chamber.Y + Chamber.H,            1.0, 0.0, Chamber.W,   0.0 },
			{ Chamber.X, Chamber.Y,                        0.0, 1.0, Chamber.H,  90.0 },
			{ Chamber.X + Chamber.W, Chamber.Y,            0.0, 1.0, Chamber.H,  90.0 },
		};

		for (const FEdge& Edge : Edges)
		{
			const int32 Count = FMath::Max(1, FMath::FloorToInt(Edge.Length / PanelWidth));
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const double Along = (Index + 0.5) * (Edge.Length / Count);
				const double PX = Edge.X + Edge.DX * Along;
				const double PY = Edge.Y + Edge.DY * Along;
				if (IsOpening(Plant, PX, PY))
				{
					continue;
				}
				if (PlaceAsset(World, TEXT("WALL_PANEL"), Assets[TEXT("WALL_PANEL")],
					ToWorld(Plant, PX, PY, 0.0), Edge.Yaw,
					FString::Printf(TEXT("%s_Wall_%d"), *Chamber.Name, Walls)) != nullptr)
				{
					++Walls;
				}
			}
		}

		// Bay lighting. Unshadowed, as on level4: point-light shadow cubemaps
		// were 16 ms a frame there and the fix is the same one.
		const int32 LightsX = FMath::Max(1, FMath::RoundToInt(Chamber.W / 12.0));
		const int32 LightsY = FMath::Max(1, FMath::RoundToInt(Chamber.H / 12.0));
		for (int32 IX = 0; IX < LightsX; ++IX)
		{
			for (int32 IY = 0; IY < LightsY; ++IY)
			{
				const double LX = Chamber.X + Chamber.W * (IX + 0.5) / LightsX;
				const double LY = Chamber.Y + Chamber.H * (IY + 0.5) / LightsY;
				if (APointLight* Light = World->SpawnActor<APointLight>(
					ToWorld(Plant, LX, LY, 5.6), FRotator::ZeroRotator))
				{
					Light->SetMobility(EComponentMobility::Stationary);
					// Far dimmer than level4's 40,000, because that hall has a
					// roof and this one does not: with the sky open the sun and
					// skylight already light the floor, and bay lamps at the
					// same intensity simply blow the image out.
					// Back up now the shed has a roof: the rooflight strips let
					// some daylight through, but a covered 130 m hall is lit by
					// its lamps, not by the sky.
					Light->PointLightComponent->SetIntensity(26000.0f);
					Light->PointLightComponent->SetAttenuationRadius(1500.0f);
					Light->PointLightComponent->SetTemperature(5200.0f);
					Light->PointLightComponent->bUseTemperature = true;
					Light->PointLightComponent->SetCastShadows(false);
					Light->SetActorLabel(FString::Printf(TEXT("%s_Bay_%d_%d"),
						*Chamber.Name, IX, IY));
					++Lights;
				}
			}
		}
	}

	// --- transfers: the door, and the thing that goes through it -----------
	//
	// A door in a wall with nothing running through it is why the belts looked
	// like they stopped at the wall and went nowhere. Each transfer now carries
	// its conveyor across the opening, extended well past the wall on both
	// sides so it visibly comes from one room and arrives in the other.
	for (const FSegment& Transfer : Plant.Transfers)
	{
		const double MX = (Transfer.AX + Transfer.BX) * 0.5;
		const double MY = (Transfer.AY + Transfer.BY) * 0.5;

		FString Carrier;
		if (Transfer.Kind == TEXT("belt"))        { Carrier = TEXT("BELT_CONVEYOR"); }
		else if (Transfer.Kind == TEXT("roller")) { Carrier = TEXT("ROLLER_CONVEYOR"); }
		else if (Transfer.Kind == TEXT("screw"))  { Carrier = TEXT("SCREW_CONVEYOR"); }
		else if (Transfer.Kind == TEXT("race"))   { Carrier = TEXT("CROWD_RACE"); }

		if (const FAssetInfo* Info = Assets.Find(Carrier))
		{
			const double DX = Transfer.BX - Transfer.AX;
			const double DY = Transfer.BY - Transfer.AY;
			const double Len = FMath::Sqrt(DX * DX + DY * DY);
			const FVector2D Direction = Len > 0.0
				? FVector2D(DX / Len, DY / Len) : FVector2D(0.0, 1.0);
			const double Along = FMath::RadiansToDegrees(
				FMath::Atan2(-Direction.X, Direction.Y));

			const double Module = FMath::Max(
				FMath::Abs(Direction.X) * Info->Size.X
				+ FMath::Abs(Direction.Y) * Info->Size.Y, 1.0);
			const double Run = 14.0;
			const int32 Tiles = FMath::Max(1, FMath::RoundToInt(Run / Module));

			for (int32 Tile = 0; Tile < Tiles; ++Tile)
			{
				const double Offset = (Tile + 0.5) * (Run / Tiles) - Run * 0.5;
				if (PlaceAsset(World, Carrier, *Info,
					ToWorld(Plant, MX + Direction.X * Offset,
							MY + Direction.Y * Offset, 0.0), Along,
					FString::Printf(TEXT("Transfer_%s_%d_%d"),
						*Transfer.Kind, Doors, Tile)) != nullptr)
				{
					++Transfers;
				}
			}
		}

		// The door faces across the wall it sits in, so it is square to the
		// direction product travels.
		const double Yaw = FMath::Abs(Transfer.BX - Transfer.AX)
			> FMath::Abs(Transfer.BY - Transfer.AY) ? 90.0 : 0.0;
		if (PlaceAsset(World, TEXT("CHAMBER_DOOR"), Assets[TEXT("CHAMBER_DOOR")],
			ToWorld(Plant, MX, MY, 0.0), Yaw,
			FString::Printf(TEXT("Door_%d"), Doors)) != nullptr)
		{
			++Doors;
		}
	}

	// --- the overhead rail --------------------------------------------------
	for (int32 Index = 0; Index + 1 < Plant.RailRoute.Num(); ++Index)
	{
		const FVector2D A = Plant.RailRoute[Index];
		const FVector2D B = Plant.RailRoute[Index + 1];
		const FVector2D Delta = B - A;
		const double Length = Delta.Size();
		if (Length < 0.1)
		{
			continue;
		}
		const FVector2D Direction = Delta / Length;
		// The rail module runs along its own +Y, so a segment heading along
		// world X needs a quarter turn.
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Direction.X, Direction.Y));

		const int32 Tiles = FMath::Max(1, FMath::RoundToInt(Length / RailModule));
		for (int32 Tile = 0; Tile < Tiles; ++Tile)
		{
			const FVector2D At = A + Direction * ((Tile + 0.5) * (Length / Tiles));

			// Two loaded modules then an empty one. A line is never uniformly
			// full -- there are gaps where a carcass has been taken off and
			// gaps behind a stoppage -- and an unbroken run of them reads as
			// wallpaper rather than as product.
			const bool bLoaded = ((Tile + RunningTile) % 3) != 2;
			const TCHAR* Asset = bLoaded ? TEXT("RAIL_CARCASS_RUN") : TEXT("RAIL_RUN");
			if (PlaceAsset(World, Asset, Assets[Asset],
				ToWorld(Plant, At.X, At.Y, 0.0), Yaw,
				FString::Printf(TEXT("Rail_%d_%d"), Index, Tile)) != nullptr)
			{
				++RailTiles;
				if (bLoaded)
				{
					++LoadedTiles;
				}
			}
		}
		RunningTile += Tiles;
	}

	// --- lines inside chambers ----------------------------------------------
	for (const FLineGroup& Group : Plant.Lines)
	{
		for (int32 SegIndex = 0; SegIndex < Group.Segments.Num(); ++SegIndex)
		{
			const FSegment& Segment = Group.Segments[SegIndex];
			const FVector2D A(Segment.AX, Segment.AY);
			const FVector2D B(Segment.BX, Segment.BY);
			const FVector2D Delta = B - A;
			const double Length = Delta.Size();
			if (Length < 0.1)
			{
				continue;
			}
			const FVector2D Direction = Delta / Length;
			const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Direction.X, Direction.Y));

			// What gets tiled along the line depends on what the line is.
			FString Asset = TEXT("BELT_CONVEYOR");
			if (Group.Kind == TEXT("rail"))          { Asset = TEXT("RAIL_RUN"); }
			else if (Group.Kind == TEXT("railfull")) { Asset = TEXT("RAIL_CARCASS_RUN"); }
			else if (Group.Kind == TEXT("pen"))      { Asset = TEXT("PEN_RAIL"); }
			else if (Group.Kind == TEXT("pipe"))     { Asset = TEXT("PIPE_RACK"); }

			const FAssetInfo* LineInfo = Assets.Find(Asset);
			if (LineInfo == nullptr)
			{
				continue;
			}
			// Tile at the asset's own length, not a fixed 6 m: pen railing is
			// 3 m and pipe bridge 6 m, and tiling both at one figure leaves
			// either gaps or overlaps down the whole run.
			const double Module = FMath::Max(
				FMath::Abs(Direction.X) * LineInfo->Size.X
				+ FMath::Abs(Direction.Y) * LineInfo->Size.Y, 1.0);

			const int32 Tiles = FMath::Max(1, FMath::FloorToInt(Length / Module));
			for (int32 Tile = 0; Tile < Tiles; ++Tile)
			{
				const FVector2D At = A + Direction * ((Tile + 0.5) * (Length / Tiles));
				if (PlaceAsset(World, Asset, *LineInfo,
					ToWorld(Plant, At.X, At.Y, 0.0), Yaw,
					FString::Printf(TEXT("%s_%s_%d_%d"), *Group.Chamber, *Group.Kind,
						SegIndex, Tile)) != nullptr)
				{
					++Belts;
				}
			}
		}
	}

	// --- stations ------------------------------------------------------------
	// Spread along the chamber's own lines where it has them, so machines land
	// on the route product takes rather than in a grid beside it.
	for (const TTuple<FString, FString, int32>& Placement : Plant.Placements)
	{
		const FString& ChamberName = Placement.Get<0>();
		const FString& Asset = Placement.Get<1>();
		const int32 Count = Placement.Get<2>();

		const FChamber* Chamber = FindChamber(Plant, ChamberName);
		const FAssetInfo* Info = Assets.Find(Asset);
		if (Chamber == nullptr || Info == nullptr)
		{
			UE_LOG(LogFactorySim, Warning, TEXT("  no asset or chamber for %s in %s"),
				*Asset, *ChamberName);
			Missing += Count;
			continue;
		}

		const FLineGroup* Group = nullptr;
		for (const FLineGroup& Candidate : Plant.Lines)
		{
			if (Candidate.Chamber == ChamberName)
			{
				Group = &Candidate;
				break;
			}
		}

		// Cursors advance per line so successive assets in the same chamber
		// queue up along it instead of stacking on one spot.
		static TMap<FString, TArray<double>> Cursors;
		TArray<double>& Cursor = Cursors.FindOrAdd(ChamberName);
		const int32 Lanes = Group != nullptr ? Group->Segments.Num() : 1;
		if (Cursor.Num() < Lanes)
		{
			Cursor.SetNumZeroed(Lanes);
		}

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Lane = Lanes > 0 ? Index % Lanes : 0;
			double PX = 0.0, PY = 0.0, Yaw = 0.0;

			if (Group != nullptr && Group->Segments.IsValidIndex(Lane))
			{
				const FSegment& Segment = Group->Segments[Lane];
				const FVector2D A(Segment.AX, Segment.AY);
				const FVector2D B(Segment.BX, Segment.BY);
				const FVector2D Delta = B - A;
				const double Length = Delta.Size();
				const FVector2D Direction = Length > 0.0 ? Delta / Length : FVector2D(1.0, 0.0);

				// Footprint along the direction of travel, plus a working gap.
				const double Span = FMath::Max(
					FMath::Abs(Direction.X) * Info->Size.X + FMath::Abs(Direction.Y) * Info->Size.Y,
					1.2) + 1.4;

				Cursor[Lane] += Span * 0.5;
				const double Along = FMath::Fmod(Cursor[Lane], FMath::Max(Length - 2.0, 1.0)) + 1.0;
				Cursor[Lane] += Span * 0.5;

				const FVector2D At = A + Direction * Along;
				PX = At.X;
				PY = At.Y;
				Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Direction.X, Direction.Y));
			}
			else
			{
				// No line: a grid inside the chamber, inset from the walls.
				const int32 Columns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt((double)Count)));
				const int32 Rows = FMath::Max(1, FMath::CeilToInt((double)Count / Columns));
				const int32 CX = Index % Columns;
				const int32 CY = Index / Columns;
				PX = Chamber->X + Chamber->W * (0.18 + 0.64 * (Columns > 1 ? (double)CX / (Columns - 1) : 0.5));
				PY = Chamber->Y + Chamber->H * (0.18 + 0.64 * (Rows > 1 ? (double)CY / (Rows - 1) : 0.5));
			}

			if (PlaceAsset(World, Asset, *Info, ToWorld(Plant, PX, PY, 0.0), Yaw,
				FString::Printf(TEXT("%s_%s_%d"), *ChamberName, *Asset, Index)) != nullptr)
			{
				++Stations;
			}
			else
			{
				++Missing;
			}
		}
	}

	// --- the shed the plant sits in -------------------------------------------
	//
	// A food plant is a steel portal frame with insulated cladding, and the
	// chambers are rooms built inside it -- which is why the interior partitions
	// stop at 4 m and this does not. Without it the level reads as a floor plan
	// standing in a field, and the interior has no ceiling for light to bounce
	// off, so everything looked flatter than it should.
	{
		const double Eaves = 8.20;
		const double Overhang = 1.20;
		const double W = Plant.Width;
		const double D = Plant.Depth;

		// Cladding: four walls, outside the chamber footprint.
		const double Thickness = 0.30;
		PlaceShellBox(World, ToWorld(Plant, W * 0.5, -Thickness * 0.5, Eaves * 0.5),
			FVector((W + Thickness * 2) * M, Thickness * M, Eaves * M),
			TEXT("FoodPlastic"), TEXT("Shell_Wall_S"));
		PlaceShellBox(World, ToWorld(Plant, W * 0.5, D + Thickness * 0.5, Eaves * 0.5),
			FVector((W + Thickness * 2) * M, Thickness * M, Eaves * M),
			TEXT("FoodPlastic"), TEXT("Shell_Wall_N"));
		PlaceShellBox(World, ToWorld(Plant, -Thickness * 0.5, D * 0.5, Eaves * 0.5),
			FVector(Thickness * M, D * M, Eaves * M),
			TEXT("FoodPlastic"), TEXT("Shell_Wall_W"));
		PlaceShellBox(World, ToWorld(Plant, W + Thickness * 0.5, D * 0.5, Eaves * 0.5),
			FVector(Thickness * M, D * M, Eaves * M),
			TEXT("FoodPlastic"), TEXT("Shell_Wall_E"));
		ShellParts += 4;

		// Portal columns on a 13 x 11 m grid, and the beams between them.
		const int32 ColumnsX = FMath::RoundToInt(W / 13.0);
		const int32 ColumnsY = FMath::RoundToInt(D / 11.0);
		for (int32 IX = 0; IX <= ColumnsX; ++IX)
		{
			for (int32 IY = 0; IY <= ColumnsY; ++IY)
			{
				const double CX = W * IX / ColumnsX;
				const double CY = D * IY / ColumnsY;
				PlaceShellBox(World, ToWorld(Plant, CX, CY, Eaves * 0.5),
					FVector(0.34 * M, 0.34 * M, Eaves * M),
					TEXT("SteelBrushed"),
					FString::Printf(TEXT("Shell_Column_%d_%d"), IX, IY));
				++ShellParts;
			}

			const double CX = W * IX / ColumnsX;
			PlaceShellBox(World, ToWorld(Plant, CX, D * 0.5, Eaves - 0.35),
				FVector(0.28 * M, D * M, 0.60 * M),
				TEXT("SteelBrushed"), FString::Printf(TEXT("Shell_Rafter_%d"), IX));
			++ShellParts;
		}

		// Yard apron. Without it the building stands on a void and reads as a
		// model of a shed rather than a shed.
		PlaceShellBox(World, ToWorld(Plant, W * 0.5, D * 0.5, -0.16),
			FVector(260.0 * M, 200.0 * M, 0.20 * M),
			TEXT("Concrete"), TEXT("Shell_Apron"));

		// Vertical cladding ribs. Trapezoidal sheet is what these walls are
		// made of, and the rib shadow is the only thing that gives a 130 m
		// elevation any scale at all.
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const double YAt = Side == 0 ? -Thickness : D + Thickness;
			const int32 Count = FMath::FloorToInt(W / 6.0);
			for (int32 Index = 0; Index <= Count; ++Index)
			{
				PlaceShellBox(World, ToWorld(Plant, W * Index / Count, YAt, Eaves * 0.5),
					FVector(0.18 * M, 0.22 * M, Eaves * M),
					TEXT("SteelBrushed"),
					FString::Printf(TEXT("Shell_Rib_%d_%d"), Side, Index));
				++ShellParts;
			}
		}

		// Eaves fascia, wrapping the top of the cladding.
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const double YAt = Side == 0 ? -Thickness : D + Thickness;
			PlaceShellBox(World, ToWorld(Plant, W * 0.5, YAt, Eaves - 0.30),
				FVector((W + Overhang * 2) * M, 0.34 * M, 0.60 * M),
				TEXT("SteelBrushed"), FString::Printf(TEXT("Shell_Fascia_%d"), Side));
			++ShellParts;
		}

		// Loading dock on the west elevation, where dispatch is. Three shutters
		// with a canopy over them: a plant this size ships on lorries, and a
		// blank wall where the lorries go is the giveaway that nobody thought
		// about how product leaves.
		for (int32 Bay = 0; Bay < 3; ++Bay)
		{
			const double DY = 68.0 + Bay * 6.0;
			PlaceShellBox(World, ToWorld(Plant, -Thickness - 0.10, DY, 2.30),
				FVector(0.16 * M, 4.00 * M, 4.40 * M),
				TEXT("SteelBrushed"), FString::Printf(TEXT("Shell_Shutter_%d"), Bay));
			PlaceShellBox(World, ToWorld(Plant, -1.40, DY, 0.55),
				FVector(2.60 * M, 3.40 * M, 1.10 * M),
				TEXT("Concrete"), FString::Printf(TEXT("Shell_DockPad_%d"), Bay));
			ShellParts += 2;
		}
		PlaceShellBox(World, ToWorld(Plant, -2.20, 74.0, 5.10),
			FVector(4.60 * M, 22.0 * M, 0.30 * M),
			TEXT("SteelBrushed"), TEXT("Shell_DockCanopy"));
		++ShellParts;

		// Roof: solid bays with a glazed rooflight strip between each pair, the
		// way a shed of this size is actually daylit. Without the strips the
		// interior goes black and needs the bay lamps doing all the work.
		const int32 Bays = bNoRoof ? 0 : ColumnsY;
		for (int32 Bay = 0; Bay < Bays; ++Bay)
		{
			const double Y0 = D * Bay / Bays;
			const double Y1 = D * (Bay + 1) / Bays;
			const double Light = 2.20;
			const double SolidDepth = (Y1 - Y0) - Light;

			PlaceShellBox(World,
				ToWorld(Plant, W * 0.5, Y0 + SolidDepth * 0.5, Eaves + 0.15),
				FVector((W + Overhang * 2) * M, SolidDepth * M, 0.30 * M),
				TEXT("FoodPlastic"), FString::Printf(TEXT("Shell_Roof_%d"), Bay));

			PlaceShellBox(World,
				ToWorld(Plant, W * 0.5, Y1 - Light * 0.5, Eaves + 0.15),
				FVector(W * M, Light * M, 0.08 * M),
				TEXT("Perspex"), FString::Printf(TEXT("Shell_Rooflight_%d"), Bay));
			ShellParts += 2;
		}
	}

	// --- daylight and sky ----------------------------------------------------
	if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
		FVector(0.0, 0.0, 1600.0), FRotator(-48.0, -40.0, 0.0)))
	{
		Sun->SetMobility(EComponentMobility::Stationary);
		Sun->GetLightComponent()->SetIntensity(6.0f);
		Sun->GetLightComponent()->SetTemperature(6200.0f);
		Sun->GetLightComponent()->bUseTemperature = true;
		if (UDirectionalLightComponent* Directional =
			Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Directional->DynamicShadowDistanceStationaryLight = 6000.0f;
			Directional->DynamicShadowCascades = 2;
		}
		Sun->SetActorLabel(TEXT("Sun"));
	}
	World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0.0, 0.0, 900.0), FRotator::ZeroRotator))
	{
		Sky->GetLightComponent()->SetIntensity(0.85f);
		Sky->GetLightComponent()->SetMobility(EComponentMobility::Stationary);
		Sky->SetActorLabel(TEXT("SkyLight"));
	}

	// A start point on the kill floor, looking down the line the way a visitor
	// would walk in.
	if (Plant.RailRoute.Num() > 0)
	{
		const FVector2D Start = Plant.RailRoute[0];
		World->SpawnActor<APlayerStart>(
			ToWorld(Plant, Start.X - 4.0, Start.Y - 6.0, 1.7), FRotator(-6.0, 45.0, 0.0));
	}

	// Without a clamp, auto-exposure chases whatever fills the frame: an
	// overhead shot of a bright floor drives it to the floor's brightness and
	// the whole plant renders white, which is exactly what the first build did.
	if (APostProcessVolume* Exposure = World->SpawnActor<APostProcessVolume>(
		FVector::ZeroVector, FRotator::ZeroRotator))
	{
		Exposure->SetActorLabel(TEXT("ExposureLock"));
		Exposure->bUnbound = true;
		FPostProcessSettings& Settings = Exposure->Settings;
		Settings.bOverride_AutoExposureMinBrightness = true;
		Settings.bOverride_AutoExposureMaxBrightness = true;
		Settings.AutoExposureMinBrightness = 0.08f;
		Settings.AutoExposureMaxBrightness = 3.0f;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = 0.6f;
		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = 0.35f;
	}

	// The still and flythrough tools drive whatever CineCameraActor the level
	// holds, so every buildable level has to place one or it cannot be
	// photographed at all.
	if (ACineCameraActor* Camera = World->SpawnActor<ACineCameraActor>(
		ToWorld(Plant, Plant.Width * 0.25, -8.0, 34.0), FRotator(-26.0, 52.0, 0.0)))
	{
		Camera->SetActorLabel(TEXT("RenderCam"));
		if (UCineCameraComponent* Lens = Camera->GetCineCameraComponent())
		{
			// Wide, because the subject is a 130 m building rather than a
			// machine: at 35 mm the plant does not fit in frame from anywhere
			// inside its own site.
			Lens->SetCurrentFocalLength(18.0f);
			Lens->FocusSettings.FocusMethod = ECameraFocusMethod::Disable;
		}
	}

	if (!UEditorLoadingAndSavingUtils::SaveMap(World, LevelPath))
	{
		UE_LOG(LogFactorySim, Error, TEXT("Could not save %s"), *LevelPath);
		return 1;
	}

	UE_LOG(LogFactorySim, Display,
		TEXT("Built %s: %d wall panel(s), %d door(s), %d station(s), "
			 "%d rail tile(s) of which %d loaded, %d line module(s), "
			 "%d transfer module(s), %d shell part(s), %d bay light(s), %d unplaced"),
		*LevelPath, Walls, Doors, Stations, RailTiles, LoadedTiles, Belts,
		Transfers, ShellParts, Lights, Missing);

	return Missing == 0 ? 0 : 1;
}
