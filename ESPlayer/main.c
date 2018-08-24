#include "davs2.h"
#include "libswscale/swscale.h"
#include "libavutil/pixfmt.h"
#include "SDL.h"
#include "SDL_ttf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-------------------------------------------------------------------
#if _WIN32
#include <sys/types.h>
#include <sys/timeb.h>
#include <windows.h>
#pragma comment( linker, "/subsystem:windows /entry:mainCRTStartup" ) 
#else
#include <sys/time.h>
#endif
#include <time.h>

char workpath[256] = {0};

static int64_t get_time()
{
#if _WIN32
	struct timeb tb;
	ftime(&tb);
	return ((int64_t)tb.time * CLOCKS_PER_SEC + (int64_t)tb.millitm);
#else
	struct timeval tv_date;
	gettimeofday(&tv_date, NULL);
	return (int64_t)(tv_date.tv_sec * CLOCKS_PER_SEC + (int64_t)tv_date.tv_usec);
#endif
}

int get_CPU_core_num()
{
#if _WIN32
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwNumberOfProcessors;
#elif defined(LINUX) || defined(SOLARIS) || defined(AIX)
	return get_nprocs();   //GNU fuction 
#endif
}

//--------------------------------------------------------------------

#define N					64
#define R					1
#define WINDOW_WIDTH		1280 * R
#define WINDOW_HEIGHT		720 * R
#define GOP_DISPLAY_NUM		18
#define FRAME_DIAGRAM_NUM	60
#define EDGE_GAP			2
#define UI_Y				(WINDOW_HEIGHT * 3 / 4)
#define TEXT_AREA_H			(24 * R)
#define FONTSIZE			(14 * R)
#define FRAME_ARRAY_SIZE	60 * 60 * 60
#define KEY_ARRAY_SIZE		60 * 60
#define DELAY				50

SDL_bool		esplayer_quit = SDL_FALSE;

SDL_Window		*window		= NULL;//main window
SDL_Renderer	*renderer	= NULL;//main render

SDL_Texture		*v_texture	= NULL;//video texture
SDL_Rect		v_rect		= { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };//video display area

SDL_Texture		*fi_texture	= NULL;//frame info texture
SDL_Rect		fi_rect		= { EDGE_GAP, UI_Y, 
								WINDOW_WIDTH - EDGE_GAP * 2, TEXT_AREA_H };//frame info display area
SDL_Rect		fi_t_rect	= { 0, 0, 0, 0 };//frame info text display area
SDL_Rect		fi_d_rect	= { EDGE_GAP, UI_Y + TEXT_AREA_H + EDGE_GAP,
								WINDOW_WIDTH - EDGE_GAP * 2, 
								WINDOW_HEIGHT - UI_Y - TEXT_AREA_H * 2 - EDGE_GAP * 3};//frame info diagram display area

SDL_Texture		*g_texture	= NULL;//gop texture
SDL_Rect		g_rect		= { EDGE_GAP, WINDOW_HEIGHT - TEXT_AREA_H - EDGE_GAP, 
								WINDOW_WIDTH - EDGE_GAP * 2, TEXT_AREA_H };//gop display area
SDL_Surface		*g_sf_clear	= NULL;//use default color to fill the gop area

TTF_Font		*font		= NULL;//ttf font
SDL_Color		font_color	= { 0, 0, 0, 0};
SDL_Color		ui_color	= { 192,192,192,0 };

SDL_Thread		*display_thread = NULL;
SDL_Thread		*decoder_send_thread = NULL;
SDL_Thread		*decoder_recv_thread = NULL;

//---------------------------------------------------------------------------------
struct node {
	int			index;
	struct node *next;
};
typedef struct node node;

typedef struct 
{
	int			count;
	node		*head;
	node		*tail;
	SDL_mutex	*mutex;
	SDL_cond	*cond;
}linkqueue;

node * create_node(int i) 
{
	node * n = malloc(sizeof(node));
	if (n != NULL) {
		n->index = i;
		n->next = NULL;
	}
	return n;
}

void delete_node(node *n)
{
	if (n != NULL)
		free(n);
}

int linkqueue_create(linkqueue ** lq)
{
	if (lq != NULL && *lq == NULL) {
		*lq = malloc(sizeof(linkqueue));
		if (*lq != NULL) {
			(*lq)->count = 0;
			(*lq)->head = NULL;
			(*lq)->tail = NULL;
			(*lq)->mutex = SDL_CreateMutex();
			(*lq)->cond = SDL_CreateCond();
			return 1;
		}
		else {
			return 0;
		}
	}
	printf("linkqueue_create: given lq not NULL\n");
	return -1;
}

void linkqueue_destroy(linkqueue ** lq)
{
	if (lq != NULL && *lq != NULL) {
		SDL_DestroyMutex((*lq)->mutex);
		SDL_DestroyCond((*lq)->cond);
		node *cur = NULL;
		while ((*lq)->head != NULL) {
			cur = (*lq)->head;
			(*lq)->head = cur->next;
			delete_node(cur);
		}
		free(*lq);
		*lq = NULL;
	}
}

int linkqueue_put(linkqueue * lq, node *it) 
{
	int ret = -1;

	if (lq != NULL && it != NULL) {
		SDL_LockMutex(lq->mutex);
		if (lq->count == 0) {
			lq->head = it;
			lq->tail = it;
			lq->count = 1;
		}
		else if (lq->count > 0) {
			lq->tail->next = it;
			lq->tail = lq->tail->next;
			lq->count += 1;
		}
		ret = 1;
		SDL_CondSignal(lq->cond);
		SDL_UnlockMutex(lq->mutex);
	}

	return ret;
}

node * linkqueue_get(linkqueue * lq)
{
	node * ret = NULL;
	
	if (lq != NULL) {
		SDL_LockMutex(lq->mutex);
		while (lq->count == 0) {
			SDL_CondWaitTimeout(lq->cond, lq->mutex, 1000);
		}
		
		if (lq->count > 0) {
			ret = lq->head;
			lq->head = lq->head->next;
			lq->count -= 1;
			ret->next = NULL;
			if (lq->count == 0) {
				lq->tail = NULL;
				lq->head = NULL;
			}
		}
		SDL_UnlockMutex(lq->mutex);
	}
	
	return ret;
}
node * linkqueue_peek(linkqueue * lq)
{
	node * ret = NULL;
	
	if (lq != NULL) {
		SDL_LockMutex(lq->mutex);
		if (lq->count > 0) {
			ret = lq->head;
		}
		SDL_UnlockMutex(lq->mutex);
	}
	
	return ret;
}

int linkqueue_size(linkqueue * lq)
{
	return lq->count;
}

void linkqueue_show(linkqueue * lq)
{
	if (lq != NULL) {
		node *it = lq->head;
		while (it != NULL) {
			printf("%d ", it->index);
			it = it->next;
		}
		printf("\n");
	}
}
//---------------------------------------------------------------------------------
typedef struct {
	void				*decoder;	// decoder handle
	davs2_param_t		param;      // decoding parameters
	davs2_packet_t		packet;     // input bitstream
	davs2_picture_t		out_frame;  // output data, frame data
	davs2_seq_info_t	headerset;  // output data, sequence header
	int					decoded_frames;
	int					start;
}avs2decoder;

uint8_t *find_start_code(uint8_t *data, int len)
{
	while (len >= 4 && (*(int *)data & 0x00FFFFFF) != 0x00010000) {
		++data;
		--len;
	}

	return len >= 4 ? data : 0;
}

int avs2decoder_open(avs2decoder *d) 
{
	if (d != NULL) {
		d->param.info_level = DAVS2_LOG_MAX;
		d->param.threads = get_CPU_core_num();
		d->param.opaque = (void*)&(d->decoded_frames);
		d->decoder = davs2_decoder_open(&d->param);
		d->start = 0;
		if (d->decoder != NULL)
			return 1;
		else return 0;
	}
	else {
		return -1;
	}
}
int avs2decoder_close(avs2decoder *d) 
{
	if (d != NULL && d->decoder != NULL) {
		davs2_decoder_close(d->decoder);
		return 1;
	}
	else return -1;
}

void avs2decoder_start(avs2decoder *d)
{
	d->start = 1;
}
void avs2decoder_stop(avs2decoder *d)
{
	d->start = 0;
}

int avs2decoder_running(avs2decoder *d) 
{
	return d->start;
}

//---------------------------------------------------------------------------------

typedef struct {
	uint8_t *planes[3];
	int		strides[3];
	int		e_sequence;//encode/decode order num
	int		d_sequence;//display/present order num
	char	type;
}yuv_frame;

#define HEADER_NUM 8
typedef struct {
	//0xb0 0xb1 0xb2 0xb3 0xb4 0xb5 0xb6 0xb7 0x00
	uint8_t *data[HEADER_NUM];
	int		size[HEADER_NUM];
	int		count;
}avs2_frame;

void avs2_frame_printf(avs2_frame * af)
{
	printf("----------------\n");
	for (int i = 0; i < af->count; i++) {
		printf("0x%x - %d\n",af->data[i][3], af->size[i]);
	}
	printf("----------------\n");
}

typedef struct {
	char		type;		// I/ N
	avs2_frame	frame;		// encoded date
	int			size;		//size of this frame in byte
	int			sequence;	//sequence number of the video frame in the es
	yuv_frame	*out;		//decoded frame
}es_frame;

typedef struct  {
	uint8_t		*data;						//encoded stream data copied from input file
	int			size;						//data size
	es_frame	*es_frame_array;			//all frames
	int			frame_array_size;			//es_frame_array size
	int			frame_count;				//frame count of the es 
	int			*es_keyframe_index_array;	//key frame index array of es_frames_array
	int			key_array_size;				//es_keyframe_index_array size
	int			key_count;					//key frame count of the es

	int			average_duration;			//avg time consuming of frame decoding 
	int			duration;					//1000 * 1 / fps(ms)
	float       framerate;						
	int			width;						//frame origin width
	int			height;						//frame origin height
	int			frame_size_base;			//max frame size
	linkqueue	*display_q;					//prepared yuv frames queue, present in order
	linkqueue	*freebuffer_q;				//buffers are free to use

	yuv_frame	outbuffer[N];				//yuv buffers for output/scale

	int			ui_frame_index;
	int			ui_left_frame_index;
	int			ui_right_frame_index;
	int			ui_key_index;				
	int			ui_left_key_index;
	int			ui_right_key_index;

	int			state;						//0-single frame, 1-stream play
	int			state_lock;					//1-frame mode lock, 0-free
	int			pause;						//0-play, 1-pause |stop decoder

	avs2decoder decoder;		
	int			needflush;
	int			decode_index;				//sequence num of the frame/packet sending to the decoder
	int			decode_num;					//num of the frames/packets will send to the decoder this time
	int			decode_recv_num;			//num of must recv decoded frames


	yuv_frame	*cur;

	struct SwsContext *sws_ctx;
}es_player;

int ui_init(void)
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("SDL_Init: %s\n", SDL_GetError());
		return -1;
	}

	if (TTF_Init() < 0) {
		printf("TTF_Init: %s\n", TTF_GetError());
		return -1;
	}

	window = SDL_CreateWindow("ESPlayer",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		WINDOW_WIDTH, WINDOW_HEIGHT,
		SDL_WINDOW_SHOWN);
	if (!window) {
		printf("SDL_CreateWindow: %s\n", SDL_GetError());
		return -1;
	}

	renderer = SDL_CreateRenderer(window, -1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) {
		printf("SDL_CreateRenderer: %s\n", SDL_GetError());
		return -1;
	}
	char fontpath[320] = { 0 };
	sprintf(fontpath, "%s%s", workpath, "arial.ttf");
	font = TTF_OpenFont(fontpath, (int)(FONTSIZE));
	if (!font) {
		printf("TTF_OpenFont: %s\n", TTF_GetError());
		return -1;
	}

	v_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
		SDL_TEXTUREACCESS_STREAMING, WINDOW_WIDTH, WINDOW_HEIGHT);
	if (!v_texture) {
		printf("SDL_CreateTexture: %s\n", SDL_GetError());
		return -1;
	}

	fi_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, WINDOW_WIDTH - EDGE_GAP * 2, TEXT_AREA_H);
	if (!fi_texture) {
		printf("SDL_CreateTexture: %s\n", SDL_GetError());
		return -1;
	}

	g_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, WINDOW_WIDTH - EDGE_GAP * 2, TEXT_AREA_H);
	if (!g_texture) {
		printf("SDL_CreateTexture: %s\n", SDL_GetError());
		return -1;
	}
	g_sf_clear = SDL_CreateRGBSurfaceWithFormat(0, g_rect.w, g_rect.h, 8, SDL_PIXELFORMAT_ARGB8888);
	if (!g_sf_clear) {
		printf("SDL_CreateRGBSurfaceWithFormat: %s\n", SDL_GetError());
		return -1;
		SDL_FillRect(g_sf_clear, &g_sf_clear->clip_rect, 0);
	}
	
	return 1;
}
void ui_quit(void)
{
	if (fi_texture != NULL) {
		SDL_DestroyTexture(fi_texture);
	}
	if (v_texture != NULL) {
		SDL_DestroyTexture(v_texture);
	}
	if (g_texture != NULL) {
		SDL_DestroyTexture(g_texture);
	}
	if (g_sf_clear != NULL) {
		SDL_FreeSurface(g_sf_clear);
	}
	if (renderer != NULL) {
		SDL_DestroyRenderer(renderer);
	}
	if (window != NULL) {
		SDL_DestroyWindow(window);
	}

	TTF_CloseFont(font);
	TTF_Quit();
	SDL_Quit();
}

static void window_display(es_player *ep)
{
	if (ep == NULL)
		return;

	SDL_RenderClear(renderer);

	//update video iamge to the v_texture
	//---------------------------------------
	if (ep->cur != NULL) {
		SDL_UpdateYUVTexture(v_texture, NULL,
			ep->cur->planes[0], ep->cur->strides[0],
			ep->cur->planes[1], ep->cur->strides[1],
			ep->cur->planes[2], ep->cur->strides[2]);
		SDL_RenderCopy(renderer, v_texture, NULL, NULL);
	}
	
	//update frame info to fi_texture
	//---------------------------------------
	SDL_SetRenderDrawColor(renderer, ui_color.r, ui_color.g, ui_color.b, ui_color.a);
	SDL_RenderFillRect(renderer, &fi_rect);
	if (ep->cur != NULL) {
		char frame_info[256];
		memset(frame_info, 0, 256);
		es_frame * ef = ep->es_frame_array + ep->ui_frame_index;
		sprintf(frame_info, "RESOLUTION = %dx%d  |  TYPE = %c  |  DURATION = %dms  |  FPS = %.2ffps  |  AVG DECODE TIME = %dms |  SEQ = %d  |  SIZE = %d",
			ep->width, ep->height, ep->cur->type, ep->duration, ep->framerate, ep->average_duration, ef->sequence, ef->size);

		SDL_Surface *fi_surf = TTF_RenderText_Blended(font, frame_info, font_color);

		SDL_UpdateTexture(fi_texture, &fi_surf->clip_rect, fi_surf->pixels, fi_surf->pitch);
		fi_t_rect.x = fi_rect.x;
		fi_t_rect.y = fi_rect.y + (fi_rect.h - fi_surf->clip_rect.h) / 2;
		fi_t_rect.h = fi_surf->clip_rect.h;
		fi_t_rect.w = fi_surf->clip_rect.w;
		SDL_SetTextureBlendMode(fi_texture, SDL_BLENDMODE_BLEND);
		SDL_RenderCopy(renderer, fi_texture, &fi_surf->clip_rect, &fi_t_rect);
		SDL_FreeSurface(fi_surf);
	}
	//update frame info diagram
	//---------------------------------------
	SDL_RenderDrawRect(renderer, &fi_d_rect);
	SDL_Rect dig;
	char t = -1;
	int index1 = ep->ui_left_frame_index;
	if (ep->frame_size_base <= 0) {
		ep->frame_size_base = ep->size / ep->frame_count;
	}
	for (int i = 0; i < FRAME_DIAGRAM_NUM; i++) {
		if ((index1 + i) > ep->ui_right_frame_index)
			break;

		t = ep->es_frame_array[index1 + i].type;
		if (t == 'I') {
			SDL_SetRenderDrawColor(renderer, 255, 0, 0, 0);
		}
		else {
			SDL_SetRenderDrawColor(renderer, 0, 0, 255, 0);
		}
		dig.h = (fi_d_rect.h - 2 * EDGE_GAP) * ep->es_frame_array[index1 + i].size / ep->frame_size_base;
		if (dig.h > (fi_d_rect.h - 2 * EDGE_GAP))
			dig.h = fi_d_rect.h - 2 * EDGE_GAP;
		if (dig.h < EDGE_GAP)
			dig.h = EDGE_GAP;
		dig.w = (fi_d_rect.w - 2 * EDGE_GAP) / FRAME_DIAGRAM_NUM - 1;
		dig.x = fi_d_rect.x + EDGE_GAP + 1 + i*(fi_d_rect.w - 2 * EDGE_GAP) / FRAME_DIAGRAM_NUM;
		dig.y = fi_d_rect.y + EDGE_GAP + fi_d_rect.h - 2 * EDGE_GAP - dig.h;
		
		if (ep->state == 1) {
			SDL_RenderFillRect(renderer, &dig);
		}
		else if (ep->state == 0) {
			if (ep->es_frame_array[index1 + i].out != NULL) {
				SDL_RenderFillRect(renderer, &dig);
			}
			else {
				SDL_RenderDrawRect(renderer, &dig);
			}
		}

		if ((index1 + i) == ep->ui_frame_index) {
			SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
			dig.x += dig.w / 2 - EDGE_GAP;
			dig.w = 2;
			dig.y = fi_d_rect.y;
			dig.h = fi_d_rect.h;
			SDL_RenderFillRect(renderer, &dig);
		}
	}

	//update gop list to g_texture
	//---------------------------------------
	SDL_SetRenderDrawColor(renderer, ui_color.r, ui_color.g, ui_color.b, ui_color.a);
	SDL_RenderFillRect(renderer, &g_rect);
	SDL_UpdateTexture(g_texture, NULL, g_sf_clear->pixels, g_sf_clear->pitch);
	int index2 = ep->ui_left_key_index;
	if (index2 > (ep->key_count - 1)) {
		index2 = ep->key_count - 1;
	}
	SDL_Rect gr;
	SDL_Surface *g_surf = NULL;
	int gop_val = 0;
	for (int i = 0; i < GOP_DISPLAY_NUM; i++) {

		if ((index2 + i) > (ep->key_count - 1))
			break;

		if ((index2 + i) == (ep->key_count - 1)) {
			gop_val = ep->frame_count - ep->es_keyframe_index_array[index2 + i] - 1;
		}
		else {
			gop_val = ep->es_keyframe_index_array[index2 + i + 1] - ep->es_keyframe_index_array[index2 + i];
		}
		char gop[24];
		sprintf(gop, "%d/%d", index2 + i, gop_val);
		g_surf = TTF_RenderText_Blended(font, gop, font_color);
		gr.x = g_surf->clip_rect.x + i * g_rect.w / GOP_DISPLAY_NUM + (g_rect.w / GOP_DISPLAY_NUM - g_surf->clip_rect.w) / 2;
		gr.y = g_surf->clip_rect.y + (g_rect.h - g_surf->clip_rect.h) / 2;
		gr.w = g_surf->clip_rect.w;
		gr.h = g_surf->clip_rect.h;
		if ((index2 + i) == ep->ui_key_index) {
			SDL_Rect _gop;
			_gop.x = EDGE_GAP + i * g_rect.w / GOP_DISPLAY_NUM;
			_gop.y = g_rect.y;
			_gop.w = g_rect.w / GOP_DISPLAY_NUM;
			_gop.h = TEXT_AREA_H;
			if (ep->state == 0) {
				SDL_SetRenderDrawColor(renderer, 255, 255, 0, 0);
			}
			else if (ep->state == 1) {
				SDL_SetRenderDrawColor(renderer, 0, 255, 0, 0);
			}
			SDL_RenderFillRect(renderer, &_gop);
			SDL_SetRenderDrawColor(renderer, ui_color.r, ui_color.g, ui_color.b, ui_color.a);
		}
		SDL_UpdateTexture(g_texture, &gr, g_surf->pixels, g_surf->pitch);
		SDL_FreeSurface(g_surf);
	}
	SDL_SetTextureBlendMode(g_texture, SDL_BLENDMODE_BLEND);
	SDL_RenderCopy(renderer, g_texture,NULL, &g_rect);


	SDL_RenderPresent(renderer);
}

static int read_input_file(FILE *fp, es_player *ep)
{
	// get size of input file
	fseek(fp, 0, SEEK_END);
	ep->size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	// memory for stream buffer
	if ((ep->data = (uint8_t *)calloc(ep->size + 1024, sizeof(uint8_t))) == NULL) {
		perror("calloc()");
		return -1;
	}

	// read stream data
	if (fread(ep->data, ep->size, 1, fp) < 1) {
		perror("fread()");
		free(ep->data);
		ep->data = NULL;
		return -1;
	}

	return 0;
}

static int check_frame_array(es_player *ep,int count)
{
	if (ep->frame_array_size < count) {
		void * p = realloc(ep->es_frame_array, ep->frame_array_size * sizeof(es_frame) * 2);
		if (p != NULL) {
			ep->es_frame_array = (es_frame *)p;
			memset(ep->es_frame_array + count, 0, ep->frame_array_size * sizeof(es_frame));
			ep->frame_array_size *= 2;
			printf("frame_array_size %d\n", ep->frame_array_size);
		}
		else {
			perror("realloc()1");
			return -1;
		}
	}
	return ep->frame_array_size;
}
static int check_key_array(es_player *ep, int count)
{
	if (ep->key_array_size < count) {
		void *p = realloc(ep->es_keyframe_index_array, ep->key_array_size * sizeof(int) * 2);
		if (p != NULL) {
			ep->es_keyframe_index_array = (int *)p;
			memset(ep->es_keyframe_index_array + count, 0, ep->key_array_size * sizeof(int));
			ep->key_array_size *= 2;
			printf("key_array_size %d\n", ep->key_array_size);
		}
		else {
			perror("realloc()2");
			return -1;
		}
	}
	return ep->key_array_size;
}
static void make_es_frame(es_frame *ef, int type, uint8_t *offset, int len, int cnt)
{
	if (type == 0xb5 || type > 0xb7)
		return;

	if (type == 0xb3) {
		ef->type = 'I';
	}
	else if(type == 0xb6){
		ef->type = 'N';
	}
	ef->frame.data[ef->frame.count] = offset;
	ef->frame.size[ef->frame.count] = len;
	ef->frame.count++;

	ef->sequence = cnt;
	ef->size += len;
	ef->out = NULL;
}
static int parse_input_data(es_player *ep)
{
	int count = 0;
	int key_count = 0;
	uint8_t *data = ep->data;
	int size = ep->size;
	uint8_t *data_next_start_code;
	int len = 0;

	for (;;) {
		data_next_start_code = find_start_code(data + 4, size - 4);
		if (data_next_start_code) {
			len = (int)(data_next_start_code - data);
		}
		else {
			len = size;
		}
		//can not go to single frame mode
		if (data[3] == 0xb5) {
			ep->state_lock = 1;
		}
		check_frame_array(ep, count + 1);
		make_es_frame(ep->es_frame_array + count, data[3], data, len, count);
		if (data[3] == 0xb3) {
			check_key_array(ep, key_count + 1);
			ep->es_keyframe_index_array[key_count] = count;
			key_count++;
		}
		if (data[3] == 0) {
			if (len > 0) {
				if (ep->frame_size_base < ep->es_frame_array[count].size) {
					ep->frame_size_base = ep->es_frame_array[count].size;
				}
				count++;
			}
			else {
				memset(ep->es_frame_array + count,0,sizeof(es_frame));
				printf("gop %d, seq %d not a image, skip it!\n", key_count, count);
			}
		}

		size -= len;
		data += len;
		if (!len) {
			break;
		}
	}
	ep->frame_count = count;
	ep->key_count = key_count;
	//printf("frame count: %d, key count: %d\n", ep->frame_count, ep->key_count);

	ep->ui_right_frame_index = FRAME_DIAGRAM_NUM - 1;
	ep->ui_right_key_index = GOP_DISPLAY_NUM - 1;
	if (ep->ui_right_frame_index > (count - 1))
		ep->ui_right_frame_index = count - 1;
	if (ep->ui_right_key_index > (key_count - 1))
		ep->ui_right_key_index = key_count - 1;
	ep->ui_frame_index = -1;
	return count;
}

static int prepare(es_player * ep, char * f)
{
	int ret = -1;
	if (ep == NULL)
		ep = (es_player *)malloc(sizeof(es_player));
	if (ep != NULL)
		memset(ep, 0, sizeof(es_player));
	else {
		printf("prepare null ep\n");
		return -1;
	}
	ep->es_frame_array = (es_frame *)calloc(FRAME_ARRAY_SIZE , sizeof(es_frame));
	if (ep->es_frame_array == NULL) {
		printf("malloc es_frame_array failed\n");
		return -1;
	}
	ep->frame_array_size = FRAME_ARRAY_SIZE;

	ep->es_keyframe_index_array = (int *)calloc(KEY_ARRAY_SIZE , sizeof(int));
	if (ep->es_keyframe_index_array == NULL) {
		printf("malloc es_keyframe_index_array failed\n");
		return -1;
	}
	ep->key_array_size = KEY_ARRAY_SIZE;

	linkqueue_create(&(ep->display_q));
	linkqueue_create(&(ep->freebuffer_q));

	for (int i = 0; i < N; i++) {
		ep->outbuffer[i].planes[0] = malloc(WINDOW_WIDTH * WINDOW_HEIGHT);
		ep->outbuffer[i].planes[1] = malloc(WINDOW_WIDTH * WINDOW_HEIGHT / 2);
		ep->outbuffer[i].planes[2] = malloc(WINDOW_WIDTH * WINDOW_HEIGHT / 2);
		ep->outbuffer[i].strides[0] = WINDOW_WIDTH;
		ep->outbuffer[i].strides[1] = WINDOW_WIDTH / 2;
		ep->outbuffer[i].strides[2] = WINDOW_WIDTH / 2;
		
		linkqueue_put(ep->freebuffer_q,create_node(i));
	}
	//linkqueue_show(ep->freebuffer_q);

	if (f != NULL) {
		FILE * fp = fopen(f, "rb+");
		if (fp != NULL) {
			ret = read_input_file(fp, ep);
			fclose(fp);
		}
		else {
			printf("fopen error\n");
			return -1;
		}
	}
	else return -1;
	if (ret == 0)
		return parse_input_data(ep);
	else
		return ret;
}
//release the memcpy alloca by prepare
static void release(es_player * ep)
{
	if (ep->data != NULL) {
		free(ep->data);
		ep->data = NULL;
	}
	if (ep->es_frame_array != NULL) {
		free(ep->es_frame_array);
		ep->es_frame_array = NULL;
	}
	if (ep->es_keyframe_index_array != NULL) {
		free(ep->es_keyframe_index_array);
		ep->es_keyframe_index_array = NULL;
	}
	
	for (int i = 0; i < N; i++) {
		if (ep->outbuffer[i].planes[0] != NULL){
			free(ep->outbuffer[i].planes[0]);
		}
		if (ep->outbuffer[i].planes[1] != NULL) {
			free(ep->outbuffer[i].planes[1]);
		}
		if (ep->outbuffer[i].planes[2] != NULL) {
			free(ep->outbuffer[i].planes[2]);
		}
	}

	if (ep->display_q != NULL) {
		linkqueue_destroy(&(ep->display_q));
	}
	if (ep->freebuffer_q != NULL) {
		linkqueue_destroy(&(ep->freebuffer_q));
	}
	if (ep->sws_ctx != NULL) {
		sws_freeContext(ep->sws_ctx);
	}
}

//-----------------------------------------------------------------------------------

void image_prepare(es_player *ep, int width, int height, int depth, int chroma, float fps)
{
	enum AVPixelFormat src = AV_PIX_FMT_YUV420P;
	ep->width = width;
	ep->height = height;
	if (ep->sws_ctx == NULL) {
		if (chroma == 1) {
			if (depth == 10)
				src = AV_PIX_FMT_YUV420P10LE;
			else if (depth == 8)
				src = AV_PIX_FMT_YUV420P;
		}
		else if (chroma == 2) {
			if (depth == 10)
				src = AV_PIX_FMT_YUV422P10LE;
			else if (depth == 8)
				src = AV_PIX_FMT_YUV422P;
		}
		ep->sws_ctx = sws_getContext(width, height, src, WINDOW_WIDTH, WINDOW_HEIGHT,
			AV_PIX_FMT_YUV420P, SWS_POINT, NULL, NULL, NULL);
		ep->framerate = fps;
		ep->duration = (int)(1000 / fps);
	}
}

int image_scale(es_player *ep, uint8_t ** buf, int strides[], int lines[], yuv_frame *out)
{
	if (ep->sws_ctx != NULL) {
		sws_scale(ep->sws_ctx, buf, strides, 0, lines[0], out->planes, out->strides);
		return 1;
	}
	else {
		return -1;
	}
}
int avs2decoder_send_thr(void *p)
{
	es_player * ep = (es_player *)p;
	avs2decoder * d = &ep->decoder;
	int r = -1;
	
	while (!esplayer_quit) {
		if (avs2decoder_running(d) && ep->decode_index < ep->frame_count 
			&& ep->decode_num > 0 && ep->needflush == 0) {
			es_frame * ef = &(ep->es_frame_array[ep->decode_index]);
			ep->decode_index++;
			ep->decode_num--;
			for (int i = 0; i < ef->frame.count; i++) {
				d->packet.data = ef->frame.data[i];
				d->packet.len = ef->frame.size[i];
				//dts to mark the frame order
				d->packet.dts = ef->sequence;
				r = davs2_decoder_send_packet(d->decoder, &(d->packet));
				if (r == DAVS2_ERROR) {
					printf("Error: An decoder error counted\n");
					avs2_frame_printf(&ef->frame);
				}
			}
		}
		else {
			SDL_Delay(DELAY);
		}
	}
	return 1;
}
int avs2decoder_recv_thr(void *p)
{
	es_player * ep = (es_player *)p;
	avs2decoder * d = &ep->decoder;
	int seq_start = 0;
	int seq_cur = 0;
	int cnt = 0;
	int64_t t_start = 0;
	int64_t t_cur = 0;
	int r = -1;
	static char IMGTYPE[] = { 'I', 'P', 'B', 'G', 'F', 'S', '\x0' };
	while (!esplayer_quit) {
		if (avs2decoder_running(d) && ep->decode_recv_num > 0 && ep->needflush == 0) {
			r = davs2_decoder_recv_frame(d->decoder, &(d->headerset), &(d->out_frame));
			if (r == DAVS2_GOT_HEADER) {
				if (ep->sws_ctx == NULL) {
					image_prepare(ep, d->headerset.width, d->headerset.height, d->headerset.output_bit_depth,
						d->headerset.chroma_format, d->headerset.frame_rate);
				}
				davs2_decoder_frame_unref(d->decoder, &(d->out_frame));
			}
			else if (r == DAVS2_GOT_FRAME) {
				ep->decode_recv_num--;
				cnt++;
				if (t_start == 0)
					t_start = get_time();
				if (cnt == 25) {
					t_cur = get_time();
					ep->average_duration = (int)((t_cur - t_start) / cnt);
					t_start = 0;
					cnt = 0;
				}
				node * free_buf_node = linkqueue_get(ep->freebuffer_q);
				if (free_buf_node != NULL && free_buf_node->index > -1 && free_buf_node->index < N) {
					yuv_frame * out = &(ep->outbuffer[free_buf_node->index]);
					image_scale(ep, d->out_frame.planes, d->out_frame.strides, d->out_frame.lines, out);
					out->d_sequence = d->out_frame.pic_order_count;
					//dts passed by send packet, this use to mark the encoded frame with the display yuv frame
					out->e_sequence = (int)(d->out_frame.dts);
					out->type = IMGTYPE[d->out_frame.type];
					ep->es_frame_array[out->e_sequence].out = out;
					linkqueue_put(ep->display_q, free_buf_node);
					if (out->e_sequence == 0 && ep->state == 0) {
						ep->cur = out;
						ep->ui_frame_index = 0;
						window_display(ep);
					}
				}
				davs2_decoder_frame_unref(d->decoder, &(d->out_frame));
			}
			else if (r == DAVS2_DEFAULT) {
				SDL_Delay(DELAY);
			}
		}
		else {
			SDL_Delay(DELAY);
		}
	}
	return 1;
}

int display_thr(void *p)
{
	es_player * ep = (es_player *)p;

	while (!esplayer_quit) {
		if (ep->state == 1 && ep->pause == 0) {
			node * fill_buf_node = linkqueue_get(ep->display_q);
			if (fill_buf_node != NULL && fill_buf_node->index > -1 && fill_buf_node->index < N) {
				ep->cur = &(ep->outbuffer[fill_buf_node->index]);
				ep->ui_frame_index = ep->cur->e_sequence;
				if (ep->ui_frame_index > (ep->ui_left_frame_index + FRAME_DIAGRAM_NUM - 1)) {
					ep->ui_left_frame_index += FRAME_DIAGRAM_NUM;
					if (ep->ui_left_frame_index > (ep->frame_count - 1)) {
						ep->ui_left_frame_index = ep->frame_count - 1;
					}
					ep->ui_right_frame_index = ep->ui_left_frame_index + FRAME_DIAGRAM_NUM - 1;
					if (ep->ui_right_frame_index > (ep->frame_count - 1)) {
						ep->ui_right_frame_index = ep->frame_count - 1;
					}
				}
				if (ep->ui_key_index > (ep->key_count - 1)){
					ep->ui_key_index = ep->key_count - 1;
				}
				int k_s = ep->es_keyframe_index_array[ep->ui_key_index];
				if (ep->es_frame_array[k_s].sequence < ep->ui_frame_index) {
					if (ep->ui_key_index < (ep->key_count - 1)) {
						ep->ui_key_index++;
					}
					if (ep->ui_key_index > ep->ui_right_key_index) {
						ep->ui_left_key_index += GOP_DISPLAY_NUM;
						if (ep->ui_left_key_index > (ep->key_count - 1)) {
							ep->ui_left_key_index = ep->key_count - 1;
						}
						ep->ui_right_key_index = ep->ui_left_key_index + GOP_DISPLAY_NUM - 1;
						if (ep->ui_right_key_index > (ep->key_count - 1)) {
							ep->ui_right_key_index = ep->key_count - 1;
						}
					}
				}
				window_display(ep);
				
				ep->es_frame_array[ep->cur->e_sequence].out = NULL;

				linkqueue_put(ep->freebuffer_q, fill_buf_node);
				if (ep->average_duration < ep->duration || linkqueue_size(ep->display_q) > (2 * N / 3))
					SDL_Delay(ep->duration); 
				else 
					SDL_Delay(ep->average_duration);
			}
		}
		else {
			SDL_Delay(DELAY);
		}
	}
	return 1;
}
//-----------------------------------------------------------------------------------
static void player_prepare(es_player *ep)
{
	avs2decoder_open(&ep->decoder);
	decoder_send_thread = SDL_CreateThread(avs2decoder_send_thr, "DecoderThread-S", (void *)ep);
	decoder_recv_thread = SDL_CreateThread(avs2decoder_recv_thr, "DecoderThread-R", (void *)ep);
	display_thread = SDL_CreateThread(display_thr, "DisplayThread", (void *)ep);
}

static void player_reset(es_player *ep)
{
	if (decoder_send_thread != NULL) {
		SDL_WaitThread(decoder_send_thread, NULL);
	}
	if (decoder_recv_thread != NULL) {
		SDL_WaitThread(decoder_recv_thread, NULL);
	}
	if (display_thread != NULL) {
		SDL_WaitThread(display_thread, NULL);
	}
	avs2decoder_close(&ep->decoder);
}

//-----------------------------------------------------------------------------------
void es_player_info(es_player *ep)
{
	printf("------------------------------------------\n");
	printf("state = %d\n",ep->state);
	printf("ui_key_index = %d \n", ep->ui_key_index);
	printf("ui_left_key_index = %d \n", ep->ui_left_key_index);
	printf("ui_right_key_index = %d \n", ep->ui_right_key_index);
	printf("ui_frame_index = %d \n", ep->ui_frame_index);
	printf("ui_left_frame_index = %d \n", ep->ui_left_frame_index);
	printf("ui_right_frame_index = %d \n", ep->ui_right_frame_index);
	printf("decode_index = %d \n", ep->decode_index);
	printf("decode_num = %d \n", ep->decode_num);
	printf("decode_recv_num = %d \n", ep->decode_recv_num);
}

//-----------------------------------------------------------------------------------
int event_get_mode(es_player *ep)
{
	return ep->state;
}

//go to stream play mode
void event_stream_play(es_player *ep)
{
	//empty the display_qu and put the node to freebuffer_q
	ep->average_duration = ep->duration;
	node *p = NULL;
	while (linkqueue_size(ep->display_q) > 0) {
		p = linkqueue_get(ep->display_q);
		ep->es_frame_array[ep->outbuffer[p->index].e_sequence].out = NULL;
		linkqueue_put(ep->freebuffer_q, p);
	}
	ep->ui_left_frame_index = ep->es_keyframe_index_array[ep->ui_key_index];
	ep->ui_frame_index = ep->ui_left_frame_index;
	ep->ui_right_frame_index = ep->ui_left_frame_index + FRAME_DIAGRAM_NUM - 1;
	if (ep->ui_right_frame_index > (ep->frame_count - 1))
		ep->ui_right_frame_index = (ep->frame_count - 1);

	if (ep->ui_key_index < ep->ui_left_key_index || ep->ui_key_index > ep->ui_right_key_index) {
		ep->ui_left_key_index = ep->ui_key_index;
		ep->ui_right_key_index = ep->ui_left_key_index + GOP_DISPLAY_NUM - 1;
		if (ep->ui_right_key_index > (ep->key_count - 1)) {
			ep->ui_right_key_index = ep->key_count - 1;
		}
	}

	ep->decode_index = ep->ui_left_frame_index;
	ep->decode_num = ep->frame_count - 1 - ep->ui_left_frame_index;
	ep->decode_recv_num = ep->frame_count - 1 - ep->ui_left_frame_index;
	ep->state = 1;
	window_display(ep);

	//es_player_info(ep);
}

//go to single frame mode
void event_single_frame(es_player *ep)
{
	ep->state = 0;
	//empty the display_qu and put the node to freebuffer_q
	node *p = NULL;
	while (linkqueue_size(ep->display_q) > 0) {
		p = linkqueue_get(ep->display_q);
		ep->es_frame_array[ep->outbuffer[p->index].e_sequence].out = NULL;
		linkqueue_put(ep->freebuffer_q, p);
	}
	ep->ui_left_frame_index = ep->es_keyframe_index_array[ep->ui_key_index];
	ep->ui_frame_index = ep->ui_left_frame_index;

	if (ep->ui_key_index < ep->ui_left_key_index || ep->ui_key_index > ep->ui_right_key_index) {
		ep->ui_left_key_index = ep->ui_key_index;
		ep->ui_right_key_index = ep->ui_left_key_index + GOP_DISPLAY_NUM - 1;
		if (ep->ui_right_key_index > (ep->key_count - 1)) {
			ep->ui_right_key_index = ep->key_count - 1;
		}
	}
	ep->decode_index = ep->ui_left_frame_index;
	if (ep->ui_key_index >=( ep->key_count-1)) {
		ep->decode_num = ep->frame_count - ep->es_keyframe_index_array[ep->ui_key_index] - 1;
	}
	else {
		ep->decode_num = ep->es_keyframe_index_array[ep->ui_key_index + 1] - ep->es_keyframe_index_array[ep->ui_key_index];
	}
	ep->ui_right_frame_index = ep->ui_left_frame_index + ep->decode_num - 1;
	ep->decode_recv_num = ep->frame_count - 1 - ep->ui_left_frame_index;
	ep->ui_frame_index = -1;
	
	ep->decode_num += 8;

	window_display(ep);

	//es_player_info(ep);
}

void event_pointed_frame(es_player *ep, int x, int y)
{
	if (y > fi_d_rect.y && y < (fi_d_rect.y + fi_d_rect.h)) {
		int xn = ep->ui_left_frame_index + (x - EDGE_GAP)*FRAME_DIAGRAM_NUM / fi_d_rect.w;
		if (xn <= ep->ui_right_frame_index)
			ep->ui_frame_index = xn;
		if (ep->es_frame_array[ep->ui_frame_index].out != NULL) {
			ep->cur = ep->es_frame_array[ep->ui_frame_index].out;
		}
		window_display(ep);
	}
}
void event_pointed_gop(es_player *ep, int x, int y)
{
	if (y > g_rect.y && y < (g_rect.y + g_rect.h)) {
		int xn = ep->ui_left_key_index + (x - EDGE_GAP)*GOP_DISPLAY_NUM / g_rect.w;
		if (xn <= ep->ui_right_key_index)
			ep->ui_key_index = xn;
		event_single_frame(ep);
	}
}

void event_wheel_gop(es_player *ep, int flag)
{
	if (flag > 0) {
		ep->ui_left_key_index -= GOP_DISPLAY_NUM;
		if (ep->ui_left_key_index < 0) {
			ep->ui_left_key_index = 0;
		}
		ep->ui_right_key_index = ep->ui_left_key_index + GOP_DISPLAY_NUM - 1;
		if (ep->ui_right_key_index > (ep->key_count - 1)) {
			ep->ui_right_key_index = ep->key_count - 1;
		}
	}
	else {
		ep->ui_left_key_index += GOP_DISPLAY_NUM;
		if (ep->ui_left_key_index > (ep->key_count - 1)) {
			ep->ui_left_key_index = ep->key_count - 1;
		}
		ep->ui_right_key_index = ep->ui_left_key_index + GOP_DISPLAY_NUM - 1;
		if (ep->ui_right_key_index > (ep->key_count - 1)) {
			ep->ui_right_key_index = ep->key_count - 1;
		}
	}
	window_display(ep);

}
//-----------------------------------------------------------------------------------


int main(int argc, char **argv)
{
	es_player my_avs2;
	
	if (argc != 2) {
		return -1;
	}
	else {
		size_t l = strlen(argv[0]);
		if (l < 256) {
			strcpy(workpath, argv[0]);
#if _WIN32
			char *p = strrchr(workpath,'\\');
#else
			char *p = strrchr(workpath, '/');
#endif
			p[1] = 0;
		}
	}

	ui_init();

	if (prepare(&my_avs2, argv[1]) > 0) {

		player_prepare(&my_avs2);

		avs2decoder_start(&my_avs2.decoder);

		if (my_avs2.state_lock == 0)
			event_single_frame(&my_avs2);
		else
			event_stream_play(&my_avs2);
		
		window_display(&my_avs2);
		while (!esplayer_quit) {
			SDL_Event event = { -1, };
			SDL_WaitEvent(&event);
			switch (event.type) {
			case SDL_QUIT:
				esplayer_quit = SDL_TRUE;
				break;
			case SDL_TEXTINPUT:
				if (event.text.text[0] == ' ') {
					if (my_avs2.state_lock != 1) {
						if (event_get_mode(&my_avs2) == 1) {
							event_single_frame(&my_avs2);
						}
						else {
							event_stream_play(&my_avs2);
						}
					}
					else {
						if (my_avs2.pause == 0) {
							avs2decoder_stop(&my_avs2.decoder);
							my_avs2.pause = 1;
						}
						else {
							avs2decoder_start(&my_avs2.decoder);
							my_avs2.pause = 0;
						}
					}
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (event.button.clicks == 1 && event_get_mode(&my_avs2) == 0) {
					event_pointed_frame(&my_avs2, event.button.x, event.button.y);
					event_pointed_gop(&my_avs2, event.button.x, event.button.y);
				}
				break;

			case SDL_MOUSEWHEEL:
				if (event_get_mode(&my_avs2) == 0) {
					event_wheel_gop(&my_avs2, event.wheel.y);
				}
				break;
			}
		}

		avs2decoder_stop(&my_avs2.decoder);

		player_reset(&my_avs2);

	}
	release(&my_avs2);

	ui_quit();

	return 0;
}