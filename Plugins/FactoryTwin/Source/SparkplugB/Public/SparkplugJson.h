#pragma once

#include "CoreMinimal.h"
#include "SparkplugTypes.h"

/**
 * JSON codec for Sparkplug B payloads, in the shape of the Eclipse Tahu JSON
 * representation:
 *
 *   {"timestamp":1760000000000,"seq":3,
 *    "metrics":[{"name":"state_code","alias":12,"timestamp":1760000000000,
 *                "dataType":"Int32","value":1}]}
 *
 * Exists so a broker or database that cannot decode protobuf can ingest the
 * stream without a decoder sidecar. It is not part of the Sparkplug B
 * specification, which defines protobuf only -- publish it on its own namespace
 * (FSparkplugEdgeNodeConfig::JsonNamespace) so spec consumers never see it.
 *
 * Choices that matter downstream:
 *  - 64-bit integers are written and read as exact number text, never through a
 *    double, so values above 2^53 survive. JavaScript consumers will still round
 *    them; that is the consumer's limit, not the wire's.
 *  - Metrics always carry their name, as the protobuf DDATA does: a consumer
 *    with no birth certificate in hand must still know what it is reading.
 *  - "dataType" is the Tahu type name ("Float", "UInt64"). Decoding also
 *    accepts the numeric code.
 *  - Bytes and File values are base64. Non-finite floats, which JSON cannot
 *    express as numbers, are the strings "NaN", "Infinity" and "-Infinity".
 *  - DataSet and Template are unsupported, as in the protobuf codec, and go out
 *    as null.
 */
namespace SparkplugJson
{
	/** Encodes a payload as UTF-8 JSON, ready to hand to MQTT publish. */
	SPARKPLUGB_API TArray<uint8> EncodePayload(const FSparkplugPayload& Payload);

	/** Decodes UTF-8 JSON. Returns false on anything that is not a payload object. */
	SPARKPLUGB_API bool DecodePayload(const TArray<uint8>& Bytes, FSparkplugPayload& OutPayload);

	/**
	 * True when the bytes are a JSON object rather than protobuf.
	 *
	 * Safe as a discriminator: '{' is 0x7B, which as the first byte of a
	 * protobuf message would be field 15 with wire type 3 (a deprecated group).
	 * The Sparkplug payload has no field 15 and no groups, so no valid protobuf
	 * payload can start with it.
	 */
	SPARKPLUGB_API bool LooksLikeJson(const TArray<uint8>& Bytes);

	/** The Tahu name for a datatype, e.g. "Int32". */
	SPARKPLUGB_API const TCHAR* DataTypeName(ESparkplugDataType DataType);
}

/** Encoding-agnostic entry points, for code that should not care which is on the wire. */
namespace SparkplugCodec
{
	/**
	 * Encodes in one concrete encoding. Both is a publishing decision, not an
	 * encoding, and is treated as Protobuf here.
	 */
	SPARKPLUGB_API TArray<uint8> EncodePayload(
		const FSparkplugPayload& Payload, ESparkplugPayloadEncoding Encoding);

	/** Decodes either encoding, chosen by looking at the bytes rather than the topic. */
	SPARKPLUGB_API bool DecodePayload(const TArray<uint8>& Bytes, FSparkplugPayload& OutPayload);
}
