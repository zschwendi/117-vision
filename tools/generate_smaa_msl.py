#!/usr/bin/env python3
"""Generate native/EngineHost/smaa_msl.inc from the vendored SMAA reference.

SMAA.hlsl (third_party/smaa, MIT) is written for porting through its
SMAA_CUSTOM_SL macros. Metal Shading Language still lacks three HLSL forms
the file uses, which are rewritten here rather than by hand so the result
can be regenerated and checked against the unchanged upstream text:

* `out` / `inout` parameters become `thread` references, and the `float4
  offset[3]` array parameters become references to arrays;
* the two corner-pattern calls pass a swizzle to an `inout` parameter, which
  a reference cannot bind, so they go through a temporary;
* `SMAAMovc` is replaced by a `select` macro (its bool4 overload forwards
  swizzles by reference), and `discard` becomes `discard_fragment()`.

The prelude supplies the porting macros, and the three fragment entry points
run SMAA 1x at the HIGH preset with luma edge detection. The render target
metrics are function constant 0, so each target size gets its own
specialised pipelines. The area and search lookup textures are emitted as raw
DEFLATE streams that NSData decompresses (no zlib link dependency); their
source is the hex text in third_party/smaa (the repository keeps text only).

    python3 tools/generate_smaa_msl.py           # write the include
    python3 tools/generate_smaa_msl.py --check   # fail if it is stale
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SMAA = ROOT / "third_party" / "smaa"
OUTPUT = ROOT / "native" / "EngineHost" / "smaa_msl.inc"
AREA_BYTES = 160 * 560 * 2
SEARCH_BYTES = 64 * 16

PRELUDE = r"""#include <metal_stdlib>
using namespace metal;
constant float4 smaa_rt_metrics [[function_constant(0)]];
#define SMAA_RT_METRICS smaa_rt_metrics
#define SMAA_CUSTOM_SL 1
#define SMAA_PRESET_HIGH 1
constexpr sampler smaa_linear_sampler(filter::linear, address::clamp_to_edge);
constexpr sampler smaa_point_sampler(filter::nearest, address::clamp_to_edge);
#define SMAATexture2D(tex) texture2d<float> tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.sample(smaa_linear_sampler, coord, level(0.0))
#define SMAASampleLevelZeroPoint(tex, coord) tex.sample(smaa_point_sampler, coord, level(0.0))
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.sample(smaa_linear_sampler, coord, level(0.0), offset)
#define SMAASample(tex, coord) tex.sample(smaa_linear_sampler, coord)
#define SMAASamplePoint(tex, coord) tex.sample(smaa_point_sampler, coord)
#define SMAASampleOffset(tex, coord, offset) tex.sample(smaa_linear_sampler, coord, offset)
#define SMAA_FLATTEN
#define SMAA_BRANCH
#define lerp(a, b, t) mix(a, b, t)
#define mad(a, b, c) ((a) * (b) + (c))
#define SMAAMovc(cond, variable, value) ((variable) = select((variable), (value), (cond)))
"""

MAINS = r"""
struct SMAAOut { float4 position [[position]]; };
vertex SMAAOut smaa_vertex(uint vid [[vertex_id]]) {
    float2 p = float2(float((vid << 1) & 2u), float(vid & 2u));
    SMAAOut o; o.position = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0); return o;
}
fragment float4 smaa_edges(SMAAOut in [[stage_in]], texture2d<float> colorTex [[texture(0)]]) {
    float2 texcoord = in.position.xy * SMAA_RT_METRICS.xy;
    float4 offset[3];
    SMAAEdgeDetectionVS(texcoord, offset);
    return float4(SMAALumaEdgeDetectionPS(texcoord, offset, colorTex), 0.0, 0.0);
}
fragment float4 smaa_weights(SMAAOut in [[stage_in]], texture2d<float> edgesTex [[texture(0)]],
                             texture2d<float> areaTex [[texture(1)]], texture2d<float> searchTex [[texture(2)]]) {
    float2 texcoord = in.position.xy * SMAA_RT_METRICS.xy;
    float2 pixcoord;
    float4 offset[3];
    SMAABlendingWeightCalculationVS(texcoord, pixcoord, offset);
    return SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset, edgesTex, areaTex, searchTex, float4(0.0));
}
fragment float4 smaa_blend(SMAAOut in [[stage_in]], texture2d<float> colorTex [[texture(0)]],
                           texture2d<float> blendTex [[texture(1)]]) {
    float2 texcoord = in.position.xy * SMAA_RT_METRICS.xy;
    float4 offset;
    SMAANeighborhoodBlendingVS(texcoord, offset);
    float4 blended = SMAANeighborhoodBlendingPS(texcoord, offset, colorTex, blendTex);
    /* World layers keep their own alpha, as the FXAA pass does. */
    return float4(blended.rgb, colorTex.sample(smaa_point_sampler, texcoord, level(0.0)).a);
}
"""


def rewrite(hlsl: str) -> str:
    text = hlsl
    # The conditional moves: drop both overloads, the macro in the prelude replaces them.
    movc = re.compile(r"void SMAAMovc\(bool2 cond, inout float2 variable, float2 value\) \{.*?\n\}\n\s*\n"
                      r"void SMAAMovc\(bool4 cond, inout float4 variable, float4 value\) \{.*?\n\}\n", re.S)
    text, count = movc.subn("/* SMAAMovc: see the generated prelude (select). */\n", text)
    if count != 1:
        raise SystemExit("SMAA.hlsl: SMAAMovc definitions not found as expected")
    # Array parameters, in and out alike, are references to arrays.
    text, arrays = re.subn(r"\b(?:out\s+)?float4 offset\[3\]", "thread float4 (&offset)[3]", text)
    if arrays < 8:
        raise SystemExit(f"SMAA.hlsl: expected the offset[3] parameters, found {arrays}")
    # Remaining out/inout parameters become thread references.
    text, refs = re.subn(r"\b(?:out|inout)\s+(float[234]?|bool[234]?)\s+(\w+)", r"thread \1 &\2", text)
    if refs < 6:
        raise SystemExit(f"SMAA.hlsl: expected out/inout parameters, found {refs}")
    corners = {
        "SMAADetectHorizontalCornerPattern(SMAATexturePass2D(edgesTex), weights.rg, coords.xyzy, d);":
            "{ float2 corner = weights.rg; SMAADetectHorizontalCornerPattern(SMAATexturePass2D(edgesTex), corner, coords.xyzy, d); weights.rg = corner; }",
        "SMAADetectVerticalCornerPattern(SMAATexturePass2D(edgesTex), weights.ba, coords.xyxz, d);":
            "{ float2 corner = weights.ba; SMAADetectVerticalCornerPattern(SMAATexturePass2D(edgesTex), corner, coords.xyxz, d); weights.ba = corner; }",
    }
    for old, new in corners.items():
        if text.count(old) != 1:
            raise SystemExit(f"SMAA.hlsl: corner pattern call not found: {old}")
        text = text.replace(old, new)
    text, discards = re.subn(r"\bdiscard;", "discard_fragment();", text)
    if discards < 1:
        raise SystemExit("SMAA.hlsl: discard not found")
    if re.search(r"\b(out|inout)\s+float", text):
        raise SystemExit("SMAA.hlsl: an out/inout parameter was not rewritten")
    return text


def c_string(text: str) -> str:
    # UTF-8 bytes; anything outside printable ASCII as three-digit octal so
    # the compiler sees exactly the upstream characters.
    lines = []
    for line in text.replace("\t", "    ").splitlines():
        escaped = []
        for byte in line.encode("utf-8"):
            char = chr(byte)
            if char in '"\\':
                escaped.append("\\" + char)
            elif 32 <= byte < 127:
                escaped.append(char)
            else:
                escaped.append(f"\\{byte:03o}")
        lines.append(f'    "{"".join(escaped)}\\n"')
    return "\n".join(lines)


def c_bytes(data: bytes) -> str:
    rows = []
    for start in range(0, len(data), 24):
        rows.append("    " + ",".join(f"0x{b:02x}" for b in data[start:start + 24]) + ",")
    return "\n".join(rows)


def texture_bytes(path: Path, expected: int, digest: str) -> bytes:
    """The hex text form of a lookup texture (one image row per line)."""
    rows = [line.strip() for line in path.read_text().splitlines() if line.strip() and not line.startswith("#")]
    data = bytes.fromhex("".join(rows))
    if len(data) != expected or hashlib.sha256(data).hexdigest() != digest:
        raise SystemExit(f"{path.name}: {len(data)} bytes or digest differ from UPSTREAM.json")
    return data


def raw_deflate(data: bytes) -> bytes:
    # NSDataCompressionAlgorithmZlib reads raw DEFLATE (no zlib header or
    # Adler-32), which is what a negative window size produces.
    compressor = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    raw = compressor.compress(data) + compressor.flush()
    if zlib.decompress(raw, -15) != data:
        raise SystemExit("raw DEFLATE round trip failed")
    return raw


def generate() -> str:
    manifest = json.loads((SMAA / "UPSTREAM.json").read_text())["files"]
    source = (SMAA / "SMAA.hlsl").read_bytes()
    if hashlib.sha256(source).hexdigest() != manifest["SMAA.hlsl"]["sha256"]:
        raise SystemExit("SMAA.hlsl differs from UPSTREAM.json")
    hlsl = source.decode("utf-8")
    license_text = (SMAA / "LICENSE").read_text().strip()
    msl = PRELUDE + rewrite(hlsl) + MAINS
    area = raw_deflate(texture_bytes(SMAA / "AreaTex.hex", AREA_BYTES, manifest["AreaTex.hex"]["data_sha256"]))
    search = raw_deflate(texture_bytes(SMAA / "SearchTex.hex", SEARCH_BYTES, manifest["SearchTex.hex"]["data_sha256"]))
    digest = hashlib.sha256((SMAA / "SMAA.hlsl").read_bytes()).hexdigest()
    notice = "\n".join(" * " + line if line else " *" for line in license_text.splitlines())
    return f"""/* GENERATED by tools/generate_smaa_msl.py from third_party/smaa; do not edit.
 * SMAA.hlsl sha256 {digest}
 *
 * SMAA: Enhanced Subpixel Morphological Antialiasing (iryoku/smaa 71c806a),
 * rewritten for the Metal Shading Language as the generator describes, with
 * its lookup textures. Original notice:
 *
{notice}
 */
static const char mr_smaa_msl[] =
{c_string(msl)};
/* AreaTex: 160x560, two channels (RG8). Raw DEFLATE. */
static const unsigned char mr_smaa_area_deflate[] = {{
{c_bytes(area)}
}};
enum {{ MR_SMAA_AREA_WIDTH = 160, MR_SMAA_AREA_HEIGHT = 560, MR_SMAA_AREA_BYTES = {AREA_BYTES} }};
/* SearchTex: 64x16, one channel (R8). Raw DEFLATE. */
static const unsigned char mr_smaa_search_deflate[] = {{
{c_bytes(search)}
}};
enum {{ MR_SMAA_SEARCH_WIDTH = 64, MR_SMAA_SEARCH_HEIGHT = 16, MR_SMAA_SEARCH_BYTES = {SEARCH_BYTES} }};
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if the include differs from a fresh generation")
    args = parser.parse_args()
    text = generate()
    if args.check:
        current = OUTPUT.read_text() if OUTPUT.exists() else ""
        if current != text:
            print(f"{OUTPUT.relative_to(ROOT)} is stale; run tools/generate_smaa_msl.py", file=sys.stderr)
            return 1
        print(f"{OUTPUT.relative_to(ROOT)} is current")
        return 0
    OUTPUT.write_text(text)
    print(f"wrote {OUTPUT.relative_to(ROOT)} ({len(text)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
