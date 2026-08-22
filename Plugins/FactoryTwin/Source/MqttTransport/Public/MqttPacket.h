#pragma once

#include "CoreMinimal.h"
#include "MqttTransportTypes.h"

/** MQTT 3.1.1 control packet types (section 2.2.1). */
enum class EMqttPacketType : uint8
{
	None        = 0,
	Connect     = 1,
	ConnAck     = 2,
	Publish     = 3,
	PubAck      = 4,
	PubRec      = 5,
	PubRel      = 6,
	PubComp     = 7,
	Subscribe   = 8,
	SubAck      = 9,
	Unsubscribe = 10,
	UnsubAck    = 11,
	PingReq     = 12,
	PingResp    = 13,
	Disconnect  = 14
};

/**
 * Byte writer for MQTT wire format. All multi-byte integers are big-endian and
 * all strings are a 2-byte length prefix followed by UTF-8 (section 1.5.3).
 */
class MQTTTRANSPORT_API FMqttPacketWriter
{
public:
	void WriteByte(uint8 Value);
	void WriteUInt16(uint16 Value);
	/** Length-prefixed UTF-8. */
	void WriteString(const FString& Value);
	/** Length-prefixed raw bytes; used for the will payload, which is binary. */
	void WriteBinary(const TArray<uint8>& Value);
	/** Unprefixed raw bytes; used for a PUBLISH body, which runs to end of packet. */
	void WriteRaw(const TArray<uint8>& Value);

	const TArray<uint8>& GetBuffer() const { return Buffer; }
	TArray<uint8> MoveBuffer() { return MoveTemp(Buffer); }
	int32 Num() const { return Buffer.Num(); }

private:
	TArray<uint8> Buffer;
};

/** Bounds-checked reader. Every accessor returns false rather than reading past the end. */
class MQTTTRANSPORT_API FMqttPacketReader
{
public:
	FMqttPacketReader(const uint8* InData, const int32 InSize)
		: Data(InData), Size(InSize), Position(0)
	{
	}

	explicit FMqttPacketReader(const TArray<uint8>& InBuffer)
		: Data(InBuffer.GetData()), Size(InBuffer.Num()), Position(0)
	{
	}

	bool ReadByte(uint8& OutValue);
	bool ReadUInt16(uint16& OutValue);
	bool ReadString(FString& OutValue);
	/** Consumes everything left. */
	bool ReadRemaining(TArray<uint8>& OutValue);

	int32 Remaining() const { return Size - Position; }
	int32 Tell() const { return Position; }

private:
	const uint8* Data;
	int32 Size;
	int32 Position;
};

namespace MqttPacket
{
	/** Max bytes a remaining-length field can occupy (section 2.2.3). */
	constexpr int32 MaxRemainingLengthBytes = 4;
	/** 256MB - 1, the largest representable remaining length. */
	constexpr uint32 MaxPayloadSize = 268435455;

	/** Encodes a remaining length into 1-4 bytes. Returns bytes written, or 0 if too large. */
	MQTTTRANSPORT_API int32 EncodeRemainingLength(uint32 Length, uint8 OutBytes[MaxRemainingLengthBytes]);

	/**
	 * Decodes a remaining length.
	 * Returns false when Available holds only part of the field, in which case
	 * the caller should wait for more bytes rather than treating it as an error.
	 */
	MQTTTRANSPORT_API bool DecodeRemainingLength(
		const uint8* Data, int32 Available, uint32& OutLength, int32& OutBytesConsumed);

	/** Prepends the fixed header to an already-built variable header + payload. */
	MQTTTRANSPORT_API TArray<uint8> Frame(EMqttPacketType Type, uint8 Flags, const TArray<uint8>& Body);

	MQTTTRANSPORT_API TArray<uint8> BuildConnect(const FMqttConnectionOptions& Options);

	MQTTTRANSPORT_API TArray<uint8> BuildPublish(
		const FString& Topic,
		const TArray<uint8>& Payload,
		EMqttQoS QoS,
		bool bRetain,
		uint16 PacketId,
		bool bDuplicate = false);

	/** PUBACK / PUBREC / PUBREL / PUBCOMP all share this shape. */
	MQTTTRANSPORT_API TArray<uint8> BuildAck(EMqttPacketType Type, uint16 PacketId);

	MQTTTRANSPORT_API TArray<uint8> BuildSubscribe(
		uint16 PacketId, const TArray<TPair<FString, EMqttQoS>>& Subscriptions);

	MQTTTRANSPORT_API TArray<uint8> BuildUnsubscribe(uint16 PacketId, const TArray<FString>& Topics);

	MQTTTRANSPORT_API TArray<uint8> BuildPingReq();
	MQTTTRANSPORT_API TArray<uint8> BuildDisconnect();

	/** Parses a CONNACK body (session-present flag + return code). */
	MQTTTRANSPORT_API bool ParseConnAck(
		const TArray<uint8>& Body, bool& bOutSessionPresent, EMqttConnectReturnCode& OutReturnCode);

	/** Parses a PUBLISH body. PacketId is only present when QoS > 0. */
	MQTTTRANSPORT_API bool ParsePublish(
		const TArray<uint8>& Body, uint8 Flags, FMqttTransportMessage& OutMessage, uint16& OutPacketId);

	/** Parses a PUBACK-shaped body. */
	MQTTTRANSPORT_API bool ParseAck(const TArray<uint8>& Body, uint16& OutPacketId);

	/** Parses a SUBACK body: packet id followed by one return code per requested topic. */
	MQTTTRANSPORT_API bool ParseSubAck(
		const TArray<uint8>& Body, uint16& OutPacketId, TArray<uint8>& OutReturnCodes);
}
