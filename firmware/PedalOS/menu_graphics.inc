// Draft vector stand-ins until the original uploaded raster assets are
// restored. Keep these drawing details separate from navigation/hit targets.
void drawMenuInfoIcon(int x, int y, float size = 1.0f) {
  const auto s=[size](float value){return us(value*size);};
  // Match the supplied information mark: circular dot, square shoulder,
  // straight stem and a wide, flat foot.
  canvas->fillCircle(x, y-s(25), s(10), WHITE);
  canvas->fillRect(x-s(22), y-s(7), s(35), s(9), WHITE);
  canvas->fillRect(x-s(5), y+s(1), s(18), s(27), WHITE);
  canvas->fillRect(x-s(22), y+s(27), s(46), s(9), WHITE);
}

void drawMenuDraftIcon(int item, int x, int y) {
  const int w = us(2);
  if (item == 0) {
    drawMenuInfoIcon(x,y);
  } else if (item == 1) {
    // Standard Bluetooth rune: one central staff and two joined chevrons.
    const auto stroke=[x,y](int x1,int y1,int x2,int y2) {
      const int ax=x+us(x1), ay=y+us(y1);
      const int bx=x+us(x2), by=y+us(y2);
      for(int offset=-us(2);offset<=us(2);++offset)
        canvas->drawLine(ax+offset,ay,bx+offset,by,WHITE);
    };
    stroke(0,-31,0,31);
    stroke(0,-31,17,-15);
    stroke(17,-15,-17,16);
    stroke(-17,-16,17,15);
    stroke(17,15,0,31);
  } else if (item == 2) {
    const int vx[] = {-29,-11,11,29,29,11,-11,-29,-29};
    const int vy[] = {-15,-21,-15,-21,27,33,27,33,-15};
    for (int i=0;i<8;++i)
      wideLine(x+us(vx[i]),y+us(vy[i]),x+us(vx[i+1]),y+us(vy[i+1]),w,WHITE);
    wideLine(x-us(11),y-us(21),x-us(11),y+us(27),w,WHITE);
    wideLine(x+us(11),y-us(15),x+us(11),y+us(33),w,WHITE);
    canvas->fillTriangle(x-us(12),y-us(14),x+us(12),y-us(14),x,y+us(8),WHITE);
    canvas->fillCircle(x,y-us(20),us(14),WHITE);
    canvas->fillCircle(x,y-us(21),us(6),BLACK);
  } else if (item == 3) {
    for(int i=0;i<us(3);++i)
      canvas->drawRoundRect(x-us(27)+i,y-us(25)+i,us(54)-2*i,us(56)-2*i,us(3),WHITE);
    for(int i=0;i<4;++i) {
      canvas->drawCircle(x+us(-18+i*12),y-us(27),us(5),WHITE);
      canvas->drawCircle(x+us(-18+i*12),y-us(27),us(4),WHITE);
    }
    for(int i=0;i<5;++i)
      canvas->fillRoundRect(x-us(18),y+us(-12+i*8),us(i==4?24:36),us(3),us(1),WHITE);
  } else if (item == 4) {
    canvas->fillCircle(x,y,us(15),WHITE);
    for(int i=0;i<8;++i) {
      const float a=i*PI/4;
      wideLine(x+lroundf(cosf(a)*us(22)),y+lroundf(sinf(a)*us(22)),
               x+lroundf(cosf(a)*us(30)),y+lroundf(sinf(a)*us(30)),w,WHITE);
    }
  } else if (item == 5) {
    for(int r=28;r<=31;++r)canvas->drawCircle(x,y,us(r),WHITE);
    canvas->drawCircle(x,y,us(25),WHITE);
    for(int i=0;i<16;++i) {
      const float a=i*PI/8;
      canvas->drawLine(x,y,x+lroundf(cosf(a)*us(25)),y+lroundf(sinf(a)*us(25)),WHITE);
    }
    canvas->fillCircle(x,y,us(4),WHITE);
    canvas->fillCircle(x,y,us(2),BLACK);
  } else {
    wideLine(x-us(26),y+us(27),x-us(7),y-us(27),w,WHITE);
    wideLine(x+us(26),y+us(27),x+us(7),y-us(27),w,WHITE);
    for(int dy=-24;dy<26;dy+=12)canvas->fillRect(x-us(1),y+us(dy),us(3),us(7),WHITE);
  }
}

void drawGraphicalMenuPage() {
  static const char *labels[] = {"Info", "Bluetooth", "Routes", "History",
                                  "Display", "Odometer", "Radar"};
  const int page = constrain(menuSelection, 0, 3);
  for(int column=0;column<2;++column) {
    const int item=page*2+column;
    if(item>=7)break;
    const int x=ux(column ? 170 : 70);
    textCentered(labels[item],x,uy(66),4,WHITE);
    drawMenuDraftIcon(item,x,uy(117));
  }
  for(int dot=0;dot<4;++dot)
    canvas->fillCircle(ux(99+dot*14),uy(170),us(4),dot==page?GREEN:0x8410);
}
