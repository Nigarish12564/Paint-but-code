////////////////////////////////////////////////////////////
//  SFML Paint Pro — Fixed Edition (With Fullscreen & Blend Output)
//  Requires: SFML 2.5+,  C++17
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
#include <iomanip>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────
//  Layout  — all Y positions are derived from fixed anchors
// ─────────────────────────────────────────────────────────
static const int WIN_W   = 1400;
static const int WIN_H   = 870;
static const int TB_H    = 100;
static const int SB_H    = 22;
static const int LEFT_W  = 200;
static const int RIGHT_W = 280;
static const int CVS_X   = LEFT_W;
static const int CVS_Y   = TB_H;
static const int CVS_W   = WIN_W - LEFT_W - RIGHT_W;
static const int CVS_H   = WIN_H - TB_H - SB_H;
static const unsigned MAX_UNDO = 50;

static const int LP_SWATCH_Y  = CVS_Y + 10;          
static const int LP_PAL_LABEL = LP_SWATCH_Y + 48;    
static const int LP_PAL_END   = LP_PAL_LABEL + 14 + 4*22 + 6; 
static const int LP_SLD1_Y    = LP_PAL_END + 20;     
static const int LP_SLD2_Y    = LP_SLD1_Y  + 50;     
static const int LP_SLD3_Y    = LP_SLD2_Y  + 50;     
static const int LP_BTN_Y     = LP_SLD3_Y  + 90;     

static const int SLIDER_X     = 14;
static const int SLIDER_W     = 170;
static const int SLIDER_H     = 24;   

// ─────────────────────────────────────────────────────────
//  Colour helpers
// ─────────────────────────────────────────────────────────
struct HSL { float h=0,s=1,l=0.5f; };
static HSL rgbToHsl(sf::Color c){
    float r=c.r/255.f,g=c.g/255.f,b=c.b/255.f;
    float mx=std::max({r,g,b}),mn=std::min({r,g,b});
    float l2=(mx+mn)/2.f,s2=0,h2=0;
    if(mx!=mn){float d=mx-mn;s2=l2>0.5f?d/(2-mx-mn):d/(mx+mn);
        if(mx==r)h2=std::fmod((g-b)/d+(g<b?6:0),6.f);
        else if(mx==g)h2=(b-r)/d+2;else h2=(r-g)/d+4;h2/=6.f;}
    return{h2,s2,l2};
}
static sf::Color hslToRgb(HSL h){
    float hh=h.h,s=h.s,l=h.l;
    if(s==0){auto v=(sf::Uint8)(l*255);return{v,v,v};}
    auto hue=[](float p,float q,float t){
        if(t<0)t+=1;if(t>1)t-=1;
        if(t<1/6.f)return p+(q-p)*6*t;
        if(t<0.5f)return q;
        if(t<2/3.f)return p+(q-p)*(2/3.f-t)*6;return p;};
    float q=l<0.5f?l*(1+s):l+s-l*s,p=2*l-q;
    return{(sf::Uint8)(hue(p,q,hh+1/3.f)*255),(sf::Uint8)(hue(p,q,hh)*255),(sf::Uint8)(hue(p,q,hh-1/3.f)*255)};
}
static std::string colStr(sf::Color c,float alpha=1.f){
    int a=std::min(255,(int)(alpha*255));
    if(a>=255)return"sf::Color("+std::to_string(c.r)+","+std::to_string(c.g)+","+std::to_string(c.b)+")";
    return"sf::Color("+std::to_string(c.r)+","+std::to_string(c.g)+","+std::to_string(c.b)+","+std::to_string(a)+")";}

// ─────────────────────────────────────────────────────────
//  Blend modes
// ─────────────────────────────────────────────────────────
enum class BM{Normal,Darken,Multiply,ColorBurn,Lighten,Screen,ColorDodge,Add,Overlay,SoftLight,HardLight,Difference,Exclusion};
struct BlendEntry{BM mode;const char*name;const char*category;};
static const BlendEntry BLEND_TABLE[]={
    {BM::Normal,    "Normal",     "Normal"    },
    {BM::Darken,    "Darken",     "Darken"    },
    {BM::Multiply,  "Multiply",   nullptr     },
    {BM::ColorBurn, "Color Burn", nullptr     },
    {BM::Lighten,   "Lighten",    "Lighten"   },
    {BM::Screen,    "Screen",     nullptr     },
    {BM::ColorDodge,"Color Dodge",nullptr     },
    {BM::Add,       "Add",        nullptr     },
    {BM::Overlay,   "Overlay",    "Contrast"  },
    {BM::SoftLight, "Soft Light", nullptr     },
    {BM::HardLight, "Hard Light", nullptr     },
    {BM::Difference,"Difference", "Difference"},
    {BM::Exclusion, "Exclusion",  nullptr     },
};
static const int BLEND_COUNT=(int)(sizeof(BLEND_TABLE)/sizeof(BLEND_TABLE[0]));
static const char*bmName(BM b){for(auto&e:BLEND_TABLE)if(e.mode==b)return e.name;return"Normal";}
static sf::BlendMode sfBlend(BM b){
    if(b==BM::Multiply)return sf::BlendMultiply;
    if(b==BM::Add)return sf::BlendAdd;
    return sf::BlendAlpha;}
static sf::Color softBlend(BM mode,sf::Color src,sf::Color dst){
    auto ch=[](float a,float b,BM m)->float{
        switch(m){
        case BM::Multiply:   return a*b;
        case BM::Screen:     return 1-(1-a)*(1-b);
        case BM::Overlay:    return a<0.5f?2*a*b:1-2*(1-a)*(1-b);
        case BM::Darken:     return std::min(a,b);
        case BM::Lighten:    return std::max(a,b);
        case BM::ColorDodge: return b>=1?1:std::min(1.f,a/(1-b));
        case BM::ColorBurn:  return b<=0?0:std::max(0.f,1-(1-a)/b);
        case BM::HardLight:  return b<0.5f?2*a*b:1-2*(1-a)*(1-b);
        case BM::SoftLight:  return b<0.5f?a-(1-2*b)*a*(1-a):a+(2*b-1)*(std::sqrt(a)-a);
        case BM::Difference: return std::abs(a-b);
        case BM::Exclusion:  return a+b-2*a*b;
        case BM::Add:        return std::min(1.f,a+b);
        default:             return a;}};
    float sr=src.r/255.f,sg=src.g/255.f,sb2=src.b/255.f;
    float dr=dst.r/255.f,dg=dst.g/255.f,db=dst.b/255.f,sa=src.a/255.f;
    return{(sf::Uint8)((ch(sr,dr,mode)*sa+dr*(1-sa))*255),
           (sf::Uint8)((ch(sg,dg,mode)*sa+dg*(1-sa))*255),
           (sf::Uint8)((ch(sb2,db,mode)*sa+db*(1-sa))*255),255};}

// ─────────────────────────────────────────────────────────
//  Tools
// ─────────────────────────────────────────────────────────
enum class Tool{Select,Pencil,Eraser,Bucket,Text,Blend,
    Line,Rect,RRect,Ellipse,Triangle,RTriangle,
    Pentagon,Hexagon,Octagon,Star5,Star6,
    Arrow,Parallelogram,Diamond,Cross,Heart,Semicircle,Arc};
static const char*toolName(Tool t){
    switch(t){
    case Tool::Select:        return"Select";
    case Tool::Pencil:        return"Pencil";
    case Tool::Eraser:        return"Eraser";
    case Tool::Bucket:        return"Bucket";
    case Tool::Text:          return"Text";
    case Tool::Blend:         return"Blend";
    case Tool::Line:          return"Line";
    case Tool::Rect:          return"Rect";
    case Tool::RRect:         return"RRect";
    case Tool::Ellipse:       return"Ellipse";
    case Tool::Triangle:      return"Triangle";
    case Tool::RTriangle:     return"RTriangle";
    case Tool::Pentagon:      return"Pentagon";
    case Tool::Hexagon:       return"Hexagon";
    case Tool::Octagon:       return"Octagon";
    case Tool::Star5:         return"Star5";
    case Tool::Star6:         return"Star6";
    case Tool::Arrow:         return"Arrow";
    case Tool::Parallelogram: return"Parallelogram";
    case Tool::Diamond:       return"Diamond";
    case Tool::Cross:         return"Cross";
    case Tool::Heart:         return"Heart";
    case Tool::Semicircle:    return"Semicircle";
    case Tool::Arc:           return"Arc";
    default:                  return"Unknown";}
}

struct ShapeObj{
    Tool type=Tool::Rect;
    sf::Color sc=sf::Color::Black,fc=sf::Color::White;
    float opacity=1.f;int sz=2;bool filled=false;BM blend=BM::Normal;
    sf::Vector2f p1,p2;
    std::vector<sf::Vector2f>pts;
    std::string text;int fontSize=18;
};

// ─────────────────────────────────────────────────────────
//  Geometry
// ─────────────────────────────────────────────────────────
static std::vector<sf::Vector2f>polyPts(sf::Vector2f c,float r,int n,float off=-(float)M_PI/2){
    std::vector<sf::Vector2f>v;
    for(int i=0;i<n;i++){float a=off+i*2*(float)M_PI/n;v.push_back({c.x+r*std::cos(a),c.y+r*std::sin(a)});}return v;}
static std::vector<sf::Vector2f>starPts(sf::Vector2f c,float ro,float ri,int n){
    std::vector<sf::Vector2f>v;
    for(int i=0;i<n*2;i++){float a=-(float)M_PI/2+i*(float)M_PI/n;float r=(i%2==0)?ro:ri;v.push_back({c.x+r*std::cos(a),c.y+r*std::sin(a)});}return v;}
static sf::ConvexShape makeConvex(const std::vector<sf::Vector2f>&pts,sf::Color sc,sf::Color fc,int sz,bool filled,float opacity){
    sf::ConvexShape s(pts.size());
    for(size_t i=0;i<pts.size();i++)s.setPoint(i,pts[i]);
    sf::Color sc2=sc;sc2.a=(sf::Uint8)(opacity*255);
    sf::Color fc2=fc;fc2.a=(sf::Uint8)(opacity*255);
    s.setOutlineThickness((float)sz);s.setOutlineColor(sc2);
    s.setFillColor(filled?fc2:sf::Color::Transparent);return s;}

static void drawThickSeg(sf::RenderTarget&tgt,sf::Vector2f a,sf::Vector2f b,
                          sf::Color col,float thickness,sf::BlendMode bm=sf::BlendAlpha){
    float r=thickness*0.5f;
    sf::CircleShape cap(r);cap.setOrigin(r,r);cap.setFillColor(col);cap.setPosition(a);tgt.draw(cap,bm);
    float dx=b.x-a.x,dy=b.y-a.y,len=std::sqrt(dx*dx+dy*dy);
    if(len<0.5f)return;
    sf::RectangleShape rs(sf::Vector2f(len,thickness));
    rs.setOrigin(0,r);rs.setPosition(a);
    rs.setRotation(std::atan2(dy,dx)*180.f/(float)M_PI);
    rs.setFillColor(col);tgt.draw(rs,bm);
    cap.setPosition(b);tgt.draw(cap,bm);}

static void renderShape(sf::RenderTarget&tgt,const ShapeObj&obj,sf::Font*font){
    float x1=obj.p1.x,y1=obj.p1.y,x2=obj.p2.x,y2=obj.p2.y;
    float ox=std::min(x1,x2),oy=std::min(y1,y2),ow=std::abs(x2-x1),oh=std::abs(y2-y1);
    float cx=(x1+x2)/2,cy=(y1+y2)/2,rx=ow/2,ry=oh/2,rr=std::max(rx,ry);
    sf::Color sc=obj.sc;sc.a=(sf::Uint8)(obj.opacity*255);
    sf::Color fc=obj.fc;fc.a=(sf::Uint8)(obj.opacity*255);
    sf::BlendMode bm=sfBlend(obj.blend);
    auto conv=[&](std::vector<sf::Vector2f>pts){return makeConvex(pts,obj.sc,obj.fc,obj.sz,obj.filled,obj.opacity);};
    switch(obj.type){
    case Tool::Pencil:{
        if(obj.pts.empty())break;float thick=(float)obj.sz;
        if(obj.pts.size()==1){sf::CircleShape cap(thick*0.5f);cap.setOrigin(thick*0.5f,thick*0.5f);cap.setPosition(obj.pts[0]);cap.setFillColor(sc);tgt.draw(cap,bm);}
        else for(size_t i=1;i<obj.pts.size();i++)drawThickSeg(tgt,obj.pts[i-1],obj.pts[i],sc,thick,bm);
        break;}
    case Tool::Eraser:{
        for(size_t i=1;i<obj.pts.size();i++){
            auto a=obj.pts[i-1],b=obj.pts[i];float dx=b.x-a.x,dy=b.y-a.y,len=std::sqrt(dx*dx+dy*dy);
            if(len<1)continue;sf::RectangleShape rs(sf::Vector2f(len,(float)obj.sz*3));
            rs.setOrigin(0,(float)obj.sz*1.5f);rs.setPosition(a);rs.setRotation(std::atan2(dy,dx)*180.f/(float)M_PI);
            rs.setFillColor(sf::Color::White);tgt.draw(rs);}break;}
    case Tool::Blend:{
        break;}
    case Tool::Line:drawThickSeg(tgt,obj.p1,obj.p2,sc,(float)obj.sz,bm);break;
    case Tool::Rect:{sf::RectangleShape rs(sf::Vector2f(ow,oh));rs.setPosition(ox,oy);rs.setOutlineThickness((float)obj.sz);rs.setOutlineColor(sc);rs.setFillColor(obj.filled?fc:sf::Color::Transparent);tgt.draw(rs,bm);break;}
    case Tool::RRect:{float rad=std::min({14.f,ow/4,oh/4});std::vector<sf::Vector2f>pts;
        auto arc=[&](float acx,float acy,float sa,float ea){for(int i=0;i<=8;i++){float a=sa+(ea-sa)*i/8.f;pts.push_back({acx+rad*std::cos(a),acy+rad*std::sin(a)});}};
        arc(ox+ow-rad,oy+rad,-(float)M_PI/2,0);arc(ox+ow-rad,oy+oh-rad,0,(float)M_PI/2);
        arc(ox+rad,oy+oh-rad,(float)M_PI/2,(float)M_PI);arc(ox+rad,oy+rad,(float)M_PI,3*(float)M_PI/2);
        tgt.draw(conv(pts),bm);break;}
    case Tool::Ellipse:{sf::CircleShape cs(rx);cs.setOrigin(rx,rx);cs.setPosition(cx,cy);
        if(std::abs(rx-ry)>0.5f)cs.setScale(1.f,ry/std::max(rx,0.5f));
        cs.setOutlineThickness((float)obj.sz);cs.setOutlineColor(sc);cs.setFillColor(obj.filled?fc:sf::Color::Transparent);tgt.draw(cs,bm);break;}
    case Tool::Triangle:   tgt.draw(conv({{(x1+x2)/2,y1},{x2,y2},{x1,y2}}),bm);break;
    case Tool::RTriangle:  tgt.draw(conv({{x1,y1},{x1,y2},{x2,y2}}),bm);break;
    case Tool::Diamond:    tgt.draw(conv({{cx,y1},{x2,cy},{cx,y2},{x1,cy}}),bm);break;
    case Tool::Pentagon:   tgt.draw(conv(polyPts({cx,cy},rr,5)),bm);break;
    case Tool::Hexagon:    tgt.draw(conv(polyPts({cx,cy},rr,6)),bm);break;
    case Tool::Octagon:    tgt.draw(conv(polyPts({cx,cy},rr,8)),bm);break;
    case Tool::Star5:      tgt.draw(conv(starPts({cx,cy},rr,rr*0.4f,5)),bm);break;
    case Tool::Star6:      tgt.draw(conv(starPts({cx,cy},rr,rr*0.5f,6)),bm);break;
    case Tool::Parallelogram:{float sk=ow*0.25f;tgt.draw(conv({{ox+sk,oy},{ox+ow,oy},{ox+ow-sk,oy+oh},{ox,oy+oh}}),bm);break;}
    case Tool::Arrow:{float aw=std::min(oh*0.5f,ow*0.35f),ah=ow*0.3f;tgt.draw(conv({{x1,cy-aw/2},{x2-ah,cy-aw/2},{x2-ah,y1},{x2,cy},{x2-ah,y2},{x2-ah,cy+aw/2},{x1,cy+aw/2}}),bm);break;}
    case Tool::Cross:{float tw=ow/3,th2=oh/3;tgt.draw(conv({{ox+tw,oy},{ox+tw*2,oy},{ox+tw*2,oy+th2},{ox+ow,oy+th2},{ox+ow,oy+th2*2},{ox+tw*2,oy+th2*2},{ox+tw*2,oy+oh},{ox+tw,oy+oh},{ox+tw,oy+th2*2},{ox,oy+th2*2},{ox,oy+th2},{ox+tw,oy+th2}}),bm);break;}
    case Tool::Heart:{sf::Color fc2=obj.filled?fc:sf::Color::Transparent;float hr=rx*0.55f;
        sf::CircleShape lc(hr),rc2(hr);lc.setOrigin(hr,hr);lc.setPosition(cx-hr*0.8f,cy-hr*0.2f);
        rc2.setOrigin(hr,hr);rc2.setPosition(cx+hr*0.8f,cy-hr*0.2f);
        lc.setFillColor(fc2);lc.setOutlineThickness(0);rc2.setFillColor(fc2);rc2.setOutlineThickness(0);
        tgt.draw(lc,bm);tgt.draw(rc2,bm);tgt.draw(conv({{cx-rx,cy},{cx,cy+ry*0.9f},{cx+rx,cy}}),bm);break;}
    case Tool::Semicircle:{std::vector<sf::Vector2f>pts;pts.push_back({cx-rx,cy});
        for(int i=0;i<=20;i++){float a=(float)M_PI+i*(float)M_PI/20.f;pts.push_back({cx+rx*std::cos(a),cy+ry*std::sin(a)});}
        pts.push_back({cx+rx,cy});tgt.draw(conv(pts),bm);break;}
    case Tool::Arc:{std::vector<sf::Vector2f>pts;pts.push_back({cx,cy});
        for(int i=0;i<=24;i++){float a=i*1.5f*(float)M_PI/24.f;pts.push_back({cx+rx*std::cos(a),cy+ry*std::sin(a)});}
        tgt.draw(conv(pts),bm);break;}
    case Tool::Text:{if(!font)break;sf::Text txt(obj.text,*font,(unsigned)obj.fontSize);txt.setFillColor(sc);txt.setPosition(obj.p1);tgt.draw(txt,bm);break;}
    default:break;}
}

// ─────────────────────────────────────────────────────────
//  Flood fill
// ─────────────────────────────────────────────────────────
static void floodFill(sf::RenderTexture&rt,sf::Vector2u pos,sf::Color fill){
    sf::Image img=rt.getTexture().copyToImage();
    unsigned W=img.getSize().x,H=img.getSize().y;
    if(pos.x>=W||pos.y>=H)return;
    sf::Color tgt2=img.getPixel(pos.x,pos.y);if(tgt2==fill)return;
    std::vector<sf::Vector2u>stack;stack.push_back(pos);
    while(!stack.empty()){auto[x,y]=stack.back();stack.pop_back();
        if(x>=W||y>=H||img.getPixel(x,y)!=tgt2)continue;
        img.setPixel(x,y,fill);
        if(x>0)stack.push_back({x-1,y});if(x+1<W)stack.push_back({x+1,y});
        if(y>0)stack.push_back({x,y-1});if(y+1<H)stack.push_back({x,y+1});}
    sf::Texture tex;tex.loadFromImage(img);rt.clear(sf::Color::White);sf::Sprite sp(tex);rt.draw(sp);rt.display();}

// ─────────────────────────────────────────────────────────
//  UI: button
// ─────────────────────────────────────────────────────────
static sf::FloatRect drawBtn(sf::RenderWindow&win,sf::Font&font,
                              sf::Vector2f pos,sf::Vector2f sz,
                              const std::string&lbl,bool active=false,
                              sf::Color bg=sf::Color(60,60,80),
                              sf::Color fg=sf::Color(210,210,230)){
    sf::RectangleShape rs(sz);rs.setPosition(pos);
    rs.setFillColor(active?sf::Color(90,150,240):bg);
    rs.setOutlineColor(sf::Color(100,100,130));rs.setOutlineThickness(1);win.draw(rs);
    sf::Text t(lbl,font,9);auto b=t.getLocalBounds();
    t.setOrigin(b.left+b.width/2,b.top+b.height/2);t.setPosition(pos.x+sz.x/2,pos.y+sz.y/2);
    t.setFillColor(active?sf::Color::White:fg);win.draw(t);
    return{pos.x,pos.y,sz.x,sz.y};}

// ─────────────────────────────────────────────────────────
//  UI: slider 
// ─────────────────────────────────────────────────────────
struct Slider{
    float x,y;      
    float w;        
    float minV,maxV,val;
    sf::FloatRect hitRect()const{return{x-4,y,w+8,(float)SLIDER_H};}
    float norm()const{return(val-minV)/(maxV-minV);}
    void setFromMouse(float mx){
        float n=std::max(0.f,std::min(1.f,(mx-x)/w));
        val=minV+n*(maxV-minV);}
};

static void drawSlider(sf::RenderWindow&win,sf::Font&font,const Slider&s,
                       const std::string&label,const std::string&unit=""){
    std::string full=label+": "+std::to_string((int)s.val)+unit;
    sf::Text lbl(full,font,9);lbl.setFillColor({170,170,200});lbl.setPosition(s.x,s.y);win.draw(lbl);
    float ty=s.y+14;
    sf::RectangleShape track(sf::Vector2f(s.w,4));track.setPosition(s.x,ty);track.setFillColor({70,70,90});win.draw(track);
    sf::RectangleShape filled(sf::Vector2f(s.w*s.norm(),4));filled.setPosition(s.x,ty);filled.setFillColor({80,140,240});win.draw(filled);
    float tx=s.x+s.w*s.norm();
    sf::CircleShape thumb(6);thumb.setOrigin(6,6);thumb.setPosition(tx,ty+2);
    thumb.setFillColor({130,180,255});thumb.setOutlineColor({60,100,200});thumb.setOutlineThickness(1.5f);win.draw(thumb);}

// ─────────────────────────────────────────────────────────
//  Blend panel
// ─────────────────────────────────────────────────────────
struct BlendPanel{bool open=false;float btnX=0,btnY=0,btnW=172,btnH=22;};
static bool drawBlendPanel(sf::RenderWindow&win,sf::Font&font,BM&cur,float opacity,
                            sf::Color strokeCol,sf::Vector2f mp,bool mdown,bool mjust,BlendPanel&panel){
    bool consumed=false;
    float bx=panel.btnX,by=panel.btnY,bw=panel.btnW,bh=panel.btnH;
    {bool hov=sf::FloatRect(bx,by,bw,bh).contains(mp);
    for(int row=0;row<(int)bh;row++){float t=row/bh;sf::Uint8 lv=(sf::Uint8)(panel.open?55+t*25:(hov?70+t*25:58+t*22));
        sf::RectangleShape s(sf::Vector2f(bw,1));s.setPosition(bx,by+row);s.setFillColor({lv,lv,(sf::Uint8)(lv+18)});win.draw(s);}
    sf::RectangleShape brd(sf::Vector2f(bw,bh));brd.setPosition(bx,by);brd.setFillColor(sf::Color::Transparent);
    brd.setOutlineColor(panel.open?sf::Color(100,140,220):sf::Color(100,100,130));brd.setOutlineThickness(1);win.draw(brd);
    sf::Color prev=softBlend(cur,strokeCol,sf::Color::White);
    sf::CircleShape dot(5);dot.setOrigin(5,5);dot.setPosition(bx+10,by+bh/2);dot.setFillColor(prev);win.draw(dot);
    sf::Text lbl(bmName(cur),font,9);auto lb=lbl.getLocalBounds();lbl.setOrigin(0,lb.top+lb.height/2);
    lbl.setPosition(bx+20,by+bh/2);lbl.setFillColor(sf::Color(220,220,255));win.draw(lbl);
    sf::Text arr(panel.open?"^":"v",font,8);arr.setFillColor(sf::Color(180,180,210));arr.setPosition(bx+bw-14,by+bh/2-5);win.draw(arr);
    if(mjust&&hov){panel.open=!panel.open;consumed=true;}}
    if(!panel.open)return consumed;
    int catCount=0;for(auto&e:BLEND_TABLE)if(e.category)catCount++;
    const int IH=22;int panelH=BLEND_COUNT*IH+catCount*14+8;
    float px=panel.btnX,py=panel.btnY+panel.btnH+2,pw=panel.btnW;
    if(py+panelH>WIN_H-40)py=panel.btnY-(float)panelH-2;
    sf::RectangleShape shd(sf::Vector2f(pw+4,(float)panelH+4));shd.setPosition(px+3,py+3);shd.setFillColor({0,0,0,80});win.draw(shd);
    sf::RectangleShape bg2(sf::Vector2f(pw,(float)panelH));bg2.setPosition(px,py);bg2.setFillColor({34,34,46});bg2.setOutlineColor({80,80,110});bg2.setOutlineThickness(1);win.draw(bg2);
    float iy=py+4;
    for(int i=0;i<BLEND_COUNT;i++){const BlendEntry&e=BLEND_TABLE[i];
        if(e.category){sf::RectangleShape sep(sf::Vector2f(pw-8,1));sep.setPosition(px+4,iy);sep.setFillColor({70,70,95});win.draw(sep);
            sf::Text cat(e.category,font,8);cat.setFillColor({130,130,170});cat.setPosition(px+6,iy+2);win.draw(cat);iy+=14;}
        float rh=(float)IH;bool isCur=(e.mode==cur),hovRow=sf::FloatRect(px,iy,pw,rh).contains(mp);
        sf::RectangleShape row(sf::Vector2f(pw,rh));row.setPosition(px,iy);
        row.setFillColor(isCur?sf::Color(60,90,160):(hovRow?sf::Color(50,50,72):sf::Color::Transparent));win.draw(row);
        sf::Color srcC=strokeCol;srcC.a=(sf::Uint8)(opacity*255);
        sf::Color blended=softBlend(e.mode,srcC,sf::Color::White);
        sf::RectangleShape sw(sf::Vector2f(14,rh-6));sw.setPosition(px+4,iy+3);sw.setFillColor(blended);sw.setOutlineColor({80,80,100});sw.setOutlineThickness(0.5f);win.draw(sw);
        sf::Text name(e.name,font,9);auto nb=name.getLocalBounds();name.setOrigin(0,nb.top+nb.height/2);name.setPosition(px+22,iy+rh/2);
        name.setFillColor(isCur?sf::Color::White:(hovRow?sf::Color(210,210,255):sf::Color(170,170,200)));win.draw(name);
        if(mjust&&hovRow){cur=e.mode;panel.open=false;consumed=true;}iy+=rh;}
    if(mjust&&!consumed&&!sf::FloatRect(px,py,pw,(float)panelH).contains(mp)&&!sf::FloatRect(panel.btnX,panel.btnY,panel.btnW,panel.btnH).contains(mp))panel.open=false;
    return consumed;}

// ─────────────────────────────────────────────────────────
//  Colour picker
// ─────────────────────────────────────────────────────────
struct Picker{
    bool open=false,forStroke=true;HSL hsl={0,1,0.5f};float gx=1,gy=0;
    bool gdrag=false,hdrag=false;sf::RenderTexture gradTex;bool dirty=true;
    std::vector<sf::Color>basic={{255,175,175},{255,255,175},{200,255,200},{0,255,0},{200,255,255},{0,0,255},{255,175,255},{232,232,255},
        {255,0,0},{255,255,0},{128,255,0},{0,192,0},{0,255,255},{128,128,255},{255,0,255},{128,128,64},
        {128,64,64},{255,128,64},{0,255,128},{0,64,64},{128,128,255},{128,0,255},{255,0,128},{255,64,64},
        {128,0,0},{255,128,0},{0,128,0},{0,64,0},{0,0,128},{0,0,64},{128,0,128},{64,0,64},
        {128,64,0},{64,64,0},{0,128,128},{0,0,0},{128,128,128},{192,192,192},{64,0,0},{255,255,255}};
    std::vector<sf::Color>custom=std::vector<sf::Color>(16,sf::Color::White);
    sf::Color cur()const{return hslToRgb(hsl);}
    void openWith(sf::Color c,bool stroke){forStroke=stroke;hsl=rgbToHsl(c);gx=hsl.s;gy=1.f-hsl.l;dirty=true;open=true;}
    void rebuild(){if(!gradTex.create(200,200))return;sf::Image img;img.create(200,200);
        for(unsigned y=0;y<200;y++)for(unsigned x=0;x<200;x++)img.setPixel(x,y,hslToRgb({hsl.h,x/199.f,1.f-y/199.f}));
        sf::Texture tex;tex.loadFromImage(img);gradTex.clear();sf::Sprite sp(tex);gradTex.draw(sp);gradTex.display();dirty=false;}
};
static void drawPicker(sf::RenderWindow&win,sf::Font&font,Picker&pk,sf::Color&sc,sf::Color&fc,sf::Vector2f mp,bool mdown,bool mjust){
    const float DX=70,DY=50,DW=680,DH=510;
    sf::RectangleShape bg(sf::Vector2f(DW,DH));bg.setPosition(DX,DY);bg.setFillColor(sf::Color(236,233,216));bg.setOutlineColor(sf::Color(80,80,80));bg.setOutlineThickness(2);win.draw(bg);
    sf::Text title("Edit Colors",font,13);title.setFillColor(sf::Color::Black);title.setPosition(DX+8,DY+6);win.draw(title);
    for(int i=0;i<(int)pk.basic.size();i++){int col2=i%8,row=i/8;float bx=DX+10+col2*30,by=DY+40+row*28;
        sf::RectangleShape sw(sf::Vector2f(26,24));sw.setPosition(bx,by);sw.setFillColor(pk.basic[i]);sw.setOutlineColor(sf::Color(110,110,110));sw.setOutlineThickness(1);win.draw(sw);
        if(mjust&&sf::FloatRect(bx,by,26,24).contains(mp)){pk.hsl=rgbToHsl(pk.basic[i]);pk.gx=pk.hsl.s;pk.gy=1.f-pk.hsl.l;pk.dirty=true;}}
    sf::Text cl("Custom colors:",font,10);cl.setFillColor(sf::Color::Black);cl.setPosition(DX+10,DY+185);win.draw(cl);
    for(int i=0;i<16;i++){int col2=i%8,row=i/8;float bx=DX+10+col2*30,by=DY+199+row*28;
        sf::RectangleShape sw(sf::Vector2f(26,24));sw.setPosition(bx,by);sw.setFillColor(pk.custom[i]);sw.setOutlineColor(sf::Color(130,130,130));sw.setOutlineThickness(1);win.draw(sw);
        if(mjust&&sf::FloatRect(bx,by,26,24).contains(mp)){pk.hsl=rgbToHsl(pk.custom[i]);pk.gx=pk.hsl.s;pk.gy=1.f-pk.hsl.l;pk.dirty=true;}}
    const float GX=DX+320,GY=DY+26,GW=200,GH=200;
    if(pk.dirty)pk.rebuild();sf::Sprite gs(pk.gradTex.getTexture());gs.setPosition(GX,GY);win.draw(gs);
    float chx=GX+pk.gx*GW,chy=GY+pk.gy*GH;sf::CircleShape ch(5);ch.setOrigin(5,5);ch.setPosition(chx,chy);ch.setFillColor(sf::Color::Transparent);ch.setOutlineColor(sf::Color::White);ch.setOutlineThickness(2);win.draw(ch);
    if(mdown&&pk.gdrag){pk.gx=std::max(0.f,std::min(1.f,(mp.x-GX)/GW));pk.gy=std::max(0.f,std::min(1.f,(mp.y-GY)/GH));pk.hsl.s=pk.gx;pk.hsl.l=1.f-pk.gy;}
    if(mjust&&sf::FloatRect(GX,GY,GW,GH).contains(mp))pk.gdrag=true;
    const float HX=GX+GW+8,HY=GY,HW=18,HH=GH;
    for(int y=0;y<(int)HH;y++){sf::RectangleShape row(sf::Vector2f(HW,1));row.setPosition(HX,HY+y);row.setFillColor(hslToRgb({y/HH,1.f,0.5f}));win.draw(row);}
    sf::RectangleShape hmark(sf::Vector2f(HW+6,3));hmark.setOrigin(3,1);hmark.setPosition(HX-3,HY+pk.hsl.h*HH);hmark.setFillColor(sf::Color(20,20,20));win.draw(hmark);
    if(mdown&&pk.hdrag){pk.hsl.h=std::max(0.f,std::min(1.f,(mp.y-HY)/HH));pk.dirty=true;pk.hsl.s=pk.gx;pk.hsl.l=1.f-pk.gy;}
    if(mjust&&sf::FloatRect(HX-3,HY,HW+6,HH).contains(mp))pk.hdrag=true;
    const float BX2=HX+HW+4,BW=14;
    for(int y=0;y<(int)HH;y++){sf::Uint8 v=(sf::Uint8)((1.f-y/HH)*255);sf::RectangleShape row(sf::Vector2f(BW,1));row.setPosition(BX2,HY+y);row.setFillColor({v,v,v});win.draw(row);}
    sf::Color curCol=pk.cur();sf::RectangleShape prev(sf::Vector2f(76,50));prev.setPosition(DX+320,DY+236);prev.setFillColor(curCol);prev.setOutlineColor(sf::Color(100,100,100));prev.setOutlineThickness(1);win.draw(prev);
    auto field=[&](const std::string&lbl2,int val,float fx,float fy){
        sf::Text lt(lbl2,font,10);lt.setFillColor(sf::Color::Black);lt.setPosition(fx,fy);win.draw(lt);
        sf::RectangleShape inp(sf::Vector2f(44,16));inp.setPosition(fx+40,fy);inp.setFillColor(sf::Color::White);inp.setOutlineColor(sf::Color(140,140,140));inp.setOutlineThickness(1);win.draw(inp);
        sf::Text vt(std::to_string(val),font,9);vt.setFillColor(sf::Color::Black);vt.setPosition(fx+43,fy+2);win.draw(vt);};
    field("Red:",curCol.r,DX+412,DY+240);field("Green:",curCol.g,DX+412,DY+258);field("Blue:",curCol.b,DX+412,DY+276);
    field("Hue:",(int)(pk.hsl.h*360),DX+508,DY+240);field("Sat:",(int)(pk.hsl.s*240),DX+508,DY+258);field("Lum:",(int)(pk.hsl.l*240),DX+508,DY+276);
    auto okR=drawBtn(win,font,{DX+320,DY+312},{70,26},"OK",false,sf::Color(60,90,160));
    auto canR=drawBtn(win,font,{DX+396,DY+312},{70,26},"Cancel");
    auto addR=drawBtn(win,font,{DX+320,DY+344},{148,26},"Add to Custom Colors");
    if(mjust&&!pk.gdrag&&!pk.hdrag){
        if(sf::FloatRect(okR).contains(mp)){if(pk.forStroke)sc=curCol;else fc=curCol;pk.open=false;}
        if(sf::FloatRect(canR).contains(mp))pk.open=false;
        if(sf::FloatRect(addR).contains(mp)){pk.custom.erase(pk.custom.begin());pk.custom.push_back(curCol);}}
    if(!mdown){pk.gdrag=false;pk.hdrag=false;}}

// ─────────────────────────────────────────────────────────
//  Code generation
// ─────────────────────────────────────────────────────────
static std::string genCode(const std::vector<ShapeObj>&objs)  

{  

std::ostringstream ss;  

ss << "////////////////////////////////////////////////////////////\n"  

<< "// SFML Paint Pro — Generated Output (SFML 2.5, C++17)\n"  

<< "////////////////////////////////////////////////////////////\n"  

<< "#include <SFML/Graphics.hpp>\n"  

<< "#include <cmath>\n"  

<< "#include <vector>\n"  

<< "#include <string>\n"  

<< "\n"  

<< "// ── Canvas size ──────────────────────────────────────────\n"  

<< "static const int W = " << CVS_W << ";\n"  

<< "static const int H = " << CVS_H << ";\n"  

<< "\n";  

bool needFont=std::any_of(objs.begin(),objs.end(),[](const ShapeObj&o){return o.type==Tool::Text;});  

ss << "void drawShapes(sf::RenderTexture& canvas";  

if(needFont) ss << ", const sf::Font& font";  

ss << ");\n\n";  

ss << "int main() {\n"  

<< " sf::RenderWindow window(\n"  

<< " sf::VideoMode(W, H),\n"  

<< " \"Paint Output\",\n"  

<< " sf::Style::Default\n"  

<< " );\n"  

<< " window.setFramerateLimit(60);\n";  

if(needFont){  

ss << "\n"  

<< " sf::Font font;\n"  

<< " if (!font.loadFromFile(\"arial.ttf\")) {\n"  

<< " font.loadFromFile(\"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf\");\n"  

<< " }\n";  

}  

ss << "\n"  

<< " sf::RenderTexture canvas;\n" 

<< " canvas.create(W, H);\n" 

<< " canvas.clear(sf::Color::White);\n" 

<< " drawShapes(canvas";  

if(needFont) ss << ", font";  

ss << ");\n"  

<< " canvas.display();\n" 

<< "\n" 

<< " while (window.isOpen()) {\n"  

<< " sf::Event ev;\n"  

<< " while (window.pollEvent(ev))\n"  

<< " if (ev.type == sf::Event::Closed)\n"  

<< " window.close();\n"  

<< "\n"  

<< " window.clear(sf::Color::White);\n"  

<< " sf::Sprite sprite(canvas.getTexture());\n"  

<< " window.draw(sprite);\n"  

<< " window.display();\n"  

<< " }\n"  

<< " return 0;\n"  

<< "}\n\n";  

ss << "void drawShapes(sf::RenderTexture& canvas";  

if(needFont) ss << ", const sf::Font& font";  

ss << ") {\n";  

if(objs.empty()){  

ss << " // No shapes yet — draw something in the paint app!\n";  

}  

for(size_t i=0;i<objs.size();i++){  

const ShapeObj&o=objs[i];  

float x1=o.p1.x,y1=o.p1.y,x2=o.p2.x,y2=o.p2.y;  

float ox=std::min(x1,x2),oy=std::min(y1,y2),ow=std::abs(x2-x1),oh=std::abs(y2-y1);  

float cx=(x1+x2)/2,cy=(y1+y2)/2,rx=ow/2,ry=oh/2,rr=std::max(rx,ry);  

std::string sc2=colStr(o.sc,o.opacity),fc2=colStr(o.fc,o.opacity);  

std::string fil=o.filled?fc2:"sf::Color::Transparent";  

std::string bm=(o.blend==BM::Multiply)?"sf::BlendMultiply":(o.blend==BM::Add)?"sf::BlendAdd":"sf::BlendAlpha";  

ss << "\n // ── Shape " << i << ": " << toolName(o.type)  

<< " blend=" << bmName(o.blend) << " ──\n";  

ss << " {\n";  

switch(o.type){  

case Tool::Rect:  

ss<<" sf::RectangleShape shape(sf::Vector2f("<<(int)ow<<","<<(int)oh<<"));\n"  

<<" shape.setPosition("<<(int)ox<<","<<(int)oy<<");\n"  

<<" shape.setOutlineThickness("<<o.sz<<");\n"  

<<" shape.setOutlineColor("<<sc2<<");\n"  

<<" shape.setFillColor("<<fil<<");\n"  

<<" canvas.draw(shape, "<<bm<<");\n";  

break;  

case Tool::Ellipse:  

ss<<" sf::CircleShape shape("<<(int)rx<<");\n"  

<<" shape.setOrigin("<<(int)rx<<","<<(int)rx<<");\n"  

<<" shape.setPosition("<<(int)cx<<","<<(int)cy<<");\n";  

if(std::abs(rx-ry)>0.5f)ss<<" shape.setScale(1.f,"<<std::fixed<<std::setprecision(4)<<(ry/std::max(rx,0.5f))<<");\n";  

ss<<" shape.setOutlineThickness("<<o.sz<<");\n"  

<<" shape.setOutlineColor("<<sc2<<");\n"  

<<" shape.setFillColor("<<fil<<");\n"  

<<" canvas.draw(shape, "<<bm<<");\n";  

break;  

case Tool::Line:{  

float dx2=x2-x1,dy2=y2-y1,len=std::sqrt(dx2*dx2+dy2*dy2);  

float ang=std::atan2(dy2,dx2)*180.f/(float)M_PI;  

ss<<" sf::RectangleShape shape(sf::Vector2f("<<(int)len<<","<<o.sz<<"));\n"  

<<" shape.setOrigin(0,"<<o.sz<<"/2.f);\n"  

<<" shape.setPosition("<<(int)x1<<","<<(int)y1<<");\n"  

<<" shape.setRotation("<<std::fixed<<std::setprecision(2)<<ang<<");\n"  

<<" shape.setFillColor("<<sc2<<");\n"  

<<" canvas.draw(shape, "<<bm<<");\n";  

break;}  

case Tool::Text:  

ss<<" sf::Text shape(\""<<o.text<<"\", font, "<<o.fontSize<<");\n"  

<<" shape.setFillColor("<<sc2<<");\n"  

<<" shape.setPosition("<<(int)x1<<","<<(int)y1<<");\n"  

<<" canvas.draw(shape, "<<bm<<");\n";  

break;  

case Tool::Triangle:  

ss<<" sf::ConvexShape shape(3);\n"  

<<" shape.setPoint(0, sf::Vector2f("<<(int)((x1+x2)/2)<<","<<(int)y1<<"));\n"  

<<" shape.setPoint(1, sf::Vector2f("<<(int)x2<<","<<(int)y2<<"));\n"  

<<" shape.setPoint(2, sf::Vector2f("<<(int)x1<<","<<(int)y2<<"));\n"  

<<" shape.setOutlineThickness("<<o.sz<<");\n"  

<<" shape.setOutlineColor("<<sc2<<");\n"  

<<" shape.setFillColor("<<fil<<");\n"  

<<" canvas.draw(shape, "<<bm<<");\n";  

break;  

case Tool::RTriangle: 

ss<<" sf::ConvexShape shape(3);\n" 

<<" shape.setPoint(0, sf::Vector2f("<<(int)x1<<","<<(int)y1<<"));\n" 

<<" shape.setPoint(1, sf::Vector2f("<<(int)x1<<","<<(int)y2<<"));\n" 

<<" shape.setPoint(2, sf::Vector2f("<<(int)x2<<","<<(int)y2<<"));\n" 

<<" shape.setOutlineThickness("<<o.sz<<");\n" 

<<" shape.setOutlineColor("<<sc2<<");\n" 

<<" shape.setFillColor("<<fil<<");\n" 

<<" canvas.draw(shape, "<<bm<<");\n"; 

break; 

case Tool::Pentagon:case Tool::Hexagon:case Tool::Octagon:{  

int n=(o.type==Tool::Pentagon)?5:(o.type==Tool::Hexagon)?6:8;  

auto pts=polyPts({cx,cy},rr,n);  

ss<<" sf::ConvexShape shape("<<n<<");\n";  

for(int k=0;k<n;k++)ss<<" shape.setPoint("<<k<<", sf::Vector2f("<<(int)pts[k].x<<","<<(int)pts[k].y<<"));\n";  

ss<<" shape.setOutlineThickness("<<o.sz<<");\n"  

<<" shape.setOutlineColor("<<sc2<<");\n"  

<<" shape.setFillColor("<<fil<<");\n"  

<<" canvas.draw(shape, "<<bm<<");\n";  

break;}  

case Tool::Diamond:  

ss<<" sf::ConvexShape shape(4);\n"  

<<" shape.setPoint(0, sf::Vector2f("<<(int)cx<<","<<(int)y1<<"));\n"  

<<" shape.setPoint(1, sf::Vector2f("<<(int)x2<<","<<(int)cy<<"));\n"  

<<" shape.setPoint(2, sf::Vector2f("<<(int)cx<<","<<(int)y2<<"));\n"  

<<" shape.setPoint(3, sf::Vector2f("<<(int)x1<<","<<(int)cy<<"));\n"  

<<" shape.setOutlineThickness("<<o.sz<<");\n"  

<<" shape.setOutlineColor("<<sc2<<");\n"  

<<" shape.setFillColor("<<fil<<");\n"  

<<" canvas.draw(shape, "<<bm<<");\n";  

break;  

case Tool::Star5:case Tool::Star6:{  

int n=(o.type==Tool::Star5)?5:6;  

float ri=rr*(o.type==Tool::Star5?0.4f:0.5f);  

auto pts=starPts({cx,cy},rr,ri,n);  

ss<<" sf::ConvexShape shape("<<(int)pts.size()<<");\n";  

for(size_t k=0;k<pts.size();k++)ss<<" shape.setPoint("<<k<<", sf::Vector2f("<<(int)pts[k].x<<","<<(int)pts[k].y<<"));\n";  

ss<<" shape.setOutlineThickness("<<o.sz<<");\n"  

<<" shape.setOutlineColor("<<sc2<<");\n"  

<<" shape.setFillColor("<<fil<<");\n"  

<<" canvas.draw(shape, "<<bm<<");\n";  

break;}  

case Tool::Parallelogram: 

ss<<" {\n" 

<<" float sk = "<<(int)ow<<" * 0.25f;\n" 

<<" std::vector<sf::Vector2f> pts = {\n" 

<<" {"<<(int)ox<<" + sk, "<<(int)oy<<"}, {"<<(int)(ox+ow)<<", "<<(int)oy<<"},\n" 

<<" {"<<(int)(ox+ow)<<" - sk, "<<(int)(oy+oh)<<"}, {"<<(int)ox<<", "<<(int)(oy+oh)<<"}\n" 

<<" };\n" 

<<" sf::ConvexShape shape(pts.size());\n" 

<<" for(size_t i=0; i<pts.size(); ++i) shape.setPoint(i, pts[i]);\n" 

<<" shape.setOutlineThickness("<<o.sz<<");\n" 

<<" shape.setOutlineColor("<<sc2<<");\n" 

<<" shape.setFillColor("<<fil<<");\n" 

<<" canvas.draw(shape, "<<bm<<");\n" 

<<" }\n"; 

break; 

case Tool::Arrow: 

ss<<" {\n" 

<<" float aw = std::min("<<(int)oh<<"*0.5f, "<<(int)ow<<"*0.35f);\n" 

<<" float ah = "<<(int)ow<<"*0.3f;\n" 

<<" std::vector<sf::Vector2f> pts = {\n" 

<<" {"<<(int)x1<<", "<<(int)cy<<"-aw/2}, {"<<(int)x2<<"-ah, "<<(int)cy<<"-aw/2},\n" 

<<" {"<<(int)x2<<"-ah, "<<(int)y1<<"}, {"<<(int)x2<<", "<<(int)cy<<"},\n" 

<<" {"<<(int)x2<<"-ah, "<<(int)y2<<"}, {"<<(int)x2<<"-ah, "<<(int)cy<<"+aw/2},\n" 

<<" {"<<(int)x1<<", "<<(int)cy<<"+aw/2}\n" 

<<" };\n" 

<<" sf::ConvexShape shape(pts.size());\n" 

<<" for(size_t i=0; i<pts.size(); ++i) shape.setPoint(i, pts[i]);\n" 

<<" shape.setOutlineThickness("<<o.sz<<");\n" 

<<" shape.setOutlineColor("<<sc2<<");\n" 

<<" shape.setFillColor("<<fil<<");\n" 

<<" canvas.draw(shape, "<<bm<<");\n" 

<<" }\n"; 

break; 

case Tool::Cross: 

ss<<" {\n" 

<<" float tw = "<<(int)ow<<"/3.f, th2 = "<<(int)oh<<"/3.f;\n" 

<<" std::vector<sf::Vector2f> pts = {\n" 

<<" {"<<(int)ox<<"+tw, "<<(int)oy<<"}, {"<<(int)ox<<"+tw*2, "<<(int)oy<<"},\n" 

<<" {"<<(int)ox<<"+tw*2, "<<(int)oy<<"+th2}, {"<<(int)(ox+ow)<<", "<<(int)oy<<"+th2},\n" 

<<" {"<<(int)(ox+ow)<<", "<<(int)oy<<"+th2*2}, {"<<(int)ox<<"+tw*2, "<<(int)oy<<"+th2*2},\n" 

<<" {"<<(int)ox<<"+tw*2, "<<(int)(oy+oh)<<"}, {"<<(int)ox<<"+tw, "<<(int)(oy+oh)<<"},\n" 

<<" {"<<(int)ox<<"+tw, "<<(int)oy<<"+th2*2}, {"<<(int)ox<<", "<<(int)oy<<"+th2*2},\n" 

<<" {"<<(int)ox<<", "<<(int)oy<<"+th2}, {"<<(int)ox<<"+tw, "<<(int)oy<<"+th2}\n" 

<<" };\n" 

<<" sf::ConvexShape shape(pts.size());\n" 

<<" for(size_t i=0; i<pts.size(); ++i) shape.setPoint(i, pts[i]);\n" 

<<" shape.setOutlineThickness("<<o.sz<<");\n" 

<<" shape.setOutlineColor("<<sc2<<");\n" 

<<" shape.setFillColor("<<fil<<");\n" 

<<" canvas.draw(shape, "<<bm<<");\n" 

<<" }\n"; 

break; 

case Tool::Heart: 

ss<<" {\n" 

<<" sf::CircleShape lc("<<(int)(rx*0.55f)<<"), rc2("<<(int)(rx*0.55f)<<");\n" 

<<" lc.setOrigin("<<(int)(rx*0.55f)<<", "<<(int)(rx*0.55f)<<");\n" 

<<" lc.setPosition("<<(int)(cx-rx*0.8f)<<", "<<(int)(cy-rx*0.2f)<<");\n" 

<<" rc2.setOrigin("<<(int)(rx*0.55f)<<", "<<(int)(rx*0.55f)<<");\n" 

<<" rc2.setPosition("<<(int)(cx+rx*0.8f)<<", "<<(int)(cy-rx*0.2f)<<");\n" 

<<" lc.setFillColor("<<fil<<"); rc2.setFillColor("<<fil<<");\n" 

<<" canvas.draw(lc, "<<bm<<"); canvas.draw(rc2, "<<bm<<");\n" 

<<" sf::ConvexShape shape(3);\n" 

<<" shape.setPoint(0, sf::Vector2f("<<(int)(cx-rx)<<", "<<(int)cy<<"));\n" 

<<" shape.setPoint(1, sf::Vector2f("<<(int)cx<<", "<<(int)(cy+ry*0.9f)<<"));\n" 

<<" shape.setPoint(2, sf::Vector2f("<<(int)(cx+rx)<<", "<<(int)cy<<"));\n" 

<<" shape.setFillColor("<<fil<<");\n" 

<<" canvas.draw(shape, "<<bm<<");\n" 

<<" }\n"; 

break; 

case Tool::Semicircle: 

ss<<" {\n" 

<<" std::vector<sf::Vector2f> pts;\n" 

<<" pts.push_back({"<<(int)(cx-rx)<<", "<<(int)cy<<"});\n" 

<<" for(int k=0; k<=20; k++) {\n" 

<<" float a = 3.14159f + k * 3.14159f / 20.f;\n" 

<<" pts.push_back({"<<(int)cx<<" + "<<(int)rx<<" * std::cos(a), "<<(int)cy<<" + "<<(int)ry<<" * std::sin(a)});\n" 

<<" }\n" 

<<" pts.push_back({"<<(int)(cx+rx)<<", "<<(int)cy<<"});\n" 

<<" sf::ConvexShape shape(pts.size());\n" 

<<" for(size_t i=0; i<pts.size(); ++i) shape.setPoint(i, pts[i]);\n" 

<<" shape.setOutlineThickness("<<o.sz<<");\n" 

<<" shape.setOutlineColor("<<sc2<<");\n" 

<<" shape.setFillColor("<<fil<<");\n" 

<<" canvas.draw(shape, "<<bm<<");\n" 

<<" }\n"; 

break; 

case Tool::Arc: 

ss<<" {\n" 

<<" std::vector<sf::Vector2f> pts;\n" 

<<" pts.push_back({"<<(int)cx<<", "<<(int)cy<<"});\n" 

<<" for(int k=0; k<=24; k++) {\n" 

<<" float a = k * 1.5f * 3.14159f / 24.f;\n" 

<<" pts.push_back({"<<(int)cx<<" + "<<(int)rx<<" * std::cos(a), "<<(int)cy<<" + "<<(int)ry<<" * std::sin(a)});\n" 

<<" }\n" 

<<" sf::ConvexShape shape(pts.size());\n" 

<<" for(size_t i=0; i<pts.size(); ++i) shape.setPoint(i, pts[i]);\n" 

<<" shape.setOutlineThickness("<<o.sz<<");\n" 

<<" shape.setOutlineColor("<<sc2<<");\n" 

<<" shape.setFillColor("<<fil<<");\n" 

<<" canvas.draw(shape, "<<bm<<");\n" 

<<" }\n"; 

break; 

case Tool::Pencil: 

case Tool::Eraser: {  

if(o.pts.empty()) break; 

ss<<" // Stroke: "<<o.pts.size()<<" pts, thickness="<<o.sz<<"px\n"  

<<" {\n"  

<<" std::vector<sf::Vector2f> pts = {\n";  

for(size_t k=0;k<o.pts.size();k++) 

ss<<" sf::Vector2f("<<(int)o.pts[k].x<<","<<(int)o.pts[k].y<<"),\n";  

ss<<" };\n"  

<<" float thick = "<<o.sz<<";\n"  

<<" sf::Color col = "<<(o.type == Tool::Eraser ? "sf::Color::White" : sc2)<<";\n"  

<<" if(pts.size() == 1) {\n" 

<<" sf::CircleShape cap(thick/2.f);\n" 

<<" cap.setOrigin(thick/2.f, thick/2.f);\n" 

<<" cap.setPosition(pts[0]);\n" 

<<" cap.setFillColor(col);\n" 

<<" canvas.draw(cap, "<<bm<<");\n" 

<<" } else if(pts.size() > 1) {\n" 

<<" for (size_t k=1; k<pts.size(); k++) {\n"  

<<" float dx=pts[k].x-pts[k-1].x, dy=pts[k].y-pts[k-1].y;\n"  

<<" float len=std::sqrt(dx*dx+dy*dy);\n"  

<<" if(len<0.5f) continue;\n"  

<<" sf::RectangleShape seg(sf::Vector2f(len,thick));\n"  

<<" seg.setOrigin(0,thick/2);\n"  

<<" seg.setPosition(pts[k-1]);\n"  

<<" seg.setRotation(std::atan2(dy,dx)*180.f/3.14159f);\n"  

<<" seg.setFillColor(col);\n"  

<<" canvas.draw(seg, "<<bm<<");\n"  

<<" }\n"  

<<" }\n" 

<<" }\n";  

break;}  

case Tool::Blend: {  

if(o.pts.empty()) break; 

ss<<" // Smudge/Blend Stroke: "<<o.pts.size()<<" pts, brush size="<<o.sz<<"px\n"  

<<" {\n"  

<<" std::vector<sf::Vector2f> pts = {\n";  

for(size_t k=0;k<o.pts.size();k++) 

ss<<" sf::Vector2f("<<(int)o.pts[k].x<<","<<(int)o.pts[k].y<<"),\n";  

ss<<" };\n"  

<<" float r = "<<o.sz<<";\n"  

<<" if(pts.size() > 1) {\n" 

<<" for (size_t k=1; k<pts.size(); k++) {\n"  

<<" sf::Sprite brush(canvas.getTexture());\n"  

<<" brush.setTextureRect(sf::IntRect((int)(pts[k-1].x - r/2.f), (int)(pts[k-1].y - r/2.f), (int)r, (int)r));\n"  

<<" brush.setOrigin(r/2.f, r/2.f);\n"  

<<" brush.setPosition(pts[k]);\n"  

<<" brush.setColor(sf::Color(255, 255, 255, 60));\n"  

<<" canvas.draw(brush, sf::BlendAlpha);\n"  

<<" canvas.display();\n"  

<<" }\n"  

<<" }\n" 

<<" }\n";  

break;} 

default:  

ss<<" // "<<toolName(o.type)<<" — custom shape fallback\n";  

break;  

}  

ss << " }\n";  

}  

ss << "}\n";  

return ss.str();  

} 

 
// ─────────────────────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────────────────────
int main()
{
    sf::RenderWindow window(sf::VideoMode(WIN_W,WIN_H),"SFML Paint Pro",sf::Style::Default);
    window.setView(sf::View(sf::FloatRect(0, 0, WIN_W, WIN_H)));
    window.setFramerateLimit(60);
    
    bool isFullscreen = false;

    sf::Font font;bool hasFont=false;
    for(auto&p:{"arial.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/System/Library/Fonts/Helvetica.ttc",
                "C:/Windows/Fonts/arial.ttf"})
        if(!hasFont&&font.loadFromFile(p))hasFont=true;

    sf::RenderTexture canvas;
    canvas.create(CVS_W,CVS_H);canvas.clear(sf::Color::White);canvas.display();

    std::vector<ShapeObj>objects;
    std::deque<sf::Image>undoStack;
    Tool curTool=Tool::Pencil;
    sf::Color strokeCol{0,0,0},fillCol{255,255,255};
    float opacity=1.f;
    bool fillMode=false,showGrid=false;
    BM blendMode=BM::Normal;
    float zoom=1.f;
    bool drawing=false;
    sf::Vector2f drawStart;
    std::vector<sf::Vector2f>pencilPts;
    bool textMode=false;
    std::string textBuf;
    sf::Vector2f textPos;
    ShapeObj*selObj=nullptr;
    
    bool selResizing = false;
    
    sf::Vector2f dragOff;
    Picker picker;
    BlendPanel blendPanel;
    blendPanel.btnX=(float)SLIDER_X;
    std::string codeText="// Draw something!\n";
    bool mwasDown=false;

    Slider strokeSlider{(float)SLIDER_X,(float)LP_SLD1_Y,(float)SLIDER_W,1.f,50.f,3.f};
    Slider blendSlider {(float)SLIDER_X,(float)LP_SLD2_Y,(float)SLIDER_W,2.f,60.f,10.f};
    Slider opacSlider  {(float)SLIDER_X,(float)LP_SLD3_Y,(float)SLIDER_W,0.05f,1.f,1.f};

    std::vector<sf::Color>quickPal={
        {0,0,0},{255,255,255},{192,192,192},{128,128,128},
        {255,0,0},{0,255,0},{0,0,255},{255,255,0},
        {255,0,255},{0,255,255},{128,0,0},{0,128,0},
        {0,0,128},{128,128,0},{255,128,0},{0,128,128}};

    auto saveUndo=[&](){
        undoStack.push_back(canvas.getTexture().copyToImage());
        if(undoStack.size()>MAX_UNDO)undoStack.pop_front();};
    auto doUndo=[&](){
        if(undoStack.empty())return;
        sf::Image img=undoStack.back();undoStack.pop_back();
        sf::Texture t;t.loadFromImage(img);
        canvas.clear(sf::Color::White);sf::Sprite sp(t);canvas.draw(sp);canvas.display();
        if(!objects.empty())objects.pop_back();codeText=genCode(objects);};
    auto toCanvas=[&](sf::Vector2i mp)->sf::Vector2f{return{(mp.x-CVS_X)/zoom,(mp.y-CVS_Y)/zoom};};
    auto inCanvas=[&](sf::Vector2i mp)->bool{sf::Vector2f p=toCanvas(mp);return p.x>=0&&p.y>=0&&p.x<CVS_W&&p.y<CVS_H;};
    auto commit=[&](ShapeObj obj){
        saveUndo();renderShape(canvas,obj,hasFont?&font:nullptr);canvas.display();
        objects.push_back(obj);codeText=genCode(objects);};

    struct TBtn{float x,y,w,h;Tool t;const char*lbl;};
    const TBtn tbns[]={
        {  8, 4,44,38,Tool::Select,       "Sel" },
        { 56, 4,44,38,Tool::Pencil,        "Pen" },
        {104, 4,44,38,Tool::Eraser,        "Era" },
        {152, 4,44,38,Tool::Bucket,        "Bkt" },
        {200, 4,44,38,Tool::Text,          "Txt" },
        {248, 4,44,38,Tool::Blend,         "Bld" },
        {  8,46,44,38,Tool::Line,          "Line"},
        { 56,46,44,38,Tool::Rect,          "Rect"},
        {104,46,44,38,Tool::RRect,         "RRct"},
        {152,46,44,38,Tool::Ellipse,       "Ell" },
        {200,46,44,38,Tool::Triangle,      "Tri" },
        {248,46,44,38,Tool::RTriangle,     "RTri"},
        {296, 4,44,38,Tool::Pentagon,      "Pent"},
        {344, 4,44,38,Tool::Hexagon,       "Hex" },
        {392, 4,44,38,Tool::Octagon,       "Oct" },
        {440, 4,44,38,Tool::Star5,         "Str5"},
        {488, 4,44,38,Tool::Star6,         "Str6"},
        {536, 4,44,38,Tool::Semicircle,    "Semi"},
        {296,46,44,38,Tool::Arrow,         "Arw" },
        {344,46,44,38,Tool::Parallelogram, "Para"},
        {392,46,44,38,Tool::Diamond,       "Diam"},
        {440,46,44,38,Tool::Cross,         "Crss"},
        {488,46,44,38,Tool::Heart,         "Hrt" },
        {536,46,44,38,Tool::Arc,           "Arc" },
    };

    while(window.isOpen())
    {
        // Proper coordinate scaling for Fullscreen resolution handling
        sf::Vector2i rawPixel = sf::Mouse::getPosition(window);
        sf::Vector2f mouseF = window.mapPixelToCoords(rawPixel);
        sf::Vector2i rawM((int)mouseF.x, (int)mouseF.y);
        
        bool mdown=sf::Mouse::isButtonPressed(sf::Mouse::Left);
        bool mjust=mdown&&!mwasDown;
        bool mup  =!mdown&&mwasDown;

        sf::Event ev;
        while(window.pollEvent(ev)){
            if(ev.type==sf::Event::Closed)window.close();
            if(ev.type==sf::Event::KeyPressed&&!picker.open&&!blendPanel.open){
                auto k=ev.key.code;
                if(ev.key.control&&k==sf::Keyboard::Z)doUndo();
                if(k==sf::Keyboard::G)showGrid=!showGrid;
                if(k==sf::Keyboard::F)fillMode=!fillMode;
                if(k==sf::Keyboard::Equal||k==sf::Keyboard::Add)    zoom=std::min(4.f,zoom+0.25f);
                if(k==sf::Keyboard::Hyphen||k==sf::Keyboard::Subtract)zoom=std::max(0.25f,zoom-0.25f);
                if(k==sf::Keyboard::P)curTool=Tool::Pencil;
                if(k==sf::Keyboard::E)curTool=Tool::Eraser;
                if(k==sf::Keyboard::T)curTool=Tool::Text;
                if(k==sf::Keyboard::B)curTool=Tool::Bucket;
                if(k==sf::Keyboard::L)curTool=Tool::Line;
                if(k==sf::Keyboard::R)curTool=Tool::Rect;
                if(k==sf::Keyboard::C)curTool=Tool::Ellipse;
                if(k==sf::Keyboard::Delete&&!objects.empty())doUndo();}
            if(ev.type==sf::Event::TextEntered&&textMode&&!picker.open&&!blendPanel.open){
                sf::Uint32 ch=ev.text.unicode;
                if(ch=='\r'||ch=='\n'){
                    if(!textBuf.empty()){
                        ShapeObj obj;obj.type=Tool::Text;obj.p1=textPos;obj.p2=textPos;
                        obj.text=textBuf;obj.fontSize=std::max(12,(int)strokeSlider.val*5);
                        obj.sc=strokeCol;obj.fc=fillCol;obj.opacity=opacity;obj.sz=(int)strokeSlider.val;obj.blend=blendMode;
                        commit(obj);}textMode=false;textBuf="";}
                else if(ch==8){if(!textBuf.empty())textBuf.pop_back();}
                else if(ch<128)textBuf+=(char)ch;}}

        if(!picker.open&&!blendPanel.open&&mdown){
            if(strokeSlider.hitRect().contains(mouseF)){strokeSlider.setFromMouse(mouseF.x);opacity=opacSlider.val;}
            if(blendSlider.hitRect().contains(mouseF)) blendSlider.setFromMouse(mouseF.x);
            if(opacSlider.hitRect().contains(mouseF))  {opacSlider.setFromMouse(mouseF.x);opacity=opacSlider.val;}
        }

        if(!picker.open&&!blendPanel.open){
            bool onSliders=strokeSlider.hitRect().contains(mouseF)||
                           blendSlider.hitRect().contains(mouseF)||
                           opacSlider.hitRect().contains(mouseF);
            if(mjust&&inCanvas(rawM)){
                sf::Vector2f cp=toCanvas(rawM);
                if(curTool==Tool::Bucket){
                    saveUndo();floodFill(canvas,{(unsigned)cp.x,(unsigned)cp.y},fillCol);
                    ShapeObj obj;obj.type=Tool::Bucket;obj.p1=cp;obj.p2=cp;
                    obj.sc=fillCol;obj.fc=fillCol;obj.opacity=opacity;obj.sz=1;obj.blend=blendMode;
                    objects.push_back(obj);codeText=genCode(objects);
                }else if(curTool==Tool::Text){textMode=true;textBuf="";textPos=cp;}
                else if(curTool==Tool::Pencil||curTool==Tool::Eraser||curTool==Tool::Blend){
                    saveUndo();drawing=true;pencilPts.clear();pencilPts.push_back(cp);}
                else if(curTool==Tool::Select){
                    selObj=nullptr;
                    selResizing = false;
                    for(auto it=objects.rbegin();it!=objects.rend();++it){
                        float ex=std::min(it->p1.x,it->p2.x),ey=std::min(it->p1.y,it->p2.y);
                        float ew=std::abs(it->p2.x-it->p1.x),eh=std::abs(it->p2.y-it->p1.y);
                        sf::FloatRect bounds(ex-8, ey-8, ew+16, eh+16);
                        sf::FloatRect resizeHandle(ex+ew-8, ey+eh-8, 16, 16);
                        
                        if(resizeHandle.contains(cp)){
                            selObj=&(*it);
                            selResizing = true;
                            selObj->p1 = sf::Vector2f(ex, ey);
                            selObj->p2 = sf::Vector2f(ex+ew, ey+eh);
                            break;
                        } else if(bounds.contains(cp)){
                            selObj=&(*it);
                            dragOff=cp-it->p1;
                            break;
                        }
                    }
                }
                else{drawing=true;drawStart=cp;}}
                
            if(mdown){
                sf::Vector2f cp=toCanvas(rawM);
                if(drawing&&(curTool==Tool::Pencil||curTool==Tool::Eraser||curTool==Tool::Blend)&&!pencilPts.empty()){
                    sf::Vector2f last=pencilPts.back(),diff=cp-last;
                    if(diff.x*diff.x+diff.y*diff.y>1){
                        pencilPts.push_back(cp);
                        if(curTool==Tool::Pencil){
                            sf::Color sc2=strokeCol;sc2.a=(sf::Uint8)(opacity*255);
                            drawThickSeg(canvas,last,cp,sc2,(float)strokeSlider.val,sfBlend(blendMode));
                        }else if(curTool==Tool::Eraser){
                            float len=std::sqrt(diff.x*diff.x+diff.y*diff.y);
                            sf::RectangleShape rs(sf::Vector2f(len,(float)strokeSlider.val*3));
                            rs.setOrigin(0,(float)strokeSlider.val*1.5f);rs.setPosition(last);
                            rs.setRotation(std::atan2(diff.y,diff.x)*180.f/(float)M_PI);
                            rs.setFillColor(sf::Color::White);canvas.draw(rs);
                        }else{
                            float r = blendSlider.val;
                            sf::Sprite brush(canvas.getTexture());
                            brush.setTextureRect(sf::IntRect((int)(last.x - r/2.f), (int)(last.y - r/2.f), (int)r, (int)r));
                            brush.setOrigin(r/2.f, r/2.f);
                            brush.setPosition(cp);
                            brush.setColor(sf::Color(255, 255, 255, 60)); 
                            canvas.draw(brush, sf::BlendAlpha);
                        }
                        canvas.display();}}
                if(curTool==Tool::Select&&selObj){
                    if (selResizing) {
                        selObj->p2 = cp; 
                    } else {
                        sf::Vector2f npos=cp-dragOff,delta=npos-selObj->p1;
                        selObj->p1+=delta;selObj->p2+=delta;
                    }
                    canvas.clear(sf::Color::White);
                    for(auto&o:objects)renderShape(canvas,o,hasFont?&font:nullptr);
                    canvas.display();codeText=genCode(objects);}}
            if(mup&&drawing){
                drawing=false;sf::Vector2f cp=toCanvas(rawM);
                if(curTool==Tool::Pencil||curTool==Tool::Eraser||curTool==Tool::Blend){
                    if(!pencilPts.empty()){
                        ShapeObj obj;obj.type=curTool;
                        obj.pts=pencilPts;obj.p1=pencilPts.front();obj.p2=pencilPts.back();
                        obj.sc=(curTool==Tool::Eraser)?sf::Color::White:strokeCol;
                        obj.fc=fillCol;obj.opacity=opacity;
                        obj.sz=(curTool==Tool::Blend)?(int)blendSlider.val:(int)strokeSlider.val;
                        obj.blend=blendMode;objects.push_back(obj);codeText=genCode(objects);}
                    pencilPts.clear();
                }else if(curTool!=Tool::Select&&curTool!=Tool::Text&&curTool!=Tool::Bucket){
                    if(std::abs(cp.x-drawStart.x)>3||std::abs(cp.y-drawStart.y)>3){
                        ShapeObj obj;obj.type=curTool;obj.p1=drawStart;obj.p2=cp;
                        obj.sc=strokeCol;obj.fc=fillCol;obj.opacity=opacity;
                        obj.sz=(int)strokeSlider.val;obj.filled=fillMode;obj.blend=blendMode;commit(obj);}}}

            if(mjust&&!onSliders){
                auto mp=mouseF;
                if(sf::FloatRect(10,(float)LP_SWATCH_Y,36,36).contains(mp))picker.openWith(strokeCol,true);
                if(sf::FloatRect(50,(float)LP_SWATCH_Y,36,36).contains(mp))picker.openWith(fillCol,false);
                int palStartY=LP_PAL_LABEL+14;
                for(int i=0;i<(int)quickPal.size();i++){
                    int c2=i%4,r2=i/4;
                    if(sf::FloatRect(10+c2*44.f,(float)(palStartY+r2*22),20,20).contains(mp))
                        strokeCol=quickPal[i];}
                if(sf::FloatRect(10,(float)LP_BTN_Y,80,18).contains(mp))fillMode=!fillMode;
                if(sf::FloatRect(10,(float)(LP_BTN_Y+22),80,18).contains(mp))showGrid=!showGrid;
                if(sf::FloatRect(10,(float)(LP_BTN_Y+48),86,20).contains(mp))doUndo();
                if(sf::FloatRect(10,(float)(LP_BTN_Y+72),86,20).contains(mp)){
                    objects.clear();selObj=nullptr;canvas.clear(sf::Color::White);canvas.display();codeText="// Draw something!\n";}
                
                // Fullscreen Toggle Detection
                if(sf::FloatRect(10,(float)(WIN_H-SB_H-76),86,20).contains(mp)){
                    isFullscreen = !isFullscreen;
                    if(isFullscreen) {
                        window.create(sf::VideoMode::getDesktopMode(), "SFML Paint Pro", sf::Style::Fullscreen);
                    } else {
                        window.create(sf::VideoMode(WIN_W, WIN_H), "SFML Paint Pro", sf::Style::Default);
                    }
                    window.setView(sf::View(sf::FloatRect(0, 0, WIN_W, WIN_H)));
                    window.setFramerateLimit(60);
                }
                
                if(sf::FloatRect(10,(float)(WIN_H-SB_H-52),86,20).contains(mp)){
                    sf::Image img=canvas.getTexture().copyToImage();img.saveToFile("canvas_output.png");std::cout<<"[Saved]\n";}
                if(sf::FloatRect(10,(float)(WIN_H-SB_H-28),86,20).contains(mp))std::cout<<codeText;
                if(rawM.y<TB_H)for(auto&b:tbns)if(sf::FloatRect(b.x,b.y,b.w,b.h).contains(mp))curTool=b.t;}}

        mwasDown=mdown;

        window.clear(sf::Color(200,200,200));

        // Toolbar
        {sf::RectangleShape tb(sf::Vector2f((float)WIN_W,(float)TB_H));tb.setFillColor(sf::Color(240,240,240));tb.setOutlineColor(sf::Color(150,150,150));tb.setOutlineThickness(1);window.draw(tb);
        if(hasFont)for(auto&b:tbns)
            drawBtn(window,font,{b.x,b.y},{b.w,b.h},b.lbl,curTool==b.t,
                curTool==b.t?sf::Color(90,150,240):sf::Color(210,210,215), 
                curTool==b.t?sf::Color::White:sf::Color::Black);
        }

        // Left panel
        {sf::RectangleShape lp(sf::Vector2f((float)LEFT_W,(float)(WIN_H-TB_H-SB_H)));
        lp.setPosition(0,(float)TB_H);lp.setFillColor({38,38,52});lp.setOutlineColor({55,55,80});lp.setOutlineThickness(1);window.draw(lp);
        if(hasFont){
            auto lbl=[&](const std::string&s,float x,float y){sf::Text t(s,font,8);t.setFillColor({150,150,180});t.setPosition(x,y);window.draw(t);};

            lbl("STROKE",10,(float)(LP_SWATCH_Y-13));
            lbl("FILL",  54,(float)(LP_SWATCH_Y-13));
            auto swatch=[&](sf::Vector2f pos,sf::Color c,bool thick){
                sf::RectangleShape s(sf::Vector2f(36,36));s.setPosition(pos);s.setFillColor(c);
                s.setOutlineColor(thick?sf::Color(100,130,255):sf::Color(80,80,100));s.setOutlineThickness(thick?2.f:1.f);window.draw(s);};
            swatch({10,(float)LP_SWATCH_Y},strokeCol,true);
            swatch({50,(float)LP_SWATCH_Y},fillCol,false);

            lbl("PALETTE",10,(float)LP_PAL_LABEL);
            int palStartY=LP_PAL_LABEL+14;
            for(int i=0;i<(int)quickPal.size();i++){
                int c2=i%4,r2=i/4;sf::RectangleShape s(sf::Vector2f(20,20));
                s.setPosition(10+c2*44.f,(float)(palStartY+r2*22));s.setFillColor(quickPal[i]);
                s.setOutlineColor({80,80,100});s.setOutlineThickness(0.5f);window.draw(s);}

            lbl("STROKE SIZE",(float)SLIDER_X,(float)(LP_SLD1_Y));
            {float ty=LP_SLD1_Y+14;
            sf::RectangleShape track(sf::Vector2f(SLIDER_W,4));track.setPosition(SLIDER_X,ty);track.setFillColor({70,70,90});window.draw(track);
            sf::RectangleShape fill2(sf::Vector2f(SLIDER_W*strokeSlider.norm(),4));fill2.setPosition(SLIDER_X,ty);fill2.setFillColor({80,140,240});window.draw(fill2);
            float tx=SLIDER_X+SLIDER_W*strokeSlider.norm();
            sf::CircleShape thumb(6);thumb.setOrigin(6,6);thumb.setPosition(tx,ty+2);thumb.setFillColor({130,180,255});thumb.setOutlineColor({60,100,200});thumb.setOutlineThickness(1.5f);window.draw(thumb);
            sf::Text val(std::to_string((int)strokeSlider.val)+"px",font,8);val.setFillColor({170,170,200});val.setPosition(SLIDER_X+SLIDER_W+5,ty-2);window.draw(val);
            float bw2=std::min((float)strokeSlider.val*2.f,(float)SLIDER_W);
            float bh2=std::min(strokeSlider.val,(float)10);
            sf::RectangleShape bar(sf::Vector2f(bw2,bh2));bar.setPosition(SLIDER_X,ty+12);bar.setFillColor(strokeCol);window.draw(bar);}

            lbl("BLEND BRUSH",(float)SLIDER_X,(float)(LP_SLD2_Y));
            {float ty=LP_SLD2_Y+14;
            sf::RectangleShape track(sf::Vector2f(SLIDER_W,4));track.setPosition(SLIDER_X,ty);track.setFillColor({70,70,90});window.draw(track);
            sf::RectangleShape fill2(sf::Vector2f(SLIDER_W*blendSlider.norm(),4));fill2.setPosition(SLIDER_X,ty);fill2.setFillColor({80,140,240});window.draw(fill2);
            float tx=SLIDER_X+SLIDER_W*blendSlider.norm();
            sf::CircleShape thumb(6);thumb.setOrigin(6,6);thumb.setPosition(tx,ty+2);thumb.setFillColor({130,180,255});thumb.setOutlineColor({60,100,200});thumb.setOutlineThickness(1.5f);window.draw(thumb);
            sf::Text val(std::to_string((int)blendSlider.val)+"px",font,8);val.setFillColor({170,170,200});val.setPosition(SLIDER_X+SLIDER_W+5,ty-2);window.draw(val);}

            lbl("OPACITY",(float)SLIDER_X,(float)(LP_SLD3_Y));
            {float ty=LP_SLD3_Y+14;
            for(int xi=0;xi<SLIDER_W/6;xi++){sf::RectangleShape ck(sf::Vector2f(6,4));ck.setPosition(SLIDER_X+xi*6,ty);ck.setFillColor((xi%2==0)?sf::Color(140,140,140):sf::Color(200,200,200));window.draw(ck);}
            sf::Color gradC=strokeCol;gradC.a=200;
            sf::RectangleShape fill2(sf::Vector2f(SLIDER_W*opacSlider.norm(),4));fill2.setPosition(SLIDER_X,ty);fill2.setFillColor(gradC);window.draw(fill2);
            float tx=SLIDER_X+SLIDER_W*opacSlider.norm();
            sf::CircleShape thumb(6);thumb.setOrigin(6,6);thumb.setPosition(tx,ty+2);thumb.setFillColor({220,220,255});thumb.setOutlineColor({80,100,180});thumb.setOutlineThickness(1.5f);window.draw(thumb);
            sf::Text val(std::to_string((int)(opacSlider.val*100))+"%",font,8);val.setFillColor({170,170,200});val.setPosition(SLIDER_X+SLIDER_W+5,ty-2);window.draw(val);}

            drawBtn(window,font,{10,(float)LP_BTN_Y},{86,18},fillMode?"Fill: ON":"Fill: OFF",fillMode,fillMode?sf::Color(60,90,160):sf::Color(55,55,75));
            drawBtn(window,font,{10,(float)(LP_BTN_Y+22)},{86,18},showGrid?"Grid: ON":"Grid: OFF",showGrid,showGrid?sf::Color(60,90,160):sf::Color(55,55,75));
            drawBtn(window,font,{10,(float)(LP_BTN_Y+48)},{86,20},"Undo (Ctrl+Z)");
            drawBtn(window,font,{10,(float)(LP_BTN_Y+72)},{86,20},"Clear All",false,sf::Color(100,40,40));

            sf::Text hk("[P]en [E]ra [B]kt\n[T]xt [L]ine [R]ect\n[G]rid [F]ill +/-Zoom",font,8);
            hk.setFillColor({90,90,110});hk.setPosition(6,(float)(WIN_H-SB_H-100));window.draw(hk);
            
            // Fullscreen Button
            drawBtn(window,font,{10,(float)(WIN_H-SB_H-76)},{86,20},isFullscreen ? "Exit Fullscreen" : "Fullscreen", false, sf::Color(80,60,100));
            drawBtn(window,font,{10,(float)(WIN_H-SB_H-52)},{86,20},"Save PNG",false,sf::Color(40,80,40));
            drawBtn(window,font,{10,(float)(WIN_H-SB_H-28)},{86,20},"Print Code");

            lbl("BLEND MODE",(float)SLIDER_X,(float)(LP_BTN_Y-40));
            blendPanel.btnY=(float)(LP_BTN_Y-28);
            drawBlendPanel(window,font,blendMode,opacSlider.val,strokeCol,mouseF,mdown,mjust,blendPanel);
        }}

        // Canvas
        {for(int y=CVS_Y;y<CVS_Y+CVS_H;y+=16)for(int x=CVS_X;x<CVS_X+CVS_W;x+=16){
            sf::RectangleShape t(sf::Vector2f(16,16));t.setPosition((float)x,(float)y);
            t.setFillColor(((x/16+y/16)%2==0)?sf::Color(175,175,175):sf::Color(205,205,205));window.draw(t);}
        sf::Sprite sp(canvas.getTexture());sp.setPosition((float)CVS_X,(float)CVS_Y);sp.setScale(zoom,zoom);window.draw(sp);
        sf::RectangleShape brd(sf::Vector2f(CVS_W*zoom+2,CVS_H*zoom+2));brd.setPosition(CVS_X-1.f,CVS_Y-1.f);brd.setFillColor(sf::Color::Transparent);brd.setOutlineColor({100,100,100});brd.setOutlineThickness(1);window.draw(brd);
        if(showGrid){float gs=20.f*zoom;
            for(float x=(float)CVS_X;x<CVS_X+CVS_W*zoom;x+=gs){sf::Vertex ln[2]={{sf::Vector2f(x,(float)CVS_Y),sf::Color(0,100,255,35)},{sf::Vector2f(x,(float)(CVS_Y+CVS_H*zoom)),sf::Color(0,100,255,35)}};window.draw(ln,2,sf::Lines);}
            for(float y=(float)CVS_Y;y<CVS_Y+CVS_H*zoom;y+=gs){sf::Vertex ln[2]={{sf::Vector2f((float)CVS_X,y),sf::Color(0,100,255,35)},{sf::Vector2f((float)(CVS_X+CVS_W*zoom),y),sf::Color(0,100,255,35)}};window.draw(ln,2,sf::Lines);}}
        if(drawing&&curTool!=Tool::Pencil&&curTool!=Tool::Eraser&&curTool!=Tool::Blend){
            sf::Vector2f cp=toCanvas(rawM);sf::RenderTexture pv;pv.create(CVS_W,CVS_H);pv.clear(sf::Color::Transparent);
            ShapeObj pr;pr.type=curTool;pr.p1=drawStart;pr.p2=cp;pr.sc=strokeCol;pr.fc=fillCol;pr.opacity=0.55f;pr.sz=(int)strokeSlider.val;pr.filled=fillMode;pr.blend=blendMode;
            renderShape(pv,pr,hasFont?&font:nullptr);pv.display();
            sf::Sprite ps(pv.getTexture());ps.setPosition((float)CVS_X,(float)CVS_Y);ps.setScale(zoom,zoom);window.draw(ps);}
        if(textMode&&hasFont){sf::Text tc(textBuf+"|",font,(unsigned)std::max(12,(int)strokeSlider.val*5));tc.setFillColor(strokeCol);tc.setPosition(CVS_X+textPos.x*zoom,CVS_Y+textPos.y*zoom);window.draw(tc);}
        
        if(selObj&&curTool==Tool::Select){
            float ex=std::min(selObj->p1.x,selObj->p2.x),ey=std::min(selObj->p1.y,selObj->p2.y);
            float ew=std::abs(selObj->p2.x-selObj->p1.x),eh=std::abs(selObj->p2.y-selObj->p1.y);
            
            sf::RectangleShape sel(sf::Vector2f(ew*zoom+16,eh*zoom+16));
            sel.setPosition(CVS_X+(ex-8)*zoom,CVS_Y+(ey-8)*zoom);
            sel.setFillColor(sf::Color::Transparent);
            sel.setOutlineColor({50,120,255});
            sel.setOutlineThickness(1.5f);
            window.draw(sel);
            
            sf::RectangleShape handle(sf::Vector2f(10, 10));
            handle.setOrigin(5, 5);
            handle.setPosition(CVS_X + (ex+ew)*zoom + 8, CVS_Y + (ey+eh)*zoom + 8);
            handle.setFillColor(sf::Color::White);
            handle.setOutlineColor(sf::Color::Blue);
            handle.setOutlineThickness(1);
            window.draw(handle);
        }}

        // Right code panel
        {sf::RectangleShape rp(sf::Vector2f((float)RIGHT_W,(float)(WIN_H-TB_H-SB_H)));rp.setPosition((float)(WIN_W-RIGHT_W),(float)TB_H);rp.setFillColor({18,18,28});window.draw(rp);
        if(hasFont){sf::Text hdr("SFML 2 Code Output",font,10);hdr.setFillColor({100,120,180});hdr.setPosition((float)(WIN_W-RIGHT_W+6),(float)(CVS_Y+4));window.draw(hdr);
            std::istringstream ss(codeText);std::string line2;int lineN=0;float ly=(float)(CVS_Y+20);
            while(std::getline(ss,line2)&&lineN<48){
                sf::Color lc=(line2.size()>0&&(line2[0]=='/'||line2[0]==' '&&line2.size()>2&&line2[2]=='/'))?sf::Color(90,100,130):sf::Color(130,210,130);
                sf::Text lt(line2,font,8);lt.setFillColor(lc);lt.setPosition((float)(WIN_W-RIGHT_W+4),ly);window.draw(lt);ly+=10.f;lineN++;}
            if(lineN>=48){sf::Text more("... Print Code for full output",font,8);more.setFillColor({80,80,100});more.setPosition((float)(WIN_W-RIGHT_W+4),ly);window.draw(more);}
            sf::Text inf(std::to_string(objects.size())+" obj | "+std::to_string(undoStack.size())+" undo",font,8);inf.setFillColor({100,100,130});inf.setPosition((float)(WIN_W-RIGHT_W+4),(float)(WIN_H-SB_H-14));window.draw(inf);}}

        // Status bar
        {sf::RectangleShape sb(sf::Vector2f((float)WIN_W,(float)SB_H));sb.setPosition(0,(float)(WIN_H-SB_H));sb.setFillColor({30,30,42});sb.setOutlineColor({50,50,70});sb.setOutlineThickness(1);window.draw(sb);
        if(hasFont){sf::Vector2f cp=toCanvas(rawM);
            std::string st="Tool:"+std::string(toolName(curTool))+"  Stroke:"+std::to_string((int)strokeSlider.val)+"px  Opacity:"+std::to_string((int)(opacSlider.val*100))+"%  Blend:"+bmName(blendMode)+"  Zoom:"+std::to_string((int)(zoom*100))+"%  Objs:"+std::to_string(objects.size());
            sf::Text stxt(st,font,9);stxt.setFillColor({140,140,170});stxt.setPosition(4,(float)(WIN_H-SB_H+3));window.draw(stxt);}}

        if(picker.open){sf::RectangleShape dim(sf::Vector2f((float)WIN_W,(float)WIN_H));dim.setFillColor({0,0,0,150});window.draw(dim);drawPicker(window,font,picker,strokeCol,fillCol,mouseF,mdown,mjust);}

        window.display();
    }
    return 0;
}