#include <M5EPD.h>
#include <time.h>
#define LGFX_M5PAPER
#include <efontEnableJa.h>
#include <efontFontData.h>
#include <LovyanGFX.hpp>

static LGFX gfx;
LGFX_Sprite sp(&gfx);

int w;
int h;
unsigned long ct;
unsigned long dt;

// タッチ座標
struct Point
{
  int x;
  int y;

  void clear()
  {
    x = y = 0;
  }
  void set(int xx, int yy)
  {
    x = xx;
    y = yy;
  }
  bool check(int xx, int yy) const
  {
    return x == xx && y == yy;
  }
  bool operator==(const Point &pt) const
  {
    return check(pt.x, pt.y);
  }
};

static constexpr const int NB_HIST = 40;
bool clearRequest;

//
struct Line
{
  Point p[2];
  bool ok;

  void set(const Point &p0, const Point &p1)
  {
    p[0] = p0;
    p[1] = p1;
    ok = true;
  }
};

Line lList[NB_HIST];
int rIndex;
int wIndex;

static constexpr const uint16_t stackSize = 4096;
static constexpr const uint32_t highV = 4200;
static constexpr const uint32_t lowV = 3650;
static constexpr const float rangeV = highV - lowV;

//
template <typename T>
void swap(T &n0, T &n1)
{
  T t = n0;
  n0 = n1;
  n1 = t;
}

// バッテリ残量
void drawBattery()
{
  int x = 465;
  gfx.drawRoundRect(x, 5, 60, 16, 4, TFT_GREEN);
  auto bv = max(min(M5.getBatteryVoltage(), highV), lowV);
  int br = (int)(((float)(bv - lowV) / rangeV) * 56.0f);
  gfx.fillRect(x + 2, 7, br, 12, TFT_GREEN);
}

// タッチイベント取得
void workJob(void *)
{
  Point p[2];
  for (auto &pp : p)
  {
    pp.clear();
  }
  //
  const TickType_t delayTime = 25 / portTICK_PERIOD_MS;
  bool enable = false;
  while (1)
  {
    if (!M5.TP.isFingerUp())
    {
      M5.TP.update();
      for (int i = 0; i < 2; i++)
      {
        auto FingerItem = M5.TP.readFinger(i);
        if (!p[i].check(FingerItem.x, FingerItem.y))
        {
          Point newP;
          newP.set(FingerItem.x, FingerItem.y);
          if (enable && i == 0)
          {
            auto &lp = lList[wIndex];
            lp.set(p[i], newP);
            wIndex = (wIndex + 1) % NB_HIST;
          }
          p[i] = newP;
          enable = true;
        }
      }
    }
    else
    {
      enable = false;
    }

    // 画面消去(Up側ボタン)
    if (M5.BtnL.isPressed())
    {
      clearRequest = true;
    }

    vTaskDelay(delayTime);
  }
}

// 初期化
void setup()
{
  M5.begin(true, true, true, true);
  gfx.init();

  gfx.setRotation(0);

  w = gfx.width();
  h = gfx.height();

  gfx.setEpdMode(epd_mode_t::epd_fast);
  //gfx.setEpdMode(epd_mode_t::epd_quality);
  gfx.setFont(&fonts::lgfxJapanGothic_32);

  M5.TP.SetRotation(90);
  ct = millis();
  dt = 0;

  clearRequest = true;
  rIndex = 0;
  wIndex = 0;

  xTaskCreatePinnedToCore(workJob, "Touch", stackSize, nullptr, 1, nullptr, 1);
}

// 点を描く
void drawPoint(int x, int y)
{
  gfx.drawPixel(x, y, TFT_BLACK);
  // アンチエイリアス風
  gfx.drawPixel(x + 1, y, TFT_DARKGREY);
  gfx.drawPixel(x - 1, y, TFT_DARKGREY);
  gfx.drawPixel(x, y + 1, TFT_DARKGREY);
  gfx.drawPixel(x, y - 1, TFT_DARKGREY);
}

// 線を描く
void drawLine(int x0, int y0, int x1, int y1)
{
  int xl = x1 - x0;
  int yl = y1 - y0;
  if (abs(xl) > abs(yl))
  {
    // 長辺がX軸
    if (xl < 0)
    {
      swap(x0, x1);
      swap(y0, y1);
      xl = -xl;
      yl = -yl;
    }

    int sy = (yl << 16) / xl;
    int yy = y0 << 16;
    for (int x = x0; x < x1; x++)
    {
      int y = yy >> 16;
      drawPoint(x, y);
      yy += sy;
    }
  }
  else
  {
    // 長辺がY軸
    if (yl < 0)
    {
      swap(x0, x1);
      swap(y0, y1);
      xl = -xl;
      yl = -yl;
    }

    int sx = (xl << 16) / yl;
    int xx = x0 << 16;
    for (int y = y0; y < y1; y++)
    {
      int x = xx >> 16;
      drawPoint(x, y);
      xx += sx;
    }
  }
}

// ループ
void loop()
{
  M5.update();

  bool update = clearRequest;
  if (!update)
  {
    unsigned long ofs = (rIndex != wIndex) ? 100 : 15 * 1000;
    if (millis() - ct > ofs)
    {
      update = true;
    }
  }

  if (update)
  {
    auto st = millis();
    gfx.startWrite();

    if (clearRequest)
    {
      gfx.clear();
      clearRequest = false;
    }
    else
    {
      char buff[32];
      snprintf(buff, sizeof(buff), "P:%d/%d %ld   ", rIndex, wIndex, dt);
      gfx.drawString(buff, 5, 5);
      for (int i = rIndex; i != wIndex; i = (i + 1) % NB_HIST)
      {
        auto &l = lList[i];
        if (!l.ok)
        {
          break;
        }
        auto &p0 = l.p[0];
        auto &p1 = l.p[1];
        drawLine(p0.x, p0.y, p1.x, p1.y);
        l.ok = false;
        rIndex = (rIndex + 1) % NB_HIST;
      }
    }
    drawBattery();
    gfx.endWrite();

    ct = millis();
    dt = ct - st;
  }
}
