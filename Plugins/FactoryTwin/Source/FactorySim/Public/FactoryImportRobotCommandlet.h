#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "FactoryImportRobotCommandlet.generated.h"

/**
 * Imports the KUKA KR10 link meshes and builds a driveable arm from them.
 *
 *   UnrealEditor-Cmd.exe AutoMotion_PCB.uproject -run=FactoryImportRobot [-Force]
 *
 * Source meshes come from ros-industrial / kroshu KUKA robot descriptions
 * (Apache-2.0), converted COLLADA to glTF because Unreal reads neither COLLADA
 * nor STL natively. See Tools/robot_import/kuka_kr10.
 *
 * The arm is assembled as a hierarchy of static mesh components placed at the
 * joint origins taken from the URDF, rather than as a skeletal mesh. That suits
 * a twin: a robot's pose comes from joint values on the wire, so components
 * driven by six angles are both simpler and more accurate than an animation
 * asset that plays a fixed motion regardless of the data.
 */
UCLASS()
class FACTORYSIM_API UFactoryImportRobotCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UFactoryImportRobotCommandlet();

	virtual int32 Main(const FString& Params) override;
};
