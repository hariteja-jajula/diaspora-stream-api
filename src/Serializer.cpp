/*
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "JsonUtil.hpp"
#include "diaspora/Exception.hpp"
#include "diaspora/Serializer.hpp"
#include "PimplUtil.hpp"
#include "DefaultSerializer.hpp"
#include "SchemaSerializer.hpp"
#include "RawSerializer.hpp"

namespace diaspora {

DIASPORA_REGISTER_SERIALIZER(diaspora, default, DefaultSerializer);
DIASPORA_REGISTER_SERIALIZER(diaspora, schema, SchemaSerializer);
DIASPORA_REGISTER_SERIALIZER(diaspora, raw, RawSerializer);

Serializer::Serializer()
: self(std::make_shared<DefaultSerializer>()) {}

Serializer Serializer::FromMetadata(const Metadata& metadata) {
    const auto& json = metadata.json();
    if(!json.is_object()) {
        throw Exception(
                "Cannot create Serializer from Metadata: "
                "invalid Metadata (expected JSON object)");
    }
    if(!json.contains("type")) {
        return Serializer{};
    }
    auto& type = json["type"];
    if(!type.is_string()) {
        throw Exception(
                "Cannot create Serializer from Metadata: "
                "invalid \"type\" field in Metadata (expected string)");
    }
    const auto& type_str = type.get_ref<const std::string&>();
    std::shared_ptr<SerializerInterface> s = SerializerFactory::create(type_str, metadata);
    return Serializer(std::move(s));
}

}
