#include "Core/AutoRig.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <exception>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace LunaFlow {
namespace {
constexpr uint32_t Unreached = 0xFFFFFFFFu;
constexpr uint32_t Ring = 511;   // Bucket ring; larger than the costliest step (17 * 30).
constexpr float Sigma = 10.0f;   // Weight falloff, in geodesic units (10 per cell).
constexpr uint32_t Cutoff = 120; // exp(-12): beyond this a bone's weight is negligible.
constexpr int Pad = 2;

enum Joint {
    JHips, JSpine, JChest, JNeck, JHead, JHeadTop,
    JLeftShoulder, JLeftUpperArm, JLeftForeArm, JLeftHand, JLeftHandEnd,
    JRightShoulder, JRightUpperArm, JRightForeArm, JRightHand, JRightHandEnd,
    JLeftUpperLeg, JLeftLowerLeg, JLeftFoot, JLeftToes, JLeftToesEnd,
    JRightUpperLeg, JRightLowerLeg, JRightFoot, JRightToes, JRightToesEnd,
    JointCount
};
struct JointInfo { const char* name; int parent; bool weighted; };
constexpr JointInfo Joints[JointCount] = {
    {"Hips", -1, true}, {"Spine", JHips, true}, {"Chest", JSpine, true}, {"Neck", JChest, true},
    {"Head", JNeck, true}, {"HeadTop_End", JHead, false},
    {"LeftShoulder", JChest, true}, {"LeftUpperArm", JLeftShoulder, true}, {"LeftForeArm", JLeftUpperArm, true},
    {"LeftHand", JLeftForeArm, true}, {"LeftHand_End", JLeftHand, false},
    {"RightShoulder", JChest, true}, {"RightUpperArm", JRightShoulder, true}, {"RightForeArm", JRightUpperArm, true},
    {"RightHand", JRightForeArm, true}, {"RightHand_End", JRightHand, false},
    {"LeftUpperLeg", JHips, true}, {"LeftLowerLeg", JLeftUpperLeg, true}, {"LeftFoot", JLeftLowerLeg, true},
    {"LeftToes", JLeftFoot, true}, {"LeftToes_End", JLeftToes, false},
    {"RightUpperLeg", JHips, true}, {"RightLowerLeg", JRightUpperLeg, true}, {"RightFoot", JRightLowerLeg, true},
    {"RightToes", JRightFoot, true}, {"RightToes_End", JRightToes, false},
};
// Left/right joint pairs used for symmetrizing a symmetric character.
constexpr std::array<std::pair<int, int>, 10> Mirrors{{
    {JLeftShoulder, JRightShoulder}, {JLeftUpperArm, JRightUpperArm}, {JLeftForeArm, JRightForeArm},
    {JLeftHand, JRightHand}, {JLeftHandEnd, JRightHandEnd}, {JLeftUpperLeg, JRightUpperLeg},
    {JLeftLowerLeg, JRightLowerLeg}, {JLeftFoot, JRightFoot}, {JLeftToes, JRightToes}, {JLeftToesEnd, JRightToesEnd}}};

XMFLOAT3 Add(const XMFLOAT3& a, const XMFLOAT3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XMFLOAT3 Sub(const XMFLOAT3& a, const XMFLOAT3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XMFLOAT3 Mul(const XMFLOAT3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(const XMFLOAT3& a, const XMFLOAT3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length(const XMFLOAT3& a) { return std::sqrt(Dot(a, a)); }
XMFLOAT3 Lerp(const XMFLOAT3& a, const XMFLOAT3& b, float t) { return Add(a, Mul(Sub(b, a), t)); }
float SegmentDistance(const XMFLOAT3& p, const XMFLOAT3& a, const XMFLOAT3& b) {
    const XMFLOAT3 ab = Sub(b, a);
    const float lengthSquared = Dot(ab, ab);
    const float t = lengthSquared > 0 ? std::clamp(Dot(Sub(p, a), ab) / lengthSquared, 0.0f, 1.0f) : 0.0f;
    return Length(Sub(p, Add(a, Mul(ab, t))));
}
template<class T> T Median(std::vector<T> values) {
    if (values.empty()) return T{};
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}
std::string Normalized(const std::string& name) {
    const size_t colon = name.find_last_of(':');
    std::string result;
    for (size_t i = colon == std::string::npos ? 0 : colon + 1; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (std::isalnum(c)) result.push_back(static_cast<char>(std::tolower(c)));
    }
    if (result.rfind("mixamorig", 0) == 0) result.erase(0, 9);
    return result;
}
// Matches the importer's path escaping so rebuilt paths look like imported ones.
std::string PathComponent(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '%') out += "%25"; else if (c == '/') out += "%2F"; else if (c == '~') out += "%7E"; else out += c;
    }
    return out.empty() ? "unnamed" : out;
}

// ---- Object selection ------------------------------------------------------------------------
struct Selection {
    int object = -1;
    std::vector<uint8_t> member; // Per scene node: owned by the selected object.
    std::vector<size_t> meshes;
    size_t vertices = 0;
    bool skinned = false, humanoid = false;
};
std::vector<int> NodeOwners(const ModelAsset& model) {
    const auto& bones = model.skeleton.bones;
    std::vector<int> rootOf(bones.size(), -1), owner(bones.size(), -1);
    for (size_t o = 0; o < model.objects.size(); ++o) {
        const uint32_t root = model.objects[o].root;
        if (root < bones.size() && rootOf[root] < 0) rootOf[root] = static_cast<int>(o);
    }
    for (size_t b = 0; b < bones.size(); ++b) {
        const int parent = bones[b].parent;
        if (parent < -1 || parent >= static_cast<int>(b)) throw std::runtime_error("Scene nodes must be stored parent-first.");
        owner[b] = rootOf[b] >= 0 ? rootOf[b] : parent >= 0 ? owner[static_cast<size_t>(parent)] : -1;
    }
    return owner;
}
int MeshOwner(const MeshData& mesh, const std::vector<int>& owner) {
    if (mesh.skin.empty() || mesh.vertices.empty() || mesh.indices.size() < 3) return -1;
    int result = -1;
    for (const SkinBinding& binding : mesh.skin) {
        if (binding.bone >= owner.size()) return -1;
        const int o = owner[binding.bone];
        if (o < 0 || (result >= 0 && o != result)) return -1;
        result = o;
    }
    return result;
}
Selection Collect(const ModelAsset& model, int object) {
    Selection s;
    const std::vector<int> owner = NodeOwners(model);
    if (object >= static_cast<int>(model.objects.size())) throw std::runtime_error("The object to auto-rig does not exist.");
    if (object < 0) {
        std::vector<size_t> counts(model.objects.size(), 0);
        for (const MeshData& mesh : model.meshes) {
            const int o = MeshOwner(mesh, owner);
            if (o >= 0) counts[static_cast<size_t>(o)] += mesh.vertices.size();
        }
        size_t best = 0;
        for (size_t o = 0; o < counts.size(); ++o)
            if (counts[o] > best) { best = counts[o]; object = static_cast<int>(o); }
        if (object < 0) return s;
    }
    s.object = object;
    const auto& bones = model.skeleton.bones;
    s.member.assign(bones.size(), 0);
    for (size_t b = 0; b < bones.size(); ++b) s.member[b] = static_cast<uint8_t>(owner[b] == object);
    for (size_t m = 0; m < model.meshes.size(); ++m) {
        if (MeshOwner(model.meshes[m], owner) != object) continue;
        s.meshes.push_back(m);
        s.vertices += model.meshes[m].vertices.size();
        s.skinned = s.skinned || model.meshes[m].skin.size() > 1;
    }
    std::vector<uint8_t> belowHips(bones.size(), 0);
    bool head = false, hand = false;
    for (size_t b = 0; b < bones.size(); ++b) {
        if (!s.member[b]) continue;
        const int parent = bones[b].parent;
        if (parent >= 0 && s.member[static_cast<size_t>(parent)]) {
            const std::string parentName = Normalized(bones[static_cast<size_t>(parent)].name);
            belowHips[b] = static_cast<uint8_t>(belowHips[static_cast<size_t>(parent)] || parentName == "hips" || parentName == "pelvis");
        }
        if (!belowHips[b]) continue;
        const std::string name = Normalized(bones[b].name);
        head = head || name == "head";
        hand = hand || name.find("hand") != std::string::npos;
    }
    s.humanoid = head && hand;
    return s;
}

// ---- Bind-pose surface -------------------------------------------------------------------------
struct Surface {
    std::vector<XMFLOAT3> positions, normals;
    std::vector<uint32_t> triangles;
    std::vector<size_t> offsets; // First surface vertex of each selected mesh.
    XMFLOAT3 lo{FLT_MAX, FLT_MAX, FLT_MAX}, hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};
};
Surface Gather(const ModelAsset& model, const Selection& selection) {
    const Pose pose = EvaluatePose(model.skeleton, nullptr, 0);
    Surface s;
    for (const size_t m : selection.meshes) {
        const MeshData& mesh = model.meshes[m];
        std::vector<XMFLOAT4X4> point(mesh.skin.size()), normal(mesh.skin.size());
        for (size_t i = 0; i < mesh.skin.size(); ++i) {
            const XMMATRIX p = XMMatrixMultiply(XMLoadFloat4x4(&mesh.skin[i].inverseBind), XMLoadFloat4x4(&pose.global[mesh.skin[i].bone]));
            XMStoreFloat4x4(&point[i], p);
            XMStoreFloat4x4(&normal[i], XMMatrixTranspose(XMMatrixInverse(nullptr, p)));
        }
        const size_t base = s.positions.size();
        s.offsets.push_back(base);
        for (const Vertex& v : mesh.vertices) {
            const XMVECTOR position = XMLoadFloat3(&v.position), direction = XMLoadFloat3(&v.normal);
            XMVECTOR p = XMVectorZero(), n = XMVectorZero();
            float total = 0;
            for (size_t k = 0; k < 4; ++k) {
                const float w = v.weights[k];
                if (!(w > 0)) continue;
                if (v.joints[k] >= point.size()) throw std::runtime_error("A vertex refers to a missing skin joint.");
                p = XMVectorAdd(p, XMVectorScale(XMVector3TransformCoord(position, XMLoadFloat4x4(&point[v.joints[k]])), w));
                n = XMVectorAdd(n, XMVectorScale(XMVector3TransformNormal(direction, XMLoadFloat4x4(&normal[v.joints[k]])), w));
                total += w;
            }
            if (total > 0) p = XMVectorScale(p, 1.0f / total);
            else {
                p = XMVector3TransformCoord(position, XMLoadFloat4x4(&point[0]));
                n = XMVector3TransformNormal(direction, XMLoadFloat4x4(&normal[0]));
            }
            XMFLOAT3 world, facing{0, 1, 0};
            XMStoreFloat3(&world, p);
            if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
                throw std::runtime_error("The mesh contains non-finite vertices.");
            const float length = XMVectorGetX(XMVector3Length(n));
            if (std::isfinite(length) && length > 1e-12f) XMStoreFloat3(&facing, XMVectorScale(n, 1.0f / length));
            s.positions.push_back(world);
            s.normals.push_back(facing);
            s.lo = {std::min(s.lo.x, world.x), std::min(s.lo.y, world.y), std::min(s.lo.z, world.z)};
            s.hi = {std::max(s.hi.x, world.x), std::max(s.hi.y, world.y), std::max(s.hi.z, world.z)};
        }
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
            for (size_t c = 0; c < 3; ++c) {
                const uint32_t index = mesh.indices[i + c];
                if (index >= mesh.vertices.size()) throw std::runtime_error("A mesh index is out of range.");
                s.triangles.push_back(static_cast<uint32_t>(base + index));
            }
    }
    return s;
}

// Character frame: x = character left, y = up, z = back. Identity for a -Z facing character.
struct Frame {
    XMFLOAT3 left, back;
    explicit Frame(const XMFLOAT3& forward) : left{-forward.z, 0, forward.x}, back{-forward.x, 0, -forward.z} {}
    XMFLOAT3 Local(const XMFLOAT3& p) const { return {Dot(p, left), p.y, Dot(p, back)}; }
    XMFLOAT3 World(const XMFLOAT3& c) const { return {c.x * left.x + c.z * back.x, c.y, c.x * left.z + c.z * back.z}; }
};

// ---- Voxels -----------------------------------------------------------------------------------
struct Grid {
    XMFLOAT3 origin{};
    float h = 1;
    int nx = 0, ny = 0, nz = 0;
    std::vector<uint8_t> solid;
    size_t LayerSize() const { return static_cast<size_t>(nx) * static_cast<size_t>(nz); }
    size_t Count() const { return LayerSize() * static_cast<size_t>(ny); }
    size_t Index(int i, int j, int k) const {
        return (static_cast<size_t>(j) * static_cast<size_t>(nz) + static_cast<size_t>(k)) * static_cast<size_t>(nx) + static_cast<size_t>(i);
    }
    void Coords(size_t c, int& i, int& j, int& k) const {
        i = static_cast<int>(c % static_cast<size_t>(nx));
        k = static_cast<int>((c / static_cast<size_t>(nx)) % static_cast<size_t>(nz));
        j = static_cast<int>(c / LayerSize());
    }
    float Y(int j) const { return origin.y + (static_cast<float>(j) + 0.5f) * h; }
    XMFLOAT3 Center(size_t c) const {
        int i = 0, j = 0, k = 0;
        Coords(c, i, j, k);
        return {origin.x + (static_cast<float>(i) + 0.5f) * h, Y(j), origin.z + (static_cast<float>(k) + 0.5f) * h};
    }
    int Cell(float value, float start, int count) const {
        return std::clamp(static_cast<int>(std::floor((value - start) / h)), 0, count - 1);
    }
    int LayerAt(float y) const { return Cell(y, origin.y, ny); }
    size_t CellAt(const XMFLOAT3& p) const { return Index(Cell(p.x, origin.x, nx), LayerAt(p.y), Cell(p.z, origin.z, nz)); }
    bool Interior(size_t c) const {
        int i = 0, j = 0, k = 0;
        Coords(c, i, j, k);
        return i > 0 && j > 0 && k > 0 && i + 1 < nx && j + 1 < ny && k + 1 < nz;
    }
};
// Surface cells plus cells enclosed by the surface along all three axes.
Grid Voxelize(const std::vector<XMFLOAT3>& points, const std::vector<uint32_t>& triangles) {
    XMFLOAT3 lo{FLT_MAX, FLT_MAX, FLT_MAX}, hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const XMFLOAT3& p : points) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    Grid g;
    g.h = (hi.y - lo.y) / 160.0f;
    for (;;) {
        g.nx = static_cast<int>(std::ceil((hi.x - lo.x) / g.h)) + 2 * Pad + 1;
        g.ny = static_cast<int>(std::ceil((hi.y - lo.y) / g.h)) + 2 * Pad + 1;
        g.nz = static_cast<int>(std::ceil((hi.z - lo.z) / g.h)) + 2 * Pad + 1;
        if (static_cast<double>(g.nx) * g.ny * g.nz <= 6.0e6) break;
        g.h *= 1.1f;
    }
    const float pad = static_cast<float>(Pad) * g.h;
    g.origin = {lo.x - pad, lo.y - pad, lo.z - pad};
    std::vector<uint8_t> surface(g.Count(), 0), between(g.Count(), 0);
    for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
        const XMFLOAT3& a = points[triangles[t]];
        const XMFLOAT3 ab = Sub(points[triangles[t + 1]], a), ac = Sub(points[triangles[t + 2]], a);
        const float longest = std::max({Length(ab), Length(ac), Length(Sub(ac, ab))});
        const int n = std::clamp(static_cast<int>(std::ceil(2.0f * longest / g.h)), 1, 4096);
        const float step = 1.0f / static_cast<float>(n);
        for (int u = 0; u <= n; ++u)
            for (int v = 0; u + v <= n; ++v)
                surface[g.CellAt(Add(a, Add(Mul(ab, static_cast<float>(u) * step), Mul(ac, static_cast<float>(v) * step))))] = 1;
    }
    auto mark = [&](size_t start, size_t stride, int count) {
        int first = -1, last = -1;
        for (int s = 0; s < count; ++s)
            if (surface[start + static_cast<size_t>(s) * stride]) { if (first < 0) first = s; last = s; }
        for (int s = first + 1; first >= 0 && s < last; ++s) ++between[start + static_cast<size_t>(s) * stride];
    };
    for (int j = 0; j < g.ny; ++j) for (int k = 0; k < g.nz; ++k) mark(g.Index(0, j, k), 1, g.nx);
    for (int j = 0; j < g.ny; ++j) for (int i = 0; i < g.nx; ++i) mark(g.Index(i, j, 0), static_cast<size_t>(g.nx), g.nz);
    for (int k = 0; k < g.nz; ++k) for (int i = 0; i < g.nx; ++i) mark(g.Index(i, 0, k), g.LayerSize(), g.ny);
    g.solid.resize(g.Count());
    for (size_t c = 0; c < g.Count(); ++c) g.solid[c] = static_cast<uint8_t>(surface[c] || between[c] == 3);
    return g;
}

// ---- Legs ---------------------------------------------------------------------------------------
struct UnionFind {
    std::vector<uint32_t> parent, size;
    std::vector<int> low;
    void Reset(size_t count) { parent.assign(count, Unreached); size.assign(count, 0); low.assign(count, 0); }
    void Make(size_t c, int layer) { parent[c] = static_cast<uint32_t>(c); size[c] = 1; low[c] = layer; }
    uint32_t Find(uint32_t a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    }
    void Union(uint32_t a, uint32_t b) { // Both must be roots.
        if (size[a] < size[b]) std::swap(a, b);
        parent[b] = a; size[a] += size[b]; low[a] = std::min(low[a], low[b]);
    }
};
struct Legs {
    int bottom = 0, top = 0, crotch = 0; // Leg cells occupy layers [bottom, crotch).
    bool found = false;
    std::vector<uint8_t> label;          // 1 = left (+x in the character frame), 2 = right.
};
Legs FindLegs(const Grid& g) {
    Legs legs;
    const size_t layer = g.LayerSize();
    std::vector<size_t> counts(static_cast<size_t>(g.ny), 0);
    size_t total = 0;
    for (size_t c = 0; c < g.Count(); ++c) if (g.solid[c]) { ++counts[c / layer]; ++total; }
    if (!total) throw std::runtime_error("The mesh has no volume to rig.");
    while (!counts[static_cast<size_t>(legs.bottom)]) ++legs.bottom;
    legs.top = g.ny - 1;
    while (!counts[static_cast<size_t>(legs.top)]) --legs.top;
    const int span = legs.top - legs.bottom + 1;
    const int lowBand = legs.bottom + span / 10;
    const int firstCrotch = legs.bottom + static_cast<int>(std::ceil(0.15 * span));
    const uint32_t minimum = static_cast<uint32_t>(std::max<size_t>(20, total / 100));
    UnionFind uf;
    // Joins 6-connected solid cells bottom-up; optionally reports the layer where two leg-sized parts meet.
    auto sweep = [&](int endLayer, bool detect) {
        int crotch = -1;
        for (int j = legs.bottom; j < endLayer && crotch < 0; ++j)
            for (int k = 0; k < g.nz; ++k)
                for (int i = 0; i < g.nx; ++i) {
                    const size_t c = g.Index(i, j, k);
                    if (!g.solid[c]) continue;
                    uf.Make(c, j);
                    const size_t neighbors[3] = {i > 0 ? c - 1 : c, k > 0 ? c - static_cast<size_t>(g.nx) : c, j > 0 ? c - layer : c};
                    for (const size_t n : neighbors) {
                        if (n == c || !g.solid[n]) continue;
                        const uint32_t a = uf.Find(static_cast<uint32_t>(c)), b = uf.Find(static_cast<uint32_t>(n));
                        if (a == b) continue;
                        if (detect && crotch < 0 && j >= firstCrotch && uf.low[a] <= lowBand && uf.low[b] <= lowBand) {
                            const uint64_t smaller = std::min(uf.size[a], uf.size[b]), larger = std::max(uf.size[a], uf.size[b]);
                            if (smaller >= minimum && 4 * smaller >= larger) crotch = j;
                        }
                        uf.Union(a, b);
                    }
                }
        return crotch;
    };
    uf.Reset(g.Count());
    const int crotch = sweep(legs.top + 1, true);
    if (crotch > legs.bottom) {
        uf.Reset(g.Count());
        sweep(crotch, false);
        uint32_t first = Unreached, second = Unreached;
        for (size_t c = static_cast<size_t>(legs.bottom) * layer; c < static_cast<size_t>(crotch) * layer; ++c) {
            if (!g.solid[c] || uf.parent[c] != c || uf.low[c] > lowBand) continue;
            const uint32_t root = static_cast<uint32_t>(c);
            if (first == Unreached || uf.size[root] > uf.size[first]) { second = first; first = root; }
            else if (second == Unreached || uf.size[root] > uf.size[second]) second = root;
        }
        if (second != Unreached) {
            legs.label.assign(g.Count(), 0);
            double sum[3] = {0, 0, 0};
            for (size_t c = static_cast<size_t>(legs.bottom) * layer; c < static_cast<size_t>(crotch) * layer; ++c) {
                if (!g.solid[c]) continue;
                const uint32_t root = uf.Find(static_cast<uint32_t>(c));
                const uint8_t label = static_cast<uint8_t>(root == first ? 1 : root == second ? 2 : 0);
                legs.label[c] = label;
                if (label) sum[label] += g.Center(c).x;
            }
            const bool swap = sum[1] / uf.size[first] < sum[2] / uf.size[second];
            if (swap)
                for (uint8_t& label : legs.label) if (label) label = static_cast<uint8_t>(3 - label);
            legs.crotch = crotch;
            legs.found = true;
        }
    }
    if (!legs.found) {
        legs.crotch = legs.bottom + static_cast<int>(0.42 * span);
        double sumX = 0;
        size_t count = 0;
        for (size_t c = static_cast<size_t>(legs.bottom) * layer; c <= static_cast<size_t>(lowBand) * layer + layer - 1; ++c)
            if (g.solid[c]) { sumX += g.Center(c).x; ++count; }
        const double split = count ? sumX / static_cast<double>(count) : 0.0;
        legs.label.assign(g.Count(), 0);
        for (size_t c = static_cast<size_t>(legs.bottom) * layer; c < static_cast<size_t>(legs.crotch) * layer; ++c)
            if (g.solid[c]) legs.label[c] = static_cast<uint8_t>(g.Center(c).x > split ? 1 : 2);
    }
    return legs;
}
// Without separate legs (robes, long skirts) the shoulders/arms span the widest horizontal axis and the feet stick
// out in front of the hem, so use those instead.
XMFLOAT3 ForwardFromSilhouette(const Grid& g, const Legs& legs, std::vector<std::string>& warnings) {
    const size_t layer = g.LayerSize();
    const int span = legs.top - legs.bottom + 1;
    const int upperFrom = legs.bottom + static_cast<int>(0.45f * static_cast<float>(span));
    const int upperTo = legs.bottom + static_cast<int>(0.85f * static_cast<float>(span));
    double n = 0, mx = 0, mz = 0, xx = 0, zz = 0, xz = 0;
    for (size_t c = static_cast<size_t>(upperFrom) * layer; c < static_cast<size_t>(upperTo) * layer; ++c) {
        if (!g.solid[c]) continue;
        const XMFLOAT3 p = g.Center(c);
        n += 1; mx += p.x; mz += p.z; xx += p.x * p.x; zz += p.z * p.z; xz += p.x * p.z;
    }
    const XMFLOAT3 fallback{0, 0, -1};
    if (n < 2) {
        warnings.push_back("The legs are not separated, so the character is assumed to face -Z.");
        return fallback;
    }
    mx /= n; mz /= n;
    const double cxx = xx / n - mx * mx, czz = zz / n - mz * mz, cxz = xz / n - mx * mz;
    const double mean = 0.5 * (cxx + czz), spread = std::sqrt(0.25 * (cxx - czz) * (cxx - czz) + cxz * cxz);
    if (!(mean - spread > 1e-12) || (mean + spread) < 1.3 * (mean - spread)) {
        warnings.push_back("The legs are not separated and the body is as deep as it is wide, so the character is assumed to face -Z.");
        return fallback;
    }
    const double angle = 0.5 * std::atan2(2 * cxz, cxx - czz);
    XMFLOAT3 lateral{static_cast<float>(std::cos(angle)), 0, static_cast<float>(std::sin(angle))};
    const float snap = std::cos(XMConvertToRadians(20.0f));
    if (std::abs(lateral.x) >= snap) lateral = {1, 0, 0};
    else if (std::abs(lateral.z) >= snap) lateral = {0, 0, 1};
    const XMFLOAT3 d{-lateral.z, 0, lateral.x};
    const int footEnd = legs.bottom + std::max(1, span / 20);
    const int hemBegin = legs.bottom + span / 10, hemEnd = legs.bottom + 3 * span / 10;
    float footMin = FLT_MAX, footMax = -FLT_MAX, hemMin = FLT_MAX, hemMax = -FLT_MAX;
    for (size_t c = static_cast<size_t>(legs.bottom) * layer; c < static_cast<size_t>(hemEnd) * layer; ++c) {
        if (!g.solid[c]) continue;
        const int j = static_cast<int>(c / layer);
        const float p = Dot(g.Center(c), d);
        if (j < footEnd) { footMin = std::min(footMin, p); footMax = std::max(footMax, p); }
        else if (j >= hemBegin) { hemMin = std::min(hemMin, p); hemMax = std::max(hemMax, p); }
    }
    const float ahead = footMax - hemMax, behind = hemMin - footMin;
    if (footMin > footMax || hemMin > hemMax || std::abs(ahead - behind) < g.h) {
        warnings.push_back("The legs are not separated and the feet do not show which way the character faces; the side closest to -Z was used.");
        return d.z <= 0 ? d : Mul(d, -1.0f);
    }
    return ahead > behind ? d : Mul(d, -1.0f);
}
// Feet point forward: compare how far each foot sticks out past its thigh on both sides.
XMFLOAT3 DetectForward(const Grid& g, const Legs& legs, std::vector<std::string>& warnings) {
    if (!legs.found) return ForwardFromSilhouette(g, legs, warnings);
    const size_t layer = g.LayerSize();
    double sx[3] = {0, 0, 0}, sz[3] = {0, 0, 0};
    size_t count[3] = {0, 0, 0};
    int low[3] = {g.ny, g.ny, g.ny};
    for (size_t c = static_cast<size_t>(legs.bottom) * layer; c < static_cast<size_t>(legs.crotch) * layer; ++c) {
        const uint8_t l = legs.label[c];
        if (!l) continue;
        const XMFLOAT3 p = g.Center(c);
        sx[l] += p.x; sz[l] += p.z; ++count[l];
        low[l] = std::min(low[l], static_cast<int>(c / layer));
    }
    XMFLOAT3 lateral{static_cast<float>(sx[1] / static_cast<double>(count[1]) - sx[2] / static_cast<double>(count[2])), 0,
        static_cast<float>(sz[1] / static_cast<double>(count[1]) - sz[2] / static_cast<double>(count[2]))};
    const float lateralLength = Length(lateral);
    if (!(lateralLength > 1e-6f)) {
        warnings.push_back("The legs overlap, so the character is assumed to face -Z.");
        return {0, 0, -1};
    }
    lateral = Mul(lateral, 1.0f / lateralLength);
    const float snap = std::cos(XMConvertToRadians(20.0f));
    if (std::abs(lateral.x) >= snap) lateral = {lateral.x > 0 ? 1.0f : -1.0f, 0, 0};
    else if (std::abs(lateral.z) >= snap) lateral = {0, 0, lateral.z > 0 ? 1.0f : -1.0f};
    const XMFLOAT3 d{-lateral.z, 0, lateral.x};
    float ahead = 0, behind = 0;
    for (const int l : {1, 2}) {
        const int length = legs.crotch - low[l];
        const int footEnd = low[l] + std::max(1, length / 10);
        const int upperBegin = low[l] + static_cast<int>(0.4f * static_cast<float>(length));
        const int upperEnd = low[l] + static_cast<int>(0.8f * static_cast<float>(length));
        float footMin = FLT_MAX, footMax = -FLT_MAX, upperMin = FLT_MAX, upperMax = -FLT_MAX;
        for (size_t c = static_cast<size_t>(low[l]) * layer; c < static_cast<size_t>(legs.crotch) * layer; ++c) {
            if (legs.label[c] != l) continue;
            const int j = static_cast<int>(c / layer);
            const float p = Dot(g.Center(c), d);
            if (j < footEnd) { footMin = std::min(footMin, p); footMax = std::max(footMax, p); }
            else if (j >= upperBegin && j < upperEnd) { upperMin = std::min(upperMin, p); upperMax = std::max(upperMax, p); }
        }
        if (footMin > footMax || upperMin > upperMax) continue;
        ahead += footMax - upperMax;
        behind += upperMin - footMin;
    }
    if (std::abs(ahead - behind) < g.h) {
        warnings.push_back("The feet do not show which way the character faces; the side closest to -Z was used.");
        return d.z <= 0 ? d : Mul(d, -1.0f);
    }
    return ahead > behind ? d : Mul(d, -1.0f);
}
struct LegFit { XMFLOAT3 hip, knee, ankle, toes, toesEnd; };
LegFit FitLeg(const Grid& g, const Legs& legs, uint8_t side, float height, std::vector<std::string>& warnings) {
    const char* name = side == 1 ? "left" : "right";
    const int layers = legs.crotch - legs.bottom;
    if (layers < 4) throw std::runtime_error("The legs are too short to rig.");
    const size_t layer = g.LayerSize();
    std::vector<size_t> count(static_cast<size_t>(layers), 0);
    std::vector<double> sx(count.size(), 0), sz(count.size(), 0);
    std::vector<float> front(count.size(), FLT_MAX);
    for (size_t c = static_cast<size_t>(legs.bottom) * layer; c < static_cast<size_t>(legs.crotch) * layer; ++c) {
        if (legs.label[c] != side) continue;
        const XMFLOAT3 p = g.Center(c);
        const size_t r = c / layer - static_cast<size_t>(legs.bottom);
        ++count[r]; sx[r] += p.x; sz[r] += p.z;
        front[r] = std::min(front[r], p.z);
    }
    int low = 0;
    while (low < layers && !count[static_cast<size_t>(low)]) ++low;
    if (low >= layers) throw std::runtime_error(std::string("Could not find the ") + name + " leg.");
    auto row = [&](float y) { return std::clamp(g.LayerAt(y) - legs.bottom, 0, layers - 1); };
    auto centroid = [&](int from, int to) { // Rows [from, to), widened until it holds leg cells.
        for (int grow = 0; grow < layers; ++grow) {
            double x = 0, z = 0;
            size_t n = 0;
            for (int r = std::max(low, from - grow); r < std::min(layers, to + grow); ++r) {
                x += sx[static_cast<size_t>(r)]; z += sz[static_cast<size_t>(r)]; n += count[static_cast<size_t>(r)];
            }
            if (n) return XMFLOAT3{static_cast<float>(x / static_cast<double>(n)), 0, static_cast<float>(z / static_cast<double>(n))};
        }
        return XMFLOAT3{};
    };
    const float y0 = g.Y(legs.bottom + low) - 0.5f * g.h;
    const float crotchY = g.Y(legs.crotch) - 0.5f * g.h;
    const float length = crotchY - y0;
    LegFit fit{};
    fit.hip = centroid(row(crotchY - 0.2f * length), layers);
    fit.hip.y = crotchY + 0.1f * length;
    std::vector<float> shin;
    for (int r = row(y0 + 0.4f * length); r <= row(y0 + 0.7f * length); ++r)
        if (count[static_cast<size_t>(r)]) shin.push_back(front[static_cast<size_t>(r)]);
    const float shinFront = Median(shin);
    int ankle = -1;
    for (int r = low; !shin.empty() && r + 2 < layers; ++r) {
        bool above = true;
        for (int d = 0; d < 3; ++d)
            above = above && count[static_cast<size_t>(r + d)] && front[static_cast<size_t>(r + d)] >= shinFront - 2 * g.h;
        if (above) { ankle = r; break; }
    }
    float ankleY = ankle > low + 1 ? g.Y(legs.bottom + ankle) : y0 + 0.12f * length;
    if (ankle <= low + 1) warnings.push_back(std::string("The ") + name + " ankle was estimated from leg proportions.");
    const float ankleTop = y0 + 0.45f * length;
    ankleY = std::clamp(ankleY, std::min(y0 + 0.03f * height, ankleTop), ankleTop);
    fit.ankle = centroid(row(ankleY), row(ankleY) + 3);
    fit.ankle.y = ankleY;
    const float kneeY = 0.5f * (fit.hip.y + ankleY);
    fit.knee = centroid(row(kneeY), row(kneeY) + 1);
    fit.knee.y = kneeY;
    float zmin = FLT_MAX;
    const size_t footEnd = static_cast<size_t>(legs.bottom + row(ankleY)) * layer;
    for (size_t c = static_cast<size_t>(legs.bottom) * layer; c < footEnd; ++c)
        if (legs.label[c] == side) zmin = std::min(zmin, g.Center(c).z);
    XMFLOAT3 tip{};
    size_t tipCount = 0;
    for (size_t c = static_cast<size_t>(legs.bottom) * layer; c < footEnd; ++c)
        if (legs.label[c] == side && g.Center(c).z <= zmin + g.h) { tip = Add(tip, g.Center(c)); ++tipCount; }
    if (tipCount) tip = Mul(tip, 1.0f / static_cast<float>(tipCount));
    else {
        tip = {fit.ankle.x, y0, fit.ankle.z - 0.05f * height};
        warnings.push_back(std::string("The ") + name + " toes were estimated from leg proportions.");
    }
    fit.toes = Lerp(fit.ankle, tip, 0.62f);
    fit.toes.y = y0 + 0.3f * (ankleY - y0);
    fit.toesEnd = {tip.x, fit.toes.y, tip.z};
    return fit;
}

// ---- Torso, neck and arms -----------------------------------------------------------------------
std::vector<size_t> LargestRegion(const Grid& g, int j) {
    const size_t base = static_cast<size_t>(j) * g.LayerSize(), nx = static_cast<size_t>(g.nx);
    std::vector<uint8_t> seen(g.LayerSize(), 0);
    std::vector<size_t> best, current, stack;
    for (size_t s = 0; s < g.LayerSize(); ++s) {
        if (seen[s] || !g.solid[base + s]) continue;
        current.clear();
        stack.assign(1, s);
        seen[s] = 1;
        while (!stack.empty()) {
            const size_t c = stack.back();
            stack.pop_back();
            current.push_back(base + c);
            const int i = static_cast<int>(c % nx), k = static_cast<int>(c / nx);
            for (int dk = -1; dk <= 1; ++dk)
                for (int di = -1; di <= 1; ++di) {
                    const int ni = i + di, nk = k + dk;
                    if ((!di && !dk) || ni < 0 || nk < 0 || ni >= g.nx || nk >= g.nz) continue;
                    const size_t n = static_cast<size_t>(nk) * nx + static_cast<size_t>(ni);
                    if (!seen[n] && g.solid[base + n]) { seen[n] = 1; stack.push_back(n); }
                }
        }
        if (current.size() > best.size()) best.swap(current);
    }
    return best;
}
// The neck is the layer that is much narrower than both the chest below and the head above.
float FindNeck(const Grid& g, const Legs& legs, float crotchY, float topY, float height, bool& found, std::vector<std::string>& warnings) {
    std::vector<size_t> area(static_cast<size_t>(g.ny), 0);
    for (int j = legs.bottom; j <= legs.top; ++j) area[static_cast<size_t>(j)] = LargestRegion(g, j).size();
    const float torso = topY - crotchY;
    const int window = std::max(1, static_cast<int>(std::lround(0.1f * height / g.h)));
    const int from = g.LayerAt(crotchY + 0.3f * torso), to = g.LayerAt(topY - 0.05f * torso);
    float bestScore = 0;
    int best = -1;
    for (int j = from; j <= to; ++j) {
        if (!area[static_cast<size_t>(j)]) continue;
        size_t above = 0, below = 0;
        for (int d = 1; d <= window; ++d) {
            if (j + d < g.ny) above = std::max(above, area[static_cast<size_t>(j + d)]);
            if (j - d >= 0) below = std::max(below, area[static_cast<size_t>(j - d)]);
        }
        const float score = static_cast<float>(std::min(above, below)) / static_cast<float>(area[static_cast<size_t>(j)]);
        if (score > bestScore) { bestScore = score; best = j; }
    }
    found = best >= 0 && bestScore >= 1.15f;
    if (!found) {
        warnings.push_back("The neck was estimated from body proportions.");
        return crotchY + 0.72f * torso;
    }
    return g.Y(best);
}
// Chamfer (3-4-5) distance from each solid cell to the nearest empty cell.
std::vector<uint16_t> DistanceField(const Grid& g) {
    std::vector<uint16_t> dt(g.Count());
    for (size_t c = 0; c < g.Count(); ++c) dt[c] = g.solid[c] ? 65535 : 0;
    struct Step { std::ptrdiff_t offset; int cost; };
    std::vector<Step> half;
    const std::ptrdiff_t nx = g.nx, layer = static_cast<std::ptrdiff_t>(g.LayerSize());
    for (int dj = -1; dj <= 1; ++dj)
        for (int dk = -1; dk <= 1; ++dk)
            for (int di = -1; di <= 1; ++di) {
                const std::ptrdiff_t offset = dj * layer + dk * nx + di;
                const int order = std::abs(di) + std::abs(dj) + std::abs(dk);
                if (offset < 0) half.push_back({offset, order + 2});
            }
    auto relax = [&](size_t c, std::ptrdiff_t sign) {
        if (!dt[c]) return;
        int best = dt[c];
        for (const Step& s : half) best = std::min(best, dt[static_cast<size_t>(static_cast<std::ptrdiff_t>(c) + sign * s.offset)] + s.cost);
        dt[c] = static_cast<uint16_t>(best);
    };
    for (size_t c = 0; c < g.Count(); ++c) relax(c, 1);
    for (size_t c = g.Count(); c-- > 0;) relax(c, -1);
    return dt;
}
XMFLOAT3 Core(const Grid& g, const std::vector<uint16_t>& dt, int j) {
    std::vector<size_t> region;
    for (int d = 0; region.empty() && d < g.ny; ++d) {
        region = LargestRegion(g, std::clamp(j + d, 0, g.ny - 1));
        if (region.empty()) region = LargestRegion(g, std::clamp(j - d, 0, g.ny - 1));
    }
    if (region.empty()) throw std::runtime_error("The torso could not be found.");
    int top = 0;
    for (const size_t c : region) top = std::max<int>(top, dt[c]);
    XMFLOAT3 sum{};
    size_t n = 0;
    for (const size_t c : region)
        if (dt[c] * 10 >= top * 6) { sum = Add(sum, g.Center(c)); ++n; }
    XMFLOAT3 core = Mul(sum, 1.0f / static_cast<float>(n));
    core.y = g.Y(j);
    return core;
}

// Dial's algorithm on the 26-connected voxel grid; one axis step costs 10.
class Geodesic {
public:
    explicit Geodesic(const Grid& grid) : grid_(grid), dist_(grid.Count(), Unreached) {
        const std::ptrdiff_t nx = grid.nx, layer = static_cast<std::ptrdiff_t>(grid.LayerSize());
        for (int dj = -1; dj <= 1; ++dj)
            for (int dk = -1; dk <= 1; ++dk)
                for (int di = -1; di <= 1; ++di) {
                    const int order = std::abs(di) + std::abs(dj) + std::abs(dk);
                    if (order) steps_.push_back({dj * layer + dk * nx + di, order == 1 ? 10u : order == 2 ? 14u : 17u});
                }
    }
    template<class Enter, class Cost> void Run(const std::vector<size_t>& seeds, Enter enter, Cost cost) {
        size_t pending = 0;
        for (const size_t s : seeds) {
            if (dist_[s] == 0) continue;
            if (dist_[s] == Unreached) touched_.push_back(s);
            dist_[s] = 0;
            buckets_[0].push_back(s);
            ++pending;
        }
        for (uint32_t d = 0; pending; ++d) {
            std::vector<size_t>& bucket = buckets_[d % Ring];
            while (!bucket.empty()) {
                const size_t c = bucket.back();
                bucket.pop_back();
                --pending;
                if (dist_[c] != d || !grid_.Interior(c)) continue;
                for (const Step& step : steps_) {
                    const size_t n = static_cast<size_t>(static_cast<std::ptrdiff_t>(c) + step.offset);
                    const uint32_t nd = d + cost(c, n, step.cost);
                    if (nd >= dist_[n] || !enter(n, nd)) continue;
                    if (dist_[n] == Unreached) touched_.push_back(n);
                    dist_[n] = nd;
                    buckets_[nd % Ring].push_back(n);
                    ++pending;
                }
            }
        }
    }
    void Reset() {
        for (const size_t c : touched_) dist_[c] = Unreached;
        touched_.clear();
    }
    const std::vector<uint32_t>& Distances() const { return dist_; }
    const std::vector<size_t>& Touched() const { return touched_; }
private:
    struct Step { std::ptrdiff_t offset; uint32_t cost; };
    const Grid& grid_;
    std::vector<uint32_t> dist_;
    std::vector<size_t> touched_;
    std::vector<Step> steps_;
    std::array<std::vector<size_t>, Ring> buckets_;
};
struct ArmFit {
    XMFLOAT3 shoulder, elbow, wrist, tip;
    bool branched; // The arm visibly joins the torso (the front's cross-section jumped).
};
// Grows a geodesic front from the fingertips; the front's cross-section jumps where the arm meets the torso.
ArmFit TraceArm(const Grid& g, const Legs& legs, Geodesic& geo, bool left, float cx, float hipY, float neckY, float height,
    std::vector<std::string>& warnings) {
    const char* side = left ? "left" : "right";
    const size_t layer = g.LayerSize();
    const int from = g.LayerAt(g.Y(legs.bottom) + 0.15f * height), to = g.LayerAt(neckY);
    auto body = [&](size_t c) { return g.solid[c] && !legs.label[c]; };
    size_t extreme = SIZE_MAX;
    float best = left ? -FLT_MAX : FLT_MAX;
    for (size_t c = static_cast<size_t>(from) * layer; c < static_cast<size_t>(to + 1) * layer; ++c) {
        if (!body(c)) continue;
        const float x = g.Center(c).x;
        if (left ? x > best : x < best) { best = x; extreme = c; }
    }
    if (extreme == SIZE_MAX) throw std::runtime_error(std::string("Could not find the ") + side + " arm.");
    const XMFLOAT3 e = g.Center(extreme);
    std::vector<size_t> seeds;
    XMFLOAT3 tip{};
    for (size_t c = static_cast<size_t>(from) * layer; c < static_cast<size_t>(to + 1) * layer; ++c) {
        if (!body(c)) continue;
        const XMFLOAT3 p = g.Center(c);
        if (std::abs(p.x - e.x) <= g.h && std::abs(p.y - e.y) <= 0.1f * height) { seeds.push_back(c); tip = Add(tip, p); }
    }
    tip = Mul(tip, 1.0f / static_cast<float>(seeds.size()));
    geo.Reset();
    geo.Run(seeds, [&](size_t n, uint32_t) { return body(n); }, [](size_t, size_t, uint32_t base) { return base; });
    std::vector<size_t> count;
    std::vector<XMFLOAT3> sum;
    for (const size_t c : geo.Touched()) {
        const size_t bin = geo.Distances()[c] / 15;
        if (bin >= count.size()) { count.resize(bin + 1, 0); sum.resize(bin + 1, XMFLOAT3{}); }
        ++count[bin];
        sum[bin] = Add(sum[bin], g.Center(c));
    }
    geo.Reset();
    if (count.size() < 4) throw std::runtime_error(std::string("The ") + side + " arm is too short to rig.");
    auto at = [&](size_t b) { // Centroid of the nearest populated bin at or below b.
        while (b > 0 && !count[b]) --b;
        return Mul(sum[b], 1.0f / static_cast<float>(count[b]));
    };
    const float reach = std::abs(tip.x - cx);
    const size_t minBins = static_cast<size_t>(std::ceil(std::max(0.35f * reach, 0.06f * height) / g.h * 10.0f / 15.0f));
    size_t last = 0;
    bool jumped = false;
    for (size_t b = std::max<size_t>(7, minBins); b + 1 < count.size(); ++b) {
        const double median = static_cast<double>(Median(std::vector<size_t>(
            count.begin() + static_cast<std::ptrdiff_t>(b - 6), count.begin() + static_cast<std::ptrdiff_t>(b))));
        if (static_cast<double>(count[b]) > 2 * median && static_cast<double>(count[b + 1]) > 2 * median) {
            last = b - 1;
            while (last > 1 && static_cast<double>(count[last]) > 1.3 * median) --last;
            jumped = true;
            break;
        }
    }
    if (!jumped) {
        warnings.push_back(std::string("The ") + side + " shoulder was estimated from the arm's reach.");
        last = count.size() - 1;
        for (size_t b = 1; b < count.size(); ++b)
            if (count[b] && std::abs(at(b).x - cx) <= 0.3f * reach) { last = b; break; }
    }
    while (last > 0 && !count[last]) --last;
    std::vector<size_t> section;
    for (size_t b = 1; b <= last; ++b) section.push_back(count[b]);
    const float radius = std::sqrt(static_cast<float>(Median(section)) * g.h * g.h / (1.5f * XM_PI));
    const XMFLOAT3 joint = at(last);
    XMFLOAT3 direction = Sub(joint, at(last > 5 ? last - 4 : 1));
    float directionLength = Length(direction);
    if (!(directionLength > 1e-6f)) { direction = {cx > joint.x ? 1.0f : -1.0f, 0, 0}; directionLength = 1; }
    ArmFit fit{};
    fit.shoulder = Add(joint, Mul(direction, radius / directionLength));
    fit.tip = tip;
    fit.branched = jumped;
    // Arms resting against the body (A/I poses, wide sleeves) merge with the torso below the shoulder, so the
    // front's jump marks that contact instead. Use the proportional shoulder height (about 18% of the
    // hip-to-neck span below the neck for both realistic and stylized bodies) and the body's width there.
    const float shoulderY = neckY - 0.18f * (neckY - hipY);
    if (fit.shoulder.y < shoulderY - 0.03f * height) {
        const std::vector<size_t> region = LargestRegion(g, g.LayerAt(shoulderY));
        float extent = 0;
        for (const size_t c : region) extent = std::max(extent, left ? g.Center(c).x - cx : cx - g.Center(c).x);
        float depth = 0;
        size_t outer = 0;
        for (const size_t c : region) {
            const XMFLOAT3 p = g.Center(c);
            if ((left ? p.x - cx : cx - p.x) >= 0.6f * extent) { depth += p.z; ++outer; }
        }
        if (extent > 2 * g.h && outer) {
            fit.shoulder = {cx + (left ? 0.75f : -0.75f) * extent, shoulderY, depth / static_cast<float>(outer)};
            warnings.push_back(std::string("The ") + side + " arm touches the body; its shoulder was placed from body proportions.");
        }
    }
    std::vector<XMFLOAT3> line{fit.shoulder};
    std::vector<size_t> lineBin{SIZE_MAX};
    for (size_t b = last; b >= 1; --b)
        if (count[b]) { line.push_back(at(b)); lineBin.push_back(b); }
    line.push_back(tip);
    lineBin.push_back(SIZE_MAX);
    std::vector<float> arc(line.size(), 0);
    for (size_t i = 1; i < line.size(); ++i) arc[i] = arc[i - 1] + Length(Sub(line[i], line[i - 1]));
    const float total = arc.back();
    if (total < 0.08f * height) throw std::runtime_error(std::string("The ") + side + " arm is too short to rig.");
    if (fit.shoulder.y <= hipY || std::abs(fit.shoulder.x - cx) >= reach)
        throw std::runtime_error(std::string("Could not find the ") + side + " shoulder; arms should hang away from the body (T or A pose).");
    auto pointAt = [&](float fraction) {
        const float target = fraction * total;
        size_t i = 1;
        while (i + 1 < line.size() && arc[i] < target) ++i;
        const float segment = arc[i] - arc[i - 1];
        return Lerp(line[i - 1], line[i], segment > 0 ? (target - arc[i - 1]) / segment : 0.0f);
    };
    fit.elbow = pointAt(0.42f);
    fit.wrist = pointAt(0.72f);
    size_t wristBin = SIZE_MAX;
    float wristFraction = 0;
    for (size_t i = 1; i + 1 < line.size(); ++i) {
        const float f = arc[i] / total;
        if (f < 0.62f || f > 0.82f) continue;
        const size_t b = lineBin[i];
        if (wristBin == SIZE_MAX || count[b] < count[wristBin]
            || (count[b] == count[wristBin] && std::abs(f - 0.72f) < std::abs(wristFraction - 0.72f))) {
            wristBin = b; wristFraction = f; fit.wrist = line[i];
        }
    }
    // An arm fused to the body along its whole length lets the front leak into the torso, so the centroids
    // drift toward the body axis; a hanging arm is nearly straight, so use the shoulder-to-tip line instead.
    const float shoulderOffset = std::abs(fit.shoulder.x - cx);
    if (std::abs(fit.elbow.x - cx) < 0.6f * shoulderOffset || std::abs(fit.wrist.x - cx) < 0.6f * shoulderOffset) {
        fit.elbow = Lerp(fit.shoulder, tip, 0.42f);
        fit.wrist = Lerp(fit.shoulder, tip, 0.72f);
        warnings.push_back(std::string("The ") + side + " arm rests against the body; its elbow and wrist were placed on a straight line.");
    }
    return fit;
}

// ---- Skin weights --------------------------------------------------------------------------------
struct Corners { std::array<size_t, 8> cells{}; std::array<float, 8> weights{}; };
// Trilinear sample of the cell-centred field, restricted to solid cells when any are present.
Corners Sample(const Grid& g, const XMFLOAT3& p) {
    const float ux = (p.x - g.origin.x) / g.h - 0.5f, uy = (p.y - g.origin.y) / g.h - 0.5f, uz = (p.z - g.origin.z) / g.h - 0.5f;
    const int i0 = std::clamp(static_cast<int>(std::floor(ux)), 0, g.nx - 2);
    const int j0 = std::clamp(static_cast<int>(std::floor(uy)), 0, g.ny - 2);
    const int k0 = std::clamp(static_cast<int>(std::floor(uz)), 0, g.nz - 2);
    const float fx = std::clamp(ux - static_cast<float>(i0), 0.0f, 1.0f);
    const float fy = std::clamp(uy - static_cast<float>(j0), 0.0f, 1.0f);
    const float fz = std::clamp(uz - static_cast<float>(k0), 0.0f, 1.0f);
    Corners s;
    float solid = 0;
    for (int corner = 0; corner < 8; ++corner) {
        const int di = corner & 1, dj = (corner >> 1) & 1, dk = (corner >> 2) & 1;
        const size_t c = g.Index(i0 + di, j0 + dj, k0 + dk);
        s.cells[static_cast<size_t>(corner)] = c;
        s.weights[static_cast<size_t>(corner)] = (di ? fx : 1 - fx) * (dj ? fy : 1 - fy) * (dk ? fz : 1 - fz);
        if (g.solid[c]) solid += s.weights[static_cast<size_t>(corner)];
    }
    if (solid > 1e-6f)
        for (size_t corner = 0; corner < 8; ++corner)
            s.weights[corner] = g.solid[s.cells[corner]] ? s.weights[corner] / solid : 0.0f;
    return s;
}
std::vector<size_t> SegmentSeeds(const Grid& g, const XMFLOAT3& a, const XMFLOAT3& b) {
    const int steps = std::max(1, static_cast<int>(std::ceil(Length(Sub(b, a)) / (0.5f * g.h))));
    std::vector<size_t> all;
    for (int i = 0; i <= steps; ++i)
        all.push_back(g.CellAt(Lerp(a, b, static_cast<float>(i) / static_cast<float>(steps))));
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    std::vector<size_t> inside;
    for (const size_t c : all) if (g.solid[c]) inside.push_back(c);
    return inside.empty() ? all : inside;
}
struct Influence { std::array<uint8_t, 4> joints{}; std::array<float, 4> weights{}; };
// Weights fall off with the geodesic distance through the body beyond the nearest bone.
std::vector<Influence> SkinWeights(const Grid& g, const Legs& legs, const std::array<XMFLOAT3, JointCount>& joints,
    const std::vector<XMFLOAT3>& points, const std::vector<uint32_t>& triangles, std::vector<uint32_t>& weld) {
    std::vector<int> weighted;
    for (int j = 0; j < JointCount; ++j) if (Joints[j].weighted) weighted.push_back(j);
    const size_t boneCount = weighted.size();
    std::vector<std::vector<size_t>> seeds(boneCount);
    std::vector<std::vector<std::pair<int, int>>> segments(boneCount);
    std::vector<size_t> allSeeds;
    for (size_t b = 0; b < boneCount; ++b) {
        const int j = weighted[b];
        if (j == JHips) segments[b] = {{JHips, JSpine}, {JHips, JLeftUpperLeg}, {JHips, JRightUpperLeg}};
        else segments[b] = {{j, j + 1}};
        for (const auto& segment : segments[b]) {
            const std::vector<size_t> cells = SegmentSeeds(g, joints[static_cast<size_t>(segment.first)], joints[static_cast<size_t>(segment.second)]);
            seeds[b].insert(seeds[b].end(), cells.begin(), cells.end());
        }
        allSeeds.insert(allSeeds.end(), seeds[b].begin(), seeds[b].end());
    }
    // Weld coincident vertices so seams receive identical weights.
    const float quantum = g.h * 1e-3f;
    XMFLOAT3 lo{FLT_MAX, FLT_MAX, FLT_MAX};
    for (const XMFLOAT3& p : points) lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    weld.assign(points.size(), 0);
    std::vector<size_t> representative;
    std::unordered_map<uint64_t, uint32_t> keys;
    keys.reserve(points.size());
    auto quantize = [&](float value, float start) { return static_cast<uint64_t>(std::llround((value - start) / quantum)) & 0x1FFFFFu; };
    for (size_t v = 0; v < points.size(); ++v) {
        const uint64_t key = quantize(points[v].x, lo.x) << 42 | quantize(points[v].y, lo.y) << 21 | quantize(points[v].z, lo.z);
        const auto inserted = keys.emplace(key, static_cast<uint32_t>(representative.size()));
        if (inserted.second) representative.push_back(v);
        weld[v] = inserted.first->second;
    }
    const size_t vertexCount = representative.size();
    std::vector<Corners> corners(vertexCount);
    for (size_t u = 0; u < vertexCount; ++u) corners[u] = Sample(g, points[representative[u]]);
    auto cost = [&](size_t c, size_t n, uint32_t base) {
        const uint8_t a = legs.label[c], b = legs.label[n];
        return g.solid[c] && g.solid[n] && !(a && b && a != b) ? base : base * 30;
    };
    Geodesic nearest(g);
    nearest.Run(allSeeds, [](size_t, uint32_t) { return true; }, cost);
    const std::vector<uint32_t>& dmin = nearest.Distances();
    std::vector<float> weights(vertexCount * boneCount, 0.0f);
    std::atomic<size_t> next{0};
    std::exception_ptr failure;
    std::mutex failureMutex;
    auto worker = [&]() {
        try {
            Geodesic geo(g);
            for (size_t b = next++; b < boneCount; b = next++) {
                geo.Reset();
                geo.Run(seeds[b], [&](size_t n, uint32_t nd) { return dmin[n] != Unreached && nd <= dmin[n] + Cutoff; }, cost);
                const std::vector<uint32_t>& dist = geo.Distances();
                for (size_t u = 0; u < vertexCount; ++u) {
                    const Corners& s = corners[u];
                    float d = 0, dm = 0;
                    bool reached = true;
                    for (size_t corner = 0; corner < 8; ++corner) {
                        const float w = s.weights[corner];
                        if (!(w > 0)) continue;
                        const size_t c = s.cells[corner];
                        if (dist[c] == Unreached || dmin[c] == Unreached) { reached = false; break; }
                        d += w * static_cast<float>(dist[c]);
                        dm += w * static_cast<float>(dmin[c]);
                    }
                    if (reached) weights[u * boneCount + b] = std::exp(-std::max(0.0f, d - dm) / Sigma);
                }
            }
        } catch (...) {
            const std::lock_guard<std::mutex> lock(failureMutex);
            if (!failure) failure = std::current_exception();
            next = boneCount;
        }
    };
    const size_t memory = g.Count() * (sizeof(uint32_t) + sizeof(size_t)) + 1;
    const size_t threads = std::max<size_t>(1, std::min<size_t>({static_cast<size_t>(std::max(1u, std::thread::hardware_concurrency())),
        size_t{9}, boneCount, (size_t{512} << 20) / memory}));
    std::vector<std::thread> pool;
    try {
        for (size_t t = 1; t < threads; ++t) pool.emplace_back(worker);
    } catch (...) {} // Fewer helper threads only make this slower.
    worker();
    for (std::thread& t : pool) t.join();
    if (failure) std::rethrow_exception(failure);
    for (size_t u = 0; u < vertexCount; ++u) {
        float* w = &weights[u * boneCount];
        float sum = 0;
        for (size_t b = 0; b < boneCount; ++b) sum += w[b];
        if (sum > 0 && std::isfinite(sum)) { for (size_t b = 0; b < boneCount; ++b) w[b] /= sum; continue; }
        size_t closest = 0;
        float distance = FLT_MAX;
        for (size_t b = 0; b < boneCount; ++b)
            for (const auto& segment : segments[b]) {
                const float e = SegmentDistance(points[representative[u]], joints[static_cast<size_t>(segment.first)], joints[static_cast<size_t>(segment.second)]);
                if (e < distance) { distance = e; closest = b; }
            }
        std::fill(w, w + boneCount, 0.0f);
        w[closest] = 1;
    }
    // Hoods, hair and hats can drape onto the shoulders, which makes the clavicles geodesically closer than the
    // head's centre. Blend everything above the neck joint into the neck/head chain unless an arm dominates it.
    const float neckY = joints[JNeck].y, headY = joints[JHead].y;
    const size_t neckBone = static_cast<size_t>(std::find(weighted.begin(), weighted.end(), JNeck) - weighted.begin());
    const size_t headBone = static_cast<size_t>(std::find(weighted.begin(), weighted.end(), JHead) - weighted.begin());
    if (headY > neckY)
        for (size_t u = 0; u < vertexCount; ++u) {
            const float y = points[representative[u]].y;
            if (y <= neckY) continue;
            float* w = &weights[u * boneCount];
            const int dominant = weighted[static_cast<size_t>(std::max_element(w, w + boneCount) - w)];
            if (dominant == JLeftUpperArm || dominant == JLeftForeArm || dominant == JLeftHand
                || dominant == JRightUpperArm || dominant == JRightForeArm || dominant == JRightHand) continue;
            const float s = std::min(1.0f, (y - neckY) / (headY - neckY)), t = s * s * (3 - 2 * s);
            for (size_t b = 0; b < boneCount; ++b) w[b] *= 1 - t;
            w[neckBone] += t * (1 - s);
            w[headBone] += t * s;
        }
    // Two Laplacian passes over the welded triangle graph soften the remaining seams.
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    edges.reserve(triangles.size() * 2);
    for (size_t t = 0; t + 2 < triangles.size(); t += 3)
        for (size_t e = 0; e < 3; ++e) {
            const uint32_t a = weld[triangles[t + e]], b = weld[triangles[t + (e + 1) % 3]];
            if (a != b) { edges.push_back({a, b}); edges.push_back({b, a}); }
        }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    std::vector<size_t> start(vertexCount + 1, 0);
    for (const auto& edge : edges) ++start[edge.first + 1];
    for (size_t u = 0; u < vertexCount; ++u) start[u + 1] += start[u];
    std::vector<float> smoothed(weights.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t u = 0; u < vertexCount; ++u) {
            const size_t degree = start[u + 1] - start[u];
            for (size_t b = 0; b < boneCount; ++b) {
                float mean = 0;
                for (size_t e = start[u]; e < start[u + 1]; ++e) mean += weights[edges[e].second * boneCount + b];
                smoothed[u * boneCount + b] = degree ? 0.5f * weights[u * boneCount + b] + 0.5f * mean / static_cast<float>(degree)
                                                     : weights[u * boneCount + b];
            }
        }
        weights.swap(smoothed);
    }
    // Contacts such as a hand resting on a skirt leak a little weight from one limb into another, so posing an arm
    // would tug the legs. Fade influences from other limbs that are minor next to the vertex's own limb; genuine
    // blends at a fused contact (comparable weights) are kept, and the fade is continuous across the surface.
    std::vector<int> limb(boneCount, -1);
    for (size_t b = 0; b < boneCount; ++b)
        if (weighted[b] >= JLeftShoulder) limb[b] = (weighted[b] - JLeftShoulder) / 5; // Left/right arm, left/right leg.
    for (size_t u = 0; u < vertexCount; ++u) {
        float* w = &weights[u * boneCount];
        std::array<float, 4> group{};
        for (size_t b = 0; b < boneCount; ++b) if (limb[b] >= 0) group[static_cast<size_t>(limb[b])] += w[b];
        const size_t own = static_cast<size_t>(std::max_element(group.begin(), group.end()) - group.begin());
        if (!(group[own] > 0)) continue;
        for (size_t b = 0; b < boneCount; ++b) {
            if (limb[b] < 0 || static_cast<size_t>(limb[b]) == own) continue;
            const float s = std::clamp((group[static_cast<size_t>(limb[b])] / group[own] - 0.05f) / 0.2f, 0.0f, 1.0f);
            w[b] *= s * s * (3 - 2 * s);
        }
    }
    std::vector<Influence> influences(vertexCount);
    std::vector<size_t> order(boneCount);
    for (size_t u = 0; u < vertexCount; ++u) {
        const float* w = &weights[u * boneCount];
        for (size_t b = 0; b < boneCount; ++b) order[b] = b;
        std::partial_sort(order.begin(), order.begin() + 4, order.end(), [&](size_t a, size_t b) { return w[a] > w[b]; });
        float sum = 0;
        for (size_t k = 0; k < 4; ++k) if (w[order[k]] >= 0.005f) sum += w[order[k]];
        Influence& influence = influences[u];
        for (size_t k = 0; k < 4; ++k) {
            influence.joints[k] = static_cast<uint8_t>(weighted[order[k]]);
            influence.weights[k] = w[order[k]] >= 0.005f && sum > 0 ? w[order[k]] / sum : 0.0f;
        }
        if (!(sum > 0)) influence.weights[0] = 1;
    }
    return influences;
}
}

int AutoRigObject(const ModelAsset& model, int object) {
    try {
        const Selection s = Collect(model, object);
        return s.meshes.empty() ? -1 : s.object;
    } catch (const std::exception&) {
        return -1;
    }
}

bool NeedsAutoRig(const ModelAsset& model, int object) {
    try {
        const Selection s = Collect(model, object);
        return !s.meshes.empty() && !s.skinned && !s.humanoid;
    } catch (const std::exception&) {
        return false;
    }
}

AutoRigReport AutoRig(ModelAsset& model, int object) {
    const Selection selection = Collect(model, object);
    if (selection.object < 0 || selection.meshes.empty()) throw std::runtime_error("There is no mesh object to auto-rig.");
    if (selection.humanoid) throw std::runtime_error("This object already has a humanoid skeleton.");
    const Surface surface = Gather(model, selection);
    const float height = surface.hi.y - surface.lo.y;
    const float width = std::max(surface.hi.x - surface.lo.x, surface.hi.z - surface.lo.z);
    if (!(height > 1e-6f) || height < 0.4f * width)
        throw std::runtime_error("Auto-rig needs an upright character standing along +Y.");
    AutoRigReport report;
    report.object = selection.object;
    report.vertices = surface.positions.size();
    report.meshes = selection.meshes.size();
    report.height = height;
    std::vector<std::string>& warnings = report.warnings;

    Grid grid = Voxelize(surface.positions, surface.triangles);
    Legs legs = FindLegs(grid);
    report.forward = DetectForward(grid, legs, warnings);
    const Frame frame(report.forward);
    std::vector<XMFLOAT3> points = surface.positions;
    if (std::abs(report.forward.z + 1) > 1e-4f) {
        for (XMFLOAT3& p : points) p = frame.Local(p);
        grid = Voxelize(points, surface.triangles);
        legs = FindLegs(grid);
    }
    if (!legs.found) warnings.push_back("The legs could not be separated; they were split down the middle.");

    const LegFit leftLeg = FitLeg(grid, legs, 1, height, warnings), rightLeg = FitLeg(grid, legs, 2, height, warnings);
    const float crotchY = grid.Y(legs.crotch) - 0.5f * grid.h, topY = grid.Y(legs.top) + 0.5f * grid.h;
    const float hipY = 0.5f * (leftLeg.hip.y + rightLeg.hip.y), cx = 0.5f * (leftLeg.hip.x + rightLeg.hip.x);
    bool neckFound = false;
    const float neckY = FindNeck(grid, legs, crotchY, topY, height, neckFound, warnings);
    Geodesic geo(grid);
    const ArmFit leftArm = TraceArm(grid, legs, geo, true, cx, hipY, neckY, height, warnings);
    const ArmFit rightArm = TraceArm(grid, legs, geo, false, cx, hipY, neckY, height, warnings);
    // Every landmark has a proportional fallback, so a block or a prop would still "rig"; require real evidence.
    const int cues = (legs.found ? 1 : 0) + (neckFound ? 1 : 0) + (leftArm.branched && rightArm.branched ? 1 : 0);
    if (cues < 2)
        throw std::runtime_error("This mesh does not look like a standing character. Auto-rig needs at least two of: "
            "separate legs, a neck, and arms that branch from the body (T or A pose).");
    const std::vector<uint16_t> dt = DistanceField(grid);
    const float shoulderY = 0.5f * (leftArm.shoulder.y + rightArm.shoulder.y);
    const float neckJoint = std::min(shoulderY + 0.6f * (neckY - shoulderY), neckY);
    const float torso = neckJoint - hipY;
    if (torso <= 0.05f * height) throw std::runtime_error("The torso is too short to rig.");

    std::array<XMFLOAT3, JointCount> j{};
    j[JHips] = {0.5f * (leftLeg.hip.x + rightLeg.hip.x), hipY + 0.08f * torso, 0.5f * (leftLeg.hip.z + rightLeg.hip.z)};
    j[JSpine] = Core(grid, dt, grid.LayerAt(hipY + 0.3f * torso));
    j[JChest] = Core(grid, dt, grid.LayerAt(hipY + 0.6f * torso));
    j[JNeck] = Core(grid, dt, grid.LayerAt(neckY));
    j[JNeck].y = neckJoint;
    j[JHead] = {j[JNeck].x, neckY + 0.02f * height, j[JNeck].z};
    j[JHeadTop] = {j[JHead].x, topY, j[JHead].z};
    auto arm = [&](const ArmFit& fit, int first) {
        j[static_cast<size_t>(first)] = {j[JNeck].x + 0.25f * (fit.shoulder.x - j[JNeck].x),
            fit.shoulder.y + 0.3f * std::max(0.0f, neckJoint - fit.shoulder.y), fit.shoulder.z};
        j[static_cast<size_t>(first + 1)] = fit.shoulder;
        j[static_cast<size_t>(first + 2)] = fit.elbow;
        j[static_cast<size_t>(first + 3)] = fit.wrist;
        j[static_cast<size_t>(first + 4)] = fit.tip;
    };
    arm(leftArm, JLeftShoulder);
    arm(rightArm, JRightShoulder);
    auto leg = [&](const LegFit& fit, int first) {
        j[static_cast<size_t>(first)] = fit.hip;
        j[static_cast<size_t>(first + 1)] = fit.knee;
        j[static_cast<size_t>(first + 2)] = fit.ankle;
        j[static_cast<size_t>(first + 3)] = fit.toes;
        j[static_cast<size_t>(first + 4)] = fit.toesEnd;
    };
    leg(leftLeg, JLeftUpperLeg);
    leg(rightLeg, JRightUpperLeg);
    // A symmetric body gets a mirror-symmetric skeleton.
    bool symmetric = true;
    for (const auto& pair : Mirrors) {
        const XMFLOAT3 l = j[static_cast<size_t>(pair.first)], r = j[static_cast<size_t>(pair.second)];
        symmetric = symmetric && std::abs((l.x - cx) + (r.x - cx)) < 0.04f * height
            && std::abs(l.y - r.y) < 0.04f * height && std::abs(l.z - r.z) < 0.04f * height;
    }
    if (symmetric)
        for (const auto& pair : Mirrors) {
            XMFLOAT3& l = j[static_cast<size_t>(pair.first)];
            XMFLOAT3& r = j[static_cast<size_t>(pair.second)];
            const float half = 0.5f * ((l.x - cx) - (r.x - cx)), y = 0.5f * (l.y + r.y), z = 0.5f * (l.z + r.z);
            l = {cx + half, y, z};
            r = {cx - half, y, z};
        }
    for (int c : {JSpine, JChest, JNeck, JHead, JHeadTop}) if (symmetric) j[static_cast<size_t>(c)].x = cx;
    if (symmetric) j[JHips].x = cx;

    std::vector<uint32_t> weld;
    const std::vector<Influence> influences = SkinWeights(grid, legs, j, points, surface.triangles, weld);

    // ---- Rebuild the scene with the new bones ----
    const auto& oldBones = model.skeleton.bones;
    const size_t nodeCount = oldBones.size();
    std::vector<uint8_t> referenced(nodeCount, 0), animated(nodeCount, 0), memberMesh(model.meshes.size(), 0);
    for (const size_t m : selection.meshes) memberMesh[m] = 1;
    for (const AnimationClip& clip : model.clips)
        for (const BoneTrack& track : clip.tracks) if (track.bone < nodeCount) animated[track.bone] = 1;
    for (const EffectData& effect : model.effects) if (effect.bone < nodeCount) referenced[effect.bone] = 1;
    for (const SceneObject& o : model.objects) if (o.root < nodeCount) referenced[o.root] = 1;
    for (size_t m = 0; m < model.meshes.size(); ++m)
        if (!memberMesh[m]) for (const SkinBinding& s : model.meshes[m].skin) if (s.bone < nodeCount) referenced[s.bone] = 1;
    std::vector<uint8_t> keep(nodeCount, 0);
    size_t animatedOnly = 0;
    for (size_t b = 0; b < nodeCount; ++b) {
        keep[b] = static_cast<uint8_t>(!selection.member[b] || referenced[b] || animated[b]);
        if (selection.member[b] && animated[b] && !referenced[b]) ++animatedOnly;
    }
    for (size_t b = nodeCount; b-- > 0;)
        if (keep[b] && oldBones[b].parent >= 0) keep[static_cast<size_t>(oldBones[b].parent)] = 1;
    if (animatedOnly)
        warnings.push_back(std::to_string(animatedOnly) + " animated mesh node(s) were kept, but their old keys no longer move the re-skinned mesh.");

    std::vector<int> remap(nodeCount, -1);
    std::vector<Bone> bones;
    std::vector<std::string> oldPath;
    std::vector<uint8_t> renamed;
    std::set<std::string> reserved{"pelvis"}, names;
    for (const JointInfo& info : Joints) { reserved.insert(Normalized(info.name)); names.insert(info.name); }
    for (size_t b = 0; b < nodeCount; ++b) {
        if (!keep[b]) continue;
        remap[b] = static_cast<int>(bones.size());
        Bone bone = oldBones[b];
        if (bone.parent >= 0) bone.parent = remap[static_cast<size_t>(bone.parent)];
        oldPath.push_back(bone.path);
        renamed.push_back(static_cast<uint8_t>(selection.member[b] && reserved.count(Normalized(bone.name))));
        if (!renamed.back()) names.insert(bone.name);
        bones.push_back(std::move(bone));
    }
    std::vector<uint8_t> moved(bones.size(), 0);
    for (size_t i = 0; i < bones.size(); ++i) {
        Bone& bone = bones[i];
        const bool parentMoved = bone.parent >= 0 && moved[static_cast<size_t>(bone.parent)];
        if (!renamed[i] && !parentMoved) continue;
        const std::string parentPath = bone.parent >= 0 ? bones[static_cast<size_t>(bone.parent)].path : std::string();
        if (renamed[i]) {
            std::string name = bone.name + "_Geo";
            for (int n = 2; names.count(name); ++n) name = bone.name + "_Geo" + std::to_string(n);
            names.insert(name);
            warnings.push_back("Renamed '" + bone.name + "' to '" + name + "' to make room for the new bones.");
            bone.name = name;
            bone.path = parentPath + "/" + PathComponent(name);
        } else {
            const std::string& previous = oldPath[static_cast<size_t>(bone.parent)];
            bone.path = bone.path.compare(0, previous.size(), previous) == 0 ? parentPath + bone.path.substr(previous.size())
                                                                            : parentPath + "/" + PathComponent(bone.name);
        }
        moved[i] = 1;
    }
    const uint32_t objectRoot = model.objects[static_cast<size_t>(selection.object)].root;
    const Pose oldPose = EvaluatePose(model.skeleton, nullptr, 0);
    std::array<XMFLOAT4X4, JointCount> jointGlobal{};
    std::array<uint32_t, JointCount> jointBone{};
    for (int joint = 0; joint < JointCount; ++joint) {
        const int parentJoint = Joints[joint].parent;
        const uint32_t parent = parentJoint < 0 ? static_cast<uint32_t>(remap[objectRoot]) : jointBone[static_cast<size_t>(parentJoint)];
        const XMMATRIX parentGlobal = XMLoadFloat4x4(parentJoint < 0 ? &oldPose.global[objectRoot] : &jointGlobal[static_cast<size_t>(parentJoint)]);
        XMVECTOR determinant;
        const XMMATRIX inverse = XMMatrixInverse(&determinant, parentGlobal);
        const XMFLOAT3 world = frame.World(j[static_cast<size_t>(joint)]);
        Transform local;
        try {
            if (!(std::abs(XMVectorGetX(determinant)) > 1e-12f)) throw std::runtime_error("singular");
            local = Decompose(XMMatrixMultiply(XMMatrixTranslation(world.x, world.y, world.z), inverse));
        } catch (const std::exception&) {
            throw std::runtime_error("The object's root transform is singular or sheared, so bones cannot be placed under it.");
        }
        XMStoreFloat4x4(&jointGlobal[static_cast<size_t>(joint)], XMMatrixMultiply(Matrix(local), parentGlobal));
        jointBone[static_cast<size_t>(joint)] = static_cast<uint32_t>(bones.size());
        const std::string path = bones[parent].path + "/" + PathComponent(Joints[joint].name);
        bones.push_back({Joints[joint].name, path, static_cast<int>(parent), local});
        report.bones.push_back(jointBone[static_cast<size_t>(joint)]);
        report.joints.push_back({Joints[joint].name, world});
    }

    ModelAsset result = model;
    result.skeleton.bones = std::move(bones);
    const Pose pose = EvaluatePose(result.skeleton, nullptr, 0);
    for (size_t m = 0; m < result.meshes.size(); ++m)
        if (!memberMesh[m])
            for (SkinBinding& s : result.meshes[m].skin) if (s.bone < nodeCount) s.bone = static_cast<uint32_t>(remap[s.bone]);
    for (size_t index = 0; index < selection.meshes.size(); ++index) {
        MeshData& mesh = result.meshes[selection.meshes[index]];
        const size_t base = surface.offsets[index];
        std::array<int, JointCount> palette;
        palette.fill(-1);
        for (size_t v = 0; v < mesh.vertices.size(); ++v) {
            const Influence& influence = influences[weld[base + v]];
            for (size_t k = 0; k < 4; ++k) if (influence.weights[k] > 0) palette[influence.joints[k]] = 0;
        }
        mesh.skin.clear();
        for (int joint = 0; joint < JointCount; ++joint) {
            if (palette[static_cast<size_t>(joint)] < 0) continue;
            palette[static_cast<size_t>(joint)] = static_cast<int>(mesh.skin.size());
            SkinBinding binding;
            binding.bone = jointBone[static_cast<size_t>(joint)];
            XMStoreFloat4x4(&binding.inverseBind, XMMatrixInverse(nullptr, XMLoadFloat4x4(&pose.global[binding.bone])));
            mesh.skin.push_back(binding);
        }
        for (size_t v = 0; v < mesh.vertices.size(); ++v) {
            Vertex& vertex = mesh.vertices[v];
            const Influence& influence = influences[weld[base + v]];
            vertex.position = surface.positions[base + v];
            vertex.normal = surface.normals[base + v];
            vertex.joints = {};
            vertex.weights = {};
            for (size_t k = 0; k < 4; ++k) {
                if (!(influence.weights[k] > 0)) continue;
                vertex.joints[k] = static_cast<uint32_t>(palette[influence.joints[k]]);
                vertex.weights[k] = influence.weights[k];
            }
        }
    }
    for (AnimationClip& clip : result.clips) {
        for (BoneTrack& track : clip.tracks) if (track.bone < nodeCount) track.bone = static_cast<uint32_t>(remap[track.bone]);
        ++clip.revision;
    }
    for (EffectData& effect : result.effects) if (effect.bone < nodeCount) effect.bone = static_cast<uint32_t>(remap[effect.bone]);
    for (SceneObject& o : result.objects) if (o.root < nodeCount) o.root = static_cast<uint32_t>(remap[o.root]);
    model = std::move(result);
    return report;
}
}
