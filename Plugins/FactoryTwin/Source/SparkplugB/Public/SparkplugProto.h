#pragma once

#include "CoreMinimal.h"
#include "SparkplugTypes.h"

/**
 * Minimal protobuf wire codec for the Eclipse Tahu Sparkplug B schema.
 *
 * Hand-rolled rather than linking libprotobuf: we only need the Payload/Metric
 * subset, encoding is the hot path, and this avoids dragging abseil + zlib + a
 * protoc codegen step into the build for ~300 lines of schema.
 *
 * Wire types used here (protobuf encoding spec):
 *   0 varint            int32/int64/uint32/uint64/bool/enum
 *   1 64-bit            double
 *   2 length-delimited  string/bytes/embedded message
 *   5 32-bit            float
 */
namespace SparkplugProto
{
	/** Field numbers in org.eclipse.tahu.protobuf.Payload. */
	namespace PayloadField
	{
		constexpr uint32 Timestamp = 1;
		constexpr uint32 Metrics   = 2;
		constexpr uint32 Seq       = 3;
		constexpr uint32 UUID      = 4;
		constexpr uint32 Body      = 5;
	}

	/** Field numbers in org.eclipse.tahu.protobuf.Payload.Metric. */
	namespace MetricField
	{
		constexpr uint32 Name         = 1;
		constexpr uint32 Alias        = 2;
		constexpr uint32 Timestamp    = 3;
		constexpr uint32 DataType     = 4;
		constexpr uint32 IsHistorical = 5;
		constexpr uint32 IsTransient  = 6;
		constexpr uint32 IsNull       = 7;
		constexpr uint32 Metadata     = 8;
		constexpr uint32 Properties   = 9;
		constexpr uint32 IntValue     = 10;
		constexpr uint32 LongValue    = 11;
		constexpr uint32 FloatValue   = 12;
		constexpr uint32 DoubleValue  = 13;
		constexpr uint32 BooleanValue = 14;
		constexpr uint32 StringValue  = 15;
		constexpr uint32 DataSetValue = 16;
		constexpr uint32 BytesValue   = 17;
		constexpr uint32 TemplateValue = 18;
	}

	enum class EWireType : uint8
	{
		Varint          = 0,
		Fixed64         = 1,
		LengthDelimited = 2,
		Fixed32         = 5
	};

	/** Low-level protobuf writer. */
	class SPARKPLUGB_API FWriter
	{
	public:
		void WriteVarint(uint64 Value);
		void WriteTag(uint32 FieldNumber, EWireType WireType);
		void WriteUInt64Field(uint32 FieldNumber, uint64 Value);
		void WriteUInt32Field(uint32 FieldNumber, uint32 Value);
		void WriteBoolField(uint32 FieldNumber, bool bValue);
		void WriteFloatField(uint32 FieldNumber, float Value);
		void WriteDoubleField(uint32 FieldNumber, double Value);
		void WriteStringField(uint32 FieldNumber, const FString& Value);
		void WriteBytesField(uint32 FieldNumber, const TArray<uint8>& Value);
		/** Writes an embedded message as a length-delimited field. */
		void WriteMessageField(uint32 FieldNumber, const TArray<uint8>& Encoded);

		const TArray<uint8>& GetBuffer() const { return Buffer; }
		TArray<uint8> MoveBuffer() { return MoveTemp(Buffer); }

	private:
		TArray<uint8> Buffer;
	};

	/** Low-level protobuf reader. Every accessor is bounds-checked. */
	class SPARKPLUGB_API FReader
	{
	public:
		FReader(const uint8* InData, const int32 InSize)
			: Data(InData), Size(InSize), Position(0)
		{
		}

		explicit FReader(const TArray<uint8>& InBuffer)
			: Data(InBuffer.GetData()), Size(InBuffer.Num()), Position(0)
		{
		}

		bool ReadVarint(uint64& OutValue);
		bool ReadTag(uint32& OutFieldNumber, EWireType& OutWireType);
		bool ReadFixed32(uint32& OutValue);
		bool ReadFixed64(uint64& OutValue);
		bool ReadString(FString& OutValue);
		bool ReadBytes(TArray<uint8>& OutValue);
		/** Skips a field whose contents we do not care about. */
		bool SkipField(EWireType WireType);

		bool IsAtEnd() const { return Position >= Size; }
		int32 Remaining() const { return Size - Position; }

	private:
		const uint8* Data;
		int32 Size;
		int32 Position;
	};

	/** Encodes a single metric as an embedded message body. */
	SPARKPLUGB_API TArray<uint8> EncodeMetric(const FSparkplugMetric& Metric);

	/** Encodes a full payload, ready to hand to MQTT publish. */
	SPARKPLUGB_API TArray<uint8> EncodePayload(const FSparkplugPayload& Payload);

	/** Decodes a payload. Returns false on malformed input. */
	SPARKPLUGB_API bool DecodePayload(const TArray<uint8>& Bytes, FSparkplugPayload& OutPayload);

	/** Decodes a single metric body. */
	SPARKPLUGB_API bool DecodeMetric(const uint8* Data, int32 Size, FSparkplugMetric& OutMetric);
}
