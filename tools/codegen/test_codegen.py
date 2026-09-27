"""Tests that tools/codegen stops on spec features it cannot map instead of dropping them.

    python3 -m unittest discover -s tools/codegen   # `make lint` runs this

Each test edits a small spec that the generator accepts, then expects SystemExit.
"""

from __future__ import annotations

import copy
import unittest

from generate import Generator
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
                "requestBody": json_content(ref("Thing")),
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


def generate(spec: dict) -> Generator:
    gen = Generator(spec)
    gen.run()
    return gen


class SupportedSpec(unittest.TestCase):
    def test_base_spec_generates(self) -> None:
        ops = {op.method_name: op for op in generate(copy.deepcopy(BASE)).operations}
        self.assertEqual(ops["create_thing"].body_type, "Thing")
        self.assertEqual(ops["get_thing"].response_type, "Thing")  # unwrapped from GetThingResponse
        self.assertTrue(ops["reset_thing"].empty_body)
        self.assertIsNone(ops["reset_thing"].response_type)

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
        spec = copy.deepcopy(BASE)  # Kalshi's Perps spec puts maxLength and pattern here
        spec["components"]["schemas"]["Code"] = {"type": "string"}
        spec["components"]["schemas"]["Thing"]["properties"]["code"] = {
            "allOf": [ref("Code")], "maxLength": 8, "pattern": "^[A-Z]+$"}
        members = {m.json_name: m for m in generate(spec).structs["Thing"].members}
        self.assertEqual(members["code"].cpp_type, "std::optional<Code>")

    def test_nullable_component_makes_members_optional(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["components"]["schemas"]["MaybeName"] = {"type": "string", "nullable": True}
        thing = spec["components"]["schemas"]["Thing"]
        thing["properties"]["alias"] = ref("MaybeName")
        thing["required"].append("alias")
        members = {m.json_name: m for m in generate(spec).structs["Thing"].members}
        self.assertEqual(members["alias"].cpp_type, "std::optional<MaybeName>")

    def test_same_body_under_two_success_codes_is_allowed(self) -> None:
        spec = copy.deepcopy(BASE)
        spec["paths"]["/things"]["post"]["responses"]["200"] = json_content(ref("Thing"))
        ops = {op.method_name: op for op in generate(spec).operations}
        self.assertEqual(ops["create_thing"].response_type, "Thing")


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
        self.schemas["Things"] = {"type": "array", "items": ref("Thing")}
        self.post["requestBody"] = json_content(ref("Things"))
        self.stops("CreateThing request body: must be a $ref to a component object")

    def test_read_only_field_in_request_body(self) -> None:
        self.schemas["Thing"]["properties"]["name"]["readOnly"] = True
        self.stops("Thing.name: required readOnly fields in request bodies")

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

    def test_all_of_with_two_members(self) -> None:
        self.schemas["BigThing"] = {"allOf": [ref("Thing"), {"type": "object", "properties": {"x": {"type": "string"}}}]}
        self.stops("BigThing: `allOf` with 2 members")

    def test_type_list(self) -> None:
        self.schemas["Thing"]["properties"]["count"] = {"type": ["integer", "null"]}
        self.stops("ThingCount: `type` lists (OpenAPI 3.1)")

    def test_properties_with_additional_properties(self) -> None:
        self.schemas["Thing"]["additionalProperties"] = True
        self.stops("Thing: properties with additionalProperties")

    def test_top_level_map(self) -> None:
        self.schemas["Totals"] = {"type": "object", "additionalProperties": {"type": "integer"}}
        self.stops("Totals: top-level map schemas")

    def test_ref_outside_component_schemas(self) -> None:
        self.schemas["Thing"]["properties"]["owner"] = {"$ref": "people.yaml#/Person"}
        self.stops("people.yaml#/Person is not a component schema")

    def test_integer_enum(self) -> None:
        self.schemas["Level"] = {"type": "integer", "enum": [1, 2, 3]}
        self.stops("Level: only string enums are supported")

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

    def test_integer_enum(self) -> None:
        self.stops({"type": "integer", "enum": [1, 2]}, "only string enums are supported")

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
    "channels": {"ticker": {"messages": {"ticker": {"$ref": "#/components/messages/ticker"}}}},
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

    def test_base_spec_generates(self) -> None:
        self.assertIn("price", self.members("Ticker"))

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

    def test_new_update_command(self) -> None:
        self.schemas["fooUpdateSubscriptionCommandPayload"] = update_command(["subscribe_foo"])
        self.stops("update_subscription command payloads are")

    def test_shared_param_with_different_schemas(self) -> None:
        self.schemas["pythUpdateSubscriptionCommandPayload"] = update_command(
            ["subscribe_feeds"], {"market_tickers": {"type": "array", "items": {"type": "integer"}}})
        self.stops("UpdateSubscriptionParams.market_tickers has different schemas across commands")

    def test_param_that_is_not_an_identifier(self) -> None:
        params = self.schemas["subscribeCommandPayload"]["properties"]["params"]["properties"]
        params["market-ticker"] = {"type": "string"}
        self.stops("SubscribeParams.market-ticker needs a rename")


if __name__ == "__main__":
    unittest.main()
