import importlib.util
import json
import math
import os
from pathlib import Path
import re

PROJECT = Path(__file__).resolve().parent
UPSTREAM = Path(os.environ.get("MPC_VST", str(PROJECT.parents[1] / "toolchain/upstream"))).resolve()
FACTORY_MODELS = ("Felt 73", "Ana 78", "Poly DCO Pulse", "Prism Bell")
FACTORY_PROGRAM_MODELS = (1, 0, 2, 3)
DISPLAY_NAMES = {
    "ChordType": "Chord Type", "SynthModel": "Voice Model", "SubOsc": "Sub Bass",
    "TapeWow": "Tape Wow", "WowSpeed": "Wow Speed", "TapeEcho": "Tape Echo",
    "ShimmerVerb": "Shimmer", "ChorusMix": "Chorus", "OutputGain": "Level",
    "FeltHammer": "Hammer", "FeltTine": "Tine", "FeltBark": "Bark", "FeltDamping": "Damping",
    "AnaDetune": "Detune", "AnaDrift": "Drift", "AnaFilterEnv": "Filter Env", "AnaWave": "Wave",
    "DcoPulseWidth": "Pulse Width", "DcoPwmDepth": "PWM Depth", "DcoPwmRate": "PWM Rate", "DcoWave": "Wave",
    "PrismColour": "Colour", "PrismPartial": "Partial", "PrismStrike": "Strike", "PrismRing": "Ring",
}
PARAMETER_KEYS = (
    "ChordType", "Inversion", "SynthModel", "SubOsc", "TapeWow", "WowSpeed",
    "Cutoff", "Resonance", "Attack", "Decay", "Sustain", "Release", "TapeEcho",
    "ShimmerVerb", "ChorusMix", "OutputGain",
    "FeltHammer", "FeltTine", "FeltBark", "FeltDamping",
    "AnaDetune", "AnaDrift", "AnaFilterEnv", "AnaWave",
    "DcoPulseWidth", "DcoPwmDepth", "DcoPwmRate", "DcoWave",
    "PrismColour", "PrismPartial", "PrismStrike", "PrismRing",
)


def finite_number(value):
    return (not isinstance(value, bool) and isinstance(value, (int, float)) and
            abs(value) <= 3.4028234663852886e38 and math.isfinite(value))


def parameters():
    result = json.loads((PROJECT / "wasm files/params.json").read_text())
    if not isinstance(result, list) or len(result) != 32:
        raise ValueError("HexChord 73 requires its original 16 parameters and sixteen engine controls")
    for index, param in enumerate(result):
        if not isinstance(param, dict):
            raise ValueError("Each parameter must be an object")
        if type(param.get("id")) is not int or param["id"] != index:
            raise ValueError("Invalid parameter order")
        name = param.get("name")
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) or name != PARAMETER_KEYS[index]:
            raise ValueError("Parameter keys and original prefix must remain unchanged")
        if not all(finite_number(param.get(key)) for key in ("min", "max")) or not param["min"] < param["max"]:
            raise ValueError("Invalid parameter order or range")
        if param.get("curve") not in ("linear", "exponential", "squared"):
            raise ValueError("Unsupported parameter curve")
        if param["curve"] == "exponential" and param["min"] <= 0:
            raise ValueError("Exponential parameter bounds must be positive")
        if param.get("type") not in ("enum", "percent", "float", "frequency", "time", "gain"):
            raise ValueError("Unsupported parameter type")
        if not isinstance(param.get("unit", ""), str):
            raise ValueError("Parameter units must be text")
        if param["type"] == "enum":
            choices = param.get("choices")
            if (not isinstance(choices, list) or len(choices) < 2 or
                    not all(isinstance(choice, str) and choice.strip() for choice in choices) or
                    len(set(choices)) != len(choices) or param["min"] != 0 or
                    param["max"] != len(choices) - 1 or param["curve"] != "linear"):
                raise ValueError("Invalid enum choices or range")
            value = param.get("default")
            if not (isinstance(value, str) and value in choices or
                    type(value) is int and 0 <= value < len(choices)):
                raise ValueError("Invalid enum default")
        elif (not finite_number(param.get("default")) or
              not param["min"] <= param["default"] <= param["max"] or "choices" in param):
            raise ValueError("Invalid numeric parameter default")
    return result


def default(param):
    value = param["default"]
    return param["choices"].index(value) if isinstance(value, str) else value


def normalized(param, value):
    if param["curve"] == "exponential":
        return math.log(value / param["min"]) / math.log(param["max"] / param["min"])
    result = (value - param["min"]) / (param["max"] - param["min"])
    return math.sqrt(result) if param["curve"] == "squared" else result


def _factory_states(definitions):
    values = [normalized(param, default(param)) if param["curve"] != "linear" else default(param)
              for param in definitions]
    states = []
    for model in range(4):
        selected = values.copy()
        selected[2] = model
        states.append("HX73 3 " + " ".join(format(value, ".9g") for value in selected))
    return states


def factory_presets():
    configuration = json.loads((PROJECT / "vst.json").read_text())
    identity = {"name": "HexChord 73", "vendor": "Twisted Cable", "uid": "Hx73",
                "version": 1000, "so": "hexachord-73.so"}
    if (not isinstance(configuration, dict) or type(configuration.get("version")) is not int or
            any(configuration.get(key) != value for key, value in identity.items())):
        raise ValueError("Factory preset identity must match HexChord 73")
    definitions = parameters()
    if tuple(definitions[2]["choices"]) != FACTORY_MODELS:
        raise ValueError("Factory presets require the four canonical engines")
    specification = importlib.util.spec_from_file_location("hexachord_xpl", UPSTREAM / "tools/xpl.py")
    helper = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(helper)
    folder = identity["vendor"] + " - VST - " + identity["name"]
    library = "%payload-path%/" + folder + "/" + identity["so"]
    result = {}
    for index, (model, state) in enumerate(zip(FACTORY_MODELS, _factory_states(definitions))):
        chunk = state.encode() + b"\0"
        title = model + " Default"
        text = helper.xpl(identity["name"], identity["vendor"], library,
                          int.from_bytes(identity["uid"].encode(), "big"), identity["version"], chunk,
                          preset=title, date="12-10-2026-00-00")
        result[f"{index:04d}-{title}.xpl"] = text.encode("utf-8")
    return result


def write_factory_presets(skin_folder):
    presets = factory_presets()
    destination = Path(skin_folder) / "Presets"
    destination.mkdir(parents=True, exist_ok=True)
    for filename, contents in presets.items():
        (destination / filename).write_bytes(contents)
    return [destination / filename for filename in presets]


def generate():
    definitions = parameters()
    build = PROJECT / "build"
    native = build / "native"
    native.mkdir(parents=True, exist_ok=True)
    mpc = []
    rows = []
    for param in definitions:
        item = {"key": param["name"], "name": DISPLAY_NAMES.get(param["name"], param["name"]),
                "default": param["default"]}
        if param["type"] == "enum":
            item["options"] = param["choices"]
            item["qlink_continuous"] = 1
        else:
            curved = param["curve"] != "linear"
            item.update(min=0 if curved else param["min"], max=1 if curved else param["max"],
                        unit=param.get("unit", ""),
                        default=normalized(param, default(param)) if curved else default(param))
            if curved:
                item["dynamic_display"] = True
        mpc.append(item)
        number = lambda value: repr(float(value)) + "f"
        rows.append("    {%s, %s, %s, %s, Curve::%s, %s}," % (
            json.dumps(param["name"]), number(param["min"]), number(param["max"]),
            number(default(param)), param["curve"].title(),
            "true" if param["type"] == "enum" else "false"))
    mpc[16:16] = [{"key": key, "name": name, "min": 0, "max": 1, "default": .5}
                  for key, name in (("ChordPadX", "Chord Touch X"), ("ChordPadY", "Chord Touch Y"))]
    (build / "mpc-params.json").write_text(json.dumps(mpc, indent=2) + "\n")
    header = """#pragma once
#ifdef __cplusplus
namespace hexachord {
enum class Curve { Linear, Exponential, Squared };
struct Parameter {
    const char* key;
    float minimum, maximum, initial;
    Curve curve;
    bool stepped;
};
constexpr Parameter parameters[32] = {
""" + "\n".join(rows) + "\n};\n#define HEXACHORD_FACTORY_STORAGE constexpr\n#else\n#define HEXACHORD_FACTORY_STORAGE static\n#endif\n" + \
        "HEXACHORD_FACTORY_STORAGE const int factoryProgramModels[4] = {" + \
        ", ".join(map(str, FACTORY_PROGRAM_MODELS)) + "};\n" + \
        "HEXACHORD_FACTORY_STORAGE const char* const factoryNames[4] = {\n" + \
        ",\n".join("    " + json.dumps(model + " Default") for model in FACTORY_MODELS) + "\n};\n" + \
        "HEXACHORD_FACTORY_STORAGE const char* const factoryStates[4] = {\n" + \
        ",\n".join("    " + json.dumps(state) for state in _factory_states(definitions)) + \
        "\n};\n#undef HEXACHORD_FACTORY_STORAGE\n#ifdef __cplusplus\n}\n#endif\n"
    (native / "hexachord_params.h").write_text(header)


if __name__ == "__main__":
    generate()
