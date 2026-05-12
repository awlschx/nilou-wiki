/******************************************************************************
 * 贪吃蛇 — EasyX 图形版
 *
 * 基于 EasyX 图形库 (https://easyx.cn) 的 Windows 桌面贪吃蛇游戏。
 * 与原控制台版本相比：
 *   - 独立窗口 (800x640)，不再依赖 cmd 黑框
 *   - 双缓冲绘图，画面流畅无闪烁
 *   - 右侧 UI 面板：实时分数、操作提示、游戏状态
 *   - 装饰性边框、圆角按钮、半透明叠加层
 *   - 保留所有原始规则：方向锁定、碰墙/碰自己即死、吃食物加长
 *
 * 编译方式（任选其一）：
 *   1. Visual Studio 2022: 新建 C++ 控制台项目 → 安装 EasyX → 编译运行
 *   2. MinGW-w64:   g++ snake_easyx.cpp -o snake.exe -leasyx -lole32
 *
 * EasyX 安装教程见文末注释与 README。
 ******************************************************************************/

#include <graphics.h>      // EasyX 核心头文件
#include <conio.h>         // _kbhit, _getch
#include <deque>           // 蛇身双端队列
#include <cstdlib>         // rand, srand
#include <ctime>           // time
#include <string>          // std::string, std::to_wstring
#include <algorithm>       // std::find
#include <thread>          // std::thread (音效异步播放)
#pragma comment(lib, "winmm.lib")  // 链接 winmm (Beep 需要)
using namespace std;

/* ===================== 游戏常量 ===================== */
const int CELL      = 28;      // 每格像素大小
const int COLS      = 20;      // 游戏区列数
const int ROWS      = 20;      // 游戏区行数
const int GAME_X    = 20;      // 游戏区左上角 X
const int GAME_Y    = 40;      // 游戏区左上角 Y
const int GAME_W    = COLS * CELL;  // 560
const int GAME_H    = ROWS * CELL;  // 560
const int PANEL_X   = GAME_X + GAME_W + 20;  // 右侧面板起始 X
const int WIN_W     = PANEL_X + 200;         // 窗口总宽 ~800
const int WIN_H     = GAME_Y + GAME_H + 20;  // 窗口总高 ~620
const int FPS       = 12;       // 游戏速度（帧/秒）
const int BASE_SCORE = 10;      // 每个食物基础得分

/* ===================== 方向枚举 ===================== */
enum Direction { UP, DOWN, LEFT, RIGHT };

/* ===================== 全局状态 ===================== */
struct Point { int x, y; };
deque<Point> snake;     // 蛇身：首=头，尾=尾
Point food;             // 食物坐标
Direction dir;          // 当前方向
int score;              // 分数
bool gameOver;          // 游戏结束标志
bool paused;            // 暂停标志
int speedDelay;         // 帧间隔（ms），值越小越快
bool soundOn;           // 音效开关

/* ===================== 音效系统 ===================== */
// 用 Beep(freq, dur) 生成 8-bit 风格音效，无需任何音频文件
// Beep 是阻塞的 → 用 std::thread 异步播放，不卡主循环

void playAsync(int freq, int ms) {
    if (!soundOn) return;
    thread([](int f, int d) { Beep(f, d); }, freq, ms).detach();
}

void sndEat() {
    // 吃食物：上行音阶 (短促愉快)
    if (!soundOn) return;
    thread([]() {
        Beep(660, 60);  Beep(880, 80);
    }).detach();
}

void sndDeath() {
    // 死亡：下行滑音 (低沉)
    if (!soundOn) return;
    thread([]() {
        Beep(440, 120); Beep(330, 150); Beep(220, 250);
    }).detach();
}

void sndPause() {
    // 暂停/继续：单音
    if (!soundOn) return;
    thread([]() { Beep(550, 80); }).detach();
}

void sndMove() {
    // 移动节拍：极短 (可选，频繁触发)
    if (!soundOn) return;
    thread([]() { Beep(200, 15); }).detach();
}

/* ===================== EasyX 颜色定义 ===================== */
#define COLOR_BG         RGB( 30,  30,  40)   // 窗口背景
#define COLOR_PANEL_BG   RGB( 40,  40,  52)   // 面板背景
#define COLOR_GAME_BG    RGB( 20,  25,  35)   // 游戏区背景
#define COLOR_GRID       RGB( 35,  40,  55)   // 网格线
#define COLOR_BORDER     RGB(100, 180, 220)   // 游戏区边框
#define COLOR_SNAKE_HEAD RGB( 80, 220, 100)   // 蛇头
#define COLOR_SNAKE_BODY RGB( 50, 180,  70)   // 蛇身
#define COLOR_SNAKE_OUT  RGB( 30, 130,  50)   // 蛇身外圈
#define COLOR_FOOD       RGB(255,  80,  80)   // 食物
#define COLOR_FOOD_GLOW  RGBA(255, 80, 80, 80)// 食物光晕（半透明）
#define COLOR_TEXT       RGB(220, 220, 230)   // 普通文字
#define COLOR_TITLE      RGB(100, 200, 240)   // 标题色（水蓝）
#define COLOR_SCORE      RGB(255, 200,  50)   // 分数字色（金色）
#define COLOR_GAMEOVER   RGB(255,  90,  90)   // Game Over 色
#define COLOR_BTN_BG     RGB( 55,  55,  70)   // 按钮背景
#define COLOR_BTN_HOVER  RGB( 75,  75,  95)   // 按钮悬停
#define COLOR_GOLD        RGB(255, 200,  50)  // 金色

/* ===================== 工具：绘制圆角矩形 ===================== */
void drawRoundRect(int x, int y, int w, int h, int r, COLORREF color,
                   bool filled = true) {
    if (filled) {
        setfillcolor(color);
        solidroundrect(x, y, x + w, y + h, r, r);
    } else {
        setlinecolor(color);
        setlinestyle(PS_SOLID, 2);
        roundrect(x, y, x + w, y + h, r, r);
    }
}

/* ===================== 工具：在矩形中居中输出文字 ===================== */
void drawTextCenter(int x, int y, int w, int h, const wchar_t* text,
                    COLORREF color, int fontSize = 20, bool bold = false) {
    settextcolor(color);
    int styleW = (bold ? 800 : 400);
    settextstyle(fontSize, 0, _T("Microsoft YaHei"), 0, 0, styleW, false, false, false);
    int tw = textwidth(text);
    int th = textheight(text);
    outtextxy(x + (w - tw) / 2, y + (h - th) / 2, text);
}

/* ===================== 绘制整个游戏画面 ===================== */
void renderAll() {
    /* ---- 清屏 ---- */
    setbkcolor(COLOR_BG);
    cleardevice();

    /* ---- 标题 ---- */
    settextcolor(COLOR_TITLE);
    settextstyle(24, 0, _T("Microsoft YaHei"), 0, 0, 800, false, false, false);
    outtextxy(GAME_X, 8, _T("🐍 贪 吃 蛇"));

    /* ---- 游戏区背景 ---- */
    drawRoundRect(GAME_X - 5, GAME_Y - 5, GAME_W + 10, GAME_H + 10,
                  12, COLOR_BORDER, false);
    setfillcolor(COLOR_GAME_BG);
    solidrectangle(GAME_X, GAME_Y, GAME_X + GAME_W, GAME_Y + GAME_H);

    /* ---- 游戏区网格 ---- */
    setlinecolor(COLOR_GRID);
    for (int i = 0; i <= COLS; i++) {
        int x = GAME_X + i * CELL;
        line(x, GAME_Y, x, GAME_Y + GAME_H);
    }
    for (int i = 0; i <= ROWS; i++) {
        int y = GAME_Y + i * CELL;
        line(GAME_X, y, GAME_X + GAME_W, y);
    }

    /* ---- 食物（带光晕） ---- */
    int fx = GAME_X + food.x * CELL + CELL / 2;
    int fy = GAME_Y + food.y * CELL + CELL / 2;
    for (int r = 14; r >= 4; r -= 4) {
        int alpha = 50 + (14 - r) * 5;
        setfillcolor(RGBA(255, 80, 80, alpha));
        solidcircle(fx, fy, r);
    }
    setfillcolor(COLOR_FOOD);
    solidcircle(fx, fy, 6);

    /* ---- 蛇身 ---- */
    for (size_t i = 0; i < snake.size(); i++) {
        int sx = GAME_X + snake[i].x * CELL;
        int sy = GAME_Y + snake[i].y * CELL;
        int pad = 1;

        if (i == 0) {
            // 蛇头：圆角矩形 + 亮色
            setfillcolor(COLOR_SNAKE_HEAD);
            solidroundrect(sx + pad, sy + pad,
                           sx + CELL - pad, sy + CELL - pad, 8, 8);
            // 眼睛
            int ex = sx + CELL / 2, ey = sy + CELL / 2;
            int edx = 0, edy = 0;
            switch (dir) {
                case UP:    edx = -4; edy = -6; break;
                case DOWN:  edx = -4; edy =  2; break;
                case LEFT:  edx = -7; edy = -4; break;
                case RIGHT: edx =  0; edy = -4; break;
            }
            setfillcolor(WHITE);
            solidcircle(ex + edx,     ey + edy,     4);
            solidcircle(ex + edx + 7, ey + edy,     4);
            setfillcolor(BLACK);
            solidcircle(ex + edx + 1, ey + edy,     2);
            solidcircle(ex + edx + 8, ey + edy,     2);
        } else {
            // 蛇身：渐变色（越靠近头部越亮）
            float ratio = (float)i / snake.size();
            int g = int(180 - ratio * 80);
            int r = int(50  - ratio * 30);
            setfillcolor(RGB(r, g, 70 - (int)(ratio * 30)));
            solidroundrect(sx + pad + 1, sy + pad + 1,
                           sx + CELL - pad - 1, sy + CELL - pad - 1, 6, 6);
        }
    }

    /* ---- 右侧面板 ---- */
    int px = PANEL_X, py = GAME_Y;
    int pw = WIN_W - px - 10, ph = GAME_H;

    // 面板背景
    drawRoundRect(px, py, pw, ph, 10, COLOR_PANEL_BG);

    // 面板标题
    drawTextCenter(px, py + 10, pw, 30, _T("— 游戏面板 —"), COLOR_TITLE, 18, false);

    // 分数
    int sy_off = py + 50;
    drawTextCenter(px, sy_off, pw, 28, _T("🏆 当前分数"), COLOR_TEXT, 16);
    wchar_t buf[32];
    _stprintf_s(buf, _T("%d"), score);
    drawTextCenter(px, sy_off + 30, pw, 44, buf, COLOR_SCORE, 36, true);

    // 食物数
    int ate = score / BASE_SCORE;
    _stprintf_s(buf, _T("已吃: %d 个"), ate);
    drawTextCenter(px, sy_off + 80, pw, 22, buf, COLOR_TEXT, 14);

    // 分隔线
    setlinecolor(RGB(70, 70, 85));
    int sepY = sy_off + 120;
    line(px + 15, sepY, px + pw - 15, sepY);

    // 操作提示
    drawTextCenter(px, sepY + 16, pw, 24, _T("🎮 操作方式"), COLOR_TEXT, 16);
    const wchar_t* tips[] = {
        _T("W/↑  向上"),
        _T("S/↓  向下"),
        _T("A/←  向左"),
        _T("D/→  向右"),
        _T("空格  暂停/继续"),
        _T("R    重新开始"),
        _T("M    音效开关"),
        _T("ESC  退出游戏"),
    };
    for (int i = 0; i < 8; i++) {
        settextcolor(RGB(180, 180, 200));
        settextstyle(13, 0, _T("Microsoft YaHei"));
        outtextxy(px + 18, sepY + 48 + i * 24, tips[i]);
    }

    // 游戏状态
    // 音效状态
    int botY = py + ph - 120;
    settextcolor(RGB(140, 140, 160));
    settextstyle(12, 0, _T("Microsoft YaHei"));
    outtextxy(px + 18, botY, soundOn ? _T("🔊 音效: 开") : _T("🔇 音效: 关"));

    botY = py + ph - 100;
    if (gameOver) {
        drawTextCenter(px, botY, pw, 30, _T("状态: 已结束"), COLOR_GAMEOVER, 18, true);
        drawTextCenter(px, botY + 36, pw, 24, _T("按 R 重新开始"), COLOR_TEXT, 14);
    } else if (paused) {
        drawTextCenter(px, botY, pw, 30, _T("状态: 暂停中"), COLOR_TITLE, 18, true);
        drawTextCenter(px, botY + 36, pw, 24, _T("按空格继续"), COLOR_TEXT, 14);
    } else {
        drawTextCenter(px, botY, pw, 30, _T("状态: 游戏中"), COLOR_SNAKE_HEAD, 18, true);
    }

    /* ---- 底部提示 ---- */
    settextcolor(RGB(100, 100, 120));
    settextstyle(12, 0, _T("Microsoft YaHei"));
    outtextxy(GAME_X, WIN_H - 22, _T("EasyX 图形版贪吃蛇 | © 2026"));

    /* ---- Game Over 半透明遮罩 ---- */
    if (gameOver) {
        setfillcolor(RGBA(0, 0, 0, 160));
        solidrectangle(GAME_X, GAME_Y, GAME_X + GAME_W, GAME_Y + GAME_H);
        drawTextCenter(GAME_X, GAME_Y + GAME_H / 2 - 30, GAME_W, 40,
                       _T("GAME OVER"), COLOR_GAMEOVER, 42, true);
        _stprintf_s(buf, _T("最终得分: %d"), score);
        drawTextCenter(GAME_X, GAME_Y + GAME_H / 2 + 20, GAME_W, 30,
                       buf, COLOR_SCORE, 22);
        drawTextCenter(GAME_X, GAME_Y + GAME_H / 2 + 55, GAME_W, 22,
                       _T("按 R 键重新开始"), COLOR_TEXT, 16);
    }

    /* ---- 暂停遮罩 ---- */
    if (paused && !gameOver) {
        setfillcolor(RGBA(0, 0, 0, 120));
        solidrectangle(GAME_X, GAME_Y, GAME_X + GAME_W, GAME_Y + GAME_H);
        drawTextCenter(GAME_X, GAME_Y + GAME_H / 2 - 15, GAME_W, 40,
                       _T("⏸  暂  停"), COLOR_TITLE, 36, true);
    }

    FlushBatchDraw();  // 一次性提交所有绘制
}

/* ===================== 生成食物 ===================== */
void spawnFood() {
    do {
        food.x = rand() % COLS;
        food.y = rand() % ROWS;
    } while (find(snake.begin(), snake.end(), food) != snake.end());
}

/* ===================== 初始化 / 重置游戏 ===================== */
void initGame() {
    snake.clear();
    // 蛇头在中间偏右，身体向左延展（初始方向 RIGHT）
    int cx = COLS / 2 + 2, cy = ROWS / 2;
    snake.push_back({ cx,     cy });  // 头
    snake.push_back({ cx - 1, cy });  // 身
    snake.push_back({ cx - 2, cy });  // 尾
    dir = RIGHT;
    score = 0;
    gameOver = false;
    paused = false;
    soundOn = true;
    speedDelay = 1000 / FPS;  // ~83ms
    srand((unsigned)time(0));
    spawnFood();
}

/* ===================== 输入处理 ===================== */
void handleInput() {
    if (!_kbhit()) return;
    int ch = _getch();

    // 方向键是两个字节 (224 + code)
    if (ch == 224) ch = _getch();

    switch (ch) {
        // ---- 方向 ----
        case 'w': case 'W': case 72:
            if (dir != DOWN)  dir = UP;    break;
        case 's': case 'S': case 80:
            if (dir != UP)    dir = DOWN;  break;
        case 'a': case 'A': case 75:
            if (dir != RIGHT) dir = LEFT;  break;
        case 'd': case 'D': case 77:
            if (dir != LEFT)  dir = RIGHT; break;
        // ---- 暂停 ----
        case ' ':  // 空格
            if (!gameOver) { paused = !paused; sndPause(); }
            break;
        // ---- 音效开关 ----
        case 'm': case 'M':
            soundOn = !soundOn;
            if (soundOn) { thread([]() { Beep(800,60); Beep(1000,60); }).detach(); }
            break;
        // ---- 重新开始 ----
        case 'r': case 'R':
            initGame();
            break;
        // ---- 退出 ----
        case 27:  // ESC
            exit(0);
    }
}

/* ===================== 游戏逻辑更新 ===================== */
void update() {
    if (gameOver || paused) return;

    // 计算新蛇头位置
    Point head = snake.front();
    switch (dir) {
        case UP:    head.y--; break;
        case DOWN:  head.y++; break;
        case LEFT:  head.x--; break;
        case RIGHT: head.x++; break;
    }

    // 碰墙检测
    if (head.x < 0 || head.x >= COLS || head.y < 0 || head.y >= ROWS) {
        gameOver = true;
        sndDeath();
        return;
    }

    // 碰自己检测
    if (find(snake.begin(), snake.end(), head) != snake.end()) {
        gameOver = true;
        sndDeath();
        return;
    }

    // 前进
    snake.push_front(head);

    // 吃食物判断
    if (head.x == food.x && head.y == food.y) {
        score += BASE_SCORE;
        sndEat();
        spawnFood();
    } else {
        snake.pop_back();  // 没吃到就去尾
    }
}

/* ===================== 主函数 ===================== */
int main() {
    // 1. 创建图形窗口
    initgraph(WIN_W, WIN_H, EW_SHOWCONSOLE);
    //   第三个参数说明：
    //     EW_SHOWCONSOLE — 同时显示控制台（调试用）
    //     EW_NOCLOSE     — 禁用关闭按钮（正式发布用）
    //     0 / EW_DEFAULT — 只显示图形窗口
    //   如果不需要调试输出，改为 initgraph(WIN_W, WIN_H);

    // 2. 设置窗口标题
    SetWindowText(GetHWnd(), _T("贪吃蛇 — EasyX 图形版"));

    // 3. 开启双缓冲（画面无闪烁）
    BeginBatchDraw();

    // 4. 初始化游戏
    initGame();

    // 5. 主循环
    while (true) {
        handleInput();   // 处理键盘输入
        update();        // 更新游戏逻辑
        renderAll();     // 绘制全部画面
        Sleep(speedDelay);  // 控制帧率
    }

    // 6. 清理（正常情况下不会到达这里，因为 ESC 调用 exit(0)）
    EndBatchDraw();
    closegraph();
    return 0;
}

/******************************************************************************
 * ======================== EasyX 安装与编译指南 ============================
 *
 * 【什么是 EasyX】
 *   EasyX 是 Windows 平台下的 C++ 图形库，封装了 GDI/GDI+，提供
 *   非常简洁的绘图 API，广泛用于国内高校 C/C++ 教学和课程设计。
 *   官网：https://easyx.cn
 *
 * 【方法一：Visual Studio 2022（推荐新手）】
 *   1. 下载 EasyX 安装包：https://easyx.cn/download/EasyX_2023大暑版.exe
 *   2. 关闭所有 VS 窗口，运行安装包
 *   3. 安装程序会自动检测 VS 版本，点击"安装"即可
 *   4. 新建 C++ 控制台项目 → 把本文件内容粘贴进去 → Ctrl+F5 运行
 *   5. 无需额外配置！EasyX 自动集成了 graphics.h 和 lib
 *
 * 【方法二：MinGW-w64（推荐有 GCC 经验的开发者）】
 *   1. 下载 EasyX for MinGW 包：
 *      https://easyx.cn/download/EasyX_MinGW_20230223.zip
 *      或从 GitHub: https://github.com/kiwirafe/easyx-for-mingw
 *   2. 解压后找到 include/ 和 lib/ 文件夹
 *   3. 把 include/ 中的 .h 文件复制到 MinGW 的 include 目录：
 *         <MinGW路径>/x86_64-w64-mingw32/include/
 *   4. 把 lib/ 中的 .a 文件复制到 MinGW 的 lib 目录：
 *         <MinGW路径>/x86_64-w64-mingw32/lib/
 *   5. 编译命令：
 *         g++ snake_easyx.cpp -o snake_easyx.exe -leasyx -lole32 -static
 *      注意：-lole32 必须放在 -leasyx 前面或后面（链接顺序）
 *
 * 【常见问题】
 *   Q: 提示 "无法打开 graphics.h"
 *   A: EasyX 没有正确安装，请重新运行安装包并确认 VS 版本被检测到。
 *
 *   Q: 窗口一闪而过
 *   A: 检查游戏循环中的 Sleep() 是否正常；如果是 VS，按 Ctrl+F5 而非 F5。
 *
 *   Q: 提示 undefined reference to `GetHWnd' 等
 *   A: 链接时缺少 -lole32，请确保编译命令包含该参数。
 *
 *   Q: 想用 VS Code 开发
 *   A: VS Code 需要手动配置 tasks.json，推荐直接用 VS 社区版（免费）。
 *
 ******************************************************************************/
