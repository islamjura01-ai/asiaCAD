#pragma once
// AsiaCAD — Phase 1 primitivlari: Point, Line, Circle, Arc, Polyline, Text
#include <asiacad/core/block.h>
#include <asiacad/core/entity.h>

#include <vector>

namespace asiacad {

class PointEntity final : public IEntity {
public:
    explicit PointEntity(const Vec3& p) : pos_(p) {}
    EntityType type() const override { return EntityType::Point; }
    std::unique_ptr<IEntity> clone() const override { return std::make_unique<PointEntity>(*this); }
    BBox bbox() const override;
    void transform(const Transform& t) override { pos_ = t.apply(pos_); }
    void writeDxf(DxfWriter& w) const override;
    std::string describe() const override;

    const Vec3& position() const { return pos_; }

private:
    Vec3 pos_;
};

class LineEntity final : public ICurve {
public:
    LineEntity(const Vec3& a, const Vec3& b) : a_(a), b_(b) {}
    EntityType type() const override { return EntityType::Line; }
    std::unique_ptr<IEntity> clone() const override { return std::make_unique<LineEntity>(*this); }
    BBox bbox() const override;
    void transform(const Transform& t) override { a_ = t.apply(a_); b_ = t.apply(b_); }
    void writeDxf(DxfWriter& w) const override;
    std::string describe() const override;

    double length() const override { return a_.distanceTo(b_); }
    Vec3 startPoint() const override { return a_; }
    Vec3 endPoint() const override { return b_; }
    Vec3 pointAtDistance(double s) const override;
    Vec3 closestPoint(const Vec3& p) const override;

private:
    Vec3 a_, b_;
};

class CircleEntity final : public ICurve {
public:
    CircleEntity(const Vec3& c, double r) : center_(c), radius_(r) {}
    EntityType type() const override { return EntityType::Circle; }
    std::unique_ptr<IEntity> clone() const override { return std::make_unique<CircleEntity>(*this); }
    BBox bbox() const override;
    void transform(const Transform& t) override;
    void writeDxf(DxfWriter& w) const override;
    std::string describe() const override;

    double length() const override { return 2 * kPi * radius_; }
    Vec3 startPoint() const override { return center_ + Vec3{radius_, 0, 0}; }
    Vec3 endPoint() const override { return startPoint(); }
    bool isClosed() const override { return true; }
    Vec3 pointAtDistance(double s) const override;
    Vec3 closestPoint(const Vec3& p) const override;

    const Vec3& center() const { return center_; }
    double radius() const { return radius_; }
    double area() const { return kPi * radius_ * radius_; }

private:
    Vec3 center_;
    double radius_;
};

// Yoy: soat strelkasiga qarshi (CCW), burchaklar radianda
class ArcEntity final : public ICurve {
public:
    ArcEntity(const Vec3& c, double r, double startRad, double endRad);
    // Uch nuqta orqali yoy (boshi, o'rtasi, oxiri). Kollinear bo'lsa std::invalid_argument.
    static ArcEntity fromThreePoints(const Vec3& p1, const Vec3& p2, const Vec3& p3);

    EntityType type() const override { return EntityType::Arc; }
    std::unique_ptr<IEntity> clone() const override { return std::make_unique<ArcEntity>(*this); }
    BBox bbox() const override;
    void transform(const Transform& t) override;
    void writeDxf(DxfWriter& w) const override;
    std::string describe() const override;

    double length() const override { return radius_ * sweep(); }
    Vec3 startPoint() const override { return pointAtAngle(start_); }
    Vec3 endPoint() const override { return pointAtAngle(end_); }
    Vec3 pointAtDistance(double s) const override;
    Vec3 closestPoint(const Vec3& p) const override;

    const Vec3& center() const { return center_; }
    double radius() const { return radius_; }
    double startAngle() const { return start_; }
    double endAngle() const { return end_; }
    double sweep() const;  // (0, 2π]
    bool containsAngle(double a) const;
    Vec3 pointAtAngle(double a) const;

private:
    Vec3 center_;
    double radius_, start_, end_;
};

// Poliliniya. Har bir uchda bulge (DXF ma'nosida): shu uchdan keyingi segment yoyi,
// bulge = tan(θ/4), θ>0 — CCW. 0 — to'g'ri segment.
class PolylineEntity final : public ICurve {
public:
    explicit PolylineEntity(std::vector<Vec3> pts, bool closed = false);
    PolylineEntity(std::vector<Vec3> pts, std::vector<double> bulges, bool closed);
    EntityType type() const override { return EntityType::Polyline; }
    std::unique_ptr<IEntity> clone() const override { return std::make_unique<PolylineEntity>(*this); }
    BBox bbox() const override;
    void transform(const Transform& t) override;
    void writeDxf(DxfWriter& w) const override;
    std::string describe() const override;

    double length() const override;
    Vec3 startPoint() const override { return pts_.empty() ? Vec3{} : pts_.front(); }
    Vec3 endPoint() const override { return pts_.empty() ? Vec3{} : (closed_ ? pts_.front() : pts_.back()); }
    bool isClosed() const override { return closed_; }
    Vec3 pointAtDistance(double s) const override;
    Vec3 closestPoint(const Vec3& p) const override;

    const std::vector<Vec3>& vertices() const { return pts_; }
    const std::vector<double>& bulges() const { return bulges_; }
    bool hasBulges() const;
    bool is3d() const;  // uchlar balandligi har xil
    std::size_t segmentCount() const;
    // Yopiq poliliniya uchun XY maydon (Gauss formulasi + yoy segmentlari), ishorasiz
    double area() const;
    double signedArea() const;  // >0 — CCW
    // Fazoviy (3D) uzunlik vs gorizontal (2D) uzunlik — geodeziya uchun alohida
    double length2d() const;
    // Yoy segmentlarini to'g'ri chiziqlarga bo'lish (maxStep — burchak qadami, radian)
    PolylineEntity tessellated(double maxStepRad = deg2rad(5.0)) const;
    // AutoCAD EXPLODE: to'g'ri segmentlar — LINE, yoy segmentlar — ARC
    std::vector<std::unique_ptr<IEntity>> explodeSegments() const;
    // Nuqta konturning ichidami (XY, yopiq deb hisoblanadi, yoylar bo'linib tekshiriladi)
    bool containsPoint2d(const Vec3& p) const;

private:
    struct ArcSeg {
        Vec3 center;
        double radius, startAngle, sweep;  // sweep ishorali
    };
    Vec3 segStart(std::size_t i) const { return pts_[i]; }
    Vec3 segEnd(std::size_t i) const { return pts_[(i + 1) % pts_.size()]; }
    bool segIsArc(std::size_t i) const;
    ArcSeg arcOf(std::size_t i) const;
    double segLength(std::size_t i, bool in3d) const;
    Vec3 segPointAt(std::size_t i, double s) const;
    Vec3 segClosest(std::size_t i, const Vec3& p) const;

    std::vector<Vec3> pts_;
    std::vector<double> bulges_;
    bool closed_;
};

enum class TextHAlign { Left, Center, Right };
enum class TextVAlign { Baseline, Bottom, Middle, Top };

// Bir qatorli matn. ins — langar nuqta: tekislashga ko'ra (Left+Baseline bo'lsa — chap-pastki tayanch chiziq).
class TextEntity final : public IEntity {
public:
    TextEntity(const Vec3& ins, double height, std::string utf8, double rotRad = 0,
               TextHAlign h = TextHAlign::Left, TextVAlign v = TextVAlign::Baseline)
        : ins_(ins), height_(height), rot_(rotRad), text_(std::move(utf8)), h_(h), v_(v) {}
    EntityType type() const override { return EntityType::Text; }
    std::unique_ptr<IEntity> clone() const override { return std::make_unique<TextEntity>(*this); }
    BBox bbox() const override;  // taxminiy (shrift metrikasiz: kenglik ≈ 0.6·h·belgilar)
    void transform(const Transform& t) override;
    void writeDxf(DxfWriter& w) const override;
    std::string describe() const override;

    const std::string& text() const { return text_; }
    const Vec3& insertion() const { return ins_; }
    double height() const { return height_; }
    double rotation() const { return rot_; }
    TextHAlign hAlign() const { return h_; }
    TextVAlign vAlign() const { return v_; }
    double estimatedWidth() const;
    // ATTRIB sifatida yozish (R12: vertikal tekislash 74-kodda)
    void writeDxfAttrib(DxfWriter& w, const std::string& tag) const;
    // Langardan chap-tayanch nuqtagacha siljish (lokal, burilmagan)
    Vec3 localOffsetToBaselineLeft() const;

private:
    Vec3 ins_;
    double height_, rot_;
    std::string text_;
    TextHAlign h_;
    TextVAlign v_;
};

// ---------------------------------------------------------------- INSERT
struct Attribute {
    std::string tag;
    TextEntity text;  // WCS'da joylashgan qiymat
};

// Blok havolasi. Joylashuv — to'liq affin matritsa (blok koordinatasi -> WCS): har qanday
// burilish, ko'zgu, notekis masshtab va ichma-ich transform aniq saqlanadi.
class InsertEntity final : public IEntity {
public:
    InsertEntity(BlockPtr def, const Transform& m) : def_(std::move(def)), m_(m) {}
    // AutoCAD parametrlari: P = ins + R(rot)·S(sx,sy,sz)·(p - base)
    static Transform placement(const Vec3& base, const Vec3& ins, double sx, double sy, double sz, double rotRad);

    EntityType type() const override { return EntityType::Insert; }
    std::unique_ptr<IEntity> clone() const override { return std::make_unique<InsertEntity>(*this); }
    BBox bbox() const override;
    void transform(const Transform& t) override;
    void writeDxf(DxfWriter& w) const override;
    std::string describe() const override;

    const BlockPtr& block() const { return def_; }
    const Transform& matrix() const { return m_; }
    Vec3 insertionPoint() const { return m_.apply(def_->base); }
    // R(rot)·diag(sx,sy) ko'rinishiga ajratish; qiyshiq (skew) bo'lsa false
    bool decompose(double& sx, double& sy, double& rotRad) const;

    const std::vector<Attribute>& attributes() const { return attribs_; }
    void addAttribute(std::string tag, TextEntity text) { attribs_.push_back({std::move(tag), std::move(text)}); }

    // Bir daraja explode: blok obyektlari WCS'da; "0" qatlam va BYBLOCK rang INSERT'dan olinadi
    std::vector<std::unique_ptr<IEntity>> explode() const;

private:
    BlockPtr def_;
    Transform m_;
    std::vector<Attribute> attribs_;
};

// ---------------------------------------------------------------- HATCH
// Shtrixlash/to'ldirish. Konturlar — yopiq bulge'li poliliniyalar; maydon even-odd qoidasi bilan
// (ichki kontur — teshik). Naqsh nomi va parametrlari saqlanadi; chizish viewport bosqichida.
class HatchEntity final : public IEntity {
public:
    HatchEntity(std::vector<PolylineEntity> loops, std::string pattern, double patternScale = 1.0, double patternAngle = 0.0);
    EntityType type() const override { return EntityType::Hatch; }
    std::unique_ptr<IEntity> clone() const override { return std::make_unique<HatchEntity>(*this); }
    BBox bbox() const override;
    void transform(const Transform& t) override;
    void writeDxf(DxfWriter& w) const override;  // R12'da HATCH yo'q — konturlar yopiq POLYLINE bo'lib yoziladi
    std::string describe() const override;

    const std::vector<PolylineEntity>& loops() const { return loops_; }
    const std::string& pattern() const { return pattern_; }
    bool isSolid() const;
    double patternScale() const { return scale_; }
    double patternAngle() const { return angle_; }
    double area() const;

private:
    std::vector<PolylineEntity> loops_;
    std::string pattern_;
    double scale_, angle_;
};

// 3D sirt (AutoCAD PolyFace / Polygon mesh, 3D model): uchlar va yuzlar (3 yoki 4 uchli; d = -1 — uchburchak).
// Qirraning ko'rinmasligi DXF'dagidek: indeks manfiy bo'lsa (1 dan boshlab) — shu uchdan boshlanadigan qirra chizilmaydi.
class MeshEntity final : public IEntity {
public:
    struct Face { int a = 0, b = 0, c = 0, d = -1; unsigned hidden = 0; };  // hidden: 1 — ab, 2 — bc, 4 — cd/ca, 8 — da
    MeshEntity(std::vector<Vec3> v, std::vector<Face> f) : v_(std::move(v)), f_(std::move(f)) {}
    EntityType type() const override { return EntityType::Mesh; }
    std::unique_ptr<IEntity> clone() const override { return std::make_unique<MeshEntity>(*this); }
    BBox bbox() const override;
    void transform(const Transform& t) override { for (auto& p : v_) p = t.apply(p); }
    void writeDxf(DxfWriter& w) const override;   // POLYLINE 70=64 (polyface)
    std::string describe() const override;

    const std::vector<Vec3>& vertices() const { return v_; }
    const std::vector<Face>& faces() const { return f_; }
    // Ko'rinadigan qirralar (takrorlarsiz) — planda chizish, tanlash, bog'lanish uchun
    std::vector<std::pair<Vec3, Vec3>> edges() const;
    double area3d() const;

private:
    std::vector<Vec3> v_;
    std::vector<Face> f_;
};

// ---------------------------------------------------------------- yordamchilar
// Aylana/yoy/poliliniyani bulge'li poliliniyaga (aniq). Boshqa turlar uchun nullptr.
std::unique_ptr<PolylineEntity> toBulgePolyline(const IEntity& e);
// Transformatsiyalangan nusxa; notekis masshtabda aylana/yoy poliliniyaga aylanadi (ellips taqribi)
std::unique_ptr<IEntity> transformedCopy(const IEntity& e, const Transform& t);

} // namespace asiacad
