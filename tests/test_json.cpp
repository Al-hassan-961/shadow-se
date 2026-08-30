// SPDX-License-Identifier: MIT
// Shadow SE - JSON value/parse/stringify tests.
#include "shadowse/json.hpp"

#include "test_framework.hpp"

#include <string>

using shadowse::json::Value;
using shadowse::json::parse;

TEST(json_scalars) {
    Value n;
    CHECK(n.isNull());
    CHECK_EQ(Value(true).dump(), "true");
    CHECK_EQ(Value(42).dump(), "42");
    CHECK_EQ(Value(-3.5).dump(), "-3.5");
    CHECK_EQ(Value("hi").dump(), "\"hi\"");
}

TEST(json_string_escaping) {
    CHECK_EQ(shadowse::json::escapeString("a\"b\\c"), "a\\\"b\\\\c");
    CHECK_EQ(shadowse::json::escapeString("line\nbreak"), "line\\nbreak");
    CHECK_EQ(Value("x\ty").dump(), "\"x\\ty\"");
}

TEST(json_parse_and_dump) {
    const std::string src =
        "{\"query\":\"onion\",\"count\":2,\"ok\":true,\"tags\":[\"a\",\"b\"],\"empty\":null}";
    Value v;
    std::string err;
    CHECK(parse(src, &v, &err));
    CHECK(v.type() == shadowse::json::Type::Object);
    CHECK_EQ(v.find("query")->asString(), "onion");
    CHECK_EQ(v.find("count")->asNumber(), 2.0);
    CHECK(v.find("ok")->asBool());
    CHECK(v.find("empty")->isNull());
    CHECK_EQ(v.find("tags")->array().size(), 2u);
    CHECK_EQ(v.find("tags")->array()[1].asString(), "b");
}

TEST(json_parse_unicode_escapes) {
    Value v;
    std::string err;
    CHECK(parse("\"caf\\u00e9 \\ud83d\\ude00\"", &v, &err));
    CHECK_EQ(v.asString(), "café \U0001F600");
}

TEST(json_parse_errors) {
    Value v;
    std::string err;
    CHECK(!parse("", &v, &err));
    CHECK(!parse("{\"a\":}", &v, &err));
    CHECK(!parse("[1,2", &v, &err));
    CHECK(!parse("hello", &v, &err));
    CHECK(!parse("{\"a\":1} trailing", &v, &err));
}

TEST(json_object_building) {
    Value obj;
    obj["title"] = "DeepNet";
    obj["score"] = 9.84;
    obj["dark"] = true;
    obj["url"] = "http://x.onion/";
    const std::string out = obj.dump();
    CHECK(out.find("\"title\":\"DeepNet\"") != std::string::npos);
    CHECK(out.find("\"score\":9.84") != std::string::npos);
    CHECK(out.find("\"dark\":true") != std::string::npos);
    // Round-trip through the parser.
    Value back;
    std::string err;
    CHECK(parse(out, &back, &err));
    CHECK_EQ(back.find("title")->asString(), "DeepNet");
}

TEST(json_array_roundtrip) {
    Value arr;
    arr.push(Value(1));
    arr.push(Value(2.5));
    arr.push(Value("three"));
    Value back;
    std::string err;
    CHECK(parse(arr.dump(), &back, &err));
    CHECK(back.type() == shadowse::json::Type::Array);
    CHECK_EQ(back.array().size(), 3u);
    CHECK_EQ(back.array()[2].asString(), "three");
}
