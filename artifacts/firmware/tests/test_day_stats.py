#!/usr/bin/env python3
"""Exercise actual Tagesstatistik presentation updates with host LVGL mocks."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("gateway_fixture", ROOT.parent / "lora-gateway/tests/test_nonce.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)

STUBS = r"""
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <climits>
#include <string>
#include <vector>
#include <array>
constexpr int MAX_BILL_CATEGORIES = 8, MAX_DAY_PRODUCTS = 32;
constexpr int VERKAUF_UNIT_PRICE_UNKNOWN = -1;
constexpr int CLR_WARN = 1, CLR_MUTED = 2;
struct lv_obj_t { std::string label; std::vector<std::array<std::string, 3>> rows; };
struct BillLine { char produktName[64]; int quantity, unitPriceCent, lineTotalCent; bool localPending; };
struct Category { char name[24]; int totalCent; };
struct Day {
    char datum[11]; int uniquePlayers, paidPlayers, games, completedGames, confirmedClays, generalTotalCent;
    bool authoritative, productOverflow; int categoryCount, productCount;
    Category categories[MAX_BILL_CATEGORIES]; BillLine products[MAX_DAY_PRODUCTS];
};
using BillDaySummary = Day;
struct { Day billDay; } g_store;
static unsigned table_writes = 0;
const char *lv_label_get_text(lv_obj_t *label) { return label->label.c_str(); }
void lv_label_set_text(lv_obj_t *label, const char *text) { label->label = text; }
const char *lv_table_get_cell_value(lv_obj_t *obj, uint32_t row, uint32_t col) {
    return obj->rows.at(row).at(col).c_str();
}
void lv_table_set_cell_value(lv_obj_t *obj, uint32_t row, uint32_t col, const char *text) {
    obj->rows.at(row).at(col) = text; ++table_writes;
}
uint32_t lv_table_get_row_cnt(lv_obj_t *obj) { return obj->rows.size(); }
void lv_table_set_row_cnt(lv_obj_t *obj, uint32_t rows) { obj->rows.resize(rows); }
int lv_color_hex(int color) { return color; }
void lv_obj_set_style_text_color(lv_obj_t *, int, int) {}
static lv_obj_t *s_bill_day_summary, *s_day_values[6], *s_day_categories, *s_day_products, *s_day_warning;
"""

MAIN = r"""
int main() {
    lv_obj_t summary, values[6], categories, products, warning;
    s_bill_day_summary = &summary; s_day_categories = &categories;
    s_day_products = &products; s_day_warning = &warning;
    for (int i = 0; i < 6; ++i) s_day_values[i] = &values[i];
    refresh_bill_day_summary();
    assert(values[0].label == "--" && summary.label.find("Noch keine") != std::string::npos);
    auto &day = g_store.billDay;
    strcpy(day.datum, "2026-10-04"); day.generalTotalCent = 1250;
    day.uniquePlayers = 5; day.games = 8; day.completedGames = 7; day.confirmedClays = 175; day.paidPlayers = 2;
    day.categoryCount = 1; strcpy(day.categories[0].name, "GETRAENKE"); day.categories[0].totalCent = 1250;
    day.productCount = MAX_DAY_PRODUCTS;
    for (int i = 0; i < MAX_DAY_PRODUCTS; ++i) {
        snprintf(day.products[i].produktName, sizeof(day.products[i].produktName), "Produkt mit langem Namen %d", i);
        day.products[i].quantity = i + 1; day.products[i].unitPriceCent = 125;
        day.products[i].lineTotalCent = (i + 1) * 125;
    }
    day.products[0].localPending = true; day.products[0].unitPriceCent = VERKAUF_UNIT_PRICE_UNKNOWN;
    refresh_bill_day_summary();
    assert(summary.label.find("04.10.2026") != std::string::npos && summary.label.find("Offline-Cache") != std::string::npos);
    assert(values[0].label == "12,50 EUR" && values[4].label == "175" && values[5].label == "2");
    assert(categories.rows[1][1] == "12,50 EUR");
    assert(products.rows.size() == MAX_DAY_PRODUCTS + 1 && products.rows.back()[0].find("31") != std::string::npos);
    assert(products.rows[1][0].find("Ausstehend") != std::string::npos);
    assert(products.rows[1][1].find("Preis unbekannt") != std::string::npos && products.rows[1][2] == "Abgleich");
    unsigned writes = table_writes; refresh_bill_day_summary(); assert(table_writes == writes);
    day.productOverflow = true; refresh_bill_day_summary(); assert(warning.label.find("Detail-Limit") != std::string::npos);
    char money[32]; day_money(money, sizeof(money), -50); assert(std::string(money) == "-0,50 EUR");
    day_money(money, sizeof(money), INT_MIN); assert(std::string(money) == "-21474836,48 EUR");
    day.productCount = day.categoryCount = 0; refresh_bill_day_summary();
    assert(products.rows.size() == 2 && products.rows[1][0] == "Keine Produkte verkauft");
    puts("PASS: six actual totals, German amounts/date, offline and pending/unknown-price warnings");
    puts("PASS: all product rows visible without text-buffer truncation; unchanged cells not rewritten; bounded empty/overflow states");
}
"""

if __name__ == "__main__":
    source = (ROOT / "main/ui/screen_einstellungen.cpp").read_text()
    functions = "\n".join(fixture.function(source, name) for name in [
        "static void day_money", "static void day_table_cell",
        "static void set_label_text_if_changed", "static void refresh_bill_day_summary"])
    with tempfile.TemporaryDirectory(prefix="tm-day-stats-") as directory:
        path = Path(directory)
        (path / "test.cpp").write_text(STUBS + functions + MAIN)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(path / "test.cpp"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)