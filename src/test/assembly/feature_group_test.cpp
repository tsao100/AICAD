#include "Feature.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <cstdio>
using namespace aicad::cad;
struct F : Feature {
    const char* ts;
    explicit F(const char* t = "Extrude") : Feature(nullptr), ts(t) {}
    FeatureType type() const override { return FeatureType::Extrude; }
    QString typeString() const override { return ts; }
    bool rebuild() override { return true; }
};
static int bad = 0;
#define CHECK(c, m) do { if (!(c)) { ++bad; std::printf("FAIL %s\n", m); } } while (0)
static QJsonObject legacy(const char* name, const char* group = nullptr, int ver = 1)
{
    QJsonObject j; j["id"] = "{11111111-1111-1111-1111-111111111111}"; j["name"] = name;
    if (group) { j["group"] = group; if (ver > 1) j["groupVer"] = ver; }
    return j;
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    for (const char* n : { "void", "Void", "VOID", "void1", "void_2", "void 3", "VOID-4" }) CHECK(Feature::isVoidName(n), n);
    for (const char* n : { "voider", "avoid", "Extrude1", "void_a", "" }) CHECK(!Feature::isVoidName(n), n);

    // 新檔（groupVer 2）：三個根都能來回
    for (FeatureGroup g : { FeatureGroup::Part, FeatureGroup::Assembly, FeatureGroup::Void }) {
        F f; f.setGroupRaw(g); QJsonObject j = f.toJson();
        CHECK(j["groupVer"].toInt() == 2, "groupVer written");
        F h; h.fromJson(j); CHECK(h.group() == g, "group round trip");
    }
    // 預設：造型特徵 → Part
    { F f; CHECK(f.group() == FeatureGroup::Part && !f.isVoid(), "default Part"); }
    // 新檔：明確 group 優先於名稱
    { F g; g.fromJson(legacy("void", "assembly", 2)); CHECK(g.group() == FeatureGroup::Assembly, "explicit group wins over name"); }
    // 上一版（groupVer 1：只有 Void／Assembly 兩個根）
    { F g; g.fromJson(legacy("Extrude1", "assembly")); CHECK(g.group() == FeatureGroup::Part, "v1 'assembly' on a shaping feature -> Part"); }
    { F g("AssemblyPart"); g.fromJson(legacy("Part1", "assembly")); CHECK(g.group() == FeatureGroup::Assembly, "v1 'assembly' on a part -> Assembly"); }
    { F g; g.fromJson(legacy("Extrude2", "void")); CHECK(g.group() == FeatureGroup::Void, "v1 void stays Void"); }
    // 更舊的檔（沒有 group 欄位）：依名稱
    { F g; g.fromJson(legacy("void2")); CHECK(g.group() == FeatureGroup::Void, "legacy name 'void2' -> Void"); }
    { F g; g.fromJson(legacy("Extrude3")); CHECK(g.group() == FeatureGroup::Part, "legacy normal name -> Part"); }
    std::printf(bad ? "FAILED\n" : "feature group tests passed\n");
    return bad;
}
