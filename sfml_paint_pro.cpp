////////////////////////////////////////////////////////////
//  SFML Paint Pro — Native Desktop Application
//  Requires: SFML 2.5+,  C++17
//
//  Linux / macOS:
//    g++ -std=c++17 -O2 -o sfml_paint sfml_paint_pro.cpp \
//        -lsfml-graphics -lsfml-window -lsfml-system
//
//  Windows (MinGW):
//    g++ -std=c++17 -O2 -o sfml_paint.exe sfml_paint_pro.cpp \
//        -lsfml-graphics -lsfml-window -lsfml-system -mwindows
//
//  NOTE: Put a font file (arial.ttf or any .ttf) next to the
//        executable. The app will also try system font paths.
//
//  Tools   : Select/Move, Pencil, Eraser, Bucket Fill, Text
//            Line, Rect, Rounded-Rect, Ellipse, Triangle,
//            Right-Triangle, Pentagon, Hexagon, Octagon,
//            Star-5, Star-6, Arrow, Parallelogram, Diamond,
//            Cross, Heart, Semicircle, Arc
//  Colors  : Full HSL+RGB "Edit Colors" dialog (MS-Paint style)
//            40 basic colours + 16 custom slots
//  Blending: Normal, Multiply, Screen, Add, Subtract,
//            Overlay, Darken, Lighten
//  Other   : Fill toggle, Opacity, Stroke size, Zoom,
//            Grid, Undo (40 steps), Save PNG,
//            Live SFML code shown in right panel + console
////////////////////////////////////////////////////////////

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/System.hpp>

#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <deque>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────
//  Layout constants
// ─────────────────────────────────────────────────────────
static const int WIN_W   = 1340;
static const int WIN_H   = 800;
static const int TB_H    = 88;    // toolbar height
static const int SB_H    = 20;    // status bar height
static const int LEFT_W  = 180;   // left panel
static const int RIGHT_W = 260;   // right code panel
static const int CVS_X   = LEFT_W;
static const int CVS_Y   = TB_H;
static const int CVS_W   = WIN_W - LEFT_W - RIGHT_W;  // 900
static const int CVS_H   = WIN_H - TB_H - SB_H;       // 692
static const unsigned MAX_UNDO = 40;

// ─────────────────────────────────────────────────────────
//  Colour helpers
// ─────────────────────────────────────────────────────────
struct HSL { float h = 0, s = 1, l = 0.5f; };

static HSL rgbToHsl(sf::Color c)
{
    float r = c.r/255.f, g = c.g/255.f, b = c.b/255.f;
    float mx = std::max({r,g,b}), mn = std::min({r,g,b});
    float l2 = (mx+mn)/2.f, s2 = 0, h2 = 0;
    if (mx != mn) {
        float d = mx - mn;
        s2 = l2 > 0.5f ? d/(2-mx-mn) : d/(mx+mn);
        if      (mx==r) h2 = std::fmod((g-b)/d + (g<b?6:0), 6.f);
        else if (mx==g) h2 = (b-r)/d + 2;
        else            h2 = (r-g)/d + 4;
        h2 /= 6.f;
    }
    return {h2, s2, l2};
}

static sf::Color hslToRgb(HSL hsl)
{
    float h = hsl.h, s = hsl.s, l = hsl.l;
    if (s == 0) { auto v = (sf::Uint8)(l*255); return {v,v,v}; }
    auto hue2 = [](float p, float q, float t) {
        if (t<0) t+=1; if (t>1) t-=1;
        if (t<1/6.f) return p+(q-p)*6*t;
        if (t<0.5f)  return q;
        if (t<2/3.f) return p+(q-p)*(2/3.f-t)*6;
        return p;
    };
    float q = l<0.5f ? l*(1+s) : l+s-l*s, p = 2*l-q;
    return { (sf::Uint8)(hue2(p,q,h+1/3.f)*255),
             (sf::Uint8)(hue2(p,q,h)*255),
             (sf::Uint8)(hue2(p,q,h-1/3.f)*255) };
}

static std::string colStr(sf::Color c, float alpha = 1.f)
{
    int a = std::min(255,(int)(alpha*255));
    if (a >= 255)
        return "sf::Color("+std::to_string(c.r)+","+std::to_string(c.g)+","+std::to_string(c.b)+")";
    return "sf::Color("+std::to_string(c.r)+","+std::to_string(c.g)+","+std::to_string(c.b)+","+std::to_string(a)+")";
}

// ─────────────────────────────────────────────────────────
//  Enums
// ─────────────────────────────────────────────────────────
enum class Tool {
    Select, Pencil, Eraser, Bucket, Text,
    Line, Rect, RRect, Ellipse,
    Triangle, RTriangle, Pentagon, Hexagon, Octagon,
    Star5, Star6, Arrow, Parallelogram, Diamond, Cross,
    Heart, Semicircle, Arc
};
static const char* toolName(Tool t) {
    switch(t){
    case Tool::Select:       return "Select";
    case Tool::Pencil:       return "Pencil";
    case Tool::Eraser:       return "Eraser";
    case Tool::Bucket:       return "Bucket";
    case Tool::Text:         return "Text";
    case Tool::Line:         return "Line";
    case Tool::Rect:         return "Rect";
    case Tool::RRect:        return "RRect";
    case Tool::Ellipse:      return "Ellipse";
    case Tool::Triangle:     return "Triangle";
    case Tool::RTriangle:    return "RTriangle";
    case Tool::Pentagon:     return "Pentagon";
    case Tool::Hexagon:      return "Hexagon";
    case Tool::Octagon:      return "Octagon";
    case Tool::Star5:        return "Star5";
    case Tool::Star6:        return "Star6";
    case Tool::Arrow:        return "Arrow";
    case Tool::Parallelogram:return "Parallelogram";
    case Tool::Diamond:      return "Diamond";
    case Tool::Cross:        return "Cross";
    case Tool::Heart:        return "Heart";
    case Tool::Semicircle:   return "Semicircle";
    case Tool::Arc:          return "Arc";
    default:                 return "Unknown";
    }
}

enum class BM { Normal, Multiply, Screen, Add, Subtract, Overlay, Darken, Lighten };
static const char* bmName(BM b) {
    switch(b){
    case BM::Multiply: return "Multiply";
    case BM::Screen:   return "Screen";
    case BM::Add:      return "Add";
    case BM::Subtract: return "Subtract";
    case BM::Overlay:  return "Overlay";
    case BM::Darken:   return "Darken";
    case BM::Lighten:  return "Lighten";
    default:           return "Normal";
    }
}
static sf::BlendMode sfBlend(BM b) {
    if (b == BM::Multiply) return sf::BlendMultiply;
    if (b == BM::Add)      return sf::BlendAdd;
    return sf::BlendAlpha;
}

// ─────────────────────────────────────────────────────────
//  Shape data
// ─────────────────────────────────────────────────────────
struct ShapeObj {
    Tool         type    = Tool::Rect;
    sf::Color    sc      = sf::Color::Black;
    sf::Color    fc      = sf::Color::White;
    float        opacity = 1.f;
    int          sz      = 2;
    bool         filled  = false;
    BM           blend   = BM::Normal;
    sf::Vector2f p1, p2;
    std::vector<sf::Vector2f> pts;
    std::string  text;
    int          fontSize = 18;
};

// ─────────────────────────────────────────────────────────
//  Geometry helpers
// ─────────────────────────────────────────────────────────
static std::vector<sf::Vector2f> polyPts(sf::Vector2f c, float r, int n,
                                          float off = -(float)M_PI/2)
{
    std::vector<sf::Vector2f> v;
    for (int i=0;i<n;i++) {
        float a = off + i*2*(float)M_PI/n;
        v.push_back({c.x+r*std::cos(a), c.y+r*std::sin(a)});
    }
    return v;
}
static std::vector<sf::Vector2f> starPts(sf::Vector2f c, float ro, float ri, int n)
{
    std::vector<sf::Vector2f> v;
    for (int i=0;i<n*2;i++) {
        float a = -(float)M_PI/2 + i*(float)M_PI/n;
        float r = (i%2==0) ? ro : ri;
        v.push_back({c.x+r*std::cos(a), c.y+r*std::sin(a)});
    }
    return v;
}
static sf::ConvexShape makeConvex(const std::vector<sf::Vector2f>& pts,
                                   sf::Color sc, sf::Color fc,
                                   int sz, bool filled, float opacity)
{
    sf::ConvexShape s(pts.size());
    for (size_t i=0;i<pts.size();i++) s.setPoint(i,pts[i]);
    sf::Color sc2=sc; sc2.a=(sf::Uint8)(opacity*255);
    sf::Color fc2=fc; fc2.a=(sf::Uint8)(opacity*255);
    s.setOutlineThickness((float)sz);
    s.setOutlineColor(sc2);
    s.setFillColor(filled ? fc2 : sf::Color::Transparent);
    return s;
}

// ─────────────────────────────────────────────────────────
//  Render a ShapeObj to any target
// ─────────────────────────────────────────────────────────
static void renderShape(sf::RenderTarget& tgt, const ShapeObj& obj, sf::Font* font)
{
    float x1=obj.p1.x, y1=obj.p1.y, x2=obj.p2.x, y2=obj.p2.y;
    float ox=std::min(x1,x2), oy=std::min(y1,y2);
    float ow=std::abs(x2-x1), oh=std::abs(y2-y1);
    float cx=(x1+x2)/2, cy=(y1+y2)/2;
    float rx=ow/2, ry=oh/2, rr=std::max(rx,ry);

    sf::Color sc=obj.sc; sc.a=(sf::Uint8)(obj.opacity*255);
    sf::Color fc=obj.fc; fc.a=(sf::Uint8)(obj.opacity*255);
    sf::BlendMode bm = sfBlend(obj.blend);

    auto conv = [&](std::vector<sf::Vector2f> pts) {
        return makeConvex(pts,obj.sc,obj.fc,obj.sz,obj.filled,obj.opacity);
    };

    switch(obj.type) {

    case Tool::Pencil: {
        if (obj.pts.size()<2) break;
        sf::VertexArray va(sf::LinesStrip, obj.pts.size());
        for (size_t i=0;i<obj.pts.size();i++) va[i]={obj.pts[i],sc};
        tgt.draw(va,bm);
        break;
    }
    case Tool::Eraser: {
        for (size_t i=1;i<obj.pts.size();i++) {
            auto a=obj.pts[i-1], b=obj.pts[i];
            float dx=b.x-a.x, dy=b.y-a.y, len=std::sqrt(dx*dx+dy*dy);
            if (len<1) continue;
            sf::RectangleShape rs(sf::Vector2f(len,(float)obj.sz*3));
            rs.setOrigin(0,(float)obj.sz*1.5f);
            rs.setPosition(a);
            rs.setRotation(std::atan2(dy,dx)*180.f/(float)M_PI);
            rs.setFillColor(sf::Color::White);
            tgt.draw(rs);
        }
        break;
    }
    case Tool::Line: {
        float dx=x2-x1,dy=y2-y1,len=std::sqrt(dx*dx+dy*dy);
        sf::RectangleShape rs(sf::Vector2f(len,(float)obj.sz));
        rs.setOrigin(0,(float)obj.sz/2);
        rs.setPosition(x1,y1);
        rs.setRotation(std::atan2(dy,dx)*180.f/(float)M_PI);
        rs.setFillColor(sc);
        tgt.draw(rs,bm);
        break;
    }
    case Tool::Rect: {
        sf::RectangleShape rs(sf::Vector2f(ow,oh));
        rs.setPosition(ox,oy);
        rs.setOutlineThickness((float)obj.sz);
        rs.setOutlineColor(sc);
        rs.setFillColor(obj.filled ? fc : sf::Color::Transparent);
        tgt.draw(rs,bm);
        break;
    }
    case Tool::RRect: {
        float rad=std::min({14.f,ow/4,oh/4});
        std::vector<sf::Vector2f> pts;
        auto arc=[&](float acx,float acy,float sa,float ea){
            for (int i=0;i<=8;i++){float a=sa+(ea-sa)*i/8.f; pts.push_back({acx+rad*std::cos(a),acy+rad*std::sin(a)});}
        };
        arc(ox+ow-rad,oy+rad,   -(float)M_PI/2,0);
        arc(ox+ow-rad,oy+oh-rad, 0,(float)M_PI/2);
        arc(ox+rad,   oy+oh-rad, (float)M_PI/2,(float)M_PI);
        arc(ox+rad,   oy+rad,    (float)M_PI,3*(float)M_PI/2);
        tgt.draw(conv(pts),bm);
        break;
    }
    case Tool::Ellipse: {
        sf::CircleShape cs(rx);
        cs.setOrigin(rx,rx); cs.setPosition(cx,cy);
        if (std::abs(rx-ry)>0.5f) cs.setScale(1.f, ry/std::max(rx,0.5f));
        cs.setOutlineThickness((float)obj.sz);
        cs.setOutlineColor(sc);
        cs.setFillColor(obj.filled ? fc : sf::Color::Transparent);
        tgt.draw(cs,bm);
        break;
    }
    case Tool::Triangle:
        tgt.draw(conv({{(x1+x2)/2,y1},{x2,y2},{x1,y2}}),bm); break;
    case Tool::RTriangle:
        tgt.draw(conv({{x1,y1},{x1,y2},{x2,y2}}),bm); break;
    case Tool::Diamond:
        tgt.draw(conv({{cx,y1},{x2,cy},{cx,y2},{x1,cy}}),bm); break;
    case Tool::Pentagon:
        tgt.draw(conv(polyPts({cx,cy},rr,5)),bm); break;
    case Tool::Hexagon:
        tgt.draw(conv(polyPts({cx,cy},rr,6)),bm); break;
    case Tool::Octagon:
        tgt.draw(conv(polyPts({cx,cy},rr,8)),bm); break;
    case Tool::Star5:
        tgt.draw(conv(starPts({cx,cy},rr,rr*0.4f,5)),bm); break;
    case Tool::Star6:
        tgt.draw(conv(starPts({cx,cy},rr,rr*0.5f,6)),bm); break;
    case Tool::Parallelogram: {
        float sk=ow*0.25f;
        tgt.draw(conv({{ox+sk,oy},{ox+ow,oy},{ox+ow-sk,oy+oh},{ox,oy+oh}}),bm);
        break;
    }
    case Tool::Arrow: {
        float aw=std::min(oh*0.5f,ow*0.35f), ah=ow*0.3f;
        tgt.draw(conv({
            {x1,cy-aw/2},{x2-ah,cy-aw/2},{x2-ah,y1},{x2,cy},
            {x2-ah,y2},{x2-ah,cy+aw/2},{x1,cy+aw/2}
        }),bm);
        break;
    }
    case Tool::Cross: {
        float tw=ow/3, th2=oh/3;
        tgt.draw(conv({
            {ox+tw,oy},{ox+tw*2,oy},{ox+tw*2,oy+th2},{ox+ow,oy+th2},
            {ox+ow,oy+th2*2},{ox+tw*2,oy+th2*2},{ox+tw*2,oy+oh},
            {ox+tw,oy+oh},{ox+tw,oy+th2*2},{ox,oy+th2*2},{ox,oy+th2},{ox+tw,oy+th2}
        }),bm);
        break;
    }
    case Tool::Heart: {
        sf::Color fc2=obj.filled?fc:sf::Color::Transparent;
        float hr=rx*0.55f;
        sf::CircleShape lc(hr), rc2(hr);
        lc.setOrigin(hr,hr);  lc.setPosition(cx-hr*0.8f,cy-hr*0.2f);
        rc2.setOrigin(hr,hr); rc2.setPosition(cx+hr*0.8f,cy-hr*0.2f);
        lc.setFillColor(fc2);  lc.setOutlineThickness(0);
        rc2.setFillColor(fc2); rc2.setOutlineThickness(0);
        tgt.draw(lc,bm); tgt.draw(rc2,bm);
        tgt.draw(conv({{cx-rx,cy},{cx,cy+ry*0.9f},{cx+rx,cy}}),bm);
        break;
    }
    case Tool::Semicircle: {
        std::vector<sf::Vector2f> pts;
        pts.push_back({cx-rx,cy});
        for (int i=0;i<=20;i++){float a=(float)M_PI+i*(float)M_PI/20.f; pts.push_back({cx+rx*std::cos(a),cy+ry*std::sin(a)});}
        pts.push_back({cx+rx,cy});
        tgt.draw(conv(pts),bm);
        break;
    }
    case Tool::Arc: {
        std::vector<sf::Vector2f> pts;
        pts.push_back({cx,cy});
        for (int i=0;i<=24;i++){float a=i*1.5f*(float)M_PI/24.f; pts.push_back({cx+rx*std::cos(a),cy+ry*std::sin(a)});}
        tgt.draw(conv(pts),bm);
        break;
    }
    case Tool::Text: {
        if (!font) break;
        sf::Text txt(obj.text,*font,(unsigned)obj.fontSize);
        txt.setFillColor(sc);
        txt.setPosition(obj.p1);
        tgt.draw(txt,bm);
        break;
    }
    default: break;
    }
}

// ─────────────────────────────────────────────────────────
//  Flood fill
// ─────────────────────────────────────────────────────────
static void floodFill(sf::RenderTexture& rt, sf::Vector2u pos, sf::Color fill)
{
    sf::Image img = rt.getTexture().copyToImage();
    unsigned W=img.getSize().x, H=img.getSize().y;
    if (pos.x>=W || pos.y>=H) return;
    sf::Color tgt = img.getPixel(pos.x,pos.y);
    if (tgt==fill) return;

    std::vector<sf::Vector2u> stack;
    stack.push_back(pos);
    while (!stack.empty()) {
        auto [x,y] = stack.back(); stack.pop_back();
        if (x>=W||y>=H) continue;
        if (img.getPixel(x,y)!=tgt) continue;
        img.setPixel(x,y,fill);
        if (x>0)   stack.push_back({x-1,y});
        if (x+1<W) stack.push_back({x+1,y});
        if (y>0)   stack.push_back({x,y-1});
        if (y+1<H) stack.push_back({x,y+1});
    }
    sf::Texture tex; tex.loadFromImage(img);
    rt.clear(sf::Color::White);
    sf::Sprite sp(tex); rt.draw(sp);
    rt.display();
}

// ─────────────────────────────────────────────────────────
//  UI helper
// ─────────────────────────────────────────────────────────
static sf::FloatRect drawBtn(sf::RenderWindow& win, sf::Font& font,
                              sf::Vector2f pos, sf::Vector2f sz,
                              const std::string& lbl, bool active=false,
                              sf::Color bg=sf::Color(210,210,210))
{
    sf::RectangleShape rs(sz);
    rs.setPosition(pos);
    rs.setFillColor(active ? sf::Color(90,150,240) : bg);
    rs.setOutlineColor(sf::Color(140,140,140));
    rs.setOutlineThickness(1);
    win.draw(rs);
    sf::Text t(lbl,font,9);
    auto b=t.getLocalBounds();
    t.setOrigin(b.left+b.width/2,b.top+b.height/2);
    t.setPosition(pos.x+sz.x/2, pos.y+sz.y/2);
    t.setFillColor(active ? sf::Color::White : sf::Color(30,30,30));
    win.draw(t);
    return {pos.x,pos.y,sz.x,sz.y};
}

// ─────────────────────────────────────────────────────────
//  Colour picker
// ─────────────────────────────────────────────────────────
struct Picker {
    bool open=false, forStroke=true;
    HSL  hsl={0,1,0.5f};
    float gx=1,gy=0;
    bool gdrag=false, hdrag=false;
    sf::RenderTexture gradTex;
    bool dirty=true;

    std::vector<sf::Color> basic = {
        {255,175,175},{255,255,175},{200,255,200},{0,255,0},{200,255,255},{0,0,255},{255,175,255},{232,232,255},
        {255,0,0},{255,255,0},{128,255,0},{0,192,0},{0,255,255},{128,128,255},{255,0,255},{128,128,64},
        {128,64,64},{255,128,64},{0,255,128},{0,64,64},{128,128,255},{128,0,255},{255,0,128},{255,64,64},
        {128,0,0},{255,128,0},{0,128,0},{0,64,0},{0,0,128},{0,0,64},{128,0,128},{64,0,64},
        {128,64,0},{64,64,0},{0,128,128},{0,0,0},{128,128,128},{192,192,192},{64,0,0},{255,255,255}
    };
    std::vector<sf::Color> custom=std::vector<sf::Color>(16,sf::Color::White);

    sf::Color cur() const { return hslToRgb(hsl); }
    void openWith(sf::Color c,bool stroke){
        forStroke=stroke; hsl=rgbToHsl(c);
        gx=hsl.s; gy=1.f-hsl.l; dirty=true; open=true;
    }
    void rebuild(){
        if (!gradTex.create(200,200)) return;
        sf::Image img; img.create(200,200);
        for (unsigned y=0;y<200;y++)
            for (unsigned x=0;x<200;x++)
                img.setPixel(x,y,hslToRgb({hsl.h,x/199.f,1.f-y/199.f}));
        sf::Texture tex; tex.loadFromImage(img);
        gradTex.clear(); sf::Sprite sp(tex); gradTex.draw(sp); gradTex.display();
        dirty=false;
    }
};

static void drawPicker(sf::RenderWindow& win, sf::Font& font, Picker& pk,
                        sf::Color& sc, sf::Color& fc,
                        sf::Vector2f mp, bool mdown, bool mjust)
{
    const float DX=70,DY=50,DW=680,DH=510;
    sf::RectangleShape bg(sf::Vector2f(DW,DH));
    bg.setPosition(DX,DY);
    bg.setFillColor(sf::Color(236,233,216));
    bg.setOutlineColor(sf::Color(80,80,80));
    bg.setOutlineThickness(2);
    win.draw(bg);

    sf::Text title("Edit Colors",font,13);
    title.setFillColor(sf::Color::Black);
    title.setPosition(DX+8,DY+6); win.draw(title);

    // Basic colours (5 rows × 8 cols)
    sf::Text bl("Basic colors:",font,10);
    bl.setFillColor(sf::Color::Black); bl.setPosition(DX+10,DY+26); win.draw(bl);
    for (int i=0;i<(int)pk.basic.size();i++){
        int col2=i%8, row=i/8;
        float bx=DX+10+col2*30, by=DY+40+row*28;
        sf::RectangleShape sw(sf::Vector2f(26,24));
        sw.setPosition(bx,by); sw.setFillColor(pk.basic[i]);
        sw.setOutlineColor(sf::Color(110,110,110)); sw.setOutlineThickness(1);
        win.draw(sw);
        if (mjust && sf::FloatRect(bx,by,26,24).contains(mp)){
            pk.hsl=rgbToHsl(pk.basic[i]);
            pk.gx=pk.hsl.s; pk.gy=1.f-pk.hsl.l; pk.dirty=true;
        }
    }

    // Custom colours (2 rows × 8 cols)
    sf::Text cl("Custom colors:",font,10);
    cl.setFillColor(sf::Color::Black); cl.setPosition(DX+10,DY+185); win.draw(cl);
    for (int i=0;i<16;i++){
        int col2=i%8, row=i/8;
        float bx=DX+10+col2*30, by=DY+199+row*28;
        sf::RectangleShape sw(sf::Vector2f(26,24));
        sw.setPosition(bx,by); sw.setFillColor(pk.custom[i]);
        sw.setOutlineColor(sf::Color(130,130,130)); sw.setOutlineThickness(1);
        win.draw(sw);
        if (mjust && sf::FloatRect(bx,by,26,24).contains(mp)){
            pk.hsl=rgbToHsl(pk.custom[i]);
            pk.gx=pk.hsl.s; pk.gy=1.f-pk.hsl.l; pk.dirty=true;
        }
    }

    // HSL gradient
    const float GX=DX+320,GY=DY+26,GW=200,GH=200;
    if (pk.dirty) pk.rebuild();
    sf::Sprite gs(pk.gradTex.getTexture());
    gs.setPosition(GX,GY); win.draw(gs);
    float chx=GX+pk.gx*GW, chy=GY+pk.gy*GH;
    sf::CircleShape ch(5); ch.setOrigin(5,5); ch.setPosition(chx,chy);
    ch.setFillColor(sf::Color::Transparent);
    ch.setOutlineColor(sf::Color::White); ch.setOutlineThickness(2);
    win.draw(ch);
    if (mdown && pk.gdrag){
        pk.gx=std::max(0.f,std::min(1.f,(mp.x-GX)/GW));
        pk.gy=std::max(0.f,std::min(1.f,(mp.y-GY)/GH));
        pk.hsl.s=pk.gx; pk.hsl.l=1.f-pk.gy;
    }
    if (mjust && sf::FloatRect(GX,GY,GW,GH).contains(mp)) pk.gdrag=true;

    // Hue slider
    const float HX=GX+GW+8,HY=GY,HW=18,HH=GH;
    for (int y=0;y<(int)HH;y++){
        sf::RectangleShape row(sf::Vector2f(HW,1));
        row.setPosition(HX,HY+y);
        row.setFillColor(hslToRgb({y/HH,1.f,0.5f}));
        win.draw(row);
    }
    sf::RectangleShape hmark(sf::Vector2f(HW+6,3));
    hmark.setOrigin(3,1); hmark.setPosition(HX-3,HY+pk.hsl.h*HH);
    hmark.setFillColor(sf::Color(20,20,20)); win.draw(hmark);
    if (mdown && pk.hdrag){
        pk.hsl.h=std::max(0.f,std::min(1.f,(mp.y-HY)/HH));
        pk.dirty=true; pk.hsl.s=pk.gx; pk.hsl.l=1.f-pk.gy;
    }
    if (mjust && sf::FloatRect(HX-3,HY,HW+6,HH).contains(mp)) pk.hdrag=true;

    // Brightness bar
    const float BX=HX+HW+4,BY=HY,BW=14;
    for (int y=0;y<(int)HH;y++){
        sf::Uint8 v=(sf::Uint8)((1.f-y/HH)*255);
        sf::RectangleShape row(sf::Vector2f(BW,1));
        row.setPosition(BX,BY+y);
        row.setFillColor({v,v,v});
        win.draw(row);
    }

    // Preview swatch
    sf::Color curCol=pk.cur();
    sf::RectangleShape prev(sf::Vector2f(76,50));
    prev.setPosition(DX+320,DY+236); prev.setFillColor(curCol);
    prev.setOutlineColor(sf::Color(100,100,100)); prev.setOutlineThickness(1);
    win.draw(prev);
    sf::Text cs("Color|Solid",font,9);
    cs.setFillColor(sf::Color::Black); cs.setPosition(DX+320,DY+290); win.draw(cs);

    // Numeric fields
    auto field=[&](const std::string& lbl,int val,float fx,float fy){
        sf::Text lt(lbl,font,10); lt.setFillColor(sf::Color::Black);
        lt.setPosition(fx,fy); win.draw(lt);
        sf::RectangleShape inp(sf::Vector2f(44,16));
        inp.setPosition(fx+40,fy); inp.setFillColor(sf::Color::White);
        inp.setOutlineColor(sf::Color(140,140,140)); inp.setOutlineThickness(1);
        win.draw(inp);
        sf::Text vt(std::to_string(val),font,9);
        vt.setFillColor(sf::Color::Black); vt.setPosition(fx+43,fy+2); win.draw(vt);
    };
    field("Red:",  curCol.r,              DX+412,DY+240);
    field("Green:",curCol.g,              DX+412,DY+258);
    field("Blue:", curCol.b,              DX+412,DY+276);
    field("Hue:",  (int)(pk.hsl.h*360),  DX+508,DY+240);
    field("Sat:",  (int)(pk.hsl.s*240),  DX+508,DY+258);
    field("Lum:",  (int)(pk.hsl.l*240),  DX+508,DY+276);

    // Buttons
    auto okR  =drawBtn(win,font,{DX+320,DY+312},{70,26},"OK",false,sf::Color(196,216,255));
    auto canR =drawBtn(win,font,{DX+396,DY+312},{70,26},"Cancel",false);
    auto addR =drawBtn(win,font,{DX+320,DY+344},{148,26},"Add to Custom Colors",false);

    if (mjust && !pk.gdrag && !pk.hdrag){
        if (sf::FloatRect(okR ).contains(mp)){ if(pk.forStroke)sc=curCol;else fc=curCol; pk.open=false; }
        if (sf::FloatRect(canR).contains(mp)) pk.open=false;
        if (sf::FloatRect(addR).contains(mp)){ pk.custom.erase(pk.custom.begin()); pk.custom.push_back(curCol); }
    }
    if (!mdown){ pk.gdrag=false; pk.hdrag=false; }
}

// ─────────────────────────────────────────────────────────
//  Code generation
// ─────────────────────────────────────────────────────────
static std::string genCode(const std::vector<ShapeObj>& objs)
{
    std::ostringstream ss;
    ss << "// SFML Paint Pro — generated\n"
       << "#include <SFML/Graphics.hpp>\n"
       << "int main() {\n"
       << "    sf::RenderWindow window(sf::VideoMode("<<CVS_W<<","<<CVS_H<<"),\"Output\");\n"
       << "    window.setFramerateLimit(60);\n";
    bool needFont=std::any_of(objs.begin(),objs.end(),[](const ShapeObj& o){return o.type==Tool::Text;});
    if (needFont) ss<<"    sf::Font font; font.loadFromFile(\"arial.ttf\");\n";
    ss<<"\n";
    for (size_t i=0;i<objs.size();i++){
        const ShapeObj& o=objs[i];
        std::string id=std::string(toolName(o.type))+std::to_string(i);
        float x1=o.p1.x,y1=o.p1.y,x2=o.p2.x,y2=o.p2.y;
        float ox=std::min(x1,x2),oy=std::min(y1,y2),ow=std::abs(x2-x1),oh=std::abs(y2-y1);
        float cx=(x1+x2)/2,cy=(y1+y2)/2,rx=ow/2,ry=oh/2,rr=std::max(rx,ry);
        std::string sc2=colStr(o.sc,o.opacity), fc2=colStr(o.fc,o.opacity);
        std::string fil=o.filled?fc2:"sf::Color::Transparent";
        std::string bm=(o.blend==BM::Multiply)?"sf::BlendMultiply":(o.blend==BM::Add)?"sf::BlendAdd":"sf::BlendAlpha";
        ss<<"    // #"<<i<<" "<<toolName(o.type)<<" blend="<<bmName(o.blend)<<"\n";
        switch(o.type){
        case Tool::Rect:
            ss<<"    sf::RectangleShape "<<id<<"(sf::Vector2f("<<(int)ow<<"f,"<<(int)oh<<"f));\n"
              <<"    "<<id<<".setPosition("<<(int)ox<<"f,"<<(int)oy<<"f);\n"
              <<"    "<<id<<".setOutlineThickness("<<o.sz<<"f);\n"
              <<"    "<<id<<".setOutlineColor("<<sc2<<");\n"
              <<"    "<<id<<".setFillColor("<<fil<<");\n";
            break;
        case Tool::Ellipse:
            ss<<"    sf::CircleShape "<<id<<"("<<(int)rx<<"f);\n"
              <<"    "<<id<<".setOrigin("<<(int)rx<<"f,"<<(int)rx<<"f);\n"
              <<"    "<<id<<".setPosition("<<(int)cx<<"f,"<<(int)cy<<"f);\n";
            if (std::abs(rx-ry)>0.5f)
                ss<<"    "<<id<<".setScale(1.f,"<<(ry/std::max(rx,0.5f))<<"f);\n";
            ss<<"    "<<id<<".setOutlineThickness("<<o.sz<<"f);\n"
              <<"    "<<id<<".setOutlineColor("<<sc2<<");\n"
              <<"    "<<id<<".setFillColor("<<fil<<");\n";
            break;
        case Tool::Line: {
            float dx=x2-x1,dy=y2-y1,len=std::sqrt(dx*dx+dy*dy);
            float ang=std::atan2(dy,dx)*180.f/(float)M_PI;
            ss<<"    sf::RectangleShape "<<id<<"(sf::Vector2f("<<(int)len<<"f,"<<o.sz<<"f));\n"
              <<"    "<<id<<".setOrigin(0,"<<o.sz<<"/2.f);\n"
              <<"    "<<id<<".setPosition("<<(int)x1<<"f,"<<(int)y1<<"f);\n"
              <<"    "<<id<<".setRotation("<<ang<<"f);\n"
              <<"    "<<id<<".setFillColor("<<sc2<<");\n";
            break;
        }
        case Tool::Text:
            ss<<"    sf::Text "<<id<<"; "<<id<<".setFont(font);\n"
              <<"    "<<id<<".setString(\""<<o.text<<"\");\n"
              <<"    "<<id<<".setCharacterSize("<<o.fontSize<<");\n"
              <<"    "<<id<<".setFillColor("<<sc2<<");\n"
              <<"    "<<id<<".setPosition("<<(int)x1<<"f,"<<(int)y1<<"f);\n";
            break;
        case Tool::Triangle:
            ss<<"    sf::ConvexShape "<<id<<"(3);\n"
              <<"    "<<id<<".setPoint(0,sf::Vector2f("<<(int)((x1+x2)/2)<<"f,"<<(int)y1<<"f));\n"
              <<"    "<<id<<".setPoint(1,sf::Vector2f("<<(int)x2<<"f,"<<(int)y2<<"f));\n"
              <<"    "<<id<<".setPoint(2,sf::Vector2f("<<(int)x1<<"f,"<<(int)y2<<"f));\n"
              <<"    "<<id<<".setOutlineThickness("<<o.sz<<"f);\n"
              <<"    "<<id<<".setOutlineColor("<<sc2<<");\n"
              <<"    "<<id<<".setFillColor("<<fil<<");\n";
            break;
        case Tool::Pentagon: case Tool::Hexagon: case Tool::Octagon: {
            int n=(o.type==Tool::Pentagon)?5:(o.type==Tool::Hexagon)?6:8;
            auto pts=polyPts({cx,cy},rr,n);
            ss<<"    sf::ConvexShape "<<id<<"("<<n<<");\n";
            for (int k=0;k<n;k++)
                ss<<"    "<<id<<".setPoint("<<k<<",sf::Vector2f("<<(int)pts[k].x<<"f,"<<(int)pts[k].y<<"f));\n";
            ss<<"    "<<id<<".setOutlineThickness("<<o.sz<<"f);\n"
              <<"    "<<id<<".setOutlineColor("<<sc2<<");\n"
              <<"    "<<id<<".setFillColor("<<fil<<");\n";
            break;
        }
        case Tool::Star5: case Tool::Star6: {
            int n=(o.type==Tool::Star5)?5:6;
            auto pts=starPts({cx,cy},rr,rr*(o.type==Tool::Star5?0.4f:0.5f),n);
            ss<<"    sf::ConvexShape "<<id<<"("<<(int)pts.size()<<");\n";
            for (size_t k=0;k<pts.size();k++)
                ss<<"    "<<id<<".setPoint("<<k<<",sf::Vector2f("<<(int)pts[k].x<<"f,"<<(int)pts[k].y<<"f));\n";
            ss<<"    "<<id<<".setOutlineThickness("<<o.sz<<"f);\n"
              <<"    "<<id<<".setOutlineColor("<<sc2<<");\n"
              <<"    "<<id<<".setFillColor("<<fil<<");\n";
            break;
        }
        case Tool::Pencil: {
            ss<<"    sf::VertexArray "<<id<<"(sf::LinesStrip,"<<o.pts.size()<<");\n";
            for (size_t k=0;k<std::min(o.pts.size(),(size_t)12);k++)
                ss<<"    "<<id<<"["<<k<<"]=sf::Vertex(sf::Vector2f("<<(int)o.pts[k].x<<"f,"<<(int)o.pts[k].y<<"f),"<<sc2<<");\n";
            if (o.pts.size()>12) ss<<"    // ..."<<o.pts.size()-12<<" more points\n";
            break;
        }
        default:
            ss<<"    sf::ConvexShape "<<id<<"; // "<<toolName(o.type)<<" ("<<(int)ox<<","<<(int)oy<<") "<<(int)ow<<"x"<<(int)oh<<"\n"
              <<"    "<<id<<".setOutlineColor("<<sc2<<"); "<<id<<".setFillColor("<<fil<<");\n";
            break;
        }
        ss<<"    // window.draw("<<id<<", "<<bm<<");\n\n";
    }
    ss<<"    while (window.isOpen()) {\n"
      <<"        sf::Event ev;\n"
      <<"        while (window.pollEvent(ev))\n"
      <<"            if (ev.type==sf::Event::Closed) window.close();\n"
      <<"        window.clear(sf::Color::White);\n";
    for (size_t i=0;i<objs.size();i++)
        ss<<"        window.draw("<<toolName(objs[i].type)<<i<<");\n";
    ss<<"        window.display();\n    }\n    return 0;\n}\n";
    return ss.str();
}

// ─────────────────────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────────────────────
int main()
{
    sf::RenderWindow window(sf::VideoMode(WIN_W,WIN_H),"SFML Paint Pro",sf::Style::Default);
    window.setFramerateLimit(60);

    sf::Font font; bool hasFont=false;
    for (auto& p: {"arial.ttf",
                   "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                   "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                   "/System/Library/Fonts/Helvetica.ttc",
                   "C:/Windows/Fonts/arial.ttf",
                   "/home/developergaish/Downloads/arial.ttf"})
        if (!hasFont && font.loadFromFile(p)) hasFont=true;

    sf::RenderTexture canvas;
    canvas.create(CVS_W,CVS_H);
    canvas.clear(sf::Color::White);
    canvas.display();

    // App state
    std::vector<ShapeObj> objects;
    std::deque<sf::Image> undoStack;
    Tool       curTool   = Tool::Pencil;
    sf::Color  strokeCol {0,0,0}, fillCol{255,255,255};
    int        strokeSz  = 3;
    float      opacity   = 1.f;
    bool       fillMode  = false;
    BM         blendMode = BM::Normal;
    bool       showGrid  = false;
    float      zoom      = 1.f;
    bool       drawing   = false;
    sf::Vector2f drawStart;
    std::vector<sf::Vector2f> pencilPts;
    bool       textMode  = false;
    std::string textBuf;
    sf::Vector2f textPos;
    ShapeObj*  selObj    = nullptr;
    sf::Vector2f dragOff;
    Picker     picker;
    std::string codeText = "// Draw something!\n";
    bool       mwasDown  = false;

    std::vector<std::pair<const char*,BM>> blends={
        {"Normal",BM::Normal},{"Multiply",BM::Multiply},{"Screen",BM::Screen},
        {"Add",BM::Add},{"Subtract",BM::Subtract},{"Overlay",BM::Overlay},
        {"Darken",BM::Darken},{"Lighten",BM::Lighten}
    };
    int blendIdx=0;

    std::vector<sf::Color> quickPal={
        {0,0,0},{255,255,255},{192,192,192},{128,128,128},
        {255,0,0},{0,255,0},{0,0,255},{255,255,0},
        {255,0,255},{0,255,255},{128,0,0},{0,128,0},
        {0,0,128},{128,128,0},{255,128,0},{0,128,128}
    };

    auto saveUndo=[&](){
        undoStack.push_back(canvas.getTexture().copyToImage());
        if (undoStack.size()>MAX_UNDO) undoStack.pop_front();
    };
    auto doUndo=[&](){
        if (undoStack.empty()) return;
        sf::Image img=undoStack.back(); undoStack.pop_back();
        sf::Texture t; t.loadFromImage(img);
        canvas.clear(sf::Color::White); sf::Sprite sp(t); canvas.draw(sp); canvas.display();
        if (!objects.empty()) objects.pop_back();
        codeText=genCode(objects);
    };
    auto toCanvas=[&](sf::Vector2i mp)->sf::Vector2f{
        return {(mp.x-CVS_X)/zoom,(mp.y-CVS_Y)/zoom};
    };
    auto inCanvas=[&](sf::Vector2i mp)->bool{
        sf::Vector2f p=toCanvas(mp);
        return p.x>=0&&p.y>=0&&p.x<CVS_W&&p.y<CVS_H;
    };
    auto commit=[&](ShapeObj obj){
        saveUndo();
        renderShape(canvas,obj,hasFont?&font:nullptr);
        canvas.display();
        objects.push_back(obj);
        codeText=genCode(objects);
    };

    // Toolbar button layout
    struct TBtn{float x,y,w,h;Tool t;const char* lbl;};
    const TBtn tbns[]={
        { 8, 4,44,38,Tool::Select,   "Sel"},
        {56, 4,44,38,Tool::Pencil,   "Pen"},
        {104,4,44,38,Tool::Eraser,   "Era"},
        {152,4,44,38,Tool::Bucket,   "Bkt"},
        {200,4,44,38,Tool::Text,     "Txt"},
        { 8,46,44,38,Tool::Line,     "Line"},
        {56,46,44,38,Tool::Rect,     "Rect"},
        {104,46,44,38,Tool::RRect,   "RRct"},
        {152,46,44,38,Tool::Ellipse, "Ell"},
        {200,46,44,38,Tool::Triangle,"Tri"},
        {248,46,44,38,Tool::RTriangle,"RTri"},
        {296, 4,44,38,Tool::Pentagon, "Pent"},
        {344, 4,44,38,Tool::Hexagon,  "Hex"},
        {392, 4,44,38,Tool::Octagon,  "Oct"},
        {440, 4,44,38,Tool::Star5,    "Str5"},
        {488, 4,44,38,Tool::Star6,    "Str6"},
        {536, 4,44,38,Tool::Semicircle,"Semi"},
        {296,46,44,38,Tool::Arrow,    "Arw"},
        {344,46,44,38,Tool::Parallelogram,"Para"},
        {392,46,44,38,Tool::Diamond,  "Diam"},
        {440,46,44,38,Tool::Cross,    "Crss"},
        {488,46,44,38,Tool::Heart,    "Hrt"},
        {536,46,44,38,Tool::Arc,      "Arc"},
    };

    while (window.isOpen())
    {
        sf::Vector2i rawM = sf::Mouse::getPosition(window);
        sf::Vector2f mouseF((float)rawM.x,(float)rawM.y);
        bool mdown  = sf::Mouse::isButtonPressed(sf::Mouse::Left);
        bool mjust  = mdown && !mwasDown;
        bool mup    = !mdown && mwasDown;

        // Events
        sf::Event ev;
        while (window.pollEvent(ev)){
            if (ev.type==sf::Event::Closed) window.close();

            if (ev.type==sf::Event::KeyPressed && !picker.open){
                auto k=ev.key.code;
                if (ev.key.control && k==sf::Keyboard::Z) doUndo();
                if (k==sf::Keyboard::G) showGrid=!showGrid;
                if (k==sf::Keyboard::F) fillMode=!fillMode;
                if (k==sf::Keyboard::Equal||k==sf::Keyboard::Add)    zoom=std::min(4.f,zoom+0.25f);
                if (k==sf::Keyboard::Hyphen||k==sf::Keyboard::Subtract) zoom=std::max(0.25f,zoom-0.25f);
                if (k==sf::Keyboard::P) curTool=Tool::Pencil;
                if (k==sf::Keyboard::E) curTool=Tool::Eraser;
                if (k==sf::Keyboard::T) curTool=Tool::Text;
                if (k==sf::Keyboard::B) curTool=Tool::Bucket;
                if (k==sf::Keyboard::L) curTool=Tool::Line;
                if (k==sf::Keyboard::R) curTool=Tool::Rect;
                if (k==sf::Keyboard::C) curTool=Tool::Ellipse;
                if (k==sf::Keyboard::Delete && !objects.empty()) doUndo();
            }
            if (ev.type==sf::Event::TextEntered && textMode && !picker.open){
                sf::Uint32 ch=ev.text.unicode;
                if (ch=='\r'||ch=='\n'){
                    if (!textBuf.empty()){
                        ShapeObj obj; obj.type=Tool::Text;
                        obj.p1=textPos; obj.p2=textPos;
                        obj.text=textBuf; obj.fontSize=std::max(12,strokeSz*5);
                        obj.sc=strokeCol; obj.fc=fillCol; obj.opacity=opacity;
                        obj.sz=strokeSz; obj.blend=blendMode;
                        commit(obj);
                    }
                    textMode=false; textBuf="";
                } else if (ch==8){
                    if (!textBuf.empty()) textBuf.pop_back();
                } else if (ch<128) textBuf+=(char)ch;
            }
        }

        // Canvas mouse
        if (!picker.open){
            if (mjust && inCanvas(rawM)){
                sf::Vector2f cp=toCanvas(rawM);
                if (curTool==Tool::Bucket){
                    saveUndo(); floodFill(canvas,{(unsigned)cp.x,(unsigned)cp.y},fillCol);
                    ShapeObj obj; obj.type=Tool::Bucket; obj.p1=cp; obj.p2=cp;
                    obj.sc=fillCol; obj.fc=fillCol; obj.opacity=opacity; obj.sz=1; obj.blend=blendMode;
                    objects.push_back(obj); codeText=genCode(objects);
                } else if (curTool==Tool::Text){
                    textMode=true; textBuf=""; textPos=cp;
                } else if (curTool==Tool::Pencil||curTool==Tool::Eraser){
                    saveUndo(); drawing=true; pencilPts.clear(); pencilPts.push_back(cp);
                } else if (curTool==Tool::Select){
                    selObj=nullptr;
                    for (auto it=objects.rbegin();it!=objects.rend();++it){
                        float ox=std::min(it->p1.x,it->p2.x)-8;
                        float oy=std::min(it->p1.y,it->p2.y)-8;
                        float ow=std::abs(it->p2.x-it->p1.x)+16;
                        float oh=std::abs(it->p2.y-it->p1.y)+16;
                        if (cp.x>=ox&&cp.x<=ox+ow&&cp.y>=oy&&cp.y<=oy+oh){
                            selObj=&(*it); dragOff=cp-it->p1; break;
                        }
                    }
                } else {
                    drawing=true; drawStart=cp;
                }
            }
            if (mdown){
                sf::Vector2f cp=toCanvas(rawM);
                if (drawing&&(curTool==Tool::Pencil||curTool==Tool::Eraser)){
                    if (!pencilPts.empty()){
                        sf::Vector2f last=pencilPts.back();
                        sf::Vector2f diff=cp-last;
                        if (diff.x*diff.x+diff.y*diff.y>1){
                            pencilPts.push_back(cp);
                            size_t n=pencilPts.size();
                            if (curTool==Tool::Pencil){
                                sf::Color sc2=strokeCol; sc2.a=(sf::Uint8)(opacity*255);
                                sf::Vertex ln[2]={{last,sc2},{cp,sc2}};
                                canvas.draw(ln,2,sf::Lines);
                            } else {
                                float dx=cp.x-last.x,dy=cp.y-last.y,len=std::sqrt(dx*dx+dy*dy);
                                if (len>0){
                                    sf::RectangleShape rs(sf::Vector2f(len,(float)strokeSz*3));
                                    rs.setOrigin(0,(float)strokeSz*1.5f);
                                    rs.setPosition(last);
                                    rs.setRotation(std::atan2(dy,dx)*180.f/(float)M_PI);
                                    rs.setFillColor(sf::Color::White);
                                    canvas.draw(rs);
                                }
                            }
                            canvas.display();
                        }
                    }
                }
                if (curTool==Tool::Select&&selObj){
                    sf::Vector2f npos=cp-dragOff;
                    sf::Vector2f delta=npos-selObj->p1;
                    selObj->p1+=delta; selObj->p2+=delta;
                    canvas.clear(sf::Color::White);
                    for (auto& o:objects) renderShape(canvas,o,hasFont?&font:nullptr);
                    canvas.display(); codeText=genCode(objects);
                }
            }
            if (mup&&drawing){
                drawing=false;
                sf::Vector2f cp=toCanvas(rawM);
                if (curTool==Tool::Pencil||curTool==Tool::Eraser){
                    if (pencilPts.size()>1){
                        ShapeObj obj; obj.type=curTool;
                        obj.pts=pencilPts; obj.p1=pencilPts.front(); obj.p2=pencilPts.back();
                        obj.sc=(curTool==Tool::Eraser)?sf::Color::White:strokeCol;
                        obj.fc=fillCol; obj.opacity=opacity; obj.sz=strokeSz; obj.blend=blendMode;
                        objects.push_back(obj); codeText=genCode(objects);
                    }
                    pencilPts.clear();
                } else if (curTool!=Tool::Select&&curTool!=Tool::Text&&curTool!=Tool::Bucket){
                    if (std::abs(cp.x-drawStart.x)>3||std::abs(cp.y-drawStart.y)>3){
                        ShapeObj obj; obj.type=curTool;
                        obj.p1=drawStart; obj.p2=cp;
                        obj.sc=strokeCol; obj.fc=fillCol; obj.opacity=opacity;
                        obj.sz=strokeSz; obj.filled=fillMode; obj.blend=blendMode;
                        commit(obj);
                    }
                }
            }

            // Left panel clicks
            if (mjust){
                auto mp=mouseF;
                float py=(float)(CVS_Y+10);

                // Stroke/fill boxes
                if (sf::FloatRect(10,py,36,36).contains(mp)) picker.openWith(strokeCol,true);
                if (sf::FloatRect(50,py,36,36).contains(mp)) picker.openWith(fillCol,false);

                // Quick palette
                for (int i=0;i<(int)quickPal.size();i++){
                    int c2=i%4,r2=i/4;
                    if (sf::FloatRect(10+c2*22.f,py+50+r2*22.f,20,20).contains(mp))
                        strokeCol=quickPal[i];
                }

                py=(float)(CVS_Y+170);
                if (sf::FloatRect(10,py,20,18).contains(mp)) strokeSz=std::max(1,strokeSz-1);
                if (sf::FloatRect(34,py,20,18).contains(mp)) strokeSz=std::min(40,strokeSz+1);
                if (sf::FloatRect(10,py+38,20,18).contains(mp)) opacity=std::max(0.1f,opacity-0.1f);
                if (sf::FloatRect(34,py+38,20,18).contains(mp)) opacity=std::min(1.f,opacity+0.1f);
                if (sf::FloatRect(10,py+64,80,18).contains(mp)) fillMode=!fillMode;
                if (sf::FloatRect(10,py+86,80,18).contains(mp)) showGrid=!showGrid;
                if (sf::FloatRect(10,py+112,20,18).contains(mp)) zoom=std::max(0.25f,zoom-0.25f);
                if (sf::FloatRect(55,py+112,20,18).contains(mp)) zoom=std::min(4.f,zoom+0.25f);
                if (sf::FloatRect(10,py+148,20,18).contains(mp)){blendIdx=std::max(0,blendIdx-1);blendMode=blends[blendIdx].second;}
                if (sf::FloatRect(34,py+148,20,18).contains(mp)){blendIdx=std::min((int)blends.size()-1,blendIdx+1);blendMode=blends[blendIdx].second;}
                if (sf::FloatRect(10,py+174,80,20).contains(mp)) doUndo();
                if (sf::FloatRect(10,py+198,80,20).contains(mp)){
                    objects.clear(); selObj=nullptr;
                    canvas.clear(sf::Color::White); canvas.display();
                    codeText="// Draw something!\n";
                }
                if (sf::FloatRect(10,(float)(WIN_H-SB_H-50),80,20).contains(mp)){
                    sf::Image img=canvas.getTexture().copyToImage();
                    img.saveToFile("canvas_output.png");
                    std::cout<<"[Saved] canvas_output.png\n";
                }
                if (sf::FloatRect(10,(float)(WIN_H-SB_H-26),80,20).contains(mp))
                    std::cout<<codeText;

                // Toolbar
                if (rawM.y<TB_H){
                    for (auto& b:tbns)
                        if (sf::FloatRect(b.x,b.y,b.w,b.h).contains(mp))
                            curTool=b.t;
                }
            }
        }

        mwasDown=mdown;

        // ── RENDER ─────────────────────────────────────────
        window.clear(sf::Color(200,200,200));

        // Toolbar
        {
            sf::RectangleShape tb(sf::Vector2f((float)WIN_W,(float)TB_H));
            tb.setFillColor(sf::Color(240,240,240));
            tb.setOutlineColor(sf::Color(150,150,150)); tb.setOutlineThickness(1);
            window.draw(tb);
            if (hasFont)
                for (auto& b:tbns)
                    drawBtn(window,font,{b.x,b.y},{b.w,b.h},b.lbl,curTool==b.t);
        }

        // Left panel
        {
            sf::RectangleShape lp(sf::Vector2f((float)LEFT_W,(float)(WIN_H-TB_H-SB_H)));
            lp.setPosition(0,(float)TB_H);
            lp.setFillColor(sf::Color(228,228,228));
            lp.setOutlineColor(sf::Color(155,155,155)); lp.setOutlineThickness(1);
            window.draw(lp);

            if (hasFont){
                float py=(float)(CVS_Y+10);
                sf::Text sl("Stroke",font,9); sl.setFillColor({60,60,60}); sl.setPosition(10,py-12); window.draw(sl);
                sf::Text fl2("Fill",font,9);  fl2.setFillColor({60,60,60}); fl2.setPosition(52,py-12); window.draw(fl2);

                auto swatch=[&](sf::Vector2f pos,sf::Color c,bool thick){
                    sf::RectangleShape s(sf::Vector2f(36,36)); s.setPosition(pos);
                    s.setFillColor(c); s.setOutlineColor({80,80,80});
                    s.setOutlineThickness(thick?2.f:1.f); window.draw(s);
                };
                swatch({10,py},strokeCol,true);
                swatch({50,py},fillCol,false);

                sf::Text palLbl("Palette:",font,9); palLbl.setFillColor({60,60,60}); palLbl.setPosition(10,py+42); window.draw(palLbl);
                for (int i=0;i<(int)quickPal.size();i++){
                    int c2=i%4,r2=i/4;
                    sf::RectangleShape s(sf::Vector2f(20,20));
                    s.setPosition(10+c2*22.f,py+54+r2*22.f);
                    s.setFillColor(quickPal[i]);
                    s.setOutlineColor({120,120,120}); s.setOutlineThickness(0.5f);
                    window.draw(s);
                }

                py=(float)(CVS_Y+170);
                sf::Text szl("Sz:"+std::to_string(strokeSz),font,9); szl.setFillColor({60,60,60}); szl.setPosition(10,py-14); window.draw(szl);
                drawBtn(window,font,{10,py},{20,18},"-");
                drawBtn(window,font,{34,py},{20,18},"+");
                sf::Text opl("Op:"+std::to_string((int)(opacity*100))+"%",font,9); opl.setFillColor({60,60,60}); opl.setPosition(10,py+24); window.draw(opl);
                drawBtn(window,font,{10,py+38},{20,18},"-");
                drawBtn(window,font,{34,py+38},{20,18},"+");
                drawBtn(window,font,{10,py+64},{80,18},fillMode?"Fill: ON":"Fill: OFF",fillMode,fillMode?sf::Color(180,220,255):sf::Color(210,210,210));
                drawBtn(window,font,{10,py+86},{80,18},showGrid?"Grid: ON":"Grid: OFF",showGrid,showGrid?sf::Color(180,220,255):sf::Color(210,210,210));
                sf::Text zl("Zoom:"+std::to_string((int)(zoom*100))+"%",font,9); zl.setFillColor({60,60,60}); zl.setPosition(10,py+106); window.draw(zl);
                drawBtn(window,font,{10,py+120},{20,18},"-");
                drawBtn(window,font,{55,py+120},{20,18},"+");
                sf::Text bmlbl("Blend:",font,9); bmlbl.setFillColor({60,60,60}); bmlbl.setPosition(10,py+144); window.draw(bmlbl);
                drawBtn(window,font,{10,py+158},{20,18},"<");
                drawBtn(window,font,{34,py+158},{20,18},">");
                sf::Text bmv(blends[blendIdx].first,font,9); bmv.setFillColor({30,60,180}); bmv.setPosition(58,py+163); window.draw(bmv);
                drawBtn(window,font,{10,py+182},{80,20},"Undo (Ctrl+Z)");
                drawBtn(window,font,{10,py+206},{80,20},"Clear All",false,sf::Color(255,175,175));

                sf::Text hk("[P]en [E]raser [B]kt\n[T]xt [L]ine [R]ect [C]ell\n[G]rid [F]ill +/-Zoom\nDel/Ctrl+Z:Undo",font,8);
                hk.setFillColor({120,120,120}); hk.setPosition(6,(float)(WIN_H-SB_H-116)); window.draw(hk);
                drawBtn(window,font,{10,(float)(WIN_H-SB_H-50)},{80,20},"Save PNG");
                drawBtn(window,font,{10,(float)(WIN_H-SB_H-26)},{80,20},"Print Code");
            }
        }

        // Canvas area
        {
            // Checker
            for (int y=CVS_Y;y<CVS_Y+CVS_H;y+=16)
            for (int x=CVS_X;x<CVS_X+CVS_W;x+=16){
                bool c2=((x/16+y/16)%2==0);
                sf::RectangleShape t(sf::Vector2f(16,16));
                t.setPosition((float)x,(float)y);
                t.setFillColor(c2?sf::Color(175,175,175):sf::Color(205,205,205));
                window.draw(t);
            }

            sf::Sprite sp(canvas.getTexture());
            sp.setPosition((float)CVS_X,(float)CVS_Y);
            sp.setScale(zoom,zoom);
            window.draw(sp);

            // Border
            sf::RectangleShape brd(sf::Vector2f(CVS_W*zoom+2,CVS_H*zoom+2));
            brd.setPosition(CVS_X-1.f,CVS_Y-1.f);
            brd.setFillColor(sf::Color::Transparent);
            brd.setOutlineColor({100,100,100}); brd.setOutlineThickness(1);
            window.draw(brd);

            // Grid
            if (showGrid){
                float gs=20.f*zoom;
                for (float x=(float)CVS_X;x<CVS_X+CVS_W*zoom;x+=gs){
                    sf::Vertex ln[2]={{sf::Vector2f(x,(float)CVS_Y),sf::Color(0,100,255,35)},
                                      {sf::Vector2f(x,(float)(CVS_Y+CVS_H*zoom)),sf::Color(0,100,255,35)}};
                    window.draw(ln,2,sf::Lines);
                }
                for (float y=(float)CVS_Y;y<CVS_Y+CVS_H*zoom;y+=gs){
                    sf::Vertex ln[2]={{sf::Vector2f((float)CVS_X,y),sf::Color(0,100,255,35)},
                                      {sf::Vector2f((float)(CVS_X+CVS_W*zoom),y),sf::Color(0,100,255,35)}};
                    window.draw(ln,2,sf::Lines);
                }
            }

            // Live shape preview
            if (drawing&&curTool!=Tool::Pencil&&curTool!=Tool::Eraser){
                sf::Vector2f cp=toCanvas(rawM);
                sf::RenderTexture pv; pv.create(CVS_W,CVS_H);
                pv.clear(sf::Color::Transparent);
                ShapeObj prev; prev.type=curTool;
                prev.p1=drawStart; prev.p2=cp;
                prev.sc=strokeCol; prev.fc=fillCol;
                prev.opacity=0.55f; prev.sz=strokeSz;
                prev.filled=fillMode; prev.blend=blendMode;
                renderShape(pv,prev,hasFont?&font:nullptr);
                pv.display();
                sf::Sprite ps(pv.getTexture());
                ps.setPosition((float)CVS_X,(float)CVS_Y);
                ps.setScale(zoom,zoom);
                window.draw(ps);
            }

            // Text cursor
            if (textMode&&hasFont){
                sf::Text tc(textBuf+"|",font,(unsigned)std::max(12,strokeSz*5));
                tc.setFillColor(strokeCol);
                tc.setPosition(CVS_X+textPos.x*zoom,CVS_Y+textPos.y*zoom);
                window.draw(tc);
            }

            // Selection box
            if (selObj&&curTool==Tool::Select){
                float ox=std::min(selObj->p1.x,selObj->p2.x);
                float oy=std::min(selObj->p1.y,selObj->p2.y);
                float ow=std::abs(selObj->p2.x-selObj->p1.x);
                float oh=std::abs(selObj->p2.y-selObj->p1.y);
                sf::RectangleShape sel(sf::Vector2f(ow*zoom+16,oh*zoom+16));
                sel.setPosition(CVS_X+(ox-8)*zoom,CVS_Y+(oy-8)*zoom);
                sel.setFillColor(sf::Color::Transparent);
                sel.setOutlineColor({50,120,255}); sel.setOutlineThickness(1.5f);
                window.draw(sel);
            }
        }

        // Right code panel
        {
            sf::RectangleShape rp(sf::Vector2f((float)RIGHT_W,(float)(WIN_H-TB_H-SB_H)));
            rp.setPosition((float)(WIN_W-RIGHT_W),(float)TB_H);
            rp.setFillColor({28,28,28});
            window.draw(rp);

            if (hasFont){
                sf::Text hdr("SFML Code",font,10);
                hdr.setFillColor({160,160,160});
                hdr.setPosition((float)(WIN_W-RIGHT_W+6),(float)(CVS_Y+4));
                window.draw(hdr);

                std::istringstream ss(codeText);
                std::string line2; int lineN=0; float ly=(float)(CVS_Y+20);
                while (std::getline(ss,line2)&&lineN<40){
                    sf::Text lt(line2,font,8);
                    lt.setFillColor({130,210,130});
                    lt.setPosition((float)(WIN_W-RIGHT_W+4),ly);
                    window.draw(lt);
                    ly+=11.f; lineN++;
                }
                if (lineN>=40){
                    sf::Text more("... click Print Code for full output",font,8);
                    more.setFillColor({120,120,120});
                    more.setPosition((float)(WIN_W-RIGHT_W+4),ly);
                    window.draw(more);
                }
                sf::Text inf(std::to_string(objects.size())+" obj | "+std::to_string(undoStack.size())+" undo",font,8);
                inf.setFillColor({140,140,140});
                inf.setPosition((float)(WIN_W-RIGHT_W+4),(float)(WIN_H-SB_H-14));
                window.draw(inf);
            }
        }

        // Status bar
        {
            sf::RectangleShape sb(sf::Vector2f((float)WIN_W,(float)SB_H));
            sb.setPosition(0,(float)(WIN_H-SB_H));
            sb.setFillColor({200,200,200});
            sb.setOutlineColor({150,150,150}); sb.setOutlineThickness(1);
            window.draw(sb);
            if (hasFont){
                sf::Vector2f cp=toCanvas(rawM);
                std::string st="Tool:"+std::string(toolName(curTool))+
                    "  x:"+std::to_string((int)cp.x)+" y:"+std::to_string((int)cp.y)+
                    "  Zoom:"+std::to_string((int)(zoom*100))+"%"+
                    "  Blend:"+blends[blendIdx].first+
                    "  Objects:"+std::to_string(objects.size());
                sf::Text stxt(st,font,9);
                stxt.setFillColor({50,50,50}); stxt.setPosition(4,(float)(WIN_H-SB_H+3));
                window.draw(stxt);
            }
        }

        // Colour picker overlay
        if (picker.open){
            sf::RectangleShape dim(sf::Vector2f((float)WIN_W,(float)WIN_H));
            dim.setFillColor({0,0,0,130}); window.draw(dim);
            drawPicker(window,font,picker,strokeCol,fillCol,mouseF,mdown,mjust);
        }

        window.display();
    }
    return 0;
}