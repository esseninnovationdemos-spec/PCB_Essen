#include "FactoryLineSubsystem.h"

#include "FactoryMachineComponent.h"
#include "FactoryMachineInstance.h"
#include "FactorySimTypes.h"
#include "FactoryTwinSettings.h"

#include "TimerManager.h"

namespace
{
	/**
	 * The bare command word from a metric name.
	 *
	 * Collapses the two spellings the twin has to accept into one: a Sparkplug
	 * primary application sends "Station Control/Trigger" on a DCMD, while a
	 * followed PLC publishes a flat tag such as "Line1/PIN_INSERTION/trigger".
	 * Both end in the command, so the last segment lowercased is the whole rule.
	 */
	FString CommandWord(const FString& MetricName)
	{
		FString Tail = MetricName;
		int32 SlashIndex = INDEX_NONE;
		if (MetricName.FindLastChar(TEXT('/'), SlashIndex))
		{
			Tail = MetricName.Mid(SlashIndex + 1);
		}
		return Tail.ToLower();
	}

	/**
	 * True when Text ends with Token, either exactly or behind a separator.
	 *
	 * A separator is required rather than a bare suffix so a station called
	 * MAIN_PIN_CHECK cannot answer to PIN_CHECK by coincidence.
	 */
	bool EndsWithToken(const FString& Text, const FString& Token)
	{
		if (Token.IsEmpty() || Text.Len() < Token.Len())
		{
			return false;
		}
		if (Text.Equals(Token, ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (Text.Len() == Token.Len() || !Text.EndsWith(Token, ESearchCase::IgnoreCase))
		{
			return false;
		}
		const TCHAR Separator = Text[Text.Len() - Token.Len() - 1];
		return Separator == TEXT('_') || Separator == TEXT('/');
	}

	/**
	 * A command metric's value as an integer.
	 *
	 * PLCs are inconsistent about the type they publish a pulse as -- boolean,
	 * int, or a float counter -- and all three mean the same thing here.
	 */
	int64 CommandValue(const FSparkplugMetric& Metric)
	{
		if (Metric.UsesDoubleStorage())
		{
			return static_cast<int64>(Metric.DoubleValue);
		}
		return Metric.IntValue;
	}
}

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
	// Resolved here rather than in StartLine because every entry point --
	// settings, explicit config, runtime broker details -- funnels through this.
	if (const UFactoryTwinSettings* ControlSettings = UFactoryTwinSettings::Get())
	{
		PlcEdgeNodeId = ControlSettings->bExternalControlEnabled
			? ControlSettings->PlcEdgeNodeId
			: FString();
	}

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
		Node->OnDeviceCommand.AddDynamic(this, &UFactoryLineSubsystem::HandleDeviceCommand);
		Node->OnForeignNodeData.AddDynamic(this, &UFactoryLineSubsystem::HandleForeignNodeData);

		// Before Connect, so the tap is part of the first subscribe batch and no
		// PLC message can slip through between connecting and subscribing.
		if (!PlcEdgeNodeId.IsEmpty() && PlcEdgeNodeId != Config.EdgeNodeId)
		{
			Node->ObserveEdgeNode(PlcEdgeNodeId);
		}

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

	RefreshPlcFollowing();
}

void UFactoryLineSubsystem::RefreshPlcFollowing()
{
	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	if (Settings == nullptr)
	{
		return;
	}

	if (!Settings->bExternalControlEnabled)
	{
		return;
	}

	SetControlMode(EFactoryControlMode::External);

	if (PlcEdgeNodeId.IsEmpty())
	{
		// DCMD-only: a primary application drives us and there is no peer to
		// watchdog, so silence is not by itself a fault.
		UE_LOG(LogFactorySim, Log,
			TEXT("External control on, following no PLC node -- commands accepted on DCMD only"));
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Count the timeout from bring-up, so a PLC that never appears at all trips
	// the watchdog just as one that stops mid-run does.
	LastPlcMessageSeconds = World->GetTimeSeconds();

	World->GetTimerManager().SetTimer(
		PlcWatchdogTimer, this, &UFactoryLineSubsystem::CheckPlcWatchdog, 1.0f, true);

	UE_LOG(LogFactorySim, Log,
		TEXT("External control on, following PLC edge node '%s' (timeout %.1fs)"),
		*PlcEdgeNodeId, Settings->PlcTimeoutSeconds);
}

void UFactoryLineSubsystem::CheckPlcWatchdog()
{
	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	UWorld* World = GetWorld();
	if (Settings == nullptr || World == nullptr)
	{
		return;
	}

	if (ControlMode != EFactoryControlMode::External || IsPlcOnline())
	{
		return;
	}

	if (!Settings->bFallBackToLocalOnPlcTimeout)
	{
		// Deliberate: the interlock is the demonstration. Warn once per tick so
		// the reason the line is stopped is visible in the log.
		UE_LOG(LogFactorySim, Warning,
			TEXT("PLC '%s' silent for over %.1fs; stations stay blocked"),
			*PlcEdgeNodeId, Settings->PlcTimeoutSeconds);
		return;
	}

	UE_LOG(LogFactorySim, Warning,
		TEXT("PLC '%s' silent for over %.1fs; handing sequencing back to the line"),
		*PlcEdgeNodeId, Settings->PlcTimeoutSeconds);

	SetControlMode(EFactoryControlMode::Local);
	World->GetTimerManager().ClearTimer(PlcWatchdogTimer);
}

bool UFactoryLineSubsystem::IsPlcOnline() const
{
	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	const UWorld* World = GetWorld();
	if (Settings == nullptr || World == nullptr || LastPlcMessageSeconds <= 0.0)
	{
		return false;
	}

	return (World->GetTimeSeconds() - LastPlcMessageSeconds) <= Settings->PlcTimeoutSeconds;
}

void UFactoryLineSubsystem::SetControlMode(const EFactoryControlMode NewMode)
{
	if (ControlMode == NewMode)
	{
		return;
	}

	ControlMode = NewMode;
	const bool bRequireTrigger = ControlMode == EFactoryControlMode::External;

	for (const TObjectPtr<UFactoryMachineComponent>& Machine : Machines)
	{
		if (Machine != nullptr)
		{
			Machine->SetRequireExternalTrigger(bRequireTrigger);
		}
	}

	UE_LOG(LogFactorySim, Log, TEXT("Control mode is now %s"),
		bRequireTrigger ? TEXT("external") : TEXT("local"));
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

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlcWatchdogTimer);
	}

	// Edge history is per session: after a reconnect the first trigger must not
	// be suppressed because its value matches one seen before the line dropped.
	LastCommandValues.Reset();
	LastPlcMessageSeconds = 0.0;

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
		// Level, not edge: an NCMD is a discrete instruction, so two identical
		// writes mean two boards.
		ApplyLineCommand(CommandWord(Metric.Name), Metric, /*bEdgeSensitive=*/false);
	}
}

void UFactoryLineSubsystem::HandleDeviceCommand(
	const FString& DeviceId, const FSparkplugPayload& Payload)
{
	UFactoryMachineComponent* Machine = FindMachine(DeviceId);
	if (Machine == nullptr)
	{
		UE_LOG(LogFactorySim, Warning,
			TEXT("DCMD for unknown device '%s'; ignoring"), *DeviceId);
		return;
	}

	for (const FSparkplugMetric& Metric : Payload.Metrics)
	{
		ApplyStationCommand(Machine, DeviceId, CommandWord(Metric.Name), Metric,
			/*bEdgeSensitive=*/false, /*bSeedOnly=*/false);
	}
}

void UFactoryLineSubsystem::HandleForeignNodeData(
	const ESparkplugMessageType MessageType,
	const FString& EdgeNodeId,
	const FString& DeviceId,
	const FSparkplugPayload& Payload)
{
	if (PlcEdgeNodeId.IsEmpty() || EdgeNodeId != PlcEdgeNodeId)
	{
		return;
	}

	// A death certificate is an announced silence. Zeroing the clock rather than
	// stamping it lets the watchdog react on its next tick instead of waiting
	// out a full timeout for news that has already arrived.
	if (MessageType == ESparkplugMessageType::NDEATH
		|| MessageType == ESparkplugMessageType::DDEATH)
	{
		UE_LOG(LogFactorySim, Warning,
			TEXT("PLC '%s' published a death certificate"), *EdgeNodeId);
		LastPlcMessageSeconds = 0.0;
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		LastPlcMessageSeconds = World->GetTimeSeconds();
	}

	const bool bSeedOnly = MessageType == ESparkplugMessageType::NBIRTH
		|| MessageType == ESparkplugMessageType::DBIRTH;

	for (const FSparkplugMetric& Metric : Payload.Metrics)
	{
		UFactoryMachineComponent* Machine = nullptr;
		FString TargetKey;
		FString Command;
		if (!ResolveCommandTarget(Metric.Name, Machine, TargetKey, Command))
		{
			// A PLC publishes its process tags on the same stream; anything that
			// carries no command word is simply not addressed to us.
			continue;
		}

		if (Machine == nullptr)
		{
			if (!bSeedOnly)
			{
				ApplyLineCommand(Command, Metric, /*bEdgeSensitive=*/true);
			}
			continue;
		}

		ApplyStationCommand(Machine, TargetKey, Command, Metric,
			/*bEdgeSensitive=*/true, bSeedOnly);
	}
}

bool UFactoryLineSubsystem::ResolveCommandTarget(
	const FString& MetricName,
	UFactoryMachineComponent*& OutMachine,
	FString& OutTargetKey,
	FString& OutCommand) const
{
	// Longest first, so a name ending in "new_material" is not read as "mode"
	// or any other word that happens to be a tail of it.
	static const TCHAR* const KnownCommands[] = {
		TEXT("new_material"),
		TEXT("trigger"),
		TEXT("enable"),
		TEXT("reset"),
		TEXT("hold"),
		TEXT("mode")
	};

	OutMachine = nullptr;
	OutTargetKey.Reset();

	const FString Lower = MetricName.ToLower();

	for (const TCHAR* const Command : KnownCommands)
	{
		const FString CommandText(Command);
		if (!EndsWithToken(Lower, CommandText))
		{
			continue;
		}

		OutCommand = CommandText;

		// The whole name is the command: a line-level tag with no prefix.
		if (Lower.Len() == CommandText.Len())
		{
			return true;
		}

		// Drop the command word and the separator ahead of it.
		const FString Remainder = MetricName.Left(MetricName.Len() - CommandText.Len() - 1);

		int32 BestLength = 0;
		for (const TObjectPtr<UFactoryMachineComponent>& Machine : Machines)
		{
			if (Machine == nullptr || Machine->Instance == nullptr)
			{
				continue;
			}

			const FString DeviceId = Machine->Instance->GetDeviceId();
			if (DeviceId.Len() <= BestLength || !EndsWithToken(Remainder, DeviceId))
			{
				continue;
			}

			// What is left once the station id is removed must name the edge
			// node. Three lines carry identically named stations, so without
			// this a Line1 tag would match whichever LOADER came first.
			const FString Prefix = Remainder.Left(
				FMath::Max(0, Remainder.Len() - DeviceId.Len() - 1));
			const FString EdgeNodeId = Machine->Instance->GetEdgeNodeId();

			if (!Prefix.IsEmpty() && !EdgeNodeId.IsEmpty()
				&& !EndsWithToken(Prefix, EdgeNodeId))
			{
				continue;
			}

			OutMachine = Machine;
			OutTargetKey = EdgeNodeId + TEXT("|") + DeviceId;
			BestLength = DeviceId.Len();
		}

		// Nothing matched, so this addresses the line rather than a station.
		return true;
	}

	return false;
}

void UFactoryLineSubsystem::ApplyStationCommand(
	UFactoryMachineComponent* Machine,
	const FString& TargetKey,
	const FString& CommandName,
	const FSparkplugMetric& Metric,
	const bool bEdgeSensitive,
	const bool bSeedOnly)
{
	if (Machine == nullptr)
	{
		return;
	}

	const int64 Value = CommandValue(Metric);
	int64& Last = LastCommandValues.FindOrAdd(TargetKey + TEXT("|") + CommandName, 0);
	const int64 Previous = Last;
	Last = Value;

	if (bSeedOnly)
	{
		return;
	}

	// Any change to a non-zero value fires, so a boolean pulse and a
	// monotonically incrementing counter each read as one command.
	const bool bFired = bEdgeSensitive ? (Value != 0 && Value != Previous) : (Value != 0);

	if (CommandName == TEXT("trigger"))
	{
		if (bFired)
		{
			Machine->ExternalTrigger();
		}
	}
	else if (CommandName == TEXT("enable"))
	{
		// A latch in both directions, so it is read as a level either way.
		Machine->SetStationEnabled(Value != 0);
	}
	else if (CommandName == TEXT("hold"))
	{
		Machine->SetStationHold(Value != 0);
	}
	else if (CommandName == TEXT("reset"))
	{
		if (bFired)
		{
			Machine->ResetStationFault();
		}
	}
}

void UFactoryLineSubsystem::ApplyLineCommand(
	const FString& CommandName, const FSparkplugMetric& Metric, const bool bEdgeSensitive)
{
	if (CommandName == TEXT("new_material"))
	{
		const int64 Value = CommandValue(Metric);
		int64& Last = LastCommandValues.FindOrAdd(TEXT("<line>|new_material"), 0);
		const int64 Previous = Last;
		Last = Value;

		if (bEdgeSensitive ? (Value != 0 && Value != Previous) : (Value != 0))
		{
			UE_LOG(LogFactorySim, Log, TEXT("new_material command received"));
			RequestNewMaterial();
		}
	}
	else if (CommandName == TEXT("mode"))
	{
		// A string where the controller can send one, an integer where it
		// cannot -- PAC Control publishes an int32 far more readily than text.
		const bool bExternal = Metric.StringValue.IsEmpty()
			? CommandValue(Metric) != 0
			: Metric.StringValue.Equals(TEXT("external"), ESearchCase::IgnoreCase);

		SetControlMode(bExternal ? EFactoryControlMode::External : EFactoryControlMode::Local);
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
