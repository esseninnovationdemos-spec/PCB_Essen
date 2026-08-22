#include "Misc/AutomationTest.h"
#include "MqttConnection.h"
#include "SparkplugProto.h"
#include "SparkplugTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags LiveTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** Spins the calling thread until Predicate is true or the budget expires. */
	bool WaitFor(const TFunctionRef<bool()>& Predicate, const double TimeoutSeconds)
	{
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		while (FPlatformTime::Seconds() < Deadline)
		{
			if (Predicate())
			{
				return true;
			}
			FPlatformProcess::Sleep(0.02f);
		}
		return Predicate();
	}
}

/**
 * End-to-end against the local broker from Tools/broker.
 *
 * Publishes a Sparkplug NBIRTH whose bytes are then decoded out-of-band by the
 * Eclipse Tahu reference implementation (Content/Python/sparkplug_b_pb2.py) to
 * confirm the hand-rolled encoder is wire-compatible.
 *
 * Skips with a warning rather than failing when no broker is reachable, so the
 * suite stays green on machines that have not started one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugLivePublishTest,
	"FactoryTwin.SparkplugB.Live.PublishToLocalBroker",
	LiveTestFlags)

bool FSparkplugLivePublishTest::RunTest(const FString& Parameters)
{
	FMqttConnectionOptions Options;
	Options.Host = TEXT("127.0.0.1");
	Options.Port = 1883;
	Options.ClientId = TEXT("factorytwin-livetest");
	Options.KeepAliveSeconds = 30;
	Options.bAutoReconnect = false;

	// Register NDEATH as the will, exactly as a real edge node would.
	FSparkplugPayload DeathPayload;
	DeathPayload.Timestamp = SparkplugUtils::UtcNowMilliseconds();
	DeathPayload.bHasSeq = false;
	DeathPayload.Metrics.Add(FSparkplugMetric::MakeUInt64(TEXT("bdSeq"), 0, 0));

	Options.Will.bEnabled = true;
	Options.Will.Topic = TEXT("spBv1.0/SMT_Line/NDEATH/Cluj");
	Options.Will.Payload = SparkplugProto::EncodePayload(DeathPayload);
	Options.Will.QoS = EMqttQoS::AtLeastOnce;
	Options.Will.bRetain = false;

	const TSharedRef<FMqttConnection, ESPMode::ThreadSafe> Connection =
		FMqttConnection::Create(Options);

	std::atomic<bool> bConnectFired{ false };
	EMqttConnectReturnCode ReturnCode = EMqttConnectReturnCode::ConnectionFailed;
	Connection->OnConnected().AddLambda([&bConnectFired, &ReturnCode](const EMqttConnectReturnCode InCode)
	{
		ReturnCode = InCode;
		bConnectFired = true;
	});

	Connection->Open();

	if (!WaitFor([&bConnectFired]() { return bConnectFired.load(); }, 10.0))
	{
		AddWarning(TEXT("No MQTT broker on 127.0.0.1:1883 - skipping. ")
			TEXT("Start it with: cd Tools/broker && docker compose up -d"));
		Connection->Close();
		return true;
	}

	if (ReturnCode != EMqttConnectReturnCode::Accepted)
	{
		AddError(FString::Printf(
			TEXT("Broker refused the connection, CONNACK code %d"), static_cast<int32>(ReturnCode)));
		Connection->Close();
		return false;
	}

	TestTrue(TEXT("connection reports connected"), Connection->IsConnected());

	// An NBIRTH carrying every datatype the factory config uses, so the Python
	// side exercises the full encoder.
	FSparkplugPayload Birth;
	Birth.Timestamp = SparkplugUtils::UtcNowMilliseconds();
	Birth.Seq = 0;
	Birth.bHasSeq = true;
	Birth.Metrics.Add(FSparkplugMetric::MakeUInt64(TEXT("bdSeq"), 0, 0));
	Birth.Metrics.Add(FSparkplugMetric::MakeBool(TEXT("Node Control/Rebirth"), 0, false));
	Birth.Metrics.Add(FSparkplugMetric::MakeString(
		TEXT("Properties/SimulatorVersion"), 0, TEXT("1.0.0")));
	Birth.Metrics.Add(FSparkplugMetric::MakeString(
		TEXT("Properties/UE5_Project"), 0, TEXT("SMT_Cluj")));
	Birth.Metrics.Add(FSparkplugMetric::MakeFloat(TEXT("oven_temp_c"), 23, 241.75f));
	Birth.Metrics.Add(FSparkplugMetric::MakeInt32(TEXT("state_code"), 26, 1));
	Birth.Metrics.Add(FSparkplugMetric::MakeString(TEXT("event_type"), 32, TEXT("CYCLE_COMPLETE")));

	const TArray<uint8> Encoded = SparkplugProto::EncodePayload(Birth);
	TestTrue(TEXT("payload is non-empty"), Encoded.Num() > 0);

	// A protobuf payload of this shape always contains at least one null byte;
	// if it did not, the test would not be proving anything about binary safety.
	TestTrue(TEXT("payload contains an embedded null"), Encoded.Contains(0));

	TestTrue(TEXT("publish accepted"), Connection->Publish(
		TEXT("spBv1.0/SMT_Line/NBIRTH/Cluj"), Encoded, EMqttQoS::AtLeastOnce, false));

	// Give the worker thread a moment to drain the outbound queue.
	FPlatformProcess::Sleep(1.0f);

	Connection->Close();

	UE_LOG(LogSparkplugB, Display,
		TEXT("Live test published %d bytes to spBv1.0/SMT_Line/NBIRTH/Cluj"), Encoded.Num());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
