"""Reproducible Latin fallback generation (pnpm dlx pins lv_font_conv)."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SYMBOLS = "€–—‘’“”„…•°²³×→≈─⚠"
SIZES = (12, 14, 16, 18, 20, 22, 24, 28, 36, 48)
if __name__ == "__main__":
    for size in SIZES:
        subprocess.run(["pnpm", "dlx", "lv_font_conv@1.5.3", "--size", str(size),
            "--bpp", "4", "--format", "lvgl", "--font",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "--range", "0xA0-0x17F", "--symbols", SYMBOLS, "--no-kerning",
            "--no-compress", "--no-prefilter", "--lv-include", "lvgl.h",
            "--lv-font-name", f"ui_font_fallback_{size}", "-o",
            str(ROOT / f"main/ui/ui_font_fallback_{size}.c")], check=True)
        output = ROOT / f"main/ui/ui_font_fallback_{size}.c"
        output.write_text(output.read_text().rstrip() + "\n")
