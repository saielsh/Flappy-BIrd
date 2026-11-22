#include<stdio.h>
#include<stdlib.h>
#include<raylib.h>

typedef enum { MENU, GAME, DYING, OVER } State;

#define SHOW_HITBOXES 0     // set to 1 to see the hitboxes as red outlines
#define PIPE_COUNT    3     // how many pipes exist at the same time
#define PIPE_SPACING  300   // horizontal distance between two pipes

typedef struct {
    float x;
    float gapy;
    bool scored;
} Pipe;

// ------------------------------------------------------------------
// Pipe sprite: remembers the VISIBLE part of the PNG (transparent
// padding is cut away) and how tall the cap is.
// ------------------------------------------------------------------
typedef struct {
    Texture2D tex;
    Rectangle src;      // visible area of the image (no transparent borders)
    float capSrcH;      // cap height in source pixels
} PipeSprite;

// capAtBottom = true  -> top pipe    (cap is at the bottom of the image)
// capAtBottom = false -> bottom pipe (cap is at the top of the image)
PipeSprite LoadPipeSprite(const char *path, bool capAtBottom){
    PipeSprite p = {0};

    Image img = LoadImage(path);
    Color *px = LoadImageColors(img);
    int w = img.width;
    int h = img.height;

    // 1) find the bounding box of all non-transparent pixels
    int minX = w, maxX = -1, minY = h, maxY = -1;
    for(int y = 0; y < h; y++){
        for(int x = 0; x < w; x++){
            if(px[y*w + x].a > 10){
                if(x < minX) minX = x;
                if(x > maxX) maxX = x;
                if(y < minY) minY = y;
                if(y > maxY) maxY = y;
            }
        }
    }
    if(maxX < 0){ minX = 0; maxX = w - 1; minY = 0; maxY = h - 1; }   // fully transparent safety

    p.src = (Rectangle){ (float)minX, (float)minY,
                         (float)(maxX - minX + 1), (float)(maxY - minY + 1) };

    // 2) measure the visible width of every row to find where the cap ends
    int sh = (int)p.src.height;
    int *rowW = (int*)malloc(sizeof(int) * sh);
    for(int r = 0; r < sh; r++){
        int y = minY + r;
        int left = w, right = -1;
        for(int x = minX; x <= maxX; x++){
            if(px[y*w + x].a > 10){
                if(x < left) left = x;
                if(x > right) right = x;
            }
        }
        rowW[r] = (right >= left) ? (right - left + 1) : 0;
    }

    int bodyW = rowW[sh/2];
    int capRows = 0;
    if(capAtBottom){
        for(int r = sh - 1; r >= 0 && rowW[r] > bodyW + 1; r--) capRows++;
    } else {
        for(int r = 0; r < sh && rowW[r] > bodyW + 1; r++) capRows++;
    }

    // fallback if the cap could not be detected
    if(capRows < 2 || capRows > sh/2) capRows = (int)(p.src.width * 0.35f);
    p.capSrcH = (float)capRows;

    free(rowW);
    p.tex = LoadTextureFromImage(img);
    SetTextureFilter(p.tex, TEXTURE_FILTER_POINT);

    UnloadImageColors(px);
    UnloadImage(img);
    return p;
}

// Draws the pipe so it fills "dest" EXACTLY (visible pixels touch all 4 edges).
// The body stretches, the cap keeps its proportions.
void DrawPipe(PipeSprite p, Rectangle dest, bool capAtBottom){
    float scale = dest.width / p.src.width;

    float capSrcH = p.capSrcH;
    float capDestH = capSrcH * scale;
    if(capDestH > dest.height){          // pipe shorter than the cap
        capDestH = dest.height;
        capSrcH = capDestH / scale;
    }

    float bodySrcH = p.src.height - capSrcH;
    float bodyDestH = dest.height - capDestH;

    Rectangle capSrc, capDst, bodySrc, bodyDst;

    if(capAtBottom){
        bodySrc = (Rectangle){p.src.x, p.src.y, p.src.width, bodySrcH};
        bodyDst = (Rectangle){dest.x, dest.y, dest.width, bodyDestH};
        capSrc  = (Rectangle){p.src.x, p.src.y + bodySrcH, p.src.width, capSrcH};
        capDst  = (Rectangle){dest.x, dest.y + bodyDestH, dest.width, capDestH};
    } else {
        capSrc  = (Rectangle){p.src.x, p.src.y, p.src.width, capSrcH};
        capDst  = (Rectangle){dest.x, dest.y, dest.width, capDestH};
        bodySrc = (Rectangle){p.src.x, p.src.y + capSrcH, p.src.width, bodySrcH};
        bodyDst = (Rectangle){dest.x, dest.y + capDestH, dest.width, bodyDestH};
    }

    if(bodyDestH > 0) DrawTexturePro(p.tex, bodySrc, bodyDst, (Vector2){0, 0}, 0.0f, WHITE);
    DrawTexturePro(p.tex, capSrc, capDst, (Vector2){0, 0}, 0.0f, WHITE);
}

int main(){
    const int screenwidth = 800;
    const int screenheight = 600;

    // variables
    int score = 0;
    float velocity = 0;
    float jump = 6.5f;
    float speed = 2;
    float bgx = 0;
    float gravity = 0.35f;
    float rotation = 0;         // bird rotation, only used while dying
    State state = MENU;
    bool start = false;
    bool needReset = true;
    bool running = true;

    InitWindow(screenwidth, screenheight, "Flappy Bird");
    InitAudioDevice();
    SetExitKey(KEY_NULL);   // ESC no longer closes the window
    SetTargetFPS(60);

    // loading assets
    Texture2D titleimg = LoadTexture("assets/images/title.png");
    Texture2D bgimg = LoadTexture("assets/images/bg.png");
    Font fontone = LoadFont("assets/fonts/jersey 93.ttf");
    Texture2D birdimg = LoadTexture("assets/images/bird.png");
    SetTextureFilter(birdimg, TEXTURE_FILTER_POINT);

    PipeSprite toppipeimg = LoadPipeSprite("assets/images/toppipe.png", true);
    PipeSprite bottompipeimg = LoadPipeSprite("assets/images/bottompipe.png", false);

    // sounds
    Sound pointsound = LoadSound("assets/sounds/point.wav");
    Sound wingsound = LoadSound("assets/sounds/wing.wav");
    Sound diesound = LoadSound("assets/sounds/die.wav");

    // taking a single sprite frame
    int framewidth = birdimg.width / 4;
    int frameheight = birdimg.height;
    int currentframe = 0;
    float animationtimer = 0;
    float animationspeed = 0.12f;

    // player
    Rectangle bird = {50, screenheight/2, 70, 50};

    // pipes (3 at the same time)
    Pipe pipes[PIPE_COUNT];
    float pipewidth = 80;
    float pipegap = 170;
    float pipespeed = 3;

    while(running && !WindowShouldClose()){

        // ---------- controls ----------
        if(state == MENU){
            if(IsKeyPressed(KEY_ENTER)){
                state = GAME;
                needReset = true;
            }
            if(IsKeyPressed(KEY_ESCAPE)){
                running = false;    // ESC in menu quits
            }
        }
        else if(state == GAME || state == DYING){
            if(IsKeyPressed(KEY_ESCAPE)){
                state = MENU;       // ESC in game -> menu
            }
        }
        else if(state == OVER){
            if(IsKeyPressed(KEY_ENTER)){
                state = GAME;       // ENTER on game over -> play again
                needReset = true;
            }
        }

        // ---------- reset everything for a new run ----------
        if(needReset){
            bird.x = 50;
            bird.y = screenheight/2;
            velocity = 0;
            rotation = 0;
            bgx = 0;
            score = 0;
            start = false;
            currentframe = 0;
            animationtimer = 0;

            for(int i = 0; i < PIPE_COUNT; i++){
                pipes[i].x = 850 + i * PIPE_SPACING;
                pipes[i].gapy = GetRandomValue(150, 450);
                pipes[i].scored = false;
            }

            needReset = false;
        }

        // ---------- game logic ----------
        if(state == GAME){

            if(IsKeyPressed(KEY_SPACE)){
                start = true;
            }

            // main game physics
            if(start){
                bgx -= speed;

                if(bgx <= -bgimg.width){
                    bgx = 0;
                }

                bird.x += speed;

                if(bird.x >= screenwidth/2){
                    bird.x = screenwidth / 2;
                }

                if(IsKeyPressed(KEY_SPACE)){
                    velocity = -jump;
                    PlaySound(wingsound);
                }

                velocity += gravity;
                bird.y += velocity;

                for(int i = 0; i < PIPE_COUNT; i++){
                    pipes[i].x -= pipespeed;
                }
            }

            // recycle pipes that left the screen: put them behind the last pipe
            for(int i = 0; i < PIPE_COUNT; i++){
                if(pipes[i].x < -pipewidth){
                    float maxx = pipes[0].x;
                    for(int j = 1; j < PIPE_COUNT; j++){
                        if(pipes[j].x > maxx) maxx = pipes[j].x;
                    }
                    pipes[i].x = maxx + PIPE_SPACING;
                    pipes[i].gapy = GetRandomValue(150, 450);
                    pipes[i].scored = false;
                }
            }

            // scoring: a pipe is fully passed the bird
            for(int i = 0; i < PIPE_COUNT; i++){
                if(!pipes[i].scored && (pipes[i].x + pipewidth) < bird.x){
                    score++;
                    pipes[i].scored = true;
                    PlaySound(pointsound);
                }
            }

            // bird boundaries
            if(bird.y > screenheight){
                PlaySound(diesound);
                state = OVER;
                start = false;
            }
            else if(bird.y < 0){
                bird.y = 0;
            }

            // logic to play bird animation
            animationtimer += GetFrameTime();

            if(animationtimer >= animationspeed){
                currentframe++;

                if(currentframe >= 4){
                    currentframe = 0;
                }

                animationtimer = 0;
            }

            // bird hitbox (slightly smaller than the sprite)
            Rectangle birdCollision = {
                bird.x + 10,
                bird.y + 8,
                bird.width - 20,
                bird.height - 16
            };

            // collision with any of the pipes (pipe rectangles = what is drawn)
            if(state == GAME){
                for(int i = 0; i < PIPE_COUNT; i++){
                    Rectangle top = {pipes[i].x, 0, pipewidth, pipes[i].gapy - pipegap/2};
                    Rectangle bottom = {pipes[i].x, pipes[i].gapy + pipegap/2, pipewidth,
                                        screenheight - (pipes[i].gapy + pipegap/2)};

                    if(CheckCollisionRecs(birdCollision, top) ||
                       CheckCollisionRecs(birdCollision, bottom)){
                        PlaySound(diesound);
                        velocity = -3.0f;       // small hop before falling
                        start = false;
                        state = DYING;
                        break;
                    }
                }
            }
        }
        // ---------- falling animation after hitting a pipe ----------
        else if(state == DYING){
            velocity += gravity;
            bird.y += velocity;

            rotation += 6.0f;               // nose dives down
            if(rotation > 90.0f) rotation = 90.0f;

            if(bird.y > screenheight){      // fell off the screen -> game over screen
                state = OVER;
            }
        }

        // ---------- drawing ----------
        BeginDrawing();

        if(state == MENU){
            ClearBackground(SKYBLUE);

            DrawTextEx(fontone, "MENU", (Vector2){50, 200}, 50, 2, WHITE);
            DrawTextEx(fontone, "Press", (Vector2){150, 250}, 50, 2, BLACK);
            DrawTextEx(fontone, "ENTER", (Vector2){290, 250}, 50, 2, RED);
            DrawTextEx(fontone, "To Play", (Vector2){430, 250}, 50, 2, BLACK);

            DrawTexturePro(titleimg,
                (Rectangle){0, 0, titleimg.width, titleimg.height},
                (Rectangle){(screenwidth/2) - 120, 10, 310, 180},
                (Vector2){0, 0}, 0.0f,
                WHITE);
        }
        else if(state == GAME || state == DYING){
            ClearBackground(BLACK);

            DrawTexture(bgimg, bgx, 0, WHITE);
            DrawTexture(bgimg, bgx + bgimg.width, 0, WHITE);

            DrawTextEx(fontone, "Developed by SAILESH DAHAL",
                (Vector2){10, 580}, 20, 1, BLACK);

            DrawTextEx(fontone, TextFormat("Score: %d", score),
                (Vector2){10, 10}, 50, 2, BLACK);

            // drawing pipes: they fill the hitbox rectangles exactly
            for(int i = 0; i < PIPE_COUNT; i++){
                Rectangle top = {pipes[i].x, 0, pipewidth, pipes[i].gapy - pipegap/2};
                Rectangle bottom = {pipes[i].x, pipes[i].gapy + pipegap/2, pipewidth,
                                    screenheight - (pipes[i].gapy + pipegap/2)};

                DrawPipe(toppipeimg, top, true);
                DrawPipe(bottompipeimg, bottom, false);

#if SHOW_HITBOXES
                DrawRectangleLinesEx(top, 2, RED);
                DrawRectangleLinesEx(bottom, 2, RED);
#endif
            }

            // drawing bird (rotates around its center while dying)
            DrawTexturePro(birdimg,
                (Rectangle){currentframe * framewidth, 0, framewidth, frameheight},
                (Rectangle){bird.x + bird.width/2, bird.y + bird.height/2, bird.width, bird.height},
                (Vector2){bird.width/2, bird.height/2}, rotation, WHITE);

#if SHOW_HITBOXES
            DrawRectangleLinesEx((Rectangle){bird.x + 10, bird.y + 8, bird.width - 20, bird.height - 16}, 2, RED);
#endif
        }
        else if(state == OVER){
            ClearBackground(BLACK);

            DrawTextEx(fontone, TextFormat("SCORE : %d", score),
                (Vector2){100, 100}, 50, 2, WHITE);
            
            if(score <= 10){
                DrawTextEx(fontone, "LOOSER", (Vector2){100, 400}, 50, 2, RED);
            }else if(score > 10 && score <= 20){
                DrawTextEx(fontone, "GOOD", (Vector2){100, 400}, 50, 2, SKYBLUE);
            }else if(score > 20){
                DrawTextEx(fontone, "GERAT", (Vector2){100, 400}, 50, 2, YELLOW);
            }

            DrawTextEx(fontone, "Hit ENTER To Play Again",
                (Vector2){200, 300}, 50, 2, WHITE);
        }

        EndDrawing();
    }

    // unloading assets
    UnloadTexture(titleimg);
    UnloadTexture(bgimg);
    UnloadTexture(birdimg);
    UnloadTexture(toppipeimg.tex);
    UnloadTexture(bottompipeimg.tex);
    UnloadFont(fontone);
    UnloadSound(pointsound);
    UnloadSound(wingsound);
    UnloadSound(diesound);

    CloseAudioDevice();
    CloseWindow();

    return 0;
}