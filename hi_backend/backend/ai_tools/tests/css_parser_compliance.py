#!/usr/bin/env python3
"""CSS Parser Compliance Test Suite for HISE's simple_css parser.

Runs ~55 black-box tests against the POST /api/parse_css endpoint,
comparing results to CSS standard behavior. Each test is tagged as:

  PASS   - result matches the CSS standard
  XFAIL  - known deviation from standard (behaves as expected-wrong)
  FAIL   - unexpected result (regression or incorrect XFAIL assumption)

Usage:
    python css_parser_compliance.py [--host HOST] [--port PORT] [--verbose]

Requires a running HISE instance with the REST API server.
"""

import argparse
import json
import sys
import urllib.request
import urllib.error

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 1900
ENDPOINT = "/api/parse_css"

# ---------------------------------------------------------------------------
# Test infrastructure
# ---------------------------------------------------------------------------

class TestResult:
    def __init__(self):
        self.passed = 0
        self.xfailed = 0
        self.failed = 0
        self.errors = 0
        self.details = []

    def record(self, num, desc, status, msg=""):
        self.details.append((num, desc, status, msg))
        if status == "PASS":
            self.passed += 1
        elif status == "XFAIL":
            self.xfailed += 1
        elif status == "FAIL":
            self.failed += 1
        elif status == "ERROR":
            self.errors += 1

    def summary(self):
        total = self.passed + self.xfailed + self.failed + self.errors
        return (f"{total} tests: {self.passed} PASS, {self.xfailed} XFAIL, "
                f"{self.failed} FAIL, {self.errors} ERROR")


results = TestResult()
verbose = False


def post_parse_css(host, port, body):
    """Send a POST request to the parse_css endpoint."""
    url = f"http://{host}:{port}{ENDPOINT}"
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url, data=data,
                                headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8")
        try:
            return json.loads(body)
        except Exception:
            return {"_http_error": e.code, "_body": body}
    except Exception as e:
        return {"_exception": str(e)}


def check(num, desc, body, check_fn, xfail=False):
    """Run one test case.

    Args:
        num:      Test number for display.
        desc:     Human-readable description.
        body:     JSON body to POST.
        check_fn: callable(response) -> (ok: bool, detail: str).
                  Return (True, "") if the standard-compliant result is observed.
        xfail:    If True, we expect the check to FAIL (known parser bug).
    """
    resp = post_parse_css(HOST, PORT, body)
    if "_exception" in resp or "_http_error" in resp:
        tag = "ERROR"
        detail = resp.get("_exception", resp.get("_body", ""))
        results.record(num, desc, tag, detail)
        print(f"  [{tag}]  #{num:02d} {desc} -- {detail}")
        return

    ok, detail = check_fn(resp)

    if xfail:
        if ok:
            # Bug was fixed! Mark as unexpected PASS (shows as FAIL so we update the test).
            tag = "FAIL"
            detail = "XFAIL test now passes - bug may be fixed, update test expectation"
            results.record(num, desc, tag, detail)
        else:
            tag = "XFAIL"
            results.record(num, desc, tag, detail)
    else:
        tag = "PASS" if ok else "FAIL"
        results.record(num, desc, tag, detail)

    line = f"  [{tag:>5s}] #{num:02d} {desc}"
    if (tag in ("FAIL", "ERROR")) or (verbose and detail):
        line += f" -- {detail}"
    print(line)


# ---------------------------------------------------------------------------
# Helper check functions
# ---------------------------------------------------------------------------

def expect_success(resp):
    if not resp.get("success"):
        diags = resp.get("diagnostics", [])
        msg = diags[0]["message"] if diags else "unknown error"
        return False, f"parse failed: {msg}"
    return True, ""


def expect_parse_error(resp):
    if resp.get("success"):
        return False, "expected parse error but succeeded"
    return True, ""


def expect_property(resp, prop, expected_value):
    """Check that a resolved property equals expected_value."""
    props = resp.get("properties", {})
    actual = props.get(prop)
    if actual is None:
        return False, f"property '{prop}' missing (got: {json.dumps(props)})"
    if actual != expected_value:
        return False, f"'{prop}': expected {expected_value!r}, got {actual!r}"
    return True, ""


def expect_properties(resp, expected):
    """Check multiple properties. expected is a dict of {prop: value}."""
    props = resp.get("properties", {})
    errors = []
    for prop, exp_val in expected.items():
        actual = props.get(prop)
        if actual is None:
            errors.append(f"'{prop}' missing")
        elif actual != exp_val:
            errors.append(f"'{prop}': expected {exp_val!r}, got {actual!r}")
    if errors:
        return False, "; ".join(errors)
    return True, ""


def expect_resolved(resp, prop, expected_px, tolerance=0.5):
    """Check that a resolved pixel value is close to expected."""
    props = resp.get("properties", {})
    entry = props.get(prop)
    if entry is None:
        return False, f"property '{prop}' missing"
    if isinstance(entry, dict):
        resolved = entry.get("resolved")
        if resolved is None:
            return False, f"'{prop}' has no 'resolved' key"
        if abs(resolved - expected_px) > tolerance:
            return False, f"'{prop}': expected ~{expected_px}, got {resolved}"
        return True, ""
    else:
        return False, f"'{prop}': expected object with 'resolved', got {entry!r}"


def expect_selectors(resp, expected_selectors):
    """Check that the parsed selector list matches."""
    selectors = resp.get("selectors", [])
    if set(selectors) != set(expected_selectors):
        return False, f"expected selectors {expected_selectors}, got {selectors}"
    return True, ""


def expect_warning_for(resp, prop_name):
    """Check that there is a warning diagnostic mentioning the property."""
    diags = resp.get("diagnostics", [])
    for d in diags:
        if d.get("severity") == "warning" and prop_name in d.get("message", ""):
            return True, ""
    return False, f"no warning found for '{prop_name}'"


def css(code):
    """Shorthand: build a body with just CSS code."""
    return {"code": code}


def css_resolve(code, selectors, width=None, height=None):
    """Build a body with code, selectors, and optional size."""
    body = {"code": code, "selectors": selectors}
    if width is not None:
        body["width"] = width
    if height is not None:
        body["height"] = height
    return body


# ---------------------------------------------------------------------------
# Test definitions
# ---------------------------------------------------------------------------

def run_all_tests():
    # =======================================================================
    print("\n--- Colors ---")
    # =======================================================================

    check(1, "Named color: red",
          css_resolve(".t { color: red; }", [".t"]),
          lambda r: expect_property(r, "color", "0xFFFF0000"))

    check(2, "Named color: transparent",
          css_resolve(".t { color: transparent; }", [".t"]),
          lambda r: expect_property(r, "color", "0x00000000"))

    check(3, "3-digit hex: #F00",
          css_resolve(".t { color: #F00; }", [".t"]),
          lambda r: expect_property(r, "color", "0xFFFF0000"))

    check(4, "6-digit hex: #AABBCC",
          css_resolve(".t { color: #AABBCC; }", [".t"]),
          lambda r: expect_property(r, "color", "0xFFAABBCC"))

    # CSS standard: #RRGGBBAA. #FF000080 = red at ~50% alpha = 0x80FF0000
    check(5, "8-digit hex: #FF000080 (RRGGBBAA byte order)",
          css_resolve(".t { color: #FF000080; }", [".t"]),
          lambda r: expect_property(r, "color", "0x80FF0000"),
          xfail=True)

    # CSS standard: #RGBA expanded -> #RRGGBBAA. #0F08 = #00FF0088 = 0x8800FF00
    check(6, "4-digit hex: #0F08 (RGBA shorthand)",
          css_resolve(".t { color: #0F08; }", [".t"]),
          lambda r: expect_property(r, "color", "0x8800FF00"),
          xfail=True)

    check(7, "rgb() classic comma syntax",
          css_resolve(".t { color: rgb(255, 0, 0); }", [".t"]),
          lambda r: expect_property(r, "color", "0xFFFF0000"))

    check(8, "rgba() with alpha",
          css_resolve(".t { color: rgba(255, 0, 0, 0.5); }", [".t"]),
          lambda r: expect_property(r, "color", "0x80FF0000"))

    # rgb(100%, 0%, 0%) should be 0xFFFF0000. Percentages map 100% -> 255.
    check(9, "rgb() with percentage values",
          css_resolve(".t { color: rgb(100%, 0%, 0%); }", [".t"]),
          lambda r: expect_property(r, "color", "0xFFFF0000"),
          xfail=True)

    # Modern CSS Color Level 4: rgb(R G B / alpha)
    check(10, "rgb() space/slash syntax with alpha",
          css_resolve(".t { color: rgb(255 0 0 / 0.5); }", [".t"]),
          lambda r: expect_property(r, "color", "0x80FF0000"),
          xfail=True)

    # hsl(0, 100%, 50%) = pure red
    check(11, "hsl(0, 100%, 50%) should be red",
          css_resolve(".t { color: hsl(0, 100%, 50%); }", [".t"]),
          lambda r: expect_property(r, "color", "0xFFFF0000"),
          xfail=True)

    # hsl(120, 100%, 50%) = pure green
    check(12, "hsl(120, 100%, 50%) should be green",
          css_resolve(".t { color: hsl(120, 100%, 50%); }", [".t"]),
          lambda r: expect_property(r, "color", "0xFF00FF00"),
          xfail=True)

    # hsla() should resolve to a color value, not be stored as raw string
    check(13, "hsla() should resolve to color value",
          css_resolve(".t { color: hsla(0, 100%, 50%, 0.5); }", [".t"]),
          lambda r: expect_property(r, "color", "0x80FF0000"),
          xfail=True)

    # =======================================================================
    print("\n--- Borders & Border Radius ---")
    # =======================================================================

    check(14, "border shorthand: 2px solid red",
          css_resolve(".t { border: 2px solid red; }", [".t"]),
          lambda r: expect_properties(r, {
              "border-width": "2px",
              "border-style": "solid",
              "border-color": "0xFFFF0000",
          }))

    check(15, "border: none",
          css_resolve(".t { border: none; }", [".t"]),
          lambda r: expect_property(r, "border", "none"))

    check(16, "border-radius: 10px (1-value)",
          css_resolve(".t { border-radius: 10px; }", [".t"]),
          lambda r: expect_properties(r, {
              "border-top-left-radius": "10px",
              "border-top-right-radius": "10px",
              "border-bottom-right-radius": "10px",
              "border-bottom-left-radius": "10px",
          }))

    # 2-value: first = TL + BR, second = TR + BL
    check(17, "border-radius: 10px 20px (2-value)",
          css_resolve(".t { border-radius: 10px 20px; }", [".t"]),
          lambda r: expect_properties(r, {
              "border-top-left-radius": "10px",
              "border-top-right-radius": "20px",
              "border-bottom-right-radius": "10px",
              "border-bottom-left-radius": "20px",
          }))

    # 3-value: TL=10 TR=20 BR=30 BL=20 (BL mirrors TR)
    check(18, "border-radius: 10px 20px 30px (3-value)",
          css_resolve(".t { border-radius: 10px 20px 30px; }", [".t"]),
          lambda r: expect_properties(r, {
              "border-top-left-radius": "10px",
              "border-top-right-radius": "20px",
              "border-bottom-right-radius": "30px",
              "border-bottom-left-radius": "20px",
          }),
          xfail=True)

    # 4-value: TL TR BR BL (same TRBL ordering bug as margin/padding)
    check(19, "border-radius: 10px 20px 30px 40px (4-value)",
          css_resolve(".t { border-radius: 10px 20px 30px 40px; }", [".t"]),
          lambda r: expect_properties(r, {
              "border-top-left-radius": "10px",
              "border-top-right-radius": "20px",
              "border-bottom-right-radius": "30px",
              "border-bottom-left-radius": "40px",
          }),
          xfail=True)

    # Elliptical border-radius with slash notation
    check(20, "border-radius: 10px / 20px (elliptical)",
          css_resolve(".t { border-radius: 10px / 20px; }", [".t"]),
          lambda r: (
              (True, "") if r.get("properties", {}) else
              (False, "properties empty - elliptical radius silently dropped")
          ),
          xfail=True)

    check(21, "border-radius: 50% (circle)",
          css_resolve(".t { border-radius: 50%; }", [".t"]),
          lambda r: expect_properties(r, {
              "border-top-left-radius": "50%",
              "border-top-right-radius": "50%",
              "border-bottom-right-radius": "50%",
              "border-bottom-left-radius": "50%",
          }))

    # =======================================================================
    print("\n--- Shorthand Expansion (TRBL) ---")
    # =======================================================================

    check(22, "margin: 10px (1-value, all sides)",
          css_resolve(".t { margin: 10px; }", [".t"]),
          lambda r: expect_properties(r, {
              "margin-top": "10px",
              "margin-right": "10px",
              "margin-bottom": "10px",
              "margin-left": "10px",
          }))

    # 2-value: T/B = first, L/R = second
    check(23, "margin: 10px 20px (2-value)",
          css_resolve(".t { margin: 10px 20px; }", [".t"]),
          lambda r: expect_properties(r, {
              "margin-top": "10px",
              "margin-right": "20px",
              "margin-bottom": "10px",
              "margin-left": "20px",
          }))

    # 3-value: T = first, L/R = second, B = third
    check(24, "margin: 10px 20px 30px (3-value)",
          css_resolve(".t { margin: 10px 20px 30px; }", [".t"]),
          lambda r: expect_properties(r, {
              "margin-top": "10px",
              "margin-right": "20px",
              "margin-bottom": "30px",
              "margin-left": "20px",
          }))

    # 4-value: T R B L (clockwise from top)
    check(25, "margin: 10px 20px 30px 40px (4-value TRBL)",
          css_resolve(".t { margin: 10px 20px 30px 40px; }", [".t"]),
          lambda r: expect_properties(r, {
              "margin-top": "10px",
              "margin-right": "20px",
              "margin-bottom": "30px",
              "margin-left": "40px",
          }),
          xfail=True)

    check(26, "padding: 5px 10px 15px 20px (4-value TRBL)",
          css_resolve(".t { padding: 5px 10px 15px 20px; }", [".t"]),
          lambda r: expect_properties(r, {
              "padding-top": "5px",
              "padding-right": "10px",
              "padding-bottom": "15px",
              "padding-left": "20px",
          }),
          xfail=True)

    # border-width 4-value should expand to -top/-right/-bottom/-left
    check(27, "border-width: 1px 2px 3px 4px (4-value TRBL)",
          css_resolve(".t { border-width: 1px 2px 3px 4px; }", [".t"]),
          lambda r: expect_properties(r, {
              "border-top-width": "1px",
              "border-right-width": "2px",
              "border-bottom-width": "3px",
              "border-left-width": "4px",
          }),
          xfail=True)

    # border-style 4-value
    check(28, "border-style: solid dashed dotted double (4-value)",
          css_resolve(".t { border-style: solid dashed dotted double; }", [".t"]),
          lambda r: expect_properties(r, {
              "border-top-style": "solid",
              "border-right-style": "dashed",
              "border-bottom-style": "dotted",
              "border-left-style": "double",
          }),
          xfail=True)

    # border-color 4-value
    check(29, "border-color: red green blue yellow (4-value)",
          css_resolve(".t { border-color: red green blue yellow; }", [".t"]),
          lambda r: expect_properties(r, {
              "border-top-color": "0xFFFF0000",
              "border-right-color": "0xFF008000",
              "border-bottom-color": "0xFF0000FF",
              "border-left-color": "0xFFFFFF00",
          }),
          xfail=True)

    # =======================================================================
    print("\n--- Box Shadow ---")
    # =======================================================================

    # We check that box-shadow parses and produces a non-empty value string.
    # The internal format is HISE-specific, so we just verify it's present.

    check(30, "box-shadow: basic outer shadow",
          css_resolve(".t { box-shadow: 5px 5px 10px black; }", [".t"]),
          lambda r: (
              (True, "") if "box-shadow" in r.get("properties", {}) else
              (False, "box-shadow missing")
          ))

    check(31, "box-shadow: inset shadow",
          css_resolve(".t { box-shadow: inset 0 0 10px rgba(0,0,0,0.5); }", [".t"]),
          lambda r: (
              (True, "") if "inset" in r.get("properties", {}).get("box-shadow", "") else
              (False, "inset keyword not found in box-shadow value")
          ))

    check(32, "box-shadow: multiple shadows (comma-separated)",
          css_resolve(".t { box-shadow: 2px 2px 5px red, -2px -2px 5px blue; }", [".t"]),
          lambda r: (
              (True, "") if r.get("properties", {}).get("box-shadow", "").count("|") >= 2 else
              (False, "expected two shadow entries separated by |")
          ))

    check(33, "box-shadow: with spread radius",
          css_resolve(".t { box-shadow: 5px 5px 10px 2px rgba(0,0,0,0.3); }", [".t"]),
          lambda r: (
              (True, "") if "2px" in r.get("properties", {}).get("box-shadow", "") else
              (False, "spread radius not found in box-shadow value")
          ))

    # =======================================================================
    print("\n--- Typography ---")
    # =======================================================================

    check(34, "Individual font properties",
          css_resolve(".t { font-size: 14px; font-weight: bold; font-family: Arial; }", [".t"]),
          lambda r: expect_properties(r, {
              "font-size": "14px",
              "font-weight": "bold",
              "font-family": "Arial",
          }))

    check(35, "Text properties: align, spacing, transform",
          css_resolve(".t { text-align: center; letter-spacing: 2px; text-transform: uppercase; }", [".t"]),
          lambda r: expect_properties(r, {
              "text-align": "center",
              "letter-spacing": "2px",
              "text-transform": "uppercase",
          }))

    # font shorthand is not supported - produces a warning (no properties resolved)
    check(36, "font shorthand: unsupported (warns correctly)",
          css_resolve(".t { font: 14px Arial; }", [".t"]),
          lambda r: expect_warning_for(r, "font"))

    # font-style stores value but warns
    check(37, "font-style: italic (warns but stores)",
          css_resolve(".t { font-style: italic; }", [".t"]),
          lambda r: expect_property(r, "font-style", "italic"))

    # text-decoration stores value but warns
    check(38, "text-decoration: underline (warns but stores)",
          css_resolve(".t { text-decoration: underline; }", [".t"]),
          lambda r: expect_property(r, "text-decoration", "underline"))

    # =======================================================================
    print("\n--- Gradients & Backgrounds ---")
    # =======================================================================

    check(39, "linear-gradient(to bottom, red, blue)",
          css_resolve(".t { background: linear-gradient(to bottom, red, blue); }", [".t"]),
          lambda r: (
              (True, "") if "linear-gradient" in r.get("properties", {}).get("background-color", "") else
              (False, f"gradient not stored: {r.get('properties', {})}")
          ))

    check(40, "linear-gradient with angle and color stops",
          css_resolve(".t { background: linear-gradient(45deg, red 0%, yellow 50%, blue 100%); }", [".t"]),
          lambda r: (
              (True, "") if "45deg" in r.get("properties", {}).get("background-color", "") else
              (False, f"angle not found: {r.get('properties', {})}")
          ))

    # background shorthand with gradient + fallback color
    # The fallback color should be preserved; background value should not be just ","
    check(41, "background: gradient + fallback color",
          css_resolve(".t { background: linear-gradient(to right, red, blue), green; }", [".t"]),
          lambda r: (
              (lambda props: (
                  (True, "") if (
                      "linear-gradient" in props.get("background", props.get("background-color", ""))
                      and props.get("background", "") != ","
                  ) else (False, f"gradient+fallback broken: {props}")
              ))(r.get("properties", {}))
          ),
          xfail=True)

    check(42, "background: none",
          css_resolve(".t { background: none; }", [".t"]),
          lambda r: expect_property(r, "background", "none"))

    check(43, "background-color: simple color",
          css_resolve(".t { background-color: #336699; }", [".t"]),
          lambda r: expect_property(r, "background-color", "0xFF336699"))

    # =======================================================================
    print("\n--- calc() ---")
    # =======================================================================

    check(44, "calc(100% - 20px) = 380px",
          css_resolve(".t { width: calc(100% - 20px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 380.0))

    check(45, "calc(50% + 10px) = 210px",
          css_resolve(".t { width: calc(50% + 10px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 210.0))

    check(46, "calc(2 * 50px) = 100px",
          css_resolve(".t { width: calc(2 * 50px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 100.0))

    check(47, "calc(100% / 3) = 133px",
          css_resolve(".t { width: calc(100% / 3); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 133.3, tolerance=1.0))

    check(48, "calc(100% * 0.5) = 200px",
          css_resolve(".t { width: calc(100% * 0.5); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 200.0))

    # Chained operators: only binary expressions work
    check(49, "calc(100% - 20px - 10px) = 370px [chained subtract]",
          css_resolve(".t { width: calc(100% - 20px - 10px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 370.0),
          xfail=True)

    check(50, "calc(100% - 2 * 20px) = 360px [mixed operators]",
          css_resolve(".t { width: calc(100% - 2 * 20px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 360.0),
          xfail=True)

    check(51, "calc(100% / 3 - 10px) = 90px [mixed operators]",
          css_resolve(".t { width: calc(100% / 3 - 10px); }", [".t"], 300, 300),
          lambda r: expect_resolved(r, "width", 90.0),
          xfail=True)

    check(52, "calc(50% + 20px + 10px) = 230px [chained add]",
          css_resolve(".t { width: calc(50% + 20px + 10px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 230.0),
          xfail=True)

    # =======================================================================
    print("\n--- min() / max() / clamp() ---")
    # =======================================================================

    check(53, "min(50%, 200px) = 200px",
          css_resolve(".t { width: min(50%, 200px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 200.0))

    check(54, "max(50%, 300px) = 300px",
          css_resolve(".t { width: max(50%, 300px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 300.0))

    check(55, "clamp(100px, 50%, 300px) = 200px",
          css_resolve(".t { width: clamp(100px, 50%, 300px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 200.0))

    check(56, "clamp(200px, 10%, 300px) = 200px (preferred < min)",
          css_resolve(".t { width: clamp(200px, 10%, 300px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 200.0))

    check(57, "min() with 3 arguments",
          css_resolve(".t { width: min(50%, 200px, 150px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 150.0))

    check(58, "max() with 3 arguments",
          css_resolve(".t { width: max(10%, 50px, 30%); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 120.0))

    check(59, "calc(min(50%, 200px) + 10px) = 210px [nested]",
          css_resolve(".t { width: calc(min(50%, 200px) + 10px); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 210.0))

    check(60, "calc(max(10%, 50px) + min(20%, 100px)) = 130px [nested]",
          css_resolve(".t { width: calc(max(10%, 50px) + min(20%, 100px)); }", [".t"], 400, 300),
          lambda r: expect_resolved(r, "width", 130.0))

    # =======================================================================
    print("\n--- Selectors ---")
    # =======================================================================

    check(61, "Child combinator: .parent > .child",
          css(".parent > .child { color: red; }"),
          lambda r: expect_success(r),
          xfail=True)

    check(62, "Selector grouping: .a, .b",
          css(".a, .b { color: red; }"),
          lambda r: (
              expect_selectors(r, [".a", ".b"])
              if r.get("success") else
              (False, "parse failed")
          ))

    check(63, ":root selector (for custom properties)",
          css(":root { --my-color: red; }"),
          lambda r: expect_success(r),
          xfail=True)

    check(64, "Pseudo-class :hover parses",
          css(".btn:hover { color: red; }"),
          lambda r: expect_success(r))

    # =======================================================================
    print("\n--- Specificity Resolution ---")
    # =======================================================================

    check(65, "ID beats class: #submit vs .btn padding",
          css_resolve(
              ".btn { padding: 10px; } #submit { padding: 20px; }",
              ["#submit", ".btn"]),
          lambda r: expect_property(r, "padding-top", "20px"))

    check(66, "Later rule wins at same specificity",
          css_resolve(
              ".a { color: red; } .a { color: blue; }",
              [".a"]),
          lambda r: expect_property(r, "color", "0xFF0000FF"))

    check(67, "Compound selector .a.b matches component with both classes",
          css_resolve(
              ".a { color: red; } .b { color: blue; } .a.b { background: green; }",
              [".a", ".b"]),
          lambda r: expect_property(r, "background-color", "0xFF008000"))

    # =======================================================================
    print("\n--- Real-World Patterns ---")
    # =======================================================================

    check(68, "Glassmorphism card style",
          css_resolve(
              ".card { "
              "background-color: rgba(255,255,255,0.1); "
              "border: 1px solid rgba(255,255,255,0.2); "
              "border-radius: 8px; "
              "box-shadow: inset 0 1px 0 rgba(255,255,255,0.05); "
              "}",
              [".card"]),
          lambda r: (
              (lambda p: (
                  (True, "") if (
                      p.get("background-color") == "0x1AFFFFFF"
                      and p.get("border-color") == "0x33FFFFFF"
                      and "border-top-left-radius" in p
                      and "box-shadow" in p
                  ) else (False, f"missing expected props: {p}")
              ))(r.get("properties", {}))
          ))

    check(69, "Dark theme button",
          css_resolve(
              ".btn { "
              "color: #FFFFFF; "
              "background-color: #333333; "
              "border: 1px solid #555555; "
              "border-radius: 4px; "
              "font-size: 13px; "
              "letter-spacing: 1px; "
              "text-transform: uppercase; "
              "}",
              [".btn"]),
          lambda r: expect_properties(r, {
              "color": "0xFFFFFFFF",
              "background-color": "0xFF333333",
              "border-color": "0xFF555555",
              "font-size": "13px",
              "text-transform": "uppercase",
          }))

    # Individual border-side shorthands parse (with warning) and expand
    check(70, "border-top/border-bottom side shorthands",
          css_resolve(
              ".t { border-top: 2px solid red; border-bottom: 3px dashed blue; }",
              [".t"]),
          lambda r: expect_properties(r, {
              "border-top-width": "2px",
              "border-top-style": "solid",
              "border-top-color": "0xFFFF0000",
              "border-bottom-width": "3px",
              "border-bottom-style": "dashed",
              "border-bottom-color": "0xFF0000FF",
          }))

    # =======================================================================
    print("\n--- Transition (drawing-related) ---")
    # =======================================================================

    # Single transition should produce transition properties
    check(71, "transition: single property",
          css_resolve(".t { transition: color 0.3s ease; }", [".t"]),
          lambda r: (
              (True, "") if r.get("properties", {}) else
              (False, "transition produced no properties")
          ),
          xfail=True)

    # Multi-value transition
    check(72, "transition: multiple properties (comma-separated)",
          css_resolve(".t { transition: color 0.3s ease, background 0.2s linear; }", [".t"]),
          lambda r: (
              (True, "") if r.get("properties", {}) else
              (False, "multi-transition produced no properties")
          ),
          xfail=True)

    # =======================================================================
    print("\n--- CSS Keywords ---")
    # =======================================================================

    # inherit and initial are fundamental CSS keywords
    check(73, "color: inherit",
          css_resolve(".t { color: inherit; }", [".t"]),
          lambda r: expect_property(r, "color", "inherit"),
          xfail=True)

    check(74, "background: initial",
          css_resolve(".t { background: initial; }", [".t"]),
          lambda r: expect_property(r, "background", "initial"),
          xfail=True)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    global HOST, PORT, verbose

    parser = argparse.ArgumentParser(
        description="CSS parser compliance tests for HISE simple_css")
    parser.add_argument("--host", default=DEFAULT_HOST,
                        help=f"HISE server host (default: {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"HISE server port (default: {DEFAULT_PORT})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Show detail for all tests, not just failures")
    args = parser.parse_args()

    HOST = args.host
    PORT = args.port
    verbose = args.verbose

    # Connectivity check
    try:
        resp = post_parse_css(HOST, PORT, {"code": ".ping { color: red; }"})
        if "_exception" in resp:
            print(f"ERROR: Cannot connect to HISE at {HOST}:{PORT}")
            print(f"       {resp['_exception']}")
            print(f"       Is HISE running with the REST API server?")
            sys.exit(2)
    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(2)

    print(f"CSS Parser Compliance Tests - HISE @ {HOST}:{PORT}")
    print("=" * 60)

    run_all_tests()

    print("\n" + "=" * 60)
    print(results.summary())

    if results.failed > 0 or results.errors > 0:
        print("\nFailed/Error tests:")
        for num, desc, status, msg in results.details:
            if status in ("FAIL", "ERROR"):
                print(f"  #{num:02d} {desc}: {msg}")
        sys.exit(1)
    else:
        sys.exit(0)


if __name__ == "__main__":
    main()
