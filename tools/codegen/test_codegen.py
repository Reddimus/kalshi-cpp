"""Tests that tools/codegen stops on spec features it cannot map instead of dropping them.

    python3 -m unittest discover -s tools/codegen   # `make lint` runs this

Each test edits a small spec that the generator accepts, then expects SystemExit.
"""

from __future__ import annotations

import copy
import unittest

from generate import Generator
from common import load_contract_yaml, emit_enum, emit_enum_adapter, emit_enum_parser
from ws import WsGenerator


def ref(name: str) -> dict:
    return {"$ref": f"#/components/schemas/{name}"}


def json_content(schema: dict) -> dict:
    return {"content": {"application/json": {"schema": schema}}}


BASE = {
    "openapi": "3.0.0",
    "paths": {
        "/things": {
            "post": {
                "operationId": "CreateThing",
                "tags": ["things"],
                "requestBody": {"required": True, **json_content(ref("Thing"))},
                "responses": {"201": json_content(ref("Thing"))},
            },
        },
        "/things/{id}": {
            "parameters": [{"name": "id", "in": "path", "required": True, "schema": {"type": "string"}}],
            "get": {
                "operationId": "GetThing",
                "tags": ["things"],
                "parameters": [
                    {"name": "limit", "in": "query", "schema": {"type": "integer"}},
                    {"name": "ids", "in": "query", "style": "form", "explode": True,
                     "schema": {"type": "array", "items": {"type": "string"}}},
                ],
                "responses": {"200": json_content(ref("GetThingResponse")), "404": {"description": "missing"}},
            },
            "put": {
                "operationId": "ResetThing",
                "tags": ["things"],
                "requestBody": json_content(ref("Empty")),
                "responses": {"204": {"description": "reset"}},
            },
        },
    },
    "components": {
        "schemas": {
            "Thing": {
                "type": "object",
                "required": ["name"],
                "properties": {
                    "name": {"type": "string"},
                    "size": {"type": "string", "enum": ["small", "large"]},
                    "detail": {"type": "object", "properties": {"note": {"type": "string"}}},
                },
            },
            "GetThingResponse": {"type": "object", "required": ["thing"], "properties": {"thing": ref("Thing")}},
            "Empty": {"type": "object"},
        },
    },
}


def generate(spec: dict, enum_pins: dict[str, str] | None = None) -> Generator:
    # The real pins name Kalshi's schemas, which these small specs don't have.
    gen = Generator(spec, enum_pins=enum_pins or {})
    gen.run()
    return gen


class SupportedSpec(unittest.TestCase):
    def test_zero_integer_enum_documentation_is_not_an_empty_string(self) -> None:
        from common import EnumType
        text = emit_enum(EnumType("Interval", [0, 100], ""))
        self.assertIn("V0 = 0, ///< `0`", text)
        self.assertNotIn("empty string", text)

    def test_contract_boolean_spelling_preserves_market_sides(self) -> None:
        schema = load_contract_yaml("enum: [yes, no, on, off]\nnullable: true\ndeprecated: false\n")
        self.assertEqual(schema["enum"], ["yes", "no", "on", "off"])
        self.assertIs(schema["nullable"], True)
        self.assertIs(schema["deprecated"], False)

    def test_base_spec_generates(self) -> None:
        ops = {op.method_name: op for op in generate(copy.deepcopy(BASE)).operations}
        self.assertEqual(ops["create_thing"].body_type, "Thing")
        self.assertEqual(ops["get_thing"].response_type, "Thing")  # unwrapped from GetThingResponse
        self.assertTrue(ops["reset_thing"].empty_body)
        self.assertIsNone(ops["reset_thing"].response_type)

    def test_pinned_enum_name_does_not_move_when_a_schema_comes_first(self) -> None:
        spec = copy.deepcopy(BASE)
        first = {"AThing": {"type": "object", "properties": {"size": {"type": "string", "enum": ["s", "m"]}}}}
        spec["components"]["schemas"] = {**first, **spec["components"]["schemas"]}
        unpinned = generate(copy.deepcopy(spec))
        self.assertEqual({m.json_name: m for m in unpinned.structs["AThing"].members}["size"].cpp_type,
                         "std::optional<Size>")  # spec order hands out the name
        gen = generate(spec, {"ThingSize": "Size"})
        thing = {m.json_name: m for m in gen.structs["Thing"].members}
        other = {m.json_name: m for m in gen.structs["AThing"].members}
        self.assertEqual(thing["size"].cpp_type, "std::optional<Size>")
        self.assertEqual(other["size"].cpp_type, "std::optional<AThingSize>")
        self.assertEqual(gen.enums["Size"].values, ["small", "large"])

    def test_array_alias_follows_its_scalar_alias(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["ZScalar"] = {"type": "string"}
        spec["components"]["schemas"]["AValues"] = {"type": "array", "items": ref("ZScalar")}
        header = generate(spec).models_header()
        self.assertLess(header.index("using ZScalar ="), header.index("using AValues ="))

    def test_read_only_outside_request_bodies_is_allowed(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["GetThingResponse"]["properties"]["etag"] = {"type": "string", "readOnly": True}
        generate(spec)

    def test_optional_read_only_in_request_body_is_allowed(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["Thing"]["properties"]["id"] = {"type": "string", "readOnly": True}
        members = {m.json_name: m for m in generate(spec).structs["Thing"].members}
        self.assertEqual(members["id"].cpp_type, "std::optional<std::string>")

    def test_scalar_query_without_explode_is_allowed(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["paths"]["/things/{id}"]["get"]["parameters"][0]["explode"] = False  # limit=5 either way
        generate(spec)

    def test_all_of_with_notes_and_vendor_extensions_is_allowed(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["GetThingResponse"]["properties"]["other"] = {
            "allOf": [ref("Thing")], "nullable": True, "description": "Another thing.", "x-go-type": "Thing"}
        members = {m.json_name: m for m in generate(spec).structs["GetThingResponse"].members}
        self.assertEqual(members["other"].cpp_type, "std::optional<Thing>")

    def test_all_of_with_validation_keywords_is_allowed(self) -> None:
        # Kalshi's Perps spec puts maxLength and pattern here. A response is used because
        # validate() cannot check `pattern`, so a request with one stops the run.
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["Code"] = {"type": "string"}
        spec["components"]["schemas"]["GetThingResponse"]["properties"]["code"] = {
            "allOf": [ref("Code")], "maxLength": 8, "pattern": "^[A-Z]+$"}
        members = {m.json_name: m for m in generate(spec).structs["GetThingResponse"].members}
        self.assertEqual(members["code"].cpp_type, "std::optional<Code>")

    def test_nullable_component_makes_members_optional(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["MaybeName"] = {"type": "string", "nullable": True}
        thing = spec["components"]["schemas"]["Thing"]
        thing["properties"]["alias"] = ref("MaybeName")
        thing["required"].append("alias")
        members = {m.json_name: m for m in generate(spec).structs["Thing"].members}
        self.assertEqual(members["alias"].cpp_type, "std::optional<MaybeName>")

    def test_required_property_not_constraints_are_validated(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["Slippage"] = {
            "type": "object", "required": ["unit", "value"],
            "properties": {
                "unit": {"type": "string", "enum": ["bps", "cents"]},
                "value": {"type": "integer", "minimum": 0, "maximum": 9999},
            },
            "not": {"required": ["unit", "value"], "properties": {
                "unit": {"enum": ["cents"]}, "value": {"minimum": 100}}},
        }
        spec["components"]["schemas"]["Thing"]["properties"]["slippage"] = ref("Slippage")
        header = generate(spec).validate_header()
        self.assertIn("value.value >= 100", header)
        self.assertIn("value.value < 0", header)
        self.assertIn("value.value > 9999", header)
        self.assertIn("::Cents", header)

    def test_object_composition_keeps_base_and_extension_fields(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["ExtendedThing"] = {"allOf": [ref("Thing"), {
            "type": "object", "required": ["extra"], "properties": {"extra": {"type": "integer"}}}]}
        members = {m.json_name: m for m in generate(spec).structs["ExtendedThing"].members}
        self.assertTrue(members["name"].required)
        self.assertTrue(members["extra"].required)
        self.assertEqual(members["extra"].base_kind, "scalar")

    def test_inline_object_response_gets_its_own_type(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["paths"]["/things"]["post"]["responses"]["201"] = json_content({
            "type": "object", "required": ["status"], "properties": {"status": {"type": "string"}}})
        gen = generate(spec)
        op = next(op for op in gen.operations if op.operation_id == "CreateThing")
        self.assertEqual(op.response_type, "CreateThingResponse")
        self.assertIsNone(op.unwrap_member)
        self.assertTrue(gen.structs[op.response_type].members[0].required)

    def test_same_body_under_two_success_codes_is_allowed(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["paths"]["/things"]["post"]["responses"]["200"] = json_content(ref("Thing"))
        ops = {op.method_name: op for op in generate(spec).operations}
        self.assertEqual(ops["create_thing"].response_type, "Thing")

    def test_all_of_extension_may_require_a_base_property(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["SizedThing"] = {"allOf": [ref("Thing"), {"type": "object", "required": ["size"]}]}
        members = {m.json_name: m for m in generate(spec).structs["SizedThing"].members}
        self.assertTrue(members["size"].required)

    def test_all_of_parts_may_annotate_a_referenced_field(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["Owned"] = {"allOf": [
            {"type": "object", "properties": {"thing": ref("Thing")}},
            {"type": "object", "properties": {"thing": {"description": "The owned thing."}}}]}
        members = {m.json_name: m for m in generate(spec).structs["Owned"].members}
        self.assertEqual(members["thing"].cpp_type, "std::optional<Thing>")

    def test_fixed_point_arrays_in_request_bodies_are_validated(self) -> None:
        spec = copy.deepcopy(BASE)
        schemas = spec["components"]["schemas"]
        schemas["FixedPointDollars"] = {"type": "string"}
        schemas["FixedPointQuote"] = {"type": "array", "items": ref("FixedPointDollars"), "minItems": 2, "maxItems": 2}
        schemas["Quote"] = {"type": "object", "required": ["prices"], "properties": {
            "prices": {"type": "array", "items": ref("FixedPointDollars")},
            "limits": {"type": "array", "items": {"allOf": [ref("FixedPointDollars")]}},
            "spread": ref("FixedPointQuote"),
            "legs": {"type": "array", "items": {"type": "object", "properties": {
                "marks": {"type": "array", "items": ref("FixedPointDollars")}}}},
        }}
        spec["paths"]["/quotes"] = {"post": {"operationId": "CreateQuote", "tags": ["things"],
                                             "requestBody": {"required": True, **json_content(ref("Quote"))},
                                             "responses": {"204": {"description": "created"}}}}
        gen = generate(spec)
        header = gen.validate_header()
        self.assertIn("for (const FixedPointDollars& item : value.prices) {\n\t\tif (!FixedPoint::parse(item)) {", header)
        self.assertIn('invalid("Quote", "prices", "has an item that is not a fixed-point decimal")', header)
        self.assertIn("for (const FixedPointDollars& item : *value.limits)", header)
        self.assertIn("for (const FixedPointDollars& item : *value.spread)", header)  # std::array alias
        self.assertNotIn("FixedPoint::parse(*value.spread)", header)
        self.assertIn("for (const FixedPointDollars& item : *value.marks)", header)
        self.assertIn("for (const QuoteLegsItem& item : *value.legs)", header)
        quote = next(op for op in gen.operations if op.operation_id == "CreateQuote")
        self.assertIn("detail::validate(body)", gen.operations_source("things", [quote]))
        self.assertFalse(gen.default_is_invalid("Quote"))  # an empty list holds no bad strings

    def test_nested_fixed_point_containers_outside_request_bodies_are_allowed(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["FixedPointDollars"] = {"type": "string"}
        spec["components"]["schemas"]["Book"] = {"type": "object", "properties": {
            "levels": {"type": "array", "items": {"type": "array", "items": ref("FixedPointDollars")}}}}
        members = {m.json_name: m for m in generate(spec).structs["Book"].members}
        self.assertEqual(members["levels"].cpp_type, "std::optional<std::vector<std::vector<FixedPointDollars>>>")

    def test_nested_structs_that_need_validation_outside_request_bodies_are_allowed(self) -> None:
        spec = copy.deepcopy(BASE)  # Thing requires a name, so it needs validation
        spec["components"]["schemas"]["GetThingResponse"]["properties"].update({
            "by_name": {"type": "object", "additionalProperties": ref("Thing")},
            "pages": {"type": "array", "items": {"type": "array", "items": ref("Thing")}},
        })
        members = {m.json_name: m for m in generate(spec).structs["GetThingResponse"].members}
        self.assertEqual(members["by_name"].cpp_type, "std::optional<std::map<std::string, Thing>>")
        self.assertEqual(members["pages"].cpp_type, "std::optional<std::vector<std::vector<Thing>>>")

    def test_nested_structs_without_validation_in_request_bodies_are_allowed(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["Note"] = {"type": "object", "properties": {"text": {"type": "string"}}}
        spec["components"]["schemas"]["Thing"]["properties"].update({
            "notes_by_author": {"type": "object", "additionalProperties": ref("Note")},
            "note_pages": {"type": "array", "items": {"type": "array", "items": ref("Note")}},
        })
        members = {m.json_name: m for m in generate(spec).structs["Thing"].members}
        self.assertEqual(members["notes_by_author"].nested_element, "Note")
        self.assertEqual(members["note_pages"].nested_element, "Note")

    def test_optional_body_without_required_fields_keeps_its_parameter(self) -> None:
        spec = copy.deepcopy(BASE)  # Kalshi's CreateSubaccount body is like this
        spec["components"]["schemas"]["Note"] = {"type": "object", "properties": {"text": {"type": "string"}}}
        for body in ({"required": False, **json_content(ref("Note"))}, json_content(ref("Note"))):
            with self.subTest(required=body.get("required", "absent")):
                spec["paths"]["/things"]["post"]["requestBody"] = body
                gen = generate(copy.deepcopy(spec))
                op = next(op for op in gen.operations if op.operation_id == "CreateThing")
                self.assertEqual(gen.signature(op, qualified=False), "Result<Thing> create_thing(const Note& body)")

    def test_ref_to_an_empty_object_component_is_raw_json(self) -> None:
        spec = copy.deepcopy(BASE)
        thing = spec["components"]["schemas"]["Thing"]
        thing["properties"].update({
            "inline_meta": {"type": "object"},
            "meta": ref("Empty"),
            "metas": {"type": "array", "items": ref("Empty")},
            "meta_by_key": {"type": "object", "additionalProperties": ref("Empty")},
        })
        thing["required"].append("meta")
        gen = generate(spec)
        members = {m.json_name: m for m in gen.structs["Thing"].members}
        self.assertEqual(members["meta"].cpp_type, "RawJson")  # the same as an inline empty object
        self.assertEqual(members["inline_meta"].cpp_type, "std::optional<RawJson>")
        self.assertEqual(members["metas"].cpp_type, "std::optional<std::vector<RawJson>>")
        self.assertEqual(members["meta_by_key"].cpp_type, "std::optional<std::map<std::string, RawJson>>")
        self.assertNotIn("Empty", gen.models_header())
        self.assertTrue(next(op for op in gen.operations if op.operation_id == "ResetThing").empty_body)

    def test_schema_bounds_and_query_params_are_validated(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["paths"]["/things/{id}"]["get"]["parameters"][0]["schema"] = {
            "type": "integer", "minimum": 1, "maximum": 200
        }
        spec["components"]["schemas"]["Thing"]["properties"]["count"] = {
            "type": "integer", "minimum": 1
        }
        spec["components"]["schemas"]["Thing"]["required"].append("count")
        gen = generate(spec)
        header = gen.validate_header()
        self.assertIn("validate(const GetThingParams& value)", header)
        self.assertIn("*value.limit < 1 || *value.limit > 200", header)
        self.assertIn("value.count < 1", header)
        get_thing = next(op for op in gen.operations if op.operation_id == "GetThing")
        self.assertIn("detail::validate(params)", gen.operations_source("things", [get_thing]))
        self.assertTrue(gen.default_is_invalid("Thing"))
        self.assertFalse(gen.default_is_invalid("GetThingParams"))

    def test_integer_enum_query_parameters_are_validated(self) -> None:
        spec = copy.deepcopy(BASE)  # Kalshi's candlestick and forecast period_interval parameters (#135)
        spec["paths"]["/things/{id}"]["get"]["parameters"].extend([
            {"name": "period_interval", "in": "query", "schema": {"type": "integer", "enum": [1, 60, 1440]}},
            {"name": "forecast_interval", "in": "query", "required": True,
             "schema": {"type": "integer", "enum": [0, 1, 60, 1440]}},
        ])
        gen = generate(spec)
        members = {m.json_name: m for m in gen.structs["GetThingParams"].members}
        self.assertEqual(members["period_interval"].cpp_type, "std::optional<std::int64_t>")
        self.assertEqual(members["period_interval"].one_of, [1, 60, 1440])
        header = gen.validate_header()
        self.assertIn("value.period_interval && (*value.period_interval != 1 && *value.period_interval != 60 && "
                      "*value.period_interval != 1440)", header)
        self.assertIn("value.forecast_interval != 0 && value.forecast_interval != 1", header)
        self.assertFalse(gen.default_is_invalid("GetThingParams"))  # 0 is one of the values

    def test_integer_enum_and_go_validator_oneof_intersect(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["paths"]["/things/{id}"]["get"]["parameters"].append(
            {"name": "period", "in": "query", "schema": {"type": "integer", "enum": [1, 60, 1440]},
             "x-oapi-codegen-extra-tags": {"validate": "omitempty,oneof=5 60 1440"}})
        members = {m.json_name: m for m in generate(spec).structs["GetThingParams"].members}
        self.assertEqual(members["period"].one_of, [60, 1440])

    def test_integer_array_item_bounds_are_validated(self) -> None:
        spec = copy.deepcopy(BASE)  # Kalshi's forecast percentiles parameter (#135)
        spec["paths"]["/things/{id}"]["get"]["parameters"].extend([
            {"name": "percentiles", "in": "query", "required": True, "schema": {
                "type": "array", "maxItems": 10, "items": {"type": "integer", "minimum": 0, "maximum": 9999}}},
            {"name": "levels", "in": "query", "schema": {
                "type": "array", "items": {"type": "integer", "minimum": 1}}},
        ])
        gen = generate(spec)
        header = gen.validate_header()
        self.assertIn("for (const std::int64_t item : value.percentiles) {\n\t\tif (item < 0 || item > 9999)", header)
        self.assertIn("for (const std::int64_t item : value.levels) {\n\t\tif (item < 1)", header)
        self.assertIn("has an item outside its allowed range", header)
        self.assertFalse(gen.default_is_invalid("GetThingParams"))  # no items to check

    def test_wrapped_bounds_keep_the_strictest_value(self) -> None:
        spec = copy.deepcopy(BASE)  # a bound beside allOf must not weaken the one inside it
        spec["paths"]["/things/{id}"]["get"]["parameters"].append(
            {"name": "levels", "in": "query", "schema": {"type": "array", "items": {
                "allOf": [{"type": "integer", "minimum": 5, "maximum": 10}], "minimum": 0, "maximum": 20}}})
        spec["components"]["schemas"]["Thing"]["properties"].update({
            "count": {"allOf": [{"type": "integer", "minimum": 5}], "minimum": 0, "maximum": 20},
            "ratio": {"allOf": [{"type": "number", "maximum": 1}], "minimum": 0, "maximum": 2},
            "codes": {"type": "array", "items": {"allOf": [{"type": "string", "maxLength": 4}], "maxLength": 8,
                                                 "minLength": 2}},
        })
        header = generate(spec).validate_header()
        self.assertIn("if (item < 5 || item > 10)", header)
        self.assertIn("*value.count < 5 || *value.count > 20", header)
        self.assertIn("!(*value.ratio >= 0.0 && *value.ratio <= 1.0)", header)
        self.assertIn("item.size() < 2U || item.size() > 4U", header)

    def test_unchecked_keywords_outside_requests_are_allowed(self) -> None:
        spec = copy.deepcopy(BASE)  # validate() only checks requests, so responses may use any keyword
        spec["components"]["schemas"]["GetThingResponse"]["properties"].update({
            "code": {"type": "string", "pattern": "^[A-Z]+$"},
            "scores": {"type": "array", "items": {"type": "number", "minimum": 0}},
            "label": ref("Label"),
        })
        spec["components"]["schemas"]["Label"] = {"type": "string", "pattern": "^[A-Z]+$"}
        generate(spec).validate_header()

    def test_go_validator_tags_are_validated(self) -> None:
        spec = copy.deepcopy(BASE)  # Kalshi's create-market requests and query parameters (#131)
        tag = "x-oapi-codegen-extra-tags"
        spec["paths"]["/things/{id}"]["get"]["parameters"].extend([
            {"name": "day", "in": "query", "schema": {"type": "string"}, tag: {"validate": "omitempty,datetime=2006-01-02"}},
            {"name": "period", "in": "query", "required": True, "schema": {"type": "integer", "enum": [1, 60]},
             tag: {"validate": "required,oneof=1 60"}},
        ])
        spec["paths"]["/things/{id}"]["get"]["parameters"][1][tag] = {"validate": "omitempty,max=4,dive,max=200"}
        thing = spec["components"]["schemas"]["Thing"]
        thing["properties"].update({
            "latitude": {"type": "number", tag: {"validate": "required,gte=-90,lte=90"}},
            "code": {"type": "string", tag: {"validate": "required,alpha,len=3"}},
            "number": {"type": "string", tag: {"validate": "required,number,max=4"}},
            "delta": {"type": "integer", tag: {"validate": "required,ne=0"}},
            "minutes": {"type": "integer", tag: {"validate": "omitempty,min=1"}},
            "size": {"type": "string", "enum": ["small", "large"], tag: {"validate": "omitempty,oneof=large small"}},
            "shard": {"allOf": [{"type": "integer", "minimum": -1}], "x-go-type-skip-optional-pointer": True,
                      tag: {"validate": "gte=0"}},
        })
        thing["required"].extend(["latitude", "code", "number", "delta"])
        gen = generate(spec)
        header = gen.validate_header()
        self.assertIn("!(value.latitude >= -90.0 && value.latitude <= 90.0)", header)
        self.assertIn("value.code.size() < 3U || value.code.size() > 3U", header)
        self.assertIn("!is_in_charset(value.code, Charset::Letters)", header)
        self.assertIn("!is_in_charset(value.number, Charset::Digits)", header)
        self.assertIn("value.number.size() > 4U", header)
        self.assertIn("value.delta == 0", header)
        self.assertIn("value.minutes && (*value.minutes < 1)", header)
        self.assertIn("value.shard && (*value.shard < 0)", header)
        self.assertIn("value.day && !is_valid_date(*value.day)", header)
        self.assertIn("value.period != 1 && value.period != 60", header)
        self.assertIn("value.ids.size() > 4U", header)
        self.assertIn("item.size() > 200U", header)
        self.assertTrue(gen.default_is_invalid("GetThingParams"))  # period 0 is not 1 or 60

    def test_go_validator_tag_alone_makes_a_body_validated(self) -> None:
        spec = copy.deepcopy(BASE)  # CreateSubaccountRequest had no validate() at all
        spec["components"]["schemas"]["Thing"] = {"type": "object", "properties": {
            "index": {"type": "integer", "x-oapi-codegen-extra-tags": {"validate": "gte=0"}}}}
        gen = generate(spec)
        self.assertIn("validate(const Thing& value)", gen.validate_header())
        create = next(op for op in gen.operations if op.operation_id == "CreateThing")
        self.assertIn("detail::validate(body)", gen.operations_source("things", [create]))
        self.assertFalse(gen.default_is_invalid("Thing"))


class Rejected(unittest.TestCase):
    def setUp(self) -> None:
        self.spec = copy.deepcopy(BASE)
        self.schemas = self.spec["components"]["schemas"]
        self.get = self.spec["paths"]["/things/{id}"]["get"]
        self.post = self.spec["paths"]["/things"]["post"]

    def stops(self, message: str) -> None:
        with self.assertRaises(SystemExit) as caught:
            generate(self.spec)
        self.assertIn(message, str(caught.exception.code))

    # ----- go-validator tags -----------------------------------------------

    def tag(self, prop: dict, rule: str) -> None:
        prop["x-oapi-codegen-extra-tags"] = {"validate": rule}

    def test_unknown_go_validator_rule(self) -> None:
        self.tag(self.schemas["Thing"]["properties"]["name"], "required,email")
        self.stops("go-validator rule `email` is not supported")

    def test_go_validator_rule_on_the_wrong_type(self) -> None:
        self.tag(self.schemas["Thing"]["properties"]["name"], "required,ne=0")
        self.stops("go-validator rule `ne` is not supported on std::string")

    def test_go_validator_datetime_layout(self) -> None:
        self.tag(self.schemas["Thing"]["properties"]["name"], "required,datetime=15:04")
        self.stops("only the datetime layout 2006-01-02 is supported")

    def test_go_validator_oneof_narrower_than_the_enum(self) -> None:
        self.tag(self.schemas["Thing"]["properties"]["size"], "omitempty,oneof=small")
        self.stops("`oneof=small` differs from the values of")

    def test_go_validator_dive_on_a_scalar(self) -> None:
        self.tag(self.schemas["Thing"]["properties"]["name"], "required,dive,max=2")
        self.stops("`dive` is supported once, on an array")

    def test_go_validator_required_on_an_optional_string(self) -> None:
        self.schemas["Thing"]["properties"]["note"] = {"type": "string"}
        self.tag(self.schemas["Thing"]["properties"]["note"], "required")
        self.stops("`required` in x-oapi-codegen-extra-tags on an optional member")

    def test_go_validator_required_on_an_optional_array(self) -> None:
        self.schemas["Thing"]["properties"]["tags"] = {"type": "array", "items": {"type": "string"}}
        self.tag(self.schemas["Thing"]["properties"]["tags"], "required,dive")
        self.stops("`required` in x-oapi-codegen-extra-tags on an optional member")

    def test_go_validator_required_after_dive(self) -> None:
        self.schemas["Thing"]["properties"]["tags"] = {"type": "array", "items": {"type": "string"}}
        self.tag(self.schemas["Thing"]["properties"]["tags"], "omitempty,dive,required")
        self.stops("go-validator rule `required` after `dive` is not supported here")

    def test_go_validator_omitempty_after_dive(self) -> None:
        self.schemas["Thing"]["properties"]["tags"] = {"type": "array", "items": {"type": "string"}}
        self.tag(self.schemas["Thing"]["properties"]["tags"], "omitempty,dive,omitempty,min=1")
        self.stops("go-validator rule `omitempty` after `dive` is not supported here")

    def test_go_validator_omitempty_zero_on_a_required_member(self) -> None:
        self.schemas["Thing"]["properties"]["count"] = {"type": "integer"}
        self.schemas["Thing"]["required"].append("count")
        self.tag(self.schemas["Thing"]["properties"]["count"], "omitempty,min=1")
        self.stops("an omitempty rule that rejects the zero value needs an optional pointer")

    def test_go_validator_omitempty_zero_without_a_pointer(self) -> None:
        self.schemas["Thing"]["properties"]["count"] = {"type": "integer", "x-go-type-skip-optional-pointer": True}
        self.tag(self.schemas["Thing"]["properties"]["count"], "omitempty,min=1")
        self.stops("an omitempty rule that rejects the zero value needs an optional pointer")

    def test_go_validator_tag_on_array_items(self) -> None:
        self.schemas["Thing"]["properties"]["tags"] = {"type": "array", "items": {"type": "string"}}
        self.tag(self.schemas["Thing"]["properties"]["tags"]["items"], "max=3")
        self.stops("on array items is not supported")

    def test_go_validator_item_rule_on_integer_items(self) -> None:
        self.schemas["Thing"]["properties"]["counts"] = {"type": "array", "items": {"type": "integer"}}
        self.tag(self.schemas["Thing"]["properties"]["counts"], "omitempty,dive,min=1")
        self.stops("go-validator rule `min` after `dive` is not supported here")

    def test_go_validator_string_rule_on_an_integer(self) -> None:
        self.schemas["Thing"]["properties"]["count"] = {"type": "integer"}
        self.tag(self.schemas["Thing"]["properties"]["count"], "omitempty,alpha")
        self.stops("go-validator rule `alpha` is not supported on std::optional<std::int64_t>")

    def test_go_validator_tag_on_wrapped_array_items(self) -> None:
        self.schemas["Thing"]["properties"]["tags"] = {"type": "array", "items": {"allOf": [{"type": "string"}]}}
        self.tag(self.schemas["Thing"]["properties"]["tags"]["items"], "max=3")
        self.stops("on array items is not supported")

    def test_go_validator_oneof_rules_with_no_common_value(self) -> None:
        self.schemas["Thing"]["properties"]["count"] = {"type": "integer"}
        self.tag(self.schemas["Thing"]["properties"]["count"], "omitempty,oneof=1 2,oneof=3 4")
        self.stops("`oneof` rules leave no allowed value")

    def test_go_validator_tag_with_other_keys(self) -> None:
        self.schemas["Thing"]["properties"]["name"]["x-oapi-codegen-extra-tags"] = {"validate": "required", "db": "x"}
        self.stops("must hold only a `validate` string")

    def test_go_validator_tag_on_a_parameter_and_its_schema(self) -> None:
        self.get["parameters"][0]["x-oapi-codegen-extra-tags"] = {"validate": "omitempty,min=1"}
        self.get["parameters"][0]["schema"]["x-oapi-codegen-extra-tags"] = {"validate": "omitempty,min=1"}
        self.stops("on both the parameter and its schema")

    # ----- operations ------------------------------------------------------

    def test_patch_operation(self) -> None:
        self.spec["paths"]["/things/{id}"]["patch"] = dict(self.get, operationId="PatchThing")
        self.stops("PATCH /things/{id}: HttpMethod has no PATCH")

    def test_path_item_ref(self) -> None:
        self.spec["paths"]["/other"] = {"$ref": "other.yaml#/paths/other"}
        self.stops("/other: path item $ref")

    def test_missing_operation_id(self) -> None:
        del self.get["operationId"]
        self.stops("GET /things/{id}: needs an operationId")

    def test_header_parameter(self) -> None:
        self.get["parameters"].append({"name": "X-Trace", "in": "header", "schema": {"type": "string"}})
        self.stops("`in: header` is not supported")

    def test_cookie_parameter(self) -> None:
        self.get["parameters"].append({"name": "session", "in": "cookie", "schema": {"type": "string"}})
        self.stops("`in: cookie` is not supported")

    def test_content_parameter(self) -> None:
        self.get["parameters"].append({"name": "filter", "in": "query", "content": {"application/json": {}}})
        self.stops("`content` parameters are not supported")

    def test_unexploded_array_query(self) -> None:
        self.get["parameters"][1]["explode"] = False
        self.stops("only style: form with explode: true")

    def test_object_query(self) -> None:
        self.get["parameters"].append({"name": "where", "in": "query", "schema": ref("Thing")})
        self.stops("detail::Query cannot send")

    def test_pipe_delimited_array_query(self) -> None:
        self.get["parameters"][1]["style"] = "pipeDelimited"
        self.stops("only style: form with explode: true is supported for arrays")

    def test_parameter_ref_outside_component_parameters(self) -> None:
        self.get["parameters"].append({"$ref": "#/components/schemas/Limit"})
        self.stops("#/components/schemas/Limit is not a component parameter")

    def test_duplicate_parameter(self) -> None:
        self.get["parameters"].append({"name": "id", "in": "path", "required": True, "schema": {"type": "string"}})
        self.stops("GetThing: a parameter is declared twice")

    def test_duplicate_method_name(self) -> None:
        self.spec["paths"]["/v2/things/{id}"] = copy.deepcopy(self.spec["paths"]["/things/{id}"])
        self.stops("GetThing: another operation already maps to get_thing")

    def test_params_struct_named_like_a_schema(self) -> None:
        self.schemas["GetThingParams"] = {"type": "object", "properties": {"x": {"type": "string"}}}
        self.stops("GetThingParams is already a schema name")

    def test_operation_servers_override(self) -> None:
        self.get["servers"] = [{"url": "https://other.example/v1"}]
        self.stops("GetThing: `servers` overrides are not supported")

    def test_path_servers_override(self) -> None:
        self.spec["paths"]["/things/{id}"]["servers"] = [{"url": "https://other.example/v1"}]
        self.stops("/things/{id}: `servers` overrides are not supported")

    # ----- request bodies --------------------------------------------------

    def test_non_json_body(self) -> None:
        self.post["requestBody"] = {"content": {"multipart/form-data": {"schema": ref("Thing")}}}
        self.stops("CreateThing request body: only application/json")

    def test_inline_body(self) -> None:
        self.post["requestBody"] = json_content({"type": "array", "items": ref("Thing")})
        self.stops("CreateThing request body: must be a $ref to a component object")

    def test_body_ref(self) -> None:
        self.post["requestBody"] = {"$ref": "#/components/requestBodies/Thing"}
        self.stops("CreateThing request body: $ref request bodies are not supported")

    def test_body_schema_ref_outside_component_schemas(self) -> None:
        self.post["requestBody"] = json_content({"$ref": "#/components/requestBodies/Thing"})
        self.stops("#/components/requestBodies/Thing is not a component schema")

    def test_body_ref_to_array_schema(self) -> None:
        self.schemas["Things"] = {"type": "array", "items": {"type": "string"}}
        self.post["requestBody"] = json_content(ref("Things"))
        self.stops("CreateThing request body: must be a $ref to a component object")

    def test_read_only_field_in_request_body(self) -> None:
        self.schemas["Thing"]["properties"]["name"]["readOnly"] = True
        self.stops("Thing.name: required readOnly fields in request bodies")

    def test_fixed_point_strings_nested_in_request_containers(self) -> None:
        self.schemas["FixedPointDollars"] = {"type": "string"}
        self.schemas["FixedPointPair"] = {"type": "array", "items": ref("FixedPointDollars")}
        nested = {
            "grid": {"type": "array", "items": {"type": "array", "items": ref("FixedPointDollars")}},
            "pairs": {"type": "array", "items": ref("FixedPointPair")},
            "by_market": {"type": "object", "additionalProperties": ref("FixedPointDollars")},
        }
        for field, schema in nested.items():
            with self.subTest(field=field):
                spec = copy.deepcopy(self.spec)
                spec["components"]["schemas"]["Thing"]["properties"][field] = schema
                with self.assertRaises(SystemExit) as caught:
                    generate(spec)
                self.assertIn(f"Thing.{field}: validation cannot check fixed-point strings", str(caught.exception.code))

    def test_validated_structs_nested_in_request_containers(self) -> None:
        self.schemas["Leg"] = {"type": "object", "required": ["ticker"], "properties": {"ticker": {"type": "string"}}}
        leg_array = {"type": "array", "items": ref("Leg")}
        nested = {
            "leg_grid": {"type": "array", "items": leg_array},
            "legs_by_market": {"type": "object", "additionalProperties": ref("Leg")},
            "leg_lists": {"type": "object", "additionalProperties": leg_array},
            "leg_maps": {"type": "array", "items": {"type": "object", "additionalProperties": ref("Leg")}},
            "inline_grid": {"type": "array", "items": {"type": "array", "items": {
                "type": "object", "required": ["side"], "properties": {"side": {"type": "string", "enum": ["a"]}}}}},
        }
        for field, schema in nested.items():
            with self.subTest(field=field):
                spec = copy.deepcopy(self.spec)
                spec["components"]["schemas"]["Thing"]["properties"][field] = schema
                with self.assertRaises(SystemExit) as caught:
                    generate(spec)
                self.assertIn(f"Thing.{field}: validation cannot check", str(caught.exception.code))
                self.assertIn("inside nested arrays or maps in request bodies", str(caught.exception.code))

    def test_validated_struct_nested_below_a_request_member(self) -> None:
        self.schemas["Leg"] = {"type": "object", "required": ["ticker"], "properties": {"ticker": {"type": "string"}}}
        self.schemas["Thing"]["properties"]["detail"]["properties"]["legs"] = {
            "type": "object", "additionalProperties": ref("Leg")}
        self.stops("ThingDetail.legs: validation cannot check Leg inside nested arrays or maps")

    def test_optional_body_with_required_fields(self) -> None:
        for body in ({"required": False, **json_content(ref("Thing"))}, json_content(ref("Thing"))):
            with self.subTest(required=body.get("required", "absent")):
                self.post["requestBody"] = body
                self.stops("CreateThing request body: an optional body with required fields is not supported")

    # ----- responses -------------------------------------------------------

    def test_inline_response(self) -> None:
        self.get["responses"]["200"] = json_content({"type": "array", "items": ref("Thing")})
        self.stops("GetThing response 200: must be a $ref to a component schema")

    def test_response_ref(self) -> None:
        self.get["responses"]["200"] = {"$ref": "#/components/responses/ThingResponse"}
        self.stops("GetThing response 200: $ref responses are not supported")

    def test_response_schema_ref_outside_component_schemas(self) -> None:
        self.get["responses"]["200"] = json_content({"$ref": "#/components/responses/Thing"})
        self.stops("#/components/responses/Thing is not a component schema")

    def test_success_codes_with_different_bodies(self) -> None:
        self.get["responses"]["204"] = {"description": "nothing to return"}
        self.stops("GetThing: 2xx responses ['200', '204'] have different bodies")

    def test_non_json_response(self) -> None:
        self.get["responses"]["200"] = {"content": {"text/csv": {"schema": {"type": "string"}}}}
        self.stops("GetThing response 200: only application/json")

    def test_accepted_response(self) -> None:
        self.get["responses"] = {"202": json_content(ref("Thing"))}
        self.stops("GetThing: responses ['202'] are not supported")

    def test_no_success_response(self) -> None:
        self.get["responses"] = {"404": {"description": "missing"}}
        self.stops("GetThing: has no 2xx response")

    # ----- schemas ---------------------------------------------------------

    def test_one_of(self) -> None:
        self.schemas["Thing"]["properties"]["shape"] = {"oneOf": [{"type": "string"}, {"type": "integer"}]}
        self.stops("ThingShape: `oneOf` is not supported")

    def test_any_of_component(self) -> None:
        self.schemas["Either"] = {"anyOf": [ref("Thing"), ref("Empty")]}
        self.stops("Either: `anyOf` is not supported")

    def test_discriminator(self) -> None:
        self.schemas["Thing"]["discriminator"] = {"propertyName": "name"}
        self.stops("Thing: `discriminator` is not supported")

    def test_not(self) -> None:
        self.schemas["Thing"]["properties"]["shape"] = {"not": {"type": "integer"}}
        self.stops("ThingShape: `not` is not supported")

    def test_component_that_is_an_all_of(self) -> None:
        self.schemas["Wrapper"] = {"allOf": [ref("Thing")]}
        self.stops("Wrapper: a component that is an `allOf` or `$ref`")

    def test_component_that_is_a_ref(self) -> None:
        self.schemas["Alias"] = ref("Thing")
        self.stops("Alias: a component that is an `allOf` or `$ref`")

    def test_all_of_with_properties_beside_it(self) -> None:
        self.schemas["Thing"]["properties"]["more"] = {"allOf": [ref("GetThingResponse")],
                                                       "properties": {"extra": {"type": "string"}}}
        self.stops("ThingMore: ['properties'] beside `allOf`")

    def test_nullable_array_items(self) -> None:
        self.schemas["Thing"]["properties"]["notes"] = {"type": "array", "items": {"type": "string", "nullable": True}}
        self.stops("ThingNotesItem: nullable array items and map values")

    def test_nullable_map_values(self) -> None:
        self.schemas["Thing"]["properties"]["counts"] = {
            "type": "object", "additionalProperties": {"type": "integer", "nullable": True}}
        self.stops("ThingCountsValue: nullable array items and map values")

    def test_two_inline_objects_with_one_name(self) -> None:
        props = self.schemas["Thing"]["properties"]
        props["detail"]["properties"]["info"] = {"type": "object", "properties": {"a": {"type": "integer"}}}
        props["detail_info"] = {"type": "object", "properties": {"b": {"type": "string"}}}
        self.stops("ThingDetailInfo: an inline object has the same name as another schema")

    def test_all_of_with_conflicting_fields(self) -> None:
        self.schemas["BigThing"] = {"allOf": [ref("Thing"), {"type": "object", "properties": {"name": {"type": "integer"}}}]}
        self.stops("BigThing.name: incompatible allOf fields")

    def test_all_of_with_a_ref_and_an_inline_type_for_one_field(self) -> None:
        inline = {"type": "object", "properties": {"count": {"type": "integer"}}}
        referenced = {"type": "object", "properties": {"count": ref("GetThingResponse")}}
        for parts in ([inline, referenced], [referenced, inline]):
            with self.subTest(first="$ref" if parts[0] is referenced else "type"):
                self.schemas["BigThing"] = {"allOf": copy.deepcopy(parts)}
                self.stops("BigThing.count: incompatible allOf fields")

    def test_untyped_component(self) -> None:
        self.schemas["Anything"] = {"description": "Free-form value"}
        self.schemas["Thing"]["properties"]["anything"] = ref("Anything")
        self.stops("Anything: a component with no `type` or `properties`")

    def test_required_names_an_undeclared_property(self) -> None:
        self.schemas["Thing"]["required"].append("colour")
        self.stops("Thing: `required` names ['colour']")

    def test_inline_object_requires_an_undeclared_property(self) -> None:
        self.schemas["Thing"]["properties"]["detail"]["required"] = ["missing"]
        self.stops("ThingDetail: `required` names ['missing']")

    def test_all_of_requires_a_property_no_part_declares(self) -> None:
        self.schemas["BigThing"] = {"allOf": [ref("Thing"), {"type": "object", "required": ["extra"]}]}
        self.stops("BigThing: `required` names ['extra']")

    def test_type_list(self) -> None:
        self.schemas["Thing"]["properties"]["count"] = {"type": ["integer", "null"]}
        self.stops("ThingCount: `type` lists (OpenAPI 3.1)")

    def test_properties_with_additional_properties(self) -> None:
        self.schemas["Thing"]["additionalProperties"] = True
        self.stops("Thing: properties with additionalProperties")

    def test_component_array_of_objects_stops_before_emitting_an_invalid_alias(self) -> None:
        self.schemas["Things"] = {"type": "array", "items": ref("Thing")}
        self.stops("Things: component arrays with composite or enum items are not supported")

    def test_unused_enum_pin(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            generate(self.spec, {"GoneThingSize": "Size"})
        self.assertIn("match no inline enum", str(caught.exception.code))

    def test_enum_pin_shared_by_different_values(self) -> None:
        self.schemas["Other"] = {"type": "object", "properties": {"size": {"type": "string", "enum": ["s"]}}}
        with self.assertRaises(SystemExit) as caught:
            generate(self.spec, {"ThingSize": "Size", "OtherSize": "Size"})
        self.assertIn("pinned enum Size already has values", str(caught.exception.code))

    def test_component_array_of_enums_stops_before_emitting_an_invalid_alias(self) -> None:
        self.schemas["Colors"] = {"type": "array", "items": {"type": "string", "enum": ["red", "blue"]}}
        self.stops("Colors: component arrays with composite or enum items are not supported")

    def test_top_level_map(self) -> None:
        self.schemas["Totals"] = {"type": "object", "additionalProperties": {"type": "integer"}}
        self.stops("Totals: top-level map schemas")

    def test_ref_outside_component_schemas(self) -> None:
        self.schemas["Thing"]["properties"]["owner"] = {"$ref": "people.yaml#/Person"}
        self.stops("people.yaml#/Person is not a component schema")

    def test_integer_enum(self) -> None:
        self.schemas["Level"] = {"type": "integer", "enum": [1, 2, 3]}
        self.stops("Level: only string enums are supported")

    def test_inline_non_string_enums_outside_query_parameters(self) -> None:
        levels = {"type": "integer", "enum": [1, 2, 3]}
        cases = {
            "ThingLevel": lambda: self.schemas["Thing"]["properties"].update(level=levels),  # request body
            "GetThingResponseLevel": lambda: self.schemas["GetThingResponse"]["properties"].update(level=levels),
            "ThingDetailLevel": lambda: self.schemas["Thing"]["properties"]["detail"]["properties"].update(level=levels),
            "ThingLevelsItem": lambda: self.schemas["Thing"]["properties"].update(
                levels={"type": "array", "items": levels}),
            "ThingLevelsValue": lambda: self.schemas["Thing"]["properties"].update(
                levels={"type": "object", "additionalProperties": levels}),
            "ThingRatio": lambda: self.schemas["Thing"]["properties"].update(
                ratio={"type": "number", "enum": [0.5, 1.0]}),
            "LevelsItem": lambda: self.schemas.update(Levels={"type": "array", "items": levels}),
            "CreateThingResponseLevel": lambda: self.post["responses"].update({"201": json_content(
                {"type": "object", "properties": {"level": levels}})}),
        }
        for where, mutate in cases.items():
            with self.subTest(where=where):
                self.setUp()
                mutate()
                self.stops(f"{where}: only string enums are supported outside query parameters")

    def test_non_integer_enum_query_parameters(self) -> None:
        cases = {
            "number enum": {"type": "number", "enum": [0.5, 1.0]},
            "integer enum items": {"type": "array", "items": {"type": "integer", "enum": [1, 60]}},
        }
        for case, schema in cases.items():
            with self.subTest(case=case):
                self.setUp()
                self.get["parameters"].append({"name": "level", "in": "query", "schema": schema})
                self.stops("GetThingParams.level: validate() cannot check `enum`")

    # ----- keywords validate() cannot check (#135) ---------------------------

    def test_unchecked_keywords_in_requests(self) -> None:
        cases = {
            "Thing.name: validate() cannot check `pattern`":
                lambda: self.schemas["Thing"]["properties"]["name"].update(pattern="^[a-z]+$"),
            "Thing.count: validate() cannot check `exclusiveMinimum`":
                lambda: self.schemas["Thing"]["properties"].update(
                    count={"type": "integer", "minimum": 0, "exclusiveMinimum": True}),
            "Thing.count: validate() cannot check `multipleOf`":
                lambda: self.schemas["Thing"]["properties"].update(count={"type": "integer", "multipleOf": 5}),
            "Thing.count: validate() cannot check `minimum`":
                lambda: self.schemas["Thing"]["properties"].update(count={"type": "integer", "minimum": 0.5}),
            "Thing.name: validate() cannot check `minimum`":
                lambda: self.schemas["Thing"]["properties"]["name"].update(minimum=1),
            "Thing.tags: validate() cannot check `uniqueItems`":
                lambda: self.schemas["Thing"]["properties"].update(
                    tags={"type": "array", "items": {"type": "string"}, "uniqueItems": True}),
            "Thing.tags: validate() cannot check `minimum` on array items":
                lambda: self.schemas["Thing"]["properties"].update(
                    tags={"type": "array", "items": {"type": "string", "minimum": 1}}),
            "Thing.tags: validate() cannot check `pattern` on array items":
                lambda: self.schemas["Thing"]["properties"].update(
                    tags={"type": "array", "items": {"type": "string", "pattern": "^[a-z]+$"}}),
            "Thing.scores: validate() cannot check `minimum` on array items":
                lambda: self.schemas["Thing"]["properties"].update(
                    scores={"type": "array", "items": {"type": "number", "minimum": 0}}),
            "Thing.counts: validate() cannot check `minLength` on array items":
                lambda: self.schemas["Thing"]["properties"].update(
                    counts={"type": "array", "items": {"type": "integer", "minLength": 1}}),
            "GetThingParams.ids: validate() cannot check `pattern` on array items":
                lambda: self.get["parameters"][1]["schema"]["items"].update(pattern="^[a-z]+$"),
            # Rules member() does not read: inside a referenced component, on nested array
            # items or map values, and on the request object itself.
            "Thing.label: validate() cannot check `pattern` in Label":
                lambda: (self.schemas.update(Label={"type": "string", "pattern": "^[A-Z]+$"}),
                         self.schemas["Thing"]["properties"].update(label=ref("Label"))),
            "Thing.levels: validate() cannot check `minimum` in Level":
                lambda: (self.schemas.update(Level={"type": "integer", "minimum": 1}),
                         self.schemas["Thing"]["properties"].update(
                             levels={"type": "array", "items": ref("Level")})),
            "Thing.grid: validate() cannot check `minimum` on nested array items":
                lambda: self.schemas["Thing"]["properties"].update(grid={"type": "array", "items": {
                    "type": "array", "items": {"type": "integer", "minimum": 0}}}),
            "Thing.limits: validate() cannot check `maximum` on map values":
                lambda: self.schemas["Thing"]["properties"].update(limits={
                    "type": "object", "additionalProperties": {"type": "integer", "maximum": 5}}),
            "Thing: validate() cannot check `minProperties`":
                lambda: self.schemas["Thing"].update(minProperties=1),
            "GetThingParams.label: validate() cannot check `maxLength` in Label":
                lambda: (self.schemas.update(Label={"type": "string", "maxLength": 3}),
                         self.get["parameters"].append({"name": "label", "in": "query", "schema": ref("Label")})),
        }
        for message, mutate in cases.items():
            with self.subTest(message=message):
                self.setUp()
                mutate()
                self.stops(message)

    def test_rules_on_path_parameters(self) -> None:
        path_id = self.spec["paths"]["/things/{id}"]["parameters"][0]
        cases = {
            "parameter id: validate() cannot check `maxLength` on a path parameter":
                lambda: path_id["schema"].update(maxLength=8),
            "parameter id: validate() cannot check `enum` on a path parameter":
                lambda: path_id["schema"].update(enum=["a", "b"]),
            "parameter id: validate() cannot check `pattern` in Label on a path parameter":
                lambda: (self.schemas.update(Label={"type": "string", "pattern": "^[A-Z]+$"}),
                         path_id.update(schema=ref("Label"))),
        }
        for message, mutate in cases.items():
            with self.subTest(message=message):
                self.setUp()
                path_id = self.spec["paths"]["/things/{id}"]["parameters"][0]
                mutate()
                self.stops(message)

    def test_inline_object_named_like_a_component(self) -> None:
        self.schemas["ThingDetail"] = {"type": "object", "properties": {"other": {"type": "integer"}}}
        self.stops("ThingDetail: an inline object has the same name as another schema")

    def test_non_identifier_key(self) -> None:
        self.schemas["Thing"]["properties"]["x-rate"] = {"type": "string"}
        self.stops("Thing.x-rate needs a rename")

    def test_reserved_key(self) -> None:
        self.schemas["Thing"]["properties"]["requires"] = {"type": "string"}
        self.stops("Thing.requires needs a rename")


class RejectedWebSocket(unittest.TestCase):
    def setUp(self) -> None:
        schemas = {"Tick": {"type": "object", "properties": {"price": {"type": "integer"}}}}
        self.ws = WsGenerator({"components": {"schemas": schemas, "messages": {}}}, {})

    def stops(self, schema: dict, message: str) -> None:
        with self.assertRaises(SystemExit) as caught:
            self.ws.type_of(schema, "TickerMsg", "field")
        self.assertIn(message, str(caught.exception.code))

    def test_supported_types(self) -> None:
        self.assertEqual(self.ws.type_of({"type": ["string", "null"]}, "TickerMsg", "field")[0], "std::string")
        self.assertEqual(self.ws.type_of(ref("Tick"), "TickerMsg", "tick")[0], "Tick")
        self.ws.schemas["Blank"] = {"type": "object"}  # REST maps this $ref to RawJson too
        self.assertEqual(self.ws.type_of(ref("Blank"), "TickerMsg", "blank")[0], "RawJson")

    def test_one_of(self) -> None:
        self.stops({"oneOf": [{"type": "string"}, {"type": "integer"}]}, "`oneOf` is not supported")

    def test_all_of(self) -> None:
        self.stops({"allOf": [ref("Tick")]}, "`allOf` with 1 members is not supported")

    def test_untyped_schema(self) -> None:
        self.stops({"description": "anything"}, "a schema with no type is not supported")

    def test_ref_outside_component_schemas(self) -> None:
        self.stops({"$ref": "#/components/messages/Tick"}, "is not a component schema")

    def test_ref_to_a_union(self) -> None:
        self.ws.schemas["Either"] = {"oneOf": [{"type": "string"}, {"type": "integer"}]}
        self.stops(ref("Either"), "`oneOf` is not supported")

    def test_array_without_items(self) -> None:
        self.stops({"type": "array"}, "an array without `items` is not supported")

    def test_null_type(self) -> None:
        self.stops({"type": "null"}, "type 'null' is not supported")

    def test_mixed_enum_is_rejected(self) -> None:
        self.stops({"type": "integer", "enum": [1, "two"]}, "enums must contain strings or nonnegative int64 values")

    def test_numeric_enum_preserves_wire_numbers(self) -> None:
        name = self.ws.type_of({"type": "integer", "enum": [0, 100, 1000]}, "TickerMsg", "interval")[0]
        enum = self.ws.enums[name]
        self.assertIn("V0 = 0", emit_enum(enum))
        self.assertIn("Unknown = -1", emit_enum(enum))
        self.assertIn("std::int64_t text", emit_enum_parser(enum, "ws::" + name))
        self.assertIn("static_cast<std::int64_t>(value)", emit_enum_adapter(enum, "ws::" + name, "parse"))

    def test_numeric_enum_rejects_unrepresentable_values(self) -> None:
        for values in ([-1, 1], [True, False], [0, 9223372036854775808]):
            with self.subTest(values=values):
                self.stops({"type": "integer", "enum": values}, "enums must contain strings or nonnegative int64 values")

    def test_properties_with_additional_properties(self) -> None:
        self.stops({"type": "object", "properties": {"a": {"type": "string"}}, "additionalProperties": True},
                   "properties with additionalProperties")

    def test_struct_from_an_all_of(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            self.ws.struct_for("TickerMsg", {"allOf": [ref("Tick")]})
        self.assertIn("`allOf` with 1 members is not supported", str(caught.exception.code))


def command(cmd: str, params: dict, required: list[str]) -> dict:
    return {"type": "object", "properties": {
        "id": {"type": "integer"}, "cmd": {"type": "string", "const": cmd},
        "params": {"type": "object", "properties": params, "required": required}}}


def update_command(actions: list[str], extra: dict | None = None, required: list[str] | None = None) -> dict:
    params = {"sid": {"type": "integer"}, "action": {"type": "string", "enum": actions}, **(extra or {})}
    return command("update_subscription", params, required or ["action"])


WS_BASE = {
    "channels": {"ticker": {"address": "ticker", "messages": {"ticker": {"$ref": "#/components/messages/ticker"}}}},
    "components": {
        "messages": {"ticker": {"summary": "Ticker update", "payload": ref("tickerPayload")}},
        "schemas": {
            "subscribeCommandPayload": command("subscribe", {
                "channels": {"type": "array", "items": {"type": "string", "enum": ["ticker"]}},
                "market_ticker": {"type": "string"},
            }, ["channels"]),
            "updateSubscriptionCommandPayload": update_command(
                ["add_markets"], {"market_tickers": {"type": "array", "items": {"type": "string"}}}),
            "cfbenchmarksUpdateSubscriptionCommandPayload": update_command(["subscribe_indices"]),
            "pythUpdateSubscriptionCommandPayload": update_command(["subscribe_feeds"]),
            "ebattleUpdateSubscriptionCommandPayload": update_command(
                ["subscribe_matches"], {"match_ids": {"type": "array", "items": {"type": "string"}}},
                ["sid", "action"]),
            "tickerPayload": {"type": "object", "properties": {
                "type": {"type": "string", "const": "ticker"},
                "sid": {"type": "integer"},
                "msg": {"type": "object", "properties": {"price": {"type": "integer"}}},
            }},
        },
    },
}


def generate_ws(spec: dict) -> WsGenerator:
    gen = WsGenerator(spec, {})
    gen.run()
    return gen


class WebSocketSpec(unittest.TestCase):
    def setUp(self) -> None:
        self.spec = copy.deepcopy(WS_BASE)
        self.schemas = self.spec["components"]["schemas"]

    def stops(self, message: str) -> None:
        with self.assertRaises(SystemExit) as caught:
            generate_ws(self.spec)
        self.assertIn(message, str(caught.exception.code))

    def members(self, struct: str) -> dict:
        return {m.json_name: m for m in generate_ws(self.spec).structs[struct].members}

    def test_contract_boolean_spelling_preserves_market_sides(self) -> None:
        schema = load_contract_yaml("enum: [yes, no, on, off]\nnullable: true\ndeprecated: false\n")
        self.assertEqual(schema["enum"], ["yes", "no", "on", "off"])
        self.assertIs(schema["nullable"], True)
        self.assertIs(schema["deprecated"], False)

    def test_base_spec_generates(self) -> None:
        self.assertIn("price", self.members("Ticker"))

    def test_update_commands_name_the_subscription_with_sid(self) -> None:
        wire = generate_ws(self.spec).wire_header()
        self.assertIn("struct UpdateSubscriptionWire {\n\tstd::int64_t sid{0};\n", wire)
        self.assertIn("\twire.sid = sid;\n", wire)
        self.assertNotIn("sids", wire)

    def test_update_command_without_sid(self) -> None:
        del self.schemas["ebattleUpdateSubscriptionCommandPayload"]["properties"]["params"]["properties"]["sid"]
        self.stops("has no `sid`")

    def test_declared_channel_is_subscribable_when_command_enum_lags(self) -> None:
        self.spec["channels"]["batch_communications"] = {
            "address": "batch_communications", "messages": self.spec["channels"]["ticker"]["messages"]}
        generator = generate_ws(self.spec)
        self.assertIn("batch_communications", generator.channels)
        self.assertIn("BatchCommunications", generator.models_header())

    def test_channel_address_is_the_wire_name(self) -> None:
        self.spec["channels"] = {"tickerChannel": self.spec["channels"]["ticker"]}
        generator = generate_ws(self.spec)
        self.assertEqual(generator.channels, ["ticker"])
        self.assertEqual(generator.messages[0].channels, ["ticker"])
        self.assertIn("| `ticker` | `Ticker` | `ws::Ticker` |", generator.channels_doc())
        self.assertNotIn("TickerChannel", generator.models_header())

    def test_channel_without_an_address(self) -> None:
        for address in ("absent", None, ""):
            with self.subTest(address=address):
                channel = self.spec["channels"]["ticker"]
                channel.pop("address", None)
                if address != "absent":
                    channel["address"] = address
                self.stops("ticker: a channel needs an `address`")

    def test_channel_address_expression(self) -> None:
        self.spec["channels"]["ticker"]["address"] = "ticker.{market_ticker}"
        self.stops("ticker: channel address 'ticker.{market_ticker}' has parameters")

    def test_msg_given_as_a_ref(self) -> None:
        self.schemas["tickerMsg"] = self.schemas["tickerPayload"]["properties"]["msg"]
        self.schemas["tickerPayload"]["properties"]["msg"] = ref("tickerMsg")
        self.assertIn("price", self.members("Ticker"))

    def test_param_required_by_one_command_is_optional(self) -> None:
        self.schemas["pythUpdateSubscriptionCommandPayload"] = update_command(
            ["subscribe_feeds"], {"feed": {"type": "string"}}, ["action", "feed"])
        members = self.members("UpdateSubscriptionParams")
        self.assertEqual(members["feed"].cpp_type, "std::optional<std::string>")
        self.assertTrue(members["action"].required)

    def test_msg_that_is_not_an_object(self) -> None:
        self.schemas["tickerPayload"]["properties"]["msg"] = {"type": "array", "items": {"type": "integer"}}
        self.stops("ticker.msg must be an object with properties")

    def test_extra_envelope_field(self) -> None:
        self.schemas["tickerPayload"]["properties"]["ts_ms"] = {"type": "integer"}
        self.stops("ticker: envelope fields ['ts_ms'] are not supported")

    def test_sending_timestamp_is_preserved(self) -> None:
        self.schemas["sendingTimestampMs"] = {"type": "integer", "format": "int64"}
        self.schemas["tickerPayload"]["properties"]["sending_ts_ms"] = ref("sendingTimestampMs")
        header = generate_ws(self.spec).models_header()
        self.assertIn("std::optional<std::int64_t> sending_ts_ms;", header)

    def test_sending_timestamp_cannot_change_type_silently(self) -> None:
        self.schemas["tickerPayload"]["properties"]["sending_ts_ms"] = {"type": "string"}
        self.stops("ticker.sending_ts_ms must be an integer")

    def test_new_update_command(self) -> None:
        self.schemas["fooUpdateSubscriptionCommandPayload"] = update_command(["subscribe_foo"])
        self.stops("update_subscription command payloads are")

    def test_shared_param_with_different_schemas(self) -> None:
        self.schemas["pythUpdateSubscriptionCommandPayload"] = update_command(
            ["subscribe_feeds"], {"market_tickers": {"type": "array", "items": {"type": "integer"}}})
        self.stops("UpdateSubscriptionParams.market_tickers has different schemas across commands")

    def test_shared_enum_param_cannot_change_shape(self) -> None:
        self.schemas["pythUpdateSubscriptionCommandPayload"] = update_command([1, 2])
        self.schemas["pythUpdateSubscriptionCommandPayload"]["properties"]["params"]["properties"]["action"]["type"] = "integer"
        self.stops("UpdateSubscriptionParams.action has different schemas across commands")

    def test_struct_name_collision_is_rejected(self) -> None:
        ws = WsGenerator(self.spec, {})
        ws.struct_for("Collision", {"type": "object", "properties": {"a": {"type": "string"}}})
        with self.assertRaisesRegex(SystemExit, "Collision: different schemas share a struct name"):
            ws.struct_for("Collision", {"type": "object", "properties": {"b": {"type": "integer"}}})

    def test_param_that_is_not_an_identifier(self) -> None:
        params = self.schemas["subscribeCommandPayload"]["properties"]["params"]["properties"]
        params["market-ticker"] = {"type": "string"}
        self.stops("SubscribeParams.market-ticker needs a rename")


if __name__ == "__main__":
    unittest.main()
