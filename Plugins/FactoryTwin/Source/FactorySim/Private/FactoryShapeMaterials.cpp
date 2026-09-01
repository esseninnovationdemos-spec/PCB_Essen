#include "FactoryShapeMaterials.h"

#include "FactorySimTypes.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/MeshComponent.h"
#include "Engine/Texture.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#endif

namespace FactoryShapeMaterials
{
	UMaterialInterface* Load(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), Folder, Name, Name);
		return LoadObject<UMaterialInterface>(nullptr, *Path);
	}

#if WITH_EDITOR
	namespace
	{
		struct FSpec
		{
			const TCHAR* Name;
			FLinearColor Colour;
		};

		const FSpec Specs[] = {
			// Deliberately loud. A carrier is the thing you follow to read the
			// line at a glance, and the first one was near-black on a near-black
			// belt, which made a running line look like a stopped one.
			{ Carrier,   FLinearColor(0.90f, 0.35f, 0.02f) },  // safety orange
			{ Housing,   FLinearColor(0.55f, 0.57f, 0.60f) },  // cast aluminium
			{ Connector, FLinearColor(0.02f, 0.02f, 0.02f) },  // black plastic
			{ Board,     FLinearColor(0.02f, 0.30f, 0.10f) },  // green laminate
			{ Lid,       FLinearColor(0.70f, 0.72f, 0.75f) },
			{ Carton,    FLinearColor(0.45f, 0.30f, 0.15f) },  // cardboard
			{ LampPass,  FLinearColor(0.05f, 0.90f, 0.15f) },
			{ LampFail,  FLinearColor(0.90f, 0.05f, 0.05f) },
			{ GridMinor, FLinearColor(0.30f, 0.32f, 0.35f) },
			{ GridMajor, FLinearColor(0.10f, 0.45f, 0.70f) },
			{ Belt,      FLinearColor(0.08f, 0.08f, 0.09f) },
			{ LampWarn,  FLinearColor(0.95f, 0.55f, 0.02f) },
			{ CabinetShell, FLinearColor(0.66f, 0.67f, 0.65f) },  // RAL 7035
			{ ModuleFace,   FLinearColor(0.11f, 0.11f, 0.12f) },
		};

		/** A machine finish: colour plus how metal it is and how polished. */
		struct FFinishSpec
		{
			const TCHAR* Name;
			FLinearColor Colour;
			float Metallic;
			float Roughness;
		};

		// Roughness does most of the work here. Fully metallic and smooth is a
		// mirror, which on a station reads as a bug rather than as steel; real
		// machine panels are brushed or powder-coated and scatter enough to show
		// their form.
		const FFinishSpec Finishes[] = {
			{ SteelBrushed,  FLinearColor(0.62f, 0.64f, 0.66f), 1.0f, 0.38f },
			// The bright covers. Smoother, so they still read as the light
			// surfaces they were, but now as stainless rather than as paint.
			{ SteelPolished, FLinearColor(0.76f, 0.78f, 0.80f), 1.0f, 0.22f },
			// Frames and uprights: darker anodised extrusion.
			{ MachineFrame,  FLinearColor(0.24f, 0.25f, 0.27f), 1.0f, 0.45f },
		};

		/** Parameter names on the generated parent. */
		const TCHAR* ColourParam = TEXT("Color");
		const TCHAR* MetallicParam = TEXT("Metallic");
		const TCHAR* RoughnessParam = TEXT("Roughness");

		/**
		 * Creates the PBR parent if it is missing, and returns it.
		 *
		 * Built in code rather than checked in as an asset so a fresh clone can
		 * generate the whole material set from the commandlet, the same way the
		 * levels are generated.
		 */
		UMaterial* EnsurePbrParent(TArray<UPackage*>& ToSave, int32& Created)
		{
			const FString PackageName = FString::Printf(TEXT("%s/%s"), Folder, PbrParent);
			const FString ObjectPath =
				FString::Printf(TEXT("%s.%s"), *PackageName, PbrParent);

			if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *ObjectPath))
			{
				return Existing;
			}

			UPackage* Package = CreatePackage(*PackageName);
			if (Package == nullptr)
			{
				return nullptr;
			}

			UMaterial* Material = NewObject<UMaterial>(
				Package, FName(PbrParent), RF_Public | RF_Standalone);
			if (Material == nullptr)
			{
				return nullptr;
			}

			UMaterialExpressionVectorParameter* Colour =
				NewObject<UMaterialExpressionVectorParameter>(Material);
			Colour->ParameterName = FName(ColourParam);
			Colour->DefaultValue = FLinearColor(0.62f, 0.64f, 0.66f);

			UMaterialExpressionScalarParameter* Metallic =
				NewObject<UMaterialExpressionScalarParameter>(Material);
			Metallic->ParameterName = FName(MetallicParam);
			Metallic->DefaultValue = 1.0f;

			UMaterialExpressionScalarParameter* Roughness =
				NewObject<UMaterialExpressionScalarParameter>(Material);
			Roughness->ParameterName = FName(RoughnessParam);
			Roughness->DefaultValue = 0.38f;

			UMaterialEditingLibrary::ConnectMaterialProperty(
				Colour, TEXT(""), EMaterialProperty::MP_BaseColor);
			UMaterialEditingLibrary::ConnectMaterialProperty(
				Metallic, TEXT(""), EMaterialProperty::MP_Metallic);
			UMaterialEditingLibrary::ConnectMaterialProperty(
				Roughness, TEXT(""), EMaterialProperty::MP_Roughness);

			UMaterialEditingLibrary::RecompileMaterial(Material);

			FAssetRegistryModule::AssetCreated(Material);
			Package->MarkPackageDirty();
			ToSave.Add(Package);
			++Created;
			return Material;
		}

		/**
		 * Reads the colour out of a material named after it.
		 *
		 * The station meshes came out of CAD, and the exporter named every
		 * material for the colour it carries: "192_192_192_11_008",
		 * "26_26_26_008", "255_255_192_11_007". That name is the only
		 * description of the surface available without evaluating the shader,
		 * and it is enough to tell a grey machine panel from a signal colour.
		 *
		 * @return True if the name began with an R_G_B triplet.
		 */
		bool ParseColourName(const FString& Name, int32& R, int32& G, int32& B)
		{
			TArray<FString> Parts;
			Name.ParseIntoArray(Parts, TEXT("_"), /*InCullEmpty=*/false);
			if (Parts.Num() < 3)
			{
				return false;
			}

			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (!Parts[Index].IsNumeric())
				{
					return false;
				}
			}

			R = FCString::Atoi(*Parts[0]);
			G = FCString::Atoi(*Parts[1]);
			B = FCString::Atoi(*Parts[2]);
			return R <= 255 && G <= 255 && B <= 255;
		}

		/** True when a material samples any texture, i.e. it is not a flat tint. */
		bool UsesTextures(UMaterialInterface* Material)
		{
			if (Material == nullptr)
			{
				return false;
			}
			// Defaulted arguments mean every quality level and shader platform.
			// The 5.7 overload that takes them explicitly is deprecated and, more
			// to the point, is an empty stub -- calling it reports no textures for
			// anything, which would have had this repaint the screens and labels
			// it exists to protect.
			TArray<UTexture*> Textures;
			Material->GetUsedTextures(Textures);
			return Textures.Num() > 0;
		}
	}

	int32 EnsureAll()
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, ParentMaterial);
		if (Parent == nullptr)
		{
			UE_LOG(LogFactorySim, Warning,
				TEXT("Shape material parent '%s' is missing; level geometry will be untinted"),
				ParentMaterial);
			return 0;
		}

		TArray<UPackage*> ToSave;
		int32 Created = 0;
		int32 Updated = 0;

		for (const FSpec& Spec : Specs)
		{
			// Reconcile rather than skip. These colours are defined in code, so an
			// asset generated by an earlier run must be brought back into line
			// with it; skipping anything that already exists meant editing a
			// colour here silently did nothing.
			if (UMaterialInterface* Existing = Load(Spec.Name))
			{
				UMaterialInstanceConstant* Constant = Cast<UMaterialInstanceConstant>(Existing);
				if (Constant == nullptr)
				{
					continue;
				}

				FLinearColor Current;
				const bool bHasValue = Constant->GetVectorParameterValue(
					FMaterialParameterInfo(TEXT("Color")), Current);
				if (bHasValue && Current.Equals(Spec.Colour))
				{
					continue;
				}

				Constant->SetVectorParameterValueEditorOnly(
					FMaterialParameterInfo(TEXT("Color")), Spec.Colour);
				Constant->PostEditChange();
				Constant->MarkPackageDirty();
				ToSave.Add(Constant->GetOutermost());
				++Updated;
				continue;
			}

			const FString PackageName = FString::Printf(TEXT("%s/%s"), Folder, Spec.Name);
			UPackage* Package = CreatePackage(*PackageName);
			if (Package == nullptr)
			{
				continue;
			}

			UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(
				Package, FName(Spec.Name), RF_Public | RF_Standalone);
			if (Instance == nullptr)
			{
				continue;
			}

			Instance->SetParentEditorOnly(Parent);
			Instance->SetVectorParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("Color")), Spec.Colour);
			Instance->PostEditChange();

			FAssetRegistryModule::AssetCreated(Instance);
			Package->MarkPackageDirty();
			ToSave.Add(Package);
			++Created;
		}

		// The machine finishes hang off the generated PBR parent instead, because
		// metal is not a colour -- BasicShapeMaterial has no Metallic input to
		// set, so a "steel" instance of it would just be a light grey plastic.
		if (UMaterial* PbrRoot = EnsurePbrParent(ToSave, Created))
		{
			for (const FFinishSpec& Finish : Finishes)
			{
				UMaterialInstanceConstant* Instance =
					Cast<UMaterialInstanceConstant>(Load(Finish.Name));

				if (Instance == nullptr)
				{
					const FString PackageName =
						FString::Printf(TEXT("%s/%s"), Folder, Finish.Name);
					UPackage* Package = CreatePackage(*PackageName);
					if (Package == nullptr)
					{
						continue;
					}

					Instance = NewObject<UMaterialInstanceConstant>(
						Package, FName(Finish.Name), RF_Public | RF_Standalone);
					if (Instance == nullptr)
					{
						continue;
					}
					Instance->SetParentEditorOnly(PbrRoot);
					FAssetRegistryModule::AssetCreated(Instance);
					++Created;
				}
				else
				{
					++Updated;
				}

				// Reconciled unconditionally, for the same reason the colours
				// are: these values are defined here, so an asset from an
				// earlier run has to be brought back into line with them.
				Instance->SetVectorParameterValueEditorOnly(
					FMaterialParameterInfo(ColourParam), Finish.Colour);
				Instance->SetScalarParameterValueEditorOnly(
					FMaterialParameterInfo(MetallicParam), Finish.Metallic);
				Instance->SetScalarParameterValueEditorOnly(
					FMaterialParameterInfo(RoughnessParam), Finish.Roughness);
				Instance->PostEditChange();
				Instance->MarkPackageDirty();
				ToSave.Add(Instance->GetOutermost());
			}
		}

		if (ToSave.Num() > 0)
		{
			UEditorLoadingAndSavingUtils::SavePackages(ToSave, /*bOnlyDirty*/ false);
			UE_LOG(LogFactorySim, Display,
				TEXT("Shape materials: %d created, %d updated"), Created, Updated);
		}
		return Created + Updated;
	}

	int32 ApplyMachineFinish(AActor* Station)
	{
		if (Station == nullptr)
		{
			return 0;
		}

		UMaterialInterface* Steel = Load(SteelBrushed);
		UMaterialInterface* Bright = Load(SteelPolished);
		UMaterialInterface* Frame = Load(MachineFrame);
		if (Steel == nullptr || Bright == nullptr || Frame == nullptr)
		{
			// Not an error: EnsureAll runs first in the build, but a caller that
			// skipped it should leave the station looking as authored rather
			// than have its materials cleared out from under it.
			return 0;
		}

		int32 Overridden = 0;

		TArray<UMeshComponent*> Meshes;
		Station->GetComponents<UMeshComponent>(Meshes);

		for (UMeshComponent* Mesh : Meshes)
		{
			if (Mesh == nullptr)
			{
				continue;
			}

			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				UMaterialInterface* Current = Mesh->GetMaterial(Slot);
				if (Current == nullptr)
				{
					continue;
				}

				// Anything textured is a screen, a label or a warning marking.
				// Those carry the information that makes a machine readable, and
				// painting steel over them would be a straight loss.
				if (UsesTextures(Current))
				{
					continue;
				}

				const FString Name = Current->GetName().ToLower();

				// Glass stays glass; a metal window is not a window.
				if (Name.Contains(TEXT("glass")) || Name.Contains(TEXT("lens")))
				{
					continue;
				}

				UMaterialInterface* Finish = nullptr;

				if (Name.StartsWith(TEXT("steel")) || Name.StartsWith(TEXT("alu"))
					|| Name.Contains(TEXT("metal")) || Name.Contains(TEXT("chrome")))
				{
					Finish = Steel;
				}
				else if (Name.StartsWith(TEXT("black")))
				{
					Finish = Frame;
				}
				else if (Name.StartsWith(TEXT("white")))
				{
					Finish = Bright;
				}
				else
				{
					int32 R = 0, G = 0, B = 0;
					if (!ParseColourName(Name, R, G, B))
					{
						// Unrecognised. Leaving it as authored is the safe way
						// to be wrong: the worst case is a station that is less
						// metallic than intended, rather than a signal lamp
						// painted over.
						continue;
					}

					// Saturated means it is carrying meaning -- a stack light, a
					// status lamp, a painted hazard marking. An earlier pass
					// repainted these and the running line lost its red, amber
					// and green, which is the one thing on a machine you read
					// from across the hall.
					const int32 Spread = FMath::Max3(R, G, B) - FMath::Min3(R, G, B);
					if (Spread > 24)
					{
						continue;
					}

					// Neutral, so it is structure. Keep the light/dark split the
					// model already has, or every machine flattens into one grey
					// mass with no readable form.
					const int32 Luminance = (R + G + B) / 3;
					Finish = (Luminance < 90) ? Frame : (Luminance > 190 ? Bright : Steel);
				}

				Mesh->SetMaterial(Slot, Finish);
				++Overridden;
			}
		}

		return Overridden;
	}
#endif
}
