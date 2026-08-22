#include "SparkplugProto.h"

namespace SparkplugProto
{
	// -----------------------------------------------------------------------
	// FWriter
	// -----------------------------------------------------------------------

	void FWriter::WriteVarint(uint64 Value)
	{
		// Base-128, little-endian groups, high bit signals continuation.
		while (Value >= 0x80)
		{
			Buffer.Add(static_cast<uint8>((Value & 0x7F) | 0x80));
			Value >>= 7;
		}
		Buffer.Add(static_cast<uint8>(Value & 0x7F));
	}

	void FWriter::WriteTag(const uint32 FieldNumber, const EWireType WireType)
	{
		WriteVarint((static_cast<uint64>(FieldNumber) << 3) | static_cast<uint64>(WireType));
	}

	void FWriter::WriteUInt64Field(const uint32 FieldNumber, const uint64 Value)
	{
		WriteTag(FieldNumber, EWireType::Varint);
		WriteVarint(Value);
	}

	void FWriter::WriteUInt32Field(const uint32 FieldNumber, const uint32 Value)
	{
		WriteTag(FieldNumber, EWireType::Varint);
		WriteVarint(static_cast<uint64>(Value));
	}

	void FWriter::WriteBoolField(const uint32 FieldNumber, const bool bValue)
	{
		WriteTag(FieldNumber, EWireType::Varint);
		WriteVarint(bValue ? 1 : 0);
	}

	void FWriter::WriteFloatField(const uint32 FieldNumber, const float Value)
	{
		WriteTag(FieldNumber, EWireType::Fixed32);

		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(uint32));
		// Little-endian on the wire.
		Buffer.Add(static_cast<uint8>(Bits & 0xFF));
		Buffer.Add(static_cast<uint8>((Bits >> 8) & 0xFF));
		Buffer.Add(static_cast<uint8>((Bits >> 16) & 0xFF));
		Buffer.Add(static_cast<uint8>((Bits >> 24) & 0xFF));
	}

	void FWriter::WriteDoubleField(const uint32 FieldNumber, const double Value)
	{
		WriteTag(FieldNumber, EWireType::Fixed64);

		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(uint64));
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			Buffer.Add(static_cast<uint8>((Bits >> Shift) & 0xFF));
		}
	}

	void FWriter::WriteStringField(const uint32 FieldNumber, const FString& Value)
	{
		const FTCHARToUTF8 Converted(*Value);
		WriteTag(FieldNumber, EWireType::LengthDelimited);
		WriteVarint(static_cast<uint64>(Converted.Length()));
		Buffer.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
	}

	void FWriter::WriteBytesField(const uint32 FieldNumber, const TArray<uint8>& Value)
	{
		WriteTag(FieldNumber, EWireType::LengthDelimited);
		WriteVarint(static_cast<uint64>(Value.Num()));
		Buffer.Append(Value);
	}

	void FWriter::WriteMessageField(const uint32 FieldNumber, const TArray<uint8>& Encoded)
	{
		WriteBytesField(FieldNumber, Encoded);
	}

	// -----------------------------------------------------------------------
	// FReader
	// -----------------------------------------------------------------------

	bool FReader::ReadVarint(uint64& OutValue)
	{
		uint64 Result = 0;
		int32 Shift = 0;

		while (Position < Size)
		{
			const uint8 Byte = Data[Position++];
			Result |= static_cast<uint64>(Byte & 0x7F) << Shift;

			if ((Byte & 0x80) == 0)
			{
				OutValue = Result;
				return true;
			}

			Shift += 7;
			// A varint wider than 10 groups cannot fit in 64 bits.
			if (Shift >= 70)
			{
				return false;
			}
		}
		return false;
	}

	bool FReader::ReadTag(uint32& OutFieldNumber, EWireType& OutWireType)
	{
		uint64 Tag = 0;
		if (!ReadVarint(Tag))
		{
			return false;
		}

		OutFieldNumber = static_cast<uint32>(Tag >> 3);
		const uint8 WireBits = static_cast<uint8>(Tag & 0x07);
		switch (WireBits)
		{
		case 0: OutWireType = EWireType::Varint; break;
		case 1: OutWireType = EWireType::Fixed64; break;
		case 2: OutWireType = EWireType::LengthDelimited; break;
		case 5: OutWireType = EWireType::Fixed32; break;
		default: return false;  // groups (3/4) are proto2-legacy and unused here
		}
		return OutFieldNumber != 0;
	}

	bool FReader::ReadFixed32(uint32& OutValue)
	{
		if (Remaining() < 4)
		{
			return false;
		}
		OutValue = static_cast<uint32>(Data[Position])
			| (static_cast<uint32>(Data[Position + 1]) << 8)
			| (static_cast<uint32>(Data[Position + 2]) << 16)
			| (static_cast<uint32>(Data[Position + 3]) << 24);
		Position += 4;
		return true;
	}

	bool FReader::ReadFixed64(uint64& OutValue)
	{
		if (Remaining() < 8)
		{
			return false;
		}
		OutValue = 0;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			OutValue |= static_cast<uint64>(Data[Position + Index]) << (Index * 8);
		}
		Position += 8;
		return true;
	}

	bool FReader::ReadString(FString& OutValue)
	{
		uint64 Length = 0;
		if (!ReadVarint(Length) || static_cast<int64>(Length) > Remaining())
		{
			return false;
		}

		// FString(const TCHAR*, int32) treats its second argument as extra slack
		// rather than a length, which reads past the end of a non-terminated
		// buffer; construct from an explicit pointer + size instead.
		const FUTF8ToTCHAR Converted(
			reinterpret_cast<const ANSICHAR*>(Data + Position), static_cast<int32>(Length));
		OutValue = FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
		Position += static_cast<int32>(Length);
		return true;
	}

	bool FReader::ReadBytes(TArray<uint8>& OutValue)
	{
		uint64 Length = 0;
		if (!ReadVarint(Length) || static_cast<int64>(Length) > Remaining())
		{
			return false;
		}

		OutValue.Reset(static_cast<int32>(Length));
		OutValue.Append(Data + Position, static_cast<int32>(Length));
		Position += static_cast<int32>(Length);
		return true;
	}

	bool FReader::SkipField(const EWireType WireType)
	{
		switch (WireType)
		{
		case EWireType::Varint:
		{
			uint64 Discard = 0;
			return ReadVarint(Discard);
		}
		case EWireType::Fixed32:
		{
			uint32 Discard = 0;
			return ReadFixed32(Discard);
		}
		case EWireType::Fixed64:
		{
			uint64 Discard = 0;
			return ReadFixed64(Discard);
		}
		case EWireType::LengthDelimited:
		{
			TArray<uint8> Discard;
			return ReadBytes(Discard);
		}
		default:
			return false;
		}
	}

	// -----------------------------------------------------------------------
	// Metric encoding
	// -----------------------------------------------------------------------

	TArray<uint8> EncodeMetric(const FSparkplugMetric& Metric)
	{
		FWriter Writer;

		// Emit in ascending field number: that is what the Python protobuf
		// runtime does, so the bytes match the reference implementation exactly.
		if (!Metric.Name.IsEmpty())
		{
			Writer.WriteStringField(MetricField::Name, Metric.Name);
		}
		if (Metric.Alias != 0)
		{
			Writer.WriteUInt64Field(MetricField::Alias, static_cast<uint64>(Metric.Alias));
		}
		if (Metric.Timestamp != 0)
		{
			Writer.WriteUInt64Field(MetricField::Timestamp, static_cast<uint64>(Metric.Timestamp));
		}
		Writer.WriteUInt32Field(MetricField::DataType, static_cast<uint32>(Metric.DataType));

		if (Metric.bIsNull)
		{
			Writer.WriteBoolField(MetricField::IsNull, true);
			return Writer.MoveBuffer();
		}

		switch (Metric.DataType)
		{
		case ESparkplugDataType::Int8:
		case ESparkplugDataType::Int16:
		case ESparkplugDataType::Int32:
			// Signed values ride in the unsigned int_value field as two's
			// complement, which is what Tahu does.
			Writer.WriteUInt32Field(
				MetricField::IntValue, static_cast<uint32>(static_cast<int32>(Metric.IntValue)));
			break;

		case ESparkplugDataType::UInt8:
		case ESparkplugDataType::UInt16:
		case ESparkplugDataType::UInt32:
			Writer.WriteUInt32Field(MetricField::IntValue, static_cast<uint32>(Metric.IntValue));
			break;

		case ESparkplugDataType::Int64:
		case ESparkplugDataType::UInt64:
		case ESparkplugDataType::DateTime:
			Writer.WriteUInt64Field(MetricField::LongValue, static_cast<uint64>(Metric.IntValue));
			break;

		case ESparkplugDataType::Float:
			Writer.WriteFloatField(MetricField::FloatValue, static_cast<float>(Metric.DoubleValue));
			break;

		case ESparkplugDataType::Double:
			Writer.WriteDoubleField(MetricField::DoubleValue, Metric.DoubleValue);
			break;

		case ESparkplugDataType::Boolean:
			Writer.WriteBoolField(MetricField::BooleanValue, Metric.IntValue != 0);
			break;

		case ESparkplugDataType::String:
		case ESparkplugDataType::Text:
		case ESparkplugDataType::UUID:
			Writer.WriteStringField(MetricField::StringValue, Metric.StringValue);
			break;

		case ESparkplugDataType::Bytes:
		case ESparkplugDataType::File:
			Writer.WriteBytesField(MetricField::BytesValue, Metric.BytesValue);
			break;

		default:
			UE_LOG(LogSparkplugB, Warning,
				TEXT("Metric '%s' has unsupported datatype %d; sending as null"),
				*Metric.Name, static_cast<int32>(Metric.DataType));
			Writer.WriteBoolField(MetricField::IsNull, true);
			break;
		}

		return Writer.MoveBuffer();
	}

	TArray<uint8> EncodePayload(const FSparkplugPayload& Payload)
	{
		FWriter Writer;

		if (Payload.Timestamp != 0)
		{
			Writer.WriteUInt64Field(PayloadField::Timestamp, static_cast<uint64>(Payload.Timestamp));
		}

		for (const FSparkplugMetric& Metric : Payload.Metrics)
		{
			Writer.WriteMessageField(PayloadField::Metrics, EncodeMetric(Metric));
		}

		// NDEATH deliberately omits seq (Tahu 5.3), which is what bHasSeq encodes.
		if (Payload.bHasSeq)
		{
			Writer.WriteUInt64Field(PayloadField::Seq, static_cast<uint64>(Payload.Seq));
		}

		if (!Payload.UUID.IsEmpty())
		{
			Writer.WriteStringField(PayloadField::UUID, Payload.UUID);
		}

		return Writer.MoveBuffer();
	}

	// -----------------------------------------------------------------------
	// Decoding
	// -----------------------------------------------------------------------

	bool DecodeMetric(const uint8* Data, const int32 Size, FSparkplugMetric& OutMetric)
	{
		FReader Reader(Data, Size);
		OutMetric = FSparkplugMetric();

		while (!Reader.IsAtEnd())
		{
			uint32 FieldNumber = 0;
			EWireType WireType = EWireType::Varint;
			if (!Reader.ReadTag(FieldNumber, WireType))
			{
				return false;
			}

			switch (FieldNumber)
			{
			case MetricField::Name:
				if (!Reader.ReadString(OutMetric.Name)) { return false; }
				break;

			case MetricField::Alias:
			{
				uint64 Value = 0;
				if (!Reader.ReadVarint(Value)) { return false; }
				OutMetric.Alias = static_cast<int64>(Value);
				break;
			}

			case MetricField::Timestamp:
			{
				uint64 Value = 0;
				if (!Reader.ReadVarint(Value)) { return false; }
				OutMetric.Timestamp = static_cast<int64>(Value);
				break;
			}

			case MetricField::DataType:
			{
				uint64 Value = 0;
				if (!Reader.ReadVarint(Value)) { return false; }
				OutMetric.DataType = static_cast<ESparkplugDataType>(Value);
				break;
			}

			case MetricField::IsNull:
			{
				uint64 Value = 0;
				if (!Reader.ReadVarint(Value)) { return false; }
				OutMetric.bIsNull = Value != 0;
				break;
			}

			case MetricField::IntValue:
			{
				uint64 Value = 0;
				if (!Reader.ReadVarint(Value)) { return false; }
				// Reinterpret as signed when the declared type is signed.
				if (OutMetric.DataType == ESparkplugDataType::Int8
					|| OutMetric.DataType == ESparkplugDataType::Int16
					|| OutMetric.DataType == ESparkplugDataType::Int32)
				{
					OutMetric.IntValue = static_cast<int32>(static_cast<uint32>(Value));
				}
				else
				{
					OutMetric.IntValue = static_cast<int64>(Value);
				}
				break;
			}

			case MetricField::LongValue:
			{
				uint64 Value = 0;
				if (!Reader.ReadVarint(Value)) { return false; }
				OutMetric.IntValue = static_cast<int64>(Value);
				break;
			}

			case MetricField::FloatValue:
			{
				uint32 Bits = 0;
				if (!Reader.ReadFixed32(Bits)) { return false; }
				float Value = 0.0f;
				FMemory::Memcpy(&Value, &Bits, sizeof(float));
				OutMetric.DoubleValue = static_cast<double>(Value);
				break;
			}

			case MetricField::DoubleValue:
			{
				uint64 Bits = 0;
				if (!Reader.ReadFixed64(Bits)) { return false; }
				double Value = 0.0;
				FMemory::Memcpy(&Value, &Bits, sizeof(double));
				OutMetric.DoubleValue = Value;
				break;
			}

			case MetricField::BooleanValue:
			{
				uint64 Value = 0;
				if (!Reader.ReadVarint(Value)) { return false; }
				OutMetric.IntValue = (Value != 0) ? 1 : 0;
				break;
			}

			case MetricField::StringValue:
				if (!Reader.ReadString(OutMetric.StringValue)) { return false; }
				break;

			case MetricField::BytesValue:
				if (!Reader.ReadBytes(OutMetric.BytesValue)) { return false; }
				break;

			default:
				// Metadata, properties, datasets and templates are not modelled
				// yet; skipping keeps decoding forward-compatible.
				if (!Reader.SkipField(WireType)) { return false; }
				break;
			}
		}

		return true;
	}

	bool DecodePayload(const TArray<uint8>& Bytes, FSparkplugPayload& OutPayload)
	{
		FReader Reader(Bytes);
		OutPayload = FSparkplugPayload();
		OutPayload.bHasSeq = false;

		while (!Reader.IsAtEnd())
		{
			uint32 FieldNumber = 0;
			EWireType WireType = EWireType::Varint;
			if (!Reader.ReadTag(FieldNumber, WireType))
			{
				return false;
			}

			switch (FieldNumber)
			{
			case PayloadField::Timestamp:
			{
				uint64 Value = 0;
				if (!Reader.ReadVarint(Value)) { return false; }
				OutPayload.Timestamp = static_cast<int64>(Value);
				break;
			}

			case PayloadField::Metrics:
			{
				TArray<uint8> MetricBytes;
				if (!Reader.ReadBytes(MetricBytes)) { return false; }

				FSparkplugMetric Metric;
				if (!DecodeMetric(MetricBytes.GetData(), MetricBytes.Num(), Metric)) { return false; }
				OutPayload.Metrics.Add(MoveTemp(Metric));
				break;
			}

			case PayloadField::Seq:
			{
				uint64 Value = 0;
				if (!Reader.ReadVarint(Value)) { return false; }
				OutPayload.Seq = static_cast<int64>(Value);
				OutPayload.bHasSeq = true;
				break;
			}

			case PayloadField::UUID:
				if (!Reader.ReadString(OutPayload.UUID)) { return false; }
				break;

			default:
				if (!Reader.SkipField(WireType)) { return false; }
				break;
			}
		}

		return true;
	}
}
