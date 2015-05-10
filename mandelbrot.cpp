/**\file   mandelbrot.cpp
 * \author Henry J Schmale
 * \date   May 9, 2015
 *
 * Draws a mandelbrot fractal on screen using SDL.
 */

#define NDEEBUG

#include <cstdio>
#include <cstdint>
#include <cassert>
#include <cstdlib>
#include <SDL/SDL.h>
#include <pthread.h>
#include <gflags/gflags.h>

double XMIN = -2.5; 
double XMAX = 1.0;
double YMIN = -1.0;
double YMAX = 1.0;
DEFINE_double(orgX, -.75, "x-axis center point of the image");
DEFINE_double(orgY, 0, "y-axis center point of the image");
DEFINE_int32(screen_width, 800, "The width of the screen");

#define DX (XMAX - XMIN)
#define DY (YMAX - YMIN)

const int THREADS  = 4;      //!< Concurrent threads to run
const int SCR_CD   = 32;     //!< Bits of color
const int MAX_ITER = 256;    //!< Max iterations for each point of the screen
const int FRAMES   = 1000;   //!< Frames to render before quiting

int64_t   SCR_WDTH = 0;     //!< Screen Width
int64_t   SCR_HGHT = 0;     //!< Screen Height

struct pixel{
    Uint8 r;       //!< Red componet
    Uint8 g;       //!< Green componet
    Uint8 b;       //!< Blue componet
    Uint8 alpha;   //!< Alpha componet

    pixel(){
        r     = 0;
        g     = 0;
        b     = 0;
        alpha = 255;
    }
}colorTable[MAX_ITER];

struct rendThrData{
    static uint32_t next_id;
    const uint32_t  id;
    double          xmin;
    double          xmax;
    double          ymin;
    double          ymax;
    uint64_t*       img;

    rendThrData():id(next_id++){
        img = new uint64_t[SCR_WDTH * SCR_HGHT];
    }
    ~rendThrData(){
        delete[] img;
    }

    uint64_t& operator()(int64_t x, int64_t y){
        assert((x*SCR_HGHT + y) < (SCR_WDTH*SCR_HGHT));
        return this->img[x * SCR_HGHT + y];
    }
};
uint32_t rendThrData::next_id = 0;

/** Maps a value between 2 limits to some other value between 2 other
 * limits
 */
inline double map(double x, double in_min, double in_max, 
        double out_min, double out_max){
    return (x - in_min) * (out_max - out_min) / 
        (in_max - in_min) + out_min;
}

/** Initialize the color table with values for color coding images
*/
void generateColorTable(){
    for(int i = 0; i < MAX_ITER; i++){
        colorTable[i].r = i;
        colorTable[i].g = i;
        colorTable[i].b = i;
    }
}

void put_px(SDL_Surface* scr, int x, int y, pixel* p){
    Uint32* p_screen = (Uint32*)scr->pixels;
    p_screen  += y * scr->w + x;
    *p_screen  = SDL_MapRGBA(scr->format, p->r, p->g, p->b,
            p->alpha);
}

uint64_t mandelbrot(double x0, double y0){
    int itr = 0;
    double x = 0.0;
    double y = 0.0;
    while((x*x + y*y < 4.0) && (itr < MAX_ITER)){
        double xtmp = x*x - y*y + x0;
        double ytmp = 2*x*y + y0;
        if((x == xtmp) && (y == ytmp)){
            itr = MAX_ITER;
            break;
        }
        x = xtmp;
        y = ytmp;
        itr++;
    }
    return itr;
}
void* renderThread(void *data){
    int itr = 0;
    rendThrData* d = (rendThrData*)data;
    for(int py = 0; py < SCR_HGHT; py++){
        for(int px = 0; px < SCR_WDTH; px++){
            double x0 = map(px, 0, SCR_WDTH, d->xmin, d->xmax);
            double y0 = map(py, 0, SCR_HGHT, d->ymin, d->ymax);
            (*d)(px, py) = mandelbrot(x0, y0);
        }
    }
    pthread_exit(NULL);
}

void setScale(rendThrData* d){
    // define local macros for calculating delta
#define dx (xmax-xmin)
#define dy (ymax-ymin)
    static double xmin = XMIN;
    static double xmax = XMAX;
    static double ymin = YMIN;
    static double ymax = YMAX;
    double xsca = (dx*.02)/2.0;
    double ysca = (dy*.02)/2.0;
    xmin += xsca;
    xmax -= xsca;
    ymin += ysca;
    ymax -= ysca;
    d->xmin = xmin;
    d->xmax = xmax;
    d->ymin = ymin;
    d->ymax = ymax;
    // Undefine local macros
#undef dx
#undef dy
}

int main(int argc, char*argv[]){
    pthread_t    thrds[THREADS];
    rendThrData* data;
    SDL_Surface* screen;
    int i, rc, x, y;
    
    // Handle command line args
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    assert(XMIN < XMAX);
    assert(YMIN < YMAX);
    SCR_WDTH = FLAGS_screen_width;
    SCR_HGHT = ((double)SCR_WDTH / DX) * DY;
    double dx = DX, dy = DY;
    XMIN = FLAGS_orgX - dx / 2.0;
    XMAX = FLAGS_orgX + dx / 2.0;
    YMIN = FLAGS_orgY - dy / 2.0;
    YMAX = FLAGS_orgY + dy / 2.0;
    fprintf(stderr, "WND SZ = %d by %d\n", SCR_WDTH, SCR_HGHT);

    SDL_Init(SDL_INIT_EVERYTHING); 
    generateColorTable();
    screen = SDL_SetVideoMode(SCR_WDTH, SCR_HGHT, SCR_CD, SDL_SWSURFACE);
    data   = new rendThrData[THREADS];
    // initialize threads
    for(i = 0; i < THREADS; i++){
        setScale(&data[i]);
        rc = pthread_create(&thrds[i], NULL, renderThread, (void*)&data[i]);
        if(rc){
            fprintf(stderr, "Couldn't create thread: %d\n", rc);
        }
    }
    for(i = 0; i < FRAMES; i++){
        pthread_join(thrds[i % THREADS], NULL); // Join current thread
        SDL_LockSurface(screen);
        // Draw to the screen
        for(x = 0; x < SCR_WDTH; x++){
            for(y = 0; y < SCR_HGHT; y++){
                // update pixel on screen for the data gotten from the
                // thread workload
                put_px(screen, x, y,
                        &colorTable[data[i%THREADS](x, y)% MAX_ITER]);
            }
        }
        SDL_UnlockSurface(screen);
        if(SDL_Flip(screen) == -1){
            fprintf(stderr, "SDL_Flip Failed");
            return 1;
        }
        // Recreate the thread
        setScale(&data[i%THREADS]); 
        rc = pthread_create(&thrds[i % THREADS], NULL, renderThread, 
                (void*)&data[i % THREADS]);
        if(rc){
            fprintf(stderr, "Couldn't create thread: %d\n", rc);
        }
    }
    for(i = 0; i < THREADS; i++){
        pthread_join(thrds[i], NULL);
    }
    delete[] data;
    SDL_Quit();
}
