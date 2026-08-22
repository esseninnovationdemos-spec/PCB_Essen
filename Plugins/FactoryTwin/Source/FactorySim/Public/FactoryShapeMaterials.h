#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * Flat colours for the generated level geometry.
 *
 * These are real material instance assets rather than dynamic instances created
 * on the fly. A dynamic instance is transient, so anything holding one cannot be
 * saved into a level -- the map save is rejected outright with an illegal
 * reference to a private object. Generated geometry has to be saved, so it needs
 * materials that live on disk.
 */
namespace FactoryShapeMaterials
{
	inline const TCHAR* Folder = TEXT("/Game/FactoryTwin/Materials");

	/** Parent for all of them; its "Color" parameter is what each one sets. */
	inline const TCHAR* ParentMaterial =
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	inline const TCHAR* Carrier   = TEXT("MI_Factory_Carrier");
	inline const TCHAR* Housing   = TEXT("MI_Factory_Housing");
	inline const TCHAR* Connector = TEXT("MI_Factory_Connector");
	inline const TCHAR* Board     = TEXT("MI_Factory_Board");
	inline const TCHAR* Lid       = TEXT("MI_Factory_Lid");
	inline const TCHAR* Carton    = TEXT("MI_Factory_Carton");
	inline const TCHAR* LampPass  = TEXT("MI_Factory_LampPass");
	inline const TCHAR* LampFail  = TEXT("MI_Factory_LampFail");
	inline const TCHAR* GridMinor = TEXT("MI_Factory_GridMinor");
	inline const TCHAR* GridMajor = TEXT("MI_Factory_GridMajor");
	inline const TCHAR* Belt      = TEXT("MI_Factory_Belt");

	/**
	 * Loads one by name.
	 *
	 * @return The material, or null if the assets have not been generated yet,
	 *         in which case callers should leave the mesh's own material alone
	 *         rather than clearing it.
	 */
	FACTORYSIM_API UMaterialInterface* Load(const TCHAR* Name);

#if WITH_EDITOR
	/**
	 * Brings the assets into line with the colours defined in code: creates any
	 * that are missing, recolours any that have drifted, and saves both.
	 * Idempotent.
	 *
	 * @return Number of assets created or changed.
	 */
	FACTORYSIM_API int32 EnsureAll();
#endif
}
