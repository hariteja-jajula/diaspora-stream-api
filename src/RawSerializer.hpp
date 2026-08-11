/*
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef DIASPORA_RAW_SERIALIZER_H
#define DIASPORA_RAW_SERIALIZER_H

#include "JsonUtil.hpp"
#include "diaspora/Serializer.hpp"
#include "diaspora/Json.hpp"

namespace diaspora {

/* Asymmetric "raw" serializer.
 *
 * Motivation: a producer that already holds a fully-formed JSON *text* (e.g. the
 * Darshan->Mofka connector's snprintf'd envelope) can construct its Metadata with
 * parse=false, which stores that text verbatim as a JSON string node. This
 * serializer then writes those bytes straight to the wire WITHOUT re-dumping them,
 * eliminating the redundant parse (in the producer push) + dump (in the default
 * serializer) round-trip -- the text is neither parsed nor re-serialized on the
 * producer side.
 *
 * It is asymmetric on purpose: deserialize() still parses the wire text into a real
 * JSON object, so consumers (FlowCept, the reconstructor) receive a structured
 * object exactly as they do with the default serializer. The single parse now lives
 * only on the consumer/broker side, off the workload's measured critical path.
 *
 * Fallback: if the Metadata was NOT built from a string (e.g. an object built the
 * normal way, or the connector's one-shot metadata event pushed with parse=true),
 * serialize() falls back to dump() so non-raw pushes still produce valid JSON.
 * The wire framing ([size_t length][text]) is identical to DefaultSerializer, so
 * the two are interchangeable on the deserialize side.
 */
class RawSerializer : public SerializerInterface {

    public:

    void serialize(Archive& archive, const Metadata& metadata) const override {
        const auto& json = metadata.json();
        if(json.is_string()) {
            /* Raw path: the string node holds already-formed JSON text; emit its
             * bytes verbatim -- no dump(), no escaping. */
            const std::string& str = json.get_ref<const std::string&>();
            size_t s = str.size();
            archive.write(&s, sizeof(s));
            archive.write(str.data(), s);
        } else {
            /* Fallback: object (or other) metadata -- serialize normally. */
            const auto str = metadata.dump();
            size_t s = str.size();
            archive.write(&s, sizeof(s));
            archive.write(str.data(), s);
        }
    }

    void deserialize(Archive& archive, Metadata& metadata) const override {
        size_t s = 0;
        archive.read(&s, sizeof(s));
        std::string str;
        str.resize(s);
        archive.read(const_cast<char*>(str.data()), s);
        try {
            metadata.json() = nlohmann::json::parse(str);
        } catch(const std::exception& ex) {
            throw Exception{std::string{"Could not deserialize Serializer metadata: "} + ex.what()};
        }
    }

    Metadata metadata() const override {
        return Metadata{"{\"type\":\"raw\"}"};
    }

    static std::shared_ptr<SerializerInterface> create(const Metadata& metadata) {
        (void)metadata;
        return std::make_shared<RawSerializer>();
    }
};

}

#endif
