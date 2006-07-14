/*********************************************************************************
*                             recordMyDesktop                                    *
**********************************************************************************
*                                                                                *
*             Copyright (C) 2006  John Varouhakis                                *
*                                                                                *
*                                                                                *
*    This program is free software; you can redistribute it and/or modify        *
*    it under the terms of the GNU General Public License as published by        *
*    the Free Software Foundation; either version 2 of the License, or           *
*    (at your option) any later version.                                         *
*                                                                                *
*    This program is distributed in the hope that it will be useful,             *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the               *
*    GNU General Public License for more details.                                *
*                                                                                *
*    You should have received a copy of the GNU General Public License           *
*    along with this program; if not, write to the Free Software                 *
*    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA   *
*                                                                                *
*                                                                                *
*                                                                                *
*    For further information contact me at biocrasher@gmail.com                  *
**********************************************************************************/


#ifndef RECORDMYDESKTOP_H
#define RECORDMYDESKTOP_H 1

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>  
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/XShm.h>
#include <theora/theora.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <ogg/ogg.h>
#include <alsa/asoundlib.h>


enum {UNSPECIFIED,OGG_THEORA_VORBIS};


/**Structs*/

typedef struct _DisplaySpecs{   //this struct holds some basic information
    int screen;                 //about the display,needed mostly for 
    uint width;                 //validity checks at startup
    uint height;
    Window root;
    Visual *visual;
    GC gc;
    int depth;
    unsigned long bpixel;
    unsigned long wpixel;
}DisplaySpecs;

typedef struct _WGeometry{  //basic geometry of a window or area
    int x;
    int y;
    int width;
    int height;
}WGeometry;    

typedef struct _RectArea{   //an area that has been damaged gets stored
    WGeometry geom;         //in a list comprised of structs of this type
    struct _RectArea *prev,*next;
}RectArea;

typedef struct _BRWindow{   //a window to be recorded specs
    WGeometry geom;         //window attributes
    WGeometry rgeom;        //part of window that is recorded
    int nbytes;             //size of zpixmap when screenshoting
    Window windowid;           //id
}BRWindow;


typedef struct _ProgArgs{
    int delay;          //start up delay
    Window windowid;    //window to record(default root)
    char *display;      //display to connect(default :0)
    int x,y;            //x,y offset(default 0,0)
    int width,height;   //defaults to window width and height
    int quietmode;      //no messages to stderr,stdout
    char *filename;     //output file(default out.[ogg|*])
    int encoding;       //encoding(default OGG_THEORA_VORBIS)
    int cursor_color;   //black or white=>1 or 0
    int have_dummy_cursor;//disable/enable drawing the dummy cursor
    float fps;            //desired framerate(default 15)
    unsigned int frequency;      //desired frequency (default 22050)
    unsigned int channels;       //no of channels(default 2)
    char *device;       //default sound device(default according to alsa or oss)
    int nosound;        //do not record sound(default 0)
    int noshared;       //do not use shared memory extension(default disabled)
    int full_shots;     //do not poll damage, take full screenshots
    int scshot;         //take screenshot and exit(default 0)
    int scale_shot;     //screenshot subscale factor(default 1)
    int v_bitrate,v_quality,s_quality;//video bitrate,video-sound quality
    int dropframes;     //option for theora encoder
}ProgArgs;


//this struct will hold anything related to encoding AND 
//writting out to file. 
//**TODO add vorbis specifics*/
typedef struct _EncData{
    ogg_stream_state m_ogg_ts;//theora
    ogg_stream_state m_ogg_vs;//vorbis
    ogg_page         m_ogg_pg;
    ogg_packet       m_ogg_pckt1;
    ogg_packet       m_ogg_pckt2;

    theora_state     m_th_st;
    theora_info      m_th_inf;
    theora_comment   m_th_cmmnt;
    yuv_buffer       yuv;

    vorbis_info      m_vo_inf;
    vorbis_comment   m_vo_cmmnt;
    vorbis_dsp_state m_vo_dsp; 
    vorbis_block     m_vo_block;

    int             x_offset,
                    y_offset;
    FILE            *fp;
}EncData;

typedef struct _SndBuffer{
    signed char *data;
    struct _SndBuffer *next;
}SndBuffer;

//this structure holds any data related to the program
//It's usage is mostly to be given as an argument to the 
//threads,so they will have access to the program data, avoiding 
//at the same time usage of any globals. 
typedef struct _ProgData{
    ProgArgs args;//the program arguments
    DisplaySpecs specs;//Display specific information
    BRWindow brwin;//recording window
    Display *dpy;//curtrent display
    XImage *image;//the image that holds the current full screenshot
    unsigned char *dummy_pointer;//a dummy pointer to be drawn in every frame
                                //data is casted to unsigned for later use in YUV buffer
    int dummy_p_size;//initially 16x16,always square
    unsigned char npxl;//this is the no pixel convention when drawing the dummy pointer
    char    *datamain,//the data of the image above
            *datatemp;//buffer for the temporary image,which will be 
                      //preallocated in case shared memory is not used.
    RectArea *rect_root[2];//the interchanging list roots for storing the changed regions
    int list_selector,//selector for the above
        damage_event,//damage event base code
        damage_error,//damage error base code
        running;
    SndBuffer *sound_buffer;
    EncData *enc_data;
    int hard_pause;//if sound device doesn't support pause
                    //we have to close and reopen
    unsigned int periodtime,
                frametime;
    pthread_mutex_t list_mutex[2],//mutexes for concurrency protection of the lists
                    sound_buffer_mutex,
                    yuv_mutex;
    pthread_cond_t  time_cond,//this gets a broadcast by the handler whenever it's time to get a screenshot
                    pause_cond,//this is blocks execution, when program is paused
                    sound_buffer_ready,//sound encoding finished
                    sound_data_read,//a buffer is ready for proccessing
                    image_buffer_ready;//image encoding finished
    snd_pcm_t *sound_handle;
    snd_pcm_uframes_t periodsize;
}ProgData;

/**Globals*/
//I've read somewhere that I'll go to hell for using globals...

int Paused,*Running,Aborted;
pthread_cond_t  *time_cond,*pause_cond;
int avd;

/**Macros*/

#define CLIP_EVENT_AREA(e,brwin,wgeom){\
    if(((e)->area.x<=(brwin)->rgeom.x)&&((e)->area.y<=(brwin)->rgeom.y)&&\
        ((e)->area.width>=(brwin)->rgeom.width)&&((e)->area.height<(brwin)->rgeom.height)){\
        (wgeom)->x=(brwin)->rgeom.x;\
        (wgeom)->y=(brwin)->rgeom.y;\
        (wgeom)->width=(brwin)->rgeom.width;\
        (wgeom)->height=(brwin)->rgeom.height;\
    }\
    else{\
        (wgeom)->x=((((e)->area.x+(e)->area.width>=(brwin)->rgeom.x)&&\
        ((e)->area.x<=(brwin)->rgeom.x+(brwin)->rgeom.width))?\
        (((e)->area.x<=(brwin)->rgeom.x)?(brwin)->rgeom.x:(e)->area.x):-1);\
    \
        (wgeom)->y=((((e)->area.y+(e)->area.height>=(brwin)->rgeom.y)&&\
        ((e)->area.y<=(brwin)->rgeom.y+(brwin)->rgeom.height))?\
        (((e)->area.y<=(brwin)->rgeom.y)?(brwin)->rgeom.y:(e)->area.y):-1);\
    \
        (wgeom)->width=((e)->area.x<=(brwin)->rgeom.x)?\
        (e)->area.width-((brwin)->rgeom.x-(e)->area.x):\
        ((e)->area.x<=(brwin)->rgeom.x+(brwin)->rgeom.width)?\
        (((brwin)->rgeom.width-(e)->area.x+(brwin)->rgeom.x<(e)->area.width)?\
        (brwin)->rgeom.width-(e)->area.x+(brwin)->rgeom.x:e->area.width):-1;\
    \
        (wgeom)->height=((e)->area.y<=(brwin)->rgeom.y)?\
        (e)->area.height-((brwin)->rgeom.y-(e)->area.y):\
        ((e)->area.y<=(brwin)->rgeom.y+(brwin)->rgeom.height)?\
        (((brwin)->rgeom.height-(e)->area.y+(brwin)->rgeom.y<(e)->area.height)?\
        (brwin)->rgeom.height-(e)->area.y+(brwin)->rgeom.y:(e)->area.height):-1;\
    \
        if((wgeom)->width>(brwin)->rgeom.width)(wgeom)->width=(brwin)->rgeom.width;\
        if((wgeom)->height>(brwin)->rgeom.height)(wgeom)->height=(brwin)->rgeom.height;\
    }\
}

#define CLIP_DUMMY_POINTER_AREA(dummy_p_area,brwin,wgeom){\
    (wgeom)->x=((((dummy_p_area).x+(dummy_p_area).width>=(brwin)->rgeom.x)&&\
    ((dummy_p_area).x<=(brwin)->rgeom.x+(brwin)->rgeom.width))?\
    (((dummy_p_area).x<=(brwin)->rgeom.x)?(brwin)->rgeom.x:(dummy_p_area).x):-1);\
    (wgeom)->y=((((dummy_p_area).y+(dummy_p_area).height>=(brwin)->rgeom.y)&&\
    ((dummy_p_area).y<=(brwin)->rgeom.y+(brwin)->rgeom.height))?\
    (((dummy_p_area).y<=(brwin)->rgeom.y)?(brwin)->rgeom.y:(dummy_p_area).y):-1);\
    (wgeom)->width=((dummy_p_area).x<=(brwin)->rgeom.x)?\
    (dummy_p_area).width-((brwin)->rgeom.x-(dummy_p_area).x):\
    ((dummy_p_area).x<=(brwin)->rgeom.x+(brwin)->rgeom.width)?\
    ((brwin)->rgeom.width-(dummy_p_area).x+(brwin)->rgeom.x<(dummy_p_area).width)?\
    (brwin)->rgeom.width-(dummy_p_area).x+(brwin)->rgeom.x:(dummy_p_area).width:-1;\
    (wgeom)->height=((dummy_p_area).y<=(brwin)->rgeom.y)?\
    (dummy_p_area).height-((brwin)->rgeom.y-(dummy_p_area).y):\
    ((dummy_p_area).y<=(brwin)->rgeom.y+(brwin)->rgeom.height)?\
    ((brwin)->rgeom.height-(dummy_p_area).y+(brwin)->rgeom.y<(dummy_p_area).height)?\
    (brwin)->rgeom.height-(dummy_p_area).y+(brwin)->rgeom.y:(dummy_p_area).height:-1;\
    if((wgeom)->width>(brwin)->rgeom.width)(wgeom)->width=(brwin)->rgeom.width;\
    if((wgeom)->height>(brwin)->rgeom.height)(wgeom)->height=(brwin)->rgeom.height;\
}



#define DEFAULT_ARGS(args){\
    (args)->delay=0;\
    (args)->display=(char *)malloc(strlen(getenv("DISPLAY"))+1);\
    strcpy((args)->display,getenv("DISPLAY"));\
    (args)->windowid=(args)->x=(args)->y\
    =(args)->width=(args)->height=(args)->quietmode\
    =(args)->nosound=(args)->scshot=(args)->full_shots=0;\
    (args)->noshared=(args)->scale_shot=1;\
    (args)->dropframes=0;\
    (args)->filename=(char *)malloc(8);\
    strcpy((args)->filename,"out.ogg");\
    (args)->encoding=OGG_THEORA_VORBIS;\
    (args)->cursor_color=1;\
    (args)->have_dummy_cursor=1;\
    (args)->device=(char *)malloc(8);\
    strcpy((args)->device,"hw:0,0");\
    (args)->fps=15;\
    (args)->channels=1;\
    (args)->frequency=22050;\
    (args)->v_bitrate=45000;\
    (args)->v_quality=63;\
    (args)->s_quality=10;\
}

#define QUERY_DISPLAY_SPECS(display,specstruct){\
    (specstruct)->screen=DefaultScreen(display);\
    (specstruct)->width=DisplayWidth(display,(specstruct)->screen);\
    (specstruct)->height=DisplayHeight(display,(specstruct)->screen);\
    (specstruct)->root=RootWindow(display,(specstruct)->screen);\
    (specstruct)->visual=DefaultVisual(display,(specstruct)->screen);\
    (specstruct)->gc=DefaultGC(display,(specstruct)->screen);\
    (specstruct)->depth=DefaultDepth(display,(specstruct)->screen);\
    (specstruct)->bpixel=XBlackPixel(display,(specstruct)->screen);\
    (specstruct)->wpixel=XWhitePixel(display,(specstruct)->screen);\
}

#define UPDATE_YUV_BUFFER_SH(yuv,data,x_tm,y_tm,width_tm,height_tm){\
    int i,k;\
    for(k=y_tm;k<y_tm+height_tm;k++){\
        for(i=x_tm;i<x_tm+width_tm;i++){\
            yuv->y[i+k*yuv->y_width]=min(abs(data[(i+k*yuv->y_width)*4+2] * 2104 + data[(i+k*yuv->y_width)*4+1] * 4130 + data[(i+k*yuv->y_width)*4] * 802 + 4096 + 131072) >> 13, 235);\
            if((k%2)&&(i%2)){\
                yuv->u[i/2+k/2*yuv->uv_width]=min(abs(data[(i+k*yuv->y_width)*4+2] * -1214 + data[(i+k*yuv->y_width)*4+1] * -2384 + data[(i+k*yuv->y_width)*4] * 3598 + 4096 + 1048576) >> 13, 240);\
                yuv->v[i/2+k/2*yuv->uv_width]=min(abs(data[(i+k*yuv->y_width)*4+2] * 3598 + data[(i+k*yuv->y_width)*4+1] * -3013 + data[(i+k*yuv->y_width)*4] * -585 + 4096 + 1048576) >> 13, 240);\
            }\
        }\
    }\
}

#define UPDATE_YUV_BUFFER_IM(yuv,data,x_tm,y_tm,width_tm,height_tm){\
    int i,k,j=0;\
    int x_2=x_tm/2,y_2=y_tm/2,y_width_2=yuv->y_width/2;\
    for(k=0;k<height_tm;k++){\
        for(i=0;i<width_tm;i++){\
            yuv->y[x_tm+i+(k+y_tm)*yuv->y_width]=min(abs(data[(j*4)+2] * 2104 + data[(j*4)+1] * 4130 + data[(j*4)] * 802 + 4096 + 131072) >> 13, 235);\
            if((k%2)&&(i%2)){\
                yuv->u[x_2+i/2+(k/2+y_2)*y_width_2]=min(abs(data[(k*width_tm+i)*4+2] * -1214 + data[(k*width_tm+i)*4+1] * -2384 + data[(k*width_tm+i)*4] * 3598 + 4096 + 1048576) >> 13, 240);\
                yuv->v[x_2+i/2+(k/2+y_2)*y_width_2]=min(abs(data[(k*width_tm+i)*4+2] * 3598 + data[(k*width_tm+i)*4+1] * -3013 + data[(k*width_tm+i)*4] * -585 + 4096 + 1048576) >> 13, 240);\
            }\
            \
            j++;\
        }\
    }\
}

#define DUMMY_POINTER_TO_YUV(yuv,data_tm,x_tm,y_tm,width_tm,height_tm,no_pixel){\
    int i,k,j=0;\
    int x_2=x_tm/2,y_2=y_tm/2,y_width_2=(yuv)->y_width/2;\
    for(k=0;k<height_tm;k++){\
        for(i=0;i<width_tm;i++){\
            if(data_tm[(j*4)]!=(no_pixel)){\
                (yuv)->y[x_tm+i+(k+y_tm)*(yuv)->y_width]=min(abs(data_tm[(j*4)+2] * 2104 + data_tm[(j*4)+1] * 4130 + data_tm[(j*4)] * 802 + 4096 + 131072) >> 13, 235);\
                if((k%2)&&(i%2)){\
                    yuv->u[x_2+i/2+(k/2+y_2)*y_width_2]=min(abs(data_tm[(k*width_tm+i)*4+2] * -1214 + data_tm[(k*width_tm+i)*4+1] * -2384 + data_tm[(k*width_tm+i)*4] * 3598 + 4096 + 1048576) >> 13, 240);\
                    yuv->v[x_2+i/2+(k/2+y_2)*y_width_2]=min(abs(data_tm[(k*width_tm+i)*4+2] * 3598 + data_tm[(k*width_tm+i)*4+1] * -3013 + data_tm[(k*width_tm+i)*4] * -585 + 4096 + 1048576) >> 13, 240);\
                }\
            }\
            j++;\
        }\
        j+=16-width_tm;\
    }\
}


/**Function prototypes*/

void *PollDamage(void *pdata);
void *GetFrame(void *pdata);
void *EncodeImageBuffer(void *pdata);
void *FlushToOgg(void *pdata);
void UpdateYUVBuffer(yuv_buffer *yuv,unsigned char *data,int x,int y,int width,int height);
void ClearList(RectArea **root);
int RectInsert(RectArea **root,WGeometry *wgeom);
int CollideRects(WGeometry *wgeom1,WGeometry *wgeom2,WGeometry **wgeom_return,int *ngeoms);
void SetExpired(int signum);
void RegisterCallbacks(ProgArgs *args);
void UpdateImage(Display * dpy,yuv_buffer *yuv,pthread_mutex_t *yuv_mutex,DisplaySpecs *specs,RectArea **root,BRWindow *brwin,EncData *enc,char *datatemp,int noshmem);
void XImageToYUV(XImage *imgz,yuv_buffer *yuv);
int GetZPixmap(Display *dpy,Window root,char *data,int x,int y,int width,int height);
int ParseArgs(int argc,char **argv,ProgArgs *arg_return);
int QueryExtensions(Display *dpy,ProgArgs *args,int *damage_event,int *damage_error);
int SetBRWindow(Display *dpy,BRWindow *brwin,DisplaySpecs *specs,ProgArgs *args);
int ZPixmapToBMP(XImage *imgz,BRWindow *brwin,char *fname,int nbytes,int scale);
unsigned char *MakeDummyPointer(DisplaySpecs *specs,int size,int color,int type,unsigned char *npxl);
void UpdateYUVBufferSh(yuv_buffer *yuv,unsigned char *data,int x,int y,int width,int height);
void UpdateYUVBufferIm(yuv_buffer *yuv,unsigned char *data,int x,int y,int width,int height);
void *CaptureSound(void *pdata);
void *EncodeSoundBuffer(void *pdata);
snd_pcm_t *OpenDev(const char *pcm_dev,unsigned int channels,unsigned int *frequency,snd_pcm_uframes_t *periodsize,unsigned int *periodtime,int *hardpause);
void InitEncoder(ProgData *pdata,EncData *enc_data_t);

#endif

