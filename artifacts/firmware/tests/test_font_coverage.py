"""CI: audit all firmware C++ literals, every generated cmap and synced UTF-8."""
from pathlib import Path
import importlib.util
import re
import subprocess
import tempfile
import sys
import ast

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("generator", ROOT / "tools/generate_fonts.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
required = set(range(0xA0, 0x180)) | set(map(ord, generator.SYMBOLS))
policy = (ROOT / "main/ui/font_coverage.h").read_text()
expression = re.search(r"return\s+(.*?);", policy, re.S)[1].replace("&&", " and ").replace("||", " or ")
tree = ast.parse(" ".join(expression.split()), mode="eval")
assert all(isinstance(n, (ast.Expression, ast.BoolOp, ast.And, ast.Or, ast.Compare,
    ast.Name, ast.Load, ast.Constant, ast.GtE, ast.LtE, ast.Eq)) for n in ast.walk(tree))
compiled_policy = compile(tree, "ui_codepoint_supported", "eval")
def supported(cp):
    return eval(compiled_policy, {"__builtins__": {}}, {"cp": cp})
assert all(supported(cp) for cp in required), "font_coverage.h disagrees with generated fallback policy"
errors = []
tokens = re.compile(r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\n]{0,16})\((?P<raw>.*?)\)(?P=delimiter)"|(?:u8|u|U|L)?"(?:[^"\\]|\\.)*"', re.S)
for path in (ROOT / "main").rglob("*.cpp"):
    text = path.read_text()
    for token in tokens.finditer(text):
        if token[0].startswith(("//","/*")):
            continue
        value=token.group("raw") if token.group("raw") is not None else token[0]
        value=re.sub(r'\\u([0-9a-fA-F]{4})|\\U([0-9a-fA-F]{8})',lambda m:chr(int(m[1] or m[2],16)),value)
        for char in value:
            if ord(char) > 127 and (ord(char) not in required or not supported(ord(char))):
                errors.append(f"{path.relative_to(ROOT)}:{text.count(chr(10),0,token.start())+1}: U+{ord(char):04X} {char}")
    if path.parent.name == "ui" and path.name.startswith("screen_"):
        if "lv_font_montserrat_" in text:
            errors.append(f"{path}: direct font bypasses UI_FONT fallback")
        if "lv_obj_create(NULL)" in text or "lv_obj_create(nullptr)" in text:
            if "screen_base_init(" not in text:
                errors.append(f"{path}: root has no shared fallback default")
for size in generator.SIZES:
    path = ROOT / f"main/ui/ui_font_fallback_{size}.c"
    text = path.read_text()
    maps = re.search(r"cmaps\[\]\s*=\s*\{(.*?)\n\};", text, re.S)[1]
    coverage = set()
    for entry in re.findall(r"\{([^{}]+)\}", maps):
        start = int(re.search(r"\.range_start\s*=\s*(\d+)", entry)[1])
        length = int(re.search(r"\.range_length\s*=\s*(\d+)", entry)[1])
        sparse = re.search(r"\.unicode_list\s*=\s*(\w+)", entry)[1]
        if sparse in ("NULL", "0"):
            coverage.update(range(start, start + length))
        else:
            values = re.search(rf"{sparse}\[\]\s*=\s*\{{(.*?)\}}", text, re.S)[1]
            coverage.update(start + int(n, 0) for n in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", values))
    for cp in sorted(required - coverage):
        errors.append(f"{path.name}: missing U+{cp:04X}")
    if '#include "lvgl.h"' not in text or '"lvgl/lvgl.h"' in text:
        errors.append(f"{path.name}: wrong LVGL include path")
assert not errors, "\n".join(errors)
if "--check-only" in sys.argv:
    print("PASS build-time literal/font coverage")
    sys.exit(0)

cpp = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include "display_text.h"
#include "font_coverage.h"
#include "terminal_logs.h"
static unsigned reports;
void terminal_log_add(const char*,const char*,const char*,const cJSON*) {reports++;}
int main(){
 char out[512];
 const char *names="Maître: José Muñoz, Zoë, François, Çağlar, Œ, João, D’Ávila, “Élodie”…";
 utf8_to_display(out,names,sizeof(out));assert(!strcmp(out,names)&&reports==0);
 utf8_to_display(out,"A🧪B中C",sizeof(out));assert(!strcmp(out,"A?B?C")&&reports==1);
 utf8_to_display(out,"\xF0\x80\x80\x80",sizeof(out));assert(!strcmp(out,"????"));
 utf8_to_display(out,"\xC3",sizeof(out));assert(!strcmp(out,"?"));
 utf8_to_display(out,"éé",4);assert(!strcmp(out,"é"));
 utf8_to_display(out,nullptr,sizeof(out));assert(!*out);
 utf8_to_display(out,"x",1);assert(!*out);
 assert(display_name_matches("JOSÉ Muñoz","josé")&&display_name_matches("Zoë","zoe"));
 assert(display_name_matches("François","FRANÇOIS")&&display_name_matches("Çağlar","çaĞLAR"));
 assert(!display_name_matches("François","Zoë"));
 for(unsigned cp=0x80;cp<0x180;cp++)assert(ui_codepoint_supported(cp)==(cp>=0xa0));
 puts("PASS synced names/products/club names/notes: accents preserved; invalid/unsupported scalars become counted '?'; UTF-8 boundaries safe");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "cJSON.h").write_text("#pragma once\ntypedef struct cJSON cJSON;\n")
    (path / "nvs.h").write_text("#pragma once\n#include <stdint.h>\ntypedef uint32_t nvs_handle_t;\n")
    (path / "test.cpp").write_text(cpp)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(path), "-I", str(ROOT / "main/ui"), "-I", str(ROOT / "main/store"),
        str(path / "test.cpp"), str(ROOT / "main/ui/display_text.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print(f"PASS all firmware C++ literals, all screen roots and {len(generator.SIZES)} fallback cmap sizes: U+00A0–017F + symbols")
