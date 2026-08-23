#include "FactoryLineSubsystem.h"

#include "FactoryMachineComponent.h"
#include "FactoryMachineInstance.h"
#include "FactorySimTypes.h"
#include "FactoryTwinSettings.h"

// ---------------------------------------------------------------------------
// UFactoryTwinSettings
// ---------------------------------------------------------------------------

UFactoryTwinSettings::UFactoryTwinSettings()
{
	// Default to the local development broker from Tools/broker. The production
	// broker (192.168.100.102:31883) is a settings change away once reachable.
	EdgeNode.Mqtt.Host = TEXT("127.0.0.1");
	EdgeNode.Mqtt.Port = 1883;
	EdgeNode.Mqtt.ClientId = TEXT("ue5_smt_simulator");
	EdgeNode.Mqtt.KeepAliveSeconds = 60;
}

const UFactoryTwinSettings* UFactoryTwinSettings::Get()
{
	return GetDefault<UFactoryTwinSettings>();
}

// ---------------------------------------------------------------------------
// UFactoryLineSubsystem
// ---------------------------------------------------------------------------

void UFactoryLineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Deliberately does not auto-start here; see OnWorldBeginPlay.
}

void UFactoryLineSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	if (Settings != nullptr && Settings->bAutoStartOnBeginPlay && Settings->bUseNativeSimulation)
	{
		StartLine();
	}
}

void UFactoryLineSubsystem::Deinitialize()
{
	StopLine();
	Machines.Reset();
	Super::Deinitialize();
}

bool UFactoryLineSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE only. An editor-preview world must not open a broker session.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFactoryLineSubsystem::StartLine()
{
	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	if (Settings == nullptr)
	{
		return;
	}

	if (!Settings->bUseNativeSimulation)
	{
		UE_LOG(LogFactorySim, Log,
			TEXT("Native simulation is disabled in Factory Twin settings; not starting. "
				 "The legacy Python layer owns publishing while this is off."));
		return;
	}

	StartLineWithConfig(Settings->EdgeNode);
}

void UFactoryLineSubsystem::StartLineWithConfig(const FSparkplugEdgeNodeConfig& InConfig)
{
	if (EdgeNodes.Num() > 0)
	{
		UE_LOG(LogFactorySim, Warning, TEXT("Line is already started"));
		return;
	}

	if (LotId.IsEmpty())
	{
		StartNewLot();
	}

	// The nodes themselves are created in BeginEdgeNodeSession: how many there
	// are depends on which work centres the registered machines belong to, and
	// none of them have registered yet.
	PendingConfig = InConfig;

	// Defer the actual session by a tick.
	//
	// Whichever Blueprint calls this does so from its own BeginPlay, and actor
	// BeginPlay order is arbitrary -- connecting immediately announced the node
	// with only the two machines that happened to have registered first, leaving
	// the rest to trickle in as late DBIRTHs. One tick is enough for every actor
	// in the level to have begun play and registered.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			this, &UFactoryLineSubsystem::BeginEdgeNodeSession);
	}
	else
	{
		BeginEdgeNodeSession();
	}
}

FString UFactoryLineSubsystem::GetEdgeNodeKey(const UFactoryMachineComponent* Machine) const
{
	FString Group = PendingConfig.GroupId;
	FString Node  = PendingConfig.EdgeNodeId;

	if (Machine != nullptr && Machine->Instance != nullptr)
	{
		const FString InstanceGroup = Machine->Instance->GetGroupId();
		const FString InstanceNode  = Machine->Instance->GetEdgeNodeId();
		if (!InstanceGroup.IsEmpty() && !InstanceNode.IsEmpty())
		{
			Group = InstanceGroup;
			Node  = InstanceNode;
		}
	}

	return FString::Printf(TEXT("%s|%s"), *Group, *Node);
}

FSparkplugEdgeNodeConfig UFactoryLineSubsystem::BuildConfigForKey(const FString& Key) const
{
	// Everything except identity comes from the template the caller supplied,
	// so a broker typed into the UI reaches every node.
	FSparkplugEdgeNodeConfig Config = PendingConfig;

	FString Group;
	FString Node;
	if (Key.Split(TEXT("|"), &Group, &Node))
	{
		Config.GroupId = Group;
		Config.EdgeNodeId = Node;
	}

	// Every edge node is a separate MQTT session, and a broker will disconnect
	// the older of two sessions sharing a client id. Without this suffix the
	// second line to connect would silently kick the first off the wire.
	Config.Mqtt.ClientId = FString::Printf(TEXT("%s_%s_%s"),
		*PendingConfig.Mqtt.ClientId,
		*Config.GroupId.Replace(FactoryIsa95::GroupSeparator, TEXT("_")),
		*Config.EdgeNodeId);

	return Config;
}

void UFactoryLineSubsystem::BeginEdgeNodeSession()
{
	// Group the registered machines by the work centre they belong to. Devices
	// must be registered before Connect so their DBIRTHs are part of the birth
	// sequence rather than trickling in afterwards.
	TMap<FString, TArray<UFactoryMachineComponent*>> ByNode;
	for (const TObjectPtr<UFactoryMachineComponent>& Machine : Machines)
	{
		if (Machine == nullptr || Machine->Instance == nullptr)
		{
			continue;
		}

		if (Machine->Instance->GetDeviceId().IsEmpty())
		{
			UE_LOG(LogFactorySim, Warning,
				TEXT("Machine on '%s' has an instance with no device id; skipping"),
				*GetNameSafe(Machine->GetOwner()));
			continue;
		}

		ByNode.FindOrAdd(GetEdgeNodeKey(Machine)).Add(Machine);
	}

	if (ByNode.Num() == 0)
	{
		UE_LOG(LogFactorySim, Warning,
			TEXT("No machines with a usable device id; not connecting."));
		return;
	}

	UE_LOG(LogFactorySim, Log,
		TEXT("Starting line with %d machine(s) across %d edge node(s), lot '%s'"),
		Machines.Num(), ByNode.Num(), *LotId);

	for (const TPair<FString, TArray<UFactoryMachineComponent*>>& Pair : ByNode)
	{
		const FSparkplugEdgeNodeConfig Config = BuildConfigForKey(Pair.Key);

		USparkplugEdgeNode* Node = NewObject<USparkplugEdgeNode>(this);
		Node->OnOnlineStateChanged.AddDynamic(this, &UFactoryLineSubsystem::HandleEdgeNodeOnline);
		Node->OnNodeCommand.AddDynamic(this, &UFactoryLineSubsystem::HandleNodeCommand);
		EdgeNodes.Add(Pair.Key, Node);

		UE_LOG(LogFactorySim, Log, TEXT("edge node %s/%s -- %d device(s)"),
			*Config.GroupId, *Config.EdgeNodeId, Pair.Value.Num());

		// Tracked as we go rather than by asking HasDeviceId: by this point every
		// machine is already in Machines, so that query would find the machine
		// currently being registered and report it as a duplicate of itself --
		// which skipped all of them and left the node announcing no devices at
		// all.
		TSet<FString> Announced;

		for (UFactoryMachineComponent* Machine : Pair.Value)
		{
			const FString DeviceId = Machine->Instance->GetDeviceId();

			// Unique per edge node, not globally: once several lines run the
			// same station names, "ReflowOven" appears once on each line's node
			// and that is correct.
			if (Announced.Contains(DeviceId))
			{
				UE_LOG(LogFactorySim, Warning,
					TEXT("  '%s' is already registered on this node; skipping %s"),
					*DeviceId, *GetNameSafe(Machine->GetOwner()));
				continue;
			}

			Announced.Add(DeviceId);
			Node->RegisterDevice(DeviceId, Machine->BuildBirthMetrics());
			UE_LOG(LogFactorySim, Log, TEXT("  device '%s' (%s) from %s"),
				*DeviceId, *Machine->Instance->GetUnsPath(),
				*GetNameSafe(Machine->GetOwner()));
		}

		Node->Connect(Config);
	}

	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	if (Settings != nullptr && Settings->bAutoProduceOnStart)
	{
		StartAutoProduction(Settings->AutoProductionIntervalSeconds);
	}
}

bool UFactoryLineSubsystem::HasDeviceId(const FString& Key, const FString& DeviceId) const
{
	for (const TObjectPtr<UFactoryMachineComponent>& Machine : Machines)
	{
		if (Machine != nullptr
			&& Machine->Instance != nullptr
			&& Machine->Instance->GetDeviceId() == DeviceId
			&& GetEdgeNodeKey(Machine) == Key)
		{
			return true;
		}
	}
	return false;
}

USparkplugEdgeNode* UFactoryLineSubsystem::GetEdgeNode() const
{
	for (const TPair<FString, TObjectPtr<USparkplugEdgeNode>>& Pair : EdgeNodes)
	{
		if (Pair.Value != nullptr)
		{
			return Pair.Value;
		}
	}
	return nullptr;
}

TArray<USparkplugEdgeNode*> UFactoryLineSubsystem::GetEdgeNodes() const
{
	TArray<USparkplugEdgeNode*> Result;
	Result.Reserve(EdgeNodes.Num());
	for (const TPair<FString, TObjectPtr<USparkplugEdgeNode>>& Pair : EdgeNodes)
	{
		if (Pair.Value != nullptr)
		{
			Result.Add(Pair.Value);
		}
	}
	return Result;
}

USparkplugEdgeNode* UFactoryLineSubsystem::FindEdgeNodeForMachine(
	const UFactoryMachineComponent* Machine) const
{
	const TObjectPtr<USparkplugEdgeNode>* Found = EdgeNodes.Find(GetEdgeNodeKey(Machine));
	return (Found != nullptr) ? *Found : nullptr;
}

bool UFactoryLineSubsystem::StartLineWithBroker(
	const FString& BrokerHost,
	const int32 BrokerPort,
	const FString& BrokerUsername,
	const FString& BrokerPassword)
{
	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	if (Settings == nullptr)
	{
		return false;
	}

	if (!Settings->bUseNativeSimulation)
	{
		UE_LOG(LogFactorySim, Log,
			TEXT("Native simulation is disabled; ignoring StartLineWithBroker."));
		return false;
	}

	// Take identity and everything else from settings; override only what the
	// operator supplied, so an empty field falls back rather than blanking.
	FSparkplugEdgeNodeConfig Config = Settings->EdgeNode;
	if (!BrokerHost.IsEmpty())
	{
		Config.Mqtt.Host = BrokerHost;
	}
	if (BrokerPort > 0)
	{
		Config.Mqtt.Port = BrokerPort;
	}
	Config.Mqtt.Username = BrokerUsername;
	Config.Mqtt.Password = BrokerPassword;

	StartLineWithConfig(Config);
	// Nodes are created a tick later, so report on whether the attempt was
	// accepted rather than on a map that is necessarily still empty.
	return true;
}

UFactoryMachineComponent* UFactoryLineSubsystem::FindMachine(
	const FString& DeviceIdOrUnsPath) const
{
	const bool bIsPath = DeviceIdOrUnsPath.Contains(TEXT("/"));

	for (const TObjectPtr<UFactoryMachineComponent>& Machine : Machines)
	{
		if (Machine == nullptr || Machine->Instance == nullptr)
		{
			continue;
		}

		const FString Candidate = bIsPath
			? Machine->Instance->GetUnsPath()
			: Machine->Instance->GetDeviceId();

		if (Candidate == DeviceIdOrUnsPath)
		{
			return Machine;
		}
	}
	return nullptr;
}

bool UFactoryLineSubsystem::PublishDeviceEvent(const FString& DeviceId, const FString& EventType)
{
	if (UFactoryMachineComponent* Machine = FindMachine(DeviceId))
	{
		Machine->PublishEvent(EventType);
		return true;
	}

	UE_LOG(LogFactorySim, Warning,
		TEXT("PublishDeviceEvent: no registered machine with device id '%s'"), *DeviceId);
	return false;
}

void UFactoryLineSubsystem::StopLine()
{
	// Releasing boards onto a line that is going offline would emit events with
	// nowhere to publish them.
	StopAutoProduction();

	for (const TPair<FString, TObjectPtr<USparkplugEdgeNode>>& Pair : EdgeNodes)
	{
		if (Pair.Value != nullptr)
		{
			Pair.Value->Disconnect();
		}
	}
	EdgeNodes.Empty();
	RefreshOnlineState();
}

bool UFactoryLineSubsystem::IsOnline() const
{
	if (EdgeNodes.Num() == 0)
	{
		return false;
	}

	// Every node, not any: with one node per line, "the line is online" is only
	// true once all of them have published their births. Reporting online while
	// a line is still dark would have the UI claim data is flowing from a line
	// that is publishing nothing.
	for (const TPair<FString, TObjectPtr<USparkplugEdgeNode>>& Pair : EdgeNodes)
	{
		if (Pair.Value == nullptr || !Pair.Value->IsOnline())
		{
			return false;
		}
	}
	return true;
}

void UFactoryLineSubsystem::RefreshOnlineState()
{
	const bool bOnline = IsOnline();
	if (bOnline == bWasOnline)
	{
		return;
	}

	bWasOnline = bOnline;
	UE_LOG(LogFactorySim, Log, TEXT("Line is %s"), bOnline ? TEXT("online") : TEXT("offline"));
	OnLineOnlineChanged.Broadcast(bOnline);
}

FString UFactoryLineSubsystem::StartNewLot()
{
	// Matches the LOT-YYYYmmdd-HHMMSS shape the Python layer produced.
	LotId = FString::Printf(TEXT("LOT-%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
	return LotId;
}

void UFactoryLineSubsystem::RegisterMachine(UFactoryMachineComponent* Machine)
{
	if (Machine == nullptr || Machines.Contains(Machine))
	{
		return;
	}

	// A Sparkplug device id must be unique on its edge node. Two components
	// claiming the same id would publish interleaved DDATA under one identity
	// and re-announce a DBIRTH each time, which is how a machine component
	// accidentally placed on a per-board spawned actor shows up.
	if (Machine->Instance != nullptr
		&& HasDeviceId(GetEdgeNodeKey(Machine), Machine->Instance->GetDeviceId()))
	{
		UE_LOG(LogFactorySim, Warning,
			TEXT("%s claims device id '%s' on %s, which is already registered. Ignoring it: a "
				 "device id maps to one physical machine, so this component likely belongs on a "
				 "persistent actor rather than one spawned per board."),
			*GetNameSafe(Machine->GetOwner()), *Machine->Instance->GetDeviceId(),
			*GetEdgeNodeKey(Machine));
		return;
	}

	Machines.Add(Machine);

	// Joining an already-live line announces immediately.
	if (Machine->Instance != nullptr)
	{
		if (USparkplugEdgeNode* Node = FindEdgeNodeForMachine(Machine))
		{
			Node->RegisterDevice(Machine->Instance->GetDeviceId(), Machine->BuildBirthMetrics());
		}
	}
}

void UFactoryLineSubsystem::UnregisterMachine(UFactoryMachineComponent* Machine)
{
	if (Machine == nullptr)
	{
		return;
	}

	if (Machine->Instance != nullptr)
	{
		if (USparkplugEdgeNode* Node = FindEdgeNodeForMachine(Machine))
		{
			Node->UnregisterDevice(Machine->Instance->GetDeviceId());
		}
	}
	Machines.Remove(Machine);
}

void UFactoryLineSubsystem::HandleEdgeNodeOnline(const bool bOnline)
{
	// One node's transition is not the line's: with several nodes the delegate
	// fires once per node, so the aggregate is recomputed and only broadcast
	// when it actually changes.
	RefreshOnlineState();
}

void UFactoryLineSubsystem::HandleNodeCommand(const FSparkplugPayload& Payload)
{
	for (const FSparkplugMetric& Metric : Payload.Metrics)
	{
		// `new_material` is the command the retired Python spawn_hook bridged to
		// the PCB spawner; keeping the name keeps existing controllers working.
		if (Metric.Name == TEXT("new_material") && Metric.IntValue != 0)
		{
			UE_LOG(LogFactorySim, Log, TEXT("new_material command received"));
			RequestNewMaterial();
		}
	}
}

void UFactoryLineSubsystem::RequestNewMaterial()
{
	const FString NewLot = StartNewLot();
	++BoardsReleased;

	// Stamp every machine, matching what the Python layer did on this command.
	for (const TObjectPtr<UFactoryMachineComponent>& Machine : Machines)
	{
		if (Machine != nullptr)
		{
			Machine->PublishEvent(FactoryEventTypes::NewMaterial);
		}
	}

	UE_LOG(LogFactorySim, Log, TEXT("New material released, lot '%s' (board %d)"),
		*NewLot, BoardsReleased);

	OnNewMaterialRequested.Broadcast(NewLot);
}

void UFactoryLineSubsystem::StartAutoProduction(const float IntervalSeconds)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	float Interval = IntervalSeconds;
	if (Interval <= 0.0f)
	{
		Interval = Settings != nullptr ? Settings->AutoProductionIntervalSeconds : 30.0f;
	}
	// A zero or negative period would schedule a timer that fires every frame.
	Interval = FMath::Max(Interval, 0.1f);

	StopAutoProduction();

	AutoProductionInterval = Interval;
	BoardsReleased = 0;

	World->GetTimerManager().SetTimer(
		AutoProductionTimer, this, &UFactoryLineSubsystem::RequestNewMaterial,
		Interval, /*bLoop*/ true, /*FirstDelay*/ 0.0f);

	UE_LOG(LogFactorySim, Log,
		TEXT("Auto production started, releasing a board every %.1fs"), Interval);
}

void UFactoryLineSubsystem::StopAutoProduction()
{
	if (UWorld* World = GetWorld())
	{
		if (AutoProductionTimer.IsValid())
		{
			World->GetTimerManager().ClearTimer(AutoProductionTimer);
			UE_LOG(LogFactorySim, Log,
				TEXT("Auto production stopped after %d board(s)"), BoardsReleased);
		}
	}
	AutoProductionTimer.Invalidate();
	AutoProductionInterval = 0.0f;
}

bool UFactoryLineSubsystem::IsAutoProductionRunning() const
{
	const UWorld* World = GetWorld();
	return World != nullptr
		&& AutoProductionTimer.IsValid()
		&& World->GetTimerManager().IsTimerActive(AutoProductionTimer);
}
