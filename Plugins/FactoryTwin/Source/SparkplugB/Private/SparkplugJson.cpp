#include "SparkplugJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Base64.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SparkplugProto.h"

#include <limits>

namespace
{
	using FWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
	using FWriterFactory = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

	// Field names of the Tahu JSON representation.
	const TCHAR* const KeyTimestamp = TEXT("timestamp");
	const TCHAR* const KeySeq = TEXT("seq");
	const TCHAR* const KeyUuid = TEXT("uuid");
	const TCHAR* const KeyMetrics = TEXT("metrics");
	const TCHAR* const KeyName = TEXT("name");
	const TCHAR* const KeyAlias = TEXT("alias");
	const TCHAR* const KeyDataType = TEXT("dataType");
	const TCHAR* const KeyIsNull = TEXT("isNull");
	const TCHAR* const KeyValue = TEXT("value");

	/** Indexed by ESparkplugDataType, whose values are wire-visible and fixed. */
	const TCHAR* const DataTypeNames[] =
	{
		TEXT("Unknown"), TEXT("Int8"), TEXT("Int16"), TEXT("Int32"), TEXT("Int64"),
		TEXT("UInt8"), TEXT("UInt16"), TEXT("UInt32"), TEXT("UInt64"),
		TEXT("Float"), TEXT("Double"), TEXT("Boolean"), TEXT("String"),
		TEXT("DateTime"), TEXT("Text"), TEXT("UUID"), TEXT("DataSet"),
		TEXT("Bytes"), TEXT("File"), TEXT("Template"),
	};
	static_assert(UE_ARRAY_COUNT(DataTypeNames) == static_cast<int32>(ESparkplugDataType::Template) + 1,
		"DataTypeNames must cover every ESparkplugDataType");

	/** 2^53: the largest magnitude below which every integer is exact in a double. */
	constexpr double MaxExactIntegerInDouble = 9007199254740992.0;

	/**
	 * Formats a number for JSON in as few digits as still read back exactly.
	 *
	 * Printf is what the engine's own JSON writer uses. The comma replacement
	 * is insurance against a C runtime numeric locale such as de_DE writing a
	 * decimal comma: a formatted number can never legitimately contain one.
	 */
	FString FormatRoundTrip(const double Value, const bool bSinglePrecision)
	{
		FString Text = bSinglePrecision
			? FString::Printf(TEXT("%.6g"), Value)
			: FString::Printf(TEXT("%.15g"), Value);
		Text.ReplaceCharInline(TEXT(','), TEXT('.'));

		const double ReadBack = FCString::Atod(*Text);
		const bool bExact = bSinglePrecision
			? static_cast<float>(ReadBack) == static_cast<float>(Value)
			: ReadBack == Value;
		if (!bExact)
		{
			Text = bSinglePrecision
				? FString::Printf(TEXT("%.9g"), Value)
				: FString::Printf(TEXT("%.17g"), Value);
			Text.ReplaceCharInline(TEXT(','), TEXT('.'));
		}
		return Text;
	}

	void WriteReal(FWriter& Writer, const double Value, const bool bSinglePrecision)
	{
		// JSON has no literal for these, so they travel as strings.
		if (FMath::IsNaN(Value))
		{
			Writer.WriteValue(KeyValue, TEXT("NaN"));
		}
		else if (!FMath::IsFinite(Value))
		{
			Writer.WriteValue(KeyValue, Value > 0.0 ? TEXT("Infinity") : TEXT("-Infinity"));
		}
		else
		{
			Writer.WriteRawJSONValue(KeyValue, FormatRoundTrip(Value, bSinglePrecision));
		}
	}

	bool IsSupported(const ESparkplugDataType DataType)
	{
		switch (DataType)
		{
		case ESparkplugDataType::Unknown:
		case ESparkplugDataType::DataSet:
		case ESparkplugDataType::Template:
			return false;
		default:
			return true;
		}
	}

	/** Writes "value". Narrowing matches SparkplugProto::EncodeMetric exactly. */
	void WriteMetricValue(FWriter& Writer, const FSparkplugMetric& Metric)
	{
		switch (Metric.DataType)
		{
		case ESparkplugDataType::Int8:
		case ESparkplugDataType::Int16:
		case ESparkplugDataType::Int32:
			Writer.WriteRawJSONValue(KeyValue, LexToString(static_cast<int32>(Metric.IntValue)));
			break;

		case ESparkplugDataType::Int64:
			Writer.WriteRawJSONValue(KeyValue, LexToString(Metric.IntValue));
			break;

		case ESparkplugDataType::UInt8:
		case ESparkplugDataType::UInt16:
		case ESparkplugDataType::UInt32:
			Writer.WriteRawJSONValue(KeyValue, LexToString(static_cast<uint32>(Metric.IntValue)));
			break;

		case ESparkplugDataType::UInt64:
		case ESparkplugDataType::DateTime:
			// Stored bit-cast in int64; written unsigned so values above 2^63 are right.
			Writer.WriteRawJSONValue(KeyValue, LexToString(static_cast<uint64>(Metric.IntValue)));
			break;

		case ESparkplugDataType::Float:
			WriteReal(Writer, static_cast<float>(Metric.DoubleValue), true);
			break;

		case ESparkplugDataType::Double:
			WriteReal(Writer, Metric.DoubleValue, false);
			break;

		case ESparkplugDataType::Boolean:
			Writer.WriteValue(KeyValue, Metric.IntValue != 0);
			break;

		case ESparkplugDataType::String:
		case ESparkplugDataType::Text:
		case ESparkplugDataType::UUID:
			Writer.WriteValue(KeyValue, Metric.StringValue);
			break;

		case ESparkplugDataType::Bytes:
		case ESparkplugDataType::File:
			Writer.WriteValue(KeyValue, FBase64::Encode(Metric.BytesValue));
			break;

		default:
			checkNoEntry();  // IsSupported() filters these out before we get here.
			break;
		}
	}

	void WriteMetric(FWriter& Writer, const FSparkplugMetric& Metric)
	{
		Writer.WriteObjectStart();

		if (!Metric.Name.IsEmpty())
		{
			Writer.WriteValue(KeyName, Metric.Name);
		}
		if (Metric.Alias != 0)
		{
			Writer.WriteRawJSONValue(KeyAlias, LexToString(static_cast<uint64>(Metric.Alias)));
		}
		if (Metric.Timestamp != 0)
		{
			Writer.WriteRawJSONValue(KeyTimestamp, LexToString(Metric.Timestamp));
		}
		Writer.WriteValue(KeyDataType, SparkplugJson::DataTypeName(Metric.DataType));

		if (!Metric.bIsNull && !IsSupported(Metric.DataType))
		{
			UE_LOG(LogSparkplugB, Warning,
				TEXT("Metric '%s' has unsupported datatype %d; sending as null"),
				*Metric.Name, static_cast<int32>(Metric.DataType));
		}

		if (Metric.bIsNull || !IsSupported(Metric.DataType))
		{
			Writer.WriteValue(KeyIsNull, true);
			Writer.WriteNull(KeyValue);
		}
		else
		{
			WriteMetricValue(Writer, Metric);
		}

		Writer.WriteObjectEnd();
	}

	// ---------------------------------------------------------------------
	// Reading. Numbers arrive as their original text (StoreNumbersAsStrings),
	// so integers parse exactly; a double is only consulted as a fallback for
	// a producer that wrote 3.0 where it meant 3.
	//
	// Parsing is done here rather than through FJsonValue::TryGetNumber,
	// because the engine's integer parse is Atoi underneath: it reads "1.5" as
	// 1 and "12abc" as 12 and reports both as success.
	// ---------------------------------------------------------------------

	/** The value's text, for a JSON number or a string. Nothing else has any. */
	bool GetText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
	{
		if (!Value.IsValid() || (Value->Type != EJson::Number && Value->Type != EJson::String))
		{
			return false;
		}
		if (!Value->TryGetString(OutText))
		{
			return false;
		}
		OutText.TrimStartAndEndInline();
		return true;
	}

	/** Digits only, no sign, and it must fit. */
	bool ParseUnsignedText(const FStringView Text, uint64& Out)
	{
		if (Text.IsEmpty())
		{
			return false;
		}
		uint64 Value = 0;
		for (const TCHAR Char : Text)
		{
			if (Char < TEXT('0') || Char > TEXT('9'))
			{
				return false;
			}
			const uint64 Digit = static_cast<uint64>(Char - TEXT('0'));
			if (Value > (MAX_uint64 - Digit) / 10)
			{
				return false;  // overflow
			}
			Value = Value * 10 + Digit;
		}
		Out = Value;
		return true;
	}

	bool ParseSignedText(const FStringView Text, int64& Out)
	{
		const bool bNegative = !Text.IsEmpty() && Text[0] == TEXT('-');
		uint64 Magnitude = 0;
		if (!ParseUnsignedText(bNegative ? Text.RightChop(1) : Text, Magnitude))
		{
			return false;
		}
		constexpr uint64 MaxPositive = static_cast<uint64>(MAX_int64);
		if (bNegative)
		{
			if (Magnitude > MaxPositive + 1)
			{
				return false;
			}
			// -(2^63) has no positive counterpart, so negate in unsigned space.
			Out = static_cast<int64>(0 - Magnitude);
			return true;
		}
		if (Magnitude > MaxPositive)
		{
			return false;
		}
		Out = static_cast<int64>(Magnitude);
		return true;
	}

	/** JSON number syntax, loosely: digits, sign, point, exponent, and at least one digit. */
	bool LooksNumeric(const FStringView Text)
	{
		bool bSawDigit = false;
		for (const TCHAR Char : Text)
		{
			if (Char >= TEXT('0') && Char <= TEXT('9'))
			{
				bSawDigit = true;
			}
			else if (Char != TEXT('-') && Char != TEXT('+') && Char != TEXT('.')
				&& Char != TEXT('e') && Char != TEXT('E'))
			{
				return false;
			}
		}
		return bSawDigit;
	}

	/** For "3.0" or "3e2" written where an integer was meant: integral and exactly representable only. */
	bool ParseIntegralRealText(const FString& Text, double& Out)
	{
		if (!LooksNumeric(Text))
		{
			return false;
		}
		const double Parsed = FCString::Atod(*Text);
		if (!FMath::IsFinite(Parsed)
			|| FMath::Abs(Parsed) > MaxExactIntegerInDouble
			|| Parsed != FMath::FloorToDouble(Parsed))
		{
			return false;
		}
		Out = Parsed;
		return true;
	}

	bool ReadInt64(const TSharedPtr<FJsonValue>& Value, int64& Out)
	{
		FString Text;
		if (!GetText(Value, Text))
		{
			return false;
		}
		if (ParseSignedText(Text, Out))
		{
			return true;
		}
		double Integral = 0.0;
		if (ParseIntegralRealText(Text, Integral))
		{
			Out = static_cast<int64>(Integral);
			return true;
		}
		return false;
	}

	bool ReadUInt64(const TSharedPtr<FJsonValue>& Value, uint64& Out)
	{
		FString Text;
		if (!GetText(Value, Text))
		{
			return false;
		}
		if (ParseUnsignedText(Text, Out))
		{
			return true;
		}
		double Integral = 0.0;
		if (ParseIntegralRealText(Text, Integral) && Integral >= 0.0)
		{
			Out = static_cast<uint64>(Integral);
			return true;
		}
		return false;
	}

	bool ReadReal(const TSharedPtr<FJsonValue>& Value, double& Out)
	{
		FString Text;
		if (!GetText(Value, Text))
		{
			return false;
		}
		if (Text == TEXT("NaN"))       { Out = std::numeric_limits<double>::quiet_NaN(); return true; }
		if (Text == TEXT("Infinity"))  { Out = std::numeric_limits<double>::infinity();  return true; }
		if (Text == TEXT("-Infinity")) { Out = -std::numeric_limits<double>::infinity(); return true; }
		if (!LooksNumeric(Text))
		{
			return false;
		}
		Text.ReplaceCharInline(TEXT(','), TEXT('.'));  // same locale insurance as on write
		Out = FCString::Atod(*Text);
		return true;
	}

	/** JSON true/false, 0/1, or the strings "true"/"false". Not anything ToBool() would wave through. */
	bool ReadBool(const TSharedPtr<FJsonValue>& Value, bool& Out)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::Boolean)
		{
			return Value->TryGetBool(Out);
		}
		FString Text;
		if (!GetText(Value, Text))
		{
			return false;
		}
		if (Text == TEXT("1") || Text.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			Out = true;
			return true;
		}
		if (Text == TEXT("0") || Text.Equals(TEXT("false"), ESearchCase::IgnoreCase))
		{
			Out = false;
			return true;
		}
		return false;
	}

	bool ParseDataType(const TSharedPtr<FJsonValue>& Value, ESparkplugDataType& Out)
	{
		FString Name;
		if (Value->Type == EJson::String && Value->TryGetString(Name))
		{
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(DataTypeNames); ++Index)
			{
				if (Name.Equals(DataTypeNames[Index], ESearchCase::IgnoreCase))
				{
					Out = static_cast<ESparkplugDataType>(Index);
					return true;
				}
			}
			return false;
		}

		int64 Code = 0;
		if (ReadInt64(Value, Code) && Code >= 0 && Code < UE_ARRAY_COUNT(DataTypeNames))
		{
			Out = static_cast<ESparkplugDataType>(Code);
			return true;
		}
		return false;
	}

	/**
	 * Guesses a datatype for a metric that did not declare one.
	 *
	 * Tahu always writes dataType, but a hand-rolled JSON publisher often will
	 * not, and refusing its whole payload over that helps nobody.
	 */
	ESparkplugDataType InferDataType(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return ESparkplugDataType::Unknown;
		}
		switch (Value->Type)
		{
		case EJson::Boolean:
			return ESparkplugDataType::Boolean;
		case EJson::String:
			return ESparkplugDataType::String;
		case EJson::Number:
		{
			FString Text;
			int64 AsSigned = 0;
			uint64 AsUnsigned = 0;
			if (!GetText(Value, Text))
			{
				return ESparkplugDataType::Unknown;
			}
			if (ParseSignedText(Text, AsSigned))
			{
				return ESparkplugDataType::Int64;
			}
			// Above 2^63 - 1 but still a whole number.
			if (ParseUnsignedText(Text, AsUnsigned))
			{
				return ESparkplugDataType::UInt64;
			}
			return ESparkplugDataType::Double;
		}
		default:
			return ESparkplugDataType::Unknown;
		}
	}

	/** Mirror of WriteMetricValue. False on a value that does not fit its datatype. */
	bool ReadValue(const TSharedPtr<FJsonValue>& Value, FSparkplugMetric& Metric)
	{
		switch (Metric.DataType)
		{
		case ESparkplugDataType::Int8:
		case ESparkplugDataType::Int16:
		case ESparkplugDataType::Int32:
		{
			int64 Parsed = 0;
			if (!ReadInt64(Value, Parsed)) { return false; }
			Metric.IntValue = static_cast<int32>(Parsed);
			return true;
		}

		case ESparkplugDataType::Int64:
			return ReadInt64(Value, Metric.IntValue);

		case ESparkplugDataType::UInt8:
		case ESparkplugDataType::UInt16:
		case ESparkplugDataType::UInt32:
		{
			uint64 Parsed = 0;
			if (!ReadUInt64(Value, Parsed)) { return false; }
			Metric.IntValue = static_cast<int64>(static_cast<uint32>(Parsed));
			return true;
		}

		case ESparkplugDataType::UInt64:
		case ESparkplugDataType::DateTime:
		{
			uint64 Parsed = 0;
			if (!ReadUInt64(Value, Parsed)) { return false; }
			Metric.IntValue = static_cast<int64>(Parsed);
			return true;
		}

		case ESparkplugDataType::Float:
		case ESparkplugDataType::Double:
		{
			double Parsed = 0.0;
			if (!ReadReal(Value, Parsed)) { return false; }
			Metric.DoubleValue = (Metric.DataType == ESparkplugDataType::Float)
				? static_cast<double>(static_cast<float>(Parsed))
				: Parsed;
			return true;
		}

		case ESparkplugDataType::Boolean:
		{
			bool bParsed = false;
			if (!ReadBool(Value, bParsed)) { return false; }
			Metric.IntValue = bParsed ? 1 : 0;
			return true;
		}

		case ESparkplugDataType::String:
		case ESparkplugDataType::Text:
		case ESparkplugDataType::UUID:
			return Value->TryGetString(Metric.StringValue);

		case ESparkplugDataType::Bytes:
		case ESparkplugDataType::File:
		{
			FString Encoded;
			return Value->TryGetString(Encoded) && FBase64::Decode(Encoded, Metric.BytesValue);
		}

		default:
			// DataSet, Template, Unknown: unsupported, as on the protobuf side.
			Metric.bIsNull = true;
			return true;
		}
	}

	bool ReadMetric(const FJsonObject& Object, FSparkplugMetric& Out)
	{
		Object.TryGetStringField(KeyName, Out.Name);

		if (const TSharedPtr<FJsonValue> Alias = Object.TryGetField(KeyAlias))
		{
			uint64 Parsed = 0;
			if (!ReadUInt64(Alias, Parsed)) { return false; }
			Out.Alias = static_cast<int64>(Parsed);
		}

		if (const TSharedPtr<FJsonValue> Stamp = Object.TryGetField(KeyTimestamp))
		{
			if (!ReadInt64(Stamp, Out.Timestamp)) { return false; }
		}

		const TSharedPtr<FJsonValue> Value = Object.TryGetField(KeyValue);
		const TSharedPtr<FJsonValue> DataType = Object.TryGetField(KeyDataType);
		if (DataType.IsValid() && !DataType->IsNull())
		{
			if (!ParseDataType(DataType, Out.DataType)) { return false; }
		}
		else
		{
			Out.DataType = InferDataType(Value);
		}

		bool bIsNull = false;
		Object.TryGetBoolField(KeyIsNull, bIsNull);
		if (bIsNull || !Value.IsValid() || Value->IsNull())
		{
			Out.bIsNull = true;
			return true;
		}

		return ReadValue(Value, Out);
	}
}

namespace SparkplugJson
{
	const TCHAR* DataTypeName(const ESparkplugDataType DataType)
	{
		const int32 Index = static_cast<int32>(DataType);
		return (Index >= 0 && Index < UE_ARRAY_COUNT(DataTypeNames)) ? DataTypeNames[Index] : DataTypeNames[0];
	}

	TArray<uint8> EncodePayload(const FSparkplugPayload& Payload)
	{
		FString Json;
		Json.Reserve(48 + Payload.Metrics.Num() * 112);

		const TSharedRef<FWriter> Writer = FWriterFactory::Create(&Json);
		Writer->WriteObjectStart();

		// Same omissions as the protobuf encoder: an absent field, not a zero.
		if (Payload.Timestamp != 0)
		{
			Writer->WriteRawJSONValue(KeyTimestamp, LexToString(Payload.Timestamp));
		}
		if (Payload.bHasSeq)
		{
			Writer->WriteRawJSONValue(KeySeq, LexToString(Payload.Seq));
		}
		if (!Payload.UUID.IsEmpty())
		{
			Writer->WriteValue(KeyUuid, Payload.UUID);
		}

		Writer->WriteArrayStart(KeyMetrics);
		for (const FSparkplugMetric& Metric : Payload.Metrics)
		{
			WriteMetric(*Writer, Metric);
		}
		Writer->WriteArrayEnd();

		Writer->WriteObjectEnd();
		Writer->Close();

		const auto Utf8 = StringCast<UTF8CHAR>(*Json, Json.Len());
		return TArray<uint8>(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	bool DecodePayload(const TArray<uint8>& Bytes, FSparkplugPayload& OutPayload)
	{
		OutPayload = FSparkplugPayload();
		if (Bytes.IsEmpty())
		{
			return false;
		}

		const auto Converted = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num());
		const FString Text(Converted.Length(), Converted.Get());

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root, FJsonSerializer::EFlags::StoreNumbersAsStrings)
			|| !Root.IsValid())
		{
			return false;
		}

		if (const TSharedPtr<FJsonValue> Stamp = Root->TryGetField(KeyTimestamp))
		{
			if (!ReadInt64(Stamp, OutPayload.Timestamp)) { return false; }
		}

		// An absent seq is meaningful -- NDEATH has none -- so it is not read as 0.
		const TSharedPtr<FJsonValue> Seq = Root->TryGetField(KeySeq);
		OutPayload.bHasSeq = Seq.IsValid() && !Seq->IsNull();
		if (OutPayload.bHasSeq && !ReadInt64(Seq, OutPayload.Seq))
		{
			return false;
		}

		Root->TryGetStringField(KeyUuid, OutPayload.UUID);

		const TSharedPtr<FJsonValue> Metrics = Root->TryGetField(KeyMetrics);
		if (!Metrics.IsValid() || Metrics->IsNull())
		{
			return true;  // A death certificate for a device carries none.
		}
		if (Metrics->Type != EJson::Array)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Element : Metrics->AsArray())
		{
			if (!Element.IsValid() || Element->Type != EJson::Object)
			{
				return false;
			}
			FSparkplugMetric& Metric = OutPayload.Metrics.AddDefaulted_GetRef();
			if (!ReadMetric(*Element->AsObject(), Metric))
			{
				return false;
			}
		}
		return true;
	}

	bool LooksLikeJson(const TArray<uint8>& Bytes)
	{
		int32 Index = 0;
		// A UTF-8 byte-order mark: 0xEF could never open a protobuf payload either.
		if (Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF)
		{
			Index = 3;
		}
		while (Index < Bytes.Num()
			&& (Bytes[Index] == ' ' || Bytes[Index] == '\t' || Bytes[Index] == '\r' || Bytes[Index] == '\n'))
		{
			++Index;
		}
		return Index < Bytes.Num() && Bytes[Index] == '{';
	}
}

namespace SparkplugCodec
{
	TArray<uint8> EncodePayload(const FSparkplugPayload& Payload, const ESparkplugPayloadEncoding Encoding)
	{
		return Encoding == ESparkplugPayloadEncoding::Json
			? SparkplugJson::EncodePayload(Payload)
			: SparkplugProto::EncodePayload(Payload);
	}

	bool DecodePayload(const TArray<uint8>& Bytes, FSparkplugPayload& OutPayload)
	{
		return SparkplugJson::LooksLikeJson(Bytes)
			? SparkplugJson::DecodePayload(Bytes, OutPayload)
			: SparkplugProto::DecodePayload(Bytes, OutPayload);
	}
}
