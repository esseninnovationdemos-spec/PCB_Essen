#include "Misc/AutomationTest.h"
#include "MqttPacket.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

// ---------------------------------------------------------------------------
// Remaining length is the one field where an off-by-one silently corrupts every
// subsequent packet in the stream, so cover all four size tiers and both sides
// of each boundary.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMqttRemainingLengthTest,
	"FactoryTwin.MqttTransport.Packet.RemainingLength",
	TestFlags)

bool FMqttRemainingLengthTest::RunTest(const FString& Parameters)
{
	const uint32 Values[] = { 0, 1, 127, 128, 16383, 16384, 2097151, 2097152, 268435455 };
	const int32 ExpectedBytes[] = { 1, 1, 1, 2, 2, 3, 3, 4, 4 };

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values); ++Index)
	{
		uint8 Encoded[MqttPacket::MaxRemainingLengthBytes];
		const int32 Written = MqttPacket::EncodeRemainingLength(Values[Index], Encoded);

		TestEqual(FString::Printf(TEXT("byte count for %u"), Values[Index]),
			Written, ExpectedBytes[Index]);

		uint32 Decoded = 0;
		int32 Consumed = 0;
		TestTrue(FString::Printf(TEXT("decode %u"), Values[Index]),
			MqttPacket::DecodeRemainingLength(Encoded, Written, Decoded, Consumed));
		TestEqual(FString::Printf(TEXT("round trip %u"), Values[Index]), Decoded, Values[Index]);
		TestEqual(FString::Printf(TEXT("consumed %u"), Values[Index]), Consumed, Written);
	}

	// A truncated field must report "not yet", not a bogus value: the connection
	// relies on this to know it should wait for more bytes.
	const uint8 Partial[] = { 0x80 };
	uint32 Decoded = 0;
	int32 Consumed = 0;
	TestFalse(TEXT("partial length is incomplete"),
		MqttPacket::DecodeRemainingLength(Partial, 1, Decoded, Consumed));

	return true;
}

// ---------------------------------------------------------------------------
// The whole reason this module exists: a binary will payload on CONNECT.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMqttConnectWithBinaryWillTest,
	"FactoryTwin.MqttTransport.Packet.ConnectWithBinaryWill",
	TestFlags)

bool FMqttConnectWithBinaryWillTest::RunTest(const FString& Parameters)
{
	FMqttConnectionOptions Options;
	Options.ClientId = TEXT("test-client");
	Options.KeepAliveSeconds = 60;
	Options.bCleanSession = true;
	Options.Will.bEnabled = true;
	Options.Will.Topic = TEXT("spBv1.0/InnoLab:Essen:SMT/NDEATH/Line1");
	Options.Will.QoS = EMqttQoS::AtLeastOnce;
	// Deliberately hostile bytes: an embedded null and a high byte are exactly
	// what broke the old mosquitto-based plugin (strlen + ANSICHAR narrowing).
	Options.Will.Payload = { 0x08, 0x00, 0xFF, 0x7F, 0x00, 0x42 };

	const TArray<uint8> Packet = MqttPacket::BuildConnect(Options);

	TestEqual(TEXT("packet type is CONNECT"), static_cast<int32>(Packet[0] >> 4), 1);

	uint32 RemainingLength = 0;
	int32 LengthBytes = 0;
	TestTrue(TEXT("remaining length parses"), MqttPacket::DecodeRemainingLength(
		Packet.GetData() + 1, Packet.Num() - 1, RemainingLength, LengthBytes));
	TestEqual(TEXT("declared length matches actual"),
		static_cast<int32>(RemainingLength), Packet.Num() - 1 - LengthBytes);

	FMqttPacketReader Reader(Packet.GetData() + 1 + LengthBytes, static_cast<int32>(RemainingLength));

	FString ProtocolName;
	uint8 ProtocolLevel = 0;
	uint8 ConnectFlags = 0;
	uint16 KeepAlive = 0;
	TestTrue(TEXT("protocol name"), Reader.ReadString(ProtocolName));
	TestEqual(TEXT("protocol is MQTT"), ProtocolName, FString(TEXT("MQTT")));
	TestTrue(TEXT("protocol level"), Reader.ReadByte(ProtocolLevel));
	TestEqual(TEXT("protocol level is 4 (3.1.1)"), static_cast<int32>(ProtocolLevel), 4);
	TestTrue(TEXT("connect flags"), Reader.ReadByte(ConnectFlags));
	TestTrue(TEXT("keep alive"), Reader.ReadUInt16(KeepAlive));
	TestEqual(TEXT("keep alive value"), static_cast<int32>(KeepAlive), 60);

	TestTrue(TEXT("will flag set"), (ConnectFlags & 0x04) != 0);
	TestTrue(TEXT("clean session set"), (ConnectFlags & 0x02) != 0);
	TestEqual(TEXT("will QoS is 1"), static_cast<int32>((ConnectFlags >> 3) & 0x03), 1);
	TestFalse(TEXT("will retain clear"), (ConnectFlags & 0x20) != 0);

	FString ClientId;
	FString WillTopic;
	TestTrue(TEXT("client id"), Reader.ReadString(ClientId));
	TestEqual(TEXT("client id value"), ClientId, FString(TEXT("test-client")));
	TestTrue(TEXT("will topic"), Reader.ReadString(WillTopic));
	TestEqual(TEXT("will topic value"), WillTopic, Options.Will.Topic);

	// The will payload is length-prefixed binary, so every byte must survive.
	uint16 WillLength = 0;
	TestTrue(TEXT("will payload length"), Reader.ReadUInt16(WillLength));
	TestEqual(TEXT("will payload length value"),
		static_cast<int32>(WillLength), Options.Will.Payload.Num());

	TArray<uint8> WillPayload;
	TestTrue(TEXT("will payload bytes"), Reader.ReadRemaining(WillPayload));
	TestEqual(TEXT("will payload survives round trip"), WillPayload, Options.Will.Payload);

	return true;
}

// ---------------------------------------------------------------------------
// PUBLISH must carry arbitrary bytes untouched.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMqttPublishBinaryPayloadTest,
	"FactoryTwin.MqttTransport.Packet.PublishBinaryPayload",
	TestFlags)

bool FMqttPublishBinaryPayloadTest::RunTest(const FString& Parameters)
{
	// Every byte value 0-255, which includes the null and high-bit cases that
	// a string-based publish path destroys.
	TArray<uint8> Payload;
	Payload.Reserve(256);
	for (int32 Value = 0; Value < 256; ++Value)
	{
		Payload.Add(static_cast<uint8>(Value));
	}

	const FString Topic = TEXT("spBv1.0/InnoLab:Essen:SMT/DDATA/Line1/REFLOW_OVEN");
	const TArray<uint8> Packet =
		MqttPacket::BuildPublish(Topic, Payload, EMqttQoS::AtLeastOnce, false, 0x1234);

	TestEqual(TEXT("packet type is PUBLISH"), static_cast<int32>(Packet[0] >> 4), 3);

	uint32 RemainingLength = 0;
	int32 LengthBytes = 0;
	TestTrue(TEXT("remaining length parses"), MqttPacket::DecodeRemainingLength(
		Packet.GetData() + 1, Packet.Num() - 1, RemainingLength, LengthBytes));

	TArray<uint8> Body;
	Body.Append(Packet.GetData() + 1 + LengthBytes, static_cast<int32>(RemainingLength));

	FMqttTransportMessage Parsed;
	uint16 PacketId = 0;
	TestTrue(TEXT("publish parses"),
		MqttPacket::ParsePublish(Body, Packet[0] & 0x0F, Parsed, PacketId));

	TestEqual(TEXT("topic round trip"), Parsed.Topic, Topic);
	TestEqual(TEXT("packet id round trip"), static_cast<int32>(PacketId), 0x1234);
	TestEqual(TEXT("QoS round trip"), static_cast<int32>(Parsed.QoS), 1);
	TestEqual(TEXT("payload length"), Parsed.Payload.Num(), 256);
	TestEqual(TEXT("all 256 byte values survive"), Parsed.Payload, Payload);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMqttQoSZeroHasNoPacketIdTest,
	"FactoryTwin.MqttTransport.Packet.QoSZeroHasNoPacketId",
	TestFlags)

bool FMqttQoSZeroHasNoPacketIdTest::RunTest(const FString& Parameters)
{
	// DDATA is published at QoS 0, where including a packet id would corrupt
	// the payload by shifting it two bytes.
	const TArray<uint8> Payload = { 0xDE, 0xAD, 0xBE, 0xEF };
	const TArray<uint8> Packet =
		MqttPacket::BuildPublish(TEXT("t"), Payload, EMqttQoS::AtMostOnce, false, 0);

	uint32 RemainingLength = 0;
	int32 LengthBytes = 0;
	MqttPacket::DecodeRemainingLength(
		Packet.GetData() + 1, Packet.Num() - 1, RemainingLength, LengthBytes);

	TArray<uint8> Body;
	Body.Append(Packet.GetData() + 1 + LengthBytes, static_cast<int32>(RemainingLength));

	// topic "t" is 2 length bytes + 1 char = 3, then the payload with no packet id.
	TestEqual(TEXT("body has no packet id"), Body.Num(), 3 + Payload.Num());

	FMqttTransportMessage Parsed;
	uint16 PacketId = 0;
	TestTrue(TEXT("parses"), MqttPacket::ParsePublish(Body, Packet[0] & 0x0F, Parsed, PacketId));
	TestEqual(TEXT("payload intact"), Parsed.Payload, Payload);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
